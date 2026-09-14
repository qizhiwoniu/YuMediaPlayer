#include "miniWindowGUI.h"

namespace YuMediaPlayer
{
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

		// 当前选中的循环模式，默认列表循环。点了子菜单里某一项之后会更新这里，
		// 下次弹菜单时对应项会带勾选标记。
		UINT s_currentLoopMode = ContextMenuCommand::LoopModeListLoop;

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

	UINT GetCurrentLoopMode()
	{
		return s_currentLoopMode;
	}

	int ShowMiniPlayerContextMenu(HWND hwnd, POINT pt)
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

		return cmd;
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
}
