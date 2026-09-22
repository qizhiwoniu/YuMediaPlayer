#include "MainWindow.h"  
#include <dwmapi.h>
#include <cstdint>
#include <memory>
#include <windowsx.h>
#include <algorithm>
#include "Core/Theme.h"
#include "UI/WindowGUI.h"
#include "UI/miniWindowGUI.h"
#include "NotifyIcon/TrayIcon.h"

#pragma comment(lib, "dwmapi.lib")

namespace YuMediaPlayer
{
	PlayButtonInfo g_playButtonInfo = {};
	std::wstring g_currentMp3Path = L"";

	MainWindow::MainWindow()
		: m_hwnd(nullptr)
		, m_theme()
		, m_trayIcon(GetModuleHandle(nullptr), L"余余音乐播放器")
		, m_windowGUI(nullptr)
	{}
	MainWindow::~MainWindow()
	{
		CleanupAudioPlayer();  // <<<新增
	}
	bool MainWindow::Initialize(const wchar_t* title, int width, int height)
	{
		if (!InitWindow(title, width, height))
			return false;
		
		if (!InitializeAudioPlayer())  // <<<新增
		{
			OutputDebugStringW(L"Warning: Audio player init failed\n");
		}

		m_windowGUI = std::make_unique<YuMediaPlayer::WindowGUI>();
		if (!m_windowGUI->InitWindow(title, width, height))
		{
			return false;
		}

		RECT rc;
		GetClientRect(m_hwnd, &rc);
		m_trayIcon.Create(m_hwnd);
		m_trayIcon.ShowBalloon(L"YuMediaPlayer starting", L"starting...");
		m_trayIcon.SetWindowGUI(m_windowGUI.get());
		int clientW = rc.right - rc.left;
		int clientH = rc.bottom - rc.top;

		Composite();
		m_windowGUI->HideWindowGUI();

		m_edgeHoverTimer = SetTimer(m_hwnd, 1001, 100, nullptr);
		if (m_edgeHoverTimer == 0)
			return false;
		
		UpdateWindow(m_hwnd);
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

		// 4. 显示窗口
		ShowWindow(m_hwnd, SW_SHOW);

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
			int cmd = ShowMiniPlayerContextMenu(hwnd, pt, m_windowGUI.get());  
			if (cmd != 0)
			{
				// ✅ 添加第三个参数：m_windowGUI.get()
				HandleMiniPlayerContextMenuCommand(hwnd, cmd, m_windowGUI.get());
			}
			return 0;
		}
		case WM_NCRBUTTONUP:
		{
			if (wParam == HTCAPTION)
			{
				POINT pt;
				pt.x = GET_X_LPARAM(lParam);
				pt.y = GET_Y_LPARAM(lParam);
				int cmd = ShowMiniPlayerContextMenu(hwnd, pt, m_windowGUI.get());  
				if (cmd != 0)
				{
					// ✅ 添加第三个参数：m_windowGUI.get()
					HandleMiniPlayerContextMenuCommand(hwnd, cmd, m_windowGUI.get());
				}
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

			int clientW = rc.right - rc.left;
			int clientH = rc.bottom - rc.top;

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
			POINT pt;
			pt.x = GET_X_LPARAM(lParam);
			pt.y = GET_Y_LPARAM(lParam);

			if (IsPointInPlayButton(pt, g_playButtonInfo))
			{
				std::wstring mp3 = L"Assets\\song\\local\\周杰伦-七里香.mp3";
				HandlePlayButtonClick(hwnd, mp3);  // 播放或暂停
				TogglePlayPause(hwnd);
				return 0;
			}
			// ✅ 检测上一曲按钮点击
			if (IsPointInRect(pt, m_playbackButtonsInfo.previous.rect))
			{
				HandlePreviousButtonClick(hwnd);
				return 0;
			}

			// ✅ 检测下一曲按钮点击
			if (IsPointInRect(pt, m_playbackButtonsInfo.next.rect))
			{
				HandleNextButtonClick(hwnd);
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

			
			pt.x = GET_X_LPARAM(lParam);
			pt.y = GET_Y_LPARAM(lParam);

			// ✅ 检测播放按钮悬停状态
			bool wasPlayHovered = g_playButtonInfo.isHovered;
			g_playButtonInfo.isHovered = IsPointInPlayButton(pt, g_playButtonInfo);

			// ✅ 检测上一曲按钮悬停状态
			bool wasPreviousHovered = m_playbackButtonsInfo.previous.hovered;
			m_playbackButtonsInfo.previous.hovered = IsPointInRect(pt, m_playbackButtonsInfo.previous.rect);

			// ✅ 检测下一曲按钮悬停状态
			bool wasNextHovered = m_playbackButtonsInfo.next.hovered;
			m_playbackButtonsInfo.next.hovered = IsPointInRect(pt, m_playbackButtonsInfo.next.rect);
			// ===== 控制按钮 =====
			bool wasCloseHovered = m_playbackButtonsInfo.close.hovered;
			m_playbackButtonsInfo.close.hovered = IsPointInRect(pt, m_playbackButtonsInfo.close.rect);

			bool wasMiniHovered = m_playbackButtonsInfo.mini.hovered;
			m_playbackButtonsInfo.mini.hovered = IsPointInRect(pt, m_playbackButtonsInfo.mini.rect);

			bool wasHeartHovered = m_playbackButtonsInfo.heart.hovered;
			m_playbackButtonsInfo.heart.hovered = IsPointInRect(pt, m_playbackButtonsInfo.heart.rect);

			bool wasSoundHovered = m_playbackButtonsInfo.sound.hovered;
			m_playbackButtonsInfo.sound.hovered = IsPointInRect(pt, m_playbackButtonsInfo.sound.rect);

			bool wasListHovered = m_playbackButtonsInfo.list.hovered;
			m_playbackButtonsInfo.list.hovered = IsPointInRect(pt, m_playbackButtonsInfo.list.rect);
			// 如果任何按钮的悬停状态改变，重新绘制
			if (wasPlayHovered != g_playButtonInfo.isHovered ||
				wasPreviousHovered != m_playbackButtonsInfo.previous.hovered ||
				wasNextHovered != m_playbackButtonsInfo.next.hovered ||
				wasCloseHovered != m_playbackButtonsInfo.close.hovered ||
				wasMiniHovered != m_playbackButtonsInfo.mini.hovered ||
				wasHeartHovered != m_playbackButtonsInfo.heart.hovered ||
				wasSoundHovered != m_playbackButtonsInfo.sound.hovered ||
				wasListHovered != m_playbackButtonsInfo.list.hovered)
			{
				Composite();
			}

			return 0;
		}
		break;

			//// 如果悬停状态改变，重新绘制按钮区域
			//if (wasHovered != g_playButtonInfo.isHovered)
			//{
			//	InvalidateRect(hwnd, &g_playButtonInfo.buttonRect, FALSE);
			//}
			//g_playButtonInfo.isHovered = IsPointInPlayButton(pt, g_playButtonInfo);
			//if (g_playButtonInfo.isHovered)
			//	InvalidateRect(hwnd, &g_playButtonInfo.buttonRect, FALSE);
			//
			//return 0;
		
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
		/*	bool middle = !left && !right && !top && !bottom;
			bool middleTop = !left && !right && top;
			bool middleBottom = !left && !right && bottom;
			bool center = pt.x >= wr.left + (wr.right - wr.left) / 2 && pt.y >= wr.top + (wr.bottom - wr.top) / 2;
			*/
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

			mmi->ptMaxPosition.x = mi.rcMonitor.left - mi.rcMonitor.left;
			mmi->ptMaxPosition.y = mi.rcMonitor.top - mi.rcMonitor.top;

			mmi->ptMaxSize.x = mi.rcMonitor.right - mi.rcMonitor.left;
			mmi->ptMaxSize.y = mi.rcMonitor.bottom - mi.rcMonitor.top;

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

		//default:
			//return DefWindowProc(hwnd, msg, wParam, lParam);
		}

		//return DefWindowProc(hwnd, msg, wParam, lParam);
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
				int finalX = mi.rcMonitor.right - 92 - 10;
				
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
			int targetX = mi.rcMonitor.right - 92 - 10;
			
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
			int collapsedX = mi.rcMonitor.right - 92 - 10;
			
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
				pt.x <= mi.rcMonitor.left + HOVER_THRESHOLD &&
				pt.y >= wr.top &&
				pt.y <= wr.bottom;
			break;

		case CollapseEdge::Right:
			shouldRestore =
				pt.x >= mi.rcMonitor.right - HOVER_THRESHOLD &&
				pt.y >= wr.top &&
				pt.y <= wr.bottom;
			break;

		case CollapseEdge::Top:
			shouldRestore =
				pt.y <= mi.rcMonitor.top + HOVER_THRESHOLD &&
				pt.x >= wr.left &&
				pt.x <= wr.right;
			break;

		case CollapseEdge::Bottom:
			shouldRestore =
				pt.y >= mi.rcMonitor.bottom - HOVER_THRESHOLD &&
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
		const bool nearLeft = wr.left <= mi.rcMonitor.left + EDGE_THRESHOLD;
		const bool nearRight = wr.right >= mi.rcMonitor.right - EDGE_THRESHOLD;
		const bool nearTop = wr.top <= mi.rcMonitor.top + EDGE_THRESHOLD;
		const bool nearBottom = wr.bottom >= mi.rcMonitor.bottom - EDGE_THRESHOLD;

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
				newX = mi.rcMonitor.left;
				newY = wr.top;
			}
			else if (nearTop)
			{
				m_collapsedEdge = CollapseEdge::Top;
				newX = wr.left;
				newY = mi.rcMonitor.top;
			}
			else if (nearBottom)
			{
				m_collapsedEdge = CollapseEdge::Bottom;
				newX = wr.left;
				newY = mi.rcMonitor.bottom - COLLAPSED_SIZE;
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
				// 绘制播放按钮。
				Gdiplus::RectF vRect(10.0f, 20.0f, (float)w - 20.0f, (float)h - 30.0f);
				DrawPlayButtonGdiplus(g, vRect, false, false);
				DrawPlaybackButtonsGdiplus(g, vRect, avatarRect);
				//DrawPreviousButtonGdiplus(g, vRect, false);
				//DrawNextButtonGdiplus(g, vRect, false);
				
			    
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
	
	void MainWindow::DrawPlayButtonGdiplus(Gdiplus::Graphics& g, const Gdiplus::RectF& vRect, bool isPlaying, bool hovered)
	{
		// 使用传入的绘图区尺寸计算按钮位置
		int clientW = static_cast<int>(vRect.Width);
		int clientH = static_cast<int>(vRect.Height);

		// 播放按钮大小
		int buttonSize = 30;
		// ========================================
		// 布局：
		// 左边 10px
		// 封面 84px
		// 封面右侧 10px
		// 剩余区域进行居中
		// 最后向左偏移 20px
		// ========================================

		const int leftMargin = 10;
		const int avatarSize = 84;
		const int avatarRightSpacing = 10;

		const int reservedWidth =
			leftMargin +
			avatarSize +
			avatarRightSpacing;

		const int availableWidth =
			clientW - reservedWidth;

		// 剩余区域居中 + 向左偏移 20px
		int buttonX =
			reservedWidth +
			(availableWidth - buttonSize) / 2 - 20;
		// 垂直居中
		int buttonY =
			static_cast<int>(vRect.Y) +
			(clientH - buttonSize) / 2;
		// 计算按钮区域
		g_playButtonInfo =
			CalculatePlayButtonRect(buttonX, buttonY, buttonSize);

		// 获取当前播放状态
		AudioPlayer* audioPlayer = GetAudioPlayer();

		if (audioPlayer)
		{
			g_playButtonInfo.isPlaying =
				(audioPlayer->GetPlaybackState() == PlaybackState::Playing);
		}
		else
		{
			g_playButtonInfo.isPlaying = false;
		}

		// ============================================================
		// 重点：
		// 这里不能再 BeginPaint()
		// 也不能使用 DrawPlayButton(hdc, ...)
		//
		// 因为 Composite() 当前正在使用 GDI+ Graphics g
		// 绘制到 m_dibSection。
		//
		// 所以播放按钮也必须直接画到这个 Graphics 上。
		// ============================================================

		const float x = static_cast<float>(buttonX);
		const float y = static_cast<float>(buttonY);
		const float size = static_cast<float>(buttonSize);

		// 播放按钮背景圆
		Gdiplus::SolidBrush buttonBrush(
			Gdiplus::Color(230, 60, 60, 60)
		);

		g.FillEllipse(
			&buttonBrush,
			x,
			y,
			size,
			size
		);

		// 按钮边框
		Gdiplus::Pen buttonPen(
			Gdiplus::Color(255, 180, 180, 180),
			1.5f
		);

		g.DrawEllipse(
			&buttonPen,
			x + 0.75f,
			y + 0.75f,
			size - 1.5f,
			size - 1.5f
		);

		// ============================================================
		// 播放 / 暂停图标
		// ============================================================

		Gdiplus::SolidBrush iconBrush(
			Gdiplus::Color(255, 255, 255, 255)
		);

		if (g_playButtonInfo.isPlaying)
		{
			// -------------------------
			// 暂停图标
			// -------------------------

			float pauseWidth = 5.0f;
			float pauseHeight = 16.0f;
			float gap = 5.0f;

			float totalWidth =
				pauseWidth * 2.0f + gap;

			float startX =
				x + (size - totalWidth) / 2.0f;

			float startY =
				y + (size - pauseHeight) / 2.0f;

			g.FillRectangle(
				&iconBrush,
				startX,
				startY,
				pauseWidth,
				pauseHeight
			);

			g.FillRectangle(
				&iconBrush,
				startX + pauseWidth + gap,
				startY,
				pauseWidth,
				pauseHeight
			);
		}
		else
		{
			// -------------------------
			// 播放图标
			// -------------------------

			Gdiplus::PointF points[3];

			float iconWidth = 10.0f;
			float iconHeight = 13.0f;

			float startX =
				x + (size - iconWidth) / 2.0f + 2.0f;

			float startY =
				y + (size - iconHeight) / 2.0f;

			points[0] = Gdiplus::PointF(
				startX,
				startY
			);

			points[1] = Gdiplus::PointF(
				startX,
				startY + iconHeight
			);

			points[2] = Gdiplus::PointF(
				startX + iconWidth,
				startY + iconHeight / 2.0f
			);

			g.FillPolygon(
				&iconBrush,
				points,
				3
			);
		}
	}
	void MainWindow::DrawPreviousButtonGdiplus(Gdiplus::Graphics& g,const Gdiplus::RectF& vRect,bool hovered)
	{
		const float x = vRect.X;
		const float y = vRect.Y;
		const float w = vRect.Width;
		const float h = vRect.Height;

		Gdiplus::SolidBrush iconBrush(
			hovered
			? Gdiplus::Color(255, 255, 255, 255)
			: Gdiplus::Color(235, 235, 235, 235)
		);

		// 图标尺寸
		const float iconWidth = 12.0f;
		const float iconHeight = 16.0f;
		const float lineWidth = 2.0f;
		const float gap = 3.0f;

		const float totalWidth =
			lineWidth + gap + iconWidth;

		const float startX =
			x + (w - totalWidth) / 2.0f;

		const float startY =
			y + (h - iconHeight) / 2.0f;

		// 左侧竖线
		g.FillRectangle(
			&iconBrush,
			startX,
			startY,
			lineWidth,
			iconHeight
		);

		// ◀ 三角形
		Gdiplus::PointF points[3];

		const float triangleX =
			startX + lineWidth + gap;

		points[0] = Gdiplus::PointF(
			triangleX + iconWidth,
			startY
		);

		points[1] = Gdiplus::PointF(
			triangleX + iconWidth,
			startY + iconHeight
		);

		points[2] = Gdiplus::PointF(
			triangleX,
			startY + iconHeight / 2.0f
		);

		g.FillPolygon(
			&iconBrush,
			points,
			3
		);
	}
	void MainWindow::DrawNextButtonGdiplus(Gdiplus::Graphics& g,const Gdiplus::RectF& vRect,bool hovered)
	{
		const float x = vRect.X;
		const float y = vRect.Y;
		const float w = vRect.Width;
		const float h = vRect.Height;

		Gdiplus::SolidBrush iconBrush(
			hovered
			? Gdiplus::Color(255, 255, 255, 255)
			: Gdiplus::Color(235, 235, 235, 235)
		);

		const float iconWidth = 12.0f;
		const float iconHeight = 16.0f;
		const float lineWidth = 2.0f;
		const float gap = 3.0f;

		const float totalWidth =
			iconWidth + gap + lineWidth;

		const float startX =
			x + (w - totalWidth) / 2.0f;

		const float startY =
			y + (h - iconHeight) / 2.0f;

		// ▶ 三角形
		Gdiplus::PointF points[3];

		points[0] = Gdiplus::PointF(
			startX,
			startY
		);

		points[1] = Gdiplus::PointF(
			startX,
			startY + iconHeight
		);

		points[2] = Gdiplus::PointF(
			startX + iconWidth,
			startY + iconHeight / 2.0f
		);

		g.FillPolygon(
			&iconBrush,
			points,
			3
		);

		// 右侧竖线
		const float lineX =
			startX + iconWidth + gap;

		g.FillRectangle(
			&iconBrush,
			lineX,
			startY,
			lineWidth,
			iconHeight
		);
	}
	void MainWindow::DrawPlaybackButtonsGdiplus(Gdiplus::Graphics& g,const Gdiplus::RectF& vRect,const Gdiplus::RectF& avatarRect)
	{
		const int clientW = static_cast<int>(vRect.Width);
		const int clientH = static_cast<int>(vRect.Height);

		// ============================
		// 左侧封面区域
		// ============================
		const int leftMargin = 10;
		const int avatarSize = 84;
		const int avatarSpacing = 10;

		const int reservedWidth = leftMargin + avatarSize + avatarSpacing;

		// ============================
		// 按钮尺寸
		// ============================
		const int buttonWidth = 40;   // 增大按钮宽度便于点击
		const int buttonHeight = 40;
		const int spacing = 20;       // 按钮之间的间距

		const int totalWidth = buttonWidth + spacing + buttonWidth;

		// ============================
		// 剩余区域
		// ============================
		const int availableWidth = clientW - reservedWidth;

		// 按钮组居中 + 向左偏移 40px
		const int startX = reservedWidth + (availableWidth - totalWidth) / 2 - 20;

		// 垂直居中
		const int centerY = static_cast<int>(vRect.Y) + (clientH / 2);

		// ============================
		// 上一曲按钮区域
		// ============================
		Gdiplus::RectF previousRect(
			static_cast<float>(startX),
			static_cast<float>(centerY - buttonHeight / 2),
			static_cast<float>(buttonWidth),
			static_cast<float>(buttonHeight)
		);

		// ============================
		// 下一曲按钮区域
		// ============================
		Gdiplus::RectF nextRect(
			static_cast<float>(startX + buttonWidth + spacing),
			static_cast<float>(centerY - buttonHeight / 2),
			static_cast<float>(buttonWidth),
			static_cast<float>(buttonHeight)
		);
		// 右上角按钮 (关闭、最小化) 
		const int topRightMargin = 0;
		//const int topMargin = 1;
		//const int rightMargin = 1;
		const int buttonSize = 20;
		const int buttonSpacing = 2;

		// 关闭按钮 (右上角)
		Gdiplus::RectF closeRect(
			static_cast<float>(clientW - topRightMargin - buttonSize),
			static_cast<float>(topRightMargin + buttonSize),
			static_cast<float>(buttonSize),
			static_cast<float>(buttonSize)
		);
		// 最小化按钮 (关闭按钮下面)
		Gdiplus::RectF miniRect(
			static_cast<float>(clientW - topRightMargin - buttonSize),
			static_cast<float>(topRightMargin + buttonSize + buttonSpacing),
			static_cast<float>(buttonSize),
			static_cast<float>(buttonSize)
		);
		// 右侧按钮 (音量、列表) 
		//const int rightMargin = 15;
		const float nextButtonRight = nextRect.GetRight(); // 假设下一曲按钮的位置
		// 音量按钮
		Gdiplus::RectF soundRect(
			static_cast<float>(clientW - topRightMargin - buttonSize * 3),
			static_cast<float>(clientH / 2 - buttonSize / 2),
			static_cast<float>(buttonSize),
			static_cast<float>(buttonSize)
		);
		// 列表按钮
		Gdiplus::RectF listRect(
			static_cast<float>(clientW - topRightMargin - buttonSize * 2),
			static_cast<float>(clientH / 2 - buttonSize / 2),
			static_cast<float>(buttonSize),
			static_cast<float>(buttonSize)
		);
		// 左侧按钮 (收藏) 
		Gdiplus::RectF heartRect(
			static_cast<float>(leftMargin),
			static_cast<float>(clientH / 2 - buttonSize / 2),
			static_cast<float>(buttonSize),
			static_cast<float>(buttonSize)
		);
		// 保存点击区域
		m_playbackButtonsInfo.heart.rect = heartRect;
		m_playbackButtonsInfo.sound.rect = soundRect;
		m_playbackButtonsInfo.list.rect = listRect;
		m_playbackButtonsInfo.close.rect = closeRect;
		m_playbackButtonsInfo.mini.rect = miniRect;
		m_playbackButtonsInfo.previous.rect = previousRect;
		m_playbackButtonsInfo.next.rect = nextRect;
		DrawHeartButtonGdiplus(g, heartRect, m_playbackButtonsInfo.heart.hovered);
		DrawSoundButtonGdiplus(g, soundRect, m_playbackButtonsInfo.sound.hovered);
		DrawListButtonGdiplus(g, listRect, m_playbackButtonsInfo.list.hovered);
		DrawCloseButtonGdiplus(g, closeRect, m_playbackButtonsInfo.close.hovered);
		DrawMiniButtonGdiplus(g, miniRect, m_playbackButtonsInfo.mini.hovered);
		DrawPreviousButtonGdiplus(g,previousRect,m_playbackButtonsInfo.previous.hovered);
		DrawNextButtonGdiplus(g,nextRect,m_playbackButtonsInfo.next.hovered);
	}

	
	void MainWindow::DrawCloseButtonGdiplus(Gdiplus::Graphics& g, const Gdiplus::RectF& vRect, bool hovered)
	{
		const float x = vRect.X;
		const float y = vRect.Y;
		const float w = vRect.Width;
		const float h = vRect.Height;

		// 根据悬停状态选择颜色
		Gdiplus::SolidBrush iconBrush(
			hovered
			? Gdiplus::Color(255, 255, 100, 100)  // 悬停时：红色
			: Gdiplus::Color(235, 235, 235, 235)  // 正常时：浅灰色
		);

		// 图标尺寸
		const float iconSize = 10.0f;
		const float lineWidth = 2.0f;

		const float startX = x + (w - iconSize);
		const float startY = y;
		const float endX = startX + iconSize;
		const float endY = startY + iconSize;

		// 绘制 X 符号（两条斜线）
		Gdiplus::Pen linePen(&iconBrush, lineWidth);
		linePen.SetLineCap(Gdiplus::LineCapRound, Gdiplus::LineCapRound, Gdiplus::DashCapRound);

		// 左上到右下的斜线
		g.DrawLine(&linePen, startX, startY, endX, endY);

		// 右上到左下的斜线
		g.DrawLine(&linePen, endX, startY, startX, endY);
		
	}
	void MainWindow::DrawMiniButtonGdiplus(Gdiplus::Graphics& g, const Gdiplus::RectF& vRect, bool hovered)
	{
		const float x = vRect.X;
		const float y = vRect.Y;
		const float w = vRect.Width;
		const float h = vRect.Height;

		// 根据悬停状态选择颜色
		Gdiplus::Color iconColor(
			hovered
			? Gdiplus::Color(255, 0, 100, 200)	  // 悬停时：蓝色
			: Gdiplus::Color(235, 235, 235, 235)  // 正常时：浅灰色
		);

		// 创建画笔用于绘制框线
		Gdiplus::Pen iconPen(iconColor, 2.0f);	// 笔宽 2.0f
		// 矩形框尺寸
		const int buttonspacing = 3;
		const float boxWidth = 12.0f;
		const float boxHeight = 10.0f;
		const float startX = x + (w - boxWidth);
		const float startY = y + (h - boxHeight) + buttonspacing;

		// 绘制矩形框
		g.DrawRectangle(
			&iconPen,
			startX,
			startY,
			boxWidth,
			boxHeight
		);
	}
	void MainWindow::DrawSoundButtonGdiplus(Gdiplus::Graphics& g, const Gdiplus::RectF& vRect, bool hovered)
	{
		const float x = vRect.X;
		const float y = vRect.Y;
		const float w = vRect.Width;
		const float h = vRect.Height;

		// 根据悬停状态选择颜色
		Gdiplus::SolidBrush iconBrush(
			hovered
			? Gdiplus::Color(255, 255, 255, 255)  // 悬停时：白色
			: Gdiplus::Color(235, 235, 235, 235)  // 正常时：浅灰色
		);

		// 喇叭图标尺寸
		const float speakerWidth = 6.0f;
		const float speakerHeight = 8.0f;
		const float waveWidth = 3.0f;

		const float centerX = x + w / 2.0f;
		const float centerY = y + h / 2.0f;

		// 绘制喇叭主体（三角形）
		Gdiplus::PointF speakerPoints[3];
		speakerPoints[0] = Gdiplus::PointF(centerX - speakerWidth / 2.0f, centerY - speakerHeight / 2.0f);
		speakerPoints[1] = Gdiplus::PointF(centerX - speakerWidth / 2.0f, centerY + speakerHeight / 2.0f);
		speakerPoints[2] = Gdiplus::PointF(centerX + speakerWidth / 2.0f, centerY);

		g.FillPolygon(&iconBrush, speakerPoints, 3);

		// 绘制音波纹（可选，表示有声音）
		Gdiplus::Pen wavePen(&iconBrush, 1.0f);
		const float waveRadius1 = speakerWidth / 2.0f + 2.0f;
		const float waveRadius2 = speakerWidth / 2.0f + 4.0f;

		// 第一道音波
		g.DrawArc(&wavePen,
			centerX + speakerWidth / 2.0f - waveRadius1,
			centerY - waveRadius1,
			waveRadius1 * 2.0f,
			waveRadius1 * 2.0f,
			-45.0f, 90.0f);

		// 第二道音波
		g.DrawArc(&wavePen,
			centerX + speakerWidth / 2.0f - waveRadius2,
			centerY - waveRadius2,
			waveRadius2 * 2.0f,
			waveRadius2 * 2.0f,
			-45.0f, 90.0f);

	}
	void MainWindow::DrawListButtonGdiplus(Gdiplus::Graphics& g, const Gdiplus::RectF& vRect, bool hovered)
	{
		const float x = vRect.X;
		const float y = vRect.Y;
		const float w = vRect.Width;
		const float h = vRect.Height;

		// 根据悬停状态选择颜色
		Gdiplus::SolidBrush iconBrush(
			hovered
			? Gdiplus::Color(255, 255, 255, 255)  // 悬停时：白色
			: Gdiplus::Color(235, 235, 235, 235)  // 正常时：浅灰色
		);

		// 列表图标尺寸
		const float lineWidth = 10.0f;
		const float lineHeight = 2.0f;
		const float spacing = 2.0f;

		const float startX = x + (w - lineWidth) / 2.0f;
		const float startY = y + (h - (lineHeight * 3 + spacing * 2)) / 2.0f;

		// 绘制三条横线（代表列表）
		for (int i = 0; i < 3; i++)
		{
			const float currentY = startY + i * (lineHeight + spacing);
			g.FillRectangle(&iconBrush, startX, currentY, lineWidth, lineHeight);
		}

		// 绘制音符装饰（可选）
		const float noteSize = 2.0f;
		const float noteX = startX + lineWidth + 2.0f;
		const float noteY = startY + lineHeight;

		Gdiplus::SolidBrush noteBrush(
			hovered
			? Gdiplus::Color(200, 255, 255, 255)
			: Gdiplus::Color(180, 235, 235, 235)
		);

		// 绘制小圆点作为装饰
		g.FillEllipse(&noteBrush, noteX, noteY, noteSize, noteSize);

	}
	void MainWindow::DrawHeartButtonGdiplus(Gdiplus::Graphics& g, const Gdiplus::RectF& vRect, bool hovered)
	{
		const float x = vRect.X;
		const float y = vRect.Y;
		const float w = vRect.Width;
		const float h = vRect.Height;

		// 根据悬停状态选择颜色（心形可以根据是否收藏改变颜色）
		Gdiplus::SolidBrush iconBrush(
			hovered
			? Gdiplus::Color(255, 255, 100, 100)  // 悬停时：红色
			: Gdiplus::Color(235, 235, 235, 235)  // 正常时：浅灰色
		);

		// 心形图标尺寸
		const float heartSize = 8.0f;
		const float centerX = x + w / 2.0f;
		const float centerY = y + h / 2.0f;

		// 绘制心形（使用多边形近似）
		// 心形的上半部分（两个圆角方块）和下半部分（三角形）
		Gdiplus::PointF heartPoints[10];

		// 左上圆弧对应的点
		heartPoints[0] = Gdiplus::PointF(centerX - heartSize / 2.0f, centerY - heartSize / 4.0f);
		heartPoints[1] = Gdiplus::PointF(centerX - heartSize / 2.0f - 1.0f, centerY - heartSize / 2.0f);
		heartPoints[2] = Gdiplus::PointF(centerX - heartSize / 4.0f, centerY - heartSize / 2.0f - 1.0f);

		// 右上圆弧对应的点
		heartPoints[3] = Gdiplus::PointF(centerX + heartSize / 4.0f, centerY - heartSize / 2.0f - 1.0f);
		heartPoints[4] = Gdiplus::PointF(centerX + heartSize / 2.0f + 1.0f, centerY - heartSize / 2.0f);
		heartPoints[5] = Gdiplus::PointF(centerX + heartSize / 2.0f, centerY - heartSize / 4.0f);

		// 右边的尖端
		heartPoints[6] = Gdiplus::PointF(centerX + heartSize / 3.0f, centerY + heartSize / 4.0f);

		// 底部尖端
		heartPoints[7] = Gdiplus::PointF(centerX, centerY + heartSize / 2.0f);

		// 左边的尖端
		heartPoints[8] = Gdiplus::PointF(centerX - heartSize / 3.0f, centerY + heartSize / 4.0f);

		// 闭合点
		heartPoints[9] = heartPoints[0];

		// 绘制心形
		g.FillPolygon(&iconBrush, heartPoints, 9);

		// 如果需要只显示轮廓（未收藏状态）
		Gdiplus::Pen heartPen(&iconBrush, 1.0f);
		if (!hovered)  // 非悬停时显示轮廓
		{
			g.DrawPolygon(&heartPen, heartPoints, 9);
		}

	}
	void MainWindow::HandleCloseButtonClick(HWND hwnd)
	{
		OutputDebugStringW(L"[Button Click] Close\n");

		// 关闭程序
		SendMessage(hwnd, WM_CLOSE, 0, 0);
	}
	void MainWindow::HandleMiniButtonClick(HWND hwnd)
	{
		OutputDebugStringW(L"[Button Click] Minimize\n");

		// 隐藏窗口，显示主窗口 GUI
		if (m_windowGUI)
		{
			ShowWindow(hwnd, SW_HIDE);
			m_windowGUI->ShowWindowGUI();
		}
	}
	void MainWindow::HandleHeartButtonClick(HWND hwnd)
	{
		OutputDebugStringW(L"[Button Click] Heart (Favorite)\n");

		// TODO: 实现收藏功能
		// 1. 切换收藏状态
		// 2. 保存收藏到文件或数据库
		// 3. 更新 UI（心形从空心变实心或反之）
		// 4. 可以播放收藏成功的音效

		/*
		示例实现：
		static bool isFavorited = false;
		isFavorited = !isFavorited;

		if (isFavorited)
		{
			OutputDebugStringW(L"[Favorite] Added to favorites\n");
			// 更新 UI 显示实心心形
		}
		else
		{
			OutputDebugStringW(L"[Favorite] Removed from favorites\n");
			// 更新 UI 显示空心心形
		}

		Composite();
		*/
	}
	void MainWindow::HandleSoundButtonClick(HWND hwnd)
	{
		OutputDebugStringW(L"[Button Click] Sound (Volume)\n");

		// TODO: 实现音量控制功能
		// 1. 显示音量滑块窗口
		// 2. 或者在长按时拖动调整
		// 3. 可以循环切换音量等级（静音 -> 低 -> 中 -> 高）

		/*
		示例实现：
		// 显示音量菜单
		ShowVolumeMenu(hwnd);

		或者：

		// 静音/取消静音
		if (m_audioPlayer)
		{
			float currentVolume = m_audioPlayer->GetVolume();
			if (currentVolume > 0.0f)
			{
				m_audioPlayer->SetVolume(0.0f);
				OutputDebugStringW(L"[Volume] Muted\n");
			}
			else
			{
				m_audioPlayer->SetVolume(0.7f);
				OutputDebugStringW(L"[Volume] Unmuted\n");
			}
		}
		*/
	}
	void MainWindow::HandleListButtonClick(HWND hwnd)
	{
		OutputDebugStringW(L"[Button Click] List (Playlist)\n");

		// TODO: 实现播放列表功能
		// 1. 显示播放列表下拉菜单
		// 2. 或展开侧边栏显示列表
		// 3. 允许用户选择要播放的曲目

		/*
		示例实现：
		ShowPlaylistWindow(hwnd);

		或者：

		// 切换列表可见性
		m_playlistVisible = !m_playlistVisible;
		if (m_playlistVisible)
		{
			OutputDebugStringW(L"[Playlist] Showing playlist\n");
			// 显示播放列表 UI
		}
		else
		{
			OutputDebugStringW(L"[Playlist] Hiding playlist\n");
			// 隐藏播放列表 UI
		}

		Composite();
		*/
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
