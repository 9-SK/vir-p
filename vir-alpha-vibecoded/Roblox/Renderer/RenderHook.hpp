#pragma once

#include <Windows.h>
#include <d3d11.h>
#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>
#include <chrono>
#include <string>

#include <Roblox/Update/Offsets.hpp>
#include <Roblox/Hyperion/TaskScheduler/TaskScheduler.hpp>
#include <Roblox/Shared/Shared.hpp>
#include <Roblox/Environment/Libraries/Drawing.hpp>
#include <Roblox/Environment/Libraries/Raknet.hpp>
#include <others/imgui_notify.h>

namespace RenderHook
{
    inline ID3D11Device* d3d11_device = nullptr;
    inline ID3D11DeviceContext* d3d11_device_context = nullptr;
    inline IDXGISwapChain* d3d11_swap_chain = nullptr;

    using PresentFn = HRESULT(WINAPI*)(IDXGISwapChain*, UINT, UINT);
    using ResizeBuffersFn = HRESULT(WINAPI*)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);

    ID3D11RenderTargetView* m_RenderTargetView = nullptr;

    HWND WindowHandle = nullptr;

    inline PresentFn OriginalPresent = nullptr;
    inline ResizeBuffersFn OriginalResizeBuffers = nullptr;

    bool imguiinitialized;

    float g_dpiScale = 1.0f;

    template<typename T>
    void SafeRelease(T*& ptr) {
        if (ptr) {
            ptr->Release();
            ptr = nullptr;
        }
    }

    void SetupCustomImGuiStyle()
    {
        ImGuiStyle& style = ImGui::GetStyle();

        style.WindowRounding = 6.0f;
        style.ChildRounding = 6.0f;
        style.FrameRounding = 4.0f;
        style.PopupRounding = 6.0f;
        style.ScrollbarRounding = 6.0f;
        style.GrabRounding = 4.0f;
        style.TabRounding = 4.0f;

        style.Alpha = 1.0f;
        style.AntiAliasedLines = true;
        style.AntiAliasedFill = true;

        style.WindowTitleAlign = ImVec2(0.5f, 0.5f);

        style.WindowPadding = ImVec2(8.0f * g_dpiScale, 8.0f * g_dpiScale);
        style.FramePadding = ImVec2(4.0f * g_dpiScale, 3.0f * g_dpiScale);
        style.CellPadding = ImVec2(4.0f * g_dpiScale, 2.0f * g_dpiScale);
        style.ItemSpacing = ImVec2(8.0f * g_dpiScale, 4.0f * g_dpiScale);
        style.ItemInnerSpacing = ImVec2(4.0f * g_dpiScale, 4.0f * g_dpiScale);
        style.ScrollbarSize = 14.0f * g_dpiScale;
        style.GrabMinSize = 10.0f * g_dpiScale;
        style.WindowBorderSize = 1.0f * g_dpiScale;
        style.ChildBorderSize = 1.0f * g_dpiScale;
        style.PopupBorderSize = 1.0f * g_dpiScale;
        style.FrameBorderSize = 0.0f;
        style.TabBorderSize = 0.0f;
        style.WindowMinSize = ImVec2(32.0f * g_dpiScale, 32.0f * g_dpiScale);

        ImVec4* colors = style.Colors;
        colors[ImGuiCol_Text] = ImVec4(0.95f, 0.95f, 0.95f, 1.00f);
        colors[ImGuiCol_TextDisabled] = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);
        colors[ImGuiCol_WindowBg] = ImVec4(0.08f, 0.08f, 0.08f, 0.94f);
        colors[ImGuiCol_ChildBg] = ImVec4(0.12f, 0.12f, 0.12f, 0.00f);
        colors[ImGuiCol_PopupBg] = ImVec4(0.11f, 0.11f, 0.11f, 0.94f);
        colors[ImGuiCol_Border] = ImVec4(0.25f, 0.25f, 0.25f, 0.50f);
        colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
        colors[ImGuiCol_FrameBg] = ImVec4(0.20f, 0.20f, 0.20f, 0.54f);
        colors[ImGuiCol_FrameBgHovered] = ImVec4(0.25f, 0.25f, 0.25f, 0.54f);
        colors[ImGuiCol_FrameBgActive] = ImVec4(0.30f, 0.30f, 0.30f, 0.54f);
        colors[ImGuiCol_TitleBg] = ImVec4(0.10f, 0.10f, 0.10f, 1.00f);
        colors[ImGuiCol_TitleBgActive] = ImVec4(0.12f, 0.12f, 0.12f, 1.00f);
        colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.10f, 0.10f, 0.10f, 0.51f);
        colors[ImGuiCol_MenuBarBg] = ImVec4(0.14f, 0.14f, 0.14f, 1.00f);
        colors[ImGuiCol_ScrollbarBg] = ImVec4(0.10f, 0.10f, 0.10f, 0.53f);
        colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.30f, 0.30f, 0.30f, 1.00f);
        colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.35f, 0.35f, 0.35f, 1.00f);
        colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.40f, 0.40f, 0.40f, 1.00f);
        colors[ImGuiCol_CheckMark] = ImVec4(0.80f, 0.80f, 0.80f, 1.00f);
        colors[ImGuiCol_SliderGrab] = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);
        colors[ImGuiCol_SliderGrabActive] = ImVec4(0.60f, 0.60f, 0.60f, 1.00f);
        colors[ImGuiCol_Button] = ImVec4(0.25f, 0.25f, 0.25f, 0.54f);
        colors[ImGuiCol_ButtonHovered] = ImVec4(0.35f, 0.35f, 0.35f, 0.54f);
        colors[ImGuiCol_ButtonActive] = ImVec4(0.45f, 0.45f, 0.45f, 0.54f);
        colors[ImGuiCol_Header] = ImVec4(0.25f, 0.25f, 0.25f, 0.54f);
        colors[ImGuiCol_HeaderHovered] = ImVec4(0.35f, 0.35f, 0.35f, 0.54f);
        colors[ImGuiCol_HeaderActive] = ImVec4(0.45f, 0.45f, 0.45f, 0.54f);
        colors[ImGuiCol_Separator] = ImVec4(0.30f, 0.30f, 0.30f, 0.50f);
        colors[ImGuiCol_SeparatorHovered] = ImVec4(0.40f, 0.40f, 0.40f, 0.54f);
        colors[ImGuiCol_SeparatorActive] = ImVec4(0.50f, 0.50f, 0.50f, 0.54f);
        colors[ImGuiCol_ResizeGrip] = ImVec4(0.30f, 0.30f, 0.30f, 0.54f);
        colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.40f, 0.40f, 0.40f, 0.54f);
        colors[ImGuiCol_ResizeGripActive] = ImVec4(0.50f, 0.50f, 0.50f, 0.54f);
        colors[ImGuiCol_Tab] = ImVec4(0.15f, 0.15f, 0.15f, 0.86f);
        colors[ImGuiCol_TabHovered] = ImVec4(0.25f, 0.25f, 0.25f, 0.86f);
        colors[ImGuiCol_TabActive] = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
        colors[ImGuiCol_TabUnfocused] = ImVec4(0.10f, 0.10f, 0.10f, 0.97f);
        colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.15f, 0.15f, 0.15f, 1.00f);
        colors[ImGuiCol_PlotLines] = ImVec4(0.61f, 0.61f, 0.61f, 1.00f);
        colors[ImGuiCol_PlotLinesHovered] = ImVec4(1.00f, 0.43f, 0.35f, 1.00f);
        colors[ImGuiCol_PlotHistogram] = ImVec4(0.73f, 0.60f, 0.15f, 1.00f);
        colors[ImGuiCol_PlotHistogramHovered] = ImVec4(1.00f, 0.60f, 0.00f, 1.00f);
        colors[ImGuiCol_TableHeaderBg] = ImVec4(0.15f, 0.15f, 0.15f, 1.00f);
        colors[ImGuiCol_TableBorderStrong] = ImVec4(0.25f, 0.25f, 0.25f, 1.00f);
        colors[ImGuiCol_TableBorderLight] = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
        colors[ImGuiCol_TableRowBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
        colors[ImGuiCol_TableRowBgAlt] = ImVec4(1.00f, 1.00f, 1.00f, 0.06f);
        colors[ImGuiCol_TextSelectedBg] = ImVec4(0.26f, 0.59f, 0.98f, 0.35f);
        colors[ImGuiCol_DragDropTarget] = ImVec4(1.00f, 1.00f, 0.00f, 0.90f);
        colors[ImGuiCol_NavHighlight] = ImVec4(0.60f, 0.60f, 0.60f, 1.00f);
        colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1.00f, 1.00f, 1.00f, 0.70f);
        colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.80f, 0.80f, 0.80f, 0.20f);
        colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.80f, 0.80f, 0.80f, 0.35f);
    }

    bool InitializeImGui()
    {
        ImGui::CreateContext();

        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        io.ConfigDockingWithShift = false;
        io.ConfigDockingTransparentPayload = true;
        io.IniFilename = nullptr;

        g_dpiScale = static_cast<float>(GetDpiForWindow(WindowHandle)) / 96.0f;
        if (g_dpiScale <= 0.0f)
            g_dpiScale = 1.0f;

        Drawing::g_dpiScale = g_dpiScale;

        SetupCustomImGuiStyle();

        if (!m_RenderTargetView)
        {
            ID3D11Texture2D* tempBackBuffer = nullptr;

            if (FAILED(d3d11_swap_chain->GetBuffer(
                0,
                __uuidof(ID3D11Texture2D),
                reinterpret_cast<void**>(&tempBackBuffer))))
            {
                return false;
            }

            HRESULT hr = d3d11_device->CreateRenderTargetView(
                tempBackBuffer,
                nullptr,
                &m_RenderTargetView
            );

            tempBackBuffer->Release();

            if (FAILED(hr))
                return false;
        }

        if (!ImGui_ImplWin32_Init(WindowHandle))
            return false;

        if (!ImGui_ImplDX11_Init(
            d3d11_device,
            d3d11_device_context))
            return false;

        ImFontConfig fontCfg;
        fontCfg.OversampleH = 2;
        fontCfg.OversampleV = 2;
        fontCfg.PixelSnapH = false;

        const float fontSize = 14.0f * g_dpiScale;

        ImFont* font = io.Fonts->AddFontFromFileTTF(
            "C:\\Windows\\Fonts\\segoeuib.ttf",
            fontSize,
            &fontCfg,
            io.Fonts->GetGlyphRangesDefault()
        );

        if (!font)
            font = io.Fonts->AddFontDefault();

        if (!font)
            return false;

        io.FontDefault = font;

        for (int i = 0; i < 4; ++i)
            Drawing::g_Fonts[i] = font;

        ImGui::MergeIconsWithLatestFont(14.0f * g_dpiScale, false);

        return true;
    }

    HRESULT WINAPI Present_Hook(IDXGISwapChain* swapChain, UINT syncInterval, UINT flags) {

        if (!imguiinitialized) {
            bool success = InitializeImGui();
            if (success) {
                imguiinitialized = true;
            }
            else {
                return OriginalPresent(swapChain, syncInterval, flags);
            }
        }

        if (!m_RenderTargetView) {
            ID3D11Texture2D* tempBackBuffer = nullptr;
            if (SUCCEEDED(swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&tempBackBuffer)))) {
                d3d11_device->CreateRenderTargetView(tempBackBuffer, nullptr, &m_RenderTargetView);
                tempBackBuffer->Release();
            }
        }


        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        Drawing::RenderAll(ImGui::GetBackgroundDrawList());

        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 5.f);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(43.f / 255.f, 43.f / 255.f, 43.f / 255.f, 100.f / 255.f));
        ImGui::RenderNotifications();
        ImGui::PopStyleVar(1); // Don't forget to Pop()
        ImGui::PopStyleColor(1);



        ImGui::Render();


        d3d11_device_context->OMSetRenderTargets(1, &m_RenderTargetView, nullptr);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        return OriginalPresent(swapChain, syncInterval, flags);
    }

    HRESULT WINAPI ResizeBuffers_Hook(IDXGISwapChain* swapChain, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags) {
        SafeRelease(m_RenderTargetView);

        return OriginalResizeBuffers(swapChain, BufferCount, Width, Height, NewFormat, SwapChainFlags);
    }

    bool StartHook() {
        uintptr_t renderJob = TaskScheduler::GetJobByName("RenderJob");
        if (!renderJob) {
            while (true) {
                uintptr_t renderJob = TaskScheduler::GetJobByName("RenderJob");
                if (renderJob) {
                    break;
                }
                Sleep(1000);
            }
        }

        uintptr_t viewBase = *reinterpret_cast<uintptr_t*>(renderJob + Offsets::Renderer::ViewBase);
        if (!viewBase) {
            return false;
        }

        uintptr_t robloxDevice = *reinterpret_cast<uintptr_t*>(viewBase + Offsets::Renderer::Device);
        if (!robloxDevice) {
            return false;
        }

        d3d11_swap_chain = *reinterpret_cast<IDXGISwapChain**>(robloxDevice + Offsets::Renderer::SwapChain);
        if (!d3d11_swap_chain) {
            return false;
        }

        DXGI_SWAP_CHAIN_DESC swap_chain_desc;
        if (FAILED(d3d11_swap_chain->GetDesc(&swap_chain_desc))) {
            return false;
        }

        WindowHandle = swap_chain_desc.OutputWindow;

        if (FAILED(d3d11_swap_chain->GetDevice(__uuidof(ID3D11Device), (void**)(&d3d11_device)))) {
            return false;
        }

        d3d11_device->GetImmediateContext(&d3d11_device_context);

        static size_t vtableSize = 18;
        uintptr_t* originalVTable = *reinterpret_cast<uintptr_t**>(d3d11_swap_chain);

        uintptr_t* shadowVTable = new uintptr_t[vtableSize];
        memcpy(shadowVTable, originalVTable, sizeof(uintptr_t) * vtableSize);

        OriginalPresent = reinterpret_cast<PresentFn>(originalVTable[8]);
        shadowVTable[8] = reinterpret_cast<uintptr_t>(&Present_Hook);

        OriginalResizeBuffers = reinterpret_cast<ResizeBuffersFn>(originalVTable[13]);
        shadowVTable[13] = reinterpret_cast<uintptr_t>(&ResizeBuffers_Hook);

        *reinterpret_cast<uintptr_t**>(d3d11_swap_chain) = shadowVTable;

        return true;
    }

    void Shutdown() {
        if (d3d11_device_context) d3d11_device_context->Release();
        if (d3d11_device) d3d11_device->Release();
    }
}
