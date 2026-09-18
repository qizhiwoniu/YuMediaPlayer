#include "MainWindow.h"  
#include <dwmapi.h>
#include <cstdint>
#include <memory>
#include <windowsx.h>
#include <algorithm>
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

		//m_trayIcon.Create(m_hwnd);  
		//m_trayIcon.ShowBalloon(L"YuMediaPlayer starting", L"starting...");

		int clientW = rc.right - rc.left;
		int clientH = rc.bottom - rc.top;

		Composite();

		return true; 
	}

	bool MainWindow::InitWindow(const wchar_t* title, int width, int height)
	{
		// 1. 注册窗口类
		static const wchar_t CLASS_NAME[] = L"YuMediaPlayerWindowClass";
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
			WS_EX_LAYERED | WS_EX_TOPMOST,  // 分层窗口 + 总在最前
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

		// 4. 显示窗口
		ShowWindow(m_hwnd, SW_SHOW);
		UpdateWindow(m_hwnd);

		// 5. 设置定时器用于边缘吸附检测和自动收起
		m_edgeHoverTimer = SetTimer(m_hwnd, 1001, 100, nullptr);
		if (m_edgeHoverTimer == 0)
			return false;

		return true;
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
					WaitMessage();
				}
			}
		}

	}

	LRESULT CALLBACK MainWindow::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
	{
		MainWindow* pThis = nullptr;

		if (msg == WM_CREATE)
		{
			CREATESTRUCT* pCreate = reinterpret_cast<CREATESTRUCT*>(lParam);
			pThis = reinterpret_cast<MainWindow*>(pCreate->lpCreateParams);
			SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)pThis);
		}
		else
		{
			pThis = reinterpret_cast<MainWindow*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
		}

		if (pThis)
			return pThis->EventProc(hwnd, msg, wParam, lParam);
		else
			return DefWindowProc(hwnd, msg, wParam, lParam);
	}

	LRESULT MainWindow::EventProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
	{
		switch (msg)
		{
		case WM_ENTERSIZEMOVE:
		{
			m_isMoving = true;
			return 0;
		}
		case WM_MOVING:
		{
			return DefWindowProc(hwnd, msg, wParam, lParam);
		}
		case WM_EXITSIZEMOVE:
		{
			m_isMoving = false;

			// 收缩状态下，用户可能手动把"小圆"沿边缘拖到了别的位置（比如拖到右下角）。
			// 这里把拖动后的最新位置同步回 m_savedWindowX/Y，
			// 否则展开时会用收缩前记录的旧位置，导致"跳回"原来的地方。
			if (m_isCollapsed && !m_isAnimating)
			{
				RECT wr;
				GetWindowRect(hwnd, &wr);

				switch (m_collapsedEdge)
				{
				case CollapseEdge::Right:
				case CollapseEdge::Left:
					// 左右两侧收起时，收起态的窗口左右位置是贴边固定的，
					// 拖动主要改变的是垂直位置，所以同步 Y。
					m_savedWindowY = wr.top;
					break;
				case CollapseEdge::Top:
				case CollapseEdge::Bottom:
					// 上下两侧收起时，同理同步 X。
					m_savedWindowX = wr.left;
					break;
				default:
					break;
				}
			}

			// 只在未收缩且未动画时才检查边缘吸附
			// 已收缩状态下只让鼠标悬停检测处理展开逻辑
			if (!m_isCollapsed && !m_isAnimating)
				CheckAndCollapseAtEdge();
			return 0;
		}
		case WM_TIMER:
		{
			if (wParam == 1001)
			{
				if (m_isCollapsed && !m_isAnimating)
					CheckCollapsedMouseHover();
				else if (!m_isMoving && !m_isAnimating)
					CheckAutoCollapse();

				return 0;
			}
			else if (wParam == 2001)
			{
				// 动画定时器
				UpdateCollapseAnimation();
				return 0;
			}
			break;
		}
		case WM_CONTEXTMENU:
		{
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
			if (wParam == HTCAPTION)
			{
				POINT pt;
				pt.x = GET_X_LPARAM(lParam);
				pt.y = GET_Y_LPARAM(lParam);
				int cmd = ShowMiniPlayerContextMenu(hwnd, pt);
				if (cmd == ContextMenuCommand::Exit)
					PostQuitMessage(0);
				return 0; 
			}
			break;
		}
		case WM_MEASUREITEM:	
			MeasureMiniPlayerMenuItem(*reinterpret_cast<MEASUREITEMSTRUCT*>(lParam));
			return TRUE;
		case WM_DRAWITEM:
			DrawMiniPlayerMenuItem(*reinterpret_cast<const DRAWITEMSTRUCT*>(lParam));
			return TRUE;
		case WM_ERASEBKGND:
			return 1;
		case WM_PAINT:
		{
			PAINTSTRUCT ps;
			HDC hdc = BeginPaint(hwnd, &ps);

			RECT rc;
			GetClientRect(hwnd, &rc);

			static HBRUSH s_darkBrush = CreateSolidBrush(RGB(24, 24, 24)); // background
			FillRect(hdc, &rc, s_darkBrush);

			EndPaint(hwnd, &ps);
			return 0;
		}
		case WM_NCCALCSIZE:
		{
			if (wParam)
			{
				return 0;  
			}
			break;
		}
		case WM_SIZE:
		{
			UINT w = LOWORD(lParam);
			UINT h = HIWORD(lParam);
			Composite(); 
			return 0;
		}
		case WM_LBUTTONUP:
		{
			if (m_isCollapsed)
			{
				if (m_collapsedEdge == MainWindow::CollapseEdge::Right)
				{
					StartExpandAnimation();
				}
				else
				{
					RestoreWindow();
				}
				return 0;
			}
			return 0;
		}
		case WM_MOUSEMOVE:
		{
			POINT pt;
			GetCursorPos(&pt);
			m_lastMousePos = pt;
			m_lastMouseMoveTick = GetTickCount();

			if (m_isCollapsed)
				CheckCollapsedMouseHover();

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
			bool middle = !left && !right && !top && !bottom;
			bool middleTop = !left && !right && top;
			bool middleBottom = !left && !right && bottom;
			bool center = pt.x >= wr.left + (wr.right - wr.left) / 2 && pt.y >= wr.top + (wr.bottom - wr.top) / 2;
			
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
			if (pt.y >= wr.bottom - 70 && pt.y < wr.bottom)
			{
				return HTCAPTION;
			}
			return HTCLIENT;
		}
		case WM_GETMINMAXINFO:
		{
			HMONITOR hMon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);

			MONITORINFO mi = { sizeof(mi) };
			GetMonitorInfo(hMon, &mi);

			MINMAXINFO* mmi = (MINMAXINFO*)lParam;

			mmi->ptMaxPosition.x = mi.rcWork.left - mi.rcMonitor.left;
			mmi->ptMaxPosition.y = mi.rcWork.top - mi.rcMonitor.top;

			mmi->ptMaxSize.x = mi.rcWork.right - mi.rcWork.left;
			mmi->ptMaxSize.y = mi.rcWork.bottom - mi.rcWork.top;

			return 0;
		}
		case WM_DESTROY:
			if (m_edgeHoverTimer != 0)
			{
				KillTimer(hwnd, m_edgeHoverTimer);
				m_edgeHoverTimer = 0;
			}
			if (m_animationTimer != 0)
			{
				KillTimer(hwnd, m_animationTimer);
				m_animationTimer = 0;
			}
			PostQuitMessage(0);
			return 0;

		default:
			return DefWindowProc(hwnd, msg, wParam, lParam);
		}

		return DefWindowProc(hwnd, msg, wParam, lParam);
	}

	void MainWindow::StartCollapseAnimation()
	{
		if (m_isAnimating || m_isCollapsed)
			return;

		m_isAnimating = true;
		m_animationIsCollapsing = true;
		m_animationProgress = 0.0f;

		RECT wr;
		GetWindowRect(m_hwnd, &wr);
		m_animationStartWidth = wr.right - wr.left;

		// 右侧收起的目标宽度（保留卡片的最小宽度 + 头像）
		// 卡片从 10px 开始，头像在 8px，宽度 84px，所以右边卡片边框到头像右边是 92px
		// 保留一点卡片的宽度，比如 110px 可以露出头像和一点背景框
		const int COLLAPSED_WIDTH = 110;
		m_animationTargetWidth = COLLAPSED_WIDTH;

		// 启动动画定时器 (16ms ≈ 60fps)
		m_animationTimer = SetTimer(m_hwnd, 2001, 16, nullptr);
	}

	void MainWindow::StartExpandAnimation()
	{
		if (m_isAnimating || !m_isCollapsed)
			return;

		m_isAnimating = true;
		m_animationIsCollapsing = false;
		m_animationProgress = 0.0f;

		RECT wr;
		GetWindowRect(m_hwnd, &wr);
		m_animationStartWidth = wr.right - wr.left;
		m_animationTargetWidth = m_savedWindowWidth;

		// 启动动画定时器
		m_animationTimer = SetTimer(m_hwnd, 2001, 16, nullptr);
	}

	void MainWindow::UpdateCollapseAnimation()
	{
		if (!m_isAnimating)
			return;

		// 动画时长：300ms
		constexpr float ANIMATION_DURATION_MS = 300.0f;
		constexpr float FRAME_TIME_MS = 16.0f;

		m_animationProgress += FRAME_TIME_MS / ANIMATION_DURATION_MS;

		if (m_animationProgress >= 1.0f)
		{
			m_animationProgress = 1.0f;
			m_isAnimating = false;

			// 动画完成
			if (m_animationTimer != 0)
			{
				KillTimer(m_hwnd, m_animationTimer);
				m_animationTimer = 0;
			}

			if (m_animationIsCollapsing)
			{
				// 收起动画完成
				m_isCollapsed = true;
				
				// 计算最终位置：头像右边缘（92px）停在屏幕右边缘
				HMONITOR hMon = MonitorFromWindow(m_hwnd, MONITOR_DEFAULTTONEAREST);
				MONITORINFO mi = { sizeof(mi) };
				GetMonitorInfo(hMon, &mi);

				// 头像：x=8, width=84, 右边缘=92
				// 我们让头像右边缘停在屏幕右边缘减去一点距离（比如10px）
				int finalX = mi.rcWork.right - 92 - 10;
				
				SetWindowPos(
					m_hwnd, nullptr,
					finalX, m_savedWindowY,
					m_animationTargetWidth, m_savedWindowHeight,
					SWP_NOZORDER | SWP_NOACTIVATE);
			}
			else
			{
				// 展开动画完成
				m_isCollapsed = false;
				SetWindowPos(
					m_hwnd, nullptr,
					m_savedWindowX, m_savedWindowY,
					m_savedWindowWidth, m_savedWindowHeight,
					SWP_NOZORDER | SWP_NOACTIVATE);
			}

			Composite();
			return;
		}

		// 缓动函数（平方缓出）
		float easeProgress = m_animationProgress * m_animationProgress;

		RECT wr;
		GetWindowRect(m_hwnd, &wr);

		if (m_animationIsCollapsing)
		{
			// 收起动画：向右收缩
			// 计算当前窗口宽度
			int currentWidth = m_animationStartWidth + 
				(int)((m_animationTargetWidth - m_animationStartWidth) * easeProgress);

			// 获取屏幕信息
			HMONITOR hMon = MonitorFromWindow(m_hwnd, MONITOR_DEFAULTTONEAREST);
			MONITORINFO mi = { sizeof(mi) };
			GetMonitorInfo(hMon, &mi);

			// 计算目标X位置：头像右边缘（92px）停在屏幕右边缘减去一点距离
			int targetX = mi.rcWork.right - 92 - 10;
			
			// 线性插值当前X位置
			int currentX = m_savedWindowX + (int)((targetX - m_savedWindowX) * easeProgress);

			SetWindowPos(
				m_hwnd, nullptr,
				currentX, wr.top,
				currentWidth, wr.bottom - wr.top,
				SWP_NOZORDER | SWP_NOACTIVATE);
		}
		else
		{
			// 展开动画：从右到左平滑展开
			// 窗口宽度从当前宽度增长到原始宽度
			int currentWidth = m_animationStartWidth + 
				(int)((m_animationTargetWidth - m_animationStartWidth) * easeProgress);

			// 获取屏幕信息
			HMONITOR hMon = MonitorFromWindow(m_hwnd, MONITOR_DEFAULTTONEAREST);
			MONITORINFO mi = { sizeof(mi) };
			GetMonitorInfo(hMon, &mi);

			// 头像始终停在屏幕右边缘
			// 当前X = 屏幕右边缘 - 头像右边缘 - 当前宽度中卡片的部分
			// 为了让窗口从右向左展开，X应该向左移动
			int collapsedX = mi.rcWork.right - 92 - 10;
			
			// 计算展开时的X位置：从收起位置逐渐向左展开到原始位置
			int currentX = collapsedX - (int)((currentWidth - m_animationStartWidth) * 0.5f);
			
			// 更准确的计算：保持卡片右边界向左移动
			// 原始X位置是 m_savedWindowX
			// 当前应该逐渐从 collapsedX 移动到 m_savedWindowX
			currentX = collapsedX + (int)((m_savedWindowX - collapsedX) * easeProgress);

			SetWindowPos(
				m_hwnd, nullptr,
				currentX, wr.top,
				currentWidth, wr.bottom - wr.top,
				SWP_NOZORDER | SWP_NOACTIVATE);
		}

		Composite();
	}

	void MainWindow::CheckAutoCollapse()
	{
		if (m_isCollapsed || m_isMoving || m_isAnimating)
			return;
		POINT pt;
		if (!GetCursorPos(&pt))
			return;

		RECT wr;
		GetWindowRect(m_hwnd, &wr);

		if (PtInRect(&wr, pt))
			return;

		// 鼠标刚离开窗口时给一点缓冲，避免移动到窗口外一个像素就瞬间缩回。
		// 这个延迟也让体验更接近播放器悬浮窗。
		constexpr DWORD COLLAPSE_DELAY_MS = 120;
		DWORD now = GetTickCount();

		if (m_lastMouseMoveTick != 0 &&
			static_cast<DWORD>(now - m_lastMouseMoveTick) < COLLAPSE_DELAY_MS)
			return;

		CheckAndCollapseAtEdge();
	}

	void MainWindow::CheckCollapsedMouseHover()
	{
		if (!m_isCollapsed || m_isMoving)
			return;

		POINT pt;
		if (!GetCursorPos(&pt))
			return;

		RECT wr;
		GetWindowRect(m_hwnd, &wr);

		HMONITOR hMon = MonitorFromWindow(
			m_hwnd,
			MONITOR_DEFAULTTONEAREST);

		MONITORINFO mi = { sizeof(mi) };
		if (!GetMonitorInfo(hMon, &mi))
			return;

		// 收起后，鼠标靠近当前隐藏的屏幕边缘即可恢复。
		// 对右侧尤其重要：即使透明区域没有 WM_MOUSEMOVE，也能恢复。
		constexpr int HOVER_THRESHOLD = 18;

		bool shouldRestore = false;

		switch (m_collapsedEdge)
		{
		case CollapseEdge::Left:
			shouldRestore =
				pt.x <= mi.rcWork.left + HOVER_THRESHOLD &&
				pt.y >= wr.top &&
				pt.y <= wr.bottom;
			break;

		case CollapseEdge::Right:
			shouldRestore =
				pt.x >= mi.rcWork.right - HOVER_THRESHOLD &&
				pt.y >= wr.top &&
				pt.y <= wr.bottom;
			break;

		case CollapseEdge::Top:
			shouldRestore =
				pt.y <= mi.rcWork.top + HOVER_THRESHOLD &&
				pt.x >= wr.left &&
				pt.x <= wr.right;
			break;

		case CollapseEdge::Bottom:
			shouldRestore =
				pt.y >= mi.rcWork.bottom - HOVER_THRESHOLD &&
				pt.x >= wr.left &&
				pt.x <= wr.right;
			break;

		default:
			break;
		}

		if (shouldRestore)
		{
			if (m_collapsedEdge == CollapseEdge::Right)
			{
				StartExpandAnimation();
			}
			else
			{
				RestoreWindow();
			}
		}
	}

	void MainWindow::CheckAndCollapseAtEdge()
	{
		if (m_isCollapsed)
			return;

		RECT wr;
		GetWindowRect(m_hwnd, &wr);
		CollapseAtEdgeRect(wr);
	}

	void MainWindow::CollapseAtEdgeRect(const RECT& wr)
	{
		if (m_isCollapsed || m_isAnimating)
			return;

		HMONITOR hMon = MonitorFromRect(&wr, MONITOR_DEFAULTTONEAREST);
		MONITORINFO mi = { sizeof(mi) };
		GetMonitorInfo(hMon, &mi);

		const int EDGE_THRESHOLD = 5;
		const int COLLAPSED_SIZE = 94;
		const int AVATAR_RIGHT = 92; // avatarRect: x=8, width=84 -> right=92
		const int RIGHT_REVEAL = 14; // 右侧收起只露出头像最右侧 14px

		const int windowWidth = wr.right - wr.left;
		const int windowHeight = wr.bottom - wr.top;
		const bool nearLeft = wr.left <= mi.rcWork.left + EDGE_THRESHOLD;
		const bool nearRight = wr.right >= mi.rcWork.right - EDGE_THRESHOLD;
		const bool nearTop = wr.top <= mi.rcWork.top + EDGE_THRESHOLD;
		const bool nearBottom = wr.bottom >= mi.rcWork.bottom - EDGE_THRESHOLD;

		if (!nearLeft && !nearRight && !nearTop && !nearBottom)
			return;

		m_savedWindowWidth = windowWidth;
		m_savedWindowHeight = windowHeight;
		m_savedWindowX = wr.left;
		m_savedWindowY = wr.top;

		// 只在右侧使用动画收起，其他方向使用原来的快速收起
		if (nearRight)
		{
			m_collapsedEdge = CollapseEdge::Right;
			// 启动向右收起的动画
			ReleaseCapture();
			StartCollapseAnimation();
		}
		else
		{
			// 其他边缘快速收起（不用动画）
			int newX = wr.left;
			int newY = wr.top;
			int newWidth = COLLAPSED_SIZE;
			int newHeight = COLLAPSED_SIZE;

			if (nearLeft)
			{
				m_collapsedEdge = CollapseEdge::Left;
				newX = mi.rcWork.left;
				newY = wr.top;
			}
			else if (nearTop)
			{
				m_collapsedEdge = CollapseEdge::Top;
				newX = wr.left;
				newY = mi.rcWork.top;
			}
			else if (nearBottom)
			{
				m_collapsedEdge = CollapseEdge::Bottom;
				newX = wr.left;
				newY = mi.rcWork.bottom - COLLAPSED_SIZE;
			}

			m_isCollapsed = true;

			SetWindowPos(
				m_hwnd,
				nullptr,
				newX, newY, newWidth, newHeight,
				SWP_NOZORDER | SWP_NOACTIVATE);

			ReleaseCapture();
			Composite();
		}
	}

	bool MainWindow::EnsureLayeredBitmap(int width, int height)
	{
		// 如果已有合适大小的位图，直接复用
		if (m_dibSection && m_bitmapW == width && m_bitmapH == height)
			return true;

		// 释放旧位图
		if (m_dibSection)
		{
			DeleteObject(m_dibSection);
			m_dibSection = nullptr;
			m_dibBits = nullptr;
		}

		// 创建 32bpp DIB (Device Independent Bitmap)
		BITMAPINFOHEADER bih = {};
		bih.biSize = sizeof(BITMAPINFOHEADER);
		bih.biWidth = width;
		bih.biHeight = -height;  // 负数表示从上到下，而不是从下到上
		bih.biPlanes = 1;
		bih.biBitCount = 32;
		bih.biCompression = BI_RGB;

		HDC hScreenDC = GetDC(nullptr);
		m_dibSection = CreateDIBSection(hScreenDC, (BITMAPINFO*)&bih, DIB_RGB_COLORS, &m_dibBits, nullptr, 0);
		ReleaseDC(nullptr, hScreenDC);

		if (!m_dibSection)
			return false;

		m_bitmapW = width;
		m_bitmapH = height;
		return true;
	}

	void MainWindow::Composite()
	{
		// SetWindowPos 会同步触发 WM_SIZE，而 WM_SIZE 又会调用 Composite。
		// 如果这里发生重入，内层 Composite 可能释放正在被外层 GDI+ Bitmap 使用的 DIB，
		// 最终导致 0xC0000374（堆已损坏）。直接禁止重入。
		if (m_isCompositing)
			return;

		m_isCompositing = true;

		struct CompositeGuard
		{
			bool& flag;
			~CompositeGuard() { flag = false; }
		} guard{ m_isCompositing };

		RECT wr;
		GetWindowRect(m_hwnd, &wr);
		int w = wr.right - wr.left;
		int h = wr.bottom - wr.top;

		if (w <= 0 || h <= 0)
			return;

		if (!EnsureLayeredBitmap(w, h))
			return;

		// 使用 GDI+ 在位图上绘制
		{
			Gdiplus::Bitmap bitmap(w, h, w * 4, 0xE200B, (BYTE*)m_dibBits);
			Gdiplus::Graphics g(&bitmap);
			g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
			g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
			g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);

			// 清空为全透明
			g.Clear(Gdiplus::Color(0, 0, 0, 0));

			if (m_isCollapsed && !m_isAnimating)
			{
				// 收起状态下也保留背景卡片，不再完全透明消失——
				// 只是卡片和窗口本身都已经缩小到只够包住头像的尺寸。
				//
				// 头像本身的位置(X=8)是固定的，不能跟着改——边缘吸附动画那边
				// 用的是"头像右边缘=92"这个假设来计算窗口该贴在屏幕哪个位置
				// (见 CollapseAtEdgeRect / UpdateCollapseAnimation 里的 AVATAR_RIGHT=92)，
				// 改了这里 X/宽度 就会跟那边对不上。
				// 所以改成让卡片以头像为中心、左右留白相等来画，而不是沿用
				// 展开卡片那种"左右各留 10px 窗口边距"的公式——窗口这么窄的时候，
				// 那个公式会让头像偏左、右边露出一截卡片，看着像"缩到一边"了。
				Gdiplus::RectF avatarRect(8.0f, 0.0f, 84.0f, 84.0f);

				const float avatarSideMargin = 6.0f; // 头像左右各留 6px 卡片边
				float cardX = (std::max)(0.0f, avatarRect.X - avatarSideMargin);
				float cardRight = (std::min)((float)w, avatarRect.GetRight() + avatarSideMargin);

				Gdiplus::RectF cardRect(cardX, 20.0f, cardRight - cardX, (float)h - 30.0f);
				Gdiplus::SolidBrush cardBrush(Gdiplus::Color(255, 40, 40, 40));

				Gdiplus::GraphicsPath path;
				float radius = 12.0f;
				float d = (std::min)(radius * 2.0f, (std::min)(cardRect.Width, cardRect.Height));
				path.AddArc(cardRect.X, cardRect.Y, d, d, 180.0f, 90.0f);
				path.AddArc(cardRect.GetRight() - d, cardRect.Y, d, d, 270.0f, 90.0f);
				path.AddArc(cardRect.GetRight() - d, cardRect.GetBottom() - d, d, d, 0.0f, 90.0f);
				path.AddArc(cardRect.X, cardRect.GetBottom() - d, d, d, 90.0f, 90.0f);
				path.CloseFigure();
				g.FillPath(&cardBrush, &path);

				// 头像仍然画在同样的位置（露出卡片顶部，且不影响边缘吸附计算）
				m_avatarRectF = avatarRect;
				m_avatar.Draw(g, avatarRect);
			}
			else if (m_isAnimating && m_animationIsCollapsing)
			{
				// 收起动画：背景卡片向右收缩，但保持可见
				// 文字逐渐透明消失，头像始终可见

				// 绘制背景卡片（保持完全不透明，但宽度递减）
				Gdiplus::RectF cardRect(10.0f, 20.0f, (float)w - 20.0f, (float)h - 30.0f);
				Gdiplus::SolidBrush cardBrush(Gdiplus::Color(255, 40, 40, 40));

				Gdiplus::GraphicsPath path;
				float radius = 12.0f;
				float d = radius * 2.0f;
				path.AddArc(cardRect.X, cardRect.Y, d, d, 180.0f, 90.0f);
				path.AddArc(cardRect.GetRight() - d, cardRect.Y, d, d, 270.0f, 90.0f);
				path.AddArc(cardRect.GetRight() - d, cardRect.GetBottom() - d, d, d, 0.0f, 90.0f);
				path.AddArc(cardRect.X, cardRect.GetBottom() - d, d, d, 90.0f, 90.0f);
				path.CloseFigure();
				g.FillPath(&cardBrush, &path);

				// 绘制头像（始终可见）
				// X 从 8 改为 18：向右移动 10px，让卡片左边露出一点在头像左侧。
				Gdiplus::RectF avatarRect(18.0f, 0.0f, 84.0f, 84.0f);
				m_avatarRectF = avatarRect;
				m_avatar.Draw(g, avatarRect);

				// 绘制歌曲信息（逐渐透明消失）
				float textAlphaProgress = m_animationProgress;  // 文字随着动画进度逐渐消失
				
				if (!m_trackTitle.empty() || !m_trackArtist.empty())
				{
					float textX = avatarRect.GetRight() + 15.0f;
					float textWidth = cardRect.GetRight() - textX - 10.0f;
					float cardCenterY = cardRect.Y + (cardRect.Height / 2.0f);

					if (textWidth > 0)
					{
						Gdiplus::StringFormat stringFormat;
						stringFormat.SetAlignment(Gdiplus::StringAlignmentNear);
						stringFormat.SetLineAlignment(Gdiplus::StringAlignmentCenter);
						stringFormat.SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);

						Gdiplus::Font titleFont(L"Microsoft YaHei UI", 11.0f, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
						Gdiplus::Font artistFont(L"Microsoft YaHei UI", 9.0f, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
						
						// 文字逐渐消失（透明度递增）
						int titleAlpha = (int)(255.0f * (1.0f - textAlphaProgress));
						int artistAlpha = (int)(200.0f * (1.0f - textAlphaProgress));
						
						Gdiplus::SolidBrush titleBrush(Gdiplus::Color(titleAlpha, 255, 255, 255));
						Gdiplus::SolidBrush artistBrush(Gdiplus::Color(artistAlpha, 200, 200, 200));

						bool hasBoth = !m_trackTitle.empty() && !m_trackArtist.empty();
						if (hasBoth)
						{
							float titleH = titleFont.GetHeight(&g);
							float artistH = artistFont.GetHeight(&g);
							float totalH = titleH + artistH - 2.0f;
							float startY = cardCenterY - totalH / 2.0f;

							g.DrawString(m_trackTitle.c_str(), -1, &titleFont,
								Gdiplus::PointF(textX, startY), &titleBrush);
							g.DrawString(m_trackArtist.c_str(), -1, &artistFont,
								Gdiplus::PointF(textX, startY + titleH - 2.0f), &artistBrush);
						}
						else
						{
							const std::wstring& single = m_trackTitle.empty() ? m_trackArtist : m_trackTitle;
							Gdiplus::Font& font = m_trackTitle.empty() ? artistFont : titleFont;
							Gdiplus::SolidBrush& brush = m_trackTitle.empty() ? artistBrush : titleBrush;

							float h = font.GetHeight(&g);
							float startY = cardCenterY - h / 2.0f;
							g.DrawString(single.c_str(), -1, &font, Gdiplus::PointF(textX, startY), &brush);
						}
					}
				}
			}
			else if (m_isAnimating && !m_animationIsCollapsing)
			{
				// 展开动画：背景卡片保持可见，文字逐渐出现
				
				// 绘制背景卡片（始终可见，完全不透明）
				Gdiplus::RectF cardRect(10.0f, 20.0f, (float)w - 20.0f, (float)h - 30.0f);
				Gdiplus::SolidBrush cardBrush(Gdiplus::Color(255, 40, 40, 40));

				Gdiplus::GraphicsPath path;
				float radius = 12.0f;
				float d = radius * 2.0f;
				path.AddArc(cardRect.X, cardRect.Y, d, d, 180.0f, 90.0f);
				path.AddArc(cardRect.GetRight() - d, cardRect.Y, d, d, 270.0f, 90.0f);
				path.AddArc(cardRect.GetRight() - d, cardRect.GetBottom() - d, d, d, 0.0f, 90.0f);
				path.AddArc(cardRect.X, cardRect.GetBottom() - d, d, d, 90.0f, 90.0f);
				path.CloseFigure();
				g.FillPath(&cardBrush, &path);

				// 绘制头像（始终可见）
				// X 从 8 改为 18：向右移动 10px，让卡片左边露出一点在头像左侧。
				Gdiplus::RectF avatarRect(18.0f, 0.0f, 84.0f, 84.0f);
				m_avatarRectF = avatarRect;
				m_avatar.Draw(g, avatarRect);

				// 绘制歌曲信息（逐渐显示）
				float textAlphaProgress = m_animationProgress;
				
				if (!m_trackTitle.empty() || !m_trackArtist.empty())
				{
					float textX = avatarRect.GetRight() + 15.0f;
					float textWidth = cardRect.GetRight() - textX - 10.0f;
					float cardCenterY = cardRect.Y + (cardRect.Height / 2.0f);

					if (textWidth > 0)
					{
						Gdiplus::StringFormat stringFormat;
						stringFormat.SetAlignment(Gdiplus::StringAlignmentNear);
						stringFormat.SetLineAlignment(Gdiplus::StringAlignmentCenter);
						stringFormat.SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);

						Gdiplus::Font titleFont(L"Microsoft YaHei UI", 11.0f, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
						Gdiplus::Font artistFont(L"Microsoft YaHei UI", 9.0f, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
						
						// 文字逐渐显示（透明度递增）
						int titleAlpha = (int)(255.0f * textAlphaProgress);
						int artistAlpha = (int)(200.0f * textAlphaProgress);
						
						Gdiplus::SolidBrush titleBrush(Gdiplus::Color(titleAlpha, 255, 255, 255));
						Gdiplus::SolidBrush artistBrush(Gdiplus::Color(artistAlpha, 200, 200, 200));

						bool hasBoth = !m_trackTitle.empty() && !m_trackArtist.empty();
						if (hasBoth)
						{
							float titleH = titleFont.GetHeight(&g);
							float artistH = artistFont.GetHeight(&g);
							float totalH = titleH + artistH - 2.0f;
							float startY = cardCenterY - totalH / 2.0f;

							g.DrawString(m_trackTitle.c_str(), -1, &titleFont,
								Gdiplus::PointF(textX, startY), &titleBrush);
							g.DrawString(m_trackArtist.c_str(), -1, &artistFont,
								Gdiplus::PointF(textX, startY + titleH - 2.0f), &artistBrush);
						}
						else
						{
							const std::wstring& single = m_trackTitle.empty() ? m_trackArtist : m_trackTitle;
							Gdiplus::Font& font = m_trackTitle.empty() ? artistFont : titleFont;
							Gdiplus::SolidBrush& brush = m_trackTitle.empty() ? artistBrush : titleBrush;

							float h = font.GetHeight(&g);
							float startY = cardCenterY - h / 2.0f;
							g.DrawString(single.c_str(), -1, &font, Gdiplus::PointF(textX, startY), &brush);
						}
					}
				}
			}
			else if (m_isAnimating && m_animationIsCollapsing)
			{
				// 收起动画：背景卡片向右收缩，但保持可见
				// 文字逐渐透明消失，头像始终可见

				// 绘制背景卡片（保持完全不透明，但宽度递减）
				Gdiplus::RectF cardRect(10.0f, 20.0f, (float)w - 20.0f, (float)h - 30.0f);
				Gdiplus::SolidBrush cardBrush(Gdiplus::Color(255, 40, 40, 40));

				Gdiplus::GraphicsPath path;
				float radius = 12.0f;
				float d = radius * 2.0f;
				path.AddArc(cardRect.X, cardRect.Y, d, d, 180.0f, 90.0f);
				path.AddArc(cardRect.GetRight() - d, cardRect.Y, d, d, 270.0f, 90.0f);
				path.AddArc(cardRect.GetRight() - d, cardRect.GetBottom() - d, d, d, 0.0f, 90.0f);
				path.AddArc(cardRect.X, cardRect.GetBottom() - d, d, d, 90.0f, 90.0f);
				path.CloseFigure();
				g.FillPath(&cardBrush, &path);

				// 绘制头像（始终可见）
				// X 从 8 改为 18：向右移动 10px，让卡片左边露出一点在头像左侧。
				Gdiplus::RectF avatarRect(18.0f, 0.0f, 84.0f, 84.0f);
				m_avatarRectF = avatarRect;
				m_avatar.Draw(g, avatarRect);

				// 绘制歌曲信息（逐渐透明消失）
				float textAlphaProgress = m_animationProgress;  // 文字随着动画进度逐渐消失
				
				if (!m_trackTitle.empty() || !m_trackArtist.empty())
				{
					float textX = avatarRect.GetRight() + 15.0f;
					float textWidth = cardRect.GetRight() - textX - 10.0f;
					float cardCenterY = cardRect.Y + (cardRect.Height / 2.0f);

					if (textWidth > 0)
					{
						Gdiplus::StringFormat stringFormat;
						stringFormat.SetAlignment(Gdiplus::StringAlignmentNear);
						stringFormat.SetLineAlignment(Gdiplus::StringAlignmentCenter);
						stringFormat.SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);

						Gdiplus::Font titleFont(L"Microsoft YaHei UI", 11.0f, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
						Gdiplus::Font artistFont(L"Microsoft YaHei UI", 9.0f, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
						
						// 文字逐渐消失（透明度递增）
						int titleAlpha = (int)(255.0f * (1.0f - textAlphaProgress));
						int artistAlpha = (int)(200.0f * (1.0f - textAlphaProgress));
						
						Gdiplus::SolidBrush titleBrush(Gdiplus::Color(titleAlpha, 255, 255, 255));
						Gdiplus::SolidBrush artistBrush(Gdiplus::Color(artistAlpha, 200, 200, 200));

						bool hasBoth = !m_trackTitle.empty() && !m_trackArtist.empty();
						if (hasBoth)
						{
							float titleH = titleFont.GetHeight(&g);
							float artistH = artistFont.GetHeight(&g);
							float totalH = titleH + artistH - 2.0f;
							float startY = cardCenterY - totalH / 2.0f;

							g.DrawString(m_trackTitle.c_str(), -1, &titleFont,
								Gdiplus::PointF(textX, startY), &titleBrush);
							g.DrawString(m_trackArtist.c_str(), -1, &artistFont,
								Gdiplus::PointF(textX, startY + titleH - 2.0f), &artistBrush);
						}
						else
						{
							const std::wstring& single = m_trackTitle.empty() ? m_trackArtist : m_trackTitle;
							Gdiplus::Font& font = m_trackTitle.empty() ? artistFont : titleFont;
							Gdiplus::SolidBrush& brush = m_trackTitle.empty() ? artistBrush : titleBrush;

							float h = font.GetHeight(&g);
							float startY = cardCenterY - h / 2.0f;
							g.DrawString(single.c_str(), -1, &font, Gdiplus::PointF(textX, startY), &brush);
						}
					}
				}
			}
			else if (m_isAnimating && !m_animationIsCollapsing)
			{
				// 展开动画：背景卡片保持可见，文字逐渐出现
				
				// 绘制背景卡片（始终可见，完全不透明）
				Gdiplus::RectF cardRect(10.0f, 20.0f, (float)w - 20.0f, (float)h - 30.0f);
				Gdiplus::SolidBrush cardBrush(Gdiplus::Color(255, 40, 40, 40));

				Gdiplus::GraphicsPath path;
				float radius = 12.0f;
				float d = radius * 2.0f;
				path.AddArc(cardRect.X, cardRect.Y, d, d, 180.0f, 90.0f);
				path.AddArc(cardRect.GetRight() - d, cardRect.Y, d, d, 270.0f, 90.0f);
				path.AddArc(cardRect.GetRight() - d, cardRect.GetBottom() - d, d, d, 0.0f, 90.0f);
				path.AddArc(cardRect.X, cardRect.GetBottom() - d, d, d, 90.0f, 90.0f);
				path.CloseFigure();
				g.FillPath(&cardBrush, &path);

				// 绘制头像（始终可见）
				// X 从 8 改为 18：向右移动 10px，让卡片左边露出一点在头像左侧。
				Gdiplus::RectF avatarRect(18.0f, 0.0f, 84.0f, 84.0f);
				m_avatarRectF = avatarRect;
				m_avatar.Draw(g, avatarRect);

				// 绘制歌曲信息（逐渐显示）
				float textAlphaProgress = m_animationProgress;
				
				if (!m_trackTitle.empty() || !m_trackArtist.empty())
				{
					float textX = avatarRect.GetRight() + 15.0f;
					float textWidth = cardRect.GetRight() - textX - 10.0f;
					float cardCenterY = cardRect.Y + (cardRect.Height / 2.0f);

					if (textWidth > 0)
					{
						Gdiplus::StringFormat stringFormat;
						stringFormat.SetAlignment(Gdiplus::StringAlignmentNear);
						stringFormat.SetLineAlignment(Gdiplus::StringAlignmentCenter);
						stringFormat.SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);

						Gdiplus::Font titleFont(L"Microsoft YaHei UI", 11.0f, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
						Gdiplus::Font artistFont(L"Microsoft YaHei UI", 9.0f, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
						
						// 文字逐渐显示（透明度递增）
						int titleAlpha = (int)(255.0f * textAlphaProgress);
						int artistAlpha = (int)(200.0f * textAlphaProgress);
						
						Gdiplus::SolidBrush titleBrush(Gdiplus::Color(titleAlpha, 255, 255, 255));
						Gdiplus::SolidBrush artistBrush(Gdiplus::Color(artistAlpha, 200, 200, 200));

						bool hasBoth = !m_trackTitle.empty() && !m_trackArtist.empty();
						if (hasBoth)
						{
							float titleH = titleFont.GetHeight(&g);
							float artistH = artistFont.GetHeight(&g);
							float totalH = titleH + artistH - 2.0f;
							float startY = cardCenterY - totalH / 2.0f;

							g.DrawString(m_trackTitle.c_str(), -1, &titleFont,
								Gdiplus::PointF(textX, startY), &titleBrush);
							g.DrawString(m_trackArtist.c_str(), -1, &artistFont,
								Gdiplus::PointF(textX, startY + titleH - 2.0f), &artistBrush);
						}
						else
						{
							const std::wstring& single = m_trackTitle.empty() ? m_trackArtist : m_trackTitle;
							Gdiplus::Font& font = m_trackTitle.empty() ? artistFont : titleFont;
							Gdiplus::SolidBrush& brush = m_trackTitle.empty() ? artistBrush : titleBrush;

							float h = font.GetHeight(&g);
							float startY = cardCenterY - h / 2.0f;
							g.DrawString(single.c_str(), -1, &font, Gdiplus::PointF(textX, startY), &brush);
						}
					}
				}
			}
			else
			{
				// 正常状态：绘制背景卡片。
				Gdiplus::RectF cardRect(10.0f, 20.0f, (float)w - 20.0f, (float)h - 30.0f);
				Gdiplus::SolidBrush cardBrush(Gdiplus::Color(255, 40, 40, 40));

				Gdiplus::GraphicsPath path;
				float radius = 12.0f;
				float d = radius * 2.0f;
				path.AddArc(cardRect.X, cardRect.Y, d, d, 180.0f, 90.0f);
				path.AddArc(cardRect.GetRight() - d, cardRect.Y, d, d, 270.0f, 90.0f);
				path.AddArc(cardRect.GetRight() - d, cardRect.GetBottom() - d, d, d, 0.0f, 90.0f);
				path.AddArc(cardRect.X, cardRect.GetBottom() - d, d, d, 90.0f, 90.0f);
				path.CloseFigure();
				g.FillPath(&cardBrush, &path);

				// 绘制圆形头像和进度环。
				// X 从 8 改为 18：向右移动 10px，让卡片左边露出一点在头像左侧。
				Gdiplus::RectF avatarRect(18.0f, 0.0f, 84.0f, 84.0f);
				m_avatarRectF = avatarRect;
				m_avatar.Draw(g, avatarRect);

				// 绘制歌曲信息。
				Gdiplus::RectF textCardRect(10.0f, 20.0f, (float)w - 20.0f, (float)h - 30.0f);
				DrawTrackInfoGdiplus(g, textCardRect, avatarRect);
			}
		}

		// 使用 UpdateLayeredWindow 推送位图到系统
		{
			HDC hScreenDC = GetDC(nullptr);
			HDC hMemDC = CreateCompatibleDC(hScreenDC);
			HBITMAP hOldBitmap = (HBITMAP)SelectObject(hMemDC, m_dibSection);

			POINT ptSrc = { 0, 0 };
			SIZE szWindow = { w, h };

			BLENDFUNCTION blend = {};
			blend.BlendOp = AC_SRC_OVER;
			blend.BlendFlags = 0;
			blend.AlphaFormat = AC_SRC_ALPHA;
			blend.SourceConstantAlpha = 255;

			UpdateLayeredWindow(m_hwnd, hScreenDC, nullptr, &szWindow, hMemDC, &ptSrc, 0, &blend, ULW_ALPHA);

			SelectObject(hMemDC, hOldBitmap);
			DeleteDC(hMemDC);
			ReleaseDC(nullptr, hScreenDC);
		}
	}

	void MainWindow::DrawTrackInfoGdiplus(Gdiplus::Graphics& g, const Gdiplus::RectF& cardRect, const Gdiplus::RectF& avatarRect)
	{
		// 在卡片右侧、头像旁边绘制歌名和歌手信息
		if (m_trackTitle.empty() && m_trackArtist.empty())
			return;

		float textX = avatarRect.GetRight() + 15.0f;
		float textWidth = cardRect.GetRight() - textX - 10.0f;
		float cardCenterY = cardRect.Y + (cardRect.Height / 2.0f);

		if (textWidth <= 0)
			return;

		Gdiplus::StringFormat stringFormat;
		stringFormat.SetAlignment(Gdiplus::StringAlignmentNear);
		stringFormat.SetLineAlignment(Gdiplus::StringAlignmentCenter);
		stringFormat.SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);

		Gdiplus::Font titleFont(L"Microsoft YaHei UI", 11.0f, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
		Gdiplus::Font artistFont(L"Microsoft YaHei UI", 9.0f, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
		Gdiplus::SolidBrush titleBrush(Gdiplus::Color(255, 255, 255));
		Gdiplus::SolidBrush artistBrush(Gdiplus::Color(200, 200, 200));

		bool hasBoth = !m_trackTitle.empty() && !m_trackArtist.empty();
		if (hasBoth)
		{
			float titleH = titleFont.GetHeight(&g);
			float artistH = artistFont.GetHeight(&g);
			float totalH = titleH + artistH - 2.0f;
			float startY = cardCenterY - totalH / 2.0f;

			g.DrawString(m_trackTitle.c_str(), -1, &titleFont,
				Gdiplus::PointF(textX, startY), &titleBrush);
			g.DrawString(m_trackArtist.c_str(), -1, &artistFont,
				Gdiplus::PointF(textX, startY + titleH - 2.0f), &artistBrush);
		}
		else
		{
			const std::wstring& single = m_trackTitle.empty() ? m_trackArtist : m_trackTitle;
			Gdiplus::Font& font = m_trackTitle.empty() ? artistFont : titleFont;
			Gdiplus::SolidBrush& brush = m_trackTitle.empty() ? artistBrush : titleBrush;

			float h = font.GetHeight(&g);
			float startY = cardCenterY - h / 2.0f;
			g.DrawString(single.c_str(), -1, &font, Gdiplus::PointF(textX, startY), &brush);
		}
	}

	void MainWindow::RestoreWindow()
	{
		if (!m_isCollapsed || m_isAnimating)
			return;

		// 如果是右侧收起，使用展开动画
		if (m_collapsedEdge == CollapseEdge::Right)
		{
			StartExpandAnimation();
		}
		else
		{
			// 其他方向快速展开
			m_isCollapsed = false;
			m_collapsedEdge = CollapseEdge::None;
			SetWindowPos(m_hwnd, nullptr, m_savedWindowX, m_savedWindowY,
				m_savedWindowWidth, m_savedWindowHeight, SWP_NOZORDER | SWP_NOACTIVATE);
			Composite();
		}
	}

	void MainWindow::SetCoverImage(const std::wstring& path)
	{
		m_avatar.LoadImageFromFile(path);
		Composite();
	}

	void MainWindow::SetPlayProgress(float progress01)
	{
		m_avatar.SetProgress(progress01);
		Composite();
	}

	void MainWindow::SetAvatarSkin(const CircularAvatar::Skin& skin)
	{
		m_avatar.SetSkin(skin);
		Composite();
	}

	void MainWindow::SetTrackInfo(const std::wstring& title, const std::wstring& artist)
	{
		m_trackTitle = title;
		m_trackArtist = artist;
		Composite();
	}

}
