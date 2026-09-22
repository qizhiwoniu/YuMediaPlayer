#include <windows.h>
#include <windowsx.h>
#include <memory>
#include <dwmapi.h>
#include "Core/Theme.h"
#include "WindowGUI.h"

#pragma comment(lib, "dwmapi.lib")
namespace YuMediaPlayer
{
	static WindowGUI* g_windowGUIInstance = nullptr;
	enum class ButtonType
	{
		Close = 0,      // 关闭
		Maximize = 1,   // 最大化
		Minimize = 2,   // 最小化
		Settings = 3,   // 设置
		Updata = 4,     // 刷新/更新
		Count = 5
	};
	WindowGUI::WindowGUI()
		: m_hwnd(nullptr)
		, m_hoverButton(-1)
		, m_isMaximized(false)
		, m_cornerRadius(15)  // 圆角半径，默认 15px
	{
		// 保存全局实例指针，用于 WndProc 回调
		g_windowGUIInstance = this;

		for (int i = 0; i < 5; i++)
			m_buttonRects[i] = { 0, 0, 0, 0 };
	}

	WindowGUI::~WindowGUI()
	{
		if (g_windowGUIInstance == this)
			g_windowGUIInstance = nullptr;

		if (m_hwnd)
		{
			DestroyWindow(m_hwnd);
			m_hwnd = nullptr;
		}
	}

	void WindowGUI::UpdateWindowRegion()
	{
		if (!m_hwnd)
			return;
		// DWMWA_CORNER_PREFERENCE: 0=默认, 1=无, 2=小圆角, 3=中圆角, 4=大圆角

		// 首先尝试使用 Windows 11 的新 API
		typedef enum {
			DWMWCP_DEFAULT = 0,
			DWMWCP_DONOTROUND = 1,
			DWMWCP_ROUND = 2,
			DWMWCP_ROUNDSMALL = 3
		} DWM_WINDOW_CORNER_PREFERENCE;

		// 获取系统版本，确定使用哪个 API
		OSVERSIONINFOEX osvi = {};
		osvi.dwOSVersionInfoSize = sizeof(OSVERSIONINFOEX);
		GetVersionExW((LPOSVERSIONINFOW)&osvi);

		if (osvi.dwMajorVersion >= 10)  // Windows 10+
		{
			// 对于 Windows 10/11，直接设置圆角偏好
			DWM_WINDOW_CORNER_PREFERENCE preference = DWMWCP_ROUND;

			HRESULT hr = DwmSetWindowAttribute(
				m_hwnd,
				static_cast<DWORD>(9),  // DWMWA_CORNER_PREFERENCE = 9
				&preference,
				sizeof(preference)
			);

			// 如果失败，使用备用的传统方法
			if (FAILED(hr))
			{
				ApplyLegacyCornerRadius();
			}
		}
		else
		{
			// 对于 Windows 7/8，使用传统的 Region 方法
			ApplyLegacyCornerRadius();
		}
	}
	void WindowGUI::ApplyLegacyCornerRadius()
	{
		if (!m_hwnd)
			return;

		RECT rect;
		GetClientRect(m_hwnd, &rect);

		// 创建圆角矩形 region
		HRGN hRgn = CreateRoundRectRgn(
			0, 0,
			rect.right - rect.left + 1,
			rect.bottom - rect.top + 1,
			m_cornerRadius * 2,  // 水平圆角直径
			m_cornerRadius * 2   // 竖直圆角直径
		);

		if (hRgn)
		{
			// 设置窗口 region
			SetWindowRgn(m_hwnd, hRgn, TRUE);
		}
	}
	void WindowGUI::SetCornerRadius(int radius)
	{
		if (radius < 0)
			radius = 0;
		if (radius > 50)
			radius = 50;  // 限制最大半径

		m_cornerRadius = radius;
		UpdateWindowRegion();
	}
	int WindowGUI::GetCornerRadius() const
	{
		return m_cornerRadius;
	}

	LRESULT CALLBACK WindowGUI::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
		// 在 WM_NCCREATE 时获取 this 指针
		WindowGUI* pThis = nullptr;

		if (msg == WM_NCCREATE)
		{
			// 创建阶段，从创建参数中获取 this 指针
			auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
			pThis = reinterpret_cast<WindowGUI*>(cs->lpCreateParams);
			SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pThis));
		}
		else
		{
			// 其他消息，从窗口用户数据中获取 this 指针
			pThis = reinterpret_cast<WindowGUI*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
		}

		// 如果有 this 指针，调用成员函数处理消息
		if (pThis)
			return pThis->EventProc(hwnd, msg, wParam, lParam);

		// 否则使用默认处理
		return DefWindowProcW(hwnd, msg, wParam, lParam);
	}
	void WindowGUI::DrawButton(HDC hdc, const RECT& rect, bool hovered, int btnType)
	{
		// 按钮背景色
		COLORREF bgColor = hovered ? RGB(70, 70, 70) : RGB(40, 40, 40);
		COLORREF borderColor = hovered ? RGB(100, 100, 100) : RGB(60, 60, 60);
		COLORREF iconColor = hovered ? RGB(255, 255, 255) : RGB(200, 200, 200);

		// 绘制背景
		HBRUSH hBgBrush = CreateSolidBrush(bgColor);
		FillRect(hdc, &rect, hBgBrush);
		DeleteObject(hBgBrush);

		// 绘制边框
		HPEN hPen = CreatePen(PS_SOLID, 1, borderColor);
		HPEN hOldPen = (HPEN)SelectObject(hdc, hPen);
		MoveToEx(hdc, rect.left, rect.top, nullptr);
		LineTo(hdc, rect.right, rect.top);
		LineTo(hdc, rect.right, rect.bottom);
		LineTo(hdc, rect.left, rect.bottom);
		LineTo(hdc, rect.left, rect.top);
		SelectObject(hdc, hOldPen);
		DeleteObject(hPen);

		// 绘制图标
		int centerX = (rect.left + rect.right) / 2;
		int centerY = (rect.top + rect.bottom) / 2;

		HPEN hIconPen = CreatePen(PS_SOLID, 2, iconColor);
		HPEN hOldIconPen = (HPEN)SelectObject(hdc, hIconPen);

		switch (btnType)
		{
		case 0:  // 关闭按钮 - X 符号
		{
			int offset = 5;
			MoveToEx(hdc, centerX - offset, centerY - offset, nullptr);
			LineTo(hdc, centerX + offset, centerY + offset);
			MoveToEx(hdc, centerX + offset, centerY - offset, nullptr);
			LineTo(hdc, centerX - offset, centerY + offset);
			break;
		}
		case 1:  // 最大化按钮 - 方框符号
		{
			int offset = 5;
			MoveToEx(hdc, centerX - offset, centerY - offset, nullptr);
			LineTo(hdc, centerX + offset, centerY - offset);
			LineTo(hdc, centerX + offset, centerY + offset);
			LineTo(hdc, centerX - offset, centerY + offset);
			LineTo(hdc, centerX - offset, centerY - offset);
			break;
		}
		case 2:  // 最小化按钮 - 横线符号
		{
			int offset = 5;
			MoveToEx(hdc, centerX - offset, centerY + 2, nullptr);
			LineTo(hdc, centerX + offset, centerY + 2);
			break;
		}
		case 3:  // 设置按钮 - 三个小圆点
		{
			int radius = 2;
			for (int i = -1; i <= 1; ++i)
			{
				Ellipse(hdc, centerX + i * 6 - radius, centerY - radius, centerX + i * 6 + radius, centerY + radius);
			}
			break;
		}
		case 4:  // 更新按钮 - 刷新符号
		{
			int radius = 6;
			Arc(hdc, centerX - radius, centerY - radius, centerX + radius, centerY + radius, centerX + radius, centerY, centerX, centerY - radius);
			LineTo(hdc, centerX + 2, centerY - 2);
			MoveToEx(hdc, centerX + 2, centerY - 2, nullptr);
			LineTo(hdc, centerX + 4, centerY - 4);
			break;
		}
		}
		SelectObject(hdc, hOldIconPen);
		DeleteObject(hIconPen);
	}
	void WindowGUI::UpdateButtonRects(int width)
	{
		const int titleBarHeight = 30;
		const int buttonWidth = 44;
		const int buttonHeight = 30;
		const int margin = 0;

		// 从右到左排列：关闭、最大化、最小化
		int rightX = width - margin;

		m_buttonRects[static_cast<int>(ButtonType::Close)] = {
			rightX - buttonWidth, 0, rightX, buttonHeight
		};
		rightX -= buttonWidth;

		m_buttonRects[static_cast<int>(ButtonType::Maximize)] = {
			rightX - buttonWidth, 0, rightX, buttonHeight
		};
		rightX -= buttonWidth;

		m_buttonRects[static_cast<int>(ButtonType::Minimize)] = {
			rightX - buttonWidth, 0, rightX, buttonHeight
		};
		rightX -= buttonWidth;
		m_buttonRects[static_cast<int>(ButtonType::Settings)] = {
			rightX - buttonWidth, 0, rightX, buttonHeight
		};
		rightX -= buttonWidth;
		m_buttonRects[static_cast<int>(ButtonType::Updata)] = {
			rightX - buttonWidth, 0, rightX, buttonHeight
		};
	}

	LRESULT WindowGUI::EventProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
	{
		switch (msg)
		{
		case WM_CREATE:
			// 窗口创建完成
			return 0;
		case WM_DESTROY:
			// 窗口销毁前的清理
			PostQuitMessage(0);
			return 0;
		case WM_CLOSE:
			// 用户点击关闭按钮
			// 这里可以选择隐藏而不是销毁
			HideWindowGUI();
			return 0;
		case WM_PAINT:
		{
			// 绘制窗口
			PAINTSTRUCT ps;
			HDC hdc = BeginPaint(hwnd, &ps);

			RECT rc;
			GetClientRect(hwnd, &rc);
			int width = rc.right - rc.left;
			int height = rc.bottom - rc.top;

			// 更新按钮位置
			UpdateButtonRects(width);

			// 绘制背景
			HBRUSH hBgBrush = CreateSolidBrush(RGB(24, 24, 24));
			FillRect(hdc, &rc, hBgBrush);
			DeleteObject(hBgBrush);

			// 绘制标题栏背景（深灰色）
			/*RECT titleBarRect = { 0, 0, width, 30 };
			HBRUSH hTitleBrush = CreateSolidBrush(RGB(32, 32, 32));
			FillRect(hdc, &titleBarRect, hTitleBrush);
			DeleteObject(hTitleBrush);*/

			//// 绘制标题栏下方的分割线
			//HPEN hLinePen = CreatePen(PS_SOLID, 1, RGB(50, 50, 50));
			//HPEN hOldPen = (HPEN)SelectObject(hdc, hLinePen);
			//MoveToEx(hdc, 0, 36, nullptr);
			//LineTo(hdc, width, 36);
			//SelectObject(hdc, hOldPen);
			//DeleteObject(hLinePen);

			//// 绘制标题文本
			//SetBkMode(hdc, TRANSPARENT);
			//SetTextColor(hdc, RGB(200, 200, 200));

			HFONT hFont = CreateFontW(14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
				DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
				DEFAULT_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei");
			HFONT hOldFont = (HFONT)SelectObject(hdc, hFont);

			/*RECT textRect = { 10, 0, width - 150, 36 };
			DrawTextW(hdc, L"YuMediaPlayer", -1, &textRect,
				DT_LEFT | DT_VCENTER | DT_SINGLELINE);*/

			SelectObject(hdc, hOldFont);
			DeleteObject(hFont);

			// 绘制按钮
			for (int i = 0; i < 5; i++)
			{
				bool hovered = (m_hoverButton == i);
				DrawButton(hdc, m_buttonRects[i], hovered, i);
			}

			EndPaint(hwnd, &ps);
			return 0;
		}
		case WM_MOUSEMOVE:
		{
			POINT pt;
			pt.x = GET_X_LPARAM(lParam);
			pt.y = GET_Y_LPARAM(lParam);

			int newHoverButton = -1;
			for (int i = 0; i < 5; i++)
			{
				if (PtInRect(&m_buttonRects[i], pt))
				{
					newHoverButton = i;
					break;
				}
			}

			// 如果悬停状态改变，重绘
			if (newHoverButton != m_hoverButton)
			{
				m_hoverButton = newHoverButton;
				InvalidateRect(hwnd, nullptr, FALSE);

				// 启用鼠标离开事件
				if (newHoverButton >= 0)
				{
					TRACKMOUSEEVENT tme = {};
					tme.cbSize = sizeof(TRACKMOUSEEVENT);
					tme.hwndTrack = hwnd;
					tme.dwFlags = TME_LEAVE;
					TrackMouseEvent(&tme);
				}
			}

			return 0;
		}
		case WM_MOUSELEAVE:
		{
			if (m_hoverButton >= 0)
			{
				m_hoverButton = -1;
				InvalidateRect(hwnd, nullptr, FALSE);
			}
			return 0;
		}
		case WM_LBUTTONUP:
		{
			POINT pt;
			pt.x = GET_X_LPARAM(lParam);
			pt.y = GET_Y_LPARAM(lParam);

			// 检查按钮点击
			if (PtInRect(&m_buttonRects[static_cast<int>(ButtonType::Close)], pt))
			{
				// 关闭按钮
				HideWindowGUI();
				return 0;
			}
			else if (PtInRect(&m_buttonRects[static_cast<int>(ButtonType::Maximize)], pt))
			{
				// 最大化按钮
				if (m_isMaximized)
				{
					// 恢复窗口
					ShowWindow(hwnd, SW_RESTORE);
					m_isMaximized = false;
				}
				else
				{
					// 最大化窗口
					ShowWindow(hwnd, SW_MAXIMIZE);
					m_isMaximized = true;
				}
				return 0;
			}
			else if (PtInRect(&m_buttonRects[static_cast<int>(ButtonType::Minimize)], pt))
			{
				// 最小化按钮
				ShowWindow(hwnd, SW_MINIMIZE);
				return 0;
			}

			// 标题栏拖动（如果点击不在按钮上）
			if (pt.y < 30)
			{
				ReleaseCapture();
				SendMessageW(hwnd, WM_NCLBUTTONDOWN, HTCAPTION, lParam);
				return 0;
			}

			return 0;
		}
		case WM_NCHITTEST:
		{
			const LONG border = 6;

			POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
			RECT wr;
			GetWindowRect(hwnd, &wr);
			// 检查是否在按钮上
			POINT clientPt = { pt.x - wr.left, pt.y - wr.top };
			for (int i = 0; i <	5; i++)
			{
				if (PtInRect(&m_buttonRects[i], clientPt))
					return HTCLIENT;  // 让客户区处理
			}
			bool left = pt.x < wr.left + border;
			bool right = pt.x >= wr.right - border;
			bool top = pt.y < wr.top + border;
			bool bottom = pt.y >= wr.bottom - border;
			/*bool middle = !left && !right && !top && !bottom;
			bool middleTop = !left && !right && top;
			bool middleBottom = !left && !right && bottom;
			bool center = pt.x >= wr.left + (wr.right - wr.left) / 2 && pt.y >= wr.top + (wr.bottom - wr.top) / 2;*/

			// 角落
			if (top && left)     return HTTOPLEFT;
			if (top && right)    return HTTOPRIGHT;
			if (bottom && left)  return HTBOTTOMLEFT;
			if (bottom && right) return HTBOTTOMRIGHT;

			// 边缘
			if (left)   return HTLEFT;
			if (right)  return HTRIGHT;
			if (top)    return HTTOP;
			if (bottom) return HTBOTTOM;

			// 标题栏（用于拖动） - 卡片顶部区域
			// 标题栏（用于拖动）
			if (clientPt.y >= 0 && clientPt.y < 30)
			{
				return HTCAPTION;
			}
			/*if (pt.y >= wr.top && pt.y < wr.top + 30)
			{
				return HTCAPTION;
			}*/
			return HTCLIENT;
		}
		default:
			return DefWindowProcW(hwnd, msg, wParam, lParam);
		}
	}
	bool WindowGUI::InitWindow(const wchar_t* title, int width, int height)
	{
		// 步骤 1：注册窗口类
		static const wchar_t CLASS_NAME[] = L"YuMediaPlayerWindowGUIClass";
		static bool classRegistered = false;

		if (!classRegistered)
		{
			WNDCLASSEXW wc = {};
			wc.cbSize = sizeof(WNDCLASSEXW);
			wc.style = CS_HREDRAW | CS_VREDRAW;
			wc.lpfnWndProc = WndProc;        // ★ 使用静态回调函数
			wc.cbClsExtra = 0;
			wc.cbWndExtra = 0;
			wc.hInstance = GetModuleHandleW(nullptr);
			wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
			wc.hbrBackground = nullptr;      // 不要背景刷，由我们自己绘制
			wc.lpszClassName = CLASS_NAME;
			wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);

			if (!RegisterClassExW(&wc))
			{
				return false;
			}

			classRegistered = true;
		}

		// 步骤 2：创建分层窗口（Layered Window）
		// WS_EX_LAYERED - 分层窗口，支持透明和半透明
		// WS_EX_TOPMOST - 总在最前
		// WS_EX_TOOLWINDOW - 隐藏任务栏（初始状态）
		m_hwnd = CreateWindowExW(
			WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
			CLASS_NAME,
			title,
			WS_POPUP,                         // 弹出式窗口，无标题栏
			100, 100,                         // 初始位置
			width = 1200, height = 720,           // 宽高
			nullptr,                          // 父窗口
			nullptr,                          // 菜单
			GetModuleHandleW(nullptr),
			this                              // 传入 this 指针给 WM_CREATE
		);

		if (!m_hwnd)
			return false;

		// 步骤 3：居中到屏幕中央
		{
			HMONITOR hMon = MonitorFromWindow(m_hwnd, MONITOR_DEFAULTTONEAREST);
			MONITORINFO mi = { sizeof(mi) };
			GetMonitorInfoW(hMon, &mi);

			int monW = mi.rcMonitor.right - mi.rcMonitor.left;
			int monH = mi.rcMonitor.bottom - mi.rcMonitor.top;

			int posX = mi.rcMonitor.left + (monW - width) / 2;
			int posY = mi.rcMonitor.top + (monH - height) / 2;

			SetWindowPos(m_hwnd, nullptr, posX, posY, width, height,
				SWP_NOZORDER | SWP_NOACTIVATE);
		}

		// 步骤 4：显示窗口
		ShowWindow(m_hwnd, SW_SHOW);
		UpdateWindow(m_hwnd);

		return true;
	}
	void WindowGUI::Run()
	{
		MSG msg = {};
		bool needsRedraw = true;

		while (msg.message != WM_QUIT)
		{
			if (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
			{
				TranslateMessage(&msg);
				DispatchMessageW(&msg);
				needsRedraw = true;
			}
			else
			{
				if (needsRedraw)
				{
					// 这里可以放置绘制逻辑
					// RenderFrame();
					needsRedraw = false;
				}
				else
				{
					WaitMessage();
				}
			}
		}
	}
	HWND WindowGUI::GetHWND() const
	{
		return m_hwnd;
	}

	void WindowGUI::ShowWindowGUI()
	{
		if (!m_hwnd)
			return;
		LONG_PTR exStyle = GetWindowLongPtrW(m_hwnd, GWL_EXSTYLE);

		// 显示任务栏按钮
		exStyle &= ~WS_EX_TOOLWINDOW;
		exStyle |= WS_EX_APPWINDOW;

		SetWindowLongPtrW(m_hwnd, GWL_EXSTYLE, exStyle);

		// 让 Shell 重新识别窗口的任务栏状态
		SetWindowPos(
			m_hwnd,
			HWND_TOPMOST,
			0, 0, 0, 0,
			SWP_NOMOVE |
			SWP_NOSIZE |
			SWP_NOACTIVATE |
			SWP_FRAMECHANGED
		);

		ShowWindow(m_hwnd, SW_SHOW);
		SetForegroundWindow(m_hwnd);
	}
	void WindowGUI::HideWindowGUI()
	{
		LONG_PTR exStyle = GetWindowLongPtrW(m_hwnd, GWL_EXSTYLE);

		// 隐藏任务栏按钮
		exStyle |= WS_EX_TOOLWINDOW;
		exStyle &= ~WS_EX_APPWINDOW;

		SetWindowLongPtrW(m_hwnd, GWL_EXSTYLE, exStyle);

		SetWindowPos(
			m_hwnd,
			HWND_TOPMOST,
			0, 0, 0, 0,
			SWP_NOMOVE |
			SWP_NOSIZE |
			SWP_NOACTIVATE |
			SWP_FRAMECHANGED
		);

		ShowWindow(m_hwnd, SW_HIDE);
	}
}