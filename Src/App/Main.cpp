#include "MainWindow.h"
#include "NotifyIcon/TrayIcon.h"
#include "Core/Theme.h"

using namespace YuMediaPlayer;

#ifdef USE_WIN32 
static int APIENTRY WinMain(HINSTANCE hInst, HINSTANCE, PSTR, int)
{
    MainWindow mainWindow;
    mainWindow.Initialize(L"YuMediaPlayer", 320, 40);
    mainWindow.Run();
    return 0;

}
#else
int main(int argc, char* argv[])
{
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    
    printf("YuMediaPlayer is running...\n Hello, World!\n 你好世界\n");
    
    MainWindow mainWindow;
    mainWindow.Initialize(L"YuMediaPlayer", 320, 40);
    mainWindow.Run();
    
    return 0;
}
#endif