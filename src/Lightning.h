// Lightning - Minimal Immediate Mode GUI Framework
// 
// API design influenced by Dear ImGui (https://github.com/ocornut/imgui)
// for familiarity and ecosystem compatibility.
// 

#pragma once
#define NOMINMAX

#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <dxgi1_2.h>
#include <dxgi1_4.h>

#include <string>
#include <vector>
#include <chrono>
#include <atomic>
#include <cstdint>
#include <algorithm>
#include <utility>
#include <sstream>
#include <iomanip>

template <typename T>
static inline void SafeRelease(T*& p)
{
    if (p)
    {
        p->Release();
        p = nullptr;
    }
}

static inline std::wstring UTF8ToWString(const std::string& str)
{
    if (str.empty())
        return std::wstring();

    int sizeNeeded = MultiByteToWideChar(
        CP_UTF8,
        0,
        str.data(),
        static_cast<int>(str.size()),
        nullptr,
        0);

    if (sizeNeeded <= 0)
        return std::wstring();

    std::wstring wstr(sizeNeeded, L'\0');
    MultiByteToWideChar(
        CP_UTF8,
        0,
        str.data(),
        static_cast<int>(str.size()),
        &wstr[0],
        sizeNeeded);

    return wstr;
}

template <typename T>
static inline T WaitForProcAddress(const wchar_t* moduleName, const char* procName)
{
    while (true)
    {
        HMODULE hModule = GetModuleHandleW(moduleName);
        if (hModule)
        {
            FARPROC p = GetProcAddress(hModule, procName);
            if (p)
                return reinterpret_cast<T>(p);
        }
        Sleep(10);
    }
}

namespace Lightning
{
    struct IO
    {
    private:
        float m_deltaTime = 1.0f / 60.0f;
        float m_frameRate = 60.0f;
        SIZE m_displaySize = { 0, 0 };
        std::chrono::steady_clock::time_point m_lastTime;

        IO()
            : m_lastTime(std::chrono::steady_clock::now())
        {
        }

    public:
        static IO& GetIO()
        {
            static IO io;
            return io;
        }

        void NewFrame(UINT width, UINT height)
        {
            auto now = std::chrono::steady_clock::now();
            std::chrono::duration<float> elapsed = now - m_lastTime;
            m_lastTime = now;

            m_deltaTime = elapsed.count();
            if (m_deltaTime < 0.000001f)
                m_deltaTime = 0.000001f;

            m_frameRate = 1.0f / m_deltaTime;
            m_displaySize.cx = static_cast<LONG>(width);
            m_displaySize.cy = static_cast<LONG>(height);
        }

        float DeltaTime() const
        {
            return m_deltaTime;
        }

        float Framerate() const
        {
            return m_frameRate;
        }

        SIZE DisplaySize() const
        {
            return m_displaySize;
        }
    };

    enum class DrawCommandType
    {
        Text,
        Line,
        Rect,
        FilledRect,
        RoundedRect,
        FilledRoundedRect,
        Ellipse,
        FilledEllipse,
        Triangle,
        FilledTriangle,
        Bezier
    };

    struct DrawCommand
    {
        DrawCommandType type = DrawCommandType::Text;
        float r = 1.0f;
        float g = 1.0f;
        float b = 1.0f;
        float a = 1.0f;
        float thickness = 1.0f;
        float x1 = 0.0f;
        float y1 = 0.0f;
        float x2 = 0.0f;
        float y2 = 0.0f;
        float x3 = 0.0f;
        float y3 = 0.0f;
        float x4 = 0.0f;
        float y4 = 0.0f;
        float radiusX = 0.0f;
        float radiusY = 0.0f;
        std::wstring text;
        std::wstring fontFamily;
        float fontSize = 16.0f;
        float fontWeight = 400.0f;
    };

    class DrawList
    {
    private:
        std::vector<DrawCommand> m_commands;

    public:
        void Clear()
        {
            m_commands.clear();
        }

        void Reserve(size_t count)
        {
            m_commands.reserve(count);
        }

        const std::vector<DrawCommand>& GetCommands() const
        {
            return m_commands;
        }

        void AddTextW(
            float x,
            float y,
            float r,
            float g,
            float b,
            float a,
            const std::wstring& fontFamily,
            float fontSize,
            float fontWeight,
            const std::wstring& text)
        {
            DrawCommand cmd;
            cmd.type = DrawCommandType::Text;
            cmd.x1 = x;
            cmd.y1 = y;
            cmd.r = r;
            cmd.g = g;
            cmd.b = b;
            cmd.a = a;
            cmd.fontFamily = fontFamily.empty() ? L"Microsoft YaHei UI" : fontFamily;
            cmd.fontSize = fontSize;
            cmd.fontWeight = fontWeight;
            cmd.text = text;
            m_commands.push_back(std::move(cmd));
        }

        void AddText(
            float x,
            float y,
            float r,
            float g,
            float b,
            float a,
            const std::string& fontFamilyUtf8,
            float fontSize,
            float fontWeight,
            const std::string& textUtf8)
        {
            AddTextW(
                x,
                y,
                r,
                g,
                b,
                a,
                UTF8ToWString(fontFamilyUtf8),
                fontSize,
                fontWeight,
                UTF8ToWString(textUtf8));
        }

        void AddLine(
            float x1,
            float y1,
            float x2,
            float y2,
            float r,
            float g,
            float b,
            float a,
            float thickness = 1.0f)
        {
            DrawCommand cmd;
            cmd.type = DrawCommandType::Line;
            cmd.x1 = x1;
            cmd.y1 = y1;
            cmd.x2 = x2;
            cmd.y2 = y2;
            cmd.r = r;
            cmd.g = g;
            cmd.b = b;
            cmd.a = a;
            cmd.thickness = thickness;
            m_commands.push_back(std::move(cmd));
        }

        void AddRect(
            float left,
            float top,
            float right,
            float bottom,
            float r,
            float g,
            float b,
            float a,
            float thickness = 1.0f)
        {
            DrawCommand cmd;
            cmd.type = DrawCommandType::Rect;
            cmd.x1 = left;
            cmd.y1 = top;
            cmd.x2 = right;
            cmd.y2 = bottom;
            cmd.r = r;
            cmd.g = g;
            cmd.b = b;
            cmd.a = a;
            cmd.thickness = thickness;
            m_commands.push_back(std::move(cmd));
        }

        void AddFilledRect(
            float left,
            float top,
            float right,
            float bottom,
            float r,
            float g,
            float b,
            float a)
        {
            DrawCommand cmd;
            cmd.type = DrawCommandType::FilledRect;
            cmd.x1 = left;
            cmd.y1 = top;
            cmd.x2 = right;
            cmd.y2 = bottom;
            cmd.r = r;
            cmd.g = g;
            cmd.b = b;
            cmd.a = a;
            m_commands.push_back(std::move(cmd));
        }

        void AddRoundedRect(
            float left,
            float top,
            float right,
            float bottom,
            float radiusX,
            float radiusY,
            float r,
            float g,
            float b,
            float a,
            float thickness = 1.0f)
        {
            DrawCommand cmd;
            cmd.type = DrawCommandType::RoundedRect;
            cmd.x1 = left;
            cmd.y1 = top;
            cmd.x2 = right;
            cmd.y2 = bottom;
            cmd.radiusX = radiusX;
            cmd.radiusY = radiusY;
            cmd.r = r;
            cmd.g = g;
            cmd.b = b;
            cmd.a = a;
            cmd.thickness = thickness;
            m_commands.push_back(std::move(cmd));
        }

        void AddFilledRoundedRect(
            float left,
            float top,
            float right,
            float bottom,
            float radiusX,
            float radiusY,
            float r,
            float g,
            float b,
            float a)
        {
            DrawCommand cmd;
            cmd.type = DrawCommandType::FilledRoundedRect;
            cmd.x1 = left;
            cmd.y1 = top;
            cmd.x2 = right;
            cmd.y2 = bottom;
            cmd.radiusX = radiusX;
            cmd.radiusY = radiusY;
            cmd.r = r;
            cmd.g = g;
            cmd.b = b;
            cmd.a = a;
            m_commands.push_back(std::move(cmd));
        }

        void AddEllipse(
            float centerX,
            float centerY,
            float radiusX,
            float radiusY,
            float r,
            float g,
            float b,
            float a,
            float thickness = 1.0f)
        {
            DrawCommand cmd;
            cmd.type = DrawCommandType::Ellipse;
            cmd.x1 = centerX;
            cmd.y1 = centerY;
            cmd.radiusX = radiusX;
            cmd.radiusY = radiusY;
            cmd.r = r;
            cmd.g = g;
            cmd.b = b;
            cmd.a = a;
            cmd.thickness = thickness;
            m_commands.push_back(std::move(cmd));
        }

        void AddFilledEllipse(
            float centerX,
            float centerY,
            float radiusX,
            float radiusY,
            float r,
            float g,
            float b,
            float a)
        {
            DrawCommand cmd;
            cmd.type = DrawCommandType::FilledEllipse;
            cmd.x1 = centerX;
            cmd.y1 = centerY;
            cmd.radiusX = radiusX;
            cmd.radiusY = radiusY;
            cmd.r = r;
            cmd.g = g;
            cmd.b = b;
            cmd.a = a;
            m_commands.push_back(std::move(cmd));
        }

        void AddTriangle(
            float ax,
            float ay,
            float bx,
            float by,
            float cx,
            float cy,
            float r,
            float g,
            float b,
            float a,
            float thickness = 1.0f)
        {
            DrawCommand cmd;
            cmd.type = DrawCommandType::Triangle;
            cmd.x1 = ax;
            cmd.y1 = ay;
            cmd.x2 = bx;
            cmd.y2 = by;
            cmd.x3 = cx;
            cmd.y3 = cy;
            cmd.r = r;
            cmd.g = g;
            cmd.b = b;
            cmd.a = a;
            cmd.thickness = thickness;
            m_commands.push_back(std::move(cmd));
        }

        void AddFilledTriangle(
            float ax,
            float ay,
            float bx,
            float by,
            float cx,
            float cy,
            float r,
            float g,
            float b,
            float a)
        {
            DrawCommand cmd;
            cmd.type = DrawCommandType::FilledTriangle;
            cmd.x1 = ax;
            cmd.y1 = ay;
            cmd.x2 = bx;
            cmd.y2 = by;
            cmd.x3 = cx;
            cmd.y3 = cy;
            cmd.r = r;
            cmd.g = g;
            cmd.b = b;
            cmd.a = a;
            m_commands.push_back(std::move(cmd));
        }

        void AddBezier(
            float sx,
            float sy,
            float c1x,
            float c1y,
            float c2x,
            float c2y,
            float ex,
            float ey,
            float r,
            float g,
            float b,
            float a,
            float thickness = 1.0f)
        {
            DrawCommand cmd;
            cmd.type = DrawCommandType::Bezier;
            cmd.x1 = sx;
            cmd.y1 = sy;
            cmd.x2 = c1x;
            cmd.y2 = c1y;
            cmd.x3 = c2x;
            cmd.y3 = c2y;
            cmd.x4 = ex;
            cmd.y4 = ey;
            cmd.r = r;
            cmd.g = g;
            cmd.b = b;
            cmd.a = a;
            cmd.thickness = thickness;
            m_commands.push_back(std::move(cmd));
        }
    };

    class GUI
    {
    private:
        DrawList m_drawList;
        GUI() = default;

    public:
        static GUI* GetBackEnd()
        {
            static GUI instance;
            return &instance;
        }

        DrawList* GetDrawList()
        {
            return &m_drawList;
        }
    };

    void OnPaint();

    namespace Hook
    {
        bool Install();
        void Uninstall();
    }
}

class IRenderBackend
{
public:
    virtual ~IRenderBackend() = default;
    virtual bool Initialize(IDXGISwapChain* swapChain) = 0;
    virtual void Shutdown() = 0;
    virtual bool EnsureSwapChainResources(IDXGISwapChain* swapChain) = 0;
    virtual void ReleaseSwapChainResources(IDXGISwapChain* swapChain) = 0;
    virtual void Render(IDXGISwapChain* swapChain, const Lightning::DrawList& drawList) = 0;
};