#include "miniWindowGUI.h"
#include "UI\WindowGUI.h"
#include "TrayIcon.h"
#include "Core/AudioPlayer.h"
#include <algorithm>
#include <cmath>

namespace YuMediaPlayer
{
	WindowGUI* m_windowGUI = nullptr;
	namespace
	{
		// 菜单项文字。字符串字面量是静态存储期，地址在整个程序运行期间
		// 都有效，所以可以放心把指针塞进 AppendMenu 的 itemData 里，
		// 之后在 WM_DRAWITEM / WM_MEASUREITEM 时再取出来用。
		constexpr wchar_t kMenuItem1Text[] = L"总在最前";
		constexpr wchar_t kMenuItem2Text[] = L"完整模式";
		constexpr wchar_t kMenuItem3Text[] = L"打开桌面歌词";
		constexpr wchar_t kMenuItem4Text[] = L"歌词/歌曲,反馈";
		constexpr wchar_t kExitText[] = L"退出";

		constexpr wchar_t kLoopModeText[] = L"循环模式"; // 子菜单入口本身的文字
		constexpr wchar_t kLoopListText[] = L"列表循环";
		constexpr wchar_t kLoopSingleText[] = L"单曲循环";
		constexpr wchar_t kLoopShuffleText[] = L"随机播放";
		constexpr wchar_t kLoopHeartText[] = L"心动循环";

		constexpr COLORREF kMenuBackColor = RGB(24, 24, 24);      // 菜单整体背景
		constexpr COLORREF kMenuHighlightColor = RGB(55, 55, 55); // 鼠标悬停/选中时的背景
		constexpr COLORREF kMenuTextColor = RGB(230, 230, 230);   // 文字颜色

		constexpr int kCheckGutter = 20; // 给勾选标记留的左侧宽度，所有项统一预留，保证文字对齐

		// 播放按钮相关常量
		constexpr int kPlayButtonSize = 50;           // 按钮大小（像素）
		constexpr int kPlayButtonPadding = 8;         // 按钮距离边缘的距离
		constexpr COLORREF kPlayButtonColor = RGB(100, 200, 100);  // 绿色播放按钮
		constexpr COLORREF kPlayButtonHoverColor = RGB(120, 220, 120);  // 悬停时的颜色
		constexpr COLORREF kPlayButtonPlayingColor = RGB(200, 100, 100);  // 播放中时的颜色（红色停止按钮）
		constexpr COLORREF kPlayButtonIconColor = RGB(255, 255, 255);  // 图标颜色（白色）

		// 当前选中的循环模式，默认列表循环。点了子菜单里某一项之后会更新这里，
		// 下次弹菜单时对应项会带勾选标记。
		UINT s_currentLoopMode = ContextMenuCommand::LoopModeListLoop;
		// 全局音频播放器实例
		AudioPlayer* g_audioPlayer = nullptr;

		// 播放按钮信息（缓存）
		PlayButtonInfo g_playButtonInfo = {};
		HBRUSH DarkMenuBackgroundBrush()
		{
			// 只创建一次，进程生命周期内复用，不需要每次弹菜单都新建/销毁。
			static HBRUSH s_brush = CreateSolidBrush(kMenuBackColor);
			return s_brush;
		}

		bool IsLoopModeCommand(UINT_PTR cmd)
		{
			return cmd == ContextMenuCommand::LoopModeListLoop
				|| cmd == ContextMenuCommand::LoopModeSingleLoop
				|| cmd == ContextMenuCommand::LoopModeShuffle
				|| cmd == ContextMenuCommand::LoopModeHeart;
		}
	}
	// 绘制播放图标（三角形）
	void DrawPlayIcon(HDC hdc, int centerX, int centerY, int size, COLORREF color)
	{
		HBRUSH hBrush = CreateSolidBrush(color);
		HPEN hPen = CreatePen(PS_NULL, 0, 0);
		HBRUSH hOldBrush = (HBRUSH)SelectObject(hdc, hBrush);
		HPEN hOldPen = (HPEN)SelectObject(hdc, hPen);

		// 绘制向右指向的三角形
		POINT points[3];
		points[0] = { centerX - size / 3, centerY - size / 3 };  // 左上
		points[1] = { centerX - size / 3, centerY + size / 3 };  // 左下
		points[2] = { centerX + size / 2, centerY };              // 右

		Polygon(hdc, points, 3);

		SelectObject(hdc, hOldBrush);
		SelectObject(hdc, hOldPen);
		DeleteObject(hBrush);
		DeleteObject(hPen);
	}

	// 绘制停止图标（正方形）
	void DrawStopIcon(HDC hdc, int centerX, int centerY, int size, COLORREF color)
	{
		HBRUSH hBrush = CreateSolidBrush(color);
		HPEN hPen = CreatePen(PS_NULL, 0, 0);
		HBRUSH hOldBrush = (HBRUSH)SelectObject(hdc, hBrush);
		HPEN hOldPen = (HPEN)SelectObject(hdc, hPen);

		// 绘制正方形
		int halfSize = size / 3;
		RECT rect = {
			centerX - halfSize,
			centerY - halfSize,
			centerX + halfSize,
			centerY + halfSize
		};
		FillRect(hdc, &rect, hBrush);

		SelectObject(hdc, hOldBrush);
		SelectObject(hdc, hOldPen);
		DeleteObject(hBrush);
		DeleteObject(hPen);
	}

	// 绘制圆形按钮背景
	void DrawCircleButton(HDC hdc, int x, int y, int radius, COLORREF bgColor)
	{
		// 创建椭圆刷子和笔
		HBRUSH hBrush = CreateSolidBrush(bgColor);
		HPEN hPen = CreatePen(PS_SOLID, 3, RGB(200, 200, 200));

		HBRUSH hOldBrush = (HBRUSH)SelectObject(hdc, hBrush);
		HPEN hOldPen = (HPEN)SelectObject(hdc, hPen);

		// 绘制圆形
		Ellipse(hdc, x - radius, y - radius, x + radius, y + radius);

		SelectObject(hdc, hOldBrush);
		SelectObject(hdc, hOldPen);
		DeleteObject(hBrush);
		DeleteObject(hPen);
	}

	UINT GetCurrentLoopMode()
	{
		return s_currentLoopMode;
	}

	int ShowMiniPlayerContextMenu(HWND hwnd, POINT pt, YuMediaPlayer::WindowGUI* windowGUI)
	{
		// "循环模式"子菜单。挂到主菜单上之后系统会自动在"循环模式"这一项右边画箭头，
		// 不需要自己画箭头。
		HMENU hLoopSubMenu = CreatePopupMenu();
		AppendMenu(hLoopSubMenu,
			MF_OWNERDRAW | (s_currentLoopMode == ContextMenuCommand::LoopModeListLoop ? MF_CHECKED : 0),
			ContextMenuCommand::LoopModeListLoop, reinterpret_cast<LPCWSTR>(kLoopListText));
		AppendMenu(hLoopSubMenu,
			MF_OWNERDRAW | (s_currentLoopMode == ContextMenuCommand::LoopModeSingleLoop ? MF_CHECKED : 0),
			ContextMenuCommand::LoopModeSingleLoop, reinterpret_cast<LPCWSTR>(kLoopSingleText));
		AppendMenu(hLoopSubMenu,
			MF_OWNERDRAW | (s_currentLoopMode == ContextMenuCommand::LoopModeShuffle ? MF_CHECKED : 0),
			ContextMenuCommand::LoopModeShuffle, reinterpret_cast<LPCWSTR>(kLoopShuffleText));
		AppendMenu(hLoopSubMenu,
			MF_OWNERDRAW | (s_currentLoopMode == ContextMenuCommand::LoopModeHeart ? MF_CHECKED : 0),
			ContextMenuCommand::LoopModeHeart, reinterpret_cast<LPCWSTR>(kLoopHeartText));

		HMENU hMenu = CreatePopupMenu();

		// 普通的 MF_STRING 菜单项是系统自己绘制的，文字颜色固定用系统的
		// "菜单文字色"（通常是黑色），改了背景色也没用，黑底黑字看不清。
		// 这里用 MF_OWNERDRAW，把文字指针存进 itemData，交给下面的
		// DrawMiniPlayerMenuItem 自己画，才能真正控制成黑底白字。
		AppendMenu(hMenu, MF_OWNERDRAW, ContextMenuCommand::MenuItem1, reinterpret_cast<LPCWSTR>(kMenuItem1Text));
		AppendMenu(hMenu, MF_OWNERDRAW, ContextMenuCommand::MenuItem2, reinterpret_cast<LPCWSTR>(kMenuItem2Text));
		AppendMenu(hMenu, MF_OWNERDRAW, ContextMenuCommand::MenuItem3, reinterpret_cast<LPCWSTR>(kMenuItem3Text));
		AppendMenu(hMenu, MF_OWNERDRAW, ContextMenuCommand::MenuItem4, reinterpret_cast<LPCWSTR>(kMenuItem4Text));
		AppendMenu(hMenu, MF_SEPARATOR, 0, NULL); // 分割线，交给系统默认绘制

		// MF_POPUP + MF_OWNERDRAW：第三个参数改成子菜单句柄而不是命令 ID，
		// 第四个参数依然是 itemData（这里存文字指针），点这一项不会返回命令，
		// 而是展开 hLoopSubMenu，箭头由系统自动画在最右边。
		AppendMenu(hMenu, MF_POPUP | MF_OWNERDRAW, reinterpret_cast<UINT_PTR>(hLoopSubMenu),
			reinterpret_cast<LPCWSTR>(kLoopModeText));

		AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
		AppendMenu(hMenu, MF_OWNERDRAW, ContextMenuCommand::Exit, reinterpret_cast<LPCWSTR>(kExitText));

		// MIM_BACKGROUND: 整个弹出菜单（含分割线所在的空白区域）都用这个刷子填色。
		// MIM_APPLYTOSUBMENUS: 顺便把子菜单（循环模式）也一起设成黑色背景，
		// 不用再对 hLoopSubMenu 单独调一次 SetMenuInfo。
		MENUINFO menuInfo = { sizeof(MENUINFO) };
		menuInfo.fMask = MIM_BACKGROUND | MIM_APPLYTOSUBMENUS;
		menuInfo.hbrBack = DarkMenuBackgroundBrush();
		SetMenuInfo(hMenu, &menuInfo);

		// 补上标准写法：如果弹出菜单时窗口不是前台窗口，点击菜单外区域菜单不会自动消失。
		// SetForegroundWindow 让窗口成为前台窗口，TrackPopupMenu 返回后再发一个 WM_NULL
		// 促使消息队列正常处理菜单的关闭。
		SetForegroundWindow(hwnd);

		// TPM_LEFTALIGN:  菜单左边缘对齐 pt.x
		// TPM_RIGHTBUTTON: 允许用右键点选菜单项
		// TPM_RETURNCMD:   直接返回选中项的 ID，而不是发送 WM_COMMAND 消息
		int cmd = TrackPopupMenu(hMenu, TPM_LEFTALIGN | TPM_RIGHTBUTTON | TPM_RETURNCMD, pt.x, pt.y, 0, hwnd, NULL);

		PostMessage(hwnd, WM_NULL, 0, 0);

		// 销毁父菜单会自动把挂在它下面的子菜单（hLoopSubMenu）一并销毁，
		// 不需要再单独 DestroyMenu(hLoopSubMenu)。
		DestroyMenu(hMenu);

		if (IsLoopModeCommand(static_cast<UINT_PTR>(cmd)))
		{
			s_currentLoopMode = static_cast<UINT>(cmd);
		}
		else if (cmd != 0)
		{
			HandleMiniPlayerContextMenuCommand(hwnd, cmd);
		}

		return cmd;
	}

	void HandleMiniPlayerContextMenuCommand(HWND hwnd, int cmd, YuMediaPlayer::WindowGUI* windowGUI)
	{
		switch (cmd)
		{
		case ContextMenuCommand::MenuItem1:
			// 总在最前
			break;

		case ContextMenuCommand::MenuItem2:
		{
			if (!m_windowGUI)
			{
				
				break;
			}

			HWND windowHwnd = m_windowGUI->GetHWND();

			if (!::IsWindow(windowHwnd))
			{
				break;
			}

			if (::IsWindowVisible(windowHwnd) && !::IsIconic(windowHwnd))
			{
				m_windowGUI->HideWindowGUI();
			}
			else
			{
				m_windowGUI->ShowWindowGUI();
			}

			break;
		}
		case ContextMenuCommand::MenuItem3:
			// 打开桌面歌词
			break;

		case ContextMenuCommand::MenuItem4:
			// 歌词/歌曲，反馈
			break;

		case ContextMenuCommand::Exit:
			// 退出
			PostQuitMessage(0);
			break;
		}
	}

	void MeasureMiniPlayerMenuItem(MEASUREITEMSTRUCT& mis)
	{
		if (mis.CtlType != ODT_MENU)
			return;

		auto* text = reinterpret_cast<LPCWSTR>(mis.itemData);
		if (!text)
			return;

		HDC hdc = GetDC(nullptr);
		HFONT hFont = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
		HFONT hOldFont = static_cast<HFONT>(SelectObject(hdc, hFont));

		RECT rc = {};
		DrawTextW(hdc, text, -1, &rc, DT_CALCRECT | DT_SINGLELINE);

		SelectObject(hdc, hOldFont);
		ReleaseDC(nullptr, hdc);

		// +32 是左右基础留白，+kCheckGutter 是给勾选标记预留的空间——
		// 所有项统一留出这块宽度，不管这一项本身能不能被勾选，
		// 这样同一个菜单里的文字左边缘才能对齐。
		mis.itemWidth = static_cast<UINT>(rc.right - rc.left) + 32 + kCheckGutter;
		mis.itemHeight = static_cast<UINT>(rc.bottom - rc.top) + 12;
	}

	void DrawMiniPlayerMenuItem(const DRAWITEMSTRUCT& dis)
	{
		if (dis.CtlType != ODT_MENU)
			return;

		HDC hdc = dis.hDC;
		RECT rc = dis.rcItem;

		bool selected = (dis.itemState & ODS_SELECTED) != 0;
		bool checked = (dis.itemState & ODS_CHECKED) != 0;

		HBRUSH hBrush = CreateSolidBrush(selected ? kMenuHighlightColor : kMenuBackColor);
		FillRect(hdc, &rc, hBrush);
		DeleteObject(hBrush);

		SetBkMode(hdc, TRANSPARENT);
		SetTextColor(hdc, kMenuTextColor);

		if (checked)
		{
			// 简单画一个勾，表示这是当前选中的循环模式
			RECT checkRc = rc;
			checkRc.left += 12;
			checkRc.right = checkRc.left + kCheckGutter;
			DrawTextW(hdc, L"\u2713", -1, &checkRc, DT_SINGLELINE | DT_VCENTER | DT_LEFT);
		}

		auto* text = reinterpret_cast<LPCWSTR>(dis.itemData);
		if (text)
		{
			RECT textRc = rc;
			textRc.left += 12 + kCheckGutter; // 固定预留勾选区域宽度，文字左边缘始终对齐
			DrawTextW(hdc, text, -1, &textRc, DT_SINGLELINE | DT_VCENTER | DT_LEFT);
		}
	}




	// ===== 新增：播放按钮相关实现 =====
		
	void DrawPlayButton(HDC hdc, const PlayButtonInfo& buttonInfo)
	{
		// 计算按钮中心
		int centerX = (buttonInfo.buttonRect.left + buttonInfo.buttonRect.right) / 2;
		int centerY = (buttonInfo.buttonRect.top + buttonInfo.buttonRect.bottom) / 2;
		int radius = (buttonInfo.buttonRect.right - buttonInfo.buttonRect.left) / 2;

		// 选择按钮背景颜色
		COLORREF bgColor = kPlayButtonColor;
		if (buttonInfo.isHovered)
		{
			bgColor = kPlayButtonHoverColor;
		}
		if (buttonInfo.isPlaying)
		{
			bgColor = kPlayButtonPlayingColor;
		}

		// 绘制圆形按钮背景
		DrawCircleButton(hdc, centerX, centerY, radius, bgColor);

		// 绘制图标（播放或停止）
		if (buttonInfo.isPlaying)
		{
			DrawStopIcon(hdc, centerX, centerY, radius - 5, kPlayButtonIconColor);
		}
		else
		{
			DrawPlayIcon(hdc, centerX, centerY, radius - 5, kPlayButtonIconColor);
		}
	}

	PlayButtonInfo CalculatePlayButtonRect(int baseX, int baseY, int buttonSize)
	{
		PlayButtonInfo info = {};
		info.buttonRect.left = baseX + kPlayButtonPadding;
		info.buttonRect.top = baseY + kPlayButtonPadding;
		info.buttonRect.right = info.buttonRect.left + buttonSize;
		info.buttonRect.bottom = info.buttonRect.top + buttonSize;
		return info;
	}

	bool IsPointInPlayButton(POINT pt, const PlayButtonInfo& buttonInfo)
	{
		// 计算按钮中心和半径
		int centerX = (buttonInfo.buttonRect.left + buttonInfo.buttonRect.right) / 2;
		int centerY = (buttonInfo.buttonRect.top + buttonInfo.buttonRect.bottom) / 2;
		int radius = (buttonInfo.buttonRect.right - buttonInfo.buttonRect.left) / 2;

		// 计算点到圆心的距离
		int dx = pt.x - centerX;
		int dy = pt.y - centerY;
		int distanceSquared = dx * dx + dy * dy;
		int radiusSquared = radius * radius;

		return distanceSquared <= radiusSquared;
	}

	bool InitializeAudioPlayer()
	{
		if (g_audioPlayer != nullptr)
			return true;

		g_audioPlayer = new AudioPlayer();
		if (!g_audioPlayer->Initialize())
		{
			delete g_audioPlayer;
			g_audioPlayer = nullptr;
			OutputDebugStringW(L"Failed to initialize audio player\n");
			return false;
		}

		OutputDebugStringW(L"Audio player initialized successfully\n");
		return true;
	}

	void CleanupAudioPlayer()
	{
		if (g_audioPlayer != nullptr)
		{
			delete g_audioPlayer;
			g_audioPlayer = nullptr;
		}
	}

	void HandlePlayButtonClick(HWND hwnd, const std::wstring& mp3FilePath)
	{
		if (g_audioPlayer == nullptr)
		{
			OutputDebugStringW(L"Audio player not initialized\n");
			return;
		}

		if (g_audioPlayer->GetPlaybackState() == PlaybackState::Playing)
		{
			// 正在播放，暂停
			g_audioPlayer->Pause();
		}
		else if (g_audioPlayer->GetPlaybackState() == PlaybackState::Paused)
		{
			// 暂停中，继续播放
			g_audioPlayer->Resume();
		}
		else
		{
			// 停止状态，播放新文件
			if (!g_audioPlayer->Play(mp3FilePath))
			{
				OutputDebugStringW(L"Failed to play audio file\n");
			}
		}

		// 重绘窗口以更新按钮状态
		InvalidateRect(hwnd, &g_playButtonInfo.buttonRect, FALSE);
	}

	void TogglePlayPause(HWND hwnd)
	{
		if (g_audioPlayer == nullptr)
			return;

		if (g_audioPlayer->GetPlaybackState() == PlaybackState::Playing)
		{
			g_audioPlayer->Pause();
		}
		else
		{
			g_audioPlayer->Resume();
		}

		// 重绘按钮
		InvalidateRect(hwnd, &g_playButtonInfo.buttonRect, FALSE);
	}

	AudioPlayer* GetAudioPlayer()
	{
		return g_audioPlayer;
	}

}
