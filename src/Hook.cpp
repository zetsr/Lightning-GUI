#include "Hook.h"
#include "D3D11.hpp"

#include "MinHook/include/MinHook.h"
extern "C" {
#include "MinHook/src/buffer.c"
#include "MinHook/src/hook.c"
#include "MinHook/src/trampoline.c"
#include "MinHook/src/hde/hde64.c"
}

#include <dxgi1_2.h>
#include <dxgi1_4.h>

#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3d11.lib")

using Present_t = HRESULT(WINAPI*)(IDXGISwapChain*, UINT, UINT);
using Present1_t = HRESULT(WINAPI*)(IDXGISwapChain1*, UINT, UINT, const DXGI_PRESENT_PARAMETERS*);
using ResizeBuffers_t = HRESULT(WINAPI*)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);
using ResizeBuffers1_t = HRESULT(WINAPI*)(IDXGISwapChain3*, UINT, UINT, UINT, DXGI_FORMAT, UINT, const UINT*, IUnknown* const*);

static ResizeBuffers1_t g_OriginalResizeBuffers1 = nullptr;
static Present_t g_OriginalPresent = nullptr;
static Present1_t g_OriginalPresent1 = nullptr;
static ResizeBuffers_t g_OriginalResizeBuffers = nullptr;
static thread_local bool g_InHook = false;
static std::atomic<bool> g_HookInstalled{ false };

struct SwapChainHookTargets
{
    uintptr_t* baseVTable = nullptr;
    uintptr_t* swapChain1VTable = nullptr;
    uintptr_t* swapChain3VTable = nullptr;
};

static void DebugOut(const wchar_t* msg)
{
    OutputDebugStringW(msg);
    OutputDebugStringW(L"\r\n");
}

static SwapChainHookTargets GetSwapChainVTables()
{
    SwapChainHookTargets targets{};

    ResolveD3D11Exports();
    if (!g_pD3D11CreateDeviceAndSwapChain)
        return targets;

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_CLASSDC;
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"LightningTmpDX11Window";

    if (!RegisterClassExW(&wc))
    {
        DWORD err = GetLastError();
        if (err != ERROR_CLASS_ALREADY_EXISTS)
            return targets;
    }

    HWND hWnd = CreateWindowExW(
        0,
        wc.lpszClassName,
        L"",
        WS_OVERLAPPEDWINDOW,
        100,
        100,
        100,
        100,
        nullptr,
        nullptr,
        wc.hInstance,
        nullptr);

    if (!hWnd)
    {
        UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return targets;
    }

    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 1;
    sd.BufferDesc.Width = 100;
    sd.BufferDesc.Height = 100;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    sd.Flags = 0;

    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    IDXGISwapChain* swapChain = nullptr;

    D3D_FEATURE_LEVEL featureLevels[] =
    {
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0
    };

    D3D_FEATURE_LEVEL outFeatureLevel = D3D_FEATURE_LEVEL_11_0;

    HRESULT hr = g_pD3D11CreateDeviceAndSwapChain(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        D3D11_CREATE_DEVICE_BGRA_SUPPORT,
        featureLevels,
        ARRAYSIZE(featureLevels),
        D3D11_SDK_VERSION,
        &sd,
        &swapChain,
        &device,
        &outFeatureLevel,
        &context);

    if (FAILED(hr))
    {
        hr = g_pD3D11CreateDeviceAndSwapChain(
            nullptr,
            D3D_DRIVER_TYPE_WARP,
            nullptr,
            D3D11_CREATE_DEVICE_BGRA_SUPPORT,
            featureLevels,
            ARRAYSIZE(featureLevels),
            D3D11_SDK_VERSION,
            &sd,
            &swapChain,
            &device,
            &outFeatureLevel,
            &context);
    }

    if (FAILED(hr) || !swapChain)
    {
        SafeRelease(context);
        SafeRelease(device);
        DestroyWindow(hWnd);
        UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return targets;
    }

    targets.baseVTable = *reinterpret_cast<uintptr_t**>(swapChain);

    IDXGISwapChain1* swapChain1 = nullptr;
    if (SUCCEEDED(swapChain->QueryInterface(__uuidof(IDXGISwapChain1), reinterpret_cast<void**>(&swapChain1))) && swapChain1)
    {
        targets.swapChain1VTable = *reinterpret_cast<uintptr_t**>(swapChain1);
        SafeRelease(swapChain1);
    }

    IDXGISwapChain3* sc3 = nullptr;
    if (SUCCEEDED(swapChain->QueryInterface(__uuidof(IDXGISwapChain3), reinterpret_cast<void**>(&sc3))) && sc3)
    {
        targets.swapChain3VTable = *reinterpret_cast<uintptr_t**>(sc3);
        SafeRelease(sc3);
    }

    SafeRelease(swapChain);
    SafeRelease(context);
    SafeRelease(device);
    DestroyWindow(hWnd);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);

    return targets;
}

static HRESULT RenderFrameCommon(IDXGISwapChain* swapChain)
{
    if (!swapChain)
        return E_FAIL;

    DXGI_SWAP_CHAIN_DESC desc{};
    UINT width = 0;
    UINT height = 0;

    if (SUCCEEDED(swapChain->GetDesc(&desc)))
    {
        width = desc.BufferDesc.Width;
        height = desc.BufferDesc.Height;
    }

    if (width == 0 || height == 0)
    {
        IDXGISwapChain1* sc1 = nullptr;
        if (SUCCEEDED(swapChain->QueryInterface(__uuidof(IDXGISwapChain1), reinterpret_cast<void**>(&sc1))) && sc1)
        {
            DXGI_SWAP_CHAIN_DESC1 desc1{};
            if (SUCCEEDED(sc1->GetDesc1(&desc1)))
            {
                width = desc1.Width;
                height = desc1.Height;
            }
            SafeRelease(sc1);
        }
    }

    Lightning::IO::GetIO().NewFrame(width, height);

    Lightning::GUI* gui = Lightning::GUI::GetBackEnd();
    Lightning::DrawList* drawList = gui ? gui->GetDrawList() : nullptr;

    if (drawList)
    {
        Lightning::OnPaint();
    }

    auto& backend = GetD3D11Backend();
    auto& backendReady = GetBackendReadyFlag();

    if (!backendReady.load(std::memory_order_acquire))
    {
        if (backend.Initialize(swapChain))
            backendReady.store(true, std::memory_order_release);
    }

    if (backendReady.load(std::memory_order_acquire))
    {
        if (backend.EnsureSwapChainResources(swapChain))
        {
            if (drawList)
            {
                backend.Render(swapChain, *drawList);
                drawList->Clear();
            }
        }
        else
        {
            if (drawList)
                drawList->Clear();
        }
    }
    else
    {
        if (drawList)
            drawList->Clear();
    }

    return S_OK;
}

static HRESULT WINAPI hkPresent(IDXGISwapChain* swapChain, UINT syncInterval, UINT flags)
{
    if (g_InHook)
        return g_OriginalPresent ? g_OriginalPresent(swapChain, syncInterval, flags) : E_FAIL;

    struct Scope
    {
        bool& flag;
        Scope(bool& f) : flag(f) { flag = true; }
        ~Scope() { flag = false; }
    } scope(g_InHook);

    RenderFrameCommon(swapChain);
    return g_OriginalPresent ? g_OriginalPresent(swapChain, syncInterval, flags) : E_FAIL;
}

static HRESULT WINAPI hkPresent1(IDXGISwapChain1* swapChain, UINT syncInterval, UINT flags, const DXGI_PRESENT_PARAMETERS* presentParameters)
{
    if (g_InHook)
        return g_OriginalPresent1 ? g_OriginalPresent1(swapChain, syncInterval, flags, presentParameters) : E_FAIL;

    struct Scope
    {
        bool& flag;
        Scope(bool& f) : flag(f) { flag = true; }
        ~Scope() { flag = false; }
    } scope(g_InHook);

    RenderFrameCommon(reinterpret_cast<IDXGISwapChain*>(swapChain));
    return g_OriginalPresent1 ? g_OriginalPresent1(swapChain, syncInterval, flags, presentParameters) : E_FAIL;
}

static HRESULT WINAPI hkResizeBuffers1(
    IDXGISwapChain3* swapChain3,
    UINT bufferCount,
    UINT width,
    UINT height,
    DXGI_FORMAT newFormat,
    UINT swapChainFlags,
    const UINT* pCreationNodeMask,
    IUnknown* const* ppPresentQueue)
{
    if (!swapChain3)
        return E_FAIL;

    IDXGISwapChain* baseSwapChain = nullptr;
    swapChain3->QueryInterface(__uuidof(IDXGISwapChain), reinterpret_cast<void**>(&baseSwapChain));

    auto& backend = GetD3D11Backend();
    auto& backendReady = GetBackendReadyFlag();

    if (baseSwapChain)
        backend.ReleaseSwapChainResources(baseSwapChain);

    HRESULT hr = g_OriginalResizeBuffers1
        ? g_OriginalResizeBuffers1(swapChain3, bufferCount, width, height, newFormat, swapChainFlags, pCreationNodeMask, ppPresentQueue)
        : E_FAIL;

    if (SUCCEEDED(hr) && backendReady.load(std::memory_order_acquire))
    {
        backend.SafeFlush();

        if (baseSwapChain)
            backend.EnsureSwapChainResources(baseSwapChain);
    }

    SafeRelease(baseSwapChain);
    return hr;
}

static HRESULT WINAPI hkResizeBuffers(
    IDXGISwapChain* swapChain,
    UINT bufferCount,
    UINT width,
    UINT height,
    DXGI_FORMAT newFormat,
    UINT swapChainFlags)
{
    if (!swapChain)
        return E_FAIL;

    auto& backend = GetD3D11Backend();
    auto& backendReady = GetBackendReadyFlag();

    backend.ReleaseSwapChainResources(swapChain);

    HRESULT hr = g_OriginalResizeBuffers
        ? g_OriginalResizeBuffers(swapChain, bufferCount, width, height, newFormat, swapChainFlags)
        : E_FAIL;

    if (FAILED(hr))
        return hr;

    if (backendReady.load(std::memory_order_acquire))
    {
        backend.SafeFlush();
        backend.EnsureSwapChainResources(swapChain);
    }

    return hr;
}

namespace Lightning::Hook
{
    bool Install()
    {
        if (g_HookInstalled.load(std::memory_order_acquire))
            return true;

        ResolveD3D11Exports();
        if (!g_pD3D11CreateDeviceAndSwapChain)
        {
            DebugOut(L"[Lightning] D3D11 export resolution failed");
            return false;
        }

        MH_STATUS initStatus = MH_Initialize();
        if (initStatus != MH_OK && initStatus != MH_ERROR_ALREADY_INITIALIZED)
        {
            DebugOut(L"[Lightning] MinHook init failed");
            return false;
        }

        SwapChainHookTargets targets = GetSwapChainVTables();
        if (!targets.baseVTable)
        {
            DebugOut(L"[Lightning] Failed to get swap chain vtable");
            MH_Uninitialize();
            return false;
        }

        if (MH_CreateHook(
            reinterpret_cast<LPVOID>(targets.baseVTable[8]),
            reinterpret_cast<LPVOID>(&hkPresent),
            reinterpret_cast<LPVOID*>(&g_OriginalPresent)) != MH_OK)
        {
            DebugOut(L"[Lightning] Hook Present failed");
            MH_DisableHook(MH_ALL_HOOKS);
            MH_Uninitialize();
            return false;
        }

        if (MH_CreateHook(
            reinterpret_cast<LPVOID>(targets.baseVTable[13]),
            reinterpret_cast<LPVOID>(&hkResizeBuffers),
            reinterpret_cast<LPVOID*>(&g_OriginalResizeBuffers)) != MH_OK)
        {
            DebugOut(L"[Lightning] Hook ResizeBuffers failed");
            MH_DisableHook(MH_ALL_HOOKS);
            MH_Uninitialize();
            return false;
        }

        if (MH_EnableHook(reinterpret_cast<LPVOID>(targets.baseVTable[8])) != MH_OK)
        {
            DebugOut(L"[Lightning] Enable Present hook failed");
            MH_DisableHook(MH_ALL_HOOKS);
            MH_Uninitialize();
            return false;
        }

        if (MH_EnableHook(reinterpret_cast<LPVOID>(targets.baseVTable[13])) != MH_OK)
        {
            DebugOut(L"[Lightning] Enable ResizeBuffers hook failed");
            MH_DisableHook(MH_ALL_HOOKS);
            MH_Uninitialize();
            return false;
        }

        if (targets.swapChain3VTable)
        {
            if (MH_CreateHook(
                reinterpret_cast<LPVOID>(targets.swapChain3VTable[15]),
                reinterpret_cast<LPVOID>(&hkResizeBuffers1),
                reinterpret_cast<LPVOID*>(&g_OriginalResizeBuffers1)) == MH_OK)
            {
                MH_EnableHook(reinterpret_cast<LPVOID>(targets.swapChain3VTable[15]));
            }
        }

        if (targets.swapChain1VTable)
        {
            if (MH_CreateHook(
                reinterpret_cast<LPVOID>(targets.swapChain1VTable[22]),
                reinterpret_cast<LPVOID>(&hkPresent1),
                reinterpret_cast<LPVOID*>(&g_OriginalPresent1)) == MH_OK)
            {
                MH_EnableHook(reinterpret_cast<LPVOID>(targets.swapChain1VTable[22]));
            }
        }

        g_HookInstalled.store(true, std::memory_order_release);
        DebugOut(L"[Lightning] Hooks installed");
        return true;
    }

    void Uninstall()
    {
        if (!g_HookInstalled.load(std::memory_order_acquire))
            return;

        MH_DisableHook(MH_ALL_HOOKS);
        MH_Uninitialize();
        g_HookInstalled.store(false, std::memory_order_release);
        DebugOut(L"[Lightning] Hooks uninstalled");
    }
}