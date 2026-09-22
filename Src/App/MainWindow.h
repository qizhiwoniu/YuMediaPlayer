#pragma once 
#include <windows.h>
#include <memory>  
#include <string>
#include <gdiplus.h>
#include "NotifyIcon/TrayIcon.h"
#include "Core/Theme.h"
#include "Core/CircularAvatar.h"

class TrayIcon;

namespace YuMediaPlayer
{
	class WindowGUI;

	class MainWindow
	{
	public:
		MainWindow();
		~MainWindow();
		bool Initialize(const wchar_t* title, int width, int height);
		void Run();

		// 加载/更换圆形封面图（jpg/png/bmp 等 GDI+ 支持的格式）
		void SetCoverImage(const std::wstring& path);
		// 更新播放进度，progress01 范围 [0, 1]，驱动封面外圈的进度环
		void SetPlayProgress(float progress01);
		// 换肤：自定义进度环颜色/粗细
		void SetAvatarSkin(const CircularAvatar::Skin& skin);
		// 设置当前歌曲信息（歌名/歌手），显示在封面右侧
		void SetTrackInfo(const std::wstring& title, const std::wstring& artist);

	private:
		// 把卡片(圆角面板)、头像(进度环+封面)、文字，全部画到 m_dibBits 这张
		// 带 Alpha 通道的位图上，再用 UpdateLayeredWindow 一次性推给系统显示。
		// 这是整个"头像探出卡片顶部、探出部分背后透明"效果的核心。
		void Composite();
		// 按当前窗口尺寸(重新)创建/复用合成用的 32bpp DIB 位图
		bool EnsureLayeredBitmap(int width, int height);
		// 在卡片右侧、头像旁边画歌名/歌手文字
		void DrawTrackInfoGdiplus(Gdiplus::Graphics& g, const Gdiplus::RectF& cardRect, const Gdiplus::RectF& avatarRect);
	private:
		static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
		LRESULT EventProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
		bool    InitWindow(const wchar_t* title, int width, int height);

		enum class CollapseEdge
		{
			None,
			Left,
			Right,
			Top,
			Bottom
		};

		// 检查并处理屏幕边缘吸附收起
		void CheckAndCollapseAtEdge();
		void CollapseAtEdgeRect(const RECT& wr);
		// 恢复窗口到原始大小
		void RestoreWindow();
		void CheckCollapsedMouseHover();
		void CheckAutoCollapse();

		// 动画相关函数
		void StartCollapseAnimation();   // 开始收起动画
		void StartExpandAnimation();     // 开始展开动画
		void UpdateCollapseAnimation();  // 更新动画帧

	private:


		// 窗口
		HWND						  m_hwnd;
		Theme						  m_theme;

		TrayIcon                      m_trayIcon;

		CircularAvatar                m_avatar;       // 圆形封面 + 播放进度环（现在只是绘制器，不再是子窗口）
		Gdiplus::RectF                m_avatarRectF{}; // 头像圆形在客户区中的位置（合成时算出来，点击检测/文字定位都用它）
		std::wstring                  m_trackTitle;   // 歌名
		std::wstring                  m_trackArtist;  // 歌手

		// ── 分层窗口合成用的位图（32bpp, 预乘 Alpha）──────────────
		HBITMAP                       m_dibSection = nullptr;
		void* m_dibBits = nullptr;
		int                           m_bitmapW = 0;
		int                           m_bitmapH = 0;

		// ── 屏幕边缘吸附收起功能 ──────────────────────────────────────
		bool                          m_isCollapsed = false;    // 是否已收起
		CollapseEdge                  m_collapsedEdge = CollapseEdge::None; // 当前收起边缘
		int                           m_savedWindowWidth = 0;   // 原始窗口宽度
		int                           m_savedWindowHeight = 0;  // 原始窗口高度
		int                           m_savedWindowX = 0;       // 原始窗口X坐标
		int                           m_savedWindowY = 0;       // 原始窗口Y坐标
		bool                          m_isMoving = false;       // 是否正在拖动（用于检测拖离边缘）
		UINT_PTR                      m_edgeHoverTimer = 0;      // 边缘自动收起/恢复定时器
		DWORD                         m_lastMouseMoveTick = 0;   // 最近一次鼠标移动时间
		POINT                         m_lastMousePos{};          // 最近一次鼠标位置
		bool                          m_isCompositing = false;   // 防止 SetWindowPos/WM_SIZE 导致 Composite 重入

		// ── 收起/展开动画 ────────────────────────────────────────────
		bool                          m_isAnimating = false;     // 是否正在动画中
		UINT_PTR                      m_animationTimer = 0;      // 动画定时器
		float                         m_animationProgress = 0.0f; // 动画进度 [0, 1]
		int                           m_animationTargetWidth = 0; // 动画目标宽度
		int                           m_animationStartWidth = 0;  // 动画起始宽度
		bool                          m_animationIsCollapsing = false; // true = 收起动画，false = 展开动画

	private:
		//std::unique_ptr<TrayIcon> m_trayIcon;
		std::unique_ptr<YuMediaPlayer::WindowGUI> m_windowGUI;
	};
}