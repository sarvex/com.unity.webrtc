#pragma once

#include <d3d11.h>
#include <memory>
#include <wrl/client.h>

#include "GraphicsDevice/Cuda/CudaContext.h"
#include "GraphicsDevice/IGraphicsDevice.h"
#include "nvEncodeAPI.h"

using namespace Microsoft::WRL;

namespace unity
{
namespace webrtc
{

    class D3D11GraphicsDevice : public IGraphicsDevice
    {
    public:
        D3D11GraphicsDevice(ID3D11Device* nativeDevice, UnityGfxRenderer renderer, ProfilerMarkerFactory* profiler);
        virtual ~D3D11GraphicsDevice() override;
        virtual bool InitV() override;
        virtual void ShutdownV() override;
        inline virtual void* GetEncodeDevicePtrV() override;
        ITexture2D* CreateTexture(void* texture) override;
        ITexture2D*
        CreateDefaultTextureV(uint32_t w, uint32_t h, UnityRenderingExtTextureFormat textureFormat) override;
        bool
        CopyToVideoFrameBuffer(rtc::scoped_refptr<::webrtc::VideoFrameBuffer>& buffer, void* texture) override;
        virtual ITexture2D*
        CreateCPUReadTextureV(uint32_t w, uint32_t h, UnityRenderingExtTextureFormat textureFormat) override;
        bool CopyResourceV(ITexture2D* dest, ITexture2D* src) override;
        bool CopyResourceFromNativeV(ITexture2D* dest, void* nativeTexturePtr) override;
        bool CopyResourceFromHandle(ITexture2D* dest, const GpuMemoryBufferHandle* handle) override;
        std::unique_ptr<GpuMemoryBufferHandle> Map(ITexture2D* texture) override;
        rtc::scoped_refptr<::webrtc::I420Buffer> ConvertRGBToI420(ITexture2D* tex) override;
        rtc::scoped_refptr<::webrtc::VideoFrameBuffer> ConvertToBuffer(void* ptr) override;

        bool IsCudaSupport() override { return m_isCudaSupport; }
        CUcontext GetCUcontext() override { return m_cudaContext.GetContext(); }
        NV_ENC_BUFFER_FORMAT GetEncodeBufferFormat() override { return NV_ENC_BUFFER_FORMAT_ARGB; }

    private:
        HRESULT WaitFlush();
        ID3D11Device* m_d3d11Device;

        bool m_isCudaSupport;
        CudaContext m_cudaContext;
    };

    //---------------------------------------------------------------------------------------------------------------------

    void* D3D11GraphicsDevice::GetEncodeDevicePtrV() { return reinterpret_cast<void*>(m_d3d11Device); }

} // end namespace webrtc
} // end namespace unity
