#pragma once
#include <windows.h>
#include <windowsx.h>
#include <gdiplus.h>
#include "WindowGUI.h"
#include "MainWindow.h"
#include "Core/AudioPlayer.h"

namespace YuMediaPlayer
{
	struct ControlButtonInfo
	{
		Gdiplus::RectF rect;
		bool hovered = false;
		bool active = false;   // 选中状态。收藏按钮：true = 当前这首已收藏（画实心红心）；音量按钮：true = 已静音
	};
	// 播放按钮状态和位置信息
	struct PlayButtonInfo
	{
		RECT buttonRect;           // 按钮矩形区域
		bool isPlaying = false;    // 是否正在播放
		bool isHovered = false;    // 鼠标是否悬停在按钮上
	};
	// 所有播放按钮的信息集合
	struct PlaybackButtonsInfo
	{
		ControlButtonInfo previous;
		ControlButtonInfo play;
		ControlButtonInfo next;
		// 右上角按钮
		ControlButtonInfo close;    // 关闭按钮
		ControlButtonInfo mini;     // 最小化按钮
		// 右侧按钮
		ControlButtonInfo sound;    // 音量按钮
		ControlButtonInfo list;     // 列表按钮
		// 左侧按钮
		ControlButtonInfo heart;    // 收藏按钮
	};
	// 右键菜单的命令 ID，MainWindow 根据 ShowMiniPlayerContextMenu 的返回值
	// 判断用户点了哪一项，然后自己调用 HandleMiniPlayerContextMenuCommand 去执行
	// （ShowMiniPlayerContextMenu 内部只负责"循环模式"这种自己记状态的选项，不再重复执行命令）。
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

		// 圆形头像菜单命令（新增）
		constexpr UINT AvatarRotate = 3001;       // 旋转头像
		constexpr UINT AvatarStop = 3002;         // 停止旋转（复位）
		constexpr UINT AvatarRingYellow = 3003;   // 进度条：黄色
		constexpr UINT AvatarRingRainbow = 3004;  // 进度条：七彩
	}

	UINT GetCurrentLoopMode();

	void MeasureMiniPlayerMenuItem(MEASUREITEMSTRUCT& mis);
	void DrawMiniPlayerMenuItem(const DRAWITEMSTRUCT& dis);

	//extern YuMediaPlayer::WindowGUI* m_windowGUI;

	// 显示迷你播放器上下文菜单
	int ShowMiniPlayerContextMenu(HWND hwnd, POINT pt, YuMediaPlayer::WindowGUI* windowGUI = nullptr);
	void HandleMiniPlayerContextMenuCommand(HWND hwnd, int cmd, YuMediaPlayer::WindowGUI* windowGUI = nullptr);
	// 显示圆形头像的右键菜单
	int ShowAvatarContextMenu(HWND hwnd, POINT pt);
	// 处理圆形头像菜单命令
	void HandleAvatarContextMenuCommand(HWND hwnd, int cmd);
	// 用户在右键菜单里手动选"旋转"：清除"用户手动停止过"的标记并开始旋转
	void StartAvatarRotation(HWND hwnd);
	// 开始播放音乐时调用：自动让封面转起来。
	// 如果用户之前在右键菜单里手动选过"停止（复位）"，这里什么都不做（尊重用户的选择，
	// 直到他再次从右键菜单选"旋转"）。已经在转的话也什么都不做。
	void AutoStartAvatarRotation(HWND hwnd);
	// 用户在右键菜单里手动选"停止（复位）"：停止旋转、角度归零，并记住这是用户主动停的。
	// 只影响封面动画，完全不碰音乐播放。
	void StopAvatarRotation(HWND hwnd);
	// 处理旋转定时器（在 MainWindow 的 WM_TIMER 中调用）
	void HandleAvatarRotationTimer(HWND hwnd, MainWindow* pMainWindow);
	// 获取当前头像旋转角度
	float GetAvatarRotation();
	// 获取头像是否正在旋转
	bool IsAvatarRotating();
	// 进度条皮肤：true = 七彩，false = 黄色。右键菜单选择后由 HandleAvatarContextMenuCommand 更新，
	// MainWindow 据此设置 CircularAvatar 的皮肤。
	bool IsRainbowRing();

	
	// ===== GDI+ 按钮绘制函数 =====

	// 绘制播放/暂停按钮
	void DrawPlayButtonGdiplus(Gdiplus::Graphics& g, const Gdiplus::RectF& vRect, bool isPlaying, bool hovered);
	// 绘制上一曲按钮
	void DrawPreviousButtonGdiplus(Gdiplus::Graphics& g, const Gdiplus::RectF& vRect, bool hovered);
	// 绘制下一曲按钮
	void DrawNextButtonGdiplus(Gdiplus::Graphics& g, const Gdiplus::RectF& vRect, bool hovered);
	// 绘制所有播放控制按钮
	void DrawPlaybackButtonsGdiplus(Gdiplus::Graphics& g, const Gdiplus::RectF& vRect, const Gdiplus::RectF& avatarRect, PlaybackButtonsInfo& buttonInfo);
	// 绘制关闭按钮
	void DrawCloseButtonGdiplus(Gdiplus::Graphics& g, const Gdiplus::RectF& vRect, bool hovered);
	// 绘制最小化按钮
	void DrawMiniButtonGdiplus(Gdiplus::Graphics& g, const Gdiplus::RectF& vRect, bool hovered);
	// 绘制音量按钮
	// muted=true 时喇叭不画音波，改画一个 ×（静音状态）
	void DrawSoundButtonGdiplus(Gdiplus::Graphics& g, const Gdiplus::RectF& vRect, bool hovered, bool muted = false);
	// 绘制列表按钮
	void DrawListButtonGdiplus(Gdiplus::Graphics& g, const Gdiplus::RectF& vRect, bool hovered);
	// 绘制收藏按钮：favorited=false 画空心描边心形，favorited=true 画实心红心
	void DrawHeartButtonGdiplus(Gdiplus::Graphics& g, const Gdiplus::RectF& vRect, bool hovered, bool favorited = false);

	// ===== 按钮点击处理函数 =====

	void HandleCloseButtonClick(HWND hwnd);
	void HandleMiniButtonClick(HWND hwnd, WindowGUI* windowGUI);
	void HandleSoundButtonClick(HWND hwnd);
	// 静音状态：点音量按钮在静音/恢复之间切换；程序启动/退出时会复位成"未静音"
	bool IsSoundMuted();
	void SetSoundMuted(bool muted);
	void HandleListButtonClick(HWND hwnd);
	void HandleHeartButtonClick(HWND hwnd);
	void HandlePreviousButtonClick(HWND hwnd);
	void HandleNextButtonClick(HWND hwnd);
	void HandlePlayButtonClick(HWND hwnd, const std::wstring& mp3FilePath);

	// ===== GDI 风格的播放按钮函数 =====
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
