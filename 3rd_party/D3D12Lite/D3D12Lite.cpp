#include "D3D12Lite.h"
#include "DXTex/DirectXTex.h"
#include "D3D12MemoryAllocator/D3D12MemAlloc.h"
#include "dxc/inc/dxcapi.h"
#include <dxgidebug.h>
#include <numeric>

extern "C" { __declspec(dllexport) extern const UINT D3D12SDKVersion = 602; }
extern "C" { __declspec(dllexport) extern const char* D3D12SDKPath = ".\\D3D12\\"; }

namespace D3D12Lite
{
    DescriptorHeap::DescriptorHeap(ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE heapType, uint32_t numDescriptors, bool isShaderVisible)
        :mHeapType(heapType)
        , mMaxDescriptors(numDescriptors)
        , mIsShaderVisible(isShaderVisible)
    {
        D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
        heapDesc.NumDescriptors = mMaxDescriptors;
        heapDesc.Type = mHeapType;
        heapDesc.Flags = mIsShaderVisible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        heapDesc.NodeMask = 0;

        AssertIfFailed(device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&mDescriptorHeap)));

        mHeapStart.mCPUHandle = mDescriptorHeap->GetCPUDescriptorHandleForHeapStart();

        if (mIsShaderVisible)
        {
            mHeapStart.mGPUHandle = mDescriptorHeap->GetGPUDescriptorHandleForHeapStart();
        }

        mDescriptorSize = device->GetDescriptorHandleIncrementSize(mHeapType);
    }

    DescriptorHeap::~DescriptorHeap()
    {
        SafeRelease(mDescriptorHeap);
    }

    StagingDescriptorHeap::StagingDescriptorHeap(ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE heapType, uint32_t numDescriptors)
        :DescriptorHeap(device, heapType, numDescriptors, false)
    {
        mFreeDescriptors.reserve(numDescriptors);
    }

    StagingDescriptorHeap::~StagingDescriptorHeap()
    {
        if (mActiveHandleCount != 0)
        {
            AssertError("There were active handles when the descriptor heap was destroyed. Look for leaks.");
        }
    }

    Descriptor StagingDescriptorHeap::GetNewDescriptor()
    {
        std::lock_guard<std::mutex> lockGuard(mUsageMutex);

        uint32_t newHandleID = 0;

        if (mCurrentDescriptorIndex < mMaxDescriptors)
        {
            newHandleID = mCurrentDescriptorIndex;
            mCurrentDescriptorIndex++;
        }
        else if (mFreeDescriptors.size() > 0)
        {
            newHandleID = mFreeDescriptors.back();
            mFreeDescriptors.pop_back();
        }
        else
        {
            AssertError("Ran out of dynamic descriptor heap handles, need to increase heap size.");
        }

        Descriptor newDescriptor;
        D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = mHeapStart.mCPUHandle;
        cpuHandle.ptr += static_cast<uint64_t>(newHandleID) * mDescriptorSize;
        newDescriptor.mCPUHandle = cpuHandle;
        newDescriptor.mHeapIndex = newHandleID;

        mActiveHandleCount++;

        return newDescriptor;
    }

    void StagingDescriptorHeap::FreeDescriptor(Descriptor descriptor)
    {
        std::lock_guard<std::mutex> lockGuard(mUsageMutex);

        mFreeDescriptors.push_back(descriptor.mHeapIndex);

        if (mActiveHandleCount == 0)
        {
            AssertError("Freeing heap handles when there should be none left");
            return;
        }

        mActiveHandleCount--;
    }

    RenderPassDescriptorHeap::RenderPassDescriptorHeap(ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE heapType, uint32_t reservedCount, uint32_t userCount)
        :DescriptorHeap(device, heapType, reservedCount + userCount, true)
        , mReservedHandleCount(reservedCount)
        , mCurrentDescriptorIndex(reservedCount)
    {
    }

    Descriptor RenderPassDescriptorHeap::GetReservedDescriptor(uint32_t index)
    {
        assert(index < mReservedHandleCount);

        D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = mHeapStart.mCPUHandle;
        D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle = mHeapStart.mGPUHandle;
        cpuHandle.ptr += static_cast<uint64_t>(index) * mDescriptorSize;
        gpuHandle.ptr += static_cast<uint64_t>(index) * mDescriptorSize;

        Descriptor descriptor;
        descriptor.mHeapIndex = index;
        descriptor.mCPUHandle = cpuHandle;
        descriptor.mGPUHandle = gpuHandle;

        return descriptor;
    }

    Descriptor RenderPassDescriptorHeap::AllocateUserDescriptorBlock(uint32_t count)
    {
        uint32_t newHandleID = 0;

        {
            std::lock_guard<std::mutex> lockGuard(mUsageMutex);

            uint32_t blockEnd = mCurrentDescriptorIndex + count;

            if (blockEnd <= mMaxDescriptors)
            {
                newHandleID = mCurrentDescriptorIndex;
                mCurrentDescriptorIndex = blockEnd;
            }
            else
            {
                AssertError("Ran out of render pass descriptor heap handles, need to increase heap size.");
            }
        }

        Descriptor newDescriptor;
        newDescriptor.mHeapIndex = newHandleID;

        D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = mHeapStart.mCPUHandle;
        cpuHandle.ptr += static_cast<uint64_t>(newHandleID) * mDescriptorSize;
        newDescriptor.mCPUHandle = cpuHandle;

        D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle = mHeapStart.mGPUHandle;
        gpuHandle.ptr += static_cast<uint64_t>(newHandleID) * mDescriptorSize;
        newDescriptor.mGPUHandle = gpuHandle;

        return newDescriptor;
    }

    void RenderPassDescriptorHeap::Reset()
    {
        mCurrentDescriptorIndex = mReservedHandleCount;
    }

    bool SortPipelineBindings(PipelineResourceBinding a, PipelineResourceBinding b)
    {
        return a.mBindingIndex < b.mBindingIndex;
    }

    void PipelineResourceSpace::SetCBV(BufferResource* resource)
    {
        if (mIsLocked)
        {
            if (mCBV == nullptr)
            {
                AssertError("Setting unused binding in a locked resource space");
            }
            else
            {
                mCBV = resource;
            }
        }
        else
        {
            mCBV = resource;
        }
    }

    void PipelineResourceSpace::SetSRV(const PipelineResourceBinding& binding)
    {
        uint32_t currentIndex = GetIndexOfBindingIndex(mSRVs, binding.mBindingIndex);

        if (mIsLocked)
        {
            if (currentIndex == UINT_MAX)
            {
                AssertError("Setting unused binding in a locked resource space");
            }
            else
            {
                mSRVs[currentIndex] = binding;
            }
        }
        else
        {
            if (currentIndex == UINT_MAX)
            {
                mSRVs.push_back(binding);
                std::sort(mSRVs.begin(), mSRVs.end(), SortPipelineBindings);
            }
            else
            {
                mSRVs[currentIndex] = binding;
            }
        }
    }

    void PipelineResourceSpace::SetUAV(const PipelineResourceBinding& binding)
    {
        uint32_t currentIndex = GetIndexOfBindingIndex(mUAVs, binding.mBindingIndex);

        if (mIsLocked)
        {
            if (currentIndex == UINT_MAX)
            {
                AssertError("Setting unused binding in a locked resource space");
            }
            else
            {
                mUAVs[currentIndex] = binding;
            }
        }
        else
        {
            if (currentIndex == UINT_MAX)
            {
                mUAVs.push_back(binding);
                std::sort(mUAVs.begin(), mUAVs.end(), SortPipelineBindings);
            }
            else
            {
                mUAVs[currentIndex] = binding;
            }
        }
    }

    void PipelineResourceSpace::Lock()
    {
        mIsLocked = true;
    }

    uint32_t PipelineResourceSpace::GetIndexOfBindingIndex(const std::vector<PipelineResourceBinding>& bindings, uint32_t bindingIndex)
    {
        const uint32_t numBindings = static_cast<uint32_t>(bindings.size());
        for (uint32_t vectorIndex = 0; vectorIndex < numBindings; vectorIndex++)
        {
            if (bindings.at(vectorIndex).mBindingIndex == bindingIndex)
            {
                return vectorIndex;
            }
        }

        return UINT_MAX;
    }

    Queue::Queue(ID3D12Device* device, D3D12_COMMAND_LIST_TYPE commandType)
        :mQueueType(commandType)
    {
        D3D12_COMMAND_QUEUE_DESC queueDesc = {};
        queueDesc.Type = mQueueType;
        queueDesc.NodeMask = 0;
        device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&mQueue));

        AssertIfFailed(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&mFence)));

        mFence->Signal(mLastCompletedFenceValue);

        mFenceEventHandle = CreateEventEx(nullptr, nullptr, 0, EVENT_ALL_ACCESS);
        assert(mFenceEventHandle != INVALID_HANDLE_VALUE);
    }

    Queue::~Queue()
    {
        CloseHandle(mFenceEventHandle);

        SafeRelease(mFence);
        SafeRelease(mQueue);
    }

    uint64_t Queue::PollCurrentFenceValue()
    {
        mLastCompletedFenceValue = (std::max)(mLastCompletedFenceValue, mFence->GetCompletedValue());
        return mLastCompletedFenceValue;
    }

    bool Queue::IsFenceComplete(uint64_t fenceValue)
    {
        if (fenceValue > mLastCompletedFenceValue)
        {
            PollCurrentFenceValue();
        }

        return fenceValue <= mLastCompletedFenceValue;
    }

    void Queue::InsertWait(uint64_t fenceValue)
    {
        mQueue->Wait(mFence, fenceValue);
    }

    void Queue::InsertWaitForQueueFence(Queue* otherQueue, uint64_t fenceValue)
    {
        mQueue->Wait(otherQueue->GetFence(), fenceValue);
    }

    void Queue::InsertWaitForQueue(Queue* otherQueue)
    {
        mQueue->Wait(otherQueue->GetFence(), otherQueue->GetNextFenceValue() - 1);
    }

    void Queue::WaitForFenceCPUBlocking(uint64_t fenceValue)
    {
        if (IsFenceComplete(fenceValue))
        {
            return;
        }

        {
            std::lock_guard<std::mutex> lockGuard(mEventMutex);

            mFence->SetEventOnCompletion(fenceValue, mFenceEventHandle);
            WaitForSingleObjectEx(mFenceEventHandle, INFINITE, false);
            mLastCompletedFenceValue = fenceValue;
        }
    }

    void Queue::WaitForIdle()
    {
        WaitForFenceCPUBlocking(mNextFenceValue - 1);
    }

    uint64_t Queue::ExecuteCommandList(ID3D12CommandList* commandList)
    {
        AssertIfFailed(static_cast<ID3D12GraphicsCommandList*>(commandList)->Close());
        mQueue->ExecuteCommandLists(1, &commandList);

        return SignalFence();
    }

    uint64_t Queue::SignalFence()
    {
        std::lock_guard<std::mutex> lockGuard(mFenceMutex);

        mQueue->Signal(mFence, mNextFenceValue);

        return mNextFenceValue++;
    }

    Context::Context(Device& device, D3D12_COMMAND_LIST_TYPE commandType)
        :mDevice(device)
        , mContextType(commandType)
    {
        for (uint32_t frameIndex = 0; frameIndex < NUM_FRAMES_IN_FLIGHT; frameIndex++)
        {
            AssertIfFailed(mDevice.GetDevice()->CreateCommandAllocator(commandType, IID_PPV_ARGS(&mCommandAllocators[frameIndex])));
        }

        AssertIfFailed(mDevice.GetDevice()->CreateCommandList1(0, commandType, D3D12_COMMAND_LIST_FLAG_NONE, IID_PPV_ARGS(&mCommandList)));
    }

    Context::~Context()
    {
        SafeRelease(mCommandList);

        for (uint32_t frameIndex = 0; frameIndex < NUM_FRAMES_IN_FLIGHT; frameIndex++)
        {
            SafeRelease(mCommandAllocators[frameIndex]);
        }
    }

    void Context::Reset()
    {
        uint32_t frameId = mDevice.GetFrameId();

        mCommandAllocators[frameId]->Reset();
        mCommandList->Reset(mCommandAllocators[frameId], nullptr);

        if (mContextType != D3D12_COMMAND_LIST_TYPE_COPY)
        {
            BindDescriptorHeaps(mDevice.GetFrameId());
        }
    }

    void Context::AddBarrier(Resource& resource, D3D12_RESOURCE_STATES newState)
    {
        if (mNumQueuedBarriers >= MAX_QUEUED_BARRIERS)
        {
            FlushBarriers();
        }

        D3D12_RESOURCE_STATES oldState = resource.mState;

        if (mContextType == D3D12_COMMAND_LIST_TYPE_COMPUTE)
        {
            constexpr D3D12_RESOURCE_STATES VALID_COMPUTE_CONTEXT_STATES = (D3D12_RESOURCE_STATE_UNORDERED_ACCESS | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE |
                                                                            D3D12_RESOURCE_STATE_COPY_DEST | D3D12_RESOURCE_STATE_COPY_SOURCE);

            assert((oldState & VALID_COMPUTE_CONTEXT_STATES) == oldState);
            assert((newState & VALID_COMPUTE_CONTEXT_STATES) == newState);
        }

        if (oldState != newState)
        {
            D3D12_RESOURCE_BARRIER& barrierDesc = mResourceBarriers[mNumQueuedBarriers];
            mNumQueuedBarriers++;

            barrierDesc.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrierDesc.Transition.pResource = resource.mResource;
            barrierDesc.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            barrierDesc.Transition.StateBefore = oldState;
            barrierDesc.Transition.StateAfter = newState;
            barrierDesc.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;

            resource.mState = newState;
        }
        else if (newState == D3D12_RESOURCE_STATE_UNORDERED_ACCESS)
        {
            D3D12_RESOURCE_BARRIER& barrierDesc = mResourceBarriers[mNumQueuedBarriers];
            mNumQueuedBarriers++;

            barrierDesc.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
            barrierDesc.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
            barrierDesc.UAV.pResource = resource.mResource;
        }
    }

    void Context::FlushBarriers()
    {
        if (mNumQueuedBarriers > 0)
        {
            mCommandList->ResourceBarrier(mNumQueuedBarriers, mResourceBarriers.data());
            mNumQueuedBarriers = 0;
        }
    }

    void Context::BindDescriptorHeaps(uint32_t frameIndex)
    {
        mCurrentSRVHeap = &mDevice.GetSRVHeap(frameIndex);
        mCurrentSRVHeap->Reset();

        ID3D12DescriptorHeap* heapsToBind[2];
        heapsToBind[0] = mDevice.GetSRVHeap(frameIndex).GetHeap();
        heapsToBind[1] = mDevice.GetSamplerHeap().GetHeap();

        mCommandList->SetDescriptorHeaps(2, heapsToBind);
    }

    void Context::CopyResource(const Resource& destination, const Resource& source)
    {
        mCommandList->CopyResource(destination.mResource, source.mResource);
    }

    void Context::CopyBufferRegion(Resource& destination, uint64_t destOffset, Resource& source, uint64_t sourceOffset, uint64_t numBytes)
    {
        mCommandList->CopyBufferRegion(destination.mResource, destOffset, source.mResource, sourceOffset, numBytes);
    }

    void Context::CopyTextureRegion(Resource& destination, Resource& source, size_t sourceOffset, SubResourceLayouts& subResourceLayouts, uint32_t numSubResources)
    {
        for (uint32_t subResourceIndex = 0; subResourceIndex < numSubResources; subResourceIndex++)
        {
            D3D12_TEXTURE_COPY_LOCATION destinationLocation = {};
            destinationLocation.pResource = destination.mResource;
            destinationLocation.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            destinationLocation.SubresourceIndex = subResourceIndex;

            D3D12_TEXTURE_COPY_LOCATION sourceLocation = {};
            sourceLocation.pResource = source.mResource;
            sourceLocation.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            sourceLocation.PlacedFootprint = subResourceLayouts[subResourceIndex];
            sourceLocation.PlacedFootprint.Offset += sourceOffset;

            mCommandList->CopyTextureRegion(&destinationLocation, 0, 0, 0, &sourceLocation, nullptr);
        }
    }

    GraphicsContext::GraphicsContext(Device& device)
        :Context(device, D3D12_COMMAND_LIST_TYPE_DIRECT)
    {
    }

    void GraphicsContext::SetDefaultViewPortAndScissor(Uint2 screenSize)
    {
        D3D12_VIEWPORT viewPort;
        viewPort.Width = static_cast<float>(screenSize.x);
        viewPort.Height = static_cast<float>(screenSize.y);
        viewPort.MinDepth = 0.0f;
        viewPort.MaxDepth = 1.0f;
        viewPort.TopLeftX = 0;
        viewPort.TopLeftY = 0;

        D3D12_RECT scissor;
        scissor.top = 0;
        scissor.left = 0;
        scissor.bottom = screenSize.y;
        scissor.right = screenSize.x;

        SetViewport(viewPort);
        SetScissorRect(scissor);
    }

    void GraphicsContext::SetViewport(const D3D12_VIEWPORT& viewPort)
    {
        mCommandList->RSSetViewports(1, &viewPort);
    }

    void GraphicsContext::SetScissorRect(const D3D12_RECT& rect)
    {
        mCommandList->RSSetScissorRects(1, &rect);
    }

    void GraphicsContext::SetStencilRef(uint32_t stencilRef)
    {
        mCommandList->OMSetStencilRef(stencilRef);
    }

    void GraphicsContext::SetBlendFactor(Color blendFactor)
    {
        float color[4] = { blendFactor.R(), blendFactor.G(), blendFactor.B(), blendFactor.A() };
        mCommandList->OMSetBlendFactor(color);
    }

    void GraphicsContext::SetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY topology)
    {
        mCommandList->IASetPrimitiveTopology(topology);
    }

    void GraphicsContext::SetPipeline(const PipelineInfo& pipelineBinding)
    {
        const bool pipelineExpectedBoundExternally = !pipelineBinding.mPipeline; //imgui

        if (!pipelineExpectedBoundExternally)
        {
            if (pipelineBinding.mPipeline->mPipelineType == PipelineType::compute)
            {
                mCommandList->SetPipelineState(pipelineBinding.mPipeline->mPipeline);
                mCommandList->SetComputeRootSignature(pipelineBinding.mPipeline->mRootSignature);
            }
            else
            {
                mCommandList->SetPipelineState(pipelineBinding.mPipeline->mPipeline);
                mCommandList->SetGraphicsRootSignature(pipelineBinding.mPipeline->mRootSignature);
            }
        }

        mCurrentPipeline = pipelineBinding.mPipeline;

        if (pipelineExpectedBoundExternally || mCurrentPipeline->mPipelineType == PipelineType::graphics)
        {
            D3D12_CPU_DESCRIPTOR_HANDLE renderTargetHandles[D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT]{};
            D3D12_CPU_DESCRIPTOR_HANDLE depthStencilHandle{ 0 };

            size_t renderTargetCount = pipelineBinding.mRenderTargets.size();

            for (size_t targetIndex = 0; targetIndex < renderTargetCount; targetIndex++)
            {
                renderTargetHandles[targetIndex] = pipelineBinding.mRenderTargets[targetIndex]->mRTVDescriptor.mCPUHandle;
            }

            if (pipelineBinding.mDepthStencilTarget)
            {
                depthStencilHandle = pipelineBinding.mDepthStencilTarget->mDSVDescriptor.mCPUHandle;
            }

            SetTargets(static_cast<uint32_t>(renderTargetCount), renderTargetHandles, depthStencilHandle);
        }
    }

    void GraphicsContext::SetPipelineResources(uint32_t spaceId, const PipelineResourceSpace& resources)
    {
        assert(mCurrentPipeline);
        assert(resources.IsLocked());

        static const uint32_t maxNumHandlesPerBinding = 16;
        static const uint32_t singleDescriptorRangeCopyArray[maxNumHandlesPerBinding]{ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 ,1 };
        
        const BufferResource* cbv = resources.GetCBV();
        const auto& uavs = resources.GetUAVs();
        const auto& srvs = resources.GetSRVs();
        const uint32_t numTableHandles = static_cast<uint32_t>(uavs.size() + srvs.size());
        D3D12_CPU_DESCRIPTOR_HANDLE handles[maxNumHandlesPerBinding]{};
        uint32_t currentHandleIndex = 0;
        assert(numTableHandles <= maxNumHandlesPerBinding);

        if(cbv)
        {
            auto& cbvMapping = mCurrentPipeline->mPipelineResourceMapping.mCbvMapping[spaceId];
            assert(cbvMapping.has_value());

            switch (mCurrentPipeline->mPipelineType)
            {
            case PipelineType::graphics:
                mCommandList->SetGraphicsRootConstantBufferView(cbvMapping.value(), cbv->mVirtualAddress);
                break;
            case PipelineType::compute:
                mCommandList->SetComputeRootConstantBufferView(cbvMapping.value(), cbv->mVirtualAddress);
                break;
            default:
                assert(false);
                break;
            }
        }

        if (numTableHandles == 0)
        {
            return;
        }

        for (auto& uav : uavs)
        {
            if (uav.mResource->mType == GPUResourceType::buffer)
            {
                handles[currentHandleIndex++] = static_cast<BufferResource*>(uav.mResource)->mUAVDescriptor.mCPUHandle;
            }
            else
            {
                handles[currentHandleIndex++] = static_cast<TextureResource*>(uav.mResource)->mUAVDescriptor.mCPUHandle;
            }
        }

        for (auto& srv : srvs)
        {
            if (srv.mResource->mType == GPUResourceType::buffer)
            {
                handles[currentHandleIndex++] = static_cast<BufferResource*>(srv.mResource)->mSRVDescriptor.mCPUHandle;
            }
            else
            {
                handles[currentHandleIndex++] = static_cast<TextureResource*>(srv.mResource)->mSRVDescriptor.mCPUHandle;
            }
        }

        Descriptor blockStart = mCurrentSRVHeap->AllocateUserDescriptorBlock(numTableHandles);
        mDevice.CopyDescriptors(1, &blockStart.mCPUHandle, &numTableHandles, numTableHandles, handles, singleDescriptorRangeCopyArray, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

        auto& tableMapping = mCurrentPipeline->mPipelineResourceMapping.mTableMapping[spaceId];
        assert(tableMapping.has_value());

        switch (mCurrentPipeline->mPipelineType)
        {
        case PipelineType::graphics:
            mCommandList->SetGraphicsRootDescriptorTable(tableMapping.value(), blockStart.mGPUHandle);
            break;
        case PipelineType::compute:
            mCommandList->SetComputeRootDescriptorTable(tableMapping.value(), blockStart.mGPUHandle);
            break;
        default:
            assert(false);
            break;
        }
    }

    void GraphicsContext::SetTargets(uint32_t numRenderTargets, const D3D12_CPU_DESCRIPTOR_HANDLE renderTargets[], D3D12_CPU_DESCRIPTOR_HANDLE depthStencil)
    {
        mCommandList->OMSetRenderTargets(numRenderTargets, renderTargets, false, depthStencil.ptr != 0 ? &depthStencil : nullptr);
    }

    void GraphicsContext::SetIndexBuffer(const BufferResource& indexBuffer)
    {
        D3D12_INDEX_BUFFER_VIEW indexBufferView;
        indexBufferView.Format = indexBuffer.mStride == 4 ? DXGI_FORMAT_R32_UINT : indexBuffer.mStride == 2 ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_UNKNOWN;
        indexBufferView.SizeInBytes = static_cast<uint32_t>(indexBuffer.mDesc.Width);
        indexBufferView.BufferLocation = indexBuffer.mResource->GetGPUVirtualAddress();

        mCommandList->IASetIndexBuffer(&indexBufferView);
    }

    void GraphicsContext::ClearRenderTarget(const TextureResource& target, Color color)
    {
        mCommandList->ClearRenderTargetView(target.mRTVDescriptor.mCPUHandle, color, 0, nullptr);
    }

    void GraphicsContext::ClearDepthStencilTarget(const TextureResource& target, float depth, uint8_t stencil)
    {
        mCommandList->ClearDepthStencilView(target.mDSVDescriptor.mCPUHandle, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, depth, stencil, 0, nullptr);
    }

    void GraphicsContext::DrawFullScreenTriangle()
    {
        SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
        mCommandList->IASetIndexBuffer(nullptr);
        Draw(3);
    }

    void GraphicsContext::Draw(uint32_t vertexCount, uint32_t vertexStartOffset)
    {
        DrawInstanced(vertexCount, 1, vertexStartOffset, 0);
    }

    void GraphicsContext::DrawIndexed(uint32_t indexCount, uint32_t startIndexLocation, uint32_t baseVertexLocation)
    {
        DrawIndexedInstanced(indexCount, 1, startIndexLocation, baseVertexLocation, 0);
    }

    void GraphicsContext::DrawInstanced(uint32_t vertexCountPerInstance, uint32_t instanceCount, uint32_t startVertexLocation, uint32_t startInstanceLocation)
    {
        mCommandList->DrawInstanced(vertexCountPerInstance, instanceCount, startVertexLocation, startInstanceLocation);
    }

    void GraphicsContext::DrawIndexedInstanced(uint32_t indexCountPerInstance, uint32_t instanceCount, uint32_t startIndexLocation, uint32_t baseVertexLocation, uint32_t startInstanceLocation)
    {
        mCommandList->DrawIndexedInstanced(indexCountPerInstance, instanceCount, startIndexLocation, baseVertexLocation, startInstanceLocation);
    }

    void GraphicsContext::Dispatch(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ)
    {
        mCommandList->Dispatch(groupCountX, groupCountY, groupCountZ);
    }

    void GraphicsContext::Dispatch1D(uint32_t threadCountX, uint32_t groupSizeX)
    {
        Dispatch(GetGroupCount(threadCountX, groupSizeX), 1, 1);
    }

    void GraphicsContext::Dispatch2D(uint32_t threadCountX, uint32_t threadCountY, uint32_t groupSizeX, uint32_t groupSizeY)
    {
        Dispatch(GetGroupCount(threadCountX, groupSizeX), GetGroupCount(threadCountY, groupSizeY), 1);
    }

    void GraphicsContext::Dispatch3D(uint32_t threadCountX, uint32_t threadCountY, uint32_t threadCountZ, uint32_t groupSizeX, uint32_t groupSizeY, uint32_t groupSizeZ)
    {
        Dispatch(GetGroupCount(threadCountX, groupSizeX), GetGroupCount(threadCountY, groupSizeY), GetGroupCount(threadCountZ, groupSizeZ));
    }

    ComputeContext::ComputeContext(Device& device)
        :Context(device, D3D12_COMMAND_LIST_TYPE_COMPUTE)
    {

    }

    void ComputeContext::SetPipeline(const PipelineInfo& pipelineBinding)
    {
        assert(pipelineBinding.mPipeline && pipelineBinding.mPipeline->mPipelineType == PipelineType::compute);

        mCommandList->SetPipelineState(pipelineBinding.mPipeline->mPipeline);
        mCommandList->SetComputeRootSignature(pipelineBinding.mPipeline->mRootSignature);

        mCurrentPipeline = pipelineBinding.mPipeline;
    }

    void ComputeContext::SetPipelineResources(uint32_t spaceId, const PipelineResourceSpace& resources)
    {
        assert(mCurrentPipeline);
        assert(resources.IsLocked());

        static const uint32_t maxNumHandlesPerBinding = 16;
        static const uint32_t singleDescriptorRangeCopyArray[maxNumHandlesPerBinding]{ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 ,1 };

        const BufferResource* cbv = resources.GetCBV();
        const auto& uavs = resources.GetUAVs();
        const auto& srvs = resources.GetSRVs();
        const uint32_t numTableHandles = static_cast<uint32_t>(uavs.size() + srvs.size());
        D3D12_CPU_DESCRIPTOR_HANDLE handles[maxNumHandlesPerBinding]{};
        uint32_t currentHandleIndex = 0;

        assert(numTableHandles <= maxNumHandlesPerBinding);

        if(cbv)
        {
            auto& cbvMapping = mCurrentPipeline->mPipelineResourceMapping.mCbvMapping[spaceId];
            assert(cbvMapping.has_value());

            mCommandList->SetComputeRootConstantBufferView(cbvMapping.value(), cbv->mVirtualAddress);
        }

        if (numTableHandles == 0)
        {
            return;
        }

        for (auto& uav : uavs)
        {
            if (uav.mResource->mType == GPUResourceType::buffer)
            {
                handles[currentHandleIndex++] = static_cast<BufferResource*>(uav.mResource)->mUAVDescriptor.mCPUHandle;
            }
            else
            {
                handles[currentHandleIndex++] = static_cast<TextureResource*>(uav.mResource)->mUAVDescriptor.mCPUHandle;
            }
        }

        for (auto& srv : srvs)
        {
            if (srv.mResource->mType == GPUResourceType::buffer)
            {
                handles[currentHandleIndex++] = static_cast<BufferResource*>(srv.mResource)->mSRVDescriptor.mCPUHandle;
            }
            else
            {
                handles[currentHandleIndex++] = static_cast<TextureResource*>(srv.mResource)->mSRVDescriptor.mCPUHandle;
            }
        }

        Descriptor blockStart = mCurrentSRVHeap->AllocateUserDescriptorBlock(numTableHandles);
        mDevice.CopyDescriptors(1, &blockStart.mCPUHandle, &numTableHandles, numTableHandles, handles, singleDescriptorRangeCopyArray, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

        auto& tableMapping = mCurrentPipeline->mPipelineResourceMapping.mTableMapping[spaceId];
        assert(tableMapping.has_value());

        mCommandList->SetComputeRootDescriptorTable(tableMapping.value(), blockStart.mGPUHandle);
    }

    void ComputeContext::Dispatch(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ)
    {
        mCommandList->Dispatch(groupCountX, groupCountY, groupCountZ);
    }

    void ComputeContext::Dispatch1D(uint32_t threadCountX, uint32_t groupSizeX)
    {
        Dispatch(GetGroupCount(threadCountX, groupSizeX), 1, 1);
    }

    void ComputeContext::Dispatch2D(uint32_t threadCountX, uint32_t threadCountY, uint32_t groupSizeX, uint32_t groupSizeY)
    {
        Dispatch(GetGroupCount(threadCountX, groupSizeX), GetGroupCount(threadCountY, groupSizeY), 1);
    }

    void ComputeContext::Dispatch3D(uint32_t threadCountX, uint32_t threadCountY, uint32_t threadCountZ, uint32_t groupSizeX, uint32_t groupSizeY, uint32_t groupSizeZ)
    {
        Dispatch(GetGroupCount(threadCountX, groupSizeX), GetGroupCount(threadCountY, groupSizeY), GetGroupCount(threadCountZ, groupSizeZ));
    }

    UploadContext::UploadContext(Device& device, std::unique_ptr<BufferResource> bufferUploadHeap, std::unique_ptr<BufferResource> textureUploadHeap)
        :Context(device, D3D12_COMMAND_LIST_TYPE_COPY)
        , mBufferUploadHeap(std::move(bufferUploadHeap))
        , mTextureUploadHeap(std::move(textureUploadHeap))
    {

    }

    UploadContext::~UploadContext()
    {
        //Upload context heaps weren't returned for some reason
        assert(mBufferUploadHeap == nullptr);
        assert(mTextureUploadHeap == nullptr);
    }

    std::unique_ptr<BufferResource> UploadContext::ReturnBufferHeap()
    {
        return std::move(mBufferUploadHeap);
    }

    std::unique_ptr<BufferResource> UploadContext::ReturnTextureHeap()
    {
        return std::move(mTextureUploadHeap);
    }

    void UploadContext::AddBufferUpload(std::unique_ptr<BufferUpload> bufferUpload)
    {
        assert(bufferUpload->mBufferDataSize <= mBufferUploadHeap->mDesc.Width);

        mBufferUploads.push_back(std::move(bufferUpload));
    }

    void UploadContext::AddTextureUpload(std::unique_ptr<TextureUpload> textureUpload)
    {
        assert(textureUpload->mTextureDataSize <= mTextureUploadHeap->mDesc.Width);

        mTextureUploads.push_back(std::move(textureUpload));
    }

    void UploadContext::ProcessUploads()
    {
        const uint32_t numBufferUploads = static_cast<uint32_t>(mBufferUploads.size());
        const uint32_t numTextureUploads = static_cast<uint32_t>(mTextureUploads.size());
        uint32_t numBuffersProcessed = 0;
        uint32_t numTexturesProcessed = 0;
        size_t bufferUploadHeapOffset = 0;
        size_t textureUploadHeapOffset = 0;

        for (numBuffersProcessed; numBuffersProcessed < numBufferUploads; numBuffersProcessed++)
        {
            BufferUpload& currentUpload = *mBufferUploads[numBuffersProcessed];

            if ((bufferUploadHeapOffset + currentUpload.mBufferDataSize) > mBufferUploadHeap->mDesc.Width)
            {
                break;
            }

            memcpy(mBufferUploadHeap->mMappedResource + bufferUploadHeapOffset, currentUpload.mBufferData.get(), currentUpload.mBufferDataSize);
            CopyBufferRegion(*currentUpload.mBuffer, 0, *mBufferUploadHeap, bufferUploadHeapOffset, currentUpload.mBufferDataSize);

            bufferUploadHeapOffset += currentUpload.mBufferDataSize;
            mBufferUploadsInProgress.push_back(currentUpload.mBuffer);
        }

        for (numTexturesProcessed; numTexturesProcessed < numTextureUploads; numTexturesProcessed++)
        {
            TextureUpload& currentUpload = *mTextureUploads[numTexturesProcessed];

            if ((textureUploadHeapOffset + currentUpload.mTextureDataSize) > mTextureUploadHeap->mDesc.Width)
            {
                break;
            }

            memcpy(mTextureUploadHeap->mMappedResource + textureUploadHeapOffset, currentUpload.mTextureData.get(), currentUpload.mTextureDataSize);
            CopyTextureRegion(*currentUpload.mTexture, *mTextureUploadHeap, textureUploadHeapOffset, currentUpload.mSubResourceLayouts, currentUpload.mNumSubResources);

            textureUploadHeapOffset += currentUpload.mTextureDataSize;
            textureUploadHeapOffset = AlignU64(textureUploadHeapOffset, 512);

            mTextureUploadsInProgress.push_back(currentUpload.mTexture);
        }

        if (numBuffersProcessed > 0)
        {
            mBufferUploads.erase(mBufferUploads.begin(), mBufferUploads.begin() + numBuffersProcessed);
        }

        if (numTexturesProcessed > 0)
        {
            mTextureUploads.erase(mTextureUploads.begin(), mTextureUploads.begin() + numTexturesProcessed);
        }
    }

    void UploadContext::ResolveProcessedUploads()
    {
        for (auto& bufferUploadInProgress : mBufferUploadsInProgress)
        {
            bufferUploadInProgress->mIsReady = true;
        }

        for (auto& textureUploadInProgress : mTextureUploadsInProgress)
        {
            textureUploadInProgress->mIsReady = true;
        }

        mBufferUploadsInProgress.clear();
        mTextureUploadsInProgress.clear();
    }

    Device::Device(HWND windowHandle, Uint2 screenSize)
    {
        InitializeDeviceResources();
        CreateWindowDependentResources(windowHandle, screenSize);

        mScreenSize = screenSize;
    }

    Device::~Device()
    {
        WaitForIdle();

        DestroyWindowDependentResources();

        for (uint32_t frameIndex = 0; frameIndex < NUM_FRAMES_IN_FLIGHT; frameIndex++)
        {
            DestroyBuffer(mUploadContexts[frameIndex]->ReturnBufferHeap());
            DestroyBuffer(mUploadContexts[frameIndex]->ReturnTextureHeap());
        }

        for (uint32_t frameIndex = 0; frameIndex < NUM_FRAMES_IN_FLIGHT; frameIndex++)
        {
            ProcessDestructions(frameIndex);
        }

        mCopyQueue = nullptr;
        mComputeQueue = nullptr;
        mGraphicsQueue = nullptr;

        mRTVStagingDescriptorHeap = nullptr;
        mDSVStagingDescriptorHeap = nullptr;
        mSRVStagingDescriptorHeap = nullptr;
        mSamplerRenderPassDescriptorHeap = nullptr;

        for (uint32_t frameIndex = 0; frameIndex < NUM_FRAMES_IN_FLIGHT; frameIndex++)
        {
            mSRVRenderPassDescriptorHeaps[frameIndex] = nullptr;
            mUploadContexts[frameIndex] = nullptr;
        }

        SafeRelease(mAllocator);
        SafeRelease(mDevice);
        SafeRelease(mDXGIFactory);

#ifdef _DEBUG
        IDXGIDebug1* pDebug = nullptr;
        if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&pDebug))))
        {
            pDebug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_FLAGS(DXGI_DEBUG_RLO_SUMMARY | DXGI_DEBUG_RLO_DETAIL | DXGI_DEBUG_RLO_IGNORE_INTERNAL));
            SafeRelease(pDebug);
        }
#endif
    }

    void Device::InitializeDeviceResources()
    {
#if defined(_DEBUG)
        ID3D12Debug* debugController;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
        {
            debugController->EnableDebugLayer();
            SafeRelease(debugController);
        }
#endif

        AssertIfFailed(CreateDXGIFactory1(IID_PPV_ARGS(&mDXGIFactory)));

        IDXGIAdapter1* adapter = nullptr;
        uint32_t bestAdapterIndex = 0;
        size_t bestAdapterMemory = 0;

        for (uint32_t adapterIndex = 0; mDXGIFactory->EnumAdapters1(adapterIndex, &adapter) != DXGI_ERROR_NOT_FOUND; adapterIndex++)
        {
            DXGI_ADAPTER_DESC1 adapterDesc;
            AssertIfFailed(adapter->GetDesc1(&adapterDesc));

            if (adapterDesc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
            {
                continue;
            }

            if (FAILED(D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_12_2, _uuidof(ID3D12Device), nullptr)))
            {
                continue;
            }

            if (adapterDesc.DedicatedVideoMemory > bestAdapterMemory)
            {
                bestAdapterIndex = adapterIndex;
                bestAdapterMemory = adapterDesc.DedicatedVideoMemory;
            }

            SafeRelease(adapter);
        }

        if (bestAdapterMemory == 0)
        {
            AssertError("Failed to find an adapter.");
        }

        mDXGIFactory->EnumAdapters1(bestAdapterIndex, &adapter);

        AssertIfFailed(D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_12_2, IID_PPV_ARGS(&mDevice)));

        D3D12MA::ALLOCATOR_DESC desc = {};
        desc.Flags = D3D12MA::ALLOCATOR_FLAG_NONE;
        desc.pDevice = mDevice;
        desc.pAdapter = adapter;

        D3D12MA::CreateAllocator(&desc, &mAllocator);

        SafeRelease(adapter);

        mGraphicsQueue = std::make_unique<Queue>(mDevice, D3D12_COMMAND_LIST_TYPE_DIRECT);
        mComputeQueue = std::make_unique<Queue>(mDevice, D3D12_COMMAND_LIST_TYPE_COMPUTE);
        mCopyQueue = std::make_unique<Queue>(mDevice, D3D12_COMMAND_LIST_TYPE_COPY);

        mRTVStagingDescriptorHeap = std::make_unique<StagingDescriptorHeap>(mDevice, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, NUM_RTV_STAGING_DESCRIPTORS);
        mDSVStagingDescriptorHeap = std::make_unique<StagingDescriptorHeap>(mDevice, D3D12_DESCRIPTOR_HEAP_TYPE_DSV, NUM_DSV_STAGING_DESCRIPTORS);
        mSRVStagingDescriptorHeap = std::make_unique<StagingDescriptorHeap>(mDevice, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, NUM_SRV_STAGING_DESCRIPTORS);
        mSamplerRenderPassDescriptorHeap = std::make_unique<RenderPassDescriptorHeap>(mDevice, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, 0, NUM_SAMPLER_DESCRIPTORS);

        for (uint32_t frameIndex = 0; frameIndex < NUM_FRAMES_IN_FLIGHT; frameIndex++)
        {
            mSRVRenderPassDescriptorHeaps[frameIndex] = std::make_unique<RenderPassDescriptorHeap>(mDevice, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, NUM_RESERVED_SRV_DESCRIPTORS, NUM_SRV_RENDER_PASS_USER_DESCRIPTORS);
            mImguiDescriptors[frameIndex] = mSRVRenderPassDescriptorHeaps[frameIndex]->GetReservedDescriptor(IMGUI_RESERVED_DESCRIPTOR_INDEX);
        }

        CreateSamplers();

        BufferCreationDesc uploadBufferDesc;
        uploadBufferDesc.mSize = 10 * 1024 * 1024;
        uploadBufferDesc.mAccessFlags = BufferAccessFlags::hostWritable;

        BufferCreationDesc uploadTextureDesc;
        uploadTextureDesc.mSize = 40 * 1024 * 1024;
        uploadTextureDesc.mAccessFlags = BufferAccessFlags::hostWritable;

        for (uint32_t frameIndex = 0; frameIndex < NUM_FRAMES_IN_FLIGHT; frameIndex++)
        {
            mUploadContexts[frameIndex] = std::make_unique<UploadContext>(*this, CreateBuffer(uploadBufferDesc), CreateBuffer(uploadTextureDesc));
        }

        //The -1 and starting at index 1 accounts for the imgui descriptor.
        mFreeReservedDescriptorIndices.resize(NUM_RESERVED_SRV_DESCRIPTORS - 1);
        std::iota(mFreeReservedDescriptorIndices.begin(), mFreeReservedDescriptorIndices.end(), 1);
    }

    void Device::CreateSamplers()
    {
        D3D12_SAMPLER_DESC samplerDescs[NUM_SAMPLER_DESCRIPTORS]{};
        samplerDescs[0].Filter = D3D12_FILTER_ANISOTROPIC;
        samplerDescs[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samplerDescs[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samplerDescs[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samplerDescs[0].BorderColor[0] = samplerDescs[0].BorderColor[1] = samplerDescs[0].BorderColor[2] = samplerDescs[0].BorderColor[3] = 0.0f;
        samplerDescs[0].MipLODBias = 0.0f;
        samplerDescs[0].MaxAnisotropy = 16;
        samplerDescs[0].ComparisonFunc = D3D12_COMPARISON_FUNC_NONE;
        samplerDescs[0].MinLOD = 0;
        samplerDescs[0].MaxLOD = D3D12_FLOAT32_MAX;

        samplerDescs[1].Filter = D3D12_FILTER_ANISOTROPIC;
        samplerDescs[1].AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        samplerDescs[1].AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        samplerDescs[1].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        samplerDescs[1].BorderColor[0] = samplerDescs[1].BorderColor[1] = samplerDescs[1].BorderColor[2] = samplerDescs[1].BorderColor[3] = 0.0f;
        samplerDescs[1].MipLODBias = 0.0f;
        samplerDescs[1].MaxAnisotropy = 16;
        samplerDescs[1].ComparisonFunc = D3D12_COMPARISON_FUNC_NONE;
        samplerDescs[1].MinLOD = 0;
        samplerDescs[1].MaxLOD = D3D12_FLOAT32_MAX;

        samplerDescs[2].Filter = D3D12_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR;
        samplerDescs[2].AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samplerDescs[2].AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samplerDescs[2].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samplerDescs[2].BorderColor[0] = samplerDescs[2].BorderColor[1] = samplerDescs[2].BorderColor[2] = samplerDescs[2].BorderColor[3] = 0.0f;
        samplerDescs[2].MipLODBias = 0.0f;
        samplerDescs[2].MaxAnisotropy = 0;
        samplerDescs[2].ComparisonFunc = D3D12_COMPARISON_FUNC_NONE;
        samplerDescs[2].MinLOD = 0;
        samplerDescs[2].MaxLOD = D3D12_FLOAT32_MAX;

        samplerDescs[3].Filter = D3D12_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR;
        samplerDescs[3].AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        samplerDescs[3].AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        samplerDescs[3].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        samplerDescs[3].BorderColor[0] = samplerDescs[3].BorderColor[1] = samplerDescs[3].BorderColor[2] = samplerDescs[3].BorderColor[3] = 0.0f;
        samplerDescs[3].MipLODBias = 0.0f;
        samplerDescs[3].MaxAnisotropy = 0;
        samplerDescs[3].ComparisonFunc = D3D12_COMPARISON_FUNC_NONE;
        samplerDescs[3].MinLOD = 0;
        samplerDescs[3].MaxLOD = D3D12_FLOAT32_MAX;

        samplerDescs[4].Filter = D3D12_FILTER_COMPARISON_MIN_MAG_MIP_POINT;
        samplerDescs[4].AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samplerDescs[4].AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samplerDescs[4].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samplerDescs[4].BorderColor[0] = samplerDescs[4].BorderColor[1] = samplerDescs[4].BorderColor[2] = samplerDescs[4].BorderColor[3] = 0.0f;
        samplerDescs[4].MipLODBias = 0.0f;
        samplerDescs[4].MaxAnisotropy = 0;
        samplerDescs[4].ComparisonFunc = D3D12_COMPARISON_FUNC_NONE;
        samplerDescs[4].MinLOD = 0;
        samplerDescs[4].MaxLOD = D3D12_FLOAT32_MAX;

        samplerDescs[5].Filter = D3D12_FILTER_COMPARISON_MIN_MAG_MIP_POINT;
        samplerDescs[5].AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        samplerDescs[5].AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        samplerDescs[5].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        samplerDescs[5].BorderColor[0] = samplerDescs[5].BorderColor[1] = samplerDescs[5].BorderColor[2] = samplerDescs[5].BorderColor[3] = 0.0f;
        samplerDescs[5].MipLODBias = 0.0f;
        samplerDescs[5].MaxAnisotropy = 0;
        samplerDescs[5].ComparisonFunc = D3D12_COMPARISON_FUNC_NONE;
        samplerDescs[5].MinLOD = 0;
        samplerDescs[5].MaxLOD = D3D12_FLOAT32_MAX;

        Descriptor samplerDescriptorBlock = mSamplerRenderPassDescriptorHeap->AllocateUserDescriptorBlock(NUM_SAMPLER_DESCRIPTORS);
        D3D12_CPU_DESCRIPTOR_HANDLE currentSamplerDescriptor = samplerDescriptorBlock.mCPUHandle;

        for (uint32_t samplerIndex = 0; samplerIndex < NUM_SAMPLER_DESCRIPTORS; samplerIndex++)
        {
            mDevice->CreateSampler(&samplerDescs[samplerIndex], currentSamplerDescriptor);
            currentSamplerDescriptor.ptr += mSamplerRenderPassDescriptorHeap->GetDescriptorSize();
        }
    }

    void Device::CreateWindowDependentResources(HWND windowHandle, Uint2 screenSize)
    {
        DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
        ZeroMemory(&swapChainDesc, sizeof(swapChainDesc));
        swapChainDesc.Width = lround(screenSize.x);
        swapChainDesc.Height = lround(screenSize.y);
        swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        swapChainDesc.Stereo = false;
        swapChainDesc.SampleDesc.Count = 1;
        swapChainDesc.SampleDesc.Quality = 0;
        swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swapChainDesc.BufferCount = NUM_BACK_BUFFERS;
        swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        swapChainDesc.Flags = 0;
        swapChainDesc.Scaling = DXGI_SCALING_NONE;
        swapChainDesc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;

        IDXGISwapChain1* swapChain = nullptr;
        AssertIfFailed(mDXGIFactory->CreateSwapChainForHwnd(mGraphicsQueue->GetDeviceQueue(), windowHandle, &swapChainDesc, nullptr, nullptr, &swapChain));
        AssertIfFailed(swapChain->QueryInterface(__uuidof(IDXGISwapChain3), (void**)&mSwapChain));
        SafeRelease(swapChain);

        for (uint32_t bufferIndex = 0; bufferIndex < NUM_BACK_BUFFERS; bufferIndex++)
        {
            ID3D12Resource* backBufferResource = nullptr;
            Descriptor backBufferRTVHandle = mRTVStagingDescriptorHeap->GetNewDescriptor();

            D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
            rtvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
            rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
            rtvDesc.Texture2D.MipSlice = 0;
            rtvDesc.Texture2D.PlaneSlice = 0;

            AssertIfFailed(mSwapChain->GetBuffer(bufferIndex, IID_PPV_ARGS(&backBufferResource)));
            mDevice->CreateRenderTargetView(backBufferResource, &rtvDesc, backBufferRTVHandle.mCPUHandle);

            mBackBuffers[bufferIndex] = std::make_unique<TextureResource>();
            mBackBuffers[bufferIndex]->mDesc = backBufferResource->GetDesc();
            mBackBuffers[bufferIndex]->mResource = backBufferResource;
            mBackBuffers[bufferIndex]->mState = D3D12_RESOURCE_STATE_PRESENT;
            mBackBuffers[bufferIndex]->mRTVDescriptor = backBufferRTVHandle;
        }

        mFrameId = 0;
    }

    void Device::DestroyWindowDependentResources()
    {
        for (uint32_t bufferIndex = 0; bufferIndex < NUM_BACK_BUFFERS; bufferIndex++)
        {
            mRTVStagingDescriptorHeap->FreeDescriptor(mBackBuffers[bufferIndex]->mRTVDescriptor);
            SafeRelease(mBackBuffers[bufferIndex]->mResource);
            mBackBuffers[bufferIndex] = nullptr;
        }

        SafeRelease(mSwapChain);
    }

    void Device::ProcessDestructions(uint32_t frameIndex)
    {
        auto& destructionQueueForFrame = mDestructionQueues[frameIndex];

        for(auto& bufferToDestroy : destructionQueueForFrame.mBuffersToDestroy)
        {
            if (bufferToDestroy->mCBVDescriptor.IsValid())
            {
                mSRVStagingDescriptorHeap->FreeDescriptor(bufferToDestroy->mCBVDescriptor);
            }

            if (bufferToDestroy->mSRVDescriptor.IsValid())
            {
                mSRVStagingDescriptorHeap->FreeDescriptor(bufferToDestroy->mSRVDescriptor);
                mFreeReservedDescriptorIndices.push_back(bufferToDestroy->mDescriptorHeapIndex);
            }

            if (bufferToDestroy->mUAVDescriptor.IsValid())
            {
                mSRVStagingDescriptorHeap->FreeDescriptor(bufferToDestroy->mUAVDescriptor);
            }

            if (bufferToDestroy->mMappedResource != nullptr)
            {
                bufferToDestroy->mResource->Unmap(0, nullptr);
            }

            SafeRelease(bufferToDestroy->mResource);
            SafeRelease(bufferToDestroy->mAllocation);
        }

        for (auto& textureToDestroy : destructionQueueForFrame.mTexturesToDestroy)
        {
            if (textureToDestroy->mRTVDescriptor.IsValid())
            {
                mRTVStagingDescriptorHeap->FreeDescriptor(textureToDestroy->mRTVDescriptor);
            }

            if (textureToDestroy->mDSVDescriptor.IsValid())
            {
                mDSVStagingDescriptorHeap->FreeDescriptor(textureToDestroy->mDSVDescriptor);
            }

            if (textureToDestroy->mSRVDescriptor.IsValid())
            {
                mSRVStagingDescriptorHeap->FreeDescriptor(textureToDestroy->mSRVDescriptor);
                mFreeReservedDescriptorIndices.push_back(textureToDestroy->mDescriptorHeapIndex);
            }

            if (textureToDestroy->mUAVDescriptor.IsValid())
            {
                mSRVStagingDescriptorHeap->FreeDescriptor(textureToDestroy->mUAVDescriptor);
            }

            SafeRelease(textureToDestroy->mResource);
            SafeRelease(textureToDestroy->mAllocation);
        }

        for (auto& pipelineToDestroy : destructionQueueForFrame.mPipelinesToDestroy)
        {
            SafeRelease(pipelineToDestroy->mRootSignature);
            SafeRelease(pipelineToDestroy->mPipeline);
        }

        destructionQueueForFrame.mBuffersToDestroy.clear();
        destructionQueueForFrame.mTexturesToDestroy.clear();
        destructionQueueForFrame.mPipelinesToDestroy.clear();
        destructionQueueForFrame.mContextsToDestroy.clear();
    }

    void Device::CopySRVHandleToReservedTable(Descriptor srvHandle, uint32_t index)
    {
        for (uint32_t frameIndex = 0; frameIndex < NUM_FRAMES_IN_FLIGHT; frameIndex++)
        {
            Descriptor targetDescriptor = mSRVRenderPassDescriptorHeaps[frameIndex]->GetReservedDescriptor(index);

            CopyDescriptorsSimple(1, targetDescriptor.mCPUHandle, srvHandle.mCPUHandle, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        }
    }

    void Device::BeginFrame()
    {
        mFrameId = (mFrameId + 1) % NUM_FRAMES_IN_FLIGHT;

        //wait on fences from 2 frames ago
        mGraphicsQueue->WaitForFenceCPUBlocking(mEndOfFrameFences[mFrameId].mGraphicsQueueFence);
        mComputeQueue->WaitForFenceCPUBlocking(mEndOfFrameFences[mFrameId].mComputeQueueFence);
        mCopyQueue->WaitForFenceCPUBlocking(mEndOfFrameFences[mFrameId].mCopyQueueFence);

        ProcessDestructions(mFrameId);

        mUploadContexts[mFrameId]->ResolveProcessedUploads();
        mUploadContexts[mFrameId]->Reset();

        mContextSubmissions[mFrameId].clear();
    }

    void Device::EndFrame()
    {
        mUploadContexts[mFrameId]->ProcessUploads();
        SubmitContextWork(*mUploadContexts[mFrameId]);

        mEndOfFrameFences[mFrameId].mComputeQueueFence = mComputeQueue->SignalFence();
        mEndOfFrameFences[mFrameId].mCopyQueueFence = mCopyQueue->SignalFence();
    }

    void Device::Present()
    {
        mSwapChain->Present(0, 0);
        mEndOfFrameFences[mFrameId].mGraphicsQueueFence = mGraphicsQueue->SignalFence();
    }

    void Device::CopyDescriptorsSimple(uint32_t numDescriptors, D3D12_CPU_DESCRIPTOR_HANDLE destDescriptorRangeStart, D3D12_CPU_DESCRIPTOR_HANDLE srcDescriptorRangeStart, D3D12_DESCRIPTOR_HEAP_TYPE descriptorType)
    {
        mDevice->CopyDescriptorsSimple(numDescriptors, destDescriptorRangeStart, srcDescriptorRangeStart, descriptorType);
    }

    void Device::CopyDescriptors(uint32_t numDestDescriptorRanges, const D3D12_CPU_DESCRIPTOR_HANDLE* destDescriptorRangeStarts, const uint32_t* destDescriptorRangeSizes,
        uint32_t numSrcDescriptorRanges, const D3D12_CPU_DESCRIPTOR_HANDLE* srcDescriptorRangeStarts, const uint32_t* srcDescriptorRangeSizes, D3D12_DESCRIPTOR_HEAP_TYPE descriptorType)
    {
        mDevice->CopyDescriptors(numDestDescriptorRanges, destDescriptorRangeStarts, destDescriptorRangeSizes, numSrcDescriptorRanges, srcDescriptorRangeStarts, srcDescriptorRangeSizes, descriptorType);
    }

    TextureResource& Device::GetCurrentBackBuffer()
    {
        return *mBackBuffers[mSwapChain->GetCurrentBackBufferIndex()];
    }

    std::unique_ptr<BufferResource> Device::CreateBuffer(const BufferCreationDesc& desc)
    {
        std::unique_ptr<BufferResource> newBuffer = std::make_unique<BufferResource>();
        newBuffer->mDesc.Width = AlignU32(static_cast<uint32_t>(desc.mSize), 256);
        newBuffer->mDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        newBuffer->mDesc.Alignment = 0;
        newBuffer->mDesc.Height = 1;
        newBuffer->mDesc.DepthOrArraySize = 1;
        newBuffer->mDesc.MipLevels = 1;
        newBuffer->mDesc.Format = DXGI_FORMAT_UNKNOWN;
        newBuffer->mDesc.SampleDesc.Count = 1;
        newBuffer->mDesc.SampleDesc.Quality = 0;
        newBuffer->mDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        newBuffer->mDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
        newBuffer->mStride = desc.mStride;

        uint32_t numElements = static_cast<uint32_t>(newBuffer->mStride > 0 ? desc.mSize / newBuffer->mStride : 1);
        bool isHostVisible = ((desc.mAccessFlags & BufferAccessFlags::hostWritable) == BufferAccessFlags::hostWritable);
        bool hasCBV = ((desc.mViewFlags & BufferViewFlags::cbv) == BufferViewFlags::cbv);
        bool hasSRV = ((desc.mViewFlags & BufferViewFlags::srv) == BufferViewFlags::srv);
        bool hasUAV = ((desc.mViewFlags & BufferViewFlags::uav) == BufferViewFlags::uav);

        D3D12_RESOURCE_STATES resourceState = D3D12_RESOURCE_STATE_COPY_DEST;

        if (isHostVisible)
        {
            resourceState = D3D12_RESOURCE_STATE_GENERIC_READ;
        }

        newBuffer->mState = resourceState;

        D3D12MA::ALLOCATION_DESC allocationDesc{};
        allocationDesc.HeapType = isHostVisible ? D3D12_HEAP_TYPE_UPLOAD : D3D12_HEAP_TYPE_DEFAULT;

        mAllocator->CreateResource(&allocationDesc, &newBuffer->mDesc, resourceState, nullptr, &newBuffer->mAllocation, IID_PPV_ARGS(&newBuffer->mResource));
        newBuffer->mVirtualAddress = newBuffer->mResource->GetGPUVirtualAddress();

        if (hasCBV)
        {
            D3D12_CONSTANT_BUFFER_VIEW_DESC constantBufferViewDesc = {};
            constantBufferViewDesc.BufferLocation = newBuffer->mResource->GetGPUVirtualAddress();
            constantBufferViewDesc.SizeInBytes = static_cast<uint32_t>(newBuffer->mDesc.Width);

            newBuffer->mCBVDescriptor = mSRVStagingDescriptorHeap->GetNewDescriptor();
            mDevice->CreateConstantBufferView(&constantBufferViewDesc, newBuffer->mCBVDescriptor.mCPUHandle);
        }

        if (hasSRV)
        {
            D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
            srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
            srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srvDesc.Format = desc.mIsRawAccess ? DXGI_FORMAT_R32_TYPELESS : DXGI_FORMAT_UNKNOWN;
            srvDesc.Buffer.FirstElement = 0;
            srvDesc.Buffer.NumElements = static_cast<uint32_t>(desc.mIsRawAccess ? (desc.mSize / 4) : numElements);
            srvDesc.Buffer.StructureByteStride = desc.mIsRawAccess ? 0 : newBuffer->mStride;
            srvDesc.Buffer.Flags = desc.mIsRawAccess ? D3D12_BUFFER_SRV_FLAG_RAW : D3D12_BUFFER_SRV_FLAG_NONE;

            newBuffer->mSRVDescriptor = mSRVStagingDescriptorHeap->GetNewDescriptor();
            mDevice->CreateShaderResourceView(newBuffer->mResource, &srvDesc, newBuffer->mSRVDescriptor.mCPUHandle);

            newBuffer->mDescriptorHeapIndex = mFreeReservedDescriptorIndices.back();
            mFreeReservedDescriptorIndices.pop_back();

            CopySRVHandleToReservedTable(newBuffer->mSRVDescriptor, newBuffer->mDescriptorHeapIndex);
        }

        if (hasUAV)
        {
            D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
            uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
            uavDesc.Format = desc.mIsRawAccess ? DXGI_FORMAT_R32_TYPELESS : DXGI_FORMAT_UNKNOWN;
            uavDesc.Buffer.CounterOffsetInBytes = 0;
            uavDesc.Buffer.FirstElement = 0;
            uavDesc.Buffer.NumElements = static_cast<uint32_t>(desc.mIsRawAccess ? (desc.mSize / 4) : numElements);
            uavDesc.Buffer.StructureByteStride = desc.mIsRawAccess ? 0 : newBuffer->mStride;
            uavDesc.Buffer.Flags = desc.mIsRawAccess ? D3D12_BUFFER_UAV_FLAG_RAW : D3D12_BUFFER_UAV_FLAG_NONE;

            newBuffer->mUAVDescriptor = mSRVStagingDescriptorHeap->GetNewDescriptor();
            mDevice->CreateUnorderedAccessView(newBuffer->mResource, nullptr, &uavDesc, newBuffer->mUAVDescriptor.mCPUHandle);
        }

        if (isHostVisible)
        {
            newBuffer->mResource->Map(0, nullptr, reinterpret_cast<void**>(&newBuffer->mMappedResource));
        }

        return newBuffer;
    }

    std::unique_ptr<TextureResource> Device::CreateTexture(const TextureCreationDesc& desc)
    {
        D3D12_RESOURCE_DESC textureDesc = desc.mResourceDesc;
        textureDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

        bool hasRTV = ((desc.mViewFlags & TextureViewFlags::rtv) == TextureViewFlags::rtv);
        bool hasDSV = ((desc.mViewFlags & TextureViewFlags::dsv) == TextureViewFlags::dsv);
        bool hasSRV = ((desc.mViewFlags & TextureViewFlags::srv) == TextureViewFlags::srv);
        bool hasUAV = ((desc.mViewFlags & TextureViewFlags::uav) == TextureViewFlags::uav);

        D3D12_RESOURCE_STATES resourceState = D3D12_RESOURCE_STATE_COPY_DEST;
        DXGI_FORMAT resourceFormat = textureDesc.Format;
        DXGI_FORMAT shaderResourceViewFormat = textureDesc.Format;

        if (hasRTV)
        {
            textureDesc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
            resourceState = D3D12_RESOURCE_STATE_RENDER_TARGET;
        }

        if (hasDSV)
        {
            switch (desc.mResourceDesc.Format)
            {
            case DXGI_FORMAT_D16_UNORM:
                resourceFormat = DXGI_FORMAT_R16_TYPELESS;
                shaderResourceViewFormat = DXGI_FORMAT_R16_UNORM;
                break;
            case DXGI_FORMAT_D24_UNORM_S8_UINT:
                resourceFormat = DXGI_FORMAT_R24G8_TYPELESS;
                shaderResourceViewFormat = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
                break;
            case DXGI_FORMAT_D32_FLOAT:
                resourceFormat = DXGI_FORMAT_R32_TYPELESS;
                shaderResourceViewFormat = DXGI_FORMAT_R32_FLOAT;
                break;
            case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
                resourceFormat = DXGI_FORMAT_R32G8X24_TYPELESS;
                shaderResourceViewFormat = DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
                break;
            default:
                AssertError("Bad depth stencil format.");
                break;
            }

            textureDesc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
            resourceState = D3D12_RESOURCE_STATE_DEPTH_WRITE;
        }

        if (hasUAV)
        {
            textureDesc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
            resourceState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        }

        textureDesc.Format = resourceFormat;

        std::unique_ptr<TextureResource> newTexture = std::make_unique<TextureResource>();
        newTexture->mDesc = textureDesc;
        newTexture->mState = resourceState;

        D3D12_CLEAR_VALUE clearValue = {};
        clearValue.Format = desc.mResourceDesc.Format;

        if (hasDSV)
        {
            clearValue.DepthStencil.Depth = 1.0f;
        }

        D3D12MA::ALLOCATION_DESC allocationDesc{};
        allocationDesc.HeapType = D3D12_HEAP_TYPE_DEFAULT;

        mAllocator->CreateResource(&allocationDesc, &textureDesc, resourceState, (!hasRTV && !hasDSV) ? nullptr : &clearValue, &newTexture->mAllocation, IID_PPV_ARGS(&newTexture->mResource));

        if (hasSRV)
        {
            newTexture->mSRVDescriptor = mSRVStagingDescriptorHeap->GetNewDescriptor();

            if (hasDSV)
            {
                D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
                srvDesc.Format = shaderResourceViewFormat;
                srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                srvDesc.Texture2D.MipLevels = 1;
                srvDesc.Texture2D.MostDetailedMip = 0;
                srvDesc.Texture2D.PlaneSlice = 0;
                srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
                srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;

                mDevice->CreateShaderResourceView(newTexture->mResource, &srvDesc, newTexture->mSRVDescriptor.mCPUHandle);
            }
            else
            {
                D3D12_SHADER_RESOURCE_VIEW_DESC* srvDescPointer = nullptr;
                D3D12_SHADER_RESOURCE_VIEW_DESC shaderResourceViewDesc = {};
                bool isCubeMap = desc.mResourceDesc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D && desc.mResourceDesc.DepthOrArraySize == 6;

                if (isCubeMap)
                {
                    shaderResourceViewDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
                    shaderResourceViewDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                    shaderResourceViewDesc.TextureCube.MostDetailedMip = 0;
                    shaderResourceViewDesc.TextureCube.MipLevels = desc.mResourceDesc.MipLevels;
                    shaderResourceViewDesc.TextureCube.ResourceMinLODClamp = 0.0f;
                    srvDescPointer = &shaderResourceViewDesc;
                }

                mDevice->CreateShaderResourceView(newTexture->mResource, srvDescPointer, newTexture->mSRVDescriptor.mCPUHandle);
            }

            newTexture->mDescriptorHeapIndex = mFreeReservedDescriptorIndices.back();
            mFreeReservedDescriptorIndices.pop_back();

            CopySRVHandleToReservedTable(newTexture->mSRVDescriptor, newTexture->mDescriptorHeapIndex);
        }

        if (hasRTV)
        {
            newTexture->mRTVDescriptor = mRTVStagingDescriptorHeap->GetNewDescriptor();
            mDevice->CreateRenderTargetView(newTexture->mResource, nullptr, newTexture->mRTVDescriptor.mCPUHandle);
        }

        if (hasDSV)
        {
            D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
            dsvDesc.Flags = D3D12_DSV_FLAG_NONE;
            dsvDesc.Format = desc.mResourceDesc.Format;
            dsvDesc.Texture2D.MipSlice = 0;
            dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;

            newTexture->mDSVDescriptor = mDSVStagingDescriptorHeap->GetNewDescriptor();
            mDevice->CreateDepthStencilView(newTexture->mResource, &dsvDesc, newTexture->mDSVDescriptor.mCPUHandle);
        }

        if (hasUAV)
        {
            newTexture->mUAVDescriptor = mSRVStagingDescriptorHeap->GetNewDescriptor();
            mDevice->CreateUnorderedAccessView(newTexture->mResource, nullptr, nullptr, newTexture->mUAVDescriptor.mCPUHandle);
        }

        newTexture->mIsReady = (hasRTV || hasDSV);

        return newTexture;
    }

    std::unique_ptr<TextureResource> Device::CreateTextureFromFile(const std::string& texturePath)
    {
        auto s2ws = [](const std::string& s)
        {
            //yoink https://stackoverflow.com/questions/27220/how-to-convert-stdstring-to-lpcwstr-in-c-unicode
            int32_t len = 0;
            int32_t slength = (int32_t)s.length() + 1;
            len = MultiByteToWideChar(CP_ACP, 0, s.c_str(), slength, 0, 0);
            wchar_t* buf = new wchar_t[len];
            MultiByteToWideChar(CP_ACP, 0, s.c_str(), slength, buf, len);
            std::wstring r(buf);
            delete[] buf;
            return r;
        };

        std::unique_ptr<DirectX::ScratchImage> imageData = std::make_unique<DirectX::ScratchImage>();
        HRESULT loadResult = DirectX::LoadFromDDSFile(s2ws(texturePath).c_str(), DirectX::DDS_FLAGS_NONE, nullptr, *imageData);
        assert(loadResult == S_OK);

        const DirectX::TexMetadata& textureMetaData = imageData->GetMetadata();
        DXGI_FORMAT textureFormat = textureMetaData.format;
        bool is3DTexture = textureMetaData.dimension == DirectX::TEX_DIMENSION_TEXTURE3D;

        TextureCreationDesc desc;
        desc.mResourceDesc.Format = textureFormat;
        desc.mResourceDesc.Width = textureMetaData.width;
        desc.mResourceDesc.Height = static_cast<UINT>(textureMetaData.height);
        desc.mResourceDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
        desc.mResourceDesc.DepthOrArraySize = static_cast<UINT16>(is3DTexture ? textureMetaData.depth : textureMetaData.arraySize);
        desc.mResourceDesc.MipLevels = static_cast<UINT16>(textureMetaData.mipLevels);
        desc.mResourceDesc.SampleDesc.Count = 1;
        desc.mResourceDesc.SampleDesc.Quality = 0;
        desc.mResourceDesc.Dimension = is3DTexture ? D3D12_RESOURCE_DIMENSION_TEXTURE3D : D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.mResourceDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        desc.mResourceDesc.Alignment = 0;
        desc.mViewFlags = TextureViewFlags::srv;

        auto newTexture = CreateTexture(desc);
        auto textureUpload = std::make_unique<TextureUpload>();

        UINT numRows[MAX_TEXTURE_SUBRESOURCE_COUNT];
        uint64_t rowSizesInBytes[MAX_TEXTURE_SUBRESOURCE_COUNT];

        textureUpload->mTexture = newTexture.get();
        textureUpload->mNumSubResources = static_cast<uint32_t>(textureMetaData.mipLevels * textureMetaData.arraySize);

        mDevice->GetCopyableFootprints(&desc.mResourceDesc, 0, textureUpload->mNumSubResources, 0, textureUpload->mSubResourceLayouts.data(), numRows, rowSizesInBytes, &textureUpload->mTextureDataSize);

        textureUpload->mTextureData = std::make_unique<uint8_t[]>(textureUpload->mTextureDataSize);

        for (uint64_t arrayIndex = 0; arrayIndex < textureMetaData.arraySize; arrayIndex++)
        {
            for (uint64_t mipIndex = 0; mipIndex < textureMetaData.mipLevels; mipIndex++)
            {
                const uint64_t subResourceIndex = mipIndex + (arrayIndex * textureMetaData.mipLevels);

                const D3D12_PLACED_SUBRESOURCE_FOOTPRINT& subResourceLayout = textureUpload->mSubResourceLayouts[subResourceIndex];
                const uint64_t subResourceHeight = numRows[subResourceIndex];
                const uint64_t subResourcePitch = AlignU32(subResourceLayout.Footprint.RowPitch, D3D12_TEXTURE_DATA_PITCH_ALIGNMENT);
                const uint64_t subResourceDepth = subResourceLayout.Footprint.Depth;
                uint8_t* destinationSubResourceMemory = textureUpload->mTextureData.get() + subResourceLayout.Offset;

                for (uint64_t sliceIndex = 0; sliceIndex < subResourceDepth; sliceIndex++)
                {
                    const DirectX::Image* subImage = imageData->GetImage(mipIndex, arrayIndex, sliceIndex);
                    const uint8_t* sourceSubResourceMemory = subImage->pixels;

                    for (uint64_t height = 0; height < subResourceHeight; height++)
                    {
                        memcpy(destinationSubResourceMemory, sourceSubResourceMemory, (std::min)(subResourcePitch, subImage->rowPitch));
                        destinationSubResourceMemory += subResourcePitch;
                        sourceSubResourceMemory += subImage->rowPitch;
                    }
                }
            }
        }

        mUploadContexts[mFrameId]->AddTextureUpload(std::move(textureUpload));

        return newTexture;
    }

    std::unique_ptr<Shader> Device::CreateShader(const ShaderCreationDesc& desc)
    {
        IDxcUtils* dxcUtils = nullptr;
        IDxcCompiler3* dxcCompiler = nullptr;
        IDxcIncludeHandler* dxcIncludeHandler = nullptr;

        DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&dxcUtils));
        DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&dxcCompiler));
        dxcUtils->CreateDefaultIncludeHandler(&dxcIncludeHandler);

        std::wstring sourcePath;
        sourcePath.append(SHADER_SOURCE_PATH);
        sourcePath.append(desc.mShaderName);

        IDxcBlobEncoding* sourceBlobEncoding = nullptr;
        dxcUtils->LoadFile(sourcePath.c_str(), nullptr, &sourceBlobEncoding);

        DxcBuffer sourceBuffer{};
        sourceBuffer.Ptr = sourceBlobEncoding->GetBufferPointer();
        sourceBuffer.Size = sourceBlobEncoding->GetBufferSize();
        sourceBuffer.Encoding = DXC_CP_ACP;

        LPCWSTR target = nullptr;

        switch (desc.mType)
        {
        case ShaderType::vertex:
            target = L"vs_6_6";
            break;
        case ShaderType::pixel:
            target = L"ps_6_6";
            break;
        case ShaderType::compute:
            target = L"cs_6_6";
            break;
        default:
            AssertError("Unimplemented shader type.");
            break;
        }

        std::vector<LPCWSTR> arguments;
        arguments.reserve(8);

        arguments.push_back(desc.mShaderName.c_str());
        arguments.push_back(L"-E");
        arguments.push_back(desc.mEntryPoint.c_str());
        arguments.push_back(L"-T");
        arguments.push_back(target);
        arguments.push_back(L"-Zi");
        arguments.push_back(L"-WX");
        arguments.push_back(L"-Qstrip_reflect");

        IDxcResult* compilationResults = nullptr;
        dxcCompiler->Compile(&sourceBuffer, arguments.data(), static_cast<uint32_t>(arguments.size()), dxcIncludeHandler, IID_PPV_ARGS(&compilationResults));

        IDxcBlobUtf8* errors = nullptr;
        HRESULT getCompilationResults = compilationResults->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&errors), nullptr);

        if (FAILED(getCompilationResults))
        {
            AssertError("Failed to get compilation result.");
        }

        if (errors != nullptr && errors->GetStringLength() != 0)
        {
            wprintf(L"Shader compilation error:\n%S\n", errors->GetStringPointer());
            AssertError("Shader compilation error");
        }

        HRESULT statusResult;
        compilationResults->GetStatus(&statusResult);
        if (FAILED(statusResult))
        {
            AssertError("Shader compilation failed");
        }

        std::wstring dxilPath;
        std::wstring pdbPath;

        dxilPath.append(SHADER_OUTPUT_PATH);
        dxilPath.append(desc.mShaderName);
        dxilPath.erase(dxilPath.end() - 5, dxilPath.end());
        dxilPath.append(L".dxil");

        pdbPath = dxilPath;
        pdbPath.append(L".pdb");

        IDxcBlob* shaderBlob = nullptr;
        compilationResults->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&shaderBlob), nullptr);
        if (shaderBlob != nullptr)
        {
            FILE* fp = nullptr;

            _wfopen_s(&fp, dxilPath.c_str(), L"wb");
            assert(fp);

            fwrite(shaderBlob->GetBufferPointer(), shaderBlob->GetBufferSize(), 1, fp);
            fclose(fp);
        }

        IDxcBlob* pdbBlob = nullptr;
        compilationResults->GetOutput(DXC_OUT_PDB, IID_PPV_ARGS(&pdbBlob), nullptr);
        {
            FILE* fp = nullptr;

            _wfopen_s(&fp, pdbPath.c_str(), L"wb");

            assert(fp);

            fwrite(pdbBlob->GetBufferPointer(), pdbBlob->GetBufferSize(), 1, fp);
            fclose(fp);
        }

        SafeRelease(pdbBlob);
        SafeRelease(errors);
        SafeRelease(compilationResults);
        SafeRelease(dxcIncludeHandler);
        SafeRelease(dxcCompiler);
        SafeRelease(dxcUtils);

        std::unique_ptr<Shader> shader = std::make_unique<Shader>();
        shader->mShaderBlob = shaderBlob;

        return shader;
    }

    ID3D12RootSignature* Device::CreateRootSignature(const PipelineResourceLayout& layout, PipelineResourceMapping& resourceMapping)
    {
        std::vector<D3D12_ROOT_PARAMETER1> rootParameters;
        std::array<std::vector<D3D12_DESCRIPTOR_RANGE1>, NUM_RESOURCE_SPACES> desciptorRanges;

        for (uint32_t spaceId = 0; spaceId < NUM_RESOURCE_SPACES; spaceId++)
        {
            PipelineResourceSpace* currentSpace = layout.mSpaces[spaceId];
            std::vector<D3D12_DESCRIPTOR_RANGE1>& currentDescriptorRange = desciptorRanges[spaceId];

            if (currentSpace)
            {
                const BufferResource* cbv = currentSpace->GetCBV();
                auto& uavs = currentSpace->GetUAVs();
                auto& srvs = currentSpace->GetSRVs();

                if (cbv)
                {
                    D3D12_ROOT_PARAMETER1 rootParameter{};
                    rootParameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
                    rootParameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
                    rootParameter.Descriptor.RegisterSpace = spaceId;
                    rootParameter.Descriptor.ShaderRegister = 0;

                    resourceMapping.mCbvMapping[spaceId] = static_cast<uint32_t>(rootParameters.size());
                    rootParameters.push_back(rootParameter);
                }

                if (uavs.empty() && srvs.empty())
                {
                    continue;
                }

                for (auto& uav : uavs)
                {
                    D3D12_DESCRIPTOR_RANGE1 range{};
                    range.BaseShaderRegister = uav.mBindingIndex;
                    range.NumDescriptors = 1;
                    range.OffsetInDescriptorsFromTableStart = static_cast<uint32_t>(currentDescriptorRange.size());
                    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
                    range.RegisterSpace = spaceId;
                    range.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE | D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE;

                    currentDescriptorRange.push_back(range);
                }

                for (auto& srv : srvs)
                {
                    D3D12_DESCRIPTOR_RANGE1 range{};
                    range.BaseShaderRegister = srv.mBindingIndex;
                    range.NumDescriptors = 1;
                    range.OffsetInDescriptorsFromTableStart = static_cast<uint32_t>(currentDescriptorRange.size());
                    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
                    range.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE | D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE;
                    range.RegisterSpace = spaceId;

                    currentDescriptorRange.push_back(range);
                }

                D3D12_ROOT_PARAMETER1 desciptorTableForSpace{};
                desciptorTableForSpace.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
                desciptorTableForSpace.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
                desciptorTableForSpace.DescriptorTable.NumDescriptorRanges = static_cast<uint32_t>(currentDescriptorRange.size());
                desciptorTableForSpace.DescriptorTable.pDescriptorRanges = currentDescriptorRange.data();

                resourceMapping.mTableMapping[spaceId] = static_cast<uint32_t>(rootParameters.size());
                rootParameters.push_back(desciptorTableForSpace);
            }
        }

        D3D12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc{};
        rootSignatureDesc.Desc_1_1.NumParameters = static_cast<uint32_t>(rootParameters.size());
        rootSignatureDesc.Desc_1_1.pParameters = rootParameters.data();
        rootSignatureDesc.Desc_1_1.NumStaticSamplers = 0;
        rootSignatureDesc.Desc_1_1.pStaticSamplers = nullptr;
        rootSignatureDesc.Desc_1_1.Flags = D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED | D3D12_ROOT_SIGNATURE_FLAG_SAMPLER_HEAP_DIRECTLY_INDEXED;
        rootSignatureDesc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;

        ID3DBlob* rootSignatureBlob = nullptr;
        ID3DBlob* errorBlob = nullptr;
        AssertIfFailed(D3D12SerializeVersionedRootSignature(&rootSignatureDesc, &rootSignatureBlob, &errorBlob));

        ID3D12RootSignature* rootSignature = nullptr;
        AssertIfFailed(mDevice->CreateRootSignature(0, rootSignatureBlob->GetBufferPointer(), rootSignatureBlob->GetBufferSize(), IID_PPV_ARGS(&rootSignature)));

        return rootSignature;
    }

    std::unique_ptr<PipelineStateObject> Device::CreateGraphicsPipeline(const GraphicsPipelineDesc& desc, const PipelineResourceLayout& layout)
    {
        D3D12_GRAPHICS_PIPELINE_STATE_DESC pipelineDesc{};
        pipelineDesc.NodeMask = 0;
        pipelineDesc.SampleMask = 0xFFFFFFFF;
        pipelineDesc.PrimitiveTopologyType = desc.mTopology;
        pipelineDesc.InputLayout.pInputElementDescs = nullptr;
        pipelineDesc.InputLayout.NumElements = 0;
        pipelineDesc.RasterizerState = desc.mRasterDesc;
        pipelineDesc.BlendState = desc.mBlendDesc;
        pipelineDesc.SampleDesc = desc.mSampleDesc;
        pipelineDesc.DepthStencilState = desc.mDepthStencilDesc;
        pipelineDesc.DSVFormat = desc.mRenderTargetDesc.mDepthStencilFormat;

        pipelineDesc.NumRenderTargets = desc.mRenderTargetDesc.mNumRenderTargets;
        for (uint32_t rtvIndex = 0; rtvIndex < pipelineDesc.NumRenderTargets; rtvIndex++)
        {
            pipelineDesc.RTVFormats[rtvIndex] = desc.mRenderTargetDesc.mRenderTargetFormats[rtvIndex];
        }

        if (desc.mVertexShader)
        {
            pipelineDesc.VS.pShaderBytecode = desc.mVertexShader->mShaderBlob->GetBufferPointer();
            pipelineDesc.VS.BytecodeLength = desc.mVertexShader->mShaderBlob->GetBufferSize();
        }

        if (desc.mPixelShader)
        {
            pipelineDesc.PS.pShaderBytecode = desc.mPixelShader->mShaderBlob->GetBufferPointer();
            pipelineDesc.PS.BytecodeLength = desc.mPixelShader->mShaderBlob->GetBufferSize();
        }

        std::unique_ptr<PipelineStateObject> newPipeline = std::make_unique<PipelineStateObject>();
        newPipeline->mPipelineType = PipelineType::graphics;
        
        pipelineDesc.pRootSignature = CreateRootSignature(layout, newPipeline->mPipelineResourceMapping);

        ID3D12PipelineState* graphicsPipeline = nullptr;
        AssertIfFailed(mDevice->CreateGraphicsPipelineState(&pipelineDesc, IID_PPV_ARGS(&graphicsPipeline)));

        newPipeline->mPipeline = graphicsPipeline;
        newPipeline->mRootSignature = pipelineDesc.pRootSignature;

        return newPipeline;
    }

    std::unique_ptr<PipelineStateObject> Device::CreateComputePipeline(const ComputePipelineDesc& desc, const PipelineResourceLayout& layout)
    {
        std::unique_ptr<PipelineStateObject> newPipeline = std::make_unique<PipelineStateObject>();
        newPipeline->mPipelineType = PipelineType::compute;

        D3D12_COMPUTE_PIPELINE_STATE_DESC pipelineDesc{};
        pipelineDesc.NodeMask = 0;
        pipelineDesc.CS.pShaderBytecode = desc.mComputeShader->mShaderBlob->GetBufferPointer();
        pipelineDesc.CS.BytecodeLength = desc.mComputeShader->mShaderBlob->GetBufferSize();
        pipelineDesc.pRootSignature = CreateRootSignature(layout, newPipeline->mPipelineResourceMapping);

        ID3D12PipelineState* computePipeline = nullptr;
        AssertIfFailed(mDevice->CreateComputePipelineState(&pipelineDesc, IID_PPV_ARGS(&computePipeline)));

        newPipeline->mPipeline = computePipeline;
        newPipeline->mRootSignature = pipelineDesc.pRootSignature;

        return newPipeline;
    }

    std::unique_ptr<GraphicsContext> Device::CreateGraphicsContext()
    {
        std::unique_ptr<GraphicsContext> newGraphicsContext = std::make_unique<GraphicsContext>(*this);

        return newGraphicsContext;
    }

    std::unique_ptr<ComputeContext> Device::CreateComputeContext()
    {
        std::unique_ptr<ComputeContext> newComputeContext = std::make_unique<ComputeContext>(*this);

        return newComputeContext;
    }

    void Device::DestroyBuffer(std::unique_ptr<BufferResource> buffer)
    {
        mDestructionQueues[mFrameId].mBuffersToDestroy.push_back(std::move(buffer));
    }

    void Device::DestroyTexture(std::unique_ptr<TextureResource> texture)
    {
        mDestructionQueues[mFrameId].mTexturesToDestroy.push_back(std::move(texture));
    }

    void Device::DestroyShader(std::unique_ptr<Shader> shader)
    {
        SafeRelease(shader->mShaderBlob);
    }

    void Device::DestroyPipelineStateObject(std::unique_ptr<PipelineStateObject> pso)
    {
        mDestructionQueues[mFrameId].mPipelinesToDestroy.push_back(std::move(pso));
    }

    void Device::DestroyContext(std::unique_ptr<Context> context)
    {
        mDestructionQueues[mFrameId].mContextsToDestroy.push_back(std::move(context));
    }

    ContextSubmissionResult Device::SubmitContextWork(Context& context)
    {
        uint64_t fenceResult = 0;

        switch (context.GetCommandType())
        {
        case D3D12_COMMAND_LIST_TYPE_DIRECT:
            fenceResult = mGraphicsQueue->ExecuteCommandList(context.GetCommandList());
            break;
        case D3D12_COMMAND_LIST_TYPE_COMPUTE:
            fenceResult = mComputeQueue->ExecuteCommandList(context.GetCommandList());
            break;
        case D3D12_COMMAND_LIST_TYPE_COPY:
            fenceResult = mCopyQueue->ExecuteCommandList(context.GetCommandList());
            break;
        default:
            AssertError("Unsupported submission type.");
            break;
        }

        ContextSubmissionResult submissionResult;
        submissionResult.mFrameId = mFrameId;
        submissionResult.mSubmissionIndex = static_cast<uint32_t>(mContextSubmissions[mFrameId].size());

        mContextSubmissions[mFrameId].push_back(std::make_pair(fenceResult, context.GetCommandType()));

        return submissionResult;
    }

    void Device::WaitOnContextWork(ContextSubmissionResult submission, ContextWaitType waitType)
    {
        std::pair<uint64_t, D3D12_COMMAND_LIST_TYPE> contextSubmission = mContextSubmissions[submission.mFrameId][submission.mSubmissionIndex];
        Queue* workSourceQueue = nullptr;

        switch (contextSubmission.second)
        {
        case D3D12_COMMAND_LIST_TYPE_DIRECT:
            workSourceQueue = mGraphicsQueue.get();
            break;
        case D3D12_COMMAND_LIST_TYPE_COMPUTE:
            workSourceQueue = mComputeQueue.get();
            break;
        case D3D12_COMMAND_LIST_TYPE_COPY:
            workSourceQueue = mCopyQueue.get();
            break;
        default:
            AssertError("Unsupported submission type.");
            break;
        }

        switch (waitType)
        {
        case ContextWaitType::graphics:
            mGraphicsQueue->InsertWaitForQueueFence(workSourceQueue, contextSubmission.first);
            break;
        case ContextWaitType::compute:
            mComputeQueue->InsertWaitForQueueFence(workSourceQueue, contextSubmission.first);
            break;
        case ContextWaitType::copy:
            mCopyQueue->InsertWaitForQueueFence(workSourceQueue, contextSubmission.first);
            break;
        case ContextWaitType::host:
            workSourceQueue->WaitForFenceCPUBlocking(contextSubmission.first);
            break;
        default:
            AssertError("Unsupported wait type.");
            break;
        }
    }

    void Device::WaitForIdle()
    {
        mGraphicsQueue->WaitForIdle();
        mComputeQueue->WaitForIdle();
        mCopyQueue->WaitForIdle();
    }
}