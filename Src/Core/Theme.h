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