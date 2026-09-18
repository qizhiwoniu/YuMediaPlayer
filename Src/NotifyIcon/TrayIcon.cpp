#include "TrayIcon.h"
#include <windows.h>
#include <shellapi.h>
#include <wininet.h>
#include <string>

#pragma comment(lib, "wininet.lib")
#pragma comment(lib, "shell32.lib")

    static TrayIcon* g_trayInstance = nullptr;

    TrayIcon::TrayIcon(HINSTANCE hInstance, const std::wstring& tooltip)
        : m_hInstance(hInstance)
        , m_hwnd(nullptr)
        , m_parentHwnd(nullptr)
        , m_nid{}
        , m_tooltip(tooltip)
        , m_created(false)
        , m_blinkIcon1(nullptr)
        , m_blinkIcon2(nullptr)
        , m_blinkState(false)
        , m_blinking(false)
        , m_iconPath()
        , m_ownedIcon(nullptr)
    {
        g_trayInstance = this;
        ZeroMemory(&m_nid, sizeof(m_nid));
    }

    TrayIcon::~TrayIcon() {
        // 先解除全局实例，防止窗口/定时器消息再次访问已经析构的对象。
        if (g_trayInstance == this)
            g_trayInstance = nullptr;

        StopBlink();
        Remove();

        if (m_ownedIcon) {
            DestroyIcon(m_ownedIcon);
            m_ownedIcon = nullptr;
        }

        if (m_hwnd) {
            DestroyWindow(m_hwnd);
            m_hwnd = nullptr;
        }
    }

    bool TrayIcon::Create(HWND parentHwnd) {
        m_parentHwnd = parentHwnd;
        // 注册隐藏窗口类（用于接收托盘消息）
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = WndProc;
        wc.hInstance = m_hInstance;
        wc.lpszClassName = L"YuMediaPlayer_TrayClass";
        if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        return false;

        m_hwnd = CreateWindowExW(0, L"YuMediaPlayer_TrayClass", L"",
            0, 0, 0, 0, 0,
            HWND_MESSAGE,   // 消息专用窗口，不显示
            nullptr, m_hInstance, nullptr);
        if (!m_hwnd) return false;

        // 填写 NOTIFYICONDATA
        m_nid.cbSize = sizeof(m_nid);
        m_nid.hWnd = m_hwnd;
        m_nid.uID = ID_TRAY_ICON;
        m_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
        m_nid.uCallbackMessage = WM_TRAYICON;
        // 记住路径，DPI 变化（WM_DPICHANGED）时要用同一张图重新按新尺寸加载
        wchar_t exePath[MAX_PATH] = {};
        DWORD pathLength = GetModuleFileNameW(nullptr, exePath, MAX_PATH); if (pathLength == 0)
        {
            m_iconPath = L"icons\\logo.ico";
        }
        else
        {
            std::wstring fullPath(exePath);
            std::wstring::size_type slash = fullPath.find_last_of(L"\\/");
            if (slash != std::wstring::npos)
            {
                fullPath.resize(slash + 1);
            }
            else { fullPath.clear(); }
            m_iconPath = fullPath + L"icons\\logo.ico";
        }
        
        m_ownedIcon = LoadTrayIconFromFile(m_iconPath);
        if (m_ownedIcon != nullptr) { m_nid.hIcon = m_ownedIcon; }
        else {
            // 加载失败时使用系统默认图标 
            m_nid.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
            if (m_nid.hIcon == nullptr)
            {
                DestroyWindow(m_hwnd);
                m_hwnd = nullptr; return false;
            }
        }

        wcsncpy_s(m_nid.szTip, m_tooltip.c_str(), _TRUNCATE);

        m_created = Shell_NotifyIconW(NIM_ADD, &m_nid);

        // 设置版本（支持气泡通知）
        m_nid.uVersion = NOTIFYICON_VERSION_4;
        Shell_NotifyIconW(NIM_SETVERSION, &m_nid);

        return m_created;
    }

    void TrayIcon::Remove() {
        if (m_created) {
            Shell_NotifyIconW(NIM_DELETE, &m_nid);
            m_created = false;
        }
    }

    void TrayIcon::ShowBalloon(const std::wstring& title,
        const std::wstring& message,
        DWORD timeoutMs,
        DWORD infoFlags)
    {
        m_nid.uFlags |= NIF_INFO;
        m_nid.uTimeout = timeoutMs;
        m_nid.dwInfoFlags = infoFlags;
        wcsncpy_s(m_nid.szInfoTitle, title.c_str(), _TRUNCATE);
        wcsncpy_s(m_nid.szInfo, message.c_str(), _TRUNCATE);
        Shell_NotifyIconW(NIM_MODIFY, &m_nid);
        // 清除 NIF_INFO 防止每次修改都弹出
        m_nid.uFlags &= ~NIF_INFO;
    }

    void TrayIcon::SetIcon(HICON hIcon) {
        if (!hIcon || !m_created || !m_hwnd)
            return;

        m_nid.hIcon = hIcon;
        Shell_NotifyIconW(NIM_MODIFY, &m_nid);
    }

    // 按"当前 DPI 下系统认为的小图标尺寸"加载，而不是写死 16x16——
    // 缩放比例变了（比如 125%/150%/200%），这个尺寸会自动变成 20/24/32 等，
    // 避免系统把一张 16x16 的图硬拉伸导致看起来又小又糊。
    // 前提：icons/logo.ico 最好是包含多个尺寸(16/20/24/32/48...)的多分辨率 ico，
    // 只有一个尺寸的话，系统依然会拉伸这一张图，清晰度提升有限。
    HICON TrayIcon::LoadTrayIconFromFile(const std::wstring& path) const {
        int cx = GetSystemMetrics(SM_CXSMICON);
        int cy = GetSystemMetrics(SM_CYSMICON);
        return (HICON)LoadImage(
            nullptr,             // hInstance 填 nullptr 表示从文件加载
            path.c_str(),        // 图标文件路径
            IMAGE_ICON,
            cx, cy,
            LR_LOADFROMFILE | LR_DEFAULTCOLOR
        );
    }

    void TrayIcon::ReloadIconForDpi() {
        if (m_iconPath.empty())
            return;

        HICON newIcon = LoadTrayIconFromFile(m_iconPath);
        if (!newIcon)
            return; // 加载失败就保留原图标，不动

        SetIcon(newIcon);

        if (m_ownedIcon)
            DestroyIcon(m_ownedIcon);
        m_ownedIcon = newIcon;
    }

    void TrayIcon::SetTooltip(const std::wstring& tooltip) {
        wcsncpy_s(m_nid.szTip, tooltip.c_str(), _TRUNCATE);
        Shell_NotifyIconW(NIM_MODIFY, &m_nid);
    }

    // ── 闪烁（模拟QQ未读消息） ──────────────────────────
    void TrayIcon::StartBlink(HICON icon1, HICON icon2, int intervalMs) {
        if (!m_hwnd || !m_created || !icon1 || !icon2)
            return;

        if (intervalMs < 50)
            intervalMs = 50;

        StopBlink();

        m_blinkIcon1 = icon1;
        m_blinkIcon2 = icon2;
        m_blinking = true;
        m_blinkState = false;
        SetIcon(m_blinkIcon1);
        SetTimer(m_hwnd, 42, static_cast<UINT>(intervalMs), BlinkTimerProc);
    }

    void TrayIcon::StopBlink() {
        if (m_hwnd)
            KillTimer(m_hwnd, 42);

        if (m_blinking) {
            m_blinking = false;
            if (m_blinkIcon1)
                SetIcon(m_blinkIcon1);
        }

        m_blinkIcon1 = nullptr;
        m_blinkIcon2 = nullptr;
        m_blinkState = false;
    }

    void CALLBACK TrayIcon::BlinkTimerProc(HWND hwnd, UINT, UINT_PTR, DWORD) {
        if (!g_trayInstance || !g_trayInstance->m_blinking)
            return;

        if (!g_trayInstance->m_blinkIcon1 ||
            !g_trayInstance->m_blinkIcon2)
            return;

        g_trayInstance->m_blinkState = !g_trayInstance->m_blinkState;
        g_trayInstance->SetIcon(g_trayInstance->m_blinkState
            ? g_trayInstance->m_blinkIcon2
            : g_trayInstance->m_blinkIcon1);
    }

    // ── 右键菜单 ────────────────────────────────────────
    void TrayIcon::ShowContextMenu() {
        HMENU hMenu = CreatePopupMenu();
        AppendMenuW(hMenu, MF_STRING, ID_TRAY_SHOW, L"显示 YuMediaPlayer");
        AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(hMenu, MF_STRING, ID_TRAY_CHECK, L"检查更新");
        AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(hMenu, MF_STRING, ID_TRAY_EXIT, L"退出");

        POINT pt;
        GetCursorPos(&pt);
        SetForegroundWindow(m_hwnd);   // 必须，否则菜单不会自动关闭
        TrackPopupMenu(hMenu, TPM_BOTTOMALIGN | TPM_LEFTALIGN,
            pt.x, pt.y, 0, m_hwnd, nullptr);
        DestroyMenu(hMenu);
    }

    // ── 消息处理 ────────────────────────────────────────
    void TrayIcon::OnTrayMessage(WPARAM wParam, LPARAM lParam) {
        UINT msg = LOWORD(lParam);
        switch (msg) {
        case WM_LBUTTONDBLCLK:   // 双击 → 显示主窗口
            if (m_parentHwnd)
            {
                ::ShowWindow(m_parentHwnd, SW_RESTORE);
                ::SetForegroundWindow(m_parentHwnd);
            }
            break;
        case WM_RBUTTONUP:        // 右键 → 弹出菜单
            ShowContextMenu();
            break;
        case NIN_BALLOONUSERCLICK: // 点击气泡通知
            if (m_parentHwnd)
            {
                ::ShowWindow(m_parentHwnd, SW_RESTORE);
                ::SetForegroundWindow(m_parentHwnd);
            }
            break;
        }
    }

    LRESULT CALLBACK TrayIcon::WndProc(HWND hwnd, UINT msg,
        WPARAM wParam, LPARAM lParam)
    {
        if (!g_trayInstance) return DefWindowProcW(hwnd, msg, wParam, lParam);

        switch (msg) {
        case WM_TRAYICON:
            g_trayInstance->OnTrayMessage(wParam, lParam);
            return 0;
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
            case ID_TRAY_EXIT:
                PostQuitMessage(0);
                break;
            case ID_TRAY_SHOW:
                // TODO: 显示你的主窗口
                if (g_trayInstance->m_parentHwnd)
                ShowWindow(g_trayInstance->m_parentHwnd, SW_RESTORE);
                break;
            case ID_TRAY_CHECK:
                g_trayInstance->CheckUpdate();
                break;
            }
            return 0;
        }
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    std::wstring TrayIcon::HttpGet(const std::wstring& url) {
        std::string result;

        HINTERNET hInternet = InternetOpenW(
            L"YuMediaPlayer-Updater",
            INTERNET_OPEN_TYPE_PRECONFIG,
            nullptr, nullptr, 0);
        if (!hInternet) return L"";

        HINTERNET hConnect = InternetOpenUrlW(
            hInternet,
            url.c_str(),
            nullptr, 0,
            INTERNET_FLAG_RELOAD | INTERNET_FLAG_SECURE,  // HTTPS
            0);

        if (hConnect) {
            char buf[4096];
            DWORD bytesRead = 0;
            while (InternetReadFile(hConnect, buf, sizeof(buf) - 1, &bytesRead)
                && bytesRead > 0) {
                buf[bytesRead] = '\0';
                result += buf;
            }
            InternetCloseHandle(hConnect);
        }
        InternetCloseHandle(hInternet);

        // GitHub API 返回 UTF-8，正确转换为 UTF-16。
        if (result.empty()) return L"";

        int len = MultiByteToWideChar(
            CP_UTF8, 0, result.data(), static_cast<int>(result.size()),
            nullptr, 0);
        if (len <= 0) return L"";

        std::wstring wideResult(static_cast<size_t>(len), L'\0');
        MultiByteToWideChar(
            CP_UTF8, 0, result.data(), static_cast<int>(result.size()),
            &wideResult[0], len);
        return wideResult;
    }

    // ── 从 GitHub API JSON 里提取 tag_name ───────────────────
    // 返回示例："1.2.0"（去掉前缀 'v'）
    std::wstring TrayIcon::ParseLatestVersion(const std::string& json) {
        // GitHub返回: {..., "tag_name": "v1.2.0", ...}
        const std::string key = "\"tag_name\":";
        size_t pos = json.find(key);
        if (pos == std::string::npos) return L"";

        pos = json.find('"', pos + key.size());   // 找到值的开头引号
        if (pos == std::string::npos) return L"";
        pos++;  // 跳过引号

        // 跳过可能的 'v' 前缀，先检查边界
        if (pos >= json.size()) return L"";
        if (json[pos] == 'v') pos++;

        size_t end = json.find('"', pos);
        if (end == std::string::npos) return L"";

        std::string ver = json.substr(pos, end - pos);
        return std::wstring(ver.begin(), ver.end());
    }

    // ── 主逻辑：检查更新 ──────────────────────────────────────
    void TrayIcon::CheckUpdate() {
        // ✅ 替换成你自己的 GitHub 用户名/仓库名
        const std::wstring apiUrl =
            L"https://api.github.com/repos/qizhiwoniu/YuMediaPlayer/releases/latest";

        // 先弹提示"正在检查"
        ShowBalloon(L"检查更新", L"正在连接 GitHub...", 2000, NIIF_INFO);

        std::wstring response = HttpGet(apiUrl);
        if (response.empty()) {
            ShowBalloon(L"检查更新失败", L"无法连接到 GitHub，请检查网络。",
                3000, NIIF_ERROR);
            return;
        }

        // 转回 UTF-8 string 用于 JSON 解析。
        int jsonLen = WideCharToMultiByte(
            CP_UTF8, 0, response.c_str(), static_cast<int>(response.size()),
            nullptr, 0, nullptr, nullptr);
        if (jsonLen <= 0) {
            ShowBalloon(L"检查更新失败", L"版本信息编码转换失败。",
                3000, NIIF_ERROR);
            return;
        }

        std::string json(static_cast<size_t>(jsonLen), '\0');
        WideCharToMultiByte(
            CP_UTF8, 0, response.c_str(), static_cast<int>(response.size()),
            &json[0], jsonLen, nullptr, nullptr);

        std::wstring latestVer = ParseLatestVersion(json);

        if (latestVer.empty()) {
            ShowBalloon(L"检查更新失败", L"无法解析版本信息。",
                3000, NIIF_ERROR);
            return;
        }

        // APP_VERSION 通常是 L"1.0.0"。这里显式构造 wstring，
        // 避免宏类型/字符串拼接造成临时对象和编码问题。
#ifdef APP_VERSION
        const std::wstring currentVer = APP_VERSION;
#else
        const std::wstring currentVer = L"0.0.0";
#endif

        if (latestVer == currentVer) {
            ShowBalloon(
                L"已是最新版本",
                L"当前版本 v" + currentVer + L" 已是最新。",
                3000,
                NIIF_INFO);
        }
        else {
            std::wstring msg =
                L"发现新版本 v" + latestVer +
                L"\n当前版本 v" + currentVer +
                L"\n\n是否前往 GitHub 下载？";

            int ret = MessageBox(m_parentHwnd, msg.c_str(),
                L"YuMediaPlayer 有新版本", MB_YESNO | MB_ICONINFORMATION);
            if (ret == IDYES) {
                // 打开浏览器跳转到 releases 页
                std::wstring releaseUrl =
                    L"https://github.com/qizhiwoniu/YuMediaPlayer/releases/latest";
                ShellExecuteW(nullptr, L"open", releaseUrl.c_str(),
                    nullptr, nullptr, SW_SHOWNORMAL);
            }
        }
    }