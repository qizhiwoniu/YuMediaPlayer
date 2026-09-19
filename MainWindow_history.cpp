#include "MainWindow.h"  
#include <dwmapi.h>
#include <cstdint>
#include <memory>
#include <windowsx.h>
#include "Core/Theme.h"
#include "UI/miniWindowGUI.h"
#include "Core/CircularAvatar.h"

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

		// 分层窗口需要至少调用一次 Composite() 来绘制初始内容，否则窗口透明不可见
		Composite();

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

		// 2. 创建窗口（使用分层窗口 WS_EX_LAYERED）
		// 注意：分层窗口没有非客户区，所以不能使用 AdjustWindowRect()
		m_hwnd = CreateWindowEx(
			WS_EX_LAYERED,
			L"YuMediaPlayerMainWindows1",
			title,
			WS_POPUP,
			CW_USEDEFAULT,
			CW_USEDEFAULT,
			width,
			height,
			nullptr,
			nullptr,
			wc.hInstance,
			this);

		// ── 居中到主显示器 ──────────────────────────────────────
		{
			HMONITOR hMon = MonitorFromWindow(m_hwnd, MONITOR_DEFAULTTONEAREST);
			MONITORINFO mi = { sizeof(mi) };
			GetMonitorInfo(hMon, &mi);

			int monW = mi.rcWork.right - mi.rcWork.left;
			int monH = mi.rcWork.bottom - mi.rcWork.top;

			int posX = mi.rcWork.left + (monW - width) / 2;
			int posY = mi.rcWork.top + (monH - height) / 2;

			SetWindowPos(m_hwnd, nullptr, posX, posY, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
		}
		// 3. 显示窗口
		ShowWindow(m_hwnd, SW_SHOW);

		return m_hwnd != nullptr;
	}

	LRESULT MainWindow::EventProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
	{
		switch (msg)
		{
		case WM_MOVING:
		{
			// 窗口正在被拖动，检查是否接近屏幕边缘
			CheckAndCollapseAtEdge();
			return 0;
		}
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
			Composite(); // 窗口尺寸改变时重新合成
			return 0;
		}
		case WM_LBUTTONUP:
		{
			// 如果窗口已收起，点击时恢复
			if (m_isCollapsed)
			{
				RestoreWindow();
				return 0;
			}
			return 0;
		}
		case WM_MOUSEMOVE:
		{
			if (!m_isCollapsed)
			{
				CheckAndCollapseAtEdge();
			}
			else
			{
				// 收起后只有圆形区域可见。
				// 鼠标真正进入这个圆形区域时才恢复，而不是简单判断
				// “鼠标距离屏幕边缘多少像素”。
				POINT pt;
				GetCursorPos(&pt);

				RECT wr;
				GetWindowRect(hwnd, &wr);

				// avatarRect = (8, 0, 84, 84)
				const float avatarCX = 8.0f + 84.0f / 2.0f;
				const float avatarCY = 84.0f / 2.0f;
				const float avatarR  = 84.0f / 2.0f;

				float cx = static_cast<float>(wr.left) + avatarCX;
				float cy = static_cast<float>(wr.top) + avatarCY;

				float dx = static_cast<float>(pt.x) - cx;
				float dy = static_cast<float>(pt.y) - cy;

				bool insideAvatar = (dx * dx + dy * dy) <= (avatarR * avatarR);

				if (insideAvatar)
				{
					RestoreWindow();
				}
			}

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

	void MainWindow::CheckAndCollapseAtEdge()
	{
		RECT wr;
		GetWindowRect(m_hwnd, &wr);

		HMONITOR hMon = MonitorFromWindow(m_hwnd, MONITOR_DEFAULTTONEAREST);
		MONITORINFO mi = { sizeof(mi) };
		GetMonitorInfo(hMon, &mi);

		// 只有真正贴近屏幕边缘 5px 内才触发收起。
		const int EDGE_THRESHOLD = 5;

		// 收起后只保留圆形唱片区域。
		// avatarRect = x: 8~92, y: 0~84，因此窗口需要在右侧露出 92px。
		const int COLLAPSED_WIDTH = 94;
		const int COLLAPSED_HEIGHT = 94;
		const int AVATAR_RIGHT = 92;

		int windowWidth  = wr.right - wr.left;
		int windowHeight = wr.bottom - wr.top;

		bool nearLeft =
			wr.left <= mi.rcWork.left + EDGE_THRESHOLD;

		bool nearRight =
			wr.right >= mi.rcWork.right - EDGE_THRESHOLD;

		bool nearTop =
			wr.top <= mi.rcWork.top + EDGE_THRESHOLD;

		bool nearBottom =
			wr.bottom >= mi.rcWork.bottom - EDGE_THRESHOLD;

		if ((nearLeft || nearRight || nearTop || nearBottom) &&
			!m_isCollapsed)
		{
			// 保存完整播放器的位置和尺寸，恢复时使用。
			m_savedWindowWidth  = windowWidth;
			m_savedWindowHeight = windowHeight;
			m_savedWindowX      = wr.left;
			m_savedWindowY      = wr.top;

			int newX = wr.left;
			int newY = wr.top;

			// 优先判断左右边缘，避免角落时同时命中两个方向。
			if (nearLeft)
			{
				m_collapsedEdge = CollapseEdge::Left;

				// avatarRect 左边是 8px，所以让窗口左边露出。
				newX = mi.rcWork.left - 8;
			}
			else if (nearRight)
			{
				m_collapsedEdge = CollapseEdge::Right;

				// QQ 式右侧收起：
				// 不缩小播放器窗口，而是把整个窗口向屏幕右侧推出。
				// 头像位于窗口 x=8..92，所以让 x+92=屏幕右边。
				// 这样屏幕上只会露出圆形唱片，卡片主体藏到屏幕外。
				newX = mi.rcWork.right - AVATAR_RIGHT;
			}
			else if (nearTop)
			{
				m_collapsedEdge = CollapseEdge::Top;

				// avatarRect 顶部就是 0。
				newY = mi.rcWork.top;
			}
			else if (nearBottom)
			{
				m_collapsedEdge = CollapseEdge::Bottom;

				// avatarRect 高度 84，所以露出圆形上半部分。
				newY = mi.rcWork.bottom - 84;
			}

			m_isCollapsed = true;

			int collapsedWidth = COLLAPSED_WIDTH;
			int collapsedHeight = COLLAPSED_HEIGHT;

			// 右侧收起必须保持原始窗口尺寸。
			// 只有这样窗口主体才能被推到屏幕外，而圆形仍停留在屏幕边缘。
			if (m_collapsedEdge == CollapseEdge::Right)
			{
				collapsedWidth = windowWidth;
				collapsedHeight = windowHeight;
			}

			SetWindowPos(
				m_hwnd,
				nullptr,
				newX,
				newY,
				collapsedWidth,
				collapsedHeight,
				SWP_NOZORDER | SWP_NOACTIVATE
			);

			Composite();
		}
		else if (m_isCollapsed)
		{
			// 收起状态下不要因为窗口本身有一大块透明区域而误恢复。
			// 真正的恢复交给 WM_MOUSEMOVE：鼠标进入圆形区域时恢复。
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

			// 收起状态：
			// 右侧收起时窗口保持原始尺寸并藏到屏幕外，只有圆形唱片露在屏幕边缘。
			if (m_isCollapsed)
			{
				Gdiplus::RectF avatarRect(8.0f, 0.0f, 84.0f, 84.0f);
				m_avatarRectF = avatarRect;
				m_avatar.Draw(g, avatarRect);
			}
			else
			{
				// 正常状态：绘制背景卡片。
				{
					Gdiplus::RectF cardRect(
						10.0f,
						20.0f,
						(float)w - 20.0f,
						(float)h - 30.0f
					);

					Gdiplus::SolidBrush cardBrush(
						Gdiplus::Color(255, 40, 40, 40)
					);

					Gdiplus::GraphicsPath path;
					float radius = 12.0f;
					float d = radius * 2.0f;

					path.AddArc(cardRect.X, cardRect.Y, d, d, 180.0f, 90.0f);
					path.AddArc(cardRect.GetRight() - d, cardRect.Y, d, d, 270.0f, 90.0f);
					path.AddArc(cardRect.GetRight() - d, cardRect.GetBottom() - d, d, d, 0.0f, 90.0f);
					path.AddArc(cardRect.X, cardRect.GetBottom() - d, d, d, 90.0f, 90.0f);
					path.CloseFigure();

					g.FillPath(&cardBrush, &path);
				}

				// 正常状态：绘制圆形头像和进度环。
				{
					Gdiplus::RectF avatarRect(8.0f, 0.0f, 84.0f, 84.0f);
					m_avatarRectF = avatarRect;
					m_avatar.Draw(g, avatarRect);
				}

				// 正常状态：绘制歌曲信息。
				{
					Gdiplus::RectF cardRect(
						10.0f,
						20.0f,
						(float)w - 20.0f,
						(float)h - 30.0f
					);

					Gdiplus::RectF avatarRect(
						8.0f,
						0.0f,
						84.0f,
						84.0f
					);

					DrawTrackInfoGdiplus(g, cardRect, avatarRect);
				}
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
