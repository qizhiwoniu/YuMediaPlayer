#include "MainWindow.h"  
#include <dwmapi.h>
#include <cstdint>
#include <cstdio>
#include <cwchar>
#include <memory>
#include <windowsx.h>
#include <algorithm>
#include <cmath>
#include <set>
#include "Core/Theme.h"
#include "UI/WindowGUI.h"
#include "UI/miniWindowGUI.h"
#include "NotifyIcon/TrayIcon.h"
#include "Core/AudioPlayer.h"
#include "Core/Playlist.h"
#include "Core/PlaylistPanel.h"

#pragma comment(lib, "dwmapi.lib")

namespace YuMediaPlayer
{
	PlayButtonInfo g_playButtonInfo = {};
	std::wstring g_currentMp3Path = L"";
	PlaybackButtonsInfo buttonsInfo = {};
	static bool IsPointInRectF(POINT pt, const Gdiplus::RectF& rect)
	{
		return pt.x >= rect.X && pt.x <= rect.X + rect.Width
			&& pt.y >= rect.Y && pt.y <= rect.Y + rect.Height;
	}

	// 定义在 miniWindowGUI.cpp：播放按钮圆心的 X 坐标（窗口坐标）。
	float GetPlayButtonCenterX(const Gdiplus::RectF& vRect);

	// ===== 自动下一首 / 播放列表面板 / 悬停显示按钮 用到的文件内状态 =====
	// （MainWindow.h 里不需要加任何成员，整个程序只有一个主窗口，用文件内状态就够了。）

	// AudioPlayer 在 MF 工作线程上收到 MESessionEnded 后，用 PostMessage 发给主窗口的消息。
	// wParam = 那首歌的编号（AudioPlayer::GetTrackGeneration），用来丢弃过期通知。
	static constexpr UINT WM_YU_TRACK_ENDED = WM_APP + 1;

	static PlaylistPanel s_playlistPanel;   // 展开式播放列表面板
	static int  s_panelExtraH = 0;          // 面板展开时窗口比"播放器本体"多出的高度；0 = 没展开
	static int  s_panelShiftUp = 0;         // 屏幕下方放不下时，窗口为此向上挪了多少像素（关闭时挪回来）
	static bool s_cardHovered = false;      // 鼠标是否在卡片/头像/列表上：在 = 显示按钮，不在 = 显示歌名歌手

	// 已收藏的歌曲（用音频文件路径当 key）。收藏按钮点一下加入/移除，Composite 里据此画实心/空心心形。
	// 目前只存在内存里，程序退出就没了；以后要持久化，把这个集合读写到文件即可。
	static std::set<std::wstring> s_favorites;

	// 播放进度定时器：定期把 AudioPlayer 的播放进度推给封面外圈的进度环。
	// （ID 1001=边缘悬停 2001=收起动画 3001=封面旋转，4001 没被占用）
	static constexpr UINT_PTR kProgressTimerId = 4001;
	static constexpr UINT     kProgressIntervalMs = 250;

	// 头像右键菜单里选了"进度条：黄色/七彩"之后，把选择应用到 CircularAvatar 的皮肤上。
	// 其它菜单命令直接忽略。调用者随后要自己 Composite()。
	static void ApplyAvatarRingChoice(CircularAvatar& avatar, int cmd)
	{
		if (cmd != static_cast<int>(ContextMenuCommand::AvatarRingYellow) &&
			cmd != static_cast<int>(ContextMenuCommand::AvatarRingRainbow))
			return;

		CircularAvatar::Skin skin = avatar.GetSkin();
		skin.rainbowProgress = IsRainbowRing();
		avatar.SetSkin(skin);
	}

	// 鼠标不在卡片上时，按钮是隐藏的，必须把它们的点击区域也挪到屏幕外，
	// 否则看不见的按钮照样能被点中（WM_NCHITTEST 和 WM_LBUTTONUP 都靠这些矩形判断）。
	static void HideControlHitAreas()
	{
		const Gdiplus::RectF off(-10000.0f, -10000.0f, 0.0f, 0.0f);
		buttonsInfo.close.rect = off;
		buttonsInfo.mini.rect = off;
		buttonsInfo.sound.rect = off;
		buttonsInfo.list.rect = off;
		buttonsInfo.heart.rect = off;
		buttonsInfo.previous.rect = off;
		buttonsInfo.next.rect = off;

		buttonsInfo.close.hovered = false;
		buttonsInfo.mini.hovered = false;
		buttonsInfo.sound.hovered = false;
		buttonsInfo.list.hovered = false;
		buttonsInfo.heart.hovered = false;
		buttonsInfo.previous.hovered = false;
		buttonsInfo.next.hovered = false;

		g_playButtonInfo.buttonRect.left = -10000;
		g_playButtonInfo.buttonRect.top = -10000;
		g_playButtonInfo.buttonRect.right = -10000;
		g_playButtonInfo.buttonRect.bottom = -10000;
		g_playButtonInfo.isHovered = false;
	}

	// 把窗口高度改成"播放器本体高度 + newExtra"（newExtra = 0 就是恢复成没有列表的样子）。
	// 窗口顶边不动、向下长；屏幕下方放不下时整个窗口往上挪，关闭时再挪回来。
	static void ResizeWindowForPlaylist(HWND hwnd, int newExtra)
	{
		RECT wr;
		GetWindowRect(hwnd, &wr);

		const int w = wr.right - wr.left;
		const int baseH = (wr.bottom - wr.top) - s_panelExtraH;
		const int newH = baseH + newExtra;
		int top = wr.top;

		if (newExtra > 0)
		{
			HMONITOR hMon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
			MONITORINFO mi = { sizeof(mi) };
			if (GetMonitorInfo(hMon, &mi))
			{
				int shift = top + newH - mi.rcWork.bottom;
				if (shift > 0)
				{
					if (top - shift < mi.rcWork.top)
						shift = top - mi.rcWork.top;
					if (shift < 0)
						shift = 0;
					top -= shift;
					s_panelShiftUp = shift;
				}
			}
		}
		else
		{
			top += s_panelShiftUp;
			s_panelShiftUp = 0;
		}

		// 必须先改 s_panelExtraH 再 SetWindowPos：SetWindowPos 会同步触发 WM_SIZE -> Composite，
		// Composite 要用它算"播放器本体高度"。
		s_panelExtraH = newExtra;
		SetWindowPos(hwnd, nullptr, wr.left, top, w, newH, SWP_NOZORDER | SWP_NOACTIVATE);
	}

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
		else if (AudioPlayer* player = GetAudioPlayer())
		{
			// 曲子自然播完时，AudioPlayer 会给 m_hwnd 发 WM_YU_TRACK_ENDED（见 EventProc）。
			player->SetEndNotify(m_hwnd, WM_YU_TRACK_ENDED);

			// 诊断：AudioPlayer.cpp 和本文件看到的 AudioPlayer 大小是否一致。
			// 不一致 = 头文件版本不同（重复的 AudioPlayer.h / 没重新编译的旧 .obj），
			// 这时跨文件读写成员（比如 GetPlaybackState）会读到乱码。
			{
				wchar_t layoutBuf[200];
				const size_t sizeInCpp = player->DebugSizeOfSelf();
				const size_t sizeHere = sizeof(AudioPlayer);
				swprintf_s(layoutBuf, L"[AudioPlayer] sizeof 检查：AudioPlayer.cpp=%zu，MainWindow.cpp=%zu %s\n",
					sizeInCpp, sizeHere, sizeInCpp == sizeHere ? L"（一致）" : L"（不一致！头文件版本不同，请清理重建）");
				OutputDebugStringW(layoutBuf);
			}
		}

		// 扫描 song\local 建立播放列表。界面先显示第一首歌的封面和歌名（不自动播放），
		// 后面 Initialize 末尾的 Composite() 会把它画出来。
		if (m_playlist.LoadDefault() > 0)
			ApplyTrackToUi(*m_playlist.Current());
		else
			OutputDebugStringW(L"Warning: 没有在 song\\local 下找到任何音频文件\n");

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
		m_windowGUI->SetMiniWindow(m_hwnd);   // 关闭主窗口时用它把迷你窗口弄回来
		int clientW = rc.right - rc.left;
		int clientH = rc.bottom - rc.top;

		Composite();
		m_windowGUI->HideWindowGUI();

		m_edgeHoverTimer = SetTimer(m_hwnd, 1001, 100, nullptr);
		if (m_edgeHoverTimer == 0)
			return false;

		SetTimer(m_hwnd, kProgressTimerId, kProgressIntervalMs, nullptr);   // 进度环随播放走动
		
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
		// 重新判断鼠标是否在"卡片 / 头像 / 展开的列表面板"上；状态变了返回 true（调用者负责重绘）。
		// 窗口是逐像素透明的分层窗口，卡片外面那圈透明边不算悬停。
		// 卡片区域的大部分在 HTCAPTION 上，那里只有 WM_NCMOUSEMOVE 没有 WM_MOUSEMOVE，
		// 所以这里直接用 GetCursorPos 算，不依赖具体收到的是哪种鼠标消息。
		auto refreshHover = [&]() -> bool
		{
			RECT wr;
			POINT cp;
			if (!GetWindowRect(hwnd, &wr) || !GetCursorPos(&cp))
				return false;

			const int x = cp.x - wr.left;
			const int y = cp.y - wr.top;
			const int w = wr.right - wr.left;
			const int bodyH = (wr.bottom - wr.top) - s_panelExtraH;

			bool inside = (x >= 10 && x < w - 10 && y >= 20 && y < bodyH - 10)   // 卡片
				|| (x >= 18 && x < 102 && y >= 0 && y < 84);                     // 头像
			if (s_panelExtraH > 0 && x >= 10 && x < w - 10 && y >= bodyH - 6 && y < bodyH + s_panelExtraH)
				inside = true;                                                    // 列表面板
			if (m_isCollapsed || m_isAnimating)
				inside = false;

			if (inside == s_cardHovered)
				return false;

			s_cardHovered = inside;
			if (!inside)
				s_playlistPanel.ClearHover();
			return true;
		};

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
			s_panelShiftUp = 0;   // 用户自己拖过窗口了，关闭列表时不再自动"挪回去"

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
				// 兜底：鼠标离开窗口时不一定有鼠标消息，靠这个定时器保证按钮/歌名及时切换。
				if (refreshHover())
					Composite();

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
			else if (wParam == kProgressTimerId)
			{
				// 播放中/暂停中：把 AudioPlayer 的进度同步给进度环。
				// 已停止就不动它（ApplyTrackToUi 切歌时会把环归零）。
				AudioPlayer* player = GetAudioPlayer();
				if (player && player->GetPlaybackState() != PlaybackState::Stopped)
				{
					const float p = player->GetPlayProgress();
					const float cur = m_avatar.GetProgress();
					const float diff = p > cur ? p - cur : cur - p;
					if (diff > 0.0005f)   // 变化不到 0.05% 不重绘，省得白白合成整个分层窗口
					{
						m_avatar.SetProgress(p);
						// 封面在旋转时，旋转定时器每帧都会重绘，不用再重复；窗口藏起来时也不用画
						if (!IsAvatarRotating() && ::IsWindowVisible(hwnd))
							Composite();
					}
				}
				return 0;
			}
			else if (wParam == 3001)
			{
				// 更新角度后必须 Composite()，因为窗口使用 UpdateLayeredWindow。
				HandleAvatarRotationTimer(hwnd, this);
				if (IsAvatarRotating())
					Composite();
				return 0;
			}
			break;
		}
		case WM_CONTEXTMENU:
		{
			// lParam 是屏幕坐标；头像矩形是客户区坐标，必须先转换。
			POINT screenPt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
			if (screenPt.x == -1 && screenPt.y == -1)
				GetCursorPos(&screenPt);

			POINT clientPt = screenPt;
			ScreenToClient(hwnd, &clientPt);

			if (m_avatar.HitTest(m_avatarRectF, clientPt))
			{
				int cmd = ShowAvatarContextMenu(hwnd, screenPt);
				if (cmd != 0)
				{
					HandleAvatarContextMenuCommand(hwnd, cmd);
					ApplyAvatarRingChoice(m_avatar, cmd);
					Composite();
				}
			}
			else
			{
				int cmd = ShowMiniPlayerContextMenu(hwnd, screenPt, m_windowGUI.get());
				if (cmd != 0)
					HandleMiniPlayerContextMenuCommand(hwnd, cmd, m_windowGUI.get());
			}
			return 0;
		}
		case WM_NCRBUTTONUP:
		{
			POINT screenPt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
			POINT clientPt = screenPt;
			ScreenToClient(hwnd, &clientPt);

			if (m_avatar.HitTest(m_avatarRectF, clientPt))
			{
				int cmd = ShowAvatarContextMenu(hwnd, screenPt);
				if (cmd != 0)
				{
					HandleAvatarContextMenuCommand(hwnd, cmd);
					ApplyAvatarRingChoice(m_avatar, cmd);
					Composite();
				}
				return 0;
			}

			if (wParam == HTCAPTION)
			{
				int cmd = ShowMiniPlayerContextMenu(hwnd, screenPt, m_windowGUI.get());
				if (cmd != 0)
					HandleMiniPlayerContextMenuCommand(hwnd, cmd, m_windowGUI.get());
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

			// 播放列表展开时，先看是不是点中了某一首歌。
			if (s_playlistPanel.IsOpen())
			{
				RECT cr;
				GetClientRect(hwnd, &cr);
				const float winW = static_cast<float>(cr.right - cr.left);
				const float baseH = static_cast<float>((cr.bottom - cr.top) - s_panelExtraH);
				const int idx = s_playlistPanel.HitTestTrack(m_playlist, winW, baseH, pt);
				if (idx >= 0)
				{
					PlayTrack(idx);   // 内部会换封面/歌名、开始播放并重绘
					return 0;
				}
			}

			if (IsPointInPlayButton(pt, g_playButtonInfo))
			{
				// 停止状态下点播放，播的是播放列表里"当前这一首"（切歌之后不再是写死的七里香）。
				// 列表为空（没扫到歌）时退回原来的路径，行为跟以前一致。
				const Track* cur = m_playlist.Current();
				std::wstring mp3 = cur ? cur->audioPath : std::wstring(L"song\\local\\周杰伦-七里香.mp3");
				// 诊断日志：点击前后的播放状态（0=停止 1=播放 2=暂停），排查"图标不切换"时用。
				AudioPlayer* diagPlayer = GetAudioPlayer();
				const int stateBefore = diagPlayer ? static_cast<int>(diagPlayer->GetPlaybackState()) : -1;
				HandlePlayButtonClick(hwnd, mp3);  // 播放/暂停/继续，三种状态它自己会判断
				{
					wchar_t diagBuf[160];
					const int stateAfter = diagPlayer ? static_cast<int>(diagPlayer->GetPlaybackState()) : -1;
					swprintf_s(diagBuf, L"[PlayButton] 点击前状态=%d，点击后状态=%d（0=停止 1=播放 2=暂停）\n", stateBefore, stateAfter);
					OutputDebugStringW(diagBuf);
				}
				// 之前这里紧接着又调用了一次 TogglePlayPause(hwnd)，
				// 相当于把刚播放起来的状态又立刻切换了一次（Playing -> Pause），
				// 导致歌曲一启动就被暂停。HandlePlayButtonClick 已经处理了
				// 全部三种状态切换，不需要再调用 TogglePlayPause。
				//
				// 重点修复：窗口是分层窗口（UpdateLayeredWindow 绘制），
				// HandlePlayButtonClick 内部只调用了 InvalidateRect，而分层
				// 窗口根本不会走常规的 WM_PAINT 重绘路径——本文件里悬停状态
				// 变化、收起/展开动画帧等其它所有地方，改完状态后都紧跟着
				// 调用了 Composite()，唯独播放按钮点击这里漏了。结果就是：
				// 哪怕 Play()/Pause() 内部状态确实切换了，按钮图标在屏幕上
				// 也永远不会跟着变。
				Composite();
				return 0;
			}
			if (IsPointInRectF(pt, buttonsInfo.close.rect))
			{
				HandleCloseButtonClick(hwnd);
				return 0;
			}
			if (IsPointInRectF(pt, buttonsInfo.mini.rect))
			{
				HandleMiniButtonClick(hwnd, m_windowGUI.get());
				return 0;
			}
			if (IsPointInRectF(pt, buttonsInfo.sound.rect))
			{
				HandleSoundButtonClick(hwnd);
				Composite();   // 分层窗口必须手动重绘，静音图标才会变
				return 0;
			}
			if (IsPointInRectF(pt, buttonsInfo.list.rect))
			{
				// 展开 / 收起播放列表：窗口向下变高，面板画在播放器卡片下面。
				if (s_playlistPanel.IsOpen())
				{
					s_playlistPanel.SetOpen(false);
					ResizeWindowForPlaylist(hwnd, 0);
				}
				else
				{
					s_playlistPanel.SetOpen(true);
					ResizeWindowForPlaylist(hwnd, s_playlistPanel.ExtraHeight(m_playlist.Count()));
					s_playlistPanel.EnsureVisible(m_playlist);
				}
				Composite();
				return 0;
			}
			if (IsPointInRectF(pt, buttonsInfo.heart.rect))
			{
				// 切换当前这首歌的收藏状态，然后重绘（分层窗口必须手动 Composite，
				// 否则心形不会跟着变）。
				if (const Track* favTrack = m_playlist.Current())
				{
					auto it = s_favorites.find(favTrack->audioPath);
					if (it != s_favorites.end())
						s_favorites.erase(it);
					else
						s_favorites.insert(favTrack->audioPath);
				}
				HandleHeartButtonClick(hwnd);
				Composite();
				return 0;
			}
			if (IsPointInRectF(pt, buttonsInfo.previous.rect))
			{
				PlayPreviousTrack();
				return 0;
			}
			if (IsPointInRectF(pt, buttonsInfo.next.rect))
			{
				PlayNextTrack();
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

			// 订阅"鼠标离开客户区"通知（一次性的，每次移动都重新订阅）。
			TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };
			TrackMouseEvent(&tme);

			if (m_isCollapsed)
				CheckCollapsedMouseHover();

			
			pt.x = GET_X_LPARAM(lParam);
			pt.y = GET_Y_LPARAM(lParam);

			// ✅ 检测播放按钮悬停状态
			bool wasPlayHovered = g_playButtonInfo.isHovered;
			g_playButtonInfo.isHovered = IsPointInPlayButton(pt, g_playButtonInfo);
			// UpdateLayeredWindow 一次性推上去的，没有独立的子窗口，
						// 所以悬停状态变了就得整体重画（Composite），不能只 InvalidateRect。
			bool anyHoverChanged = (wasPlayHovered != g_playButtonInfo.isHovered);

			// 鼠标进入/离开卡片：切换"按钮"和"歌名歌手"。
			if (refreshHover())
				anyHoverChanged = true;

			// 列表面板里悬停的那一行。
			if (s_playlistPanel.IsOpen())
			{
				RECT cr;
				GetClientRect(hwnd, &cr);
				const float winW = static_cast<float>(cr.right - cr.left);
				const float baseH = static_cast<float>((cr.bottom - cr.top) - s_panelExtraH);
				if (s_playlistPanel.UpdateHover(m_playlist, winW, baseH, pt))
					anyHoverChanged = true;
			}

			auto updateHover = [&](ControlButtonInfo& btn)
				{
					bool was = btn.hovered;
					btn.hovered = IsPointInRectF(pt, btn.rect);
					if (was != btn.hovered)
						anyHoverChanged = true;
				};
			updateHover(buttonsInfo.close);
			updateHover(buttonsInfo.mini);
			updateHover(buttonsInfo.sound);
			updateHover(buttonsInfo.list);
			updateHover(buttonsInfo.heart);
			updateHover(buttonsInfo.previous);
			updateHover(buttonsInfo.next);

			if (anyHoverChanged)
				Composite();

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
		
		case WM_NCMOUSEMOVE:
		{
			// 鼠标在 HTCAPTION（可拖动的卡片区域）上移动时只有非客户区消息。
			TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE | TME_NONCLIENT, hwnd, 0 };
			TrackMouseEvent(&tme);
			if (refreshHover())
				Composite();
			break;   // 交给系统默认处理（拖动等）
		}
		case WM_MOUSELEAVE:
		case WM_NCMOUSELEAVE:
		{
			if (refreshHover())
				Composite();
			break;
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
			// 列表展开时窗口下半部分是列表面板，不当作可拖拽的下边缘。
			bool bottom = (s_panelExtraH == 0) && pt.y >= wr.bottom - border;
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
			
			POINT clientPt = pt;
			ScreenToClient(hwnd, &clientPt);
			if (m_avatar.HitTest(m_avatarRectF, clientPt))
				return HTCLIENT;

			// 重点修复：下面"标题栏（用于拖动）"那段把窗口底部 70px 整条
			// 都判成 HTCAPTION，而播放/暂停、上一曲、下一曲、收藏、关闭、
			// mini、音量、列表这些按钮全部都在这 70px 范围内。HTCAPTION
			// 会让点击被当成"拖标题栏"走非客户区消息，根本不会产生
			// WM_LBUTTONUP 这种客户区消息——这才是"点播放按钮完全没反应、
			// 连一行日志都没有"的真正原因，不是按钮坐标算错了。
			// 这里把所有按钮命中区域都从"可拖动区域"里挖掉，落在按钮上
			// 就正常返回 HTCLIENT，落在按钮之间的空白处才继续走拖动逻辑。
			if (IsPointInPlayButton(clientPt, g_playButtonInfo)
				|| IsPointInRectF(clientPt, buttonsInfo.close.rect)
				|| IsPointInRectF(clientPt, buttonsInfo.mini.rect)
				|| IsPointInRectF(clientPt, buttonsInfo.sound.rect)
				|| IsPointInRectF(clientPt, buttonsInfo.list.rect)
				|| IsPointInRectF(clientPt, buttonsInfo.heart.rect)
				|| IsPointInRectF(clientPt, buttonsInfo.previous.rect)
				|| IsPointInRectF(clientPt, buttonsInfo.next.rect))
			{
				return HTCLIENT;
			}

			// 标题栏（用于拖动） - 卡片顶部区域
			// 列表展开时，"播放器本体"的底边是 wr.bottom - s_panelExtraH，拖动区要按它算，
			// 面板区域走下面的 HTCLIENT，这样才能收到点击和悬停。
			const LONG bodyBottom = wr.bottom - s_panelExtraH;
			if (pt.y >= bodyBottom - 70 && pt.y < bodyBottom)
			{
				return HTCAPTION;
			}
			return HTCLIENT;
		}
		case WM_MOUSEWHEEL:
		{
			// 列表展开时，滚轮滚动歌曲行。
			if (s_playlistPanel.IsOpen())
			{
				const int rows = GET_WHEEL_DELTA_WPARAM(wParam) > 0 ? -1 : 1;
				if (s_playlistPanel.Scroll(m_playlist, rows))
					Composite();
				return 0;
			}
			break;
		}
		case WM_YU_TRACK_ENDED:
		{
			// 一首歌自然播完了（AudioPlayer 在 MF 线程上收到 MESessionEnded 后 PostMessage 过来）。
			AudioPlayer* player = GetAudioPlayer();
			if (!player)
				return 0;

			// 通知里带着"那首歌的编号"。编号跟现在不一样，说明通知排队期间用户已经手动切歌了，
			// 这是上一首遗留的过期通知，忽略。
			if (static_cast<unsigned int>(wParam) != player->GetTrackGeneration())
				return 0;

			if (m_playlist.Empty())
			{
				player->Stop();
				Composite();
				return 0;
			}

			// 单曲循环：重播当前这首；列表循环 / 随机 / 心动循环：下一首
			// （随机模式下 PlayNextTrack 里会随机挑，列表循环到末尾回到第一首）。
			if (GetCurrentLoopMode() == ContextMenuCommand::LoopModeSingleLoop)
				PlayTrack(m_playlist.CurrentIndex());
			else
				PlayNextTrack();
			return 0;
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
			if (IsAvatarRotating() || GetAvatarRotation() != 0.0f)
				StopAvatarRotation(hwnd);

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
			KillTimer(hwnd, kProgressTimerId);
			PostQuitMessage(0);
			return 0;

		default:
			return DefWindowProc(hwnd, msg, wParam, lParam);
		}

		// 上面好几个 case 是 break 出 switch 的（WM_TIMER 的未知 ID、WM_NCRBUTTONUP 没点中标题栏、
		// WM_NCCALCSIZE 的 wParam==0 等），必须在这里兜底返回，否则函数没有返回值（未定义行为）。
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
		// 播放列表展开时不做贴边收起（收起逻辑假定窗口只有播放器本体那么高）。
		if (m_isCollapsed || m_isAnimating || s_panelExtraH > 0)
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

			auto drawAvatar = [&](const Gdiplus::RectF& avatarRect)
			{
				m_avatarRectF = avatarRect;
				Gdiplus::GraphicsState state = g.Save();
				Gdiplus::GraphicsPath clipPath;
				clipPath.AddEllipse(avatarRect.X, avatarRect.Y,
					avatarRect.Width, avatarRect.Height);
				g.SetClip(&clipPath, Gdiplus::CombineModeReplace);

				// 旋转交给 CircularAvatar 处理：它只转中间的封面图，进度环不跟着转。
				// （以前是在这里把整个 Graphics 转一下再画，进度环和封面会一起转，
				//  而且这里如果再转一次就会转两遍，所以这里不能再加旋转变换。）
				m_avatar.SetRotation(GetAvatarRotation());
				m_avatar.Draw(g, avatarRect);
				g.Restore(state);
			};

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
				drawAvatar(avatarRect);
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
				drawAvatar(avatarRect);

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
				drawAvatar(avatarRect);

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
				drawAvatar(avatarRect);

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
				drawAvatar(avatarRect);

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
				// 播放列表展开时窗口比播放器本体高 s_panelExtraH，卡片和按钮布局只按本体高度算，
				// 多出来的那一截留给列表面板。
				const int bodyH = h - s_panelExtraH;
				Gdiplus::RectF cardRect(10.0f, 20.0f, (float)w - 20.0f, (float)bodyH - 30.0f);
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
				drawAvatar(avatarRect);

				// 跟 QQ 音乐一样：鼠标不在卡片上时显示歌名 + 歌手，
				// 鼠标放上来（或者列表展开着）时换成播放控制按钮。
				Gdiplus::RectF vRect(10.0f, 20.0f, (float)w - 20.0f, (float)bodyH - 30.0f);
				if (s_cardHovered || s_playlistPanel.IsOpen())
				{
					// 当前这首是否已收藏 -> 决定收藏按钮画实心红心还是空心描边心
					if (const Track* favTrack = m_playlist.Current())
						buttonsInfo.heart.active = (s_favorites.count(favTrack->audioPath) > 0);
					else
						buttonsInfo.heart.active = false;

					buttonsInfo.sound.active = IsSoundMuted();   // 音量按钮：静音时画 ×
					DrawPlayButtonGdiplus(g, vRect, false, false);
					DrawPlaybackButtonsGdiplus(g, vRect, avatarRect, buttonsInfo);
				}
				else
				{
					HideControlHitAreas();
					DrawTrackInfoGdiplus(g, vRect, avatarRect);
				}

				// 展开的播放列表面板（没展开时 Draw 什么都不画）。
				s_playlistPanel.Draw(g, m_playlist, (float)w, (float)bodyH);
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

	void MainWindow::DrawTrackInfoGdiplus(Gdiplus::Graphics& g, const Gdiplus::RectF& cardRect, const Gdiplus::RectF& /*avatarRect*/)
	{
		// 歌名 + 歌手：以"播放按钮的圆心"为中线水平居中——鼠标放上去时按钮出现在这个位置，
		// 切换前后视觉重心不跳。中线取自按钮布局本身（GetPlayButtonCenterX），
		// 以后改按钮布局，文字会自动跟着走。
		if (m_trackTitle.empty() && m_trackArtist.empty())
			return;

		// 左边至少留 10 + 84 + 10 = 104px 给旋转封面，右边到卡片右边缘。
		const float areaLeft = 10.0f + 84.0f + 10.0f;
		const float areaRight = cardRect.GetRight();
		const float centerX = GetPlayButtonCenterX(cardRect);
		const float innerPad = 6.0f;   // 太长的歌名用 ... 截断时不贴边

		// 以中线为轴左右对称：半宽取"中线到左边界"和"中线到右边界"里小的那个，
		// 这样既不会压到封面，也不会超出卡片，同时保证严格居中在中线上。
		float halfW = centerX - areaLeft;
		if (areaRight - centerX < halfW)
			halfW = areaRight - centerX;
		halfW -= innerPad;
		const float cardCenterY = cardRect.Y + (cardRect.Height / 2.0f);

		if (halfW <= 0)
			return;

		const float textX = centerX - halfW;
		const float textWidth = halfW * 2.0f;

		Gdiplus::StringFormat format;
		format.SetAlignment(Gdiplus::StringAlignmentCenter);      // 水平居中
		format.SetLineAlignment(Gdiplus::StringAlignmentNear);    // 垂直位置由下面自己算
		format.SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);
		format.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);

		Gdiplus::Font titleFont(L"Microsoft YaHei UI", 11.0f, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
		Gdiplus::Font artistFont(L"Microsoft YaHei UI", 9.0f, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
		Gdiplus::SolidBrush titleBrush(Gdiplus::Color(255, 255, 255));
		Gdiplus::SolidBrush artistBrush(Gdiplus::Color(200, 200, 200));

		const bool hasBoth = !m_trackTitle.empty() && !m_trackArtist.empty();
		if (hasBoth)
		{
			const float titleH = titleFont.GetHeight(&g);
			const float artistH = artistFont.GetHeight(&g);
			const float totalH = titleH + artistH - 2.0f;
			const float startY = cardCenterY - totalH / 2.0f;

			g.DrawString(m_trackTitle.c_str(), -1, &titleFont,
				Gdiplus::RectF(textX, startY, textWidth, titleH), &format, &titleBrush);
			g.DrawString(m_trackArtist.c_str(), -1, &artistFont,
				Gdiplus::RectF(textX, startY + titleH - 2.0f, textWidth, artistH), &format, &artistBrush);
		}
		else
		{
			const std::wstring& single = m_trackTitle.empty() ? m_trackArtist : m_trackTitle;
			Gdiplus::Font& font = m_trackTitle.empty() ? artistFont : titleFont;
			Gdiplus::SolidBrush& brush = m_trackTitle.empty() ? artistBrush : titleBrush;

			const float h = font.GetHeight(&g);
			const float startY = cardCenterY - h / 2.0f;
			g.DrawString(single.c_str(), -1, &font,
				Gdiplus::RectF(textX, startY, textWidth, h), &format, &brush);
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

	// ===== 播放列表 / 上一曲 / 下一曲 =====

	// 把一首歌对应的封面、歌名、歌手、进度环复位应用到界面上。
	// 注意这里不调用 Composite()——调用者（PlayTrack / Initialize）
	// 会在合适的时机统一重绘一次。
	void MainWindow::ApplyTrackToUi(const Track& track)
	{
		// 这首歌没有找到同名封面：明确清空，回到黑胶占位样式，
		// 否则上一首歌的封面会一直留在界面上。
		// （LoadImageFromFile 加载失败时内部也会先清掉旧图，所以两条路径的结果一致。）
		if (track.coverPath.empty())
		{
			m_avatar.ClearImage();
		}
		else if (!m_avatar.LoadImageFromFile(track.coverPath))
		{
			OutputDebugStringW((L"[MainWindow] 封面加载失败：" + track.coverPath + L"\n").c_str());
		}

		// 新的一首，进度环归零。
		m_avatar.SetProgress(0.0f);

		m_trackTitle = track.title;
		m_trackArtist = track.artist;
	}

	// 切到第 index 首并立即开始播放（不管之前是播放、暂停还是停止）。
	// 头像旋转状态（右键菜单里的"旋转"）不受影响：正在转就继续转，只是换了张封面。
	void MainWindow::PlayTrack(int index)
	{
		if (!m_playlist.SetCurrent(index))
			return;

		const Track& track = *m_playlist.Current();
		s_playlistPanel.EnsureVisible(m_playlist);   // 列表展开着的话，保证当前这首在可见范围内

		// 先换封面和歌名，再播放，最后统一重绘一次——
		// 这样重绘时播放按钮读到的已经是"播放中"的最终状态，不会闪一下旧图标。
		ApplyTrackToUi(track);

		if (AudioPlayer* player = GetAudioPlayer())
		{
			if (player->Play(track.audioPath))
			{
				// 开始播放 -> 封面自动开始旋转（切歌时已经在转就继续转；
				// 用户在右键菜单手动停过旋转的话，这里不会再自动转）。
				AutoStartAvatarRotation(m_hwnd);
			}
			else
			{
				OutputDebugStringW((L"[MainWindow] 播放失败：" + track.audioPath + L"\n").c_str());
			}
		}

		Composite();
	}

	void MainWindow::PlayNextTrack()
	{
		if (m_playlist.Empty())
			return;

		// 随机播放模式下"下一首"是随机挑的；其它模式（列表循环/单曲循环/心动循环）
		// 手动点下一首都是顺序往后走，到末尾回到第一首。
		const bool shuffle = (GetCurrentLoopMode() == ContextMenuCommand::LoopModeShuffle);
		PlayTrack(m_playlist.MoveNext(shuffle));
	}

	void MainWindow::PlayPreviousTrack()
	{
		if (m_playlist.Empty())
			return;

		const bool shuffle = (GetCurrentLoopMode() == ContextMenuCommand::LoopModeShuffle);
		PlayTrack(m_playlist.MovePrevious(shuffle));
	}


}
