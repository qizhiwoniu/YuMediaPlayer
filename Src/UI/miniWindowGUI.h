#pragma once
#include <windows.h>
#include <windowsx.h>

namespace YuMediaPlayer
{
	// 右键菜单的命令 ID，MainWindow 根据 ShowMiniPlayerContextMenu 的返回值
	// 判断用户点了哪一项。
	namespace ContextMenuCommand
	{
		constexpr UINT MenuItem1 = 1001;
		constexpr UINT MenuItem2 = 1002;
		constexpr UINT MenuItem3 = 1003;
		constexpr UINT MenuItem4 = 1004;
		constexpr UINT Exit = 1005;

		// "循环模式"子菜单里的选项
		constexpr UINT LoopModeListLoop = 2001;   // 列表循环
		constexpr UINT LoopModeSingleLoop = 2002; // 单曲循环
		constexpr UINT LoopModeShuffle = 2003;    // 随机播放
		constexpr UINT LoopModeHeart = 2004;      // 心动循环
	}

	// 弹出 mini 播放器的右键菜单（内含"循环模式"子菜单，会自动带箭头）。
	// pt 必须是屏幕坐标（WM_CONTEXTMENU / WM_NCRBUTTONUP 的 lParam 本身就是屏幕坐标，
	// 直接用 GET_X_LPARAM/GET_Y_LPARAM 取出来传进来即可，不需要 ClientToScreen）。
	// 返回被选中的命令 ID（见 ContextMenuCommand），用户点了菜单外区域取消则返回 0。
	// 如果选中的是循环模式子菜单里的某一项，内部会记住这个选择，
	// 下次再弹菜单时该项会带勾选标记，可以用 GetCurrentLoopMode() 查询当前是哪个模式。
	int ShowMiniPlayerContextMenu(HWND hwnd, POINT pt);

	// 当前选中的循环模式（ContextMenuCommand::LoopMode* 系列值之一），
	// 默认是 LoopModeListLoop。真正切歌逻辑可以用这个来判断下一首怎么选。
	UINT GetCurrentLoopMode();

	// 菜单是黑色背景（自绘/owner-draw），需要拥有者窗口（也就是传给
	// ShowMiniPlayerContextMenu 的 hwnd 所在的 WndProc/EventProc）把
	// WM_MEASUREITEM 和 WM_DRAWITEM 转发到这两个函数，菜单才画得出来。
	// 子菜单（循环模式）里的项也是同一套机制，不需要额外处理。
	// 例如：
	//   case WM_MEASUREITEM:
	//       MeasureMiniPlayerMenuItem(*reinterpret_cast<MEASUREITEMSTRUCT*>(lParam));
	//       return TRUE;
	//   case WM_DRAWITEM:
	//       DrawMiniPlayerMenuItem(*reinterpret_cast<const DRAWITEMSTRUCT*>(lParam));
	//       return TRUE;
	void MeasureMiniPlayerMenuItem(MEASUREITEMSTRUCT& mis);
	void DrawMiniPlayerMenuItem(const DRAWITEMSTRUCT& dis);
}
