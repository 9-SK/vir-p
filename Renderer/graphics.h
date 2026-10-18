#pragma once

#include <d3d11.h>
#include <dxgi.h>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>
#include <imgui/backends/imgui_impl_dx11.h>
#include <imgui/backends/imgui_impl_win32.h>

#include <globals.h>
#include <roblox.h>

namespace module::core::graphics
{
    struct c_graphics
    {
        IDXGISwapChain* swapchain = nullptr;
        ID3D11Device* d3d11_device = nullptr;
        ID3D11DeviceContext* d3d11_devicecontext = nullptr;
        ID3D11RenderTargetView* d3d11_rendertargetview = nullptr;
        HWND window_handle = nullptr;

        static float dpi_scale;
        static bool show_ui;

        using present_t = HRESULT(WINAPI*)(IDXGISwapChain*, UINT, UINT);
        using resize_buffer_t = HRESULT(WINAPI*)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);

        present_t org_present = nullptr;
        resize_buffer_t org_resize_buffer = nullptr;
        WNDPROC org_window_proc = nullptr;

        bool is_initialized = false;

        void initialize();
        bool initialize_imgui();

        static HRESULT WINAPI hk_present(IDXGISwapChain* swapchain, UINT sync, UINT flags);
        static HRESULT WINAPI hk_resize_buffers(IDXGISwapChain* swapchain, UINT buffer_count, UINT width, UINT height, DXGI_FORMAT fmt, UINT flags);
        static LRESULT CALLBACK hk_wndproc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

        static ImFont* get_font(int id);
        static float get_dpi_scale() { return dpi_scale; }
        static ID3D11Device* get_device();

        template<typename T>
        void safe_release(T*& ptr)
        {
            if (ptr) { ptr->Release(); ptr = nullptr; }
        }
    };

    inline std::unique_ptr<c_graphics> graphics = std::make_unique<c_graphics>();
}