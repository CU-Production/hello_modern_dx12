#include "GLFW/glfw3.h"
#define GLFW_EXPOSE_NATIVE_WIN32
#include "GLFW/glfw3native.h"

#define SCREEN_WIDTH  800
#define SCREEN_HEIGHT 600

class Renderer
{
public:
    Renderer(HWND windowHandle, D3D12Lite::Uint2 screenSize) {
        mDevice = std::make_unique<D3D12Lite::Device>(windowHandle, screenSize);
        mGraphicsContext = mDevice->CreateGraphicsContext();
    }

    ~Renderer() {
        mDevice->WaitForIdle();
        mDevice->DestroyContext(std::move(mGraphicsContext));
        mDevice = nullptr;
    }

    void RenderClearColorTutorial() {
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

    void Render() {
        RenderClearColorTutorial();
    }

private:
    std::unique_ptr<D3D12Lite::Device> mDevice;
    std::unique_ptr<D3D12Lite::GraphicsContext> mGraphicsContext;
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