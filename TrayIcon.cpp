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

        // 填写 NOTIFYICONDATA - 关键：必须全部初始化为 0，然后填充必要字段
        // ✅ 不能有任何垃圾数据，否则 Shell_NotifyIcon 可能会失败
        ZeroMemory(&m_nid, sizeof(m_nid));
        
        m_nid.cbSize = sizeof(m_nid);
        m_nid.hWnd = m_hwnd;
        m_nid.uID = ID_TRAY_ICON;
        // ✅ 必须包含这些标志，缺一不可
        m_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_SHOWTIP;
        m_nid.uCallbackMessage = WM_TRAYICON;
        
        // ✅ 设置版本，支持更新消息
        m_nid.uVersion = NOTIFYICON_VERSION_4;
        
        // 获取可执行文件路径
        wchar_t exePath[MAX_PATH] = {};
        DWORD pathLength = GetModuleFileNameW(nullptr, exePath, MAX_PATH);
        
        if (pathLength > 0)
        {
            std::wstring fullPath(exePath);
            std::wstring::size_type slash = fullPath.find_last_of(L"\\/");
            if (slash != std::wstring::npos)
            {
                fullPath.resize(slash + 1);
                m_iconPath = fullPath + L"icons\\logo.ico";
            }
            else
            {
                m_iconPath = L"icons\\logo.ico";
            }
        }
        else
        {
            m_iconPath = L"icons\\logo.ico";
        }
        
        // ✅ 尝试从文件加载图标
        m_ownedIcon = LoadTrayIconFromFile(m_iconPath);
        
        if (m_ownedIcon == nullptr)
        {
            // 备选方案 1: 从可执行文件中提取图标
            // 尝试提取 exe 自身的图标（如果编译时包含了）
            HICON hExeIcon = nullptr;
            UINT iconCount = ExtractIconExW(exePath, 0, nullptr, &hExeIcon, 1);
            if (hExeIcon != nullptr)
            {
                m_ownedIcon = hExeIcon;
                m_iconPath.clear();  // 标记为系统/提取的图标
            }
        }
        
        if (m_ownedIcon == nullptr)
        {
            // 备选方案 2: 使用系统默认图标
            m_ownedIcon = LoadIconW(nullptr, IDI_APPLICATION);
            if (m_ownedIcon)
            {
                m_iconPath.clear();
            }
        }
        
        if (m_ownedIcon == nullptr)
        {
            // 备选方案 3: 使用信息图标
            m_ownedIcon = LoadIconW(nullptr, IDI_INFORMATION);
            if (m_ownedIcon)
            {
                m_iconPath.clear();
            }
        }

        if (m_ownedIcon == nullptr)
        {
            // 无法加载任何图标，失败
            DestroyWindow(m_hwnd);
            m_hwnd = nullptr;
            return false;
        }

        m_nid.hIcon = m_ownedIcon;
        wcsncpy_s(m_nid.szTip, m_tooltip.c_str(), _TRUNCATE);

        // ✅ 第 1 步：添加图标到系统托盘
        m_created = Shell_NotifyIconW(NIM_ADD, &m_nid);
        
        if (!m_created)
        {
            // 添加失败，清理并返回
            if (!m_iconPath.empty() && m_ownedIcon)
            {
                DestroyIcon(m_ownedIcon);
            }
            m_ownedIcon = nullptr;
            DestroyWindow(m_hwnd);
            m_hwnd = nullptr;
            return false;
        }

        // ✅ 第 2 步：设置版本（支持气泡通知和现代消息）
        // 注意：这必须在 NIM_ADD 之后调用
        Shell_NotifyIconW(NIM_SETVERSION, &m_nid);

        return true;
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
        if (!m_created || !m_hwnd)
            return;

        // 临时保存原始标志
        UINT originalFlags = m_nid.uFlags;
        
        // 设置气泡通知相关字段
        m_nid.uFlags |= NIF_INFO;
        m_nid.uTimeout = timeoutMs;
        m_nid.dwInfoFlags = infoFlags;
        
        wcsncpy_s(m_nid.szInfoTitle, title.c_str(), _TRUNCATE);
        wcsncpy_s(m_nid.szInfo, message.c_str(), _TRUNCATE);
        
        // 发送更新
        Shell_NotifyIconW(NIM_MODIFY, &m_nid);
        
        // 立即清除 NIF_INFO 标志，防止下次 NIM_MODIFY 时重复弹出
        m_nid.uFlags = originalFlags;
    }

    void TrayIcon::SetIcon(HICON hIcon) {
        if (!hIcon || !m_created || !m_hwnd)
            return;

        // ✅ 保留所有重要标志
        m_nid.uFlags |= NIF_ICON | NIF_SHOWTIP;  // 确保图标可见
        m_nid.hIcon = hIcon;
        Shell_NotifyIconW(NIM_MODIFY, &m_nid);
    }

    void TrayIcon::SetNotificationIcon(HICON hIcon) {
        if (!m_created || !m_hwnd)
            return;

        // 设置用于气泡通知的图标（通常为应用图标）
        // 注意：这是通知消息中显示的图标，不是托盘图标本身
        m_nid.hBalloonIcon = hIcon;
        m_nid.dwInfoFlags &= ~NIIF_ICON_MASK;  // 清除旧的图标类型
        m_nid.dwInfoFlags |= NIIF_USER;        // 使用自定义图标
        m_nid.uFlags |= NIF_INFO;              // 确保支持通知
        Shell_NotifyIconW(NIM_MODIFY, &m_nid);
    }

    // 按"当前 DPI 下的标准图标尺寸"加载，而不是小图标（SM_CXSMICON 太小）
    // - 使用 SM_CXICON (32x32 @100DPI) 而不是 SM_CXSMICON (16x16)
    // - 在高 DPI 屏幕上，系统会自动按比例放大（125%→40x40, 150%→48x48, 200%→64x64）
    // - 前提：icons/logo.ico 最好包含多个尺寸(32/40/48/64 等)的多分辨率 ico
    // - 即使单一尺寸，32x32 拉伸也比 16x16 清晰得多
    HICON TrayIcon::LoadTrayIconFromFile(const std::wstring& path) const {
        if (path.empty())
            return nullptr;

        // 检查文件是否存在
        WIN32_FIND_DATAW findData = {};
        HANDLE hFind = FindFirstFileW(path.c_str(), &findData);
        if (hFind == INVALID_HANDLE_VALUE)
        {
            // 文件不存在
            return nullptr;
        }
        FindClose(hFind);

        // ✅ 终极方案：使用 ExtractIconEx 获取大图标，系统会自动缩放到合适大小
        // 这是最可靠的方式，完全避免黑色背景问题
        HICON hLargeIcon = nullptr;
        HICON hSmallIcon = nullptr;
        
        UINT iconCount = ExtractIconExW(
            path.c_str(),        // ico 文件路径
            0,                   // 第一个图标
            &hLargeIcon,         // 获取大图标
            &hSmallIcon,         // 获取小图标
            1                    // 提取 1 个
        );

        // 优先使用小图标（如果成功提取）
        if (hSmallIcon != nullptr)
        {
            // 销毁大图标（不用）
            if (hLargeIcon != nullptr)
                DestroyIcon(hLargeIcon);
            return hSmallIcon;
        }

        // 备选：使用大图标（系统会自动缩放）
        if (hLargeIcon != nullptr)
        {
            return hLargeIcon;
        }

        // 最后备选：使用 LoadImage
        // 👉 这次不用任何特殊标志，让 Windows 自动处理透明度
        int cx = GetSystemMetrics(SM_CXSMICON);
        int cy = GetSystemMetrics(SM_CYSMICON);
        
        hLargeIcon = (HICON)LoadImage(
            nullptr,
            path.c_str(),
            IMAGE_ICON,
            cx, cy,
            LR_LOADFROMFILE  // ✅ 最简单的标志
        );

        return hLargeIcon;
    }

    void TrayIcon::ReloadIconForDpi() {
        if (m_iconPath.empty() || !m_created || !m_hwnd)
            return;

        HICON newIcon = LoadTrayIconFromFile(m_iconPath);
        if (!newIcon)
            return; // 加载失败就保留原图标，不动

        // 销毁旧图标（仅销毁由我们加载的图标）
        if (m_ownedIcon && m_ownedIcon != newIcon)
        {
            DestroyIcon(m_ownedIcon);
        }

        m_ownedIcon = newIcon;
        SetIcon(newIcon);
    }

    void TrayIcon::SetTooltip(const std::wstring& tooltip) {
        if (!m_created || !m_hwnd)
            return;
        
        wcsncpy_s(m_nid.szTip, tooltip.c_str(), _TRUNCATE);
        m_nid.uFlags |= NIF_TIP | NIF_SHOWTIP;  // ✅ 确保工具提示标志
        Shell_NotifyIconW(NIM_MODIFY, &m_nid);
    }

    void TrayIcon::StartBlink(HICON icon1, HICON icon2, int intervalMs) {
        if (!icon1 || !icon2 || !m_hwnd)
            return;

        m_blinkIcon1 = icon1;
        m_blinkIcon2 = icon2;
        m_blinking = true;
        m_blinkState = false;
        SetTimer(m_hwnd, 42, intervalMs, (TIMERPROC)BlinkTimerProc);
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
        if (!hMenu) return;

        // ✅ 添加菜单项
        AppendMenuW(hMenu, MF_STRING, ID_TRAY_SHOW, L"显示 YuMediaPlayer");
        AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(hMenu, MF_STRING, ID_TRAY_CHECK, L"检查更新");
        AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(hMenu, MF_STRING, ID_TRAY_EXIT, L"退出");

        POINT pt;
        GetCursorPos(&pt);
        
        // ✅ 设置前景窗口，确保菜单能响应焦点并自动关闭
        SetForegroundWindow(m_hwnd);
        
        // ✅ 显示菜单（位置：鼠标右下角对齐）
        TrackPopupMenu(hMenu, TPM_BOTTOMALIGN | TPM_LEFTALIGN | TPM_RIGHTBUTTON,
            pt.x, pt.y, 0, m_hwnd, nullptr);
        
        DestroyMenu(hMenu);
    }

    // ── 消息处理 ────────────────────────────────────────
    void TrayIcon::OnTrayMessage(WPARAM wParam, LPARAM lParam) {
        UINT msg = LOWORD(lParam);
        
        // ✅ 处理所有可能的托盘消息
        switch (msg) {
        case WM_LBUTTONDBLCLK:   // 双击 → 显示主窗口
            if (m_parentHwnd)
            {
                ::ShowWindow(m_parentHwnd, SW_RESTORE);
                ::SetForegroundWindow(m_parentHwnd);
            }
            break;
            
        case WM_LBUTTONUP:        // 左键单击 → 显示主窗口
            if (m_parentHwnd)
            {
                ::ShowWindow(m_parentHwnd, SW_RESTORE);
                ::SetForegroundWindow(m_parentHwnd);
            }
            break;
            
        case WM_RBUTTONUP:        // 右键 → 弹出菜单
            ShowContextMenu();
            break;
            
        case NIN_BALLOONUSERCLICK: // 点击气泡通知 → 显示主窗口
            if (m_parentHwnd)
            {
                ::ShowWindow(m_parentHwnd, SW_RESTORE);
                ::SetForegroundWindow(m_parentHwnd);
            }
            break;
            
        case NIN_BALLOONTIMEOUT:   // 气泡通知超时消失
            // 通知已经自动消失，无需处理
            break;
            
        case WM_MOUSEMOVE:         // 鼠标悬停 → 显示工具提示（系统自动）
            // 系统会自动显示 szTip 中的内容
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
