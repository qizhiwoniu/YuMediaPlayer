#include "MainWindow.h"
#include "NotifyIcon/TrayIcon.h"
#include "Core/Theme.h"
#include "Core/CircularAvatar.h"
#include <memory>

using namespace YuMediaPlayer;

#ifdef USE_WIN32 
static int APIENTRY WinMain(HINSTANCE hInst, HINSTANCE, PSTR, int)
{
    CircularAvatar::InitGdiplus();
    {
        auto mainWindow = std::make_unique<MainWindow>();
        mainWindow->Initialize(L"YuMediaPlayer", 350, 104);
        mainWindow->Run();
    }
    CircularAvatar::ShutdownGdiplus();

    return 0;
}
#else
int main(int argc, char* argv[])
{
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    printf("YuMediaPlayer is running...\n Hello, World!\n 你好世界\n");

    CircularAvatar::InitGdiplus();
    {
        auto mainWindow = std::make_unique<MainWindow>();
        mainWindow->Initialize(L"YuMediaPlayer", 350, 104);
        mainWindow->Run();
    }
    CircularAvatar::ShutdownGdiplus();

    return 0;
}
#endif
