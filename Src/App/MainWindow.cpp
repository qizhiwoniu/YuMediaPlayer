#include "MainWindow.h"  
#include <dwmapi.h>
#include <cstdint>
#include <memory>
#include <windowsx.h>
#include "Core/Theme.h"

#pragma comment(lib, "dwmapi.lib")

namespace YuMediaPlayer
{

	MainWindow::MainWindow()
		: m_hwnd(0)
		, m_theme()
		, m_trayIcon(GetModuleHandle(nullptr), L"YuMediaPlayer")
	{}

	MainWindow::~MainWindow()
	{}

	bool MainWindow::Initialize(const wchar_t* title, int width, int height)
	{
		if (!InitWindow(title, width, height))
			return false;

		RECT rc;
		GetClientRect(m_hwnd, &rc);

		m_trayIcon.Create(m_hwnd); // 创建托盘图标
		m_trayIcon.ShowBalloon(L"YuMediaPlayer 已启动", L"程序正在运行中...");

		int clientW = rc.right - rc.left;
		int clientH = rc.bottom - rc.top;
		
		/*if (!InitD3D11(clientW, clientH))
			return false;

		if (!InitDocument(clientW, clientH))
			return false;*/

		/*return  m_uiManager.Init(m_hwnd, m_device->GetDevice(), m_device->GetContext());*/
	}

	void MainWindow::Run()
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
	LRESULT MainWindow::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
	{
		MainWindow* pThis = nullptr;
		if (msg == WM_NCCREATE)
		{
			auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
			pThis = reinterpret_cast<MainWindow*>(cs->lpCreateParams);
			SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pThis));
		}
		else
		{
			pThis = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
		}

		if (pThis)
			return pThis->EventProc(hwnd, msg, wParam, lParam);

		return DefWindowProc(hwnd, msg, wParam, lParam);
	}

	bool MainWindow::InitWindow(const wchar_t* title, int width, int height)
	{
		HINSTANCE hInstance = GetModuleHandle(NULL);

		WNDCLASSEXW wc = {};
		wc.cbSize = sizeof(wc);
		wc.style = CS_HREDRAW | CS_VREDRAW;
		wc.lpfnWndProc = WndProc;
		wc.hInstance = hInstance;
		wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
		wc.hbrBackground = nullptr;
		wc.lpszClassName = L"YuMediaPlayerMainWindows1";
		wc.hIcon = (HICON)LoadImageW(
			nullptr,
			L"icons/logo.ico",
			IMAGE_ICON,
			0,
			0,
			LR_LOADFROMFILE | LR_DEFAULTSIZE
		);

		wc.hIconSm = (HICON)LoadImageW(
			nullptr,
			L"icons/logo.ico",
			IMAGE_ICON,
			GetSystemMetrics(SM_CXSMICON),
			GetSystemMetrics(SM_CYSMICON),
			LR_LOADFROMFILE
		);
		// 1. 注册窗口
		RegisterClassEx(&wc);

		RECT rc = { 0, 0, width, height };
		AdjustWindowRect(&rc, WS_POPUP | WS_THICKFRAME | WS_MINIMIZEBOX, TRUE); //WS_OVERLAPPEDWINDOW

		// 2. 创建窗口
		m_hwnd = CreateWindowEx(
			0,
			L"YuMediaPlayerMainWindows1",
			title,
			WS_POPUP | WS_THICKFRAME | WS_MINIMIZEBOX, //  WS_OVERLAPPEDWINDOW  ， WS_THICKFRAME 阴影
			CW_USEDEFAULT,
			CW_USEDEFAULT,
			rc.right - rc.left,
			rc.bottom - rc.top,
			nullptr,
			nullptr,
			wc.hInstance,
			this); // 参数this

		MARGINS margins = { 1,1,1,1 };
		DwmExtendFrameIntoClientArea(m_hwnd, &margins); // Win10/11 都有阴影

		// ── 居中到主显示器 ──────────────────────────────────────
		{
			HMONITOR hMon = MonitorFromWindow(m_hwnd, MONITOR_DEFAULTTONEAREST);
			MONITORINFO mi = { sizeof(mi) };
			GetMonitorInfo(hMon, &mi);

			int monW = mi.rcWork.right - mi.rcWork.left;
			int monH = mi.rcWork.bottom - mi.rcWork.top;
			int winW = rc.right - rc.left;
			int winH = rc.bottom - rc.top;

			int posX = mi.rcWork.left + (monW - winW) / 2;
			int posY = mi.rcWork.top + (monH - winH) / 2;

			SetWindowPos(m_hwnd, nullptr, posX, posY, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
		}
		// 3.显示窗口
		ShowWindow(m_hwnd, SW_SHOW);

		// 4.更新窗口
		UpdateWindow(m_hwnd);

		return m_hwnd != nullptr;
	}

	LRESULT MainWindow::EventProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
	{
		switch (msg)
		{
		case WM_ERASEBKGND:
			return 1;
		case WM_NCCALCSIZE:
		{
			if (wParam)
			{
				return 0;  // 完全移除 non-client area
			}
			break;
		}
		case WM_SIZE:
		{
			UINT w = LOWORD(lParam);
			UINT h = HIWORD(lParam);

			return 0;
		}
		case WM_NCLBUTTONDBLCLK: // 双击顶部大化与恢复
		{
			if (wParam == HTCAPTION)
			{
				if (IsZoomed(hwnd))
					ShowWindow(hwnd, SW_RESTORE);
				else
					ShowWindow(hwnd, SW_MAXIMIZE);
				return 0;
			}
			break;
		}
		case WM_NCHITTEST:
		{
			const LONG border = 6; // resize 边框厚度

			POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
			RECT wr;
			GetWindowRect(hwnd, &wr);

			bool left = pt.x < wr.left + border;
			bool right = pt.x >= wr.right - border;
			bool top = pt.y < wr.top + border;
			bool bottom = pt.y >= wr.bottom - border;
			bool middle = !left && !right && !top && !bottom;
			bool middleTop = !left && !right && top;
			bool middleBottom = !left && !right && bottom;
			bool center = pt.x >= wr.left + (wr.right - wr.left) / 2 && pt.y >= wr.top + (wr.bottom - wr.top) / 2;
			// 角
			if (top && left)     return HTTOPLEFT;
			if (top && right)    return HTTOPRIGHT;
			if (bottom && left)  return HTBOTTOMLEFT;
			if (bottom && right) return HTBOTTOMRIGHT;

			// 边
			if (left)   return HTLEFT;
			if (right)  return HTRIGHT;
			if (top)    return HTTOP;
			if (bottom) return HTBOTTOM;
			// 拖动
			/*if (pt.y >= wr.top && pt.y < wr.top + 30)
			{
				return HTCAPTION;
			}*/
			if (pt.y >= wr.bottom - 70 && pt.y < wr.bottom)
			{
				return HTCAPTION;
			}
			return HTCLIENT;
		}
		case WM_GETMINMAXINFO:
		{
			// 告诉 Windows 最大化时覆盖哪个显示器的工作区
			HMONITOR hMon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);

			MONITORINFO mi = { sizeof(mi) };
			GetMonitorInfo(hMon, &mi);

			MINMAXINFO* mmi = (MINMAXINFO*)lParam;

			//  
			mmi->ptMaxPosition.x = mi.rcWork.left - mi.rcMonitor.left;
			mmi->ptMaxPosition.y = mi.rcWork.top - mi.rcMonitor.top;

			mmi->ptMaxSize.x = mi.rcWork.right - mi.rcWork.left;
			mmi->ptMaxSize.y = mi.rcWork.bottom - mi.rcWork.top;

			return 0;
		}
		case WM_DESTROY:
			PostQuitMessage(0);
			return 0;

		default:
			return DefWindowProc(hwnd, msg, wParam, lParam);
		}
	}

	
}

