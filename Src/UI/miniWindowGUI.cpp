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


	void DrawPlayButtonGdiplus(Gdiplus::Graphics& g, const Gdiplus::RectF& vRect, bool isPlaying, bool hovered)
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
	void DrawPreviousButtonGdiplus(Gdiplus::Graphics& g, const Gdiplus::RectF& vRect, bool hovered)
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
	void DrawNextButtonGdiplus(Gdiplus::Graphics& g, const Gdiplus::RectF& vRect, bool hovered)
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
	void DrawPlaybackButtonsGdiplus(Gdiplus::Graphics& g, const Gdiplus::RectF& vRect, const Gdiplus::RectF& avatarRect, PlaybackButtonsInfo& buttonInfo)
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
		buttonInfo.heart.rect = heartRect;
		buttonInfo.sound.rect = soundRect;
		buttonInfo.list.rect = listRect;
		buttonInfo.close.rect = closeRect;
		buttonInfo.mini.rect = miniRect;
		buttonInfo.previous.rect = previousRect;
		buttonInfo.next.rect = nextRect;
		DrawHeartButtonGdiplus(g, heartRect, buttonInfo.heart.hovered);
		DrawSoundButtonGdiplus(g, soundRect, buttonInfo.sound.hovered);
		DrawListButtonGdiplus(g, listRect, buttonInfo.list.hovered);
		DrawCloseButtonGdiplus(g, closeRect, buttonInfo.close.hovered);
		DrawMiniButtonGdiplus(g, miniRect, buttonInfo.mini.hovered);
		DrawPreviousButtonGdiplus(g, previousRect, buttonInfo.previous.hovered);
		DrawNextButtonGdiplus(g, nextRect, buttonInfo.next.hovered);
	}
	void DrawCloseButtonGdiplus(Gdiplus::Graphics& g, const Gdiplus::RectF& vRect, bool hovered)
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
	void DrawMiniButtonGdiplus(Gdiplus::Graphics& g, const Gdiplus::RectF& vRect, bool hovered)
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
	void DrawSoundButtonGdiplus(Gdiplus::Graphics& g, const Gdiplus::RectF& vRect, bool hovered)
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
	void DrawListButtonGdiplus(Gdiplus::Graphics& g, const Gdiplus::RectF& vRect, bool hovered)
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
	void DrawHeartButtonGdiplus(Gdiplus::Graphics& g, const Gdiplus::RectF& vRect, bool hovered)
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
	void HandleCloseButtonClick(HWND hwnd)
	{
		OutputDebugStringW(L"[Button Click] Close\n");

		// 关闭程序
		SendMessage(hwnd, WM_CLOSE, 0, 0);
	}
	void HandleMiniButtonClick(HWND hwnd, WindowGUI* windowGUI)
	{
		OutputDebugStringW(L"[Button Click] Minimize\n");

		// 隐藏窗口，显示主窗口 GUI
		if (m_windowGUI)
		{
			ShowWindow(hwnd, SW_HIDE);
			m_windowGUI->ShowWindowGUI();
		}
	}
	void HandleHeartButtonClick(HWND hwnd)
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
	void HandleSoundButtonClick(HWND hwnd)
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
	void HandleListButtonClick(HWND hwnd)
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
