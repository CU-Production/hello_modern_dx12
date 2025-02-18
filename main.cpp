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
    }

    DestroyWindow(windowHandle);
    windowHandle = nullptr;

    UnregisterClass(applicationName.c_str(), wc.hInstance);
    moduleHandle = nullptr;

    return 0;
}