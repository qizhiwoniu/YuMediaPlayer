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

		m_trayIcon.Create(m_hwnd);  
		m_trayIcon.ShowBalloon(L"YuMediaPlayer starting", L"starting...");

		int clientW = rc.right - rc.left;
		int clientH = rc.bottom - rc.top;

		Composite();

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
					WaitMessage(); // 
				}
			}
		}

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
			if (!m_isCollapsed)
				CheckAndCollapseAtEdge();
			return 0;
		}
		case WM_TIMER:
		{
			if (wParam == 1001)
			{
				if (m_isCollapsed)
					CheckCollapsedMouseHover();
				else if (!m_isMoving)
					CheckAutoCollapse();

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

			static HBRUSH s_darkBrush = CreateSolidBrush(RGB(24, 24, 24)); // blackground
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
				RestoreWindow();
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
			// 角
			if (top && left)     return HTTOPLEFT;
			if (top && right)    return HTTOPRIGHT;
			if (bottom && left)  return HTBOTTOMLEFT;
			if (bottom && right) return HTBOTTOMRIGHT;

			// bian
			if (left)   return HTLEFT;
			if (right)  return HTRIGHT;
			if (top)    return HTTOP;
			if (bottom) return HTBOTTOM;
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
			if (m_edgeHoverTimer != 0)
			{
				KillTimer(hwnd, m_edgeHoverTimer);
				m_edgeHoverTimer = 0;
			}
			PostQuitMessage(0);
			return 0;

		default:
			return DefWindowProc(hwnd, msg, wParam, lParam);
		}

		return DefWindowProc(hwnd, msg, wParam, lParam);
	}

	void MainWindow::CheckAutoCollapse()
	{
		if (m_isCollapsed || m_isMoving)
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
			RestoreWindow();
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
		if (m_isCollapsed)
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

		int newX = wr.left;
		int newY = wr.top;
		int newWidth = COLLAPSED_SIZE;
		int newHeight = COLLAPSED_SIZE;

		// 左/上/下：保持原来的 94x94 收起方式。
		// 右侧：保持原窗口尺寸，只把窗口向右藏到屏幕外，露出 84px 圆形头像。
		if (nearLeft)
		{
			m_collapsedEdge = CollapseEdge::Left;
			newX = mi.rcWork.left;
			newY = wr.top;
		}
		else if (nearRight)
		{
			m_collapsedEdge = CollapseEdge::Right;
			newX = mi.rcWork.right - RIGHT_REVEAL - AVATAR_RIGHT;
			newY = wr.top;
			newWidth = windowWidth;
			newHeight = windowHeight;
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

		// 结束系统当前的拖动，否则系统可能紧接着下一条 WM_MOVING 把收起后的窗口又拖走。
		ReleaseCapture();

		Composite();
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

			if (m_isCollapsed)
			{
				// 所有收起状态都只绘制头像。
				// 右侧虽然窗口仍保持原尺寸，但其余区域完全透明，
				// 因此视觉上只有圆形头像露在屏幕边缘。
				Gdiplus::RectF avatarRect(8.0f, 0.0f, 84.0f, 84.0f);
				m_avatarRectF = avatarRect;
				m_avatar.Draw(g, avatarRect);
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
				Gdiplus::RectF avatarRect(8.0f, 0.0f, 84.0f, 84.0f);
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
		if (!m_isCollapsed)
			return;

		m_isCollapsed = false;
		m_collapsedEdge = CollapseEdge::None;

		// 恢复到之前保存的大小和位置
		SetWindowPos(m_hwnd, nullptr, m_savedWindowX, m_savedWindowY,
			m_savedWindowWidth, m_savedWindowHeight, SWP_NOZORDER | SWP_NOACTIVATE);
		// 关键：分层窗口改变尺寸后必须调用 Composite() 重新绘制
		Composite();
	}

}
