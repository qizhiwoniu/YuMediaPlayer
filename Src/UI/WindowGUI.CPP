#include <windows.h>
#include <windowsx.h>
#include <memory>
#include <dwmapi.h>
#include "Core/Theme.h"
#include "WindowGui.h"

#pragma comment(lib, "dwmapi.lib")
namespace YuMediaPlayer
{

	WindowGUI::WindowGUI()
		: m_hwnd(0)
		, m_theme()
	{}
	WindowGUI::~WindowGUI()
	{}
	//void WindowGUI::Run()
	//{
	//	MSG msg = {};

	//	bool needsRedraw = true;

	//	while (msg.message != WM_QUIT)
	//	{
	//		if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
	//		{
	//			TranslateMessage(&msg);
	//			DispatchMessage(&msg);
	//			needsRedraw = true;
	//		}
	//		else
	//		{
	//			if (needsRedraw)
	//			{
	//				/*RenderFrame();*/
	//				needsRedraw = false;
	//			}
	//			else
	//			{
	//				WaitMessage(); // 等待新消息，节省 CPU
	//			}
	//		}
	//	}

	//}
	//LRESULT WindowGUI::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
	//{
	//	WindowGUI* pThis = nullptr;
	//	if (msg == WM_NCCREATE)
	//	{
	//		auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
	//		pThis = reinterpret_cast<WindowGUI*>(cs->lpCreateParams);
	//		SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pThis));
	//	}
	//	else
	//	{
	//		pThis = reinterpret_cast<WindowGUI*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
	//	}

	//	if (pThis)
	//		return pThis->EventProc(hwnd, msg, wParam, lParam);

	//	return DefWindowProc(hwnd, msg, wParam, lParam);
	//}
	//bool WindowGUI::InitWindow(const wchar_t* title, int width, int height)
	//{
	//	HINSTANCE hInstance = GetModuleHandle(NULL);

	//	WNDCLASSEXW wc = {};
	//	wc.cbSize = sizeof(wc);
	//	wc.style = CS_HREDRAW | CS_VREDRAW;
	//	wc.lpfnWndProc = WndProc;
	//	wc.hInstance = hInstance;
	//	wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
	//	wc.hbrBackground = nullptr;
	//	wc.lpszClassName = L"YuMediaPlayerMainWindows1";
	//	wc.hIcon = (HICON)LoadImageW(
	//		nullptr,
	//		L"icons/logo.ico",
	//		IMAGE_ICON,
	//		0,
	//		0,
	//		LR_LOADFROMFILE | LR_DEFAULTSIZE
	//	);

	//	wc.hIconSm = (HICON)LoadImageW(
	//		nullptr,
	//		L"icons/logo.ico",
	//		IMAGE_ICON,
	//		GetSystemMetrics(SM_CXSMICON),
	//		GetSystemMetrics(SM_CYSMICON),
	//		LR_LOADFROMFILE
	//	);
	//	// 1. 注册窗口
	//	RegisterClassEx(&wc);

	//	RECT rc = { 0, 0, width, height };
	//	AdjustWindowRect(&rc, WS_POPUP | WS_THICKFRAME | WS_MINIMIZEBOX, TRUE); //WS_OVERLAPPEDWINDOW

	//	// 2. 创建窗口
	//	m_hwnd = CreateWindowEx(
	//		0,
	//		L"YuMediaPlayerMainWindows1",
	//		title,
	//		WS_POPUP | WS_THICKFRAME | WS_MINIMIZEBOX, //  WS_OVERLAPPEDWINDOW  ， WS_THICKFRAME 阴影
	//		CW_USEDEFAULT,
	//		CW_USEDEFAULT,
	//		rc.right - rc.left,
	//		rc.bottom - rc.top,
	//		nullptr,
	//		nullptr,
	//		wc.hInstance,
	//		this); // 参数this

	//	MARGINS margins = { 1,1,1,1 };
	//	DwmExtendFrameIntoClientArea(m_hwnd, &margins); // Win10/11 都有阴影

	//	// ── 居中到主显示器 ──────────────────────────────────────
	//	{
	//		HMONITOR hMon = MonitorFromWindow(m_hwnd, MONITOR_DEFAULTTONEAREST);
	//		MONITORINFO mi = { sizeof(mi) };
	//		GetMonitorInfo(hMon, &mi);

	//		int monW = mi.rcWork.right - mi.rcWork.left;
	//		int monH = mi.rcWork.bottom - mi.rcWork.top;
	//		int winW = rc.right - rc.left;
	//		int winH = rc.bottom - rc.top;

	//		int posX = mi.rcWork.left + (monW - winW) / 2;
	//		int posY = mi.rcWork.top + (monH - winH) / 2;

	//		SetWindowPos(m_hwnd, nullptr, posX, posY, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
	//	}
	//	// 3.显示窗口
	//	ShowWindow(m_hwnd, SW_SHOW);

	//	// 4.更新窗口
	//	UpdateWindow(m_hwnd);

	//	return m_hwnd != nullptr;
	//}
	
}