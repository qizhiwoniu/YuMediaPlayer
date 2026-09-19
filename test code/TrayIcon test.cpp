#include "TrayIcon.h" 
#include <windows.h> 
#include <shellapi.h> 
#include <wininet.h> 
#include <string> 
#pragma comment(lib, "Wininet.lib") 
#pragma comment(lib, "Shell32.lib") // 这个类目前通过全局实例接收托盘窗口消息。 
// 生命周期必须严格管理，否则 Timer / WndProc 很容易访问已经释放的对象。 
static TrayIcon* g_trayInstance = nullptr;
//============================================================ // 构造 // ============================================================ 
TrayIcon::TrayIcon( HINSTANCE hInstance, const std::wstring& tooltip) 
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
	{ g_trayInstance = this; ZeroMemory(&m_nid, sizeof(m_nid)); } 
// ============================================================ // 析构 // ============================================================ 
TrayIcon::~TrayIcon() 
{ // 非常重要： // 先让全局指针失效，避免销毁过程中 Timer / 消息再次访问 this。 
	if (g_trayInstance == this) g_trayInstance = nullptr; // 先停止 Timer StopBlink(); 
	// 删除托盘图标 Remove(); 
	// 销毁自己拥有的图标 
	if (m_ownedIcon != nullptr) { DestroyIcon(m_ownedIcon); m_ownedIcon = nullptr; } // 最后销毁隐藏窗口 
	if (m_hwnd != nullptr) { HWND hwnd = m_hwnd; m_hwnd = nullptr; DestroyWindow(hwnd); } 
	m_parentHwnd = nullptr; 
} 
// ============================================================ // 创建托盘图标 // ==================================================== 
bool TrayIcon::Create(HWND parentHwnd) {
	m_parentHwnd = parentHwnd; // 防止重复 Create if (m_hwnd != nullptr) return m_created;
	// -------------------------------------------------------- // 注册窗口类 // -------------------------------------------------------- 
	WNDCLASSEXW wc = {}; 
		wc.cbSize = sizeof(WNDCLASSEXW); 
		wc.lpfnWndProc = &TrayIcon::WndProc; 
		wc.hInstance = m_hInstance; 
		wc.lpszClassName = L"YuMediaPlayer_TrayClass"; 
	ATOM atom = RegisterClassExW(&wc); 
		if (atom == 0) 
		{ DWORD error = GetLastError(); // 已经注册过属于正常情况 
		if (error != ERROR_CLASS_ALREADY_EXISTS) { return false; } }
	// -------------------------------------------------------- // 创建消息窗口 // -------------------------------------------------------- 
	m_hwnd = CreateWindowExW( 0, L"YuMediaPlayer_TrayClass", L"YuMediaPlayer Tray", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, m_hInstance, nullptr); 
	if (m_hwnd == nullptr) { return false; }
	// -------------------------------------------------------- // 设置托盘数据 // -------------------------------------------------------- 
	 ZeroMemory(&m_nid, sizeof(m_nid)); 
	 m_nid.cbSize = sizeof(NOTIFYICONDATAW); 
	 m_nid.hWnd = m_hwnd; 
     m_nid.uID = ID_TRAY_ICON; 
	 m_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
	 m_nid.uCallbackMessage = WM_TRAYICON;
	 // -------------------------------------------------------- // 记录图标路径 // -------------------------------------------------------- 
	 wchar_t exePath[MAX_PATH] = {}; 
	 DWORD pathLength = GetModuleFileNameW( nullptr, exePath, MAX_PATH); if (pathLength == 0) 
	 { m_iconPath = L"icons\\logo.ico"; } 
	 else 
	 { std::wstring fullPath(exePath); 
	 std::wstring::size_type slash = fullPath.find_last_of(L"\\/"); 
	 if (slash != std::wstring::npos) 
	 { fullPath.resize(slash + 1); }
	 else { fullPath.clear(); } 
	 m_iconPath = fullPath + L"icons\\logo.ico"; }
	 // -------------------------------------------------------- // 加载图标 // -------------------------------------------------------- 
	 m_ownedIcon = LoadTrayIconFromFile(m_iconPath); 
	 if (m_ownedIcon != nullptr) { m_nid.hIcon = m_ownedIcon; } 
	 else { 
		 // 加载失败时使用系统默认图标 
		 m_nid.hIcon = LoadIconW( nullptr, IDI_APPLICATION); 
	 if (m_nid.hIcon == nullptr) 
		{ DestroyWindow(m_hwnd); 
		m_hwnd = nullptr; return false;
		}
	 }
	 // -------------------------------------------------------- // 设置提示 // -------------------------------------------------------- 
	 wcsncpy_s( m_nid.szTip, _countof(m_nid.szTip), m_tooltip.c_str(), _TRUNCATE);
	 // -------------------------------------------------------- // 添加托盘 // -------------------------------------------------------- 
	 BOOL result = Shell_NotifyIconW( NIM_ADD, &m_nid); 
	 if (!result) { if (m_ownedIcon != nullptr) 
	 { DestroyIcon(m_ownedIcon); m_ownedIcon = nullptr; } 
	 DestroyWindow(m_hwnd); m_hwnd = nullptr; return false; }
	 m_created = true; // Windows Vista+ 托盘版本 
	 m_nid.uVersion = NOTIFYICON_VERSION_4; 
	 Shell_NotifyIconW( NIM_SETVERSION, &m_nid); return true; }
	// ============================================================ // 删除托盘 // ============================================================ 
	void TrayIcon::Remove() {
		if (!m_created) return; 
		if (m_nid.hWnd == nullptr) 
		{ m_created = false; return; } 
	Shell_NotifyIconW( NIM_DELETE, &m_nid); m_created = false; }
	// ============================================================ // 气泡通知 // ============================================================ 
	void TrayIcon::ShowBalloon( const std::wstring& title, const std::wstring& message, DWORD timeoutMs, DWORD infoFlags) 
	{ 
		if (!m_created) 
			return; 
		if (m_hwnd == nullptr) 
			return; 
		m_nid.uFlags |= NIF_INFO; 
		m_nid.uTimeout = timeoutMs; 
		m_nid.dwInfoFlags = infoFlags; 
	wcsncpy_s( m_nid.szInfoTitle, _countof(m_nid.szInfoTitle), title.c_str(), _TRUNCATE); 
	wcsncpy_s( m_nid.szInfo, _countof(m_nid.szInfo), message.c_str(), _TRUNCATE); 
	Shell_NotifyIconW( NIM_MODIFY, &m_nid); // 防止后续修改托盘图标时再次触发通知 
	m_nid.uFlags &= ~NIF_INFO; }
	// ============================================================ // 设置图标 // ============================================================ 
	void TrayIcon::SetIcon(HICON hIcon) { 
		if (hIcon == nullptr) 
			return; 
		if (!m_created) 
			return; 
		if (m_hwnd == nullptr) 
			return; 
		m_nid.hIcon = hIcon; 
		Shell_NotifyIconW( NIM_MODIFY, &m_nid); }
	// ============================================================ // 加载图标 // ============================================================ 
	HICON TrayIcon::LoadTrayIconFromFile( const std::wstring& path) const { if (path.empty()) return nullptr; int cx = GetSystemMetrics(SM_CXSMICON); int cy = GetSystemMetrics(SM_CYSMICON); if (cx <= 0) cx = 16; if (cy <= 0) cy = 16; return reinterpret_cast<HICON>( LoadImageW( nullptr, path.c_str(), IMAGE_ICON, cx, cy, LR_LOADFROMFILE | LR_DEFAULTCOLOR)); }

	// ============================================================ // DPI 改变后重新加载图标 // ============================================================ 
	void TrayIcon::ReloadIconForDpi() { if (m_iconPath.empty()) return; HICON newIcon = LoadTrayIconFromFile(m_iconPath); if (newIcon == nullptr) return; // 保存旧图标 HICON oldIcon = m_ownedIcon; // 先切换到新图标 m_ownedIcon = newIcon; SetIcon(newIcon); // 最后销毁旧图标 if (oldIcon != nullptr && oldIcon != newIcon) { DestroyIcon(oldIcon); } }

	// ============================================================ // 设置 Tooltip // ============================================================ 
	void TrayIcon::SetTooltip( const std::wstring& tooltip) 
	{ 
		m_tooltip = tooltip; 
	if (!m_created) 
		return; 
	if (m_hwnd == nullptr) 
		return; 
	wcsncpy_s( m_nid.szTip, 
		_countof(m_nid.szTip), 
		tooltip.c_str(), 
		_TRUNCATE); 
	Shell_NotifyIconW( NIM_MODIFY, &m_nid); 
	}

	// ============================================================ // 开始闪烁 // ============================================================ 
	void TrayIcon::StartBlink( HICON icon1, HICON icon2, int intervalMs) { if (m_hwnd == nullptr) return; if (!m_created) return; if (icon1 == nullptr || icon2 == nullptr) { return; } if (intervalMs < 100) intervalMs = 100; // 如果已经在闪烁，先停止 StopBlink(); m_blinkIcon1 = icon1; m_blinkIcon2 = icon2; m_blinkState = false; m_blinking = true; // 立即显示第一张 SetIcon(m_blinkIcon1); UINT_PTR timerResult = SetTimer( m_hwnd, 42, static_cast<UINT>(intervalMs), &TrayIcon::BlinkTimerProc); // SetTimer 失败 if (timerResult == 0) { m_blinking = false; m_blinkIcon1 = nullptr; m_blinkIcon2 = nullptr; m_blinkState = false; } }
	// ============================================================ // 停止闪烁 // ============================================================ 
	void TrayIcon::StopBlink() { // 先停止 Timer if (m_hwnd != nullptr) { KillTimer( m_hwnd, 42); } // 如果当前正在闪烁，恢复第一张图标 if (m_blinking) { m_blinking = false; if (m_blinkIcon1 != nullptr && m_created && m_hwnd != nullptr) { SetIcon(m_blinkIcon1); } } // 非常重要： // 不再保留外部 HICON 指针。 m_blinkIcon1 = nullptr; m_blinkIcon2 = nullptr; m_blinkState = false; }
	// ============================================================ // Timer 回调 // ============================================================ 
	void CALLBACK TrayIcon::BlinkTimerProc( HWND hwnd, UINT, UINT_PTR id, DWORD) { // 只处理自己的 Timer if (id != 42) return; TrayIcon* tray = g_trayInstance; // 对象已经不存在 if (tray == nullptr) return; // 窗口已经不存在 if (tray->m_hwnd == nullptr) return; // Timer 窗口不是当前对象的窗口 if (hwnd != tray->m_hwnd) return; if (!tray->m_blinking) return; if (!tray->m_created) return; if (tray->m_blinkIcon1 == nullptr || tray->m_blinkIcon2 == nullptr) { tray->m_blinking = false; KillTimer(hwnd, id); return; } tray->m_blinkState = !tray->m_blinkState; HICON icon = tray->m_blinkState ? tray->m_blinkIcon2 : tray->m_blinkIcon1; if (icon != nullptr) { tray->SetIcon(icon); } }
	// ============================================================ // 右键菜单 // ============================================================ 
	void TrayIcon::ShowContextMenu() { if (m_hwnd == nullptr) return; HMENU hMenu = CreatePopupMenu(); if (hMenu == nullptr) return; AppendMenuW( hMenu, MF_STRING, ID_TRAY_SHOW, L"显示 YuMediaPlayer"); AppendMenuW( hMenu, MF_SEPARATOR, 0, nullptr); AppendMenuW( hMenu, MF_STRING, ID_TRAY_CHECK, L"检查更新"); AppendMenuW( hMenu, MF_SEPARATOR, 0, nullptr); AppendMenuW( hMenu, MF_STRING, ID_TRAY_EXIT, L"退出"); POINT pt = {}; if (GetCursorPos(&pt)) { SetForegroundWindow(m_hwnd); TrackPopupMenu( hMenu, TPM_BOTTOMALIGN | TPM_LEFTALIGN, pt.x, pt.y, 0, m_hwnd, nullptr); // 防止菜单关闭异常 PostMessageW( m_hwnd, WM_NULL, 0, 0); } DestroyMenu(hMenu); }
	// ============================================================ // 托盘消息 // ============================================================ 
	void TrayIcon::OnTrayMessage( WPARAM, LPARAM lParam) { UINT msg = LOWORD(lParam); switch (msg) { case WM_LBUTTONDBLCLK: { if (m_parentHwnd != nullptr) { ::ShowWindow( m_parentHwnd, SW_RESTORE); ::SetForegroundWindow( m_parentHwnd); } break; } case WM_RBUTTONUP: { ShowContextMenu(); break; } case NIN_BALLOONUSERCLICK: { if (m_parentHwnd != nullptr) { ::ShowWindow( m_parentHwnd, SW_RESTORE); ::SetForegroundWindow( m_parentHwnd); } break; } default: break; } }
	// ============================================================ // 隐藏窗口消息处理 // ============================================================ 
	LRESULT CALLBACK TrayIcon::WndProc( HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) 
	{ TrayIcon* tray = g_trayInstance; // 对象已经不存在 
	if (tray == nullptr) 
	{ return DefWindowProcW( hwnd, msg, wParam, lParam); }
	 switch (msg) 
	 { 
	 case WM_TRAYICON: { tray->OnTrayMessage( wParam, lParam); return 0; } 
	 case WM_COMMAND: 
	 { 
	 switch (LOWORD(wParam)) 
	 {
	 case ID_TRAY_EXIT: 
	 { PostQuitMessage(0); return 0; } 
	 case ID_TRAY_SHOW: 
	 {
	if (tray->m_parentHwnd != nullptr) { ShowWindow( tray->m_parentHwnd, SW_RESTORE); SetForegroundWindow( tray->m_parentHwnd); } return 0; } case ID_TRAY_CHECK: { tray->CheckUpdate(); return 0; } default: break; } break; } default: break; } return DefWindowProcW( hwnd, msg, wParam, lParam); }
	// ============================================================ // HTTP GET // ============================================================ 
	std::wstring TrayIcon::HttpGet( const std::wstring& url) { if (url.empty()) return L""; HINTERNET hInternet = InternetOpenW( L"YuMediaPlayer-Updater", INTERNET_OPEN_TYPE_PRECONFIG, nullptr, nullptr, 0); if (hInternet == nullptr) return L""; HINTERNET hConnect = InternetOpenUrlW( hInternet, url.c_str(), nullptr, 0, INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_SECURE, 0); if (hConnect == nullptr) { InternetCloseHandle( hInternet); return L""; } std::string result; char buffer[4096] = {}; DWORD bytesRead = 0; while (true) { bytesRead = 0; BOOL ok = InternetReadFile( hConnect, buffer, sizeof(buffer), &bytesRead); if (!ok || bytesRead == 0) break; result.append( buffer, bytesRead); } InternetCloseHandle( hConnect); InternetCloseHandle( hInternet); if (result.empty()) return L""; // UTF-8 → UTF-16 
	int length = MultiByteToWideChar(
		CP_UTF8, 0, result.data(), static_cast<int>(result.size()), nullptr, 0); 
	if (length <= 0) return L""; std::wstring response( static_cast<size_t>(length), L'\0'); 
	if (MultiByteToWideChar( CP_UTF8, 0, result.data(), static_cast<int>(result.size()), &response[0], length) <= 0) 
	{ return L""; } return response; }
	// ============================================================ // 解析 GitHub tag_name // ============================================================ 
	std::wstring TrayIcon::ParseLatestVersion( const std::string& json) { if (json.empty()) return L""; const std::string key = "\"tag_name\""; size_t pos = json.find(key); if (pos == std::string::npos) return L""; pos += key.size(); pos = json.find( ':', pos); if (pos == std::string::npos) return L""; ++pos; // 跳过空格 while ( pos < json.size() && ( json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\r' || json[pos] == '\n' )) { ++pos; } if (pos >= json.size()) return L""; if (json[pos] != '"') return L""; ++pos; if (pos >= json.size()) return L""; size_t end = json.find( '"', pos); if (end == std::string::npos) return L""; std::string version = json.substr( pos, end - pos); if (version.empty()) return L""; // 去掉 v 前缀 if (version[0] == 'v' || version[0] == 'V') { version.erase( 0, 1); } if (version.empty()) return L""; // UTF-8 → UTF-16 int length = MultiByteToWideChar( CP_UTF8, 0, version.data(), static_cast<int>(version.size()), nullptr, 0); if (length <= 0) return L""; std::wstring result( static_cast<size_t>(length), L'\0'); if (MultiByteToWideChar( CP_UTF8, 0, version.data(), static_cast<int>(version.size()), &result[0], length) <= 0) { return L""; } return result; }
	// ============================================================ // 检查更新 // ============================================================ 
	void TrayIcon::CheckUpdate() { const std::wstring apiUrl = L"https://api.github.com/repos/" L"qizhiwoniu/YuMediaPlayer/" L"releases/latest"; ShowBalloon( L"检查更新", L"正在连接 GitHub...", 2000, NIIF_INFO); std::wstring response = HttpGet(apiUrl); if (response.empty()) { ShowBalloon( L"检查更新失败", L"无法连接到 GitHub，请检查网络。", 3000, NIIF_ERROR); return; } // -------------------------------------------------------- // UTF-16 → UTF-8 // -------------------------------------------------------- int jsonLength = WideCharToMultiByte( CP_UTF8, 0, response.c_str(), static_cast<int>(response.size()), nullptr, 0, nullptr, nullptr); if (jsonLength <= 0) { ShowBalloon( L"检查更新失败", L"版本信息编码转换失败。", 3000, NIIF_ERROR); return; } std::string json( static_cast<size_t>(jsonLength), '\0'); if (WideCharToMultiByte( CP_UTF8, 0, response.c_str(), static_cast<int>(response.size()), &json[0], jsonLength, nullptr, nullptr) <= 0) { ShowBalloon( L"检查更新失败", L"版本信息编码转换失败。", 3000, NIIF_ERROR); return; } std::wstring latestVer = ParseLatestVersion(json); if (latestVer.empty()) { ShowBalloon( L"检查更新失败", L"无法解析 GitHub 版本信息。", 3000, NIIF_ERROR); return; } // ======================================================== // 注意： // version.h 中的 APP_VERSION 应该定义为： // // #define APP_VERSION L"1.0.0" // // 如果你的 version.h 是： // // #define APP_VERSION "1.0.0" // // 请告诉我，我再按照你的 version.h 修改。 // ======================================================== std::wstring currentVer = APP_VERSION; if (currentVer.empty()) { ShowBalloon( L"检查更新失败", L"当前程序版本号为空。", 3000, NIIF_ERROR); return; } if (latestVer == currentVer) { std::wstring message = L"当前版本 v" + currentVer + L" 已是最新。"; ShowBalloon( L"已是最新版本", message, 3000, NIIF_INFO); return; } std::wstring message = L"发现新版本 v" + latestVer + L"\n当前版本 v" + currentVer + L"\n\n是否前往 GitHub 下载？"; int ret = MessageBoxW( m_parentHwnd, message.c_str(), L"YuMediaPlayer 有新版本", MB_YESNO | MB_ICONINFORMATION); if (ret == IDYES) { const std::wstring releaseUrl = L"https://github.com/" L"qizhiwoniu/YuMediaPlayer/" L"releases/latest"; ShellExecuteW( nullptr, L"open", releaseUrl.c_str(), nullptr, nullptr, SW_SHOWNORMAL); } }

