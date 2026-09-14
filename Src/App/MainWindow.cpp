#include "MainWindow.h"  
#include <dwmapi.h>
#include <cstdint>
#include <memory>
#include <windowsx.h>
#include "Core/Theme.h"
#include "UI/miniWindowGUI.h"

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

		return true; // 修复：原来这里没有返回值，函数是 bool 但会导致未定义行为
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
		// 修复：原来这里有一段坏掉的 switch/if/case 混合代码，
		// case WM_CONTEXTMENU 被嵌在 if(msg == WM_RBUTTONDOWN) 内部，
		// 导致这两个条件不可能同时满足，右键菜单代码是死代码，已删除。
		// 右键菜单逻辑现在统一放到 EventProc 的 WM_CONTEXTMENU 分支处理。

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

		// 去掉 WS_THICKFRAME 窗口自带的那条 1px 强调色描边。
		// DWMWA_BORDER_COLOR 是 Windows 11 22H2 (22621) 之后才有的属性，
		// 老版本 SDK 头文件里可能没有对应的宏定义，这里手动兜底一下。
		// 在不支持的系统（Win10 / 更老的 Win11）上这个调用会返回失败，
		// 属于安全失败，不影响其余逻辑，所以直接无条件调用即可。
#ifndef DWMWA_BORDER_COLOR
#define DWMWA_BORDER_COLOR 34
#endif
#ifndef DWMWA_COLOR_NONE
#define DWMWA_COLOR_NONE 0xFFFFFFFE
#endif
		{
			COLORREF borderColor = DWMWA_COLOR_NONE;
			DwmSetWindowAttribute(m_hwnd, DWMWA_BORDER_COLOR, &borderColor, sizeof(borderColor));
		}

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
		case WM_CONTEXTMENU:
		{
			// WM_CONTEXTMENU 的 lParam 本身就是屏幕坐标，不需要 ClientToScreen 转换
			POINT pt;
			pt.x = GET_X_LPARAM(lParam);
			pt.y = GET_Y_LPARAM(lParam);
			int cmd = ShowMiniPlayerContextMenu(hwnd, pt);
			if (cmd == ContextMenuCommand::Exit)
				PostQuitMessage(0);
			return 0;
		}
		case WM_NCRBUTTONUP:
		{
			// 底部 70px 的"空白拖动区"在 WM_NCHITTEST 里返回的是 HTCAPTION，落在非客户区，
			// 右键点这块地方 Windows 发的是 WM_NCRBUTTONUP 而不是 WM_CONTEXTMENU，
			// 所以这里也要单独处理，否则拖动区右键没反应。
			// NC 消息的 lParam 本身就是屏幕坐标，wParam 是命中测试结果。
			if (wParam == HTCAPTION)
			{
				POINT pt;
				pt.x = GET_X_LPARAM(lParam);
				pt.y = GET_Y_LPARAM(lParam);
				int cmd = ShowMiniPlayerContextMenu(hwnd, pt);
				if (cmd == ContextMenuCommand::Exit)
					PostQuitMessage(0);
				return 0; // 吞掉默认行为（系统菜单），避免和我们自己的菜单冲突
			}
			break;
		}
		case WM_MEASUREITEM:
			// 右键菜单是自绘的（owner-draw），系统在弹出前先发这个消息
			// 来问每一项应该占多大尺寸，交给 miniWindowGUI 里统一处理。
			MeasureMiniPlayerMenuItem(*reinterpret_cast<MEASUREITEMSTRUCT*>(lParam));
			return TRUE;
		case WM_DRAWITEM:
			// 同上，真正把每一项画出来（黑色背景 + 白色文字）的地方。
			DrawMiniPlayerMenuItem(*reinterpret_cast<const DRAWITEMSTRUCT*>(lParam));
			return TRUE;
		case WM_ERASEBKGND:
			return 1;
		case WM_PAINT:
		{
			// 修复暗黑模式背景发白的问题：之前 WM_ERASEBKGND 返回 1 阻止了系统擦除背景，
			// 但没有任何地方真正绘制客户区，导致颜色是未定义的（多数情况下显示白色/残影）。
			// 这里先用 GDI 填一个深色纯色，后续接入真正的渲染（D3D/ImGui等）后可以删掉这段，
			// 改成用 m_theme 里定义的背景色，或者干脆交给渲染层处理。
			PAINTSTRUCT ps;
			HDC hdc = BeginPaint(hwnd, &ps);

			RECT rc;
			GetClientRect(hwnd, &rc);

			static HBRUSH s_darkBrush = CreateSolidBrush(RGB(24, 24, 24)); // 深色背景，可按需替换为主题色
			FillRect(hdc, &rc, s_darkBrush);

			EndPaint(hwnd, &ps);
			return 0;
		}
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
			// 拖动（左键空白处拖动窗口）
			/*if (pt.y >= wr.top && pt.y < wr.top + 30)
			{
				return HTCAPTION;
			}*/
			if (pt.y >= wr.bottom - 70 && pt.y < wr.bottom)
			{
				return HTCAPTION;
			}
			// 修复：原来这里有 if (msg == WM_RBUTTONDOWN) return WM_CONTEXTMENU;
			// msg 在这个分支里恒等于 WM_NCHITTEST，不可能等于 WM_RBUTTONDOWN，
			// 而且 WM_NCHITTEST 应该返回 HT* 命中测试码而不是消息号，已删除该行。
			// 右键弹出菜单会由 WM_CONTEXTMENU 消息自动触发，不需要在这里处理。
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

		return DefWindowProc(hwnd, msg, wParam, lParam);
	}
	
}
