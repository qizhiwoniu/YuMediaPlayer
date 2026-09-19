#pragma once
#include <windows.h>
#include <shellapi.h>
#include <string>
#include <wininet.h>
#include "../../version.h" 
#include "UI/WindowGUI.h"

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
class TrayIcon {
public:
    TrayIcon(HINSTANCE hInstance, const std::wstring& tooltip);
    ~TrayIcon();

    bool Create(HWND parentHwnd = nullptr);
    void Remove();
    // 显示气泡通知（类似QQ消息提示）
    void ShowBalloon(const std::wstring& title,
        const std::wstring& message,
        DWORD timeoutMs = 3000,
        DWORD infoFlags = NIIF_INFO);

    // 更新图标（比如未读消息时换红点图标）
    void SetIcon(HICON hIcon);
    
    // 设置通知消息中显示的图标（与托盘图标分开）
    void SetNotificationIcon(HICON hIcon);
    
    void SetTooltip(const std::wstring& tooltip);

    // 闪烁效果（模拟QQ消息闪烁）
    void StartBlink(HICON icon1, HICON icon2, int intervalMs = 500);
    void StopBlink();

    HWND GetHwnd() const { return m_hwnd; }

    // DPI（系统缩放比例）变化时调用，会按新的 DPI 对应尺寸重新加载 icons/logo.ico。
    // 注意：托盘用的这个隐藏窗口是 HWND_MESSAGE 消息专用窗口，不在屏幕上、
    // 不关联具体显示器，系统不会给它发 WM_DPICHANGED；真正会收到这个消息的
    // 是主窗口（MainWindow 那个可见的顶层窗口），所以要在主窗口处理
    // WM_DPICHANGED 的地方调用一下 m_trayIcon.ReloadIconForDpi()。
    // 
    // 推荐做法：在 MainWindow::WndProc 中处理 WM_DPICHANGED：
    //   case WM_DPICHANGED:
    //       m_trayIcon.ReloadIconForDpi();  // 刷新托盘图标
    //       break;
    void ReloadIconForDpi();

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg,
        WPARAM wParam, LPARAM lParam);
    void OnTrayMessage(WPARAM wParam, LPARAM lParam);
    void ShowContextMenu();
    static void CALLBACK BlinkTimerProc(HWND hwnd, UINT msg,
        UINT_PTR id, DWORD time);
    void CheckUpdate();                          // 检查更新
    std::wstring HttpGet(const std::wstring& url); // 请求GitHub API
    std::wstring ParseLatestVersion(const std::string& json); // 解析版本号
    HINSTANCE       m_hInstance;
    HWND            m_hwnd;
    HWND            m_parentHwnd;
    NOTIFYICONDATA  m_nid;
    std::wstring    m_tooltip;
    bool            m_created;

    // 闪烁相关
    HICON   m_blinkIcon1;
    HICON   m_blinkIcon2;
    bool    m_blinkState;
    bool    m_blinking;

    // ── 图标随 DPI 自适应 ──────────────────────────────────
    std::wstring m_iconPath;     // 记住加载路径，DPI 变化时用同一张图重新按新尺寸加载
    HICON        m_ownedIcon;    // 当前通过 LoadImage 从文件加载、由本类持有/负责销毁的图标句柄
    HICON        LoadTrayIconFromFile(const std::wstring& path) const; // 按当前 DPI 对应的小图标尺寸加载
};
