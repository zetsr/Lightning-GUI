# Lightning-GUI

## 📌 项目简介

Lightning 是一个轻量级的绘制与叠加层渲染模块，主要用于在图形应用中快速绘制文本。
它通过 Hook 机制注入目标进程，在渲染阶段调用用户自定义的 `OnPaint()` 函数，从而实现自定义绘制逻辑。

---

## 🧩 核心模块结构

Lightning 主要由以下几个核心组件组成：

### 1. `Lightning::GUI`

负责与底层渲染后端交互，提供绘制入口。

**主要接口：**

```cpp
static GUI* GetBackEnd();
DrawList* GetDrawList();
```

---

### 2. `Lightning::DrawList`

绘制指令列表，所有绘制操作最终都提交到这里。

**常用方法：**

```cpp
void AddTextW(
    float x, float y,
    float r, float g, float b, float a,
    const wchar_t* font,
    float size,
    float weight,
    const std::wstring& text
);
```

---

### 3. `Lightning::IO`

用于获取实时运行时信息（帧率、时间、窗口尺寸等）。

**主要接口：**

```cpp
static IO& GetIO();

float DeltaTime();
float Framerate();
SIZE DisplaySize();
```

---

## 🎯 使用流程

### `OnPaint()`

这是整个系统的灵魂函数，每一帧都会调用它。

示例：

```cpp
namespace Lightning
{
    void OnPaint()
    {
        Lightning::GUI* gui = Lightning::GUI::GetBackEnd();
        if (!gui)
            return;

        Lightning::DrawList* drawList = gui->GetDrawList();
        if (!drawList)
            return;

        Lightning::IO& io = Lightning::IO::GetIO();
        float dt = io.DeltaTime();
        float fps = io.Framerate();
        SIZE size = io.DisplaySize();

        std::wstringstream detail;
        detail << L"当前帧信息：";
        detail << L" dt=" << std::fixed << std::setprecision(4) << dt;
        detail << L" fps=" << std::fixed << std::setprecision(1) << fps;
        detail << L" size=" << size.cx << L"x" << size.cy;

        drawList->AddTextW(
            20.0f,
            82.0f,
            0.9f,
            0.9f,
            0.9f,
            1.0f,
            L"Microsoft YaHei UI",
            16.0f,
            400.0f,
            detail.str());
    }
}
```

---

### 确保 Hook 正确执行

Lightning 本身不会主动运行，它依赖 DLL 注入和 Hook 触发。

必须确保：

* `DllMain` 被成功执行
* Hook 初始化逻辑已运行
* 渲染函数（如 Present / EndScene）被正确 Hook
* 在 Hook 回调中调用 `Lightning::OnPaint()`

---
