#include "GLFW/glfw3.h"
#define GLFW_EXPOSE_NATIVE_WIN32
#include "GLFW/glfw3native.h"
#include "Shaders/Shared.h"

#define SCREEN_WIDTH  800
#define SCREEN_HEIGHT 600

class Renderer
{
public:
    Renderer(HWND windowHandle, D3D12Lite::Uint2 screenSize) {
        mDevice = std::make_unique<D3D12Lite::Device>(windowHandle, screenSize);
        mGraphicsContext = mDevice->CreateGraphicsContext();

        InitializeTriangleResources();
    }

    ~Renderer() {
        mDevice->WaitForIdle();

        mDevice->DestroyPipelineStateObject(std::move(mTrianglePSO));
        mDevice->DestroyShader(std::move(mTriangleVertexShader));
        mDevice->DestroyShader(std::move(mTrianglePixelShader));
        mDevice->DestroyBuffer(std::move(mTriangleVertexBuffer));
        mDevice->DestroyBuffer(std::move(mTriangleConstantBuffer));

        mDevice->DestroyContext(std::move(mGraphicsContext));
        mDevice = nullptr;
    }

    void InitializeTriangleResources() {
        TriangleVertex vertices[3] = {
            {{-0.5f, -0.5f}, {1.0f, 0.0f, 0.0f}},
            {{ 0.0f,  0.5f}, {0.0f, 1.0f, 0.0f}},
            {{ 0.5f, -0.5f}, {0.0f, 0.0f, 1.0f}},
        };

        D3D12Lite::BufferCreationDesc trignaleBufferDesc{};
        trignaleBufferDesc.mSize = sizeof(vertices);
        trignaleBufferDesc.mAccessFlags = D3D12Lite::BufferAccessFlags::hostWritable;
        trignaleBufferDesc.mViewFlags = D3D12Lite::BufferViewFlags::srv;
        trignaleBufferDesc.mIsRawAccess = true;

        mTriangleVertexBuffer = mDevice->CreateBuffer(trignaleBufferDesc);
        mTriangleVertexBuffer->SetMappedData(&vertices, sizeof(vertices));

        D3D12Lite::BufferCreationDesc triangleConstantDesc{};
        triangleConstantDesc.mSize = sizeof(TriangleConstants);
        triangleConstantDesc.mAccessFlags = D3D12Lite::BufferAccessFlags::hostWritable;
        triangleConstantDesc.mViewFlags = D3D12Lite::BufferViewFlags::cbv;

        TriangleConstants triangleConstants{};
        triangleConstants.vertexBufferIndex = mTriangleVertexBuffer->mDescriptorHeapIndex;

        mTriangleConstantBuffer = mDevice->CreateBuffer(triangleConstantDesc);
        mTriangleConstantBuffer->SetMappedData(&triangleConstants, sizeof(TriangleConstants));

        D3D12Lite::ShaderCreationDesc triangleShaderVSDesc{};
        triangleShaderVSDesc.mShaderName = L"Triangle.hlsl";
        triangleShaderVSDesc.mEntryPoint = L"VertexShader";
        triangleShaderVSDesc.mType = D3D12Lite::ShaderType::vertex;

        D3D12Lite::ShaderCreationDesc triangleShaderPSDesc{};
        triangleShaderPSDesc.mShaderName = L"Triangle.hlsl";
        triangleShaderPSDesc.mEntryPoint = L"PixelShader";
        triangleShaderPSDesc.mType = D3D12Lite::ShaderType::pixel;

        mTriangleVertexShader = mDevice->CreateShader(triangleShaderVSDesc);
        mTrianglePixelShader = mDevice->CreateShader(triangleShaderPSDesc);

        mTrianglePerObjectSpace.SetCBV(mTriangleConstantBuffer.get());
        mTrianglePerObjectSpace.Lock();

        D3D12Lite::PipelineResourceLayout resourceLayout{};
        resourceLayout.mSpaces[D3D12Lite::PER_OBJECT_SPACE] = &mTrianglePerObjectSpace;

        D3D12Lite::GraphicsPipelineDesc trianglePipelineDesc = D3D12Lite::GetDefaultGraphicsPipelineDesc();
        trianglePipelineDesc.mVertexShader = mTriangleVertexShader.get();
        trianglePipelineDesc.mPixelShader = mTrianglePixelShader.get();
        trianglePipelineDesc.mRenderTargetDesc.mNumRenderTargets = 1;
        trianglePipelineDesc.mRenderTargetDesc.mRenderTargetFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;

        mTrianglePSO = mDevice->CreateGraphicsPipeline(trianglePipelineDesc, resourceLayout);
    }

    void RenderClearColorTutorial() const {
        mDevice->BeginFrame();

        D3D12Lite::TextureResource& backBuffer = mDevice->GetCurrentBackBuffer();

        mGraphicsContext->Reset();

        mGraphicsContext->AddBarrier(backBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET);
        mGraphicsContext->FlushBarriers();

        mGraphicsContext->ClearRenderTarget(backBuffer, Color(0.3f, 0.3f, 0.8f));

        mGraphicsContext->AddBarrier(backBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET);
        mGraphicsContext->FlushBarriers();

        mDevice->SubmitContextWork(*mGraphicsContext);

        mDevice->EndFrame();
        mDevice->Present();
    }

    void RenderTriangleTutorial() {
        mDevice->BeginFrame();

        D3D12Lite::TextureResource& backBuffer = mDevice->GetCurrentBackBuffer();

        mGraphicsContext->Reset();
        mGraphicsContext->AddBarrier(backBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET);
        mGraphicsContext->FlushBarriers();

        mGraphicsContext->ClearRenderTarget(backBuffer, Color(0.3f, 0.3f, 0.8f));

        D3D12Lite::PipelineInfo pipeline{};
        pipeline.mPipeline = mTrianglePSO.get();
        pipeline.mRenderTargets.push_back(&backBuffer);

        mGraphicsContext->SetPipeline(pipeline);
        mGraphicsContext->SetPipelineResources(D3D12Lite::PER_OBJECT_SPACE, mTrianglePerObjectSpace);
        mGraphicsContext->SetDefaultViewPortAndScissor(mDevice->GetScreenSize());
        mGraphicsContext->SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        mGraphicsContext->Draw(3);

        mGraphicsContext->AddBarrier(backBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET);
        mGraphicsContext->FlushBarriers();

        mDevice->SubmitContextWork(*mGraphicsContext);

        mDevice->EndFrame();
        mDevice->Present();
    }

    void Render() {
        // RenderClearColorTutorial();
        RenderTriangleTutorial();
    }

private:
    std::unique_ptr<D3D12Lite::Device> mDevice;
    std::unique_ptr<D3D12Lite::GraphicsContext> mGraphicsContext;

    std::unique_ptr<D3D12Lite::BufferResource> mTriangleVertexBuffer;
    std::unique_ptr<D3D12Lite::BufferResource> mTriangleConstantBuffer;
    std::unique_ptr<D3D12Lite::Shader> mTriangleVertexShader;
    std::unique_ptr<D3D12Lite::Shader> mTrianglePixelShader;
    std::unique_ptr<D3D12Lite::PipelineStateObject> mTrianglePSO;
    D3D12Lite::PipelineResourceSpace mTrianglePerObjectSpace;
};

int main() {
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow* window = glfwCreateWindow(SCREEN_WIDTH, SCREEN_HEIGHT, "DX12", NULL, NULL);

    std::unique_ptr<Renderer> renderer = std::make_unique<Renderer>(glfwGetWin32Window(window), D3D12Lite::Uint2(SCREEN_WIDTH, SCREEN_HEIGHT));

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
            glfwSetWindowShouldClose(window, true);
        }

        renderer->Render();
    }

    renderer = nullptr;

    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}