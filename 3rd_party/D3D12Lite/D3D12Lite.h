#pragma once
#include <d3d12.h>
#include <dxgi1_6.h>
#include <cstdint>
#include <array>
#include <vector>
#include <mutex>
#include <optional>
#include "SimpleMath/SimpleMath.h"

using namespace DirectX::SimpleMath;
struct IDxcBlob;

namespace D3D12MA
{
    class Allocator;
    class Allocation;
}

namespace D3D12Lite
{
    constexpr uint32_t NUM_FRAMES_IN_FLIGHT = 2;
    constexpr uint32_t NUM_BACK_BUFFERS = 3;
    constexpr uint32_t NUM_RTV_STAGING_DESCRIPTORS = 256;
    constexpr uint32_t NUM_DSV_STAGING_DESCRIPTORS = 32;
    constexpr uint32_t NUM_SRV_STAGING_DESCRIPTORS = 4096;
    constexpr uint32_t NUM_SAMPLER_DESCRIPTORS = 6;
    constexpr uint32_t MAX_QUEUED_BARRIERS = 16;
    constexpr uint8_t PER_OBJECT_SPACE = 0;
    constexpr uint8_t PER_MATERIAL_SPACE = 1;
    constexpr uint8_t PER_PASS_SPACE = 2;
    constexpr uint8_t PER_FRAME_SPACE = 3;
    constexpr uint8_t NUM_RESOURCE_SPACES = 4;
    constexpr uint32_t NUM_RESERVED_SRV_DESCRIPTORS = 8192;
    constexpr uint32_t IMGUI_RESERVED_DESCRIPTOR_INDEX = 0;
    constexpr uint32_t NUM_SRV_RENDER_PASS_USER_DESCRIPTORS = 65536;
    constexpr uint32_t INVALID_RESOURCE_TABLE_INDEX = UINT_MAX;
    constexpr uint32_t MAX_TEXTURE_SUBRESOURCE_COUNT = 32;
    static const wchar_t* SHADER_SOURCE_PATH = L"Shaders/";
    static const wchar_t* SHADER_OUTPUT_PATH = L"Shaders/Compiled/";
    static const char* RESOURCE_PATH = "Resources/";

    using SubResourceLayouts = std::array<D3D12_PLACED_SUBRESOURCE_FOOTPRINT, MAX_TEXTURE_SUBRESOURCE_COUNT>;

    enum class GPUResourceType : bool
    {
        buffer = false,
        texture = true
    };

    enum class BufferAccessFlags : uint8_t
    {
        gpuOnly = 0,
        hostWritable = 1
    };

    enum class BufferViewFlags : uint8_t
    {
        none = 0,
        cbv = 1,
        srv = 2,
        uav = 4
    };

    enum class TextureViewFlags : uint8_t
    {
        none = 0,
        rtv = 1,
        dsv = 2,
        srv = 4,
        uav = 8
    };

    enum class ContextWaitType : uint8_t
    {
        host = 0,
        graphics,
        compute,
        copy
    };

    enum class PipelineType : uint8_t
    {
        graphics = 0,
        compute
    };

    enum class ShaderType : uint8_t
    {
        vertex = 0,
        pixel,
        compute
    };

    inline BufferAccessFlags operator|(BufferAccessFlags a, BufferAccessFlags b)
    {
        return static_cast<BufferAccessFlags>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
    }

    inline BufferAccessFlags operator&(BufferAccessFlags a, BufferAccessFlags b)
    {
        return static_cast<BufferAccessFlags>(static_cast<uint8_t>(a)& static_cast<uint8_t>(b));
    }

    inline BufferViewFlags operator|(BufferViewFlags a, BufferViewFlags b)
    {
        return static_cast<BufferViewFlags>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
    }

    inline BufferViewFlags operator&(BufferViewFlags a, BufferViewFlags b)
    {
        return static_cast<BufferViewFlags>(static_cast<uint8_t>(a)& static_cast<uint8_t>(b));
    }

    inline TextureViewFlags operator|(TextureViewFlags a, TextureViewFlags b)
    {
        return static_cast<TextureViewFlags>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
    }

    inline TextureViewFlags operator&(TextureViewFlags a, TextureViewFlags b)
    {
        return static_cast<TextureViewFlags>(static_cast<uint8_t>(a)& static_cast<uint8_t>(b));
    }

    template <class T> void SafeRelease(T& ppT)
    {
        if (ppT)
        {
            ppT->Release();
            ppT = nullptr;
        }
    }

    inline void AssertIfFailed(HRESULT hr)
    {
        assert(SUCCEEDED(hr));
    }

    inline void AssertError(const char* errorMessage)
    {
        assert((errorMessage, false));
    }

    inline uint32_t GetGroupCount(uint32_t threadCount, uint32_t groupSize)
    {
        return (threadCount + groupSize - 1) / groupSize;
    }

    inline uint32_t AlignU32(uint32_t valueToAlign, uint32_t alignment)
    {
        alignment -= 1;
        return (uint32_t)((valueToAlign + alignment) & ~alignment);
    }

    inline uint64_t AlignU64(uint64_t valueToAlign, uint64_t alignment)
    {
        alignment -= 1;
        return (uint64_t)((valueToAlign + alignment) & ~alignment);
    }

    struct Uint2
    {
        uint32_t x = 0;
        uint32_t y = 0;
    };

    struct ContextSubmissionResult
    {
        uint32_t mFrameId = 0;
        uint32_t mSubmissionIndex = 0;
    };

    struct BufferCreationDesc
    {
        uint32_t mSize = 0;
        uint32_t mStride = 0;
        BufferViewFlags mViewFlags = BufferViewFlags::none;
        BufferAccessFlags mAccessFlags = BufferAccessFlags::gpuOnly;
        bool mIsRawAccess = false;
    };

    struct TextureCreationDesc
    {
        TextureCreationDesc()
        {
            mResourceDesc.Format = DXGI_FORMAT_UNKNOWN;
            mResourceDesc.Width = 0;
            mResourceDesc.Height = 0;
            mResourceDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
            mResourceDesc.DepthOrArraySize = 1;
            mResourceDesc.MipLevels = 1;
            mResourceDesc.SampleDesc.Count = 1;
            mResourceDesc.SampleDesc.Quality = 0;
            mResourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
            mResourceDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
            mResourceDesc.Alignment = 0;
        }

        D3D12_RESOURCE_DESC mResourceDesc{};
        TextureViewFlags mViewFlags = TextureViewFlags::none;
    };

    struct Descriptor
    {
        bool IsValid() const { return mCPUHandle.ptr != 0; }
        bool IsReferencedByShader() const { return mGPUHandle.ptr != 0; }

        D3D12_CPU_DESCRIPTOR_HANDLE mCPUHandle{ 0 };
        D3D12_GPU_DESCRIPTOR_HANDLE mGPUHandle{ 0 };
        uint32_t mHeapIndex = 0;
    };

    struct Resource
    {
        GPUResourceType mType = GPUResourceType::buffer;
        D3D12_RESOURCE_DESC mDesc{};
        ID3D12Resource* mResource = nullptr;
        D3D12MA::Allocation* mAllocation = nullptr;
        D3D12_GPU_VIRTUAL_ADDRESS mVirtualAddress = 0;
        D3D12_RESOURCE_STATES mState = D3D12_RESOURCE_STATE_COMMON;
        bool mIsReady = false;
        uint32_t mDescriptorHeapIndex = INVALID_RESOURCE_TABLE_INDEX;
    };

    struct BufferResource : public Resource
    {
        BufferResource() : Resource()
        {
            mType = GPUResourceType::buffer;
        }

        void SetMappedData(void* data, size_t dataSize)
        {
            assert(mMappedResource != nullptr && data != nullptr && dataSize > 0 && dataSize <= mDesc.Width);
            memcpy_s(mMappedResource, mDesc.Width, data, dataSize);
        }

        uint8_t* mMappedResource = nullptr;
        uint32_t mStride = 0;
        Descriptor mCBVDescriptor{};
        Descriptor mSRVDescriptor{};
        Descriptor mUAVDescriptor{};
    };

    struct TextureResource : public Resource
    {
        TextureResource() : Resource()
        {
            mType = GPUResourceType::texture;
        }

        Descriptor mRTVDescriptor{};
        Descriptor mDSVDescriptor{};
        Descriptor mSRVDescriptor{};
        Descriptor mUAVDescriptor{};
    };

    struct PipelineResourceBinding
    {
        uint32_t mBindingIndex = 0;
        Resource* mResource = nullptr;
    };

    class PipelineResourceSpace
    {
    public:
        void SetCBV(BufferResource* resource);
        void SetSRV(const PipelineResourceBinding& binding);
        void SetUAV(const PipelineResourceBinding& binding);
        void Lock();

        const BufferResource* GetCBV() const { return mCBV; }
        const std::vector<PipelineResourceBinding>& GetUAVs() const { return mUAVs; }
        const std::vector<PipelineResourceBinding>& GetSRVs() const { return mSRVs; }

        bool IsLocked() const { return mIsLocked; }

    private:
        uint32_t GetIndexOfBindingIndex(const std::vector<PipelineResourceBinding>& bindings, uint32_t bindingIndex);

        //If a resource space needs more than one CBV, it is likely a design flaw, as you want to consolidate these as much
        //as possible if they have the same update frequency (which is contained by a PipelineResourceSpace). Of course,
        //you can freely change this to a vector like the others if you want.
        BufferResource* mCBV = nullptr;
        std::vector<PipelineResourceBinding> mUAVs;
        std::vector<PipelineResourceBinding> mSRVs;
        bool mIsLocked = false;
    };

    struct PipelineResourceLayout
    {
        std::array<PipelineResourceSpace*, NUM_RESOURCE_SPACES> mSpaces{ nullptr };
    };

    struct RenderTargetDesc
    {
        std::array<DXGI_FORMAT, D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT> mRenderTargetFormats{ DXGI_FORMAT_UNKNOWN };
        uint8_t mNumRenderTargets = 0;
        DXGI_FORMAT mDepthStencilFormat = DXGI_FORMAT_UNKNOWN;
    };

    struct ShaderCreationDesc
    {
        std::wstring mShaderName;
        std::wstring mEntryPoint;
        ShaderType mType = ShaderType::compute;
    };

    struct Shader
    {
        IDxcBlob* mShaderBlob = nullptr;
    };

    struct GraphicsPipelineDesc
    {
        Shader* mVertexShader = nullptr;
        Shader* mPixelShader = nullptr;
        D3D12_RASTERIZER_DESC mRasterDesc{};
        D3D12_BLEND_DESC mBlendDesc{};
        D3D12_DEPTH_STENCIL_DESC mDepthStencilDesc{};
        RenderTargetDesc mRenderTargetDesc{};
        DXGI_SAMPLE_DESC mSampleDesc{};
        D3D12_PRIMITIVE_TOPOLOGY_TYPE mTopology = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    };

    inline GraphicsPipelineDesc GetDefaultGraphicsPipelineDesc()
    {
        GraphicsPipelineDesc desc;
        desc.mRasterDesc.FillMode = D3D12_FILL_MODE_SOLID;
        desc.mRasterDesc.CullMode = D3D12_CULL_MODE_BACK;
        desc.mRasterDesc.FrontCounterClockwise = false;
        desc.mRasterDesc.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
        desc.mRasterDesc.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
        desc.mRasterDesc.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
        desc.mRasterDesc.DepthClipEnable = true;
        desc.mRasterDesc.MultisampleEnable = false;
        desc.mRasterDesc.AntialiasedLineEnable = false;
        desc.mRasterDesc.ForcedSampleCount = 0;
        desc.mRasterDesc.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;

        desc.mBlendDesc.AlphaToCoverageEnable = false;
        desc.mBlendDesc.IndependentBlendEnable = false;

        const D3D12_RENDER_TARGET_BLEND_DESC defaultRenderTargetBlendDesc =
        {
            false, false,
            D3D12_BLEND_ONE, D3D12_BLEND_ZERO, D3D12_BLEND_OP_ADD,
            D3D12_BLEND_ONE, D3D12_BLEND_ZERO, D3D12_BLEND_OP_ADD,
            D3D12_LOGIC_OP_NOOP,
            D3D12_COLOR_WRITE_ENABLE_ALL,
        };

        for (UINT i = 0; i < D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i)
        {
            desc.mBlendDesc.RenderTarget[i] = defaultRenderTargetBlendDesc;
        }

        desc.mDepthStencilDesc.DepthEnable = false;
        desc.mDepthStencilDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
        desc.mDepthStencilDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
        desc.mDepthStencilDesc.StencilEnable = false;
        desc.mDepthStencilDesc.StencilReadMask = D3D12_DEFAULT_STENCIL_READ_MASK;
        desc.mDepthStencilDesc.StencilWriteMask = D3D12_DEFAULT_STENCIL_WRITE_MASK;

        const D3D12_DEPTH_STENCILOP_DESC defaultStencilOp =
        { D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_COMPARISON_FUNC_ALWAYS };

        desc.mDepthStencilDesc.FrontFace = defaultStencilOp;
        desc.mDepthStencilDesc.BackFace = defaultStencilOp;

        desc.mSampleDesc.Count = 1;
        desc.mSampleDesc.Quality = 0;

        return desc;
    }

    struct ComputePipelineDesc
    {
        Shader* mComputeShader = nullptr;
    };

    struct PipelineResourceMapping
    {
        std::array<std::optional<uint32_t>, NUM_RESOURCE_SPACES> mCbvMapping{};
        std::array<std::optional<uint32_t>, NUM_RESOURCE_SPACES> mTableMapping{};
    };

    struct PipelineStateObject
    {
        ID3D12PipelineState* mPipeline = nullptr;
        ID3D12RootSignature* mRootSignature = nullptr;
        PipelineType mPipelineType = PipelineType::graphics;
        PipelineResourceMapping mPipelineResourceMapping;
    };

    struct PipelineInfo
    {
        PipelineStateObject* mPipeline = nullptr;
        std::vector<TextureResource*> mRenderTargets;
        TextureResource* mDepthStencilTarget = nullptr;
    };

    struct BufferUpload
    {
        BufferResource* mBuffer = nullptr;
        std::unique_ptr<uint8_t[]> mBufferData;
        size_t mBufferDataSize = 0;
    };

    struct TextureUpload
    {
        TextureResource* mTexture = nullptr;
        std::unique_ptr<uint8_t[]> mTextureData;
        size_t mTextureDataSize = 0;
        uint32_t mNumSubResources = 0;
        SubResourceLayouts mSubResourceLayouts{ 0 };
    };

    class DescriptorHeap
    {
    public:
        DescriptorHeap(ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE heapType, uint32_t numDescriptors, bool isShaderVisible);
        virtual ~DescriptorHeap();

        ID3D12DescriptorHeap* GetHeap() const { return mDescriptorHeap; }
        D3D12_DESCRIPTOR_HEAP_TYPE GetHeapType() const { return mHeapType; }
        D3D12_CPU_DESCRIPTOR_HANDLE GetHeapCPUStart() const { return mHeapStart.mCPUHandle; }
        D3D12_GPU_DESCRIPTOR_HANDLE GetHeapGPUStart() const { return mHeapStart.mGPUHandle; }
        uint32_t GetMaxDescriptors() const { return mMaxDescriptors; }
        uint32_t GetDescriptorSize() const { return mDescriptorSize; }

    protected:
        D3D12_DESCRIPTOR_HEAP_TYPE mHeapType = D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES;
        uint32_t mMaxDescriptors = 0;
        uint32_t mDescriptorSize = 0;
        bool mIsShaderVisible = false;
        ID3D12DescriptorHeap* mDescriptorHeap = nullptr;
        Descriptor mHeapStart{};
    };

    class StagingDescriptorHeap final : public DescriptorHeap
    {
    public:
        StagingDescriptorHeap(ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE heapType, uint32_t numDescriptors);
        ~StagingDescriptorHeap();

        Descriptor GetNewDescriptor();
        void FreeDescriptor(Descriptor descriptor);

    private:
        std::vector<uint32_t> mFreeDescriptors;
        uint32_t mCurrentDescriptorIndex = 0;
        uint32_t mActiveHandleCount = 0;
        std::mutex mUsageMutex;
    };

    class RenderPassDescriptorHeap final : public DescriptorHeap
    {
    public:
        RenderPassDescriptorHeap(ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE heapType, uint32_t reservedCount, uint32_t userCount);

        void Reset();
        Descriptor AllocateUserDescriptorBlock(uint32_t count);
        Descriptor GetReservedDescriptor(uint32_t index);

    private:
        uint32_t mReservedHandleCount = 0;
        uint32_t mCurrentDescriptorIndex = 0;
        std::mutex mUsageMutex;
    };

    class Queue
    {
    public:
        Queue(ID3D12Device* device, D3D12_COMMAND_LIST_TYPE commandType);
        ~Queue();

        bool IsFenceComplete(uint64_t fenceValue);
        void InsertWait(uint64_t fenceValue);
        void InsertWaitForQueueFence(Queue* otherQueue, uint64_t fenceValue);
        void InsertWaitForQueue(Queue* otherQueue);
        void WaitForFenceCPUBlocking(uint64_t fenceValue);
        void WaitForIdle();

        uint64_t PollCurrentFenceValue();
        uint64_t GetLastCompletedFence() { return mLastCompletedFenceValue; }
        uint64_t GetNextFenceValue() { return mNextFenceValue; }
        uint64_t ExecuteCommandList(ID3D12CommandList* commandList);
        uint64_t SignalFence();

        ID3D12CommandQueue* GetDeviceQueue() { return mQueue; }
        ID3D12Fence* GetFence() { return mFence; }

    private:
        D3D12_COMMAND_LIST_TYPE mQueueType = D3D12_COMMAND_LIST_TYPE_DIRECT;
        ID3D12CommandQueue* mQueue = nullptr;
        ID3D12Fence* mFence = nullptr;
        uint64_t mNextFenceValue = 1;
        uint64_t mLastCompletedFenceValue = 0;
        HANDLE mFenceEventHandle = 0;
        std::mutex mFenceMutex;
        std::mutex mEventMutex;
    };

    class Context
    {
    public:
        Context(class Device& device, D3D12_COMMAND_LIST_TYPE commandType);
        virtual ~Context();

        D3D12_COMMAND_LIST_TYPE GetCommandType() { return mContextType; }
        ID3D12GraphicsCommandList* GetCommandList() { return mCommandList; }

        void Reset();
        void AddBarrier(Resource& resource, D3D12_RESOURCE_STATES newState);
        void FlushBarriers();
        void CopyResource(const Resource& destination, const Resource& source);
        void CopyBufferRegion(Resource& destination, uint64_t destOffset, Resource& source, uint64_t sourceOffset, uint64_t numBytes);
        void CopyTextureRegion(Resource& destination, Resource& source, size_t sourceOffset, SubResourceLayouts& subResourceLayouts, uint32_t numSubResources);

    protected:
        void BindDescriptorHeaps(uint32_t frameIndex);

        class Device& mDevice;
        D3D12_COMMAND_LIST_TYPE mContextType = D3D12_COMMAND_LIST_TYPE_DIRECT;
        ID3D12GraphicsCommandList4* mCommandList = nullptr;
        std::array<ID3D12DescriptorHeap*, D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES> mCurrentDescriptorHeaps{ nullptr };
        std::array<ID3D12CommandAllocator*, NUM_FRAMES_IN_FLIGHT> mCommandAllocators{ nullptr };
        std::array<D3D12_RESOURCE_BARRIER, MAX_QUEUED_BARRIERS> mResourceBarriers{};
        uint32_t mNumQueuedBarriers = 0;
        RenderPassDescriptorHeap* mCurrentSRVHeap = nullptr;
        D3D12_CPU_DESCRIPTOR_HANDLE mCurrentSRVHeapHandle{ 0 };
    };

    class GraphicsContext final : public Context
    {
    public:
        GraphicsContext(class Device& device);

        void SetDefaultViewPortAndScissor(Uint2 screenSize);
        void SetViewport(const D3D12_VIEWPORT& viewPort);
        void SetScissorRect(const D3D12_RECT& rect);
        void SetStencilRef(uint32_t stencilRef);
        void SetBlendFactor(Color blendFactor);
        void SetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY topology);
        void SetPipeline(const PipelineInfo& pipelineBinding);
        void SetPipelineResources(uint32_t spaceId, const PipelineResourceSpace& resources);
        void SetIndexBuffer(const BufferResource& indexBuffer);
        void ClearRenderTarget(const TextureResource& target, Color color);
        void ClearDepthStencilTarget(const TextureResource& target, float depth, uint8_t stencil);
        void DrawFullScreenTriangle();
        void Draw(uint32_t vertexCount, uint32_t vertexStartOffset = 0);
        void DrawIndexed(uint32_t indexCount, uint32_t startIndexLocation = 0, uint32_t baseVertexLocation = 0);
        void DrawInstanced(uint32_t vertexCountPerInstance, uint32_t instanceCount, uint32_t startVertexLocation = 0, uint32_t startInstanceLocation = 0);
        void DrawIndexedInstanced(uint32_t indexCountPerInstance, uint32_t instanceCount, uint32_t startIndexLocation, uint32_t baseVertexLocation, uint32_t startInstanceLocation);
        void Dispatch(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ);
        void Dispatch1D(uint32_t threadCountX, uint32_t groupSizeX);
        void Dispatch2D(uint32_t threadCountX, uint32_t threadCountY, uint32_t groupSizeX, uint32_t groupSizeY);
        void Dispatch3D(uint32_t threadCountX, uint32_t threadCountY, uint32_t threadCountZ, uint32_t groupSizeX, uint32_t groupSizeY, uint32_t groupSizeZ);

    private:
        void SetTargets(uint32_t numRenderTargets, const D3D12_CPU_DESCRIPTOR_HANDLE renderTargets[], D3D12_CPU_DESCRIPTOR_HANDLE depthStencil);

        PipelineStateObject* mCurrentPipeline = nullptr;
    };

    class ComputeContext final : public Context
    {
    public:
        ComputeContext(class Device& device);

        void SetPipeline(const PipelineInfo& pipelineBinding);
        void SetPipelineResources(uint32_t spaceId, const PipelineResourceSpace& resources);
        void Dispatch(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ);
        void Dispatch1D(uint32_t threadCountX, uint32_t groupSizeX);
        void Dispatch2D(uint32_t threadCountX, uint32_t threadCountY, uint32_t groupSizeX, uint32_t groupSizeY);
        void Dispatch3D(uint32_t threadCountX, uint32_t threadCountY, uint32_t threadCountZ, uint32_t groupSizeX, uint32_t groupSizeY, uint32_t groupSizeZ);

    private:
        PipelineStateObject* mCurrentPipeline = nullptr;
    };

    class UploadContext final : public Context
    {
    public:
        UploadContext(class Device& device, std::unique_ptr<BufferResource> bufferUploadHeap, std::unique_ptr<BufferResource> textureUploadHeap);
        ~UploadContext();

        std::unique_ptr<BufferResource> ReturnBufferHeap();
        std::unique_ptr<BufferResource> ReturnTextureHeap();

        void AddBufferUpload(std::unique_ptr<BufferUpload> bufferUpload);
        void AddTextureUpload(std::unique_ptr<TextureUpload> textureUpload);
        void ProcessUploads();
        void ResolveProcessedUploads();

    private:
        std::vector<std::unique_ptr<BufferUpload>> mBufferUploads;
        std::vector<std::unique_ptr<TextureUpload>> mTextureUploads;
        std::vector<BufferResource*> mBufferUploadsInProgress;
        std::vector<TextureResource*> mTextureUploadsInProgress;
        std::unique_ptr<BufferResource> mBufferUploadHeap;
        std::unique_ptr<BufferResource> mTextureUploadHeap;
    };

    class Device
    {
    public:
        Device(HWND windowHandle, Uint2 screenSize);
        ~Device();

        void BeginFrame();
        void EndFrame();
        void Present();

        ID3D12Device5* GetDevice() { return mDevice; }
        RenderPassDescriptorHeap& GetSamplerHeap() { return *mSamplerRenderPassDescriptorHeap; }
        RenderPassDescriptorHeap& GetSRVHeap(uint32_t frameIndex) { return *mSRVRenderPassDescriptorHeaps[frameIndex]; }
        TextureResource& GetCurrentBackBuffer();
        Descriptor& GetImguiDescriptor(uint32_t index) { return mImguiDescriptors[index]; }
        uint32_t GetFrameId() { return mFrameId; }
        Uint2 GetScreenSize() { return mScreenSize; }
        UploadContext& GetUploadContextForCurrentFrame() { return *mUploadContexts[mFrameId]; }

        std::unique_ptr<BufferResource> CreateBuffer(const BufferCreationDesc& desc);
        std::unique_ptr<TextureResource> CreateTexture(const TextureCreationDesc& desc);
        std::unique_ptr<TextureResource> CreateTextureFromFile(const std::string& texturePath);
        std::unique_ptr<Shader> CreateShader(const ShaderCreationDesc& desc);
        std::unique_ptr<PipelineStateObject> CreateGraphicsPipeline(const GraphicsPipelineDesc& desc, const PipelineResourceLayout& layout);
        std::unique_ptr<PipelineStateObject> CreateComputePipeline(const ComputePipelineDesc& desc, const PipelineResourceLayout& layout);
        std::unique_ptr<GraphicsContext> CreateGraphicsContext();
        std::unique_ptr<ComputeContext> CreateComputeContext();

        void DestroyBuffer(std::unique_ptr<BufferResource> buffer);
        void DestroyTexture(std::unique_ptr<TextureResource> texture);
        void DestroyShader(std::unique_ptr<Shader> shader);
        void DestroyPipelineStateObject(std::unique_ptr<PipelineStateObject> pso);
        void DestroyContext(std::unique_ptr<Context> context);

        ContextSubmissionResult SubmitContextWork(Context& context);
        void WaitOnContextWork(ContextSubmissionResult submission, ContextWaitType waitType);
        void WaitForIdle();

        void CopyDescriptorsSimple(uint32_t numDescriptors, D3D12_CPU_DESCRIPTOR_HANDLE destDescriptorRangeStart, D3D12_CPU_DESCRIPTOR_HANDLE srcDescriptorRangeStart, D3D12_DESCRIPTOR_HEAP_TYPE descriptorType);
        void CopyDescriptors(uint32_t numDestDescriptorRanges, const D3D12_CPU_DESCRIPTOR_HANDLE* destDescriptorRangeStarts, const uint32_t* destDescriptorRangeSizes,
                             uint32_t numSrcDescriptorRanges, const D3D12_CPU_DESCRIPTOR_HANDLE* srcDescriptorRangeStarts, const uint32_t* srcDescriptorRangeSizes, D3D12_DESCRIPTOR_HEAP_TYPE descriptorType);

    private:
        void InitializeDeviceResources();
        void CreateSamplers();
        void CreateWindowDependentResources(HWND windowHandle, Uint2 screenSize);
        void DestroyWindowDependentResources();
        void ProcessDestructions(uint32_t frameIndex);
        void CopySRVHandleToReservedTable(Descriptor srvHandle, uint32_t index);

        ID3D12RootSignature* CreateRootSignature(const PipelineResourceLayout& layout, PipelineResourceMapping& resourceMapping);

        struct EndOfFrameFences
        {
            uint64_t mGraphicsQueueFence = 0;
            uint64_t mComputeQueueFence = 0;
            uint64_t mCopyQueueFence = 0;
        };

        struct DestructionQueue
        {
            std::vector<std::unique_ptr<BufferResource>> mBuffersToDestroy;
            std::vector<std::unique_ptr<TextureResource>> mTexturesToDestroy;
            std::vector<std::unique_ptr<PipelineStateObject>> mPipelinesToDestroy;
            std::vector<std::unique_ptr<Context>> mContextsToDestroy;
        };

        uint32_t mFrameId = 0;
        Uint2 mScreenSize{ 0, 0 };
        ID3D12Device5* mDevice = nullptr;
        IDXGIFactory7* mDXGIFactory = nullptr;
        IDXGISwapChain4* mSwapChain = nullptr;
        D3D12MA::Allocator* mAllocator = nullptr;
        std::unique_ptr<Queue> mGraphicsQueue;
        std::unique_ptr<Queue> mComputeQueue;
        std::unique_ptr<Queue> mCopyQueue;
        std::unique_ptr<StagingDescriptorHeap> mRTVStagingDescriptorHeap;
        std::unique_ptr<StagingDescriptorHeap> mDSVStagingDescriptorHeap;
        std::unique_ptr<StagingDescriptorHeap> mSRVStagingDescriptorHeap;
        std::array<Descriptor, NUM_FRAMES_IN_FLIGHT> mImguiDescriptors;
        std::vector<uint32_t> mFreeReservedDescriptorIndices;
        std::unique_ptr<RenderPassDescriptorHeap> mSamplerRenderPassDescriptorHeap;
        std::array<std::unique_ptr<RenderPassDescriptorHeap>, NUM_FRAMES_IN_FLIGHT> mSRVRenderPassDescriptorHeaps;
        std::array<std::unique_ptr<TextureResource>, NUM_BACK_BUFFERS> mBackBuffers;
        std::array<EndOfFrameFences, NUM_FRAMES_IN_FLIGHT> mEndOfFrameFences;
        std::array<std::unique_ptr<UploadContext>, NUM_FRAMES_IN_FLIGHT> mUploadContexts;
        std::array<std::vector<std::pair<uint64_t, D3D12_COMMAND_LIST_TYPE>>, NUM_FRAMES_IN_FLIGHT> mContextSubmissions;
        std::array<DestructionQueue, NUM_FRAMES_IN_FLIGHT> mDestructionQueues;
    };
}

