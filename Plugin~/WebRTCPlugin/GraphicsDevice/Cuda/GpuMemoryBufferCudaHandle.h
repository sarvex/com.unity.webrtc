#pragma once

#if defined(_WIN32)
#include <d3d11.h>
#include <d3d12.h>
#endif

#if defined(__linux__)
#include <GL.h>
#endif

#include <vulkan/vulkan.h>

#include <cuda.h>

#include "GpuMemoryBuffer.h"

namespace unity
{
namespace webrtc
{
    struct GpuMemoryBufferCudaHandle : public GpuMemoryBufferHandle
    {
        GpuMemoryBufferCudaHandle();
        GpuMemoryBufferCudaHandle(GpuMemoryBufferCudaHandle&& other);
        GpuMemoryBufferCudaHandle& operator=(GpuMemoryBufferCudaHandle&& other);
        virtual ~GpuMemoryBufferCudaHandle() override;

        CUcontext context;
        CUarray array;
        CUarray mappedArray;
        CUdeviceptr mappedPtr;
        CUgraphicsResource resource;
        CUexternalMemory externalMemory;

        static std::unique_ptr<GpuMemoryBufferCudaHandle> CreateHandle(CUcontext context, CUdeviceptr ptr, AccessMode mode);
#if defined(_WIN32)
        static std::unique_ptr<GpuMemoryBufferCudaHandle>
        CreateHandle(CUcontext context, ID3D11Resource* resource, AccessMode mode);
        static std::unique_ptr<GpuMemoryBufferCudaHandle> CreateHandle(
            CUcontext context, ID3D12Resource* resource, HANDLE sharedHandle, size_t memorySize, AccessMode mode);
#endif
        static std::unique_ptr<GpuMemoryBufferCudaHandle>
        CreateHandle(CUcontext context, void* exportHandle, size_t memorySize, const Size& size, AccessMode mode);
#if defined(__linux__)
        static std::unique_ptr<GpuMemoryBufferCudaHandle>
        CreateHandle(CUcontext context, GLuint texture, GLenum target, AccessMode mode);
#endif
    };
}
}
