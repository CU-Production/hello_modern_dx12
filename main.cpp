#include "D3D12Lite.h"
#include "Shaders/Shared.h"

LRESULT CALLBACK WndProc(HWND hwnd, UINT umessage, WPARAM wparam, LPARAM lparam)
{
    switch (umessage)
    {
        case WM_KEYDOWN:
            if (wparam == VK_ESCAPE)
            {
                PostQuitMessage(0);
                return 0;
            }
            else
            {
                return DefWindowProc(hwnd, umessage, wparam, lparam);
            }

        case WM_DESTROY:
            [[fallthrough]];
        case WM_CLOSE:
            PostQuitMessage(0);
        return 0;

        default:
            return DefWindowProc(hwnd, umessage, wparam, lparam);
    }
}

class Renderer
{
public:
    Renderer(HWND windowHandle, D3D12Lite::Uint2 screenSize)
    {
        mDevice = std::make_unique<D3D12Lite::Device>(windowHandle, screenSize);
        mGraphicsContext = mDevice->CreateGraphicsContext();

        InitializeTriangleResources();

        InitializeMeshResources();
    }

    ~Renderer()
    {
        mDevice->WaitForIdle();
        mDevice->DestroyShader(std::move(mTriangleVertexShader));
        mDevice->DestroyShader(std::move(mTrianglePixelShader));
        mDevice->DestroyPipelineStateObject(std::move(mTrianglePSO));
        mDevice->DestroyBuffer(std::move(mTriangleVertexBuffer));
        mDevice->DestroyBuffer(std::move(mTriangleConstantBuffer));

        mDevice->DestroyTexture(std::move(mDepthBuffer));
        mDevice->DestroyTexture(std::move(mWoodTexture));
        mDevice->DestroyBuffer(std::move(mMeshVertexBuffer));
        mDevice->DestroyBuffer(std::move(mMeshPassConstantBuffer));

        mDevice->DestroyShader(std::move(mMeshVertexShader));
        mDevice->DestroyShader(std::move(mMeshPixelShader));
        mDevice->DestroyPipelineStateObject(std::move(mMeshPSO));

        for (uint32_t frameIndex = 0; frameIndex < D3D12Lite::NUM_FRAMES_IN_FLIGHT; frameIndex++)
        {
            mDevice->DestroyBuffer(std::move(mMeshConstantBuffers[frameIndex]));
        }

        mDevice->DestroyContext(std::move(mGraphicsContext));
        mDevice = nullptr;
    }

    void InitializeTriangleResources()
    {
        std::array<TriangleVertex, 3> vertices;
        vertices[0].position = Vector2{ -0.5f, -0.5f };
        vertices[0].color    = Vector3{ 1.0f, 0.0f, 0.0f };
        vertices[1].position = Vector2{ 0.0f, 0.5f };
        vertices[1].color    = Vector3{ 0.0f, 1.0f, 0.0f };
        vertices[2].position = Vector2{ 0.5f, -0.5f };
        vertices[2].color    = Vector3{ 0.0f, 0.0f, 1.0f };

        D3D12Lite::BufferCreationDesc triangleBufferDesc{};
        triangleBufferDesc.mSize = sizeof(vertices);
        triangleBufferDesc.mAccessFlags = D3D12Lite::BufferAccessFlags::hostWritable;
        triangleBufferDesc.mViewFlags = D3D12Lite::BufferViewFlags::srv;
        triangleBufferDesc.mStride = sizeof(TriangleVertex);
        triangleBufferDesc.mIsRawAccess = true;

        mTriangleVertexBuffer = mDevice->CreateBuffer(triangleBufferDesc);
        mTriangleVertexBuffer->SetMappedData(&vertices, sizeof(vertices));

        D3D12Lite::BufferCreationDesc triangleConstantDesc{};
        triangleConstantDesc.mSize = sizeof(TriangleConstants);
        triangleConstantDesc.mAccessFlags = D3D12Lite::BufferAccessFlags::hostWritable;
        triangleConstantDesc.mViewFlags = D3D12Lite::BufferViewFlags::cbv;

        TriangleConstants triangleConstants;
        triangleConstants.vertexBufferIndex = mTriangleVertexBuffer->mDescriptorHeapIndex;

        mTriangleConstantBuffer = mDevice->CreateBuffer(triangleConstantDesc);
        mTriangleConstantBuffer->SetMappedData(&triangleConstants, sizeof(TriangleConstants));

        D3D12Lite::ShaderCreationDesc triangleShaderVSDesc;
        triangleShaderVSDesc.mShaderName = L"Triangle.hlsl";
        triangleShaderVSDesc.mEntryPoint = L"VertexShader";
        triangleShaderVSDesc.mType = D3D12Lite::ShaderType::vertex;

        D3D12Lite::ShaderCreationDesc triangleShaderPSDesc;
        triangleShaderPSDesc.mShaderName = L"Triangle.hlsl";
        triangleShaderPSDesc.mEntryPoint = L"PixelShader";
        triangleShaderPSDesc.mType = D3D12Lite::ShaderType::pixel;

        mTriangleVertexShader = mDevice->CreateShader(triangleShaderVSDesc);
        mTrianglePixelShader = mDevice->CreateShader(triangleShaderPSDesc);

        mTrianglePerObjectSpace.SetCBV(mTriangleConstantBuffer.get());
        mTrianglePerObjectSpace.Lock();

        D3D12Lite::PipelineResourceLayout resourceLayout;
        resourceLayout.mSpaces[D3D12Lite::PER_OBJECT_SPACE] = &mTrianglePerObjectSpace;

        D3D12Lite::GraphicsPipelineDesc trianglePipelineDesc = D3D12Lite::GetDefaultGraphicsPipelineDesc();
        trianglePipelineDesc.mVertexShader = mTriangleVertexShader.get();
        trianglePipelineDesc.mPixelShader = mTrianglePixelShader.get();
        trianglePipelineDesc.mRenderTargetDesc.mNumRenderTargets = 1;
        trianglePipelineDesc.mRenderTargetDesc.mRenderTargetFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;

        mTrianglePSO = mDevice->CreateGraphicsPipeline(trianglePipelineDesc, resourceLayout);
    }

    void InitializeMeshResources()
    {
        MeshVertex meshVertices[36] = {
            {{1.0f, -1.0f, 1.0f}, {1.0f, 1.0f}, {0.0f, -1.0f, 0.0f}},
            {{1.0f, -1.0f, -1.0f}, {1.0f, 0.0f}, {0.0f, -1.0f, 0.0f}},
            {{-1.0f, -1.0f, 1.0f}, {0.0f, 1.0f}, {0.0f, -1.0f, 0.0f}},
            {{-1.0f, -1.0f, 1.0f}, {0.0f, 1.0f}, {0.0f, -1.0f, 0.0f}},
            {{1.0f, -1.0f, -1.0f}, {1.0f, 0.0f}, {0.0f, -1.0f, 0.0f}},
            {{-1.0f, -1.0f, -1.0f}, {0.0f, 0.0f}, {0.0f, -1.0f, 0.0f}},
            {{1.0f, 1.0f, -1.0f}, {1.0f, 1.0f}, {0.0f, 1.0f, 0.0f}},
            {{1.0f, 1.0f, 1.0f}, {1.0f, 0.0f}, {0.0f, 1.0f, 0.0f}},
            {{-1.0f, 1.0f, -1.0f}, {0.0f, 1.0f}, {0.0f, 1.0f, 0.0f}},
            {{-1.0f, 1.0f, -1.0f}, {0.0f, 1.0f}, {0.0f, 1.0f, 0.0f}},
            {{1.0f, 1.0f, 1.0f}, {1.0f, 0.0f}, {0.0f, 1.0f, 0.0f}},
            {{-1.0f, 1.0f, 1.0f}, {0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}},
            {{-1.0f, -1.0f, -1.0f}, {1.0f, 1.0f}, {-1.0f, 0.0f, 0.0f}},
            {{-1.0f, 1.0f, -1.0f}, {1.0f, 0.0f}, {-1.0f, 0.0f, 0.0f}},
            {{-1.0f, -1.0f, 1.0f}, {0.0f, 1.0f}, {-1.0f, 0.0f, 0.0f}},
            {{-1.0f, -1.0f, 1.0f}, {0.0f, 1.0f}, {-1.0f, 0.0f, 0.0f}},
            {{-1.0f, 1.0f, -1.0f}, {1.0f, 0.0f}, {-1.0f, 0.0f, 0.0f}},
            {{-1.0f, 1.0f, 1.0f}, {0.0f, 0.0f}, {-1.0f, 0.0f, 0.0f}},
            {{-1.0f, -1.0f, 1.0f}, {1.0f, 1.0f}, {0.0f, 0.0f, 1.0f}},
            {{-1.0f, 1.0f, 1.0f}, {1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}},
            {{1.0f, -1.0f, 1.0f}, {0.0f, 1.0f}, {0.0f, 0.0f, 1.0f}},
            {{1.0f, -1.0f, 1.0f}, {0.0f, 1.0f}, {0.0f, 0.0f, 1.0f}},
            {{-1.0f, 1.0f, 1.0f}, {1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}},
            {{1.0f, 1.0f, 1.0f}, {0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}},
            {{1.0f, -1.0f, 1.0f}, {1.0f, 1.0f}, {1.0f, 0.0f, 0.0f}},
            {{1.0f, 1.0f, 1.0f}, {1.0f, 0.0f}, {1.0f, 0.0f, 0.0f}},
            {{1.0f, -1.0f, -1.0f}, {0.0f, 1.0f}, {1.0f, 0.0f, 0.0f}},
            {{1.0f, -1.0f, -1.0f}, {0.0f, 1.0f}, {1.0f, 0.0f, 0.0f}},
            {{1.0f, 1.0f, 1.0f}, {1.0f, 0.0f}, {1.0f, 0.0f, 0.0f}},
            {{1.0f, 1.0f, -1.0f}, {0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}},
            {{1.0f, -1.0f, -1.0f}, {1.0f, 1.0f}, {0.0f, 0.0f, -1.0f}},
            {{1.0f, 1.0f, -1.0f}, {1.0f, 0.0f}, {0.0f, 0.0f, -1.0f}},
            {{-1.0f, -1.0f, -1.0f}, {0.0f, 1.0f}, {0.0f, 0.0f, -1.0f}},
            {{-1.0f, -1.0f, -1.0f}, {0.0f, 1.0f}, {0.0f, 0.0f, -1.0f}},
            {{1.0f, 1.0f, -1.0f}, {1.0f, 0.0f}, {0.0f, 0.0f, -1.0f}},
            {{-1.0f, 1.0f, -1.0f}, {0.0f, 0.0f}, {0.0f, 0.0f, -1.0f}},
        };

    D3D12Lite::BufferCreationDesc meshVertexBufferDesc{};
    meshVertexBufferDesc.mSize = sizeof(meshVertices);
    meshVertexBufferDesc.mAccessFlags = D3D12Lite::BufferAccessFlags::gpuOnly;
    meshVertexBufferDesc.mViewFlags = D3D12Lite::BufferViewFlags::srv;
    meshVertexBufferDesc.mStride = sizeof(MeshVertex);
    meshVertexBufferDesc.mIsRawAccess = true;

    mMeshVertexBuffer = mDevice->CreateBuffer(meshVertexBufferDesc);

    auto bufferUpload = std::make_unique<D3D12Lite::BufferUpload>();
    bufferUpload->mBuffer = mMeshVertexBuffer.get();
    bufferUpload->mBufferData = std::make_unique<uint8_t[]>(sizeof(meshVertices));
    bufferUpload->mBufferDataSize = sizeof(meshVertices);

    memcpy_s(bufferUpload->mBufferData.get(), sizeof(meshVertices), meshVertices, sizeof(meshVertices));

    mDevice->GetUploadContextForCurrentFrame().AddBufferUpload(std::move(bufferUpload));

    mWoodTexture = mDevice->CreateTextureFromFile("Wood.dds");

    MeshConstants meshConstants;
    meshConstants.vertexBufferIndex = mMeshVertexBuffer->mDescriptorHeapIndex;
    meshConstants.textureIndex = mWoodTexture->mDescriptorHeapIndex;

    D3D12Lite::BufferCreationDesc meshConstantDesc{};
    meshConstantDesc.mSize = sizeof(MeshConstants);
    meshConstantDesc.mAccessFlags = D3D12Lite::BufferAccessFlags::hostWritable;
    meshConstantDesc.mViewFlags = D3D12Lite::BufferViewFlags::cbv;

    for (uint32_t frameIndex = 0; frameIndex < D3D12Lite::NUM_FRAMES_IN_FLIGHT; frameIndex++)
    {
        mMeshConstantBuffers[frameIndex] = mDevice->CreateBuffer(meshConstantDesc);
        mMeshConstantBuffers[frameIndex]->SetMappedData(&meshConstants, sizeof(MeshConstants));
    }

    D3D12Lite::BufferCreationDesc meshPassConstantDesc{};
    meshPassConstantDesc.mSize = sizeof(MeshPassConstants);
    meshPassConstantDesc.mAccessFlags = D3D12Lite::BufferAccessFlags::hostWritable;
    meshPassConstantDesc.mViewFlags = D3D12Lite::BufferViewFlags::cbv;

    D3D12Lite::Uint2 screenSize = mDevice->GetScreenSize();

    float fieldOfView = 3.14159f / 4.0f;
    float aspectRatio = (float)screenSize.x / (float)screenSize.y;
    Vector3 cameraPosition = Vector3(-3.0f, 3.0f, -8.0f);

    MeshPassConstants passConstants;
    passConstants.viewMatrix = Matrix::CreateLookAt(cameraPosition, Vector3(0, 0, 0), Vector3(0, 1, 0));
    passConstants.projectionMatrix = Matrix::CreatePerspectiveFieldOfView(fieldOfView, aspectRatio, 0.001f, 1000.0f);
    passConstants.cameraPosition = cameraPosition;

    mMeshPassConstantBuffer = mDevice->CreateBuffer(meshPassConstantDesc);
    mMeshPassConstantBuffer->SetMappedData(&passConstants, sizeof(MeshPassConstants));

    D3D12Lite::TextureCreationDesc depthBufferDesc;
    depthBufferDesc.mResourceDesc.Format = DXGI_FORMAT_D32_FLOAT;
    depthBufferDesc.mResourceDesc.Width = screenSize.x;
    depthBufferDesc.mResourceDesc.Height = screenSize.y;
    depthBufferDesc.mViewFlags = D3D12Lite::TextureViewFlags::srv | D3D12Lite::TextureViewFlags::dsv;

    mDepthBuffer = mDevice->CreateTexture(depthBufferDesc);

    D3D12Lite::ShaderCreationDesc meshShaderVSDesc;
    meshShaderVSDesc.mShaderName = L"Mesh.hlsl";
    meshShaderVSDesc.mEntryPoint = L"VertexShader";
    meshShaderVSDesc.mType = D3D12Lite::ShaderType::vertex;

    D3D12Lite::ShaderCreationDesc meshShaderPSDesc;
    meshShaderPSDesc.mShaderName = L"Mesh.hlsl";
    meshShaderPSDesc.mEntryPoint = L"PixelShader";
    meshShaderPSDesc.mType = D3D12Lite::ShaderType::pixel;

    mMeshVertexShader = mDevice->CreateShader(meshShaderVSDesc);
    mMeshPixelShader = mDevice->CreateShader(meshShaderPSDesc);

    D3D12Lite::GraphicsPipelineDesc meshPipelineDesc = D3D12Lite::GetDefaultGraphicsPipelineDesc();
    meshPipelineDesc.mVertexShader = mMeshVertexShader.get();
    meshPipelineDesc.mPixelShader = mMeshPixelShader.get();
    meshPipelineDesc.mRenderTargetDesc.mNumRenderTargets = 1;
    meshPipelineDesc.mRenderTargetDesc.mRenderTargetFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    meshPipelineDesc.mDepthStencilDesc.DepthEnable = true;
    meshPipelineDesc.mRenderTargetDesc.mDepthStencilFormat = depthBufferDesc.mResourceDesc.Format;
    meshPipelineDesc.mDepthStencilDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;

    mMeshPerObjectResourceSpace.SetCBV(mMeshConstantBuffers[0].get());
    mMeshPerObjectResourceSpace.Lock();

    mMeshPerPassResourceSpace.SetCBV(mMeshPassConstantBuffer.get());
    mMeshPerPassResourceSpace.Lock();

    D3D12Lite::PipelineResourceLayout meshResourceLayout;
    meshResourceLayout.mSpaces[D3D12Lite::PER_OBJECT_SPACE] = &mMeshPerObjectResourceSpace;
    meshResourceLayout.mSpaces[D3D12Lite::PER_PASS_SPACE] = &mMeshPerPassResourceSpace;

    mMeshPSO = mDevice->CreateGraphicsPipeline(meshPipelineDesc, meshResourceLayout);
    }

    void RenderClearColorTutorial()
    {
        mDevice->BeginFrame();

        D3D12Lite::TextureResource& backBuffer = mDevice->GetCurrentBackBuffer();

        mGraphicsContext->Reset();

        mGraphicsContext->AddBarrier(backBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET);
        mGraphicsContext->FlushBarriers();

        mGraphicsContext->ClearRenderTarget(backBuffer, Color(0.3f, 0.3f, 0.8f));

        mGraphicsContext->AddBarrier(backBuffer, D3D12_RESOURCE_STATE_PRESENT);
        mGraphicsContext->FlushBarriers();

        mDevice->SubmitContextWork(*mGraphicsContext);

        mDevice->EndFrame();
        mDevice->Present();
    }

    void RenderTriangleTutorial()
    {
        mDevice->BeginFrame();

        D3D12Lite::TextureResource& backBuffer = mDevice->GetCurrentBackBuffer();

        mGraphicsContext->Reset();

        mGraphicsContext->AddBarrier(backBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET);
        mGraphicsContext->FlushBarriers();

        mGraphicsContext->ClearRenderTarget(backBuffer, Color(0.3f, 0.3f, 0.8f));

        D3D12Lite::PipelineInfo pipeline;
        pipeline.mPipeline = mTrianglePSO.get();
        pipeline.mRenderTargets.push_back(&backBuffer);

        mGraphicsContext->SetPipeline(pipeline);
        mGraphicsContext->SetPipelineResources(D3D12Lite::PER_OBJECT_SPACE, mTrianglePerObjectSpace);
        mGraphicsContext->SetDefaultViewPortAndScissor(mDevice->GetScreenSize());
        mGraphicsContext->SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        mGraphicsContext->Draw(3);

        mGraphicsContext->AddBarrier(backBuffer, D3D12_RESOURCE_STATE_PRESENT);
        mGraphicsContext->FlushBarriers();

        mDevice->SubmitContextWork(*mGraphicsContext);

        mDevice->EndFrame();
        mDevice->Present();
    }

    void RenderMeshTutorial()
    {
        mDevice->BeginFrame();

        D3D12Lite::TextureResource& backBuffer = mDevice->GetCurrentBackBuffer();

        mGraphicsContext->Reset();

        mGraphicsContext->Reset();
        mGraphicsContext->AddBarrier(backBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET);
        mGraphicsContext->AddBarrier(*mDepthBuffer, D3D12_RESOURCE_STATE_DEPTH_WRITE);
        mGraphicsContext->FlushBarriers();

        mGraphicsContext->ClearRenderTarget(backBuffer, Color(0.3f, 0.3f, 0.8f));
        mGraphicsContext->ClearDepthStencilTarget(*mDepthBuffer, 1.0f, 0);

        static float rotation = 0.0f;
        rotation += 0.01f;

        if (mMeshVertexBuffer->mIsReady && mWoodTexture->mIsReady)
        {
            MeshConstants meshConstants;
            meshConstants.vertexBufferIndex = mMeshVertexBuffer->mDescriptorHeapIndex;
            meshConstants.textureIndex = mWoodTexture->mDescriptorHeapIndex;
            meshConstants.worldMatrix = Matrix::CreateRotationY(rotation);

            mMeshConstantBuffers[mDevice->GetFrameId()]->SetMappedData(&meshConstants, sizeof(MeshConstants));

            mMeshPerObjectResourceSpace.SetCBV(mMeshConstantBuffers[mDevice->GetFrameId()].get());

            D3D12Lite::PipelineInfo pipeline;
            pipeline.mPipeline = mMeshPSO.get();
            pipeline.mRenderTargets.push_back(&backBuffer);
            pipeline.mDepthStencilTarget = mDepthBuffer.get();

            mGraphicsContext->SetPipeline(pipeline);
            mGraphicsContext->SetPipelineResources(D3D12Lite::PER_OBJECT_SPACE, mMeshPerObjectResourceSpace);
            mGraphicsContext->SetPipelineResources(D3D12Lite::PER_PASS_SPACE, mMeshPerPassResourceSpace);
            mGraphicsContext->SetDefaultViewPortAndScissor(mDevice->GetScreenSize());
            mGraphicsContext->SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            mGraphicsContext->Draw(36);
        }

        mGraphicsContext->AddBarrier(backBuffer, D3D12_RESOURCE_STATE_PRESENT);
        mGraphicsContext->FlushBarriers();

        mDevice->SubmitContextWork(*mGraphicsContext);

        mDevice->EndFrame();
        mDevice->Present();
    }

    void Render()
    {
        // RenderClearColorTutorial();
        // RenderTriangleTutorial();
        RenderMeshTutorial();
    }

private:
    std::unique_ptr<D3D12Lite::Device> mDevice;
    std::unique_ptr<D3D12Lite::GraphicsContext> mGraphicsContext;

    // Trignale
    std::unique_ptr<D3D12Lite::BufferResource> mTriangleVertexBuffer;
    std::unique_ptr<D3D12Lite::BufferResource> mTriangleConstantBuffer;
    std::unique_ptr<D3D12Lite::Shader> mTriangleVertexShader;
    std::unique_ptr<D3D12Lite::Shader> mTrianglePixelShader;
    std::unique_ptr<D3D12Lite::PipelineStateObject> mTrianglePSO;
    D3D12Lite::PipelineResourceSpace mTrianglePerObjectSpace;

    //Mesh
    std::unique_ptr<D3D12Lite::TextureResource> mDepthBuffer;
    std::unique_ptr<D3D12Lite::TextureResource> mWoodTexture;
    std::unique_ptr<D3D12Lite::BufferResource> mMeshVertexBuffer;
    std::array<std::unique_ptr<D3D12Lite::BufferResource>, D3D12Lite::NUM_FRAMES_IN_FLIGHT> mMeshConstantBuffers;
    std::unique_ptr<D3D12Lite::BufferResource> mMeshPassConstantBuffer;
    D3D12Lite::PipelineResourceSpace mMeshPerObjectResourceSpace;
    D3D12Lite::PipelineResourceSpace mMeshPerPassResourceSpace;
    std::unique_ptr<D3D12Lite::Shader> mMeshVertexShader;
    std::unique_ptr<D3D12Lite::Shader> mMeshPixelShader;
    std::unique_ptr<D3D12Lite::PipelineStateObject> mMeshPSO;
};

int main()
{
    std::string applicationName = "D3D12 Tutorial";
    D3D12Lite::Uint2 windowSize = { 1280, 720 };
    HINSTANCE moduleHandle = GetModuleHandle(nullptr);

    WNDCLASSEX wc = { 0 };
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = moduleHandle;
    wc.hIcon = LoadIcon(nullptr, IDI_WINLOGO);
    wc.hIconSm = wc.hIcon;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszMenuName = nullptr;
    wc.lpszClassName = applicationName.c_str();
    wc.cbSize = sizeof(WNDCLASSEX);
    RegisterClassEx(&wc);

    HWND windowHandle = CreateWindowEx(WS_EX_APPWINDOW, applicationName.c_str(), applicationName.c_str(),
        WS_CLIPSIBLINGS | WS_CLIPCHILDREN | WS_OVERLAPPED | WS_OVERLAPPEDWINDOW,
        (GetSystemMetrics(SM_CXSCREEN) - windowSize.x) / 2, (GetSystemMetrics(SM_CYSCREEN) - windowSize.y) / 2, windowSize.x, windowSize.y,
        nullptr, nullptr, moduleHandle, nullptr);

    ShowWindow(windowHandle, SW_SHOW);
    SetForegroundWindow(windowHandle);
    SetFocus(windowHandle);
    ShowCursor(TRUE);

    std::unique_ptr<Renderer> renderer = std::make_unique<Renderer>(windowHandle, windowSize);

    bool shouldExit = false;
    while(!shouldExit)
    {
        MSG msg{ 0 };
        if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        if (msg.message == WM_QUIT)
        {
            shouldExit = true;
        }

        renderer->Render();
    }

    renderer = nullptr;

    DestroyWindow(windowHandle);
    windowHandle = nullptr;

    UnregisterClass(applicationName.c_str(), wc.hInstance);
    moduleHandle = nullptr;

    return 0;
}