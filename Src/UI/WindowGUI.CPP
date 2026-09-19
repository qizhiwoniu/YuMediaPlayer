#include <windows.h>
#include <windowsx.h>
#include <memory>
#include <dwmapi.h>
#include "Core/Theme.h"
#include "WindowGUI.h"

#pragma comment(lib, "dwmapi.lib")
namespace YuMediaPlayer
{

	WindowGUI::WindowGUI()
		: m_hwnd(0)
		//, m_theme()
	{}
	WindowGUI::~WindowGUI()
	{}
	void WindowGUI::Run()
	{
		MSG msg = {};
		bool needsRedraw = true;
		while (msg.message != WM_QUIT)
		{
			if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
			{
				TranslateMessage(&msg);
				DispatchMessage(&msg);
				needsRedraw = true;
			}
			else
			{
				if (needsRedraw)
				{
					/*RenderFrame();*/
					needsRedraw = false;
				}
				else
				{
					WaitMessage(); // 等待新消息，节省 CPU
				}
			}
		}

	}
	LRESULT WindowGUI::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
	{
		/*WindowGUI* pThis = nullptr;
		if (msg == WM_NCCREATE)
		{
			auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
			pThis = reinterpret_cast<WindowGUI*>(cs->lpCreateParams);
			SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pThis));
		}
		else
		{
			pThis = reinterpret_cast<WindowGUI*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
		}

		if (pThis)
			return pThis->EventProc(hwnd, msg, wParam, lParam);
			*/
		return DefWindowProc(hwnd, msg, wParam, lParam);
	}
	bool WindowGUI::InitWindow(const wchar_t* title, int width, int height)
	{
		// 1. 注册窗口类
		static const wchar_t CLASS_NAME[] = L"YuMediaPlayerWindowGUIClass";
		static bool classRegistered = false;

		if (!classRegistered)
		{
			WNDCLASSEX wc = {};
			wc.cbSize = sizeof(WNDCLASSEX);
			wc.style = CS_HREDRAW | CS_VREDRAW;
			wc.lpfnWndProc = WndProc;
			wc.cbClsExtra = 0;
			wc.cbWndExtra = 0;
			wc.hInstance = GetModuleHandle(nullptr);
			wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
			wc.hbrBackground = nullptr;
			wc.lpszClassName = CLASS_NAME;

			if (!RegisterClassEx(&wc))
				return false;

			classRegistered = true;
		}

		// 2. 创建分层窗口 - WS_EX_LAYERED 是关键！
		m_hwnd = CreateWindowEx(
			WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW, // 分层窗口 + 总在最前 + 隐藏任务栏
			CLASS_NAME,
			title,
			WS_POPUP,  // 弹出式窗口，无标题栏
			100, 100,  // 初始位置 (x, y)
			width, height,
			nullptr,   // 父窗口
			nullptr,   // 菜单
			GetModuleHandle(nullptr),
			this       // 传入 this 指针
		);

		if (!m_hwnd)
			return false;

		// 3. 绑定 this 指针到窗口用户数据
		SetWindowLongPtr(m_hwnd, GWLP_USERDATA, (LONG_PTR)this);

		// ── 居中到主显示器 ──────────────────────────────────────
		{
			HMONITOR hMon = MonitorFromWindow(m_hwnd, MONITOR_DEFAULTTONEAREST);
			MONITORINFO mi = { sizeof(mi) };
			GetMonitorInfo(hMon, &mi);

			int monW = mi.rcMonitor.right - mi.rcMonitor.left;
			int monH = mi.rcMonitor.bottom - mi.rcMonitor.top;
			int winW = width;
			int winH = height;

			int posX = mi.rcMonitor.left + (monW - winW) / 2;
			int posY = mi.rcMonitor.top + (monH - winH) / 2;

			SetWindowPos(m_hwnd, nullptr, posX, posY, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
		}
		// 3.显示窗口
		ShowWindow(m_hwnd, SW_SHOW);

		// 4.更新窗口
		UpdateWindow(m_hwnd);

		return m_hwnd != nullptr;
	}
	
	void WindowGUI::ShowWindowGUI()
	{
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