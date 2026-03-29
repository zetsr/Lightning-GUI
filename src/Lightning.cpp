#include "Lightning.h"

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