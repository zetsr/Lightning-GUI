// Lightning - Minimal Immediate Mode GUI Framework
// 
// API design influenced by Dear ImGui (https://github.com/ocornut/imgui)
// for familiarity and ecosystem compatibility.
// 

#include "D3D11.hpp"

#include <d3dcompiler.h>
#include <dxgi1_2.h>
#include <dxgi1_4.h>

#include <cmath>
#include <algorithm>
#include <cstring>
#include <sstream>
#include <iomanip>
#include <vector>
#include <map>
#include <utility>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

PFN_D3D11_CREATE_DEVICE_AND_SWAP_CHAIN g_pD3D11CreateDeviceAndSwapChain = nullptr;

static std::atomic<bool> g_backendReady{ false };

D3D11Backend& GetD3D11Backend()
{
    static D3D11Backend backend;
    return backend;
}

std::atomic<bool>& GetBackendReadyFlag()
{
    return g_backendReady;
}

void ResolveD3D11Exports()
{
    if (!g_pD3D11CreateDeviceAndSwapChain)
    {
        g_pD3D11CreateDeviceAndSwapChain = WaitForProcAddress<PFN_D3D11_CREATE_DEVICE_AND_SWAP_CHAIN>(
            L"d3d11.dll",
            "D3D11CreateDeviceAndSwapChain");
    }
}

int D3D11Backend::ClampInt(int v, int lo, int hi)
{
    return std::max(lo, std::min(v, hi));
}

float D3D11Backend::ClampFloat(float v, float lo, float hi)
{
    return std::max(lo, std::min(v, hi));
}

std::wstring D3D11Backend::MakeFontKey(const std::wstring& fontFamily, float fontSize, float fontWeight)
{
    std::wstringstream ss;
    ss << fontFamily << L"|" << std::fixed << std::setprecision(2) << fontSize << L"|"
        << std::fixed << std::setprecision(0) << fontWeight;
    return ss.str();
}

std::wstring D3D11Backend::MakeTextKey(const std::wstring& fontFamily, float fontSize, float fontWeight, const std::wstring& text)
{
    std::wstringstream ss;
    ss << fontFamily << L"|" << std::fixed << std::setprecision(2) << fontSize << L"|"
        << std::fixed << std::setprecision(0) << fontWeight << L"|" << text;
    return ss.str();
}

bool D3D11Backend::QueryDeviceFromSwapChain(IDXGISwapChain* swapChain)
{
    if (!swapChain)
        return false;

    if (m_device && m_context)
        return true;

    HRESULT hr = swapChain->GetDevice(__uuidof(ID3D11Device), reinterpret_cast<void**>(&m_device));
    if (FAILED(hr) || !m_device)
        return false;

    m_device->GetImmediateContext(&m_context);
    if (!m_context)
    {
        SafeRelease(m_device);
        return false;
    }

    return true;
}

void D3D11Backend::BackupState(D3D11StateBackup& s)
{
    if (!m_context)
        return;

    m_context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, s.rtvs, &s.dsv);
    s.numViewports = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
    m_context->RSGetViewports(&s.numViewports, s.viewports);
    m_context->OMGetBlendState(&s.blendState, s.blendFactor, &s.sampleMask);
    m_context->IAGetInputLayout(&s.inputLayout);
    m_context->IAGetPrimitiveTopology(&s.topology);
    m_context->VSGetShader(&s.vs, nullptr, nullptr);
    m_context->PSGetShader(&s.ps, nullptr, nullptr);
    m_context->IAGetVertexBuffers(0, 1, &s.vertexBuffer, &s.vbStride, &s.vbOffset);
    m_context->PSGetShaderResources(0, 1, &s.psSRV);
}

void D3D11Backend::RestoreState(D3D11StateBackup& s)
{
    if (!m_context)
        return;

    m_context->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, s.rtvs, s.dsv);
    m_context->RSSetViewports(s.numViewports, s.viewports);
    m_context->OMSetBlendState(s.blendState, s.blendFactor, s.sampleMask);
    m_context->IASetInputLayout(s.inputLayout);
    m_context->IASetPrimitiveTopology(s.topology);
    m_context->VSSetShader(s.vs, nullptr, 0);
    m_context->PSSetShader(s.ps, nullptr, 0);
    m_context->IASetVertexBuffers(0, 1, &s.vertexBuffer, &s.vbStride, &s.vbOffset);
    m_context->PSSetShaderResources(0, 1, &s.psSRV);

    for (auto& r : s.rtvs) SafeRelease(r);
    SafeRelease(s.dsv);
    SafeRelease(s.blendState);
    SafeRelease(s.inputLayout);
    SafeRelease(s.vs);
    SafeRelease(s.ps);
    SafeRelease(s.vertexBuffer);
    SafeRelease(s.psSRV);
}

void D3D11Backend::ReleaseGlobalResources()
{
    SafeRelease(m_whiteSRV);
    SafeRelease(m_whiteTexture);
    SafeRelease(m_samplerState);
    SafeRelease(m_blendState);
    SafeRelease(m_vertexBuffer);
    SafeRelease(m_constantBuffer);
    SafeRelease(m_inputLayout);
    SafeRelease(m_ps);
    SafeRelease(m_vs);
    SafeRelease(m_context);
    SafeRelease(m_device);
}

void D3D11Backend::ReleaseAllSwapChainResources()
{
    for (auto& kv : m_swapChainResources)
    {
        SafeRelease(kv.second.rtv);
    }
    m_swapChainResources.clear();
}

void D3D11Backend::ReleaseSwapChainResourcesInternal(IDXGISwapChain* swapChain)
{
    auto it = m_swapChainResources.find(swapChain);
    if (it != m_swapChainResources.end())
    {
        SafeRelease(it->second.rtv);
        m_swapChainResources.erase(it);
    }
}

bool D3D11Backend::CreateShadersAndStates()
{
    if (!m_device || !m_context)
        return false;

    const char* vsCode = R"(
cbuffer FrameCB : register(b0)
{
    float2 viewportSize;
    float2 pad;
};
struct VSIn
{
    float4 pos : POSITION;
    float4 col : COLOR0;
    float2 uv : TEXCOORD0;
};
struct VSOut
{
    float4 pos : SV_POSITION;
    float4 col : COLOR0;
    float2 uv : TEXCOORD0;
};
VSOut main(VSIn input)
{
    VSOut output;
    float2 ndc;
    ndc.x = (input.pos.x / viewportSize.x) * 2.0f - 1.0f;
    ndc.y = 1.0f - (input.pos.y / viewportSize.y) * 2.0f;
    output.pos = float4(ndc, input.pos.z, input.pos.w);
    output.col = input.col;
    output.uv = input.uv;
    return output;
}
)";

    const char* psCode = R"(
Texture2D tex0 : register(t0);
SamplerState samp0 : register(s0);
struct PSIn
{
    float4 pos : SV_POSITION;
    float4 col : COLOR0;
    float2 uv : TEXCOORD0;
};
float4 main(PSIn input) : SV_TARGET
{
    float4 t = tex0.Sample(samp0, input.uv);
    return t * input.col;
}
)";

    ID3DBlob* vsBlob = nullptr;
    ID3DBlob* psBlob = nullptr;
    ID3DBlob* errorBlob = nullptr;

    HRESULT hr = D3DCompile(vsCode, strlen(vsCode), nullptr, nullptr, nullptr, "main", "vs_4_0", 0, 0, &vsBlob, &errorBlob);
    if (FAILED(hr) || !vsBlob)
    {
        SafeRelease(errorBlob);
        return false;
    }
    SafeRelease(errorBlob);

    hr = D3DCompile(psCode, strlen(psCode), nullptr, nullptr, nullptr, "main", "ps_4_0", 0, 0, &psBlob, &errorBlob);
    if (FAILED(hr) || !psBlob)
    {
        SafeRelease(vsBlob);
        SafeRelease(errorBlob);
        return false;
    }
    SafeRelease(errorBlob);

    hr = m_device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &m_vs);
    if (FAILED(hr) || !m_vs)
    {
        SafeRelease(vsBlob);
        SafeRelease(psBlob);
        return false;
    }

    hr = m_device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &m_ps);
    if (FAILED(hr) || !m_ps)
    {
        SafeRelease(vsBlob);
        SafeRelease(psBlob);
        return false;
    }

    D3D11_INPUT_ELEMENT_DESC layout[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,       0, 32, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };

    hr = m_device->CreateInputLayout(
        layout,
        ARRAYSIZE(layout),
        vsBlob->GetBufferPointer(),
        vsBlob->GetBufferSize(),
        &m_inputLayout);

    SafeRelease(vsBlob);
    SafeRelease(psBlob);

    if (FAILED(hr) || !m_inputLayout)
        return false;

    D3D11_BUFFER_DESC cbd{};
    cbd.ByteWidth = sizeof(FrameCB);
    cbd.Usage = D3D11_USAGE_DEFAULT;
    cbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbd.CPUAccessFlags = 0;

    hr = m_device->CreateBuffer(&cbd, nullptr, &m_constantBuffer);
    if (FAILED(hr) || !m_constantBuffer)
        return false;

    D3D11_BUFFER_DESC vbd{};
    vbd.ByteWidth = sizeof(Vertex) * 65536;
    vbd.Usage = D3D11_USAGE_DYNAMIC;
    vbd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    vbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    hr = m_device->CreateBuffer(&vbd, nullptr, &m_vertexBuffer);
    if (FAILED(hr) || !m_vertexBuffer)
        return false;

    D3D11_BLEND_DESC bd{};
    bd.AlphaToCoverageEnable = FALSE;
    bd.IndependentBlendEnable = FALSE;
    bd.RenderTarget[0].BlendEnable = TRUE;
    bd.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    bd.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    bd.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    bd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

    hr = m_device->CreateBlendState(&bd, &m_blendState);
    if (FAILED(hr) || !m_blendState)
        return false;

    D3D11_SAMPLER_DESC sd{};
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.ComparisonFunc = D3D11_COMPARISON_ALWAYS;
    sd.MinLOD = 0.0f;
    sd.MaxLOD = D3D11_FLOAT32_MAX;

    hr = m_device->CreateSamplerState(&sd, &m_samplerState);
    if (FAILED(hr) || !m_samplerState)
        return false;

    {
        UINT32 pixel = 0xFFFFFFFF;
        D3D11_TEXTURE2D_DESC td{};
        td.Width = 1;
        td.Height = 1;
        td.MipLevels = 1;
        td.ArraySize = 1;
        td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_IMMUTABLE;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE;

        D3D11_SUBRESOURCE_DATA init{};
        init.pSysMem = &pixel;
        init.SysMemPitch = 4;

        hr = m_device->CreateTexture2D(&td, &init, &m_whiteTexture);
        if (FAILED(hr) || !m_whiteTexture)
            return false;

        hr = m_device->CreateShaderResourceView(m_whiteTexture, nullptr, &m_whiteSRV);
        if (FAILED(hr) || !m_whiteSRV)
            return false;
    }

    return true;
}

bool D3D11Backend::CreateSwapChainRTV(IDXGISwapChain* swapChain)
{
    if (!swapChain || !m_device)
        return false;

    DXGI_SWAP_CHAIN_DESC desc{};
    if (FAILED(swapChain->GetDesc(&desc)))
        return false;

    UINT bufferIndex = 0;
    if (desc.SwapEffect == DXGI_SWAP_EFFECT_FLIP_DISCARD ||
        desc.SwapEffect == DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL)
    {
        IDXGISwapChain3* sc3 = nullptr;
        if (SUCCEEDED(swapChain->QueryInterface(__uuidof(IDXGISwapChain3), reinterpret_cast<void**>(&sc3))) && sc3)
        {
            bufferIndex = sc3->GetCurrentBackBufferIndex();
            SafeRelease(sc3);
        }
    }

    ID3D11Texture2D* backBuffer = nullptr;
    HRESULT hr = swapChain->GetBuffer(bufferIndex, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&backBuffer));
    if (FAILED(hr) || !backBuffer)
        return false;

    ID3D11RenderTargetView* rtv = nullptr;
    hr = m_device->CreateRenderTargetView(backBuffer, nullptr, &rtv);
    SafeRelease(backBuffer);

    if (FAILED(hr) || !rtv)
        return false;

    SwapChainResources res{};
    res.rtv = rtv;
    m_swapChainResources[swapChain] = res;
    return true;
}

bool D3D11Backend::BuildTextTextureFromGDI(
    const std::wstring& text,
    const std::wstring& fontFamily,
    float fontSize,
    float fontWeight,
    ID3D11ShaderResourceView** outSRV,
    UINT* outWidth,
    UINT* outHeight)
{
    if (!m_device || !outSRV || !outWidth || !outHeight)
        return false;

    *outSRV = nullptr;
    *outWidth = 0;
    *outHeight = 0;

    if (text.empty())
        return false;

    HDC screenDC = GetDC(nullptr);
    if (!screenDC)
        return false;

    int dpiY = GetDeviceCaps(screenDC, LOGPIXELSY);
    ReleaseDC(nullptr, screenDC);

    int pixelHeight = -MulDiv(static_cast<int>(std::round(fontSize)), dpiY, 72);
    if (pixelHeight == 0)
        pixelHeight = -16;

    int weight = FW_NORMAL;
    if (fontWeight >= 700.0f)
        weight = FW_BOLD;
    else if (fontWeight <= 300.0f)
        weight = FW_LIGHT;

    HFONT hFont = CreateFontW(
        pixelHeight,
        0,
        0,
        0,
        weight,
        FALSE,
        FALSE,
        FALSE,
        DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE,
        fontFamily.empty() ? L"Microsoft YaHei UI" : fontFamily.c_str());

    if (!hFont)
        return false;

    HDC memDC = CreateCompatibleDC(nullptr);
    if (!memDC)
    {
        DeleteObject(hFont);
        return false;
    }

    HGDIOBJ oldFont = SelectObject(memDC, hFont);
    SetBkMode(memDC, TRANSPARENT);
    SetTextColor(memDC, RGB(255, 255, 255));

    RECT measure{};
    DrawTextW(
        memDC,
        text.c_str(),
        -1,
        &measure,
        DT_CALCRECT | DT_LEFT | DT_TOP | DT_NOPREFIX | DT_NOCLIP | DT_SINGLELINE);

    int width = std::max(1L, measure.right - measure.left + 8);
    int height = std::max(1L, measure.bottom - measure.top + 8);

    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -height;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HBITMAP hBitmap = CreateDIBSection(memDC, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!hBitmap || !bits)
    {
        SelectObject(memDC, oldFont);
        DeleteDC(memDC);
        DeleteObject(hFont);
        return false;
    }

    HGDIOBJ oldBmp = SelectObject(memDC, hBitmap);

    std::memset(bits, 0, static_cast<size_t>(width) * static_cast<size_t>(height) * 4);
    RECT drawRc{ 4, 2, width - 4, height - 2 };

    DrawTextW(
        memDC,
        text.c_str(),
        -1,
        &drawRc,
        DT_LEFT | DT_TOP | DT_NOPREFIX | DT_NOCLIP | DT_SINGLELINE);

    std::vector<uint32_t> pixels(static_cast<size_t>(width) * static_cast<size_t>(height), 0);
    uint8_t* src = static_cast<uint8_t*>(bits);

    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            size_t idx = static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x);
            uint8_t* p = src + idx * 4;
            uint8_t b = p[0];
            uint8_t g = p[1];
            uint8_t r = p[2];
            uint8_t alpha = static_cast<uint8_t>(std::max({ r, g, b }));
            pixels[idx] = (static_cast<uint32_t>(alpha) << 24) | 0x00FFFFFF;
        }
    }

    D3D11_TEXTURE2D_DESC td{};
    td.Width = static_cast<UINT>(width);
    td.Height = static_cast<UINT>(height);
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_IMMUTABLE;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA init{};
    init.pSysMem = pixels.data();
    init.SysMemPitch = static_cast<UINT>(width) * 4;

    ID3D11Texture2D* texture = nullptr;
    HRESULT hr = m_device->CreateTexture2D(&td, &init, &texture);
    if (FAILED(hr) || !texture)
    {
        SelectObject(memDC, oldBmp);
        SelectObject(memDC, oldFont);
        DeleteObject(hBitmap);
        DeleteDC(memDC);
        DeleteObject(hFont);
        return false;
    }

    hr = m_device->CreateShaderResourceView(texture, nullptr, outSRV);
    SafeRelease(texture);

    SelectObject(memDC, oldBmp);
    SelectObject(memDC, oldFont);
    DeleteObject(hBitmap);
    DeleteDC(memDC);
    DeleteObject(hFont);

    if (FAILED(hr) || !*outSRV)
        return false;

    *outWidth = static_cast<UINT>(width);
    *outHeight = static_cast<UINT>(height);
    return true;
}

std::vector<D3D11Backend::Vertex> D3D11Backend::BuildQuad(float x, float y, float w, float h, float r, float g, float b, float a)
{
    std::vector<Vertex> v;
    v.reserve(6);

    Vertex v0{ x, y, 0.0f, 1.0f, r, g, b, a, 0.0f, 0.0f };
    Vertex v1{ x + w, y, 0.0f, 1.0f, r, g, b, a, 1.0f, 0.0f };
    Vertex v2{ x + w, y + h, 0.0f, 1.0f, r, g, b, a, 1.0f, 1.0f };
    Vertex v3{ x, y, 0.0f, 1.0f, r, g, b, a, 0.0f, 0.0f };
    Vertex v4{ x + w, y + h, 0.0f, 1.0f, r, g, b, a, 1.0f, 1.0f };
    Vertex v5{ x, y + h, 0.0f, 1.0f, r, g, b, a, 0.0f, 1.0f };

    v.push_back(v0);
    v.push_back(v1);
    v.push_back(v2);
    v.push_back(v3);
    v.push_back(v4);
    v.push_back(v5);

    return v;
}

std::vector<D3D11Backend::Vertex> D3D11Backend::BuildLineList(const std::vector<std::pair<float, float>>& points, float r, float g, float b, float a)
{
    std::vector<Vertex> v;
    if (points.size() < 2)
        return v;

    v.reserve((points.size() - 1) * 2);
    for (size_t i = 0; i + 1 < points.size(); ++i)
    {
        Vertex a0{ points[i].first, points[i].second, 0.0f, 1.0f, r, g, b, a, 0.0f, 0.0f };
        Vertex a1{ points[i + 1].first, points[i + 1].second, 0.0f, 1.0f, r, g, b, a, 0.0f, 0.0f };
        v.push_back(a0);
        v.push_back(a1);
    }
    return v;
}

std::vector<std::pair<float, float>> D3D11Backend::BuildEllipsePoints(float cx, float cy, float rx, float ry, int segments)
{
    std::vector<std::pair<float, float>> pts;
    segments = std::max(12, segments);
    pts.reserve(static_cast<size_t>(segments) + 1);

    for (int i = 0; i <= segments; ++i)
    {
        float t = static_cast<float>(i) / static_cast<float>(segments);
        float ang = t * 2.0f * PI;
        float x = cx + std::cos(ang) * rx;
        float y = cy + std::sin(ang) * ry;
        pts.emplace_back(x, y);
    }

    return pts;
}

std::vector<std::pair<float, float>> D3D11Backend::BuildRoundedRectPoints(float left, float top, float right, float bottom, float rx, float ry, int segmentsPerCorner)
{
    std::vector<std::pair<float, float>> pts;
    rx = ClampFloat(rx, 0.0f, (right - left) * 0.5f);
    ry = ClampFloat(ry, 0.0f, (bottom - top) * 0.5f);
    segmentsPerCorner = std::max(1, segmentsPerCorner);

    auto addArc = [&](float cx, float cy, float startAngle, float endAngle)
        {
            for (int i = 0; i <= segmentsPerCorner; ++i)
            {
                float t = static_cast<float>(i) / static_cast<float>(segmentsPerCorner);
                float ang = startAngle + (endAngle - startAngle) * t;
                float x = cx + std::cos(ang) * rx;
                float y = cy + std::sin(ang) * ry;
                pts.emplace_back(x, y);
            }
        };

    pts.emplace_back(left + rx, top);
    pts.emplace_back(right - rx, top);
    addArc(right - rx, top + ry, -PI * 0.5f, 0.0f);
    pts.emplace_back(right, bottom - ry);
    addArc(right - rx, bottom - ry, 0.0f, PI * 0.5f);
    pts.emplace_back(left + rx, bottom);
    addArc(left + rx, bottom - ry, PI * 0.5f, PI);
    pts.emplace_back(left, top + ry);
    addArc(left + rx, top + ry, PI, PI * 1.5f);

    return pts;
}

std::vector<D3D11Backend::Vertex> D3D11Backend::BuildFilledFan(const std::vector<std::pair<float, float>>& poly, float centerX, float centerY, float r, float g, float b, float a)
{
    std::vector<Vertex> v;
    if (poly.size() < 3)
        return v;

    v.reserve((poly.size() - 1) * 3);
    for (size_t i = 0; i + 1 < poly.size(); ++i)
    {
        Vertex c{ centerX, centerY, 0.0f, 1.0f, r, g, b, a, 0.0f, 0.0f };
        Vertex p0{ poly[i].first, poly[i].second, 0.0f, 1.0f, r, g, b, a, 0.0f, 0.0f };
        Vertex p1{ poly[i + 1].first, poly[i + 1].second, 0.0f, 1.0f, r, g, b, a, 0.0f, 0.0f };
        v.push_back(c);
        v.push_back(p0);
        v.push_back(p1);
    }

    return v;
}

std::vector<D3D11Backend::Vertex> D3D11Backend::BuildFilledEllipse(float cx, float cy, float rx, float ry, float r, float g, float b, float a)
{
    const int segments = 48;
    auto pts = BuildEllipsePoints(cx, cy, rx, ry, segments);
    return BuildFilledFan(pts, cx, cy, r, g, b, a);
}

std::vector<D3D11Backend::Vertex> D3D11Backend::BuildFilledPolygonFromPath(const std::vector<std::pair<float, float>>& pts, float r, float g, float b, float a)
{
    if (pts.size() < 3)
        return {};

    float cx = 0.0f;
    float cy = 0.0f;
    for (const auto& p : pts)
    {
        cx += p.first;
        cy += p.second;
    }

    cx /= static_cast<float>(pts.size());
    cy /= static_cast<float>(pts.size());

    std::vector<std::pair<float, float>> closed = pts;
    closed.push_back(pts.front());
    return BuildFilledFan(closed, cx, cy, r, g, b, a);
}

void D3D11Backend::DrawVertices(const std::vector<Vertex>& vertices, D3D11_PRIMITIVE_TOPOLOGY topology, ID3D11ShaderResourceView* srv, UINT viewportWidth, UINT viewportHeight)
{
    if (!m_context || !m_vertexBuffer || !m_inputLayout || !m_vs || !m_ps || !m_constantBuffer || !m_samplerState || !m_whiteSRV)
        return;

    if (vertices.empty())
        return;

    D3D11_MAPPED_SUBRESOURCE mapped{};
    HRESULT hr = m_context->Map(m_vertexBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (FAILED(hr) || !mapped.pData)
        return;

    std::memcpy(mapped.pData, vertices.data(), sizeof(Vertex) * vertices.size());
    m_context->Unmap(m_vertexBuffer, 0);

    UINT stride = sizeof(Vertex);
    UINT offset = 0;
    m_context->IASetInputLayout(m_inputLayout);
    m_context->IASetPrimitiveTopology(topology);
    m_context->IASetVertexBuffers(0, 1, &m_vertexBuffer, &stride, &offset);

    FrameCB cb{};
    cb.viewportWidth = static_cast<float>(std::max<UINT>(1, viewportWidth));
    cb.viewportHeight = static_cast<float>(std::max<UINT>(1, viewportHeight));
    m_context->UpdateSubresource(m_constantBuffer, 0, nullptr, &cb, 0, 0);

    m_context->VSSetShader(m_vs, nullptr, 0);
    m_context->VSSetConstantBuffers(0, 1, &m_constantBuffer);
    m_context->PSSetShader(m_ps, nullptr, 0);
    m_context->PSSetSamplers(0, 1, &m_samplerState);

    ID3D11ShaderResourceView* useSRV = srv ? srv : m_whiteSRV;
    m_context->PSSetShaderResources(0, 1, &useSRV);

    m_context->Draw(static_cast<UINT>(vertices.size()), 0);
}

void D3D11Backend::DrawTextCommand(const Lightning::DrawCommand& cmd, UINT viewportWidth, UINT viewportHeight)
{
    ID3D11ShaderResourceView* textSRV = nullptr;
    UINT texW = 0;
    UINT texH = 0;

    if (!BuildTextTextureFromGDI(cmd.text, cmd.fontFamily, cmd.fontSize, cmd.fontWeight, &textSRV, &texW, &texH))
        return;

    std::vector<Vertex> quad = BuildQuad(
        cmd.x1,
        cmd.y1,
        static_cast<float>(texW),
        static_cast<float>(texH),
        cmd.r,
        cmd.g,
        cmd.b,
        cmd.a);

    DrawVertices(quad, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST, textSRV, viewportWidth, viewportHeight);
    SafeRelease(textSRV);
}

void D3D11Backend::DrawLineCommand(const Lightning::DrawCommand& cmd, UINT viewportWidth, UINT viewportHeight)
{
    std::vector<std::pair<float, float>> pts;
    pts.emplace_back(cmd.x1, cmd.y1);
    pts.emplace_back(cmd.x2, cmd.y2);
    auto v = BuildLineList(pts, cmd.r, cmd.g, cmd.b, cmd.a);
    DrawVertices(v, D3D11_PRIMITIVE_TOPOLOGY_LINELIST, nullptr, viewportWidth, viewportHeight);
}

void D3D11Backend::DrawRectCommand(const Lightning::DrawCommand& cmd, UINT viewportWidth, UINT viewportHeight)
{
    if (cmd.type == Lightning::DrawCommandType::FilledRect)
    {
        std::vector<Vertex> v;
        v.reserve(6);

        float x1 = cmd.x1;
        float y1 = cmd.y1;
        float x2 = cmd.x2;
        float y2 = cmd.y2;

        Vertex a{ x1, y1, 0.0f, 1.0f, cmd.r, cmd.g, cmd.b, cmd.a, 0.0f, 0.0f };
        Vertex b{ x2, y1, 0.0f, 1.0f, cmd.r, cmd.g, cmd.b, cmd.a, 1.0f, 0.0f };
        Vertex c{ x2, y2, 0.0f, 1.0f, cmd.r, cmd.g, cmd.b, cmd.a, 1.0f, 1.0f };
        Vertex d{ x1, y1, 0.0f, 1.0f, cmd.r, cmd.g, cmd.b, cmd.a, 0.0f, 0.0f };
        Vertex e{ x2, y2, 0.0f, 1.0f, cmd.r, cmd.g, cmd.b, cmd.a, 1.0f, 1.0f };
        Vertex f{ x1, y2, 0.0f, 1.0f, cmd.r, cmd.g, cmd.b, cmd.a, 0.0f, 1.0f };

        v.push_back(a);
        v.push_back(b);
        v.push_back(c);
        v.push_back(d);
        v.push_back(e);
        v.push_back(f);

        DrawVertices(v, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST, nullptr, viewportWidth, viewportHeight);
    }
    else
    {
        float x1 = cmd.x1;
        float y1 = cmd.y1;
        float x2 = cmd.x2;
        float y2 = cmd.y2;

        std::vector<std::pair<float, float>> pts;
        pts.emplace_back(x1, y1);
        pts.emplace_back(x2, y1);
        pts.emplace_back(x2, y2);
        pts.emplace_back(x1, y2);
        pts.emplace_back(x1, y1);

        auto v = BuildLineList(pts, cmd.r, cmd.g, cmd.b, cmd.a);
        DrawVertices(v, D3D11_PRIMITIVE_TOPOLOGY_LINELIST, nullptr, viewportWidth, viewportHeight);
    }
}

void D3D11Backend::DrawRoundedRectCommand(const Lightning::DrawCommand& cmd, UINT viewportWidth, UINT viewportHeight)
{
    auto pts = BuildRoundedRectPoints(cmd.x1, cmd.y1, cmd.x2, cmd.y2, cmd.radiusX, cmd.radiusY, 8);

    if (cmd.type == Lightning::DrawCommandType::FilledRoundedRect)
    {
        auto v = BuildFilledPolygonFromPath(pts, cmd.r, cmd.g, cmd.b, cmd.a);
        DrawVertices(v, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST, nullptr, viewportWidth, viewportHeight);
    }
    else
    {
        pts.push_back(pts.front());
        auto v = BuildLineList(pts, cmd.r, cmd.g, cmd.b, cmd.a);
        DrawVertices(v, D3D11_PRIMITIVE_TOPOLOGY_LINELIST, nullptr, viewportWidth, viewportHeight);
    }
}

void D3D11Backend::DrawEllipseCommand(const Lightning::DrawCommand& cmd, UINT viewportWidth, UINT viewportHeight)
{
    auto pts = BuildEllipsePoints(cmd.x1, cmd.y1, cmd.radiusX, cmd.radiusY, 48);

    if (cmd.type == Lightning::DrawCommandType::FilledEllipse)
    {
        auto v = BuildFilledFan(pts, cmd.x1, cmd.y1, cmd.r, cmd.g, cmd.b, cmd.a);
        DrawVertices(v, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST, nullptr, viewportWidth, viewportHeight);
    }
    else
    {
        auto v = BuildLineList(pts, cmd.r, cmd.g, cmd.b, cmd.a);
        DrawVertices(v, D3D11_PRIMITIVE_TOPOLOGY_LINELIST, nullptr, viewportWidth, viewportHeight);
    }
}

void D3D11Backend::DrawTriangleCommand(const Lightning::DrawCommand& cmd, UINT viewportWidth, UINT viewportHeight)
{
    if (cmd.type == Lightning::DrawCommandType::FilledTriangle)
    {
        std::vector<Vertex> v;
        v.reserve(3);

        Vertex a{ cmd.x1, cmd.y1, 0.0f, 1.0f, cmd.r, cmd.g, cmd.b, cmd.a, 0.0f, 0.0f };
        Vertex b{ cmd.x2, cmd.y2, 0.0f, 1.0f, cmd.r, cmd.g, cmd.b, cmd.a, 0.0f, 0.0f };
        Vertex c{ cmd.x3, cmd.y3, 0.0f, 1.0f, cmd.r, cmd.g, cmd.b, cmd.a, 0.0f, 0.0f };

        v.push_back(a);
        v.push_back(b);
        v.push_back(c);

        DrawVertices(v, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST, nullptr, viewportWidth, viewportHeight);
    }
    else
    {
        std::vector<std::pair<float, float>> pts;
        pts.emplace_back(cmd.x1, cmd.y1);
        pts.emplace_back(cmd.x2, cmd.y2);
        pts.emplace_back(cmd.x3, cmd.y3);
        pts.emplace_back(cmd.x1, cmd.y1);

        auto v = BuildLineList(pts, cmd.r, cmd.g, cmd.b, cmd.a);
        DrawVertices(v, D3D11_PRIMITIVE_TOPOLOGY_LINELIST, nullptr, viewportWidth, viewportHeight);
    }
}

void D3D11Backend::DrawBezierCommand(const Lightning::DrawCommand& cmd, UINT viewportWidth, UINT viewportHeight)
{
    const int segments = 32;
    std::vector<std::pair<float, float>> pts;
    pts.reserve(static_cast<size_t>(segments) + 1);

    for (int i = 0; i <= segments; ++i)
    {
        float t = static_cast<float>(i) / static_cast<float>(segments);
        float it = 1.0f - t;

        float x =
            it * it * it * cmd.x1 +
            3.0f * it * it * t * cmd.x2 +
            3.0f * it * t * t * cmd.x3 +
            t * t * t * cmd.x4;

        float y =
            it * it * it * cmd.y1 +
            3.0f * it * it * t * cmd.y2 +
            3.0f * it * t * t * cmd.y3 +
            t * t * t * cmd.y4;

        pts.emplace_back(x, y);
    }

    auto v = BuildLineList(pts, cmd.r, cmd.g, cmd.b, cmd.a);
    DrawVertices(v, D3D11_PRIMITIVE_TOPOLOGY_LINELIST, nullptr, viewportWidth, viewportHeight);
}

bool D3D11Backend::Initialize(IDXGISwapChain* swapChain)
{
    if (m_initialized)
        return true;

    if (!QueryDeviceFromSwapChain(swapChain))
        return false;

    if (!CreateShadersAndStates())
    {
        ReleaseGlobalResources();
        return false;
    }

    m_initialized = true;
    return true;
}

void D3D11Backend::Shutdown()
{
    ReleaseAllSwapChainResources();
    ReleaseGlobalResources();
    m_initialized = false;
}

bool D3D11Backend::EnsureSwapChainResources(IDXGISwapChain* swapChain)
{
    if (!swapChain)
        return false;

    if (!m_initialized && !Initialize(swapChain))
        return false;

    auto it = m_swapChainResources.find(swapChain);
    if (it != m_swapChainResources.end() && it->second.rtv)
        return true;

    return CreateSwapChainRTV(swapChain);
}

void D3D11Backend::ReleaseSwapChainResources(IDXGISwapChain* swapChain)
{
    if (!swapChain)
        return;

    if (m_context)
    {
        ID3D11RenderTargetView* nullRTV[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
        m_context->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, nullRTV, nullptr);

        ID3D11ShaderResourceView* nullSRV[1] = { nullptr };
        m_context->PSSetShaderResources(0, 1, nullSRV);

        m_context->Flush();
        m_context->ClearState();
    }

    ReleaseSwapChainResourcesInternal(swapChain);
}

void D3D11Backend::Render(IDXGISwapChain* swapChain, const Lightning::DrawList& drawList)
{
    if (!swapChain || !m_context)
        return;

    if (!EnsureSwapChainResources(swapChain))
        return;

    auto it = m_swapChainResources.find(swapChain);
    if (it == m_swapChainResources.end() || !it->second.rtv)
        return;

    D3D11StateBackup backup{};
    BackupState(backup);

    struct ScopedRestore
    {
        D3D11Backend* self = nullptr;
        D3D11StateBackup* backup = nullptr;
        ~ScopedRestore()
        {
            if (self && backup)
                self->RestoreState(*backup);
        }
    } restore{ this, &backup };

    DXGI_SWAP_CHAIN_DESC desc{};
    if (FAILED(swapChain->GetDesc(&desc)))
        return;

    UINT width = desc.BufferDesc.Width;
    UINT height = desc.BufferDesc.Height;

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

    if (width == 0 || height == 0)
        return;

    ID3D11RenderTargetView* rtv = it->second.rtv;
    m_context->OMSetRenderTargets(1, &rtv, nullptr);

    D3D11_VIEWPORT vp{};
    vp.TopLeftX = 0.0f;
    vp.TopLeftY = 0.0f;
    vp.Width = static_cast<float>(width);
    vp.Height = static_cast<float>(height);
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    m_context->RSSetViewports(1, &vp);

    FLOAT blendFactor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    m_context->OMSetBlendState(m_blendState, blendFactor, 0xFFFFFFFF);

    const auto& cmds = drawList.GetCommands();
    if (cmds.empty())
        return;

    for (const auto& cmd : cmds)
    {
        switch (cmd.type)
        {
        case Lightning::DrawCommandType::Text:
            DrawTextCommand(cmd, width, height);
            break;
        case Lightning::DrawCommandType::Line:
            DrawLineCommand(cmd, width, height);
            break;
        case Lightning::DrawCommandType::Rect:
        case Lightning::DrawCommandType::FilledRect:
            DrawRectCommand(cmd, width, height);
            break;
        case Lightning::DrawCommandType::RoundedRect:
        case Lightning::DrawCommandType::FilledRoundedRect:
            DrawRoundedRectCommand(cmd, width, height);
            break;
        case Lightning::DrawCommandType::Ellipse:
        case Lightning::DrawCommandType::FilledEllipse:
            DrawEllipseCommand(cmd, width, height);
            break;
        case Lightning::DrawCommandType::Triangle:
        case Lightning::DrawCommandType::FilledTriangle:
            DrawTriangleCommand(cmd, width, height);
            break;
        case Lightning::DrawCommandType::Bezier:
            DrawBezierCommand(cmd, width, height);
            break;
        default:
            break;
        }
    }

    ID3D11ShaderResourceView* nullSRV[1] = { nullptr };
    m_context->PSSetShaderResources(0, 1, nullSRV);
}