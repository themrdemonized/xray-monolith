// Тестовый лаунчер для Vulkan рендера
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>

// Forward declarations из Vulkan рендера
extern void TestRenderFrame();
extern bool SupportsVulkanRendering();

// HW и Swapchain
struct CHW {
    void* m_Instance;
    void* m_PhysicalDevice;
    void* m_Device;
    void* m_Surface;
    void* m_GraphicsQueue;
    void* m_PresentQueue;
    void* m_Allocator;
    unsigned int m_GraphicsFamily;
    unsigned int m_PresentFamily;
    void* m_hWnd;
};

struct CVulkanSwapchain {
    void Create(unsigned int width, unsigned int height);
    void Destroy();
    bool ShouldRender() const;
};

struct CVulkanCommandManager {
    void Create();
    void Destroy();
};

struct CVulkanSync {
    void Create();
    void Destroy();
};

extern CHW HW;
extern CVulkanSwapchain Swapchain;
extern CVulkanCommandManager CommandManager;
extern CVulkanSync Sync;

// DllMain
extern "C" BOOL DllMainXrRenderVulkan(HANDLE, DWORD, LPVOID);

// Window proc
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message) {
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) {
            PostQuitMessage(0);
        }
        return 0;
    }
    return DefWindowProc(hWnd, message, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int)
{
    printf("[TEST] Vulkan Renderer Test\n");
    printf("========================================\n");

    // 1. Проверка поддержки Vulkan
    if (!SupportsVulkanRendering()) {
        printf("[TEST] ERROR: Vulkan not supported!\n");
        MessageBoxA(nullptr, "Vulkan 1.3 not supported on this system", "Error", MB_ICONERROR);
        return 1;
    }

    printf("[TEST] Vulkan is supported!\n");

    // 2. Создаём окно
    WNDCLASSEXA wc = {};
    wc.cbSize = sizeof(WNDCLASSEXA);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = "VulkanTestWindow";

    if (!RegisterClassExA(&wc)) {
        printf("[TEST] ERROR: Failed to register window class\n");
        return 1;
    }

    HWND hWnd = CreateWindowExA(
        0,
        "VulkanTestWindow",
        "Vulkan Render Test - Press ESC to exit",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT,
        1280, 720,
        nullptr, nullptr, hInstance, nullptr
    );

    if (!hWnd) {
        printf("[TEST] ERROR: Failed to create window\n");
        return 1;
    }

    printf("[TEST] Window created: 1280x720\n");

    // 3. Инициализация Vulkan рендера
    printf("[TEST] Initializing Vulkan renderer...\n");

    // Вызываем DllMain для инициализации
    DllMainXrRenderVulkan(nullptr, DLL_PROCESS_ATTACH, nullptr);

    // TODO: Нужно вызвать HW.CreateDevice(hWnd) и остальные инициализации
    // Пока что это заглушка - нужно будет добавить экспорты из xrRender_Vulkan

    printf("[TEST] Renderer initialized\n");

    // 4. Главный цикл
    printf("[TEST] Starting render loop...\n");
    printf("[TEST] Press ESC to exit\n");
    printf("========================================\n");

    MSG msg = {};
    while (msg.message != WM_QUIT) {
        if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        } else {
            // Рендер фрейм
            // TODO: Вызвать TestRenderFrame() когда рендер полностью инициализирован
            Sleep(16);  // ~60 FPS
        }
    }

    printf("\n[TEST] Shutting down...\n");

    // 5. Cleanup
    DllMainXrRenderVulkan(nullptr, DLL_PROCESS_DETACH, nullptr);

    DestroyWindow(hWnd);
    UnregisterClassA("VulkanTestWindow", hInstance);

    printf("[TEST] Test completed successfully!\n");
    return 0;
}
