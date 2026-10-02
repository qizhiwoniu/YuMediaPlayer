#pragma once
#include <windows.h>
#include <shellapi.h>
#include <string>
#include <wininet.h>
#include "../../version.h" 

#pragma comment(lib, "Wininet.lib")
#pragma comment(lib, "Shell32.lib")

#define WM_TRAYICON (WM_USER + 1)   // 自定义托盘消息
#define ID_TRAY_ICON 1001
#define ID_TRAY_EXIT 2001
#define ID_TRAY_SHOW 2002
#define ID_TRAY_CHECK 2003
#define ID_TRAY_CLOSETXT 2004
#define ID_TRAY_SETTINGS 2005
#define ID_THEME_LIGHT 2006
#define ID_THEME_DARK 2007
class Theme {
public:

    //background - color: f0f0f0;
	//background - color: #000000;

private:

};


// ======================================================================
//  界面主题（深色 / 浅色）—— 全局共享，设置页改了以后主窗口、迷你窗口一起变
//  0 = 深色（默认）  1 = 浅色
//  数值保存在 userprofile\ConfigSettings.ini 的 [Theme] Skin，第一次用到时自动读取。
// ======================================================================
namespace YuMediaPlayer
{
    namespace UITheme
    {
        // 读 exe 目录下 userprofile\ConfigSettings.ini 里的 [Theme] Skin（和 SettingPage 用同一个文件）
        inline int ReadModeFromIni()
        {
            wchar_t path[MAX_PATH] = { 0 };
            GetModuleFileNameW(nullptr, path, MAX_PATH);
            wchar_t* slash = wcsrchr(path, L'\\');
            if (slash)
                *slash = L'\0';
            std::wstring ini = std::wstring(path) + L"\\userprofile\\ConfigSettings.ini";
            return GetPrivateProfileIntW(L"Theme", L"Skin", 0, ini.c_str()) == 1 ? 1 : 0;
        }

        inline int& ModeRef()
        {
            static int s_mode = ReadModeFromIni();
            return s_mode;
        }

        inline bool IsLight() { return ModeRef() == 1; }

        // 按当前主题二选一：深色用 dark，浅色用 light
        inline COLORREF Pick(COLORREF dark, COLORREF light) { return IsLight() ? light : dark; }

        // 主题变化通知消息（注册消息）。迷你窗口是 UpdateLayeredWindow 的分层窗口，
        // 光靠 RedrawWindow 不一定会重画，所以额外给每个窗口 Post 一条这个消息，
        // MainWindow 收到后自己 Composite()。其它窗口不认识这条消息，DefWindowProc 会忽略。
        inline UINT ThemeChangedMessage()
        {
            static UINT s_msg = RegisterWindowMessageW(L"YuMediaPlayer.ThemeChanged");
            return s_msg;
        }

        inline BOOL CALLBACK RedrawEnumProc(HWND hwnd, LPARAM)
        {
            RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
            PostMessageW(hwnd, ThemeChangedMessage(), 0, 0);
            return TRUE;
        }

        // 重绘当前线程里的所有窗口（主窗口、迷你窗口、设置页……）
        inline void RefreshAllWindows()
        {
            EnumThreadWindows(GetCurrentThreadId(), RedrawEnumProc, 0);
        }

        // 切换主题并立刻刷新所有窗口；主题没变就什么都不做
        inline void SetMode(int mode)
        {
            mode = (mode == 1) ? 1 : 0;
            if (ModeRef() == mode)
                return;
            ModeRef() = mode;
            RefreshAllWindows();
        }
    }
}
