#include "pch.h"

#include <api/video/i420_buffer.h>
#include <api/video/video_codec_type.h>
#include <modules/video_coding/include/video_error_codes.h>
#include <third_party/libyuv/include/libyuv/convert.h>

#include "ColorSpace.h"
#include "NvCodecUtils.h"
#include "NvDecoder/NvDecoder.h"
#include "NvDecoderImpl.h"
#include "ProfilerMarkerFactory.h"
#include "ScopedProfiler.h"
#include "NativeFrameBuffer.h"
#include "GraphicsDevice/IGraphicsDevice.h"
#include "GraphicsDevice/Cuda/GpuMemoryBufferCudaHandle.h"

namespace unity
{
namespace webrtc
{
    using namespace ::webrtc;

    ColorSpace ExtractH264ColorSpace(const CUVIDEOFORMAT& format)
    {
        return ColorSpace(
            static_cast<ColorSpace::PrimaryID>(format.video_signal_description.color_primaries),
            static_cast<ColorSpace::TransferID>(format.video_signal_description.transfer_characteristics),
            static_cast<ColorSpace::MatrixID>(format.video_signal_description.matrix_coefficients),
            static_cast<ColorSpace::RangeID>(format.video_signal_description.video_full_range_flag));
    }

    NvDecoderImpl::NvDecoderImpl(CUcontext context, ProfilerMarkerFactory* profiler, IGraphicsDevice* device)
        : m_context(context)
        , m_decoder(nullptr)
        , m_dpFrame(0)
        , m_isConfiguredDecoder(false)
        , m_decodedCompleteCallback(nullptr)
        , m_buffer_pool(device, Clock::GetRealTimeClock()->GetRealTimeClock())
        , m_profiler(profiler)
    {
        // if (profiler)
        //    m_marker = profiler->CreateMarker(
        //        "NvDecoderImpl.ConvertNV12ToI420", kUnityProfilerCategoryOther, kUnityProfilerMarkerFlagDefault, 0);
    }

    NvDecoderImpl::~NvDecoderImpl() { Release(); }

    VideoDecoder::DecoderInfo NvDecoderImpl::GetDecoderInfo() const
    {
        VideoDecoder::DecoderInfo info;
        info.implementation_name = "NvCodec";
        info.is_hardware_accelerated = true;
        return info;
    }

    int NvDecoderImpl::InitDecode(const VideoCodec* codec_settings, int32_t number_of_cores)
    {
        if (codec_settings == nullptr)
        {
            RTC_LOG(LS_ERROR) << "initialization failed on codec_settings is null ";
            return WEBRTC_VIDEO_CODEC_ERR_PARAMETER;
        }

        if (codec_settings->codecType != kVideoCodecH264)
        {
            RTC_LOG(LS_ERROR) << "initialization failed on codectype is not kVideoCodecH264";
            return WEBRTC_VIDEO_CODEC_ERR_PARAMETER;
        }
        if (codec_settings->width < 1 || codec_settings->height < 1)
        {
            RTC_LOG(LS_ERROR) << "initialization failed on codec_settings width < 0 or height < 0";
            return WEBRTC_VIDEO_CODEC_ERR_PARAMETER;
        }

        m_codec = *codec_settings;

        const CUresult result = cuCtxSetCurrent(m_context);
        if (!ck(result))
        {
            RTC_LOG(LS_ERROR) << "initialization failed on cuCtxSetCurrent result" << result;
            return WEBRTC_VIDEO_CODEC_ENCODER_FAILURE;
        }

        // todo(kazuki): Max resolution is differred each architecture.
        // Refer to the table in Video Decoder Capabilities.
        // https://docs.nvidia.com/video-technologies/video-codec-sdk/nvdec-video-decoder-api-prog-guide
        const int maxWidth = 4096;
        const int maxHeight = 4096;

        // bUseDeviceFrame: allocate in memory or cuda device memory
        m_decoder = std::make_unique<NvDecoderInternal>(
            m_context, true, cudaVideoCodec_H264, true, false, nullptr, nullptr, maxWidth, maxHeight);
        return WEBRTC_VIDEO_CODEC_OK;
    }

    int32_t NvDecoderImpl::RegisterDecodeCompleteCallback(DecodedImageCallback* callback)
    {
        this->m_decodedCompleteCallback = callback;
        return WEBRTC_VIDEO_CODEC_OK;
    }

    int32_t NvDecoderImpl::Release()
    {
        if (m_dpFrame != 0)
        {
            CUresult result = cuMemFree(m_dpFrame);
            m_dpFrame = 0;
            if (result != CUDA_SUCCESS)
            {
                RTC_LOG(LS_INFO) << "cuMemFree failed";
            }
        }
        return WEBRTC_VIDEO_CODEC_OK;
    }


    CUresult AllocDeviceMemory(CUcontext cuContext, CUdeviceptr& ptr, size_t size)
    {
        CUresult result;
        result = cuCtxPushCurrent(cuContext);
        if (result != CUDA_SUCCESS)
        {
            RTC_LOG(LS_INFO) << "cuCtxPushCurrent failed. result=" << result;
            return result;
        }

        if (ptr != 0)
        {
            result = cuMemFree(ptr);
            if (result != CUDA_SUCCESS)
            {
                RTC_LOG(LS_INFO) << "cuMemFree failed. result=" << result;
                return result;
            }
        }

        result = cuMemAlloc(&ptr, size);
        if (result != CUDA_SUCCESS)
        {
            RTC_LOG(LS_INFO) << "cuMemAlloc failed. result=" << result;
            return result;
        }
        result = cuCtxPopCurrent(NULL);
        if (result != CUDA_SUCCESS)
        {
            RTC_LOG(LS_INFO) << "cuCtxPopCurrent failed. result=" << result;
            return result;
        }
        return result;
    }

    CUresult
    CopyDeviceFrame(CUcontext cuContext, CUarray dstArray, CUdeviceptr dpBgra, int nWidth, int nHeight, int nPitch)
    {
        CUresult result;
        result = cuCtxPushCurrent(cuContext);
        if (result != CUDA_SUCCESS)
        {
            RTC_LOG(LS_INFO) << "cuCtxPushCurrent failed. result=" << result;
            return result;
        }
        CUDA_MEMCPY2D m = { 0 };
        m.srcMemoryType = CU_MEMORYTYPE_DEVICE;
        m.srcDevice = dpBgra;
        m.srcPitch = nPitch ? nPitch : nWidth * 4;
        m.dstMemoryType = CU_MEMORYTYPE_ARRAY;
        m.dstArray = dstArray;
        m.WidthInBytes = nWidth * 4;
        m.Height = nHeight;
        result = cuMemcpy2D(&m);
        if (result != CUDA_SUCCESS)
        {
            RTC_LOG(LS_INFO) << "cuMemcpy2D failed. result=" << result;
            return result;
        }

        result = cuCtxPopCurrent(NULL);
        if (result != CUDA_SUCCESS)
        {
            RTC_LOG(LS_INFO) << "cuCtxPopCurrent failed. result=" << result;
            return result;
        }
        return result;
    }

    int32_t NvDecoderImpl::Decode(const EncodedImage& input_image, bool missing_frames, int64_t render_time_ms)
    {
        CUcontext current;
        if (!ck(cuCtxGetCurrent(&current)))
        {
            RTC_LOG(LS_ERROR) << "decode failed on cuCtxGetCurrent is failed";
            return WEBRTC_VIDEO_CODEC_UNINITIALIZED;
        }
        if (current != m_context)
        {
            RTC_LOG(LS_ERROR) << "decode failed on not match current context and hold context";
            return WEBRTC_VIDEO_CODEC_UNINITIALIZED;
        }
        if (m_decodedCompleteCallback == nullptr)
        {
            RTC_LOG(LS_ERROR) << "decode failed on not set m_decodedCompleteCallback";
            return WEBRTC_VIDEO_CODEC_UNINITIALIZED;
        }
        if (!input_image.data() || !input_image.size())
        {
            RTC_LOG(LS_ERROR) << "decode failed on input image is null";
            return WEBRTC_VIDEO_CODEC_ERR_PARAMETER;
        }

        m_h264_bitstream_parser.ParseBitstream(input_image);
        absl::optional<int> qp = m_h264_bitstream_parser.GetLastSliceQp();
        absl::optional<SpsParser::SpsState> sps = m_h264_bitstream_parser.sps();

        if (m_isConfiguredDecoder)
        {
            if (!sps || sps.value().width != static_cast<uint32_t>(m_decoder->GetWidth()) ||
                sps.value().height != static_cast<uint32_t>(m_decoder->GetHeight()))
            {
                m_decoder->setReconfigParams(nullptr, nullptr);
                CUresult result = AllocDeviceMemory(current, m_dpFrame, sps.value().width * sps.value().height * 4);
                if (result != CUDA_SUCCESS)
                    return WEBRTC_VIDEO_CODEC_ERR_PARAMETER;
            }
        }
        else
        {
            CUresult result = AllocDeviceMemory(current, m_dpFrame, sps.value().width * sps.value().height * 4);
            if (result != CUDA_SUCCESS)
                return WEBRTC_VIDEO_CODEC_ERR_PARAMETER;
        }

        int nFrameReturnd = 0;
        do
        {
            nFrameReturnd = m_decoder->Decode(
                input_image.data(), static_cast<int>(input_image.size()), CUVID_PKT_TIMESTAMP, input_image.Timestamp());
        } while (nFrameReturnd == 0);

        m_isConfiguredDecoder = true;

        // todo: support other output format
        // Chromium's H264 Encoder is output on NV12, so currently only NV12 is supported.
        if (m_decoder->GetOutputFormat() != cudaVideoSurfaceFormat_NV12)
        {
            RTC_LOG(LS_ERROR) << "not supported this format: " << m_decoder->GetOutputFormat();
            return WEBRTC_VIDEO_CODEC_ERR_PARAMETER;
        }

        // Pass on color space from input frame if explicitly specified.
        const ColorSpace& color_space = input_image.ColorSpace()
            ? *input_image.ColorSpace()
            : ExtractH264ColorSpace(m_decoder->GetVideoFormatInfo());

        int width = static_cast<int>(input_image._encodedWidth);
        int nRGBWidth = (width + 1) & ~1;

        for (int i = 0; i < nFrameReturnd; i++)
        {
            int64_t timeStamp;
            uint8_t* pFrame = m_decoder->GetFrame(&timeStamp);
            UnityRenderingExtTextureFormat format = kUnityRenderingExtFormatB8G8R8A8_UNorm;
            int iMatrix = m_decoder->GetVideoFormatInfo().video_signal_description.matrix_coefficients;
            if (m_decoder->GetBitDepth() == 8)
            {
                if (m_decoder->GetOutputFormat() == cudaVideoSurfaceFormat_YUV444)
                    YUV444ToColor32<BGRA32>(
                        pFrame,
                        m_decoder->GetWidth(),
                        reinterpret_cast<uint8_t*>(m_dpFrame),
                        4 * nRGBWidth,
                        m_decoder->GetWidth(),
                        m_decoder->GetHeight(),
                        iMatrix);
                else // default assumed as NV12
                    Nv12ToColor32<BGRA32>(
                        pFrame,
                        m_decoder->GetWidth(),
                        reinterpret_cast<uint8_t*>(m_dpFrame),
                        4 * nRGBWidth,
                        m_decoder->GetWidth(),
                        m_decoder->GetHeight(),
                        iMatrix);
            }
            else
            {
                if (m_decoder->GetOutputFormat() == cudaVideoSurfaceFormat_YUV444_16Bit)
                    YUV444P16ToColor32<BGRA32>(
                        pFrame,
                        2 * m_decoder->GetWidth(),
                        reinterpret_cast<uint8_t*>(m_dpFrame),
                        4 * nRGBWidth,
                        m_decoder->GetWidth(),
                        m_decoder->GetHeight(),
                        iMatrix);
                else // default assumed as P016
                    P016ToColor32<BGRA32>(
                        pFrame,
                        2 * m_decoder->GetWidth(),
                        reinterpret_cast<uint8_t*>(m_dpFrame),
                        4 * nRGBWidth,
                        m_decoder->GetWidth(),
                        m_decoder->GetHeight(),
                        iMatrix);
            }

            rtc::scoped_refptr<VideoFrameBuffer> buffer =
                m_buffer_pool.Create(m_decoder->GetWidth(), m_decoder->GetHeight(), format);
            NativeFrameBuffer* nativeFrameBuffer = static_cast<NativeFrameBuffer*>(buffer.get());

            auto handle = nativeFrameBuffer->handle();
            if (!handle)
            {
                nativeFrameBuffer->Map(GpuMemoryBufferHandle::AccessMode::kWrite);
                handle = nativeFrameBuffer->handle();
            }
            const GpuMemoryBufferCudaHandle* cudaHandle =
                static_cast<const GpuMemoryBufferCudaHandle*>(handle);
            CUresult result = CopyDeviceFrame(
                current, cudaHandle->mappedArray, m_dpFrame, m_decoder->GetWidth(), m_decoder->GetHeight(), 0);
            if (result != CUDA_SUCCESS)
            {
                RTC_LOG(LS_INFO) << "CopyDeviceFrame failed. result=" << result;
                return WEBRTC_VIDEO_CODEC_ERR_PARAMETER;
            }

            ::webrtc::VideoFrame decoded_frame = ::webrtc::VideoFrame::Builder()
                                                     .set_video_frame_buffer(buffer)
                                                     .set_timestamp_rtp(static_cast<uint32_t>(timeStamp))
                                                     .set_color_space(color_space)
                                                     .build();

            // todo: measurement decoding time
            absl::optional<int32_t> decodetime;
            m_decodedCompleteCallback->Decoded(decoded_frame, decodetime, qp);
        }

        return WEBRTC_VIDEO_CODEC_OK;
    }

} // end namespace webrtc
} // end namespace unity
