#pragma once

#include <api/video_codecs/h264_profile_level_id.h>
#include <api/video_codecs/sdp_video_format.h>
#include <api/video_codecs/video_decoder.h>
#include <api/video_codecs/video_decoder_factory.h>
#include <api/video_codecs/video_encoder.h>
#include <api/video_codecs/video_encoder_factory.h>
#include <media/base/codec.h>

namespace unity
{
namespace webrtc
{
    using namespace ::webrtc;

    const char VIDEO_H264[] = "video/avc";
    //    const char VIDEO_VP8[] = "video/x-vnd.on2.vp8";
    //    const char VIDEO_VP9[] = "video/x-vnd.on2.vp9";
    //    const char VIDEO_AV1[] = "video/av01";

    // https://developer.android.com/reference/android/media/MediaCodecInfo.CodecCapabilities
    //    const int ColorFormatYUV420Planar = 0x00000013;
    //const int ColorFormatYUV420SemiPlanar = 0x00000015;
    const int COLOR_Format32bitBGRA8888 = 0x0000000f;
    const int ColorFormatSurface = 0x7f000789;
    const int ColorFormatPrivate = 34;

    const int keyFrameIntervalSec = 5;
    const int VIDEO_ControlRateConstant = 2;
    const int VIDEO_AVC_PROFILE_HIGH = 8;
    const int VIDEO_AVC_LEVEL_3 = 0x100;
    const int kDequeueOutputBufferTimeoutMicrosecond = 100000;

    class IGraphicsDevice;
    class ProfilerMarkerFactory;
    class MediaCodecEncoder : public VideoEncoder
    {
    public:
        static std::unique_ptr<MediaCodecEncoder> Create(
            const cricket::VideoCodec& codec,
            IGraphicsDevice* device,
            ProfilerMarkerFactory* profiler);
    };

    class MediaCodecDecoder : public VideoDecoder
    {
    public:
        static std::unique_ptr<MediaCodecDecoder> Create(
            const cricket::VideoCodec& codec,
            IGraphicsDevice* device,
            ProfilerMarkerFactory* profiler);
    };

    class MedicCodecEncoderFactory : public VideoEncoderFactory
    {
    public:
        MedicCodecEncoderFactory(ProfilerMarkerFactory* profiler);
        ~MedicCodecEncoderFactory() override;

        std::vector<SdpVideoFormat> GetSupportedFormats() const override;
        VideoEncoderFactory::CodecInfo QueryVideoEncoder(const SdpVideoFormat& format) const override;
        std::unique_ptr<VideoEncoder> CreateVideoEncoder(const SdpVideoFormat& format) override;

    private:
        ProfilerMarkerFactory* profiler_;

        // Cache of capability to reduce calling SessionOpenAPI of NvEncoder
        std::vector<SdpVideoFormat> m_cachedSupportedFormats;
    };

    class MedicCodecDecoderFactory : public VideoDecoderFactory
    {
    public:
        MedicCodecDecoderFactory(ProfilerMarkerFactory* profiler);
        ~MedicCodecDecoderFactory() override;

        std::vector<webrtc::SdpVideoFormat> GetSupportedFormats() const override;
        std::unique_ptr<webrtc::VideoDecoder> CreateVideoDecoder(const webrtc::SdpVideoFormat& format) override;

    private:
        ProfilerMarkerFactory* profiler_;
    };
}
}