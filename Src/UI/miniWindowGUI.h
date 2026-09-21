#pragma once
#include <windows.h>
#include <windowsx.h>
#include "WindowGUI.h"
#include "Core/AudioPlayer.h"

namespace YuMediaPlayer
{
	// 播放按钮状态和位置信息
	struct PlayButtonInfo
	{
		RECT buttonRect;           // 按钮矩形区域
		bool isPlaying = false;    // 是否正在播放
		bool isHovered = false;    // 鼠标是否悬停在按钮上
	};
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

	UINT GetCurrentLoopMode();

	void MeasureMiniPlayerMenuItem(MEASUREITEMSTRUCT& mis);
	void DrawMiniPlayerMenuItem(const DRAWITEMSTRUCT& dis);

	//extern YuMediaPlayer::WindowGUI* m_windowGUI;

	// 显示迷你播放器上下文菜单
	int ShowMiniPlayerContextMenu(HWND hwnd, POINT pt, YuMediaPlayer::WindowGUI* windowGUI = nullptr);
	void HandleMiniPlayerContextMenuCommand(HWND hwnd, int cmd, YuMediaPlayer::WindowGUI* windowGUI = nullptr);

	// ===== 新增：播放按钮相关函数 =====

	// 在给定的 HDC 上绘制播放按钮
	void DrawPlayButton(HDC hdc, const PlayButtonInfo& buttonInfo);

	// 计算播放按钮的矩形区域
	// baseX, baseY 是参考点（通常是窗口客户区左上角）
	// buttonSize 是按钮大小（宽度和高度相同）
	PlayButtonInfo CalculatePlayButtonRect(int baseX, int baseY, int buttonSize);

	// 检查点是否在播放按钮内
	bool IsPointInPlayButton(POINT pt, const PlayButtonInfo& buttonInfo);

	// 初始化音频播放器（在主窗口初始化时调用）
	bool InitializeAudioPlayer();

	// 清理音频播放器资源
	void CleanupAudioPlayer();

	// 处理播放按钮点击
	void HandlePlayButtonClick(HWND hwnd, const std::wstring& mp3FilePath);

	// 切换播放/暂停状态
	void TogglePlayPause(HWND hwnd);

	// 获取全局音频播放器实例
	AudioPlayer* GetAudioPlayer();
}
