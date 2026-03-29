#pragma once

#include "Lightning.h"

#include <map>
#include <sstream>
#include <iomanip>
#include <cstring>
#include <cmath>
#include <vector>
#include <utility>

typedef HRESULT(WINAPI* PFN_D3D11_CREATE_DEVICE_AND_SWAP_CHAIN)(
    IDXGIAdapter*,
    D3D_DRIVER_TYPE,
    HMODULE,
    UINT,
    const D3D_FEATURE_LEVEL*,
    UINT,
    UINT,
    const DXGI_SWAP_CHAIN_DESC*,
    IDXGISwapChain**,
    ID3D11Device**,
    D3D_FEATURE_LEVEL*,
    ID3D11DeviceContext**);

extern PFN_D3D11_CREATE_DEVICE_AND_SWAP_CHAIN g_pD3D11CreateDeviceAndSwapChain;

void ResolveD3D11Exports();

class D3D11Backend final : public IRenderBackend
{
public:
    void SafeFlush()
    {
        if (m_context)
            m_context->Flush();
    }

private:
    struct Vertex
    {
        float x;
        float y;
        float z;
        float w;
        float r;
        float g;
        float b;
        float a;
        float u;
        float v;
    };

    struct FrameCB
    {
        float viewportWidth = 0.0f;
        float viewportHeight = 0.0f;
        float pad1 = 0.0f;
        float pad2 = 0.0f;
    };

    struct D3D11StateBackup
    {
        ID3D11RenderTargetView* rtvs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
        ID3D11DepthStencilView* dsv = nullptr;
        D3D11_VIEWPORT viewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE] = {};
        UINT numViewports = 0;
        ID3D11BlendState* blendState = nullptr;
        FLOAT blendFactor[4] = {};
        UINT sampleMask = 0;
        ID3D11InputLayout* inputLayout = nullptr;
        D3D11_PRIMITIVE_TOPOLOGY topology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
        ID3D11VertexShader* vs = nullptr;
        ID3D11PixelShader* ps = nullptr;
        ID3D11Buffer* vertexBuffer = nullptr;
        UINT vbStride = 0;
        UINT vbOffset = 0;
        ID3D11ShaderResourceView* psSRV = nullptr;
    };

    struct SwapChainResources
    {
        ID3D11RenderTargetView* rtv = nullptr;
    };

    ID3D11Device* m_device = nullptr;
    ID3D11DeviceContext* m_context = nullptr;
    ID3D11VertexShader* m_vs = nullptr;
    ID3D11PixelShader* m_ps = nullptr;
    ID3D11InputLayout* m_inputLayout = nullptr;
    ID3D11Buffer* m_constantBuffer = nullptr;
    ID3D11Buffer* m_vertexBuffer = nullptr;
    ID3D11BlendState* m_blendState = nullptr;
    ID3D11SamplerState* m_samplerState = nullptr;
    ID3D11ShaderResourceView* m_whiteSRV = nullptr;
    ID3D11Texture2D* m_whiteTexture = nullptr;
    bool m_initialized = false;
    std::map<IDXGISwapChain*, SwapChainResources> m_swapChainResources;

private:
    static constexpr float PI = 3.14159265358979323846f;

    static int ClampInt(int v, int lo, int hi);
    static float ClampFloat(float v, float lo, float hi);

    static std::wstring MakeFontKey(const std::wstring& fontFamily, float fontSize, float fontWeight);
    static std::wstring MakeTextKey(const std::wstring& fontFamily, float fontSize, float fontWeight, const std::wstring& text);

    bool QueryDeviceFromSwapChain(IDXGISwapChain* swapChain);
    void BackupState(D3D11StateBackup& s);
    void RestoreState(D3D11StateBackup& s);

    void ReleaseGlobalResources();
    void ReleaseAllSwapChainResources();
    void ReleaseSwapChainResourcesInternal(IDXGISwapChain* swapChain);

    bool CreateShadersAndStates();
    bool CreateSwapChainRTV(IDXGISwapChain* swapChain);

    bool BuildTextTextureFromGDI(
        const std::wstring& text,
        const std::wstring& fontFamily,
        float fontSize,
        float fontWeight,
        ID3D11ShaderResourceView** outSRV,
        UINT* outWidth,
        UINT* outHeight);

    std::vector<Vertex> BuildQuad(float x, float y, float w, float h, float r, float g, float b, float a);
    std::vector<Vertex> BuildLineList(const std::vector<std::pair<float, float>>& points, float r, float g, float b, float a);
    std::vector<std::pair<float, float>> BuildEllipsePoints(float cx, float cy, float rx, float ry, int segments);
    std::vector<std::pair<float, float>> BuildRoundedRectPoints(float left, float top, float right, float bottom, float rx, float ry, int segmentsPerCorner);
    std::vector<Vertex> BuildFilledFan(const std::vector<std::pair<float, float>>& poly, float centerX, float centerY, float r, float g, float b, float a);
    std::vector<Vertex> BuildFilledEllipse(float cx, float cy, float rx, float ry, float r, float g, float b, float a);
    std::vector<Vertex> BuildFilledPolygonFromPath(const std::vector<std::pair<float, float>>& pts, float r, float g, float b, float a);

    void DrawVertices(const std::vector<Vertex>& vertices, D3D11_PRIMITIVE_TOPOLOGY topology, ID3D11ShaderResourceView* srv, UINT viewportWidth, UINT viewportHeight);
    void DrawTextCommand(const Lightning::DrawCommand& cmd, UINT viewportWidth, UINT viewportHeight);
    void DrawLineCommand(const Lightning::DrawCommand& cmd, UINT viewportWidth, UINT viewportHeight);
    void DrawRectCommand(const Lightning::DrawCommand& cmd, UINT viewportWidth, UINT viewportHeight);
    void DrawRoundedRectCommand(const Lightning::DrawCommand& cmd, UINT viewportWidth, UINT viewportHeight);
    void DrawEllipseCommand(const Lightning::DrawCommand& cmd, UINT viewportWidth, UINT viewportHeight);
    void DrawTriangleCommand(const Lightning::DrawCommand& cmd, UINT viewportWidth, UINT viewportHeight);
    void DrawBezierCommand(const Lightning::DrawCommand& cmd, UINT viewportWidth, UINT viewportHeight);

public:
    bool Initialize(IDXGISwapChain* swapChain) override;
    void Shutdown() override;
    bool EnsureSwapChainResources(IDXGISwapChain* swapChain) override;
    void ReleaseSwapChainResources(IDXGISwapChain* swapChain) override;
    void Render(IDXGISwapChain* swapChain, const Lightning::DrawList& drawList) override;
};

D3D11Backend& GetD3D11Backend();
std::atomic<bool>& GetBackendReadyFlag();