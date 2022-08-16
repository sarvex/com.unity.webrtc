#include "pch.h"

#include "GpuMemoryBufferCudaHandle.h"

namespace unity
{
namespace webrtc
{
    GpuMemoryBufferCudaHandle::GpuMemoryBufferCudaHandle()
        : context(nullptr)
        , array(nullptr)
        , mappedArray(nullptr)
        , mappedPtr(0)
        , resource(nullptr)
        , externalMemory(nullptr)
    {
    }

    GpuMemoryBufferCudaHandle::GpuMemoryBufferCudaHandle(GpuMemoryBufferCudaHandle&& other) = default;
    GpuMemoryBufferCudaHandle& GpuMemoryBufferCudaHandle::operator=(GpuMemoryBufferCudaHandle&& other) = default;

    GpuMemoryBufferCudaHandle::~GpuMemoryBufferCudaHandle()
    {
        cuCtxPushCurrent(context);

        CUresult result;
        if (externalMemory != nullptr)
        {
            result = cuDestroyExternalMemory(externalMemory);
            if (result != CUDA_SUCCESS)
            {
                RTC_LOG(LS_ERROR) << "faild cuDestroyExternalMemory CUresult: " << result;
            }
        }
        if (resource != nullptr)
        {
            result = cuGraphicsUnmapResources(1, &resource, nullptr);
            if (result != CUDA_SUCCESS)
            {
                RTC_LOG(LS_ERROR) << "faild cuGraphicsUnmapResources CUresult: " << result;
            }
            result = cuGraphicsUnregisterResource(resource);
            if (result != CUDA_SUCCESS)
            {
                RTC_LOG(LS_ERROR) << "faild cuGraphicsUnregisterResource CUresult: " << result;
            }
        }
        if (array != nullptr)
        {
            result = cuArrayDestroy(array);
            if (result != CUDA_SUCCESS)
            {
                RTC_LOG(LS_ERROR) << "faild cuArrayDestroy CUresult: " << result;
            }
        }

        cuCtxPopCurrent(nullptr);
    }
    std::unique_ptr<GpuMemoryBufferCudaHandle>
    GpuMemoryBufferCudaHandle::CreateHandle(CUcontext context, CUdeviceptr ptr, AccessMode mode)
    {
        std::unique_ptr<GpuMemoryBufferCudaHandle> handle = std::make_unique<GpuMemoryBufferCudaHandle>();
        handle->context = context;
        handle->mappedPtr = ptr;

        return std::move(handle);
    }

#if defined(_WIN32)
    std::unique_ptr<GpuMemoryBufferCudaHandle>
    GpuMemoryBufferCudaHandle::CreateHandle(CUcontext context, ID3D11Resource* resource, AccessMode mode)
    {
        // set context on the thread.
        cuCtxPushCurrent(context);

        CUresult result;
        CUgraphicsResource cuResource = 0;

        switch (mode)
        {
        case AccessMode::kRead:
            result = cuGraphicsD3D11RegisterResource(&cuResource, resource, CU_GRAPHICS_REGISTER_FLAGS_NONE);
            if (result != CUDA_SUCCESS)
            {
                RTC_LOG(LS_ERROR) << "cuGraphicsD3D11RegisterResource failed. result" << result;
                return nullptr;
            }
            result = cuGraphicsResourceSetMapFlags(cuResource, CU_GRAPHICS_MAP_RESOURCE_FLAGS_READ_ONLY);
            if (result != CUDA_SUCCESS)
            {
                RTC_LOG(LS_ERROR) << "cuGraphicsResourceSetMapFlags failed. result" << result;
                return nullptr;
            }
            break;
        case AccessMode::kWrite:
            result = cuGraphicsD3D11RegisterResource(&cuResource, resource, CU_GRAPHICS_REGISTER_FLAGS_NONE);
            if (result != CUDA_SUCCESS)
            {
                RTC_LOG(LS_ERROR) << "cuGraphicsD3D11RegisterResource failed. result" << result;
                return nullptr;
            }
            result = cuGraphicsResourceSetMapFlags(cuResource, CU_GRAPHICS_MAP_RESOURCE_FLAGS_WRITE_DISCARD);
            if (result != CUDA_SUCCESS)
            {
                RTC_LOG(LS_ERROR) << "cuGraphicsResourceSetMapFlags failed. result" << result;
                return nullptr;
            }
            break;
        }

        result = cuGraphicsMapResources(1, &cuResource, nullptr);
        if (result != CUDA_SUCCESS)
        {
            RTC_LOG(LS_ERROR) << "cuGraphicsMapResources failed. result" << result;
            return nullptr;
        }

        CUarray mappedArray;
        result = cuGraphicsSubResourceGetMappedArray(&mappedArray, cuResource, 0, 0);
        if (result != CUDA_SUCCESS)
        {
            RTC_LOG(LS_ERROR) << "cuGraphicsSubResourceGetMappedArray failed. result" << result;
            return nullptr;
        }
        cuCtxPopCurrent(nullptr);

        std::unique_ptr<GpuMemoryBufferCudaHandle> handle = std::make_unique<GpuMemoryBufferCudaHandle>();
        handle->context = context;
        handle->mappedArray = mappedArray;
        handle->resource = cuResource;
        return std::move(handle);
    }

    std::unique_ptr<GpuMemoryBufferCudaHandle> GpuMemoryBufferCudaHandle::CreateHandle(
        CUcontext context, ID3D12Resource* resource, HANDLE sharedHandle, size_t memorySize, AccessMode mode)
    {
        // set context on the thread.
        cuCtxPushCurrent(context);

        D3D12_RESOURCE_DESC desc = resource->GetDesc();
        size_t width = desc.Width;
        size_t height = desc.Height;
        CUDA_EXTERNAL_MEMORY_HANDLE_DESC memDesc = {};
        memDesc.type = CU_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_RESOURCE;
        memDesc.handle.win32.handle = static_cast<void*>(sharedHandle);
        memDesc.size = memorySize;
        memDesc.flags = CUDA_EXTERNAL_MEMORY_DEDICATED;

        CUresult result;
        CUexternalMemory externalMemory = {};
        result = cuImportExternalMemory(&externalMemory, &memDesc);
        if (result != CUDA_SUCCESS)
        {
            RTC_LOG(LS_ERROR) << "cuImportExternalMemory error";
            return nullptr;
        }

        CUDA_ARRAY3D_DESCRIPTOR arrayDesc = {};
        arrayDesc.Width = width;
        arrayDesc.Height = height;
        arrayDesc.Depth = 0; /* CUDA 2D arrays are defined to have depth 0 */
        arrayDesc.Format = CU_AD_FORMAT_UNSIGNED_INT32;
        arrayDesc.NumChannels = 1;
        arrayDesc.Flags = CUDA_ARRAY3D_SURFACE_LDST | CUDA_ARRAY3D_COLOR_ATTACHMENT;

        CUDA_EXTERNAL_MEMORY_MIPMAPPED_ARRAY_DESC mipmapArrayDesc = {};
        mipmapArrayDesc.arrayDesc = arrayDesc;
        mipmapArrayDesc.numLevels = 1;

        CUmipmappedArray mipmappedArray;
        result = cuExternalMemoryGetMappedMipmappedArray(&mipmappedArray, externalMemory, &mipmapArrayDesc);
        if (result != CUDA_SUCCESS)
        {
            RTC_LOG(LS_ERROR) << "cuExternalMemoryGetMappedMipmappedArray error";
            return nullptr;
        }

        CUarray array;
        result = cuMipmappedArrayGetLevel(&array, mipmappedArray, 0);
        if (result != CUDA_SUCCESS)
        {
            RTC_LOG(LS_ERROR) << "cuMipmappedArrayGetLevel error";
            return nullptr;
        }
        cuCtxPopCurrent(nullptr);

        std::unique_ptr<GpuMemoryBufferCudaHandle> handle = std::make_unique<GpuMemoryBufferCudaHandle>();
        handle->context = context;
        handle->mappedArray = array;
        handle->externalMemory = externalMemory;
        return std::move(handle);
    }
#endif

    std::unique_ptr<GpuMemoryBufferCudaHandle> GpuMemoryBufferCudaHandle::CreateHandle(
        CUcontext context, void* exportHandle, size_t memorySize, const Size& size, AccessMode mode)
    {
        // set context on the thread.
        cuCtxPushCurrent(context);

        CUDA_EXTERNAL_MEMORY_HANDLE_DESC memDesc = {};
#ifndef _WIN32
        memDesc.type = CU_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD;
#else
        memDesc.type = CU_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32;
#endif
        memDesc.handle.fd = static_cast<int>(reinterpret_cast<uintptr_t>(exportHandle));
        memDesc.size = memorySize;

        CUresult result;
        CUexternalMemory externalMemory;
        result = cuImportExternalMemory(&externalMemory, &memDesc);
        if (result != CUDA_SUCCESS)
        {
            RTC_LOG(LS_ERROR) << "cuImportExternalMemory error:" << result;
            return nullptr;
        }

        CUDA_ARRAY3D_DESCRIPTOR arrayDesc = {};
        arrayDesc.Width = static_cast<size_t>(size.width());
        arrayDesc.Height = static_cast<size_t>(size.height());
        arrayDesc.Depth = 0; /* CUDA 2D arrays are defined to have depth 0 */
        arrayDesc.Format = CU_AD_FORMAT_UNSIGNED_INT32;
        arrayDesc.NumChannels = 1;
        arrayDesc.Flags = CUDA_ARRAY3D_SURFACE_LDST | CUDA_ARRAY3D_COLOR_ATTACHMENT;

        CUDA_EXTERNAL_MEMORY_MIPMAPPED_ARRAY_DESC mipmapArrayDesc = {};
        mipmapArrayDesc.arrayDesc = arrayDesc;
        mipmapArrayDesc.numLevels = 1;

        CUmipmappedArray mipmappedArray;
        result = cuExternalMemoryGetMappedMipmappedArray(&mipmappedArray, externalMemory, &mipmapArrayDesc);
        if (result != CUDA_SUCCESS)
        {
            RTC_LOG(LS_ERROR) << "cuExternalMemoryGetMappedMipmappedArray error:" << result;
            return nullptr;
        }

        CUarray array;
        result = cuMipmappedArrayGetLevel(&array, mipmappedArray, 0);
        if (result != CUDA_SUCCESS)
        {
            RTC_LOG(LS_ERROR) << "cuMipmappedArrayGetLevel error:" << result;
            return nullptr;
        }

        cuCtxPopCurrent(nullptr);

        std::unique_ptr<GpuMemoryBufferCudaHandle> handle = std::make_unique<GpuMemoryBufferCudaHandle>();
        handle->context = context;
        handle->mappedArray = array;
        handle->externalMemory = externalMemory;
        return std::move(handle);
    }
}
}
