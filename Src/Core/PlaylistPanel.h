#pragma once
#include <windows.h>
#include <gdiplus.h>
#include "Core/Playlist.h"

namespace YuMediaPlayer
{
	// 展开式播放列表面板：画在播放器卡片下面，一行一首歌（序号/歌名/歌手）。
	//
	// 跟 CircularAvatar 一样，这个类不是独立窗口，只负责
	//   1) 画到调用者给的 Graphics 上（Draw）
	//   2) 把窗口客户区坐标翻译成"点中了第几首歌"（HitTestTrack）
	//   3) 自己的展开/滚动/悬停状态
	// 窗口变高、重绘（Composite）、点击后播放哪首，都由 MainWindow 负责。
	class PlaylistPanel
	{
	public:
		bool IsOpen() const { return m_open; }
		void SetOpen(bool open);

		// 展开时，窗口需要比"播放器本体"多出多少像素来放列表。
		int ExtraHeight(int trackCount) const;

		// 画面板。winW = 窗口宽度；baseH = 播放器本体高度（面板紧贴在它下面）。
		// 没展开时什么都不画。
		void Draw(Gdiplus::Graphics& g, const Playlist& playlist, float winW, float baseH) const;

		// pt 是窗口客户区坐标。落在某一行上返回该歌曲在 playlist 里的索引，否则 -1。
		int HitTestTrack(const Playlist& playlist, float winW, float baseH, POINT pt) const;

		// 更新鼠标悬停的那一行；悬停行变了返回 true（调用者据此决定要不要重绘）。
		bool UpdateHover(const Playlist& playlist, float winW, float baseH, POINT pt);
		void ClearHover() { m_hoverTrack = -1; }

		// 滚动 rows 行（正数向下）。位置变了返回 true。
		bool Scroll(const Playlist& playlist, int rows);

		// 保证当前播放的那首在可见范围内（切歌、展开时调用）。
		void EnsureVisible(const Playlist& playlist);

	private:
		bool m_open = false;
		int  m_scroll = 0;        // 第一行可见的曲目索引
		int  m_hoverTrack = -1;   // 鼠标悬停的曲目索引，-1 = 没有
	};
}
