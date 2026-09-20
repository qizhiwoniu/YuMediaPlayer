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

	WindowGUI::WindowGUI()
		: m_hwnd(nullptr)
	{
		// 保存全局实例指针，用于 WndProc 回调
		g_windowGUIInstance = this;
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

	LRESULT CALLBACK WindowGUI::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
	{
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

			// 填充背景色
			HBRUSH hBrush = CreateSolidBrush(RGB(24, 24, 24));  // 深灰色背景
			FillRect(hdc, &ps.rcPaint, hBrush);
			DeleteObject(hBrush);

			EndPaint(hwnd, &ps);
			return 0;
		}
		case WM_NCHITTEST:
			{
				const LONG border = 6;

				POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
				RECT wr;
				GetWindowRect(hwnd, &wr);

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
				if (pt.y >= wr.top && pt.y < wr.top + 30)
				{
					return HTCAPTION;
				}
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
			width=1200, height=720,           // 宽高
			nullptr,                          // 父窗口
			nullptr,                          // 菜单
			GetModuleHandleW(nullptr),
			this                              // ★ 传入 this 指针给 WM_CREATE
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