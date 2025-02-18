#include "D3D12Lite.h"

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

    void Render()
    {
        RenderClearColorTutorial();
    }

private:
    std::unique_ptr<D3D12Lite::Device> mDevice;
    std::unique_ptr<D3D12Lite::GraphicsContext> mGraphicsContext;
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