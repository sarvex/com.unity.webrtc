#pragma once

#include <cuda.h>

#include <common_video/h264/h264_bitstream_parser.h>
#include <common_video/include/video_frame_buffer_pool.h>

#include "NvCodec.h"
#include "NvCodecUtils.h"
#include "NvDecoder/NvDecoder.h"
#include "VideoFrameAdapter.h"
#include "VideoFrameBufferPool.h"

using namespace webrtc;

namespace unity
{
namespace webrtc
{

    using NvDecoderInternal = ::NvDecoder;

    class UnityProfilerMarkerDesc;
    class H264BitstreamParser : public ::webrtc::H264BitstreamParser
    {
    public:
        absl::optional<SpsParser::SpsState> sps() { return sps_; }
        absl::optional<PpsParser::PpsState> pps() { return pps_; }
    };

    class ProfilerMarkerFactory;
    class NvDecoderImpl : public unity::webrtc::NvDecoder
    {
    public:
        NvDecoderImpl(CUcontext context, ProfilerMarkerFactory* profiler, IGraphicsDevice* device);
        NvDecoderImpl(const NvDecoderImpl&) = delete;
        NvDecoderImpl& operator=(const NvDecoderImpl&) = delete;
        ~NvDecoderImpl() override;

        virtual int32_t InitDecode(const VideoCodec* codec_settings, int32_t number_of_cores) override;
        virtual int32_t Decode(const EncodedImage& input_image, bool missing_frames, int64_t render_time_ms) override;
        virtual int32_t RegisterDecodeCompleteCallback(DecodedImageCallback* callback) override;
        virtual int32_t Release() override;
        virtual DecoderInfo GetDecoderInfo() const override;

    private:
        CUcontext m_context;
        CUdeviceptr m_dpFrame;
        std::unique_ptr<NvDecoderInternal> m_decoder;
        bool m_isConfiguredDecoder;

        VideoCodec m_codec;

        DecodedImageCallback* m_decodedCompleteCallback = nullptr;
        VideoFrameBufferPool m_bufferPool;
        H264BitstreamParser m_h264_bitstream_parser;

        ProfilerMarkerFactory* m_profiler;
        const UnityProfilerMarkerDesc* m_marker;

        const size_t kMaxNumberOfBuffers = 10;
    };

} // end namespace webrtc
} // end namespace unity
