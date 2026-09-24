#include "Core/PlaylistPanel.h"
#include <string>

// 注意：这里不能定义 NOMINMAX——GDI+ 的头文件依赖 min/max 宏。
// 下面需要取最小值/最大值的地方都用普通的条件表达式，避免碰宏。

using namespace Gdiplus;

namespace YuMediaPlayer
{
	namespace
	{
		constexpr int   kMaxVisibleRows = 10;     // 最多同时显示几行；超过 10 首时最上面的被挤出去，用滚轮滚动查看
		constexpr float kRowH = 28.0f;            // 每行高度
		constexpr float kHeaderH = 30.0f;         // 标题栏高度（"播放列表  N 首"）
		constexpr float kPadBottom = 6.0f;        // 最后一行下面的留白
		constexpr float kSidePad = 6.0f;          // 行区域左右留白
		constexpr float kPanelOverlap = 6.0f;     // 面板顶边比播放器本体底边高 6px：
		                                          // 卡片底边距窗口底 10px，这样卡片和面板之间留 4px 缝
		constexpr float kWindowBottomMargin = 10.0f; // 面板底边距窗口底边（跟卡片一致）
		constexpr float kScrollBarW = 8.0f;       // 需要滚动条时，给它留的宽度

		const Color kPanelColor(255, 40, 40, 40);
		const Color kHoverColor(255, 58, 58, 58);
		const Color kAccentColor(255, 255, 205, 60);   // 跟进度环的"已播放"颜色一致
		const Color kTextColor(255, 235, 235, 235);
		const Color kSubTextColor(255, 150, 150, 150);

		struct Layout
		{
			RectF panel;       // 整个面板（圆角矩形）
			RectF rowsArea;    // 放歌曲行的区域
			int   count = 0;
			int   visibleRows = 0;
		};

		Layout MakeLayout(float winW, float baseH, int count)
		{
			Layout L;
			L.count = count;
			L.visibleRows = count < kMaxVisibleRows ? count : kMaxVisibleRows;

			// 列表为空时也留一行的高度，用来显示"没有找到歌曲"的提示。
			const int rowsForHeight = L.visibleRows > 0 ? L.visibleRows : 1;
			const float inner = kHeaderH + rowsForHeight * kRowH + kPadBottom;

			L.panel = RectF(10.0f, baseH - kPanelOverlap, winW - 20.0f, inner);
			L.rowsArea = RectF(L.panel.X + kSidePad, L.panel.Y + kHeaderH,
				L.panel.Width - kSidePad * 2.0f, rowsForHeight * kRowH);
			return L;
		}

		void AddRoundedRect(GraphicsPath& path, const RectF& r, float radius)
		{
			const float d = radius * 2.0f;
			path.AddArc(r.X, r.Y, d, d, 180.0f, 90.0f);
			path.AddArc(r.GetRight() - d, r.Y, d, d, 270.0f, 90.0f);
			path.AddArc(r.GetRight() - d, r.GetBottom() - d, d, d, 0.0f, 90.0f);
			path.AddArc(r.X, r.GetBottom() - d, d, d, 90.0f, 90.0f);
			path.CloseFigure();
		}
	}

	void PlaylistPanel::SetOpen(bool open)
	{
		m_open = open;
		if (!open)
			m_hoverTrack = -1;
	}

	int PlaylistPanel::ExtraHeight(int trackCount) const
	{
		const int visible = trackCount < kMaxVisibleRows ? trackCount : kMaxVisibleRows;
		const int rowsForHeight = visible > 0 ? visible : 1;
		const float inner = kHeaderH + rowsForHeight * kRowH + kPadBottom;
		// 窗口底边 = 面板底边 + 底部留白 = (baseH - overlap + inner) + margin
		return static_cast<int>(inner - kPanelOverlap + kWindowBottomMargin);
	}

	void PlaylistPanel::Draw(Graphics& g, const Playlist& playlist, float winW, float baseH) const
	{
		if (!m_open)
			return;

		const int count = playlist.Count();
		const Layout L = MakeLayout(winW, baseH, count);

		// 背景
		{
			GraphicsPath path;
			AddRoundedRect(path, L.panel, 12.0f);
			SolidBrush bg(kPanelColor);
			g.FillPath(&bg, &path);
		}

		Font headerFont(L"Microsoft YaHei UI", 13.0f, FontStyleBold, UnitPixel);
		Font rowFont(L"Microsoft YaHei UI", 12.0f, FontStyleRegular, UnitPixel);

		StringFormat left;
		left.SetAlignment(StringAlignmentNear);
		left.SetLineAlignment(StringAlignmentCenter);
		left.SetTrimming(StringTrimmingEllipsisCharacter);   // 太长的歌名用 ... 截断，不会画出面板
		left.SetFormatFlags(StringFormatFlagsNoWrap);

		StringFormat center;
		center.SetAlignment(StringAlignmentCenter);
		center.SetLineAlignment(StringAlignmentCenter);
		center.SetFormatFlags(StringFormatFlagsNoWrap);

		SolidBrush textBrush(kTextColor);
		SolidBrush subBrush(kSubTextColor);
		SolidBrush accentBrush(kAccentColor);

		// 标题栏 + 分隔线
		{
			std::wstring title = L"播放列表";
			if (count > 0)
				title += L"  " + std::to_wstring(count) + L" 首";
			RectF headerRect(L.panel.X + 14.0f, L.panel.Y + 2.0f, L.panel.Width - 28.0f, kHeaderH - 4.0f);
			g.DrawString(title.c_str(), -1, &headerFont, headerRect, &left, &textBrush);

			Pen sepPen(Color(50, 255, 255, 255), 1.0f);
			const float sepY = L.panel.Y + kHeaderH - 1.0f;
			g.DrawLine(&sepPen, L.panel.X + 12.0f, sepY, L.panel.GetRight() - 12.0f, sepY);
		}

		// 空列表提示
		if (count == 0)
		{
			g.DrawString(L"没有找到歌曲，请把音频放进 song\\local 文件夹", -1, &rowFont,
				L.rowsArea, &center, &subBrush);
			return;
		}

		const bool needScrollBar = count > L.visibleRows;
		const float rowsWidth = L.rowsArea.Width - (needScrollBar ? kScrollBarW : 0.0f);
		const int current = playlist.CurrentIndex();

		for (int r = 0; r < L.visibleRows; ++r)
		{
			const int idx = m_scroll + r;
			const Track* track = playlist.At(idx);
			if (!track)
				break;

			const RectF row(L.rowsArea.X, L.rowsArea.Y + r * kRowH, rowsWidth, kRowH);
			const bool isCurrent = (idx == current);

			if (idx == m_hoverTrack)
			{
				SolidBrush hoverBrush(kHoverColor);
				g.FillRectangle(&hoverBrush, row);
			}

			// 左边：当前播放的显示一个小三角，其它显示序号
			if (isCurrent)
			{
				const float cx = row.X + 14.0f;
				const float cy = row.Y + row.Height / 2.0f;
				PointF tri[3] = { PointF(cx - 3.5f, cy - 5.0f), PointF(cx - 3.5f, cy + 5.0f), PointF(cx + 5.0f, cy) };
				g.FillPolygon(&accentBrush, tri, 3);
			}
			else
			{
				RectF numRect(row.X + 2.0f, row.Y, 24.0f, row.Height);
				const std::wstring num = std::to_wstring(idx + 1);
				g.DrawString(num.c_str(), -1, &rowFont, numRect, &center, &subBrush);
			}

			// 右边：歌名 + 歌手（有歌手时歌名占 60%，歌手占剩下的）
			const float textX = row.X + 30.0f;
			const float textW = row.GetRight() - textX - 8.0f;
			const bool hasArtist = !track->artist.empty();
			const float titleW = hasArtist ? textW * 0.60f : textW;

			SolidBrush& titleBrush = isCurrent ? accentBrush : textBrush;
			g.DrawString(track->title.c_str(), -1, &rowFont,
				RectF(textX, row.Y, titleW, row.Height), &left, &titleBrush);

			if (hasArtist)
			{
				SolidBrush& artistBrush = isCurrent ? accentBrush : subBrush;
				g.DrawString(track->artist.c_str(), -1, &rowFont,
					RectF(textX + titleW + 6.0f, row.Y, textW - titleW - 6.0f, row.Height), &left, &artistBrush);
			}
		}

		// 滚动条
		if (needScrollBar)
		{
			const float trackX = L.panel.GetRight() - kScrollBarW - 2.0f;
			const float trackH = L.visibleRows * kRowH;

			float thumbH = trackH * L.visibleRows / count;
			if (thumbH < 14.0f)
				thumbH = 14.0f;

			const int maxScroll = count - L.visibleRows;
			const float thumbY = L.rowsArea.Y + (trackH - thumbH) * (maxScroll > 0 ? (float)m_scroll / maxScroll : 0.0f);

			SolidBrush thumbBrush(Color(90, 255, 255, 255));
			GraphicsPath thumbPath;
			AddRoundedRect(thumbPath, RectF(trackX + 2.0f, thumbY, 3.0f, thumbH), 1.5f);
			g.FillPath(&thumbBrush, &thumbPath);
		}
	}

	int PlaylistPanel::HitTestTrack(const Playlist& playlist, float winW, float baseH, POINT pt) const
	{
		if (!m_open || playlist.Count() == 0)
			return -1;

		const Layout L = MakeLayout(winW, baseH, playlist.Count());
		const RectF rows(L.rowsArea.X, L.rowsArea.Y, L.rowsArea.Width, L.visibleRows * kRowH);
		if (!rows.Contains(static_cast<REAL>(pt.x), static_cast<REAL>(pt.y)))
			return -1;

		const int r = static_cast<int>((pt.y - rows.Y) / kRowH);
		const int idx = m_scroll + r;
		return (idx >= 0 && idx < playlist.Count()) ? idx : -1;
	}

	bool PlaylistPanel::UpdateHover(const Playlist& playlist, float winW, float baseH, POINT pt)
	{
		const int idx = HitTestTrack(playlist, winW, baseH, pt);
		if (idx == m_hoverTrack)
			return false;
		m_hoverTrack = idx;
		return true;
	}

	bool PlaylistPanel::Scroll(const Playlist& playlist, int rows)
	{
		const int count = playlist.Count();
		const int visible = count < kMaxVisibleRows ? count : kMaxVisibleRows;
		int maxScroll = count - visible;
		if (maxScroll < 0)
			maxScroll = 0;

		int next = m_scroll + rows;
		if (next < 0) next = 0;
		if (next > maxScroll) next = maxScroll;

		if (next == m_scroll)
			return false;
		m_scroll = next;
		return true;
	}

	void PlaylistPanel::EnsureVisible(const Playlist& playlist)
	{
		const int count = playlist.Count();
		if (count == 0)
		{
			m_scroll = 0;
			return;
		}

		const int visible = count < kMaxVisibleRows ? count : kMaxVisibleRows;
		const int current = playlist.CurrentIndex();

		if (current < m_scroll)
			m_scroll = current;
		else if (current >= m_scroll + visible)
			m_scroll = current - visible + 1;

		int maxScroll = count - visible;
		if (maxScroll < 0) maxScroll = 0;
		if (m_scroll > maxScroll) m_scroll = maxScroll;
		if (m_scroll < 0) m_scroll = 0;
	}
}
