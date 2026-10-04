#include <windows.h>
#include <windowsx.h>
#include <memory>
#include <cmath>
#include <vector>
#include <dwmapi.h>
#include <gdiplus.h>
#include "Core/Theme.h"
#include "WindowGUI.h"
#include "Core/SettingPage.h"
#include <set>
#include "DesktopLyric.h"   // 桌面歌词 + 歌词下载目录 Assets\lrc
#include <thread>
#include <shlobj.h>
#include <knownfolders.h>
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#include "OnlineMusic.h"   // 在线歌曲数据源（WinHTTP + iTunes Search）

#pragma comment(lib, "dwmapi.lib")
#ifndef EM_SETCUEBANNER
#define EM_SETCUEBANNER (0x1500 + 1)   // ECM_FIRST + 1，不引入 commctrl.h 也能用
#endif
#pragma comment(lib, "gdiplus.lib")
namespace YuMediaPlayer
{
	namespace
	{
		void BuildRoundRectPath(Gdiplus::GraphicsPath& path, const Gdiplus::Rect& rect, int radius)
		{
			int d = radius * 2;
			if (d > rect.Width)  d = rect.Width;
			if (d > rect.Height) d = rect.Height;

			path.Reset();
			path.AddArc(rect.X, rect.Y, d, d, 180, 90);
			path.AddArc(rect.X + rect.Width - d, rect.Y, d, d, 270, 90);
			path.AddArc(rect.X + rect.Width - d, rect.Y + rect.Height - d, d, d, 0, 90);
			path.AddArc(rect.X, rect.Y + rect.Height - d, d, d, 90, 90);
			path.CloseFigure();
		}
		// ---- 主题：0=深色 1=浅色（由设置页写入 UITheme，改了会自动重绘）----
		// 灰度色：深色主题用 dark，浅色主题用 light
		Gdiplus::Color ThemeGray(int dark, int light, int alpha = 255)
		{
			int v = UITheme::IsLight() ? light : dark;
			return Gdiplus::Color((BYTE)alpha, (BYTE)v, (BYTE)v, (BYTE)v);
		}
		COLORREF ThemeRef(int dark, int light)
		{
			int v = UITheme::IsLight() ? light : dark;
			return RGB(v, v, v);
		}
		// 强调色：深色=亮黄，浅色=琥珀色（亮黄在浅色背景上看不清）
		Gdiplus::Color ThemeAccent()
		{
			return UITheme::IsLight() ? Gdiplus::Color(255, 184, 128, 0) : Gdiplus::Color(255, 255, 204, 0);
		}

		// 圆形播放/暂停按钮：浅色圆底 + 深色三角形/暂停条
		void DrawPlayPauseGlyph(Gdiplus::Graphics& g, float cx, float cy, float radius, bool isPlaying)
		{
			Gdiplus::SolidBrush circleBrush(ThemeGray(235, 40));
			g.FillEllipse(&circleBrush, cx - radius, cy - radius, radius * 2, radius * 2);

			Gdiplus::SolidBrush glyphBrush(ThemeGray(28, 245));

			if (isPlaying)
			{
				float barW = radius * 0.24f;
				float barH = radius * 0.85f;
				float gap = radius * 0.18f;
				g.FillRectangle(&glyphBrush, Gdiplus::RectF(cx - gap - barW, cy - barH / 2, barW, barH));
				g.FillRectangle(&glyphBrush, Gdiplus::RectF(cx + gap, cy - barH / 2, barW, barH));
			}
			else
			{
				float w = radius * 0.9f;
				float h = radius * 1.0f;
				Gdiplus::PointF pts[3] = {
					Gdiplus::PointF(cx - w * 0.32f, cy - h / 2),
					Gdiplus::PointF(cx - w * 0.32f, cy + h / 2),
					Gdiplus::PointF(cx + w * 0.55f, cy)
				};
				g.FillPolygon(&glyphBrush, pts, 3);
			}
		}
		// 上一曲/下一曲：三角形 + 竖条
		void DrawSkipGlyph(Gdiplus::Graphics& g, float cx, float cy, float size, bool isNext, Gdiplus::Color color)
		{
			Gdiplus::SolidBrush brush(color);
			float triW = size * 0.5f;
			float triH = size * 0.85f;
			float barW = size * 0.16f;
			float gap = size * 0.10f;

			if (isNext)
			{
				Gdiplus::PointF tri[3] = {
					Gdiplus::PointF(cx - triW / 2, cy - triH / 2),
					Gdiplus::PointF(cx - triW / 2, cy + triH / 2),
					Gdiplus::PointF(cx + triW / 2, cy)
				};
				g.FillPolygon(&brush, tri, 3);
				g.FillRectangle(&brush, Gdiplus::RectF(cx + triW / 2 + gap, cy - triH / 2, barW, triH));
			}
			else
			{
				Gdiplus::PointF tri[3] = {
					Gdiplus::PointF(cx + triW / 2, cy - triH / 2),
					Gdiplus::PointF(cx + triW / 2, cy + triH / 2),
					Gdiplus::PointF(cx - triW / 2, cy)
				};
				g.FillPolygon(&brush, tri, 3);
				g.FillRectangle(&brush, Gdiplus::RectF(cx - triW / 2 - gap - barW, cy - triH / 2, barW, triH));
			}
		}
		// 循环模式图标：0=列表循环 1=单曲循环(带"1") 2=随机播放(交叉箭头) 3=心动循环
		void DrawLoopGlyph(Gdiplus::Graphics& g, float cx, float cy, float size, int mode, Gdiplus::Color color)
		{
			Gdiplus::Pen pen(color, size * 0.11f);
			pen.SetStartCap(Gdiplus::LineCapRound);
			pen.SetEndCap(Gdiplus::LineCapRound);
			Gdiplus::SolidBrush brush(color);
			if (mode == 3)
			{

			}
			if (mode == 2)
			{
				float half = size * 0.42f;
				g.DrawLine(&pen, cx - half, cy - half * 0.55f, cx + half, cy + half * 0.55f);
				g.DrawLine(&pen, cx - half, cy + half * 0.55f, cx + half, cy - half * 0.55f);

				float ah = size * 0.16f;
				Gdiplus::PointF arrowUp[3] = {
					Gdiplus::PointF(cx + half - ah, cy - half * 0.55f - ah),
					Gdiplus::PointF(cx + half,      cy - half * 0.55f),
					Gdiplus::PointF(cx + half - ah, cy - half * 0.55f + ah)
				};
				g.FillPolygon(&brush, arrowUp, 3);

				Gdiplus::PointF arrowDown[3] = {
					Gdiplus::PointF(cx + half - ah, cy + half * 0.55f - ah),
					Gdiplus::PointF(cx + half,      cy + half * 0.55f),
					Gdiplus::PointF(cx + half - ah, cy + half * 0.55f + ah)
				};
				g.FillPolygon(&brush, arrowDown, 3);
				return;
			}

			float halfW = size * 0.42f;
			float halfH = size * 0.30f;

			g.DrawLine(&pen, cx - halfW * 0.5f, cy - halfH, cx + halfW * 0.5f, cy - halfH);
			g.DrawLine(&pen, cx - halfW * 0.5f, cy + halfH, cx + halfW * 0.5f, cy + halfH);

			Gdiplus::RectF rightArc(cx + halfW * 0.5f - halfH, cy - halfH, halfH * 2, halfH * 2);
			g.DrawArc(&pen, rightArc, -90, 180);

			Gdiplus::RectF leftArc(cx - halfW * 0.5f - halfH, cy - halfH, halfH * 2, halfH * 2);
			g.DrawArc(&pen, leftArc, 90, 180);

			float ah = size * 0.14f;
			Gdiplus::PointF arrowR[3] = {
				Gdiplus::PointF(cx + halfW * 0.5f - ah, cy - halfH - ah * 0.8f),
				Gdiplus::PointF(cx + halfW * 0.5f - ah, cy - halfH + ah * 0.8f),
				Gdiplus::PointF(cx + halfW * 0.5f + ah * 0.6f, cy - halfH)
			};
			g.FillPolygon(&brush, arrowR, 3);

			if (mode == 1)
			{
				Gdiplus::FontFamily ff(L"Microsoft YaHei");
				Gdiplus::Font f(&ff, size * 0.42f, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
				Gdiplus::StringFormat sf;
				sf.SetAlignment(Gdiplus::StringAlignmentCenter);
				sf.SetLineAlignment(Gdiplus::StringAlignmentCenter);
				Gdiplus::RectF textBox(cx - size / 2, cy - size / 2, size, size);
				g.DrawString(L"1", -1, &f, textBox, &sf, &brush);
			}
		}
		// 音量图标：喇叭 + 声波弧线，volume<=0 时画静音的 X
		void DrawVolumeGlyph(Gdiplus::Graphics& g, float cx, float cy, float size, float volume, Gdiplus::Color color)
		{
			Gdiplus::SolidBrush brush(color);
			Gdiplus::Pen pen(color, size * 0.09f);
			pen.SetStartCap(Gdiplus::LineCapRound);
			pen.SetEndCap(Gdiplus::LineCapRound);

			float bodyW = size * 0.22f;
			float bodyH = size * 0.36f;
			Gdiplus::RectF body(cx - size * 0.42f, cy - bodyH / 2, bodyW, bodyH);
			g.FillRectangle(&brush, body);

			Gdiplus::PointF cone[4] = {
				Gdiplus::PointF(body.X + body.Width, body.Y),
				Gdiplus::PointF(body.X + body.Width + size * 0.22f, cy - size * 0.34f),
				Gdiplus::PointF(body.X + body.Width + size * 0.22f, cy + size * 0.34f),
				Gdiplus::PointF(body.X + body.Width, body.Y + body.Height)
			};
			g.FillPolygon(&brush, cone, 4);

			if (volume <= 0.001f)
			{
				float x0 = cx + size * 0.10f, x1 = cx + size * 0.34f;
				g.DrawLine(&pen, x0, cy - size * 0.16f, x1, cy + size * 0.16f);
				g.DrawLine(&pen, x0, cy + size * 0.16f, x1, cy - size * 0.16f);
				return;
			}

			Gdiplus::RectF arc1(cx + size * 0.02f, cy - size * 0.22f, size * 0.30f, size * 0.44f);
			g.DrawArc(&pen, arc1, -45, 90);

			if (volume > 0.5f)
			{
				Gdiplus::RectF arc2(cx + size * 0.14f, cy - size * 0.34f, size * 0.46f, size * 0.68f);
				g.DrawArc(&pen, arc2, -40, 80);
			}
		}
		// 秒数 -> "mm:ss" 或 "h:mm:ss"（useHours 由调用方根据总时长决定，保证两端格式一致）
		std::wstring FormatTime(int totalSecondsValue, bool useHours)
		{
			if (totalSecondsValue < 0)
				totalSecondsValue = 0;

			int hours = totalSecondsValue / 3600;
			int minutes = (totalSecondsValue % 3600) / 60;
			int secs = totalSecondsValue % 60;

			wchar_t buf[32];
			if (useHours)
				swprintf_s(buf, L"%d:%02d:%02d", hours, minutes, secs);
			else
				swprintf_s(buf, L"%02d:%02d", minutes, secs);
			return buf;
		}
		// 把白色单色 PNG 染成指定颜色再画出来（ColorMatrix：RGB 取目标色，保留原 alpha）
		void DrawTintedIcon(Gdiplus::Graphics& g, Gdiplus::Image* img, const Gdiplus::RectF& dest, Gdiplus::Color color)
		{
			if (!img || img->GetLastStatus() != Gdiplus::Ok)
				return;

			Gdiplus::ColorMatrix cm = {};
			cm.m[3][3] = 1.0f;                       // alpha 保持不变
			cm.m[4][0] = color.GetR() / 255.0f;      // R
			cm.m[4][1] = color.GetG() / 255.0f;      // G
			cm.m[4][2] = color.GetB() / 255.0f;      // B
			cm.m[4][4] = 1.0f;

			Gdiplus::ImageAttributes attr;
			attr.SetColorMatrix(&cm, Gdiplus::ColorMatrixFlagsDefault, Gdiplus::ColorAdjustTypeBitmap);
			attr.SetWrapMode(Gdiplus::WrapModeTileFlipXY);   // 避免缩放时边缘出现杂线

			g.DrawImage(img, dest, 0.0f, 0.0f,
				(Gdiplus::REAL)img->GetWidth(), (Gdiplus::REAL)img->GetHeight(),
				Gdiplus::UnitPixel, &attr);
		}
	}
	// ===================== 右侧内容区的辅助代码 =====================
	namespace
	{
		const int kContentPad = 20;      // 内容卡片内边距
		const int kRowHeight = 48;       // 歌曲行高
		const int kHeaderHeight = 32;    // 列表表头高度
		const int kColIdx = 56;          // 序号列宽
		const int kColHeart = 48;        // 爱心列宽
		const int kColDur = 72;          // 时长列宽
		const int kSearchHeight = 36;
		const int kChipHeight = 30;
		const int kSearchEditId = 1001;
		const UINT kMsgOnlineResult = WM_APP + 2;   // 后台线程取完在线歌曲 -> 通知主窗口
		// 后台线程 new 出来，主窗口收到消息后 delete
		struct OnlineResult
		{
			int gen = 0;                   // 请求序号，过期的结果直接丢弃
			bool ok = false;
			std::wstring error;            // failure reason
			std::vector<TrackItem> tracks;
		};
		// 在线试听/下载的调试输出：VS 的"输出"窗口里搜 [Online] 就能看到每一步走到哪了
		void OnlineLog(const std::wstring& s)
		{
			OutputDebugStringW((L"[Online] " + s + L"\n").c_str());
		}
		const UINT kMsgTaskDone = WM_APP + 3;   // 试听缓存 / 下载歌曲 / 下载歌词 完成
		struct TaskResult
		{
			int kind = 0;      // 0=试听 1=歌曲 2=歌词
			int gen = 0;
			bool ok = false;
			std::wstring message;   // 失败原因
			std::wstring file;      // 成功后的本地文件
			TrackItem track;
		};
		// 自动下载歌词：最近一次请求的歌（回来时对得上才读进桌面歌词），以及已经试过的歌（只试一次）
		std::wstring s_autoLyricKey;
		std::set<std::wstring> s_autoLyricTried;
		std::wstring LyricKey(const TrackItem& t) { return t.title + L"\x1" + t.artist; }
		// 默认下载目录：音乐\YuMediaPlayer
		std::wstring GetDownloadDir(const std::wstring& custom)
		{
			if (!custom.empty())
				return custom;
			std::wstring dir = L".\\Downloads";
			PWSTR p = nullptr;
			if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Music, 0, nullptr, &p)) && p)
			{
				dir = std::wstring(p) + L"\\YuMediaPlayer";
				CoTaskMemFree(p);
			}
			return dir;
		}
		const UINT kMsgSearchSubmit = WM_APP + 1;   // 搜索框按回车 -> 通知主窗口
		const float kPi = 3.14159265f;
		WNDPROC g_oldEditProc = nullptr;

		int ClampInt(int v, int lo, int hi)
		{
			return v < lo ? lo : (v > hi ? hi : v);
		}

		// 搜索框子类化：回车触发搜索；滚轮转给主窗口（否则输入框有焦点时列表滚不动）
		LRESULT CALLBACK SearchEditProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
		{
			switch (msg)
			{
			case WM_CHAR:
				if (wParam == VK_RETURN)
				{
					PostMessageW(GetParent(hwnd), kMsgSearchSubmit, 0, 0);
					return 0;   // 吃掉回车，避免"叮"一声
				}
				break;
			case WM_MOUSEWHEEL:
				return SendMessageW(GetParent(hwnd), msg, wParam, lParam);
			}
			return CallWindowProcW(g_oldEditProc, hwnd, msg, wParam, lParam);
		}

		// 列表各列的横坐标。album 列放不下时 albumX == heartX（宽度为 0，不画）
		struct ListColumns
		{
			int idxX, titleX, artistX, albumX, heartX, durX, endX;
		};
		ListColumns CalcColumns(const RECT& r)
		{
			ListColumns c = {};
			int w = r.right - r.left;
			c.idxX = r.left;
			c.titleX = r.left + kColIdx;
			c.durX = r.right - kColDur;
			c.heartX = c.durX - kColHeart;
			c.endX = r.right;
			int rest = c.heartX - c.titleX;
			if (rest < 0) rest = 0;
			if (w >= 620)
			{
				c.albumX = c.heartX - rest * 25 / 100;
				c.artistX = c.albumX - rest * 28 / 100;
			}
			else
			{
				c.albumX = c.heartX;
				c.artistX = c.heartX - rest * 38 / 100;
			}
			return c;
		}

		// 心形（参数方程），size 为整体宽度
		void BuildHeartPath(Gdiplus::GraphicsPath& path, float cx, float cy, float size)
		{
			const int n = 48;
			Gdiplus::PointF pts[n];
			float s = size / 32.0f;
			for (int i = 0; i < n; i++)
			{
				float t = 2.0f * kPi * i / n;
				float st = sinf(t);
				float x = 16.0f * st * st * st;
				float y = 13.0f * cosf(t) - 5.0f * cosf(2 * t) - 2.0f * cosf(3 * t) - cosf(4 * t);
				pts[i] = Gdiplus::PointF(cx + x * s, cy - (y + 2.5f) * s);
			}
			path.Reset();
			path.AddPolygon(pts, n);
		}

		void FormatDuration(int seconds, wchar_t* buf, size_t count)
		{
			if (seconds < 0) seconds = 0;
			swprintf_s(buf, count, L"%d:%02d", seconds / 60, seconds % 60);
		}
	}
	static WindowGUI* g_windowGUIInstance = nullptr;
	enum class ButtonType
	{
		Close = 0,      // 关闭
		Maximize = 1,   // 最大化
		Minimize = 2,   // 最小化
		Settings = 3,   // 设置
		Update = 4,     // 刷新/更新
		Count = 5
	};
	WindowGUI::WindowGUI()
		: m_hwnd(nullptr)
		, m_hoverButton(-1)
		, m_isMaximized(false)
		, m_cornerRadius(13)  // 圆角半径，默认 15px
		, m_settingsIcon(nullptr)
		, m_refreshIcon(nullptr)
		, m_gdiplusToken(0)
	{
		// 保存全局实例指针，用于 WndProc 回调
		g_windowGUIInstance = this;

		// 乐馆默认分类（可用 SetCategories 替换）
		m_categories = {
			L"\u63A8\u8350", L"\u6D41\u884C", L"\u56FD\u8BED", L"\u6B27\u7F8E", L"\u65E5\u97E9",
			L"\u6C11\u8C23", L"\u6447\u6EDA", L"\u7535\u5B50", L"\u53E4\u5178", L"\u7EAF\u97F3\u4E50"
		};

		for (int i = 0; i < 5; i++)
			m_buttonRects[i] = { 0, 0, 0, 0 };
		Gdiplus::GdiplusStartupInput gdiplusStartupInput;
		Gdiplus::GdiplusStartup(&m_gdiplusToken, &gdiplusStartupInput, nullptr);
	}

	WindowGUI::~WindowGUI()
	{
		m_settingPage.reset();   // 先销毁设置页（它自己有一份 GDI+ 引用）
		m_coverImage.reset();    // GDI+ 图像必须在 GdiplusShutdown 之前释放
		if (g_windowGUIInstance == this)
			g_windowGUIInstance = nullptr;
		if (m_logoIcon)
		{
			DestroyIcon(m_logoIcon);
			m_logoIcon = nullptr;
		}
		// 释放侧边栏图标
		for (int i = 0; i < kSidebarCount; i++)
		{
			delete m_sidebarIcons[i];
			m_sidebarIcons[i] = nullptr;
		}
		// 清理图标资源
		if (m_settingsIcon)
		{
			delete m_settingsIcon;
			m_settingsIcon = nullptr;
		}
		if (m_refreshIcon)
		{
			delete m_refreshIcon;
			m_refreshIcon = nullptr;
		}

		// 关闭 GDI+
		if (m_gdiplusToken)
		{
			Gdiplus::GdiplusShutdown(m_gdiplusToken);
		}
		if (m_hwnd)
		{
			DestroyWindow(m_hwnd);
			m_hwnd = nullptr;
		}
		m_searchEdit = nullptr;   // 子窗口随主窗口一起销毁
		if (m_searchFont)
		{
			DeleteObject(m_searchFont);
			m_searchFont = nullptr;
		}
		if (m_searchBrush)
		{
			DeleteObject(m_searchBrush);
			m_searchBrush = nullptr;
		}
	}
	bool WindowGUI::LoadSettingsIcon(const wchar_t* imagePath)
	{
		// 清理旧的图标
		if (m_settingsIcon)
		{
			delete m_settingsIcon;
			m_settingsIcon = nullptr;
		}
		// 加载新的图标
		m_settingsIcon = new Gdiplus::Image(imagePath);
		if (!m_settingsIcon || m_settingsIcon->GetLastStatus() != Gdiplus::Ok)
		{
			if (m_settingsIcon)
			{
				delete m_settingsIcon;
				m_settingsIcon = nullptr;
			}
			return false;
		}
		return true;
	}
	bool WindowGUI::LoadRefreshIcon(const wchar_t* imagePath)
	{
		// 清理旧的图标
		if (m_refreshIcon)
		{
			delete m_refreshIcon;
			m_refreshIcon = nullptr;
		}
		// 加载新的图标
		m_refreshIcon = new Gdiplus::Image(imagePath);
		if (!m_refreshIcon || m_refreshIcon->GetLastStatus() != Gdiplus::Ok)
		{
			if (m_refreshIcon)
			{
				delete m_refreshIcon;
				m_refreshIcon = nullptr;
			}
			return false;
		}
		return true;
	}
	void WindowGUI::UpdateWindowRegion()
	{
		if (!m_hwnd)
			return;

		// 最大化时铺满屏幕，不需要圆角；半径为 0 也表示不要圆角
		if (m_isMaximized || IsZoomed(m_hwnd) || m_cornerRadius <= 0)
		{
			SetWindowRgn(m_hwnd, nullptr, TRUE);   // 清除 region
			return;
		}
		ApplyLegacyCornerRadius();
	}
	void WindowGUI::ApplyLegacyCornerRadius()
	{
		if (!m_hwnd)
			return;

		// WS_POPUP 窗口没有非客户区，窗口矩形 == 客户区矩形
		RECT rect;
		GetWindowRect(m_hwnd, &rect);
		int w = rect.right - rect.left;
		int h = rect.bottom - rect.top;

		// 创建圆角矩形 region（右、下边界要 +1，否则右下角会少一像素）
		HRGN hRgn = CreateRoundRectRgn(
			0, 0,
			w + 1,
			h + 1,
			m_cornerRadius * 2,  // 水平圆角直径
			m_cornerRadius * 2   // 竖直圆角直径
		);

		if (hRgn)
		{
			// 设置成功后 region 的所有权归系统，不需要也不能 DeleteObject
			SetWindowRgn(m_hwnd, hRgn, TRUE);
		}
	}
	void WindowGUI::SetCornerRadius(int radius)
	{
		if (radius < 0)
			radius = 0;
		if (radius > 50)
			radius = 50;  // 限制最大半径

		m_cornerRadius = radius;
		UpdateWindowRegion();
	}
	int WindowGUI::GetCornerRadius() const
	{
		return m_cornerRadius;
	}

	LRESULT CALLBACK WindowGUI::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
		// 在 WM_NCCREATE 时获取 this 指针
		WindowGUI* pThis = nullptr;

		if (msg == WM_NCCREATE)
		{
			// 创建阶段，从创建参数中获取 this 指针
			auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
			pThis = reinterpret_cast<WindowGUI*>(cs->lpCreateParams);
			SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pThis));
		}
		else
		{
			// 其他消息，从窗口用户数据中获取 this 指针
			pThis = reinterpret_cast<WindowGUI*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
		}

		// 如果有 this 指针，调用成员函数处理消息
		if (pThis)
			return pThis->EventProc(hwnd, msg, wParam, lParam);

		// 否则使用默认处理
		return DefWindowProcW(hwnd, msg, wParam, lParam);
	}
	void WindowGUI::DrawButton(HDC hdc, const RECT& rect, bool hovered, int btnType)
	{
		// 按钮背景色
		COLORREF bgColor = hovered ? ThemeRef(70, 214) : ThemeRef(40, 232);
		COLORREF borderColor = hovered ? ThemeRef(100, 176) : ThemeRef(60, 208);
		COLORREF iconColor = hovered ? ThemeRef(255, 20) : ThemeRef(200, 90);

		// 绘制背景
		HBRUSH hBgBrush = CreateSolidBrush(bgColor);
		FillRect(hdc, &rect, hBgBrush);
		DeleteObject(hBgBrush);

		// 绘制边框
		HPEN hPen = CreatePen(PS_SOLID, 1, borderColor);
		HPEN hOldPen = (HPEN)SelectObject(hdc, hPen);
		MoveToEx(hdc, rect.left, rect.top, nullptr);
		LineTo(hdc, rect.right, rect.top);
		LineTo(hdc, rect.right, rect.bottom);
		LineTo(hdc, rect.left, rect.bottom);
		LineTo(hdc, rect.left, rect.top);
		SelectObject(hdc, hOldPen);
		DeleteObject(hPen);

		// 绘制图标
		int centerX = (rect.left + rect.right) / 2;
		int centerY = (rect.top + rect.bottom) / 2;

		HPEN hIconPen = CreatePen(PS_SOLID, 2, iconColor);
		HPEN hOldIconPen = (HPEN)SelectObject(hdc, hIconPen);
		
		switch (btnType)
		{
		case 0:  // 关闭按钮 - X 符号
		{
			int offset = 5;
			MoveToEx(hdc, centerX - offset, centerY - offset, nullptr);
			LineTo(hdc, centerX + offset, centerY + offset);
			MoveToEx(hdc, centerX + offset, centerY - offset, nullptr);
			LineTo(hdc, centerX - offset, centerY + offset);
			break;
		}
		case 1:  // 最大化按钮 - 方框符号
		{
			int offset = 5;
			MoveToEx(hdc, centerX - offset, centerY - offset, nullptr);
			LineTo(hdc, centerX + offset, centerY - offset);
			LineTo(hdc, centerX + offset, centerY + offset);
			LineTo(hdc, centerX - offset, centerY + offset);
			LineTo(hdc, centerX - offset, centerY - offset);
			break;
		}
		case 2:  // 最小化按钮 - 横线符号
		{
			int offset = 5;
			MoveToEx(hdc, centerX - offset, centerY + 2, nullptr);
			LineTo(hdc, centerX + offset, centerY + 2);
			break;
		}
		case 3:  // 设置按钮 - 三个小圆点
		{
			bool iconDrawn = false;
			if (m_settingsIcon && m_settingsIcon->GetLastStatus() == Gdiplus::Ok)
			{
				try
				{
				Gdiplus::Graphics graphics(hdc);
				graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
				graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
				graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
				graphics.SetCompositingQuality(Gdiplus::CompositingQualityHighQuality);
					
				int displaySize = 20;
				int iconX = centerX - displaySize / 2;
				int iconY = centerY - displaySize / 2;
				if (UITheme::IsLight())   // 图标是白色的，浅色主题下染成深灰
					DrawTintedIcon(graphics, m_settingsIcon, Gdiplus::RectF((float)iconX, (float)iconY, (float)displaySize, (float)displaySize),
						hovered ? Gdiplus::Color(255, 20, 20, 20) : Gdiplus::Color(255, 90, 90, 90));
				else
					graphics.DrawImage(m_settingsIcon, iconX, iconY, displaySize, displaySize);
				iconDrawn = true;
				}
				catch (...)
				{
					iconDrawn = false;
				}
			}
			break;
		}
		case 4:  // 更新按钮 - 刷新符号
			{
			bool iconDrawn = false;
			if (m_refreshIcon && m_refreshIcon->GetLastStatus() == Gdiplus::Ok)
			{
				try
				{
					Gdiplus::Graphics graphics(hdc);
					graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
					graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
					graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
					graphics.SetCompositingQuality(Gdiplus::CompositingQualityHighQuality);

					int displaySize = 20;
					int iconX = centerX - displaySize / 2;
					int iconY = centerY - displaySize / 2;
					if (UITheme::IsLight())
						DrawTintedIcon(graphics, m_refreshIcon, Gdiplus::RectF((float)iconX, (float)iconY, (float)displaySize, (float)displaySize),
							hovered ? Gdiplus::Color(255, 20, 20, 20) : Gdiplus::Color(255, 90, 90, 90));
					else
						graphics.DrawImage(m_refreshIcon, iconX, iconY, displaySize, displaySize);
					iconDrawn = true;
				}
				catch (...)
				{
					iconDrawn = false;
				}
			}
			break;
			}
		}
		SelectObject(hdc, hOldIconPen);
		DeleteObject(hIconPen);
	}
	void WindowGUI::UpdateButtonRects(int width)
	{
		const int titleBarHeight = 30;
		const int buttonWidth = 44;
		const int buttonHeight = 30;
		const int margin = 0;

		// 从右到左排列：关闭、最大化、最小化
		int rightX = width - margin;

		m_buttonRects[static_cast<int>(ButtonType::Close)] = {
			rightX - buttonWidth, 0, rightX, buttonHeight
		};
		rightX -= buttonWidth;

		m_buttonRects[static_cast<int>(ButtonType::Maximize)] = {
			rightX - buttonWidth, 0, rightX, buttonHeight
		};
		rightX -= buttonWidth;

		m_buttonRects[static_cast<int>(ButtonType::Minimize)] = {
			rightX - buttonWidth, 0, rightX, buttonHeight
		};
		rightX -= buttonWidth;
		m_buttonRects[static_cast<int>(ButtonType::Settings)] = {
			rightX - buttonWidth, 0, rightX, buttonHeight
		};
		rightX -= buttonWidth;
		m_buttonRects[static_cast<int>(ButtonType::Update)] = {
			rightX - buttonWidth, 0, rightX, buttonHeight
		};
	}

	void WindowGUI::UpdatePlayBarRect(int width, int height)
	{
		const int margin = 10;       // 左右留白
		const int bottomMargin = 10; // 底部也留一点距离，别贴死到窗口边缘
		const int barHeight = 120;   // 播放栏高度

		m_playBarRect = {
			margin,
			height - bottomMargin - barHeight,
			width - margin,
			height - bottomMargin
		};
	}
	void WindowGUI::DrawPlayBar(HDC hdc, const RECT& rect)
	{
		int w = rect.right - rect.left;
		int h = rect.bottom - rect.top;
		if (w <= 0 || h <= 0)
			return;

		Gdiplus::Graphics graphics(hdc);
		graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
		graphics.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);

		const int radius = 16; // 比标题栏按钮更大一点的圆角，和整窗风格呼应

		Gdiplus::GraphicsPath path;
		BuildRoundRectPath(path, Gdiplus::Rect(rect.left, rect.top, w, h), radius);

		// 背景比主窗口背景稍亮，形成一张"卡片"
		Gdiplus::SolidBrush bgBrush(ThemeGray(32, 255));
		graphics.FillPath(&bgBrush, &path);

		// 细描边，增加层次感
		Gdiplus::Pen borderPen(ThemeGray(55, 222), 1.0f);
		graphics.DrawPath(&borderPen, &path);
		//------------------------------
		const int paddingX = 16;
		const int paddingY = 10;
		const int progressAreaHeight = 22; //
		float coverCenterY = rect.top + h / 2.0f;
		const int albumArtSize = h - 2 * paddingY;

		Gdiplus::Rect albumRect(
			rect.left + paddingX,
			(int)(coverCenterY - albumArtSize / 2.0f),
			albumArtSize, albumArtSize
		);

		Gdiplus::GraphicsPath albumPath;
		BuildRoundRectPath(albumPath, albumRect, 8);
		Gdiplus::SolidBrush albumBrush(ThemeGray(60, 226));
		graphics.FillPath(&albumBrush, &albumPath);
		// 封面：有图就居中裁成正方形，画进圆角方块里；没有图就保留上面的灰色占位
		if (m_coverImage && m_coverImage->GetWidth() > 0 && m_coverImage->GetHeight() > 0)
		{
			const INT iw = (INT)m_coverImage->GetWidth();
			const INT ih = (INT)m_coverImage->GetHeight();
			const INT side = iw < ih ? iw : ih;
			graphics.SetClip(&albumPath);
			graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
			graphics.DrawImage(m_coverImage.get(), albumRect,
				(iw - side) / 2, (ih - side) / 2, side, side, Gdiplus::UnitPixel);
			graphics.ResetClip();
		}
		// ---- 播放控制区：循环模式 - 上一曲 - 播放/暂停 - 下一曲 - 音量 ----
		//
		int textLeft = albumRect.X + albumRect.Width + paddingX;
		int textWidth = (rect.right - paddingX) - textLeft;
		if (textWidth < 0)
			textWidth = 0;

		const float playRadius = 22.0f;
		const float skipSize = 26.0f;
		const float sideSize = 22.0f;
		const float gapPlaySkip = 50.0f;   // 
		const float gapSkipSide = 80.0f;   // 

		const float titleBlockH = 24.0f;        // 
		const float titleArtistGap = 10.0f;     //
		const float artistBlockH = 20.0f;       // 
		const float artistButtonsGap = 10.0f;   // 
		const float buttonsBlockH = playRadius * 2.0f; // 

		const float totalContentH = titleBlockH + titleArtistGap + artistBlockH
			+ artistButtonsGap + buttonsBlockH;
		float availableCenterY = rect.top + (h - progressAreaHeight) / 2.0f;
		float blockTop = availableCenterY - totalContentH / 2.0f;
		// 保证内容块不会跑到播放条卡片的上边缘之外
		const float minBlockTop = rect.top + paddingY;
		if (blockTop < minBlockTop)
			blockTop = minBlockTop;

		float titleY = blockTop;
		float artistY = titleY + titleBlockH + titleArtistGap;
		const float buttonsUpShift = 45.0f; // 按钮整体上移一些,与进度条留出更多间距
		float buttonsCenterY = artistY + artistBlockH + artistButtonsGap + buttonsBlockH / 2.0f - buttonsUpShift;


		float centerX = textLeft + textWidth / 2.0f;
		float centerY = buttonsCenterY;

		float prevCx = centerX - gapPlaySkip;
		float nextCx = centerX + gapPlaySkip;
		float loopCx = prevCx - gapSkipSide;
		float volumeCx = nextCx + gapSkipSide;

		m_playPauseRect = { (LONG)(centerX - playRadius), (LONG)(centerY - playRadius),
							 (LONG)(centerX + playRadius), (LONG)(centerY + playRadius) };
		m_prevRect = { (LONG)(prevCx - skipSize / 2 - 6), (LONG)(centerY - skipSize / 2 - 6),
					   (LONG)(prevCx + skipSize / 2 + 6), (LONG)(centerY + skipSize / 2 + 6) };
		m_nextRect = { (LONG)(nextCx - skipSize / 2 - 6), (LONG)(centerY - skipSize / 2 - 6),
					   (LONG)(nextCx + skipSize / 2 + 6), (LONG)(centerY + skipSize / 2 + 6) };
		m_loopRect = { (LONG)(loopCx - sideSize / 2 - 6), (LONG)(centerY - sideSize / 2 - 6),
					   (LONG)(loopCx + sideSize / 2 + 6), (LONG)(centerY + sideSize / 2 + 6) };
		m_volumeRect = { (LONG)(volumeCx - sideSize / 2 - 6), (LONG)(centerY - sideSize / 2 - 6),
						 (LONG)(volumeCx + sideSize / 2 + 6), (LONG)(centerY + sideSize / 2 + 6) };

		Gdiplus::FontFamily fontFamily(L"Microsoft YaHei");
		Gdiplus::Font titleFont(&fontFamily, 15, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
		Gdiplus::Font artistFont(&fontFamily, 12, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);

		Gdiplus::SolidBrush titleBrush(ThemeGray(235, 30));
		Gdiplus::SolidBrush artistBrush(ThemeGray(160, 115));

		std::wstring title = m_nowPlayingTitle.empty() ? L"\u672A\u5728\u64AD\u653E" : m_nowPlayingTitle;
		const std::wstring& artist = m_nowPlayingArtist;

		Gdiplus::RectF titleRect(
			(Gdiplus::REAL)textLeft, titleY,
			(Gdiplus::REAL)textWidth, titleBlockH
		);
		Gdiplus::RectF artistRect(
			(Gdiplus::REAL)textLeft, artistY,
			(Gdiplus::REAL)textWidth, artistBlockH
		);

		Gdiplus::StringFormat format;
		format.SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);
		format.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);

		graphics.DrawString(title.c_str(), -1, &titleFont, titleRect, &format, &titleBrush);
		if (!artist.empty())
			graphics.DrawString(artist.c_str(), -1, &artistFont, artistRect, &format, &artistBrush);
		// ---- 画控制按钮 ----
		Gdiplus::Color iconColor = ThemeGray(210, 85);
		Gdiplus::Color activeColor = ThemeGray(255, 20);

		DrawLoopGlyph(graphics, loopCx, centerY, sideSize, m_loopMode,
			m_loopMode == 0 ? iconColor : activeColor); // 非默认模式时点亮显示
		DrawSkipGlyph(graphics, prevCx, centerY, skipSize, false, iconColor);
		DrawPlayPauseGlyph(graphics, centerX, centerY, playRadius, m_isPlaying);
		DrawSkipGlyph(graphics, nextCx, centerY, skipSize, true, iconColor);
		DrawVolumeGlyph(graphics, volumeCx, centerY, sideSize, m_volume, iconColor);
		// ---- 音量：波浪线 + 圆点（拖动圆点调整音量）----
		{
			const float waveLeft = volumeCx + sideSize / 2.0f + 14.0f;
			float waveRight = waveLeft + 120.0f;
			const float maxRight = (float)(rect.right - paddingX);
			if (waveRight > maxRight)
				waveRight = maxRight;      // 窗口窄的时候缩短，太短就不画

			if (waveRight - waveLeft >= 40.0f)
			{
				const float amp = 6.0f;        // 波幅（越大波浪越明显）
				const float period = 45.0f;    // 波长
				const float twoPi = 6.2831853f;
				auto waveY = [&](float x) { return centerY + amp * sinf((x - waveLeft) * twoPi / period); };

				const float dotX = waveLeft + (waveRight - waveLeft) * m_volume;

				// 整条波浪（暗色）
				std::vector<Gdiplus::PointF> all;
				for (float x = waveLeft; x <= waveRight; x += 1.0f)
					all.push_back(Gdiplus::PointF(x, waveY(x)));
				// 圆点左侧（亮色，表示当前音量）
				std::vector<Gdiplus::PointF> lit;
				for (float x = waveLeft; x <= dotX; x += 1.0f)
					lit.push_back(Gdiplus::PointF(x, waveY(x)));
				lit.push_back(Gdiplus::PointF(dotX, waveY(dotX)));

				Gdiplus::Pen dimPen(ThemeGray(75, 205), 2.5f);
				dimPen.SetStartCap(Gdiplus::LineCapRound);
				dimPen.SetEndCap(Gdiplus::LineCapRound);
				dimPen.SetLineJoin(Gdiplus::LineJoinRound);
				graphics.DrawLines(&dimPen, all.data(), (INT)all.size());

				if (lit.size() >= 2)
				{
					Gdiplus::Pen litPen(ThemeGray(235, 60), 2.5f);
					litPen.SetStartCap(Gdiplus::LineCapRound);
					litPen.SetEndCap(Gdiplus::LineCapRound);
					litPen.SetLineJoin(Gdiplus::LineJoinRound);
					graphics.DrawLines(&litPen, lit.data(), (INT)lit.size());
				}

				const float dotR = (m_volDragging ? 7.5f : 6.5f);
				Gdiplus::SolidBrush dotBrush(ThemeGray(255, 45));
				graphics.FillEllipse(&dotBrush, dotX - dotR, waveY(dotX) - dotR, dotR * 2.0f, dotR * 2.0f);

				m_volumeSliderRect = { (LONG)(waveLeft - 8), (LONG)(centerY - 18),
									   (LONG)(waveRight + 8), (LONG)(centerY + 18) };
			}
			else
			{
				m_volumeSliderRect = {};
			}
		}
		// ---- 进度条：00:00 ———●——— xx:xx:xx ----
		{
			const int timeLabelWidth = 44;  //
			const int labelGap = 6;
			const int thumbRadius = 5;
			float trackY = rect.bottom - progressAreaHeight / 2.0f - 2;  // 稍微偏上，给时间文字留baseline空间
			float trackLeft = textLeft + timeLabelWidth + labelGap; //
			float trackRight = rect.right - paddingX - timeLabelWidth - labelGap;
			if (trackRight < trackLeft)
				trackRight = trackLeft;

			bool useHours = m_totalSeconds >= 3600;
			std::wstring currentText = FormatTime(m_currentSeconds, useHours);
			std::wstring totalText = FormatTime(m_totalSeconds, useHours);

			Gdiplus::FontFamily timeFontFamily(L"Microsoft YaHei");
			Gdiplus::Font timeFont(&timeFontFamily, 11, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
			Gdiplus::SolidBrush timeBrush(ThemeGray(170, 120));

			Gdiplus::StringFormat leftFormat;
			leftFormat.SetAlignment(Gdiplus::StringAlignmentNear);
			leftFormat.SetLineAlignment(Gdiplus::StringAlignmentCenter);
			Gdiplus::RectF leftLabelRect((Gdiplus::REAL)textLeft, trackY - 10, (Gdiplus::REAL)timeLabelWidth, 20.0f);
			graphics.DrawString(currentText.c_str(), -1, &timeFont, leftLabelRect, &leftFormat, &timeBrush);

			Gdiplus::StringFormat rightFormat;
			rightFormat.SetAlignment(Gdiplus::StringAlignmentFar);
			rightFormat.SetLineAlignment(Gdiplus::StringAlignmentCenter);
			Gdiplus::RectF rightLabelRect(rect.right - paddingX - timeLabelWidth, trackY - 10, (Gdiplus::REAL)timeLabelWidth, 20.0f);
			graphics.DrawString(totalText.c_str(), -1, &timeFont, rightLabelRect, &rightFormat, &timeBrush);

			// 细线滑轨：底色 + 已播放部分高亮
			Gdiplus::Pen trackPen(ThemeGray(75, 215), 3.0f);
			trackPen.SetStartCap(Gdiplus::LineCapRound);
			trackPen.SetEndCap(Gdiplus::LineCapRound);
			graphics.DrawLine(&trackPen, trackLeft, trackY, trackRight, trackY);

			float progressRatio = 0.0f;
			if (m_totalSeconds > 0)
				progressRatio = (float)m_currentSeconds / (float)m_totalSeconds;
			if (progressRatio < 0.0f) progressRatio = 0.0f;
			if (progressRatio > 1.0f) progressRatio = 1.0f;

			float thumbX = trackLeft + (trackRight - trackLeft) * progressRatio;

			if (thumbX > trackLeft)
			{
				Gdiplus::Pen playedPen(ThemeGray(235, 60), 3.0f);
				playedPen.SetStartCap(Gdiplus::LineCapRound);
				playedPen.SetEndCap(Gdiplus::LineCapRound);
				graphics.DrawLine(&playedPen, trackLeft, trackY, thumbX, trackY);
			}

			// 可滑动的圆点
			Gdiplus::SolidBrush thumbBrush(ThemeGray(255, 45));
			graphics.FillEllipse(&thumbBrush, thumbX - thumbRadius, trackY - thumbRadius,
				thumbRadius * 2.0f, thumbRadius * 2.0f);

			// 保存命中区域，方便以后接拖动：整条滑轨给一点上下容差，圆点单独存一份精确范围
			m_progressBarRect = {
				(LONG)trackLeft, (LONG)(trackY - 8),
				(LONG)trackRight, (LONG)(trackY + 8)
			};
			m_progressThumbRect = {
				(LONG)(thumbX - thumbRadius - 4), (LONG)(trackY - thumbRadius - 4),
				(LONG)(thumbX + thumbRadius + 4), (LONG)(trackY + thumbRadius + 4)
			};
		}
	}
	
	void WindowGUI::SetPlaying(bool playing)
	{
		m_isPlaying = playing;
		if (m_hwnd)
			InvalidateRect(m_hwnd, &m_playBarRect, FALSE);
	}
	void WindowGUI::SetLoopMode(int mode)
	{
		m_loopMode = mode % 4;
		if (m_loopMode < 0)
			m_loopMode += 4;
		if (m_hwnd)
			InvalidateRect(m_hwnd, &m_playBarRect, FALSE);
	}
	void WindowGUI::SetVolume(float volume)
	{
		if (volume < 0.0f) volume = 0.0f;
		if (volume > 1.0f) volume = 1.0f;
		m_volume = volume;
		if (m_hwnd)
			InvalidateRect(m_hwnd, &m_playBarRect, FALSE);
	}
	void WindowGUI::SetVolumeCallback(std::function<void(float)> callback) { m_onVolume = std::move(callback); }
	void WindowGUI::SetLoopCallback(std::function<void()> callback) { m_onLoop = std::move(callback); }

	void WindowGUI::SetNowPlayingCover(std::unique_ptr<Gdiplus::Image> cover)
	{
		m_coverImage = std::move(cover);
		if (m_hwnd)
			InvalidateRect(m_hwnd, &m_playBarRect, FALSE);
	}

	float WindowGUI::VolumeFromX(int x) const
	{
		const int l = m_volumeSliderRect.left + 8;
		const int r = m_volumeSliderRect.right - 8;
		if (r <= l)
			return m_volume;
		float v = (float)(x - l) / (float)(r - l);
		if (v < 0.0f) v = 0.0f;
		if (v > 1.0f) v = 1.0f;
		return v;
	}

	void WindowGUI::SetProgress(int currentSeconds, int totalSeconds)
	{
		if (m_seeking)
			return;   // 用户正在拖进度条，别用播放器的进度覆盖他的拖动预览
		if (totalSeconds < 0) totalSeconds = 0;
		if (currentSeconds < 0) currentSeconds = 0;
		if (currentSeconds > totalSeconds) currentSeconds = totalSeconds;

		m_currentSeconds = currentSeconds;
		m_totalSeconds = totalSeconds;

		if (m_hwnd)
			InvalidateRect(m_hwnd, &m_playBarRect, FALSE);
	}
	void WindowGUI::SetNowPlaying(const wchar_t* title, const wchar_t* artist)
	{
		m_nowPlayingTitle = title ? title : L"";
		m_nowPlayingArtist = artist ? artist : L"";

		if (m_hwnd)
			InvalidateRect(m_hwnd, &m_playBarRect, FALSE); // 只重绘播放栏这一块区域,避免整窗闪烁
	}

	// 标题栏 logo 的显示尺寸和位置（想改大小/位置改这里）
	static const int kLogoSize = 36;
	static const int kLogoX = 55;
	static const int kLogoY = 5;   // （已不再使用：logo 现在跟着侧边栏一起布局，见 SidebarLogoY）

	// 侧边栏里 "logo + 三个按钮" 作为一个整体，垂直放在侧边栏中间偏上的位置
	static const int kLogoGap = 12;          // logo 与第一个按钮的间距
	static const int kSbItemHeight = 42;
	static const int kSbItemGap = 6;
	static const int kSbItemCount = 3;       // 要和 kSidebarCount 保持一致
	static const float kSbGroupRatio = 0.30f; // 0=贴顶 0.5=正中，越小越靠上

	static int SidebarGroupTop(const RECT& sb)
	{
		const int groupH = kLogoSize + kLogoGap
			+ kSbItemCount * kSbItemHeight + (kSbItemCount - 1) * kSbItemGap;
		int spare = (sb.bottom - sb.top) - groupH;
		if (spare < 0) spare = 0;                       // 窗口太矮时贴顶，不会跑出去
		return sb.top + (int)(spare * kSbGroupRatio);
	}
	static const int kLogoLift = 95;        // logo 比按钮组再往上抬多少像素（按钮位置不受影响）
	static int SidebarLogoY(const RECT& sb)
	{
		int y = SidebarGroupTop(sb) - kLogoLift;
		return y < 0 ? 0 : y;
	}

	bool WindowGUI::LoadLogoIcon()
	{
		if (m_logoIcon)
		{
			DestroyIcon(m_logoIcon);
			m_logoIcon = nullptr;
		}

		wchar_t path[MAX_PATH] = { 0 };
		GetModuleFileNameW(nullptr, path, MAX_PATH);
		wchar_t* lastSlash = wcsrchr(path, L'\\');
		if (lastSlash)
			*lastSlash = L'\0';
		wcscat_s(path, MAX_PATH, L"\\ui\\window\\logo.ico");

		// ico 里有多种尺寸，指定目标尺寸后系统会挑最接近的一张，比手动缩放清晰
		m_logoIcon = (HICON)LoadImageW(nullptr, path, IMAGE_ICON,
			kLogoSize, kLogoSize, LR_LOADFROMFILE);
		return m_logoIcon != nullptr;
	}

	bool WindowGUI::LoadSidebarIcons()
	{
		wchar_t exePath[MAX_PATH] = { 0 };
		GetModuleFileNameW(nullptr, exePath, MAX_PATH);
		wchar_t* lastSlash = wcsrchr(exePath, L'\\');
		if (lastSlash)
			*lastSlash = L'\0';

		static const wchar_t* kFiles[kSidebarCount] = {
			L"Sidebar_Music.png",   // 乐馆
			L"Sidebar_Local.png",   // 本地
			L"Sidebar_Heart.png"    // 喜欢
		};

		bool allOk = true;
		for (int i = 0; i < kSidebarCount; i++)
		{
			delete m_sidebarIcons[i];
			m_sidebarIcons[i] = nullptr;

			wchar_t path[MAX_PATH] = { 0 };
			swprintf_s(path, MAX_PATH, L"%s\\ui\\window\\%s", exePath, kFiles[i]);

			Gdiplus::Image* img = new Gdiplus::Image(path);
			if (img->GetLastStatus() != Gdiplus::Ok)
			{
				delete img;
				allOk = false;   // 图标缺失时只显示文字，不影响程序运行
			}
			else
			{
				m_sidebarIcons[i] = img;
			}
		}
		return allOk;
	}

	// 计算侧边栏区域和三个按钮的位置（需在 UpdatePlayBarRect 之后调用）
	void WindowGUI::UpdateSidebarRect(int width, int height)
	{
		(void)width; (void)height;

		const int marginLeft = 10;
		const int sidebarWidth = 130;
		const int topY = 40;           // 标题栏(30px)下方留一点空隙
		const int gapToPlayBar = 10;   // 与底部播放栏的间距
		const int padding = 8;
		const int itemHeight = kSbItemHeight;
		const int itemGap = kSbItemGap;

		m_sidebarRect = {
			marginLeft,
			topY,
			marginLeft + sidebarWidth,
			m_playBarRect.top - gapToPlayBar
		};

		// 按钮从 logo 下面开始排（整体已经下移到中间偏上）
		const int itemsTop = SidebarGroupTop(m_sidebarRect) + kLogoSize + kLogoGap;
		for (int i = 0; i < kSidebarCount; i++)
		{
			int y = itemsTop + i * (itemHeight + itemGap);
			m_sidebarItemRects[i] = {
				m_sidebarRect.left + padding,
				y,
				m_sidebarRect.right - padding,
				y + itemHeight
			};
		}
	}

	// 图标 + 文字，选中项高亮底板 + 黄色
	void WindowGUI::DrawSidebar(HDC hdc, const RECT& rect)
	{
		if (rect.right <= rect.left || rect.bottom <= rect.top)
			return;

		Gdiplus::Graphics graphics(hdc);
		graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
		graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
		graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
		graphics.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);

		// 乐馆 / 本地 / 喜欢（用 \u 转义，避免源文件编码问题）
		static const wchar_t* kLabels[kSidebarCount] = {
			L"\u4E50\u9986", L"\u672C\u5730", L"\u559C\u6B22"
		};

		const Gdiplus::Color accentColor = ThemeAccent();     // 选中：黄色
		const Gdiplus::Color normalColor = ThemeGray(200, 85);  // 默认：浅灰
		const Gdiplus::Color hoverColor = ThemeGray(255, 20);   // 悬停：白
		const Gdiplus::Color selectedBg = ThemeGray(46, 226);
		const Gdiplus::Color hoverBg = ThemeGray(36, 234);
		const int itemRadius = 10;
		const float iconSize = 22.0f;
		const float iconPadLeft = 16.0f;
		const float textGap = 12.0f;

		Gdiplus::FontFamily fontFamily(L"Microsoft YaHei");
		Gdiplus::Font normalFont(&fontFamily, 15, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
		Gdiplus::Font boldFont(&fontFamily, 15, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);

		Gdiplus::StringFormat format;
		format.SetAlignment(Gdiplus::StringAlignmentNear);
		format.SetLineAlignment(Gdiplus::StringAlignmentCenter);
		format.SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);
		format.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);

		for (int i = 0; i < kSidebarCount; i++)
		{
			const RECT& r = m_sidebarItemRects[i];
			int w = r.right - r.left;
			int h = r.bottom - r.top;
			if (w <= 0 || h <= 0)
				continue;

			bool selected = (i == m_sidebarSelected);
			bool hovered = (i == m_sidebarHover);

			if (selected || hovered)
			{
				Gdiplus::GraphicsPath path;
				BuildRoundRectPath(path, Gdiplus::Rect(r.left, r.top, w, h), itemRadius);
				Gdiplus::SolidBrush bgBrush(selected ? selectedBg : hoverBg);
				graphics.FillPath(&bgBrush, &path);
			}

			Gdiplus::Color fg = selected ? accentColor : (hovered ? hoverColor : normalColor);

			float cy = r.top + h / 2.0f;
			Gdiplus::RectF iconRect(r.left + iconPadLeft, cy - iconSize / 2.0f, iconSize, iconSize);
			DrawTintedIcon(graphics, m_sidebarIcons[i], iconRect, fg);

			float textX = iconRect.X + iconSize + textGap;
			Gdiplus::RectF textRect(textX, (Gdiplus::REAL)r.top,
				(Gdiplus::REAL)r.right - textX - 8.0f, (Gdiplus::REAL)h);
			Gdiplus::SolidBrush textBrush(fg);
			graphics.DrawString(kLabels[i], -1, selected ? &boldFont : &normalFont,
				textRect, &format, &textBrush);
		}
	}

	// ===================== 右侧内容区 =====================
	void WindowGUI::RefreshLayout()
	{
		if (!m_hwnd)
			return;
		RECT rc;
		GetClientRect(m_hwnd, &rc);
		int w = rc.right - rc.left;
		int h = rc.bottom - rc.top;
		UpdateButtonRects(w);
		UpdatePlayBarRect(w, h);
		UpdateSidebarRect(w, h);
		UpdateContentRects(w, h);
		LayoutSearchEdit();
	}

	void WindowGUI::UpdateContentRects(int width, int height)
	{
		(void)height;
		const int gap = 10;
		const int marginRight = 10;

		// 内容卡片：侧边栏右边 ~ 窗口右边，标题栏下方 ~ 播放栏上方
		m_contentRect = { m_sidebarRect.right + gap, m_sidebarRect.top, width - marginRight, m_playBarRect.top - gap };
		if (m_contentRect.right < m_contentRect.left) m_contentRect.right = m_contentRect.left;
		if (m_contentRect.bottom < m_contentRect.top) m_contentRect.bottom = m_contentRect.top;

		m_searchRect = { 0, 0, 0, 0 };
		m_searchIconRect = { 0, 0, 0, 0 };
		m_categoryRects.clear();

		const int left = m_contentRect.left + kContentPad;
		const int right = m_contentRect.right - kContentPad;
		int y = m_contentRect.top + 16;

		if (m_sidebarSelected == 0)
		{
			// 乐馆：搜索框 + 分类标签
			int sw = right - left;
			if (sw > 380) sw = 380;
			if (sw < 0) sw = 0;
			m_searchRect = { left, y, left + sw, y + kSearchHeight };
			m_searchIconRect = { left, y, left + (sw < 40 ? sw : 40), y + kSearchHeight };
			y += kSearchHeight + 22;   // search box to category row gap

			int x = left;
			for (size_t i = 0; i < m_categories.size(); i++)
			{
				int w = 28 + (int)m_categories[i].size() * 14;
				RECT r = { 0, 0, 0, 0 };
				if (x + w <= right)
				{
					r = { x, y, x + w, y + kChipHeight };
					x += w + 8;
				}
				else
				{
					x = right + 1;   // 这一个放不下，后面的也隐藏
				}
				m_categoryRects.push_back(r);
			}
			y += kChipHeight + 12;
		}
		else
		{
			// 本地 / 喜欢：页面标题 + "共 N 首"
			y += 60;
		}

		m_listHeaderRect = { left, y, right, y + kHeaderHeight };
		m_listRect = { left, y + kHeaderHeight, right, m_contentRect.bottom - 12 };
		if (m_listRect.bottom < m_listRect.top)
			m_listRect.bottom = m_listRect.top;
	}

	void WindowGUI::DrawContent(HDC hdc)
	{
		const int cw = m_contentRect.right - m_contentRect.left;
		const int ch = m_contentRect.bottom - m_contentRect.top;
		if (cw <= 0 || ch <= 0)
			return;

		Gdiplus::Graphics g(hdc);
		g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
		g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);

		const Gdiplus::Color accent = ThemeAccent();
		const Gdiplus::Color white = ThemeGray(240, 30);
		const Gdiplus::Color gray = ThemeGray(170, 110);
		const Gdiplus::Color dimGray = ThemeGray(130, 150);

		// ---- 卡片底板（和播放栏同风格）----
		{
			Gdiplus::GraphicsPath path;
			BuildRoundRectPath(path, Gdiplus::Rect(m_contentRect.left, m_contentRect.top, cw, ch), 16);
			Gdiplus::SolidBrush bg(ThemeGray(32, 255));
			g.FillPath(&bg, &path);
			Gdiplus::Pen border(ThemeGray(55, 222), 1.0f);
			g.DrawPath(&border, &path);
		}

		Gdiplus::FontFamily family(L"Microsoft YaHei");
		Gdiplus::Font f13(&family, 13, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
		Gdiplus::Font f14(&family, 14, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
		Gdiplus::Font f14b(&family, 14, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
		Gdiplus::Font f15(&family, 15, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
		Gdiplus::Font f24b(&family, 24, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);

		Gdiplus::StringFormat fmtLeft;
		fmtLeft.SetAlignment(Gdiplus::StringAlignmentNear);
		fmtLeft.SetLineAlignment(Gdiplus::StringAlignmentCenter);
		fmtLeft.SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);
		fmtLeft.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);
		Gdiplus::StringFormat fmtCenter;
		fmtCenter.SetAlignment(Gdiplus::StringAlignmentCenter);
		fmtCenter.SetLineAlignment(Gdiplus::StringAlignmentCenter);
		fmtCenter.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);
		Gdiplus::StringFormat fmtRight;
		fmtRight.SetAlignment(Gdiplus::StringAlignmentFar);
		fmtRight.SetLineAlignment(Gdiplus::StringAlignmentCenter);
		fmtRight.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);

		auto drawText = [&](const wchar_t* text, const Gdiplus::Font& font, const Gdiplus::Color& color,
			float x, float y, float w, float h, const Gdiplus::StringFormat& fmt)
		{
			if (w <= 0 || h <= 0)
				return;
			Gdiplus::SolidBrush brush(color);
			g.DrawString(text, -1, &font, Gdiplus::RectF(x, y, w, h), &fmt, &brush);
		};

		const int page = m_sidebarSelected;

		if (page == 0)
		{
			// ---- 搜索框（胶囊形 + 放大镜；输入框本体是子窗口 EDIT，叠在上面）----
			const RECT& sr = m_searchRect;
			int sw = sr.right - sr.left;
			if (sw > 0)
			{
				Gdiplus::GraphicsPath path;
				BuildRoundRectPath(path, Gdiplus::Rect(sr.left, sr.top, sw - 1, kSearchHeight - 1), (kSearchHeight - 1) / 2);   // inset 1px so the border stays inside the rect
				Gdiplus::SolidBrush pillBg(ThemeGray(44, 238));
				g.FillPath(&pillBg, &path);
				Gdiplus::Pen pillBorder(m_searchFocused ? accent : ThemeGray(64, 208), 1.0f);
				g.DrawPath(&pillBorder, &path);

				float cx = sr.left + 20.0f;
				float cy = sr.top + kSearchHeight / 2.0f;
				Gdiplus::Pen iconPen(ThemeGray(170, 120), 1.8f);
				iconPen.SetLineCap(Gdiplus::LineCapRound, Gdiplus::LineCapRound, Gdiplus::DashCapRound);
				g.DrawEllipse(&iconPen, cx - 7.0f, cy - 7.0f, 10.0f, 10.0f);
				g.DrawLine(&iconPen, cx + 1.5f, cy + 1.5f, cx + 6.0f, cy + 6.0f);
			}

			// ---- 分类标签 ----
			for (size_t i = 0; i < m_categoryRects.size(); i++)
			{
				const RECT& r = m_categoryRects[i];
				int w = r.right - r.left;
				int h = r.bottom - r.top;
				if (w <= 0 || h <= 0)
					continue;

				bool selected = ((int)i == m_categorySelected);
				bool hovered = ((int)i == m_categoryHover);

				Gdiplus::GraphicsPath path;
				BuildRoundRectPath(path, Gdiplus::Rect(r.left, r.top, w, h), h / 2);
				Gdiplus::SolidBrush chipBg(selected ? Gdiplus::Color(255, 255, 204, 0)
					: (hovered ? ThemeGray(58, 222) : ThemeGray(42, 236)));
				g.FillPath(&chipBg, &path);

				Gdiplus::Color textColor = selected ? Gdiplus::Color(255, 28, 28, 28)
					: (hovered ? white : ThemeGray(200, 90));
				drawText(m_categories[i].c_str(), selected ? f14b : f14, textColor,
					(float)r.left, (float)r.top, (float)w, (float)h, fmtCenter);
			}
		}
		else
		{
			// ---- 页面标题 ----
			const wchar_t* title = (page == 1) ? L"\u672C\u5730\u97F3\u4E50" : L"\u6211\u559C\u6B22\u7684\u97F3\u4E50";
			float tx = (float)(m_contentRect.left + kContentPad);
			float tw = (float)(m_contentRect.right - m_contentRect.left - 2 * kContentPad);
			drawText(title, f24b, white, tx, (float)(m_contentRect.top + 14), tw, 34.0f, fmtLeft);

			wchar_t sub[64];
			swprintf_s(sub, 64, L"\u5171 %d \u9996", (int)m_tracks[page].size());
			drawText(sub, f13, dimGray, tx, (float)(m_contentRect.top + 48), tw, 20.0f, fmtLeft);
		}

		// ---- 列表表头 ----
		const ListColumns c = CalcColumns(m_listHeaderRect);
		{
			float hy = (float)m_listHeaderRect.top;
			float hh = (float)(m_listHeaderRect.bottom - m_listHeaderRect.top);
			drawText(L"#", f13, dimGray, (float)c.idxX + 8, hy, (float)(kColIdx - 8), hh, fmtLeft);
			drawText(L"\u6807\u9898", f13, dimGray, (float)c.titleX, hy, (float)(c.artistX - c.titleX - 12), hh, fmtLeft);
			drawText(L"\u6B4C\u624B", f13, dimGray, (float)c.artistX, hy, (float)(c.albumX - c.artistX - 12), hh, fmtLeft);
			if (c.albumX < c.heartX)
				drawText(L"\u4E13\u8F91", f13, dimGray, (float)c.albumX, hy, (float)(c.heartX - c.albumX - 12), hh, fmtLeft);
			drawText(L"\u65F6\u957F", f13, dimGray, (float)c.durX, hy, (float)(kColDur - 8), hh, fmtRight);

			Gdiplus::Pen divider(ThemeGray(50, 232), 1.0f);
			g.DrawLine(&divider, (int)m_listHeaderRect.left, (int)m_listHeaderRect.bottom - 1,
				(int)m_listHeaderRect.right, (int)m_listHeaderRect.bottom - 1);
		}

		// ---- 歌曲列表 ----
		const std::vector<TrackItem>& list = m_tracks[page];
		const int listW = m_listRect.right - m_listRect.left;
		const int listH = m_listRect.bottom - m_listRect.top;
		if (listW <= 0 || listH <= 0)
			return;

		const int total = (int)list.size() * kRowHeight;
		const int maxScroll = total > listH ? total - listH : 0;
		m_scroll[page] = ClampInt(m_scroll[page], 0, maxScroll);
		const int scroll = m_scroll[page];

		if (list.empty())
		{
			const wchar_t* msg = (page == 0 && !m_onlineStatus.empty()) ? m_onlineStatus.c_str() : (page == 0) ? L"\u8F93\u5165\u5173\u952E\u8BCD\u641C\u7D22\u6B4C\u66F2\uFF0C\u6216\u9009\u62E9\u4E0A\u65B9\u5206\u7C7B"
				: (page == 1 ? L"\u6682\u65E0\u672C\u5730\u6B4C\u66F2" : L"\u8FD8\u6CA1\u6709\u559C\u6B22\u7684\u6B4C\u66F2\uFF0C\u70B9\u51FB\u6B4C\u66F2\u53F3\u4FA7\u7684\u7231\u5FC3\u5373\u53EF\u6536\u85CF");
			drawText(msg, f15, dimGray, (float)m_listRect.left, (float)m_listRect.top,
				(float)listW, (float)(listH > 160 ? 160 : listH), fmtCenter);
			return;
		}

		g.SetClip(Gdiplus::Rect(m_listRect.left, m_listRect.top, listW, listH));

		int first = scroll / kRowHeight;
		int last = (scroll + listH) / kRowHeight;
		if (last > (int)list.size() - 1)
			last = (int)list.size() - 1;

		for (int i = first; i <= last; i++)
		{
			const TrackItem& t = list[i];
			const int rowTop = m_listRect.top + i * kRowHeight - scroll;
			const float ry = (float)rowTop;
			const float rh = (float)kRowHeight;
			const float cy = ry + rh / 2.0f;

			bool selected = (i == m_selectedRow[page]);
			bool hovered = (i == m_hoverRow);
			bool playing = !m_nowPlayingTitle.empty() && t.title == m_nowPlayingTitle
				&& (m_nowPlayingArtist.empty() || t.artist == m_nowPlayingArtist);

			if (selected || hovered)
			{
				Gdiplus::GraphicsPath path;
				BuildRoundRectPath(path, Gdiplus::Rect(m_listRect.left, rowTop + 2, listW, kRowHeight - 4), 8);
				Gdiplus::SolidBrush rowBg(selected ? ThemeGray(50, 236) : ThemeGray(40, 248));
				g.FillPath(&rowBg, &path);
			}

			// 序号 / 正在播放的小三角
			if (playing)
			{
				float px = (float)c.idxX + 18.0f;
				Gdiplus::PointF tri[3] = {
					Gdiplus::PointF(px - 4.0f, cy - 5.0f),
					Gdiplus::PointF(px - 4.0f, cy + 5.0f),
					Gdiplus::PointF(px + 5.0f, cy)
				};
				Gdiplus::SolidBrush triBrush(accent);
				g.FillPolygon(&triBrush, tri, 3);
			}
			else
			{
				wchar_t num[16];
				swprintf_s(num, 16, L"%d", i + 1);
				drawText(num, f14, dimGray, (float)c.idxX + 8, ry, (float)(kColIdx - 8), rh, fmtLeft);
			}

			drawText(t.title.c_str(), f15, playing ? accent : white,
				(float)c.titleX, ry, (float)(c.artistX - c.titleX - 12), rh, fmtLeft);
			drawText(t.artist.c_str(), f14, gray,
				(float)c.artistX, ry, (float)(c.albumX - c.artistX - 12), rh, fmtLeft);
			if (c.albumX < c.heartX)
				drawText(t.album.c_str(), f14, gray,
					(float)c.albumX, ry, (float)(c.heartX - c.albumX - 12), rh, fmtLeft);

			// 爱心
			{
				Gdiplus::GraphicsPath heart;
				BuildHeartPath(heart, c.heartX + kColHeart / 2.0f, cy, 16.0f);
				if (t.favorite)
				{
					Gdiplus::SolidBrush heartBrush(Gdiplus::Color(255, 255, 64, 96));
					g.FillPath(&heartBrush, &heart);
				}
				else
				{
					Gdiplus::Pen heartPen(ThemeGray(120, 160), 1.5f);
					g.DrawPath(&heartPen, &heart);
				}
			}

			wchar_t dur[16];
			FormatDuration(t.durationSeconds, dur, 16);
			drawText(dur, f14, gray, (float)c.durX, ry, (float)(kColDur - 8), rh, fmtRight);
		}
		g.ResetClip();

		// ---- 滚动条 ----
		if (maxScroll > 0)
		{
			int thumbH = (int)((long long)listH * listH / total);
			if (thumbH < 30) thumbH = 30;
			int thumbY = m_listRect.top + (int)((long long)(listH - thumbH) * scroll / maxScroll);
			Gdiplus::GraphicsPath thumb;
			BuildRoundRectPath(thumb, Gdiplus::Rect(m_contentRect.right - 12, thumbY, 4, thumbH), 2);
			Gdiplus::SolidBrush thumbBrush(ThemeGray(90, 200));
			g.FillPath(&thumbBrush, &thumb);
		}
	}

	void WindowGUI::CreateSearchEdit()
	{
		if (m_searchEdit || !m_hwnd)
			return;

		m_searchBrush = CreateSolidBrush(ThemeRef(44, 238));   // 和搜索胶囊同色
		m_searchFont = CreateFontW(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
			DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
			CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei");

		m_searchEdit = CreateWindowExW(0, L"EDIT", L"",
			WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_LEFT,
			0, 0, 10, 10,
			m_hwnd, (HMENU)(INT_PTR)kSearchEditId, GetModuleHandleW(nullptr), nullptr);
		if (!m_searchEdit)
			return;

		SendMessageW(m_searchEdit, WM_SETFONT, (WPARAM)m_searchFont, TRUE);
		SendMessageW(m_searchEdit, EM_LIMITTEXT, 100, 0);
		// 灰色提示文字（需要程序启用 Common Controls v6 清单才会显示，否则忽略，不影响使用）
		SendMessageW(m_searchEdit, EM_SETCUEBANNER, TRUE, (LPARAM)L"\u641C\u7D22\u6B4C\u66F2\u3001\u6B4C\u624B");
		g_oldEditProc = (WNDPROC)SetWindowLongPtrW(m_searchEdit, GWLP_WNDPROC, (LONG_PTR)SearchEditProc);
	}

	void WindowGUI::LayoutSearchEdit()
	{
		if (!m_searchEdit)
			return;

		bool show = (m_sidebarSelected == 0) && (m_searchRect.right - m_searchRect.left > 90);
		if (show)
		{
			const int h = 22;
			int x = m_searchRect.left + 40;                  // 让出左侧放大镜
			int w = m_searchRect.right - 16 - x;
			int y = m_searchRect.top + (kSearchHeight - h) / 2;
			MoveWindow(m_searchEdit, x, y, w, h, TRUE);
			ShowWindow(m_searchEdit, SW_SHOW);
		}
		else
		{
			if (GetFocus() == m_searchEdit)
				SetFocus(m_hwnd);
			ShowWindow(m_searchEdit, SW_HIDE);
		}
	}

	void WindowGUI::SubmitSearch()
	{
		wchar_t buf[256] = { 0 };
		if (m_searchEdit)
			GetWindowTextW(m_searchEdit, buf, 256);
		SearchOnline(buf);          // 去互联网搜歌，结果自动显示在乐馆列表
		if (m_onSearch)
			m_onSearch(buf);
	}

	// ---------------- 在线试听 / 下载 ----------------
	void WindowGUI::SetOnlineAudioReadyCallback(std::function<void(const TrackItem&, const std::wstring&)> callback) { m_onOnlineAudioReady = std::move(callback); }
	void WindowGUI::SetOnlineMessageCallback(std::function<void(const std::wstring&)> callback) { m_onOnlineMessage = std::move(callback); }
	void WindowGUI::SetDownloadDir(const std::wstring& dir) { m_downloadDir = dir; }

	// important=true：没有设置消息回调时也要弹框告诉用户（结果/错误）；false：只是进度，没回调就不提示
	void WindowGUI::Notify(const std::wstring& msg, bool important)
	{
		if (m_onOnlineMessage)
		{
			m_onOnlineMessage(msg);
			return;
		}
		if (important)
			MessageBoxW(m_hwnd, msg.c_str(), L"YuMediaPlayer", MB_OK | MB_ICONINFORMATION);
	}

	void WindowGUI::PlayTrackItem(int page, int row, const TrackItem& track)
	{
		OnlineLog(L"PlayTrackItem page=" + std::to_wstring(page) + L" row=" + std::to_wstring(row)
			+ L" title=" + track.title + L" stream=" + (track.streamUrl.empty() ? L"(empty)" : track.streamUrl));
		if (!track.streamUrl.empty())
		{
			RunTrackTask(0, page, track);   // 在线歌曲：缓存整首 -> 回调给外部播放
			return;
		}
		if (page == 0)
		{
			Notify(OnlineMusic::GetSource() == OnlineMusic::SourceKugou
				? L"\u5F53\u524D\u6570\u636E\u6E90\uFF08\u9177\u72D7\uFF09\u53EA\u63D0\u4F9B\u6B4C\u66F2\u5217\u8868\uFF0C\u4E0D\u63D0\u4F9B\u64AD\u653E/\u4E0B\u8F7D\u5730\u5740\u3002\n\u8BF7\u5230 \u8BBE\u7F6E \u2192 \u97F3\u4E50\u6E90\uFF0C\u5207\u6362\u5230 iTunes \u6216\u81EA\u5EFA\u670D\u52A1\u5668\uFF0C\u518D\u70B9\u201C\u5237\u65B0\u5217\u8868\u201D\u3002"
				: L"\u8FD9\u9996\u6B4C\u6CA1\u6709\u53EF\u7528\u7684\u64AD\u653E\u5730\u5740\uFF0C\u6362\u4E00\u9996\u8BD5\u8BD5\uFF0C\u6216\u70B9\u8BBE\u7F6E\u91CC\u7684\u201C\u5237\u65B0\u5217\u8868\u201D\u91CD\u65B0\u52A0\u8F7D\u3002", true);
			return;
		}
		if (m_onTrackActivate)
			m_onTrackActivate(page, row);   // 本地 / 喜欢里的本地歌曲，照旧交给外部
	}

	void WindowGUI::RunTrackTask(int kind, int page, TrackItem track)
	{
		(void)page;
		if (!m_hwnd)
			return;
		HWND hwnd = m_hwnd;
		const int gen = (kind == 0) ? ++m_audioGen : 0;

		std::wstring dir = GetDownloadDir(m_downloadDir);
		std::wstring base;   // 歌词文件名（空 = "歌手 - 歌名"）
		if (kind == 2 || kind == 3)   // 3 = 自动下载歌词（静默）
		{
			// 歌词统一保存到 YuMediaPlayer 的 Assets\lrc 目录（exe 目录下的 Assets\lrc，不存在会自动创建）
			dir = DesktopLyric::GetLyricDir();
			if (track.streamUrl.empty() && !track.path.empty())
			{
				// 本地歌曲：文件名和歌曲同名，桌面歌词按歌曲文件名就能找到
				size_t sep = track.path.find_last_of(L"\\/");
				std::wstring name = (sep == std::wstring::npos) ? track.path : track.path.substr(sep + 1);
				size_t dot = name.find_last_of(L'.');
				base = (dot == std::wstring::npos) ? name : name.substr(0, dot);
			}
		}

		if (kind == 0)      Notify(L"\u6B63\u5728\u7F13\u51B2\uFF1A" + track.title + L" ...", false);
		else if (kind == 1) Notify(L"\u5F00\u59CB\u4E0B\u8F7D\uFF1A" + track.title + L" ...", false);
		else if (kind == 2) Notify(L"\u6B63\u5728\u83B7\u53D6\u6B4C\u8BCD\uFF1A" + track.title + L" ...", false);

		std::thread([hwnd, kind, gen, track, dir, base]()
		{
			TaskResult* r = new TaskResult();
			r->kind = kind;
			r->gen = gen;
			r->track = track;
			std::wstring err;
			if (kind == 0)      r->ok = OnlineMusic::CacheAudio(track, r->file, &err);
			else if (kind == 1) r->ok = OnlineMusic::DownloadSong(track, dir, r->file, &err);
			else                r->ok = OnlineMusic::DownloadLyrics(track, dir, base, r->file, &err);
			r->message = err;
			OnlineLog(std::wstring(L"task kind=") + std::to_wstring(kind) + L" ok=" + (r->ok ? L"1" : L"0")
				+ L" file=" + r->file + L" err=" + err);
			if (!IsWindow(hwnd) || !PostMessageW(hwnd, kMsgTaskDone, 0, (LPARAM)r))
				delete r;
		}).detach();
	}

	bool WindowGUI::AutoDownloadLyrics(const TrackItem& track)
	{
		if (!m_hwnd || track.title.empty())
			return false;
		const std::wstring key = LyricKey(track);
		if (!s_autoLyricTried.insert(key).second)
			return false;          // 这首歌已经自动试过了，别反复请求
		s_autoLyricKey = key;
		RunTrackTask(3, 1, track);
		return true;
	}

	void WindowGUI::ShowTrackMenu(POINT clientPt, int page, int row)
	{
		const TrackItem* src = GetTrack(page, row);
		if (!src || !m_hwnd)
			return;
		const TrackItem track = *src;   // 菜单是模态的，期间列表可能被刷新，所以先复制一份
		const bool online = (page == 0) || !track.streamUrl.empty();

		HMENU menu = CreatePopupMenu();
		AppendMenuW(menu, MF_STRING, 1, L"\u64AD\u653E / \u8BD5\u542C");
		if (online)
			AppendMenuW(menu, MF_STRING | (track.downloadUrl.empty() ? MF_GRAYED : 0), 2,
				track.downloadUrl.empty() ? L"\u4E0B\u8F7D\u6B4C\u66F2\uFF08\u4E0D\u53EF\u4E0B\u8F7D\uFF09" : L"\u4E0B\u8F7D\u6B4C\u66F2");
		AppendMenuW(menu, MF_STRING, 3, L"\u4E0B\u8F7D\u6B4C\u8BCD");

		POINT sp = clientPt;
		ClientToScreen(m_hwnd, &sp);
		SetForegroundWindow(m_hwnd);
		const int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, sp.x, sp.y, 0, m_hwnd, nullptr);
		PostMessageW(m_hwnd, WM_NULL, 0, 0);
		DestroyMenu(menu);

		if (cmd == 1)      PlayTrackItem(page, row, track);
		else if (cmd == 2) RunTrackTask(1, page, track);
		else if (cmd == 3) RunTrackTask(2, page, track);
	}

	// ---------------- 在线歌曲：异步获取 ----------------
	void WindowGUI::StartOnlineQuery(const std::wstring& keyword)
	{
		if (!m_hwnd)
			return;
		m_onlineStatus = L"\u6B63\u5728\u52A0\u8F7D\u6B4C\u66F2...";
		InvalidateRect(m_hwnd, &m_listRect, FALSE);
		const int gen = ++m_onlineGen;   // 新请求作废所有旧请求
		HWND hwnd = m_hwnd;
		std::thread([hwnd, gen, keyword]()
		{
			OnlineResult* r = new OnlineResult();
			r->gen = gen;
			r->ok = OnlineMusic::Search(keyword, 50, r->tracks, &r->error);
			if (!IsWindow(hwnd) || !PostMessageW(hwnd, kMsgOnlineResult, 0, (LPARAM)r))
				delete r;
		}).detach();
	}

	void WindowGUI::SearchOnline(const std::wstring& keyword)
	{
		size_t b = keyword.find_first_not_of(L" \t\r\n");
		if (b == std::wstring::npos)
		{
			// 搜索框是空的：回到当前分类的推荐列表
			LoadOnlineCategory(m_categorySelected);
			return;
		}
		size_t e = keyword.find_last_not_of(L" \t\r\n");
		StartOnlineQuery(keyword.substr(b, e - b + 1));
	}

	void WindowGUI::LoadOnlineCategory(int index)
	{
		if (index < 0 || index >= (int)m_categories.size())
			return;
		StartOnlineQuery(OnlineMusic::CategoryKeyword(m_categories[index]));
	}

	void WindowGUI::LoadOnlineHome()
	{
		LoadOnlineCategory(m_categorySelected);
	}

	int WindowGUI::HitTestRow(POINT pt) const
	{
		if (!PtInRect(&m_listRect, pt))
			return -1;
		const int page = m_sidebarSelected;
		int idx = (pt.y - m_listRect.top + m_scroll[page]) / kRowHeight;
		return (idx >= 0 && idx < (int)m_tracks[page].size()) ? idx : -1;
	}

	void WindowGUI::UpdateContentHover(POINT pt)
	{
		if (!m_hwnd)
			return;

		auto armLeave = [this]()
		{
			TRACKMOUSEEVENT tme = {};
			tme.cbSize = sizeof(TRACKMOUSEEVENT);
			tme.hwndTrack = m_hwnd;
			tme.dwFlags = TME_LEAVE;
			TrackMouseEvent(&tme);
		};

		// 列表行
		int newRow = HitTestRow(pt);
		if (newRow != m_hoverRow)
		{
			m_hoverRow = newRow;
			InvalidateRect(m_hwnd, &m_listRect, FALSE);
			if (newRow >= 0)
				armLeave();
		}

		// 分类标签（只有乐馆页才有）
		int newCat = -1;
		for (size_t i = 0; i < m_categoryRects.size(); i++)
		{
			if (PtInRect(&m_categoryRects[i], pt))
			{
				newCat = (int)i;
				break;
			}
		}
		if (newCat != m_categoryHover)
		{
			if (m_categoryHover >= 0 && m_categoryHover < (int)m_categoryRects.size())
			{
				RECT r = m_categoryRects[m_categoryHover];
				InflateRect(&r, 2, 2);
				InvalidateRect(m_hwnd, &r, FALSE);
			}
			if (newCat >= 0)
			{
				RECT r = m_categoryRects[newCat];
				InflateRect(&r, 2, 2);
				InvalidateRect(m_hwnd, &r, FALSE);
				armLeave();
			}
			m_categoryHover = newCat;
		}
	}

	bool WindowGUI::HandleContentClick(POINT pt)
	{
		if (!m_hwnd || !PtInRect(&m_contentRect, pt))
			return false;

		const int page = m_sidebarSelected;
		if (page == 0)
		{
			if (PtInRect(&m_searchIconRect, pt))
			{
				SubmitSearch();
				return true;
			}
			for (size_t i = 0; i < m_categoryRects.size(); i++)
			{
				if (PtInRect(&m_categoryRects[i], pt))
				{
					m_categorySelected = (int)i;
					InvalidateRect(m_hwnd, &m_contentRect, FALSE);
					LoadOnlineCategory((int)i);   // 切分类 -> 重新取该分类的歌
					if (m_onCategory)
						m_onCategory((int)i, m_categories[i]);
					return true;
				}
			}
		}

		int row = HitTestRow(pt);
		if (row < 0)
			return false;

		const ListColumns c = CalcColumns(m_listHeaderRect);
		if (pt.x >= c.heartX && pt.x < c.heartX + kColHeart)
		{
			// 点爱心：翻转收藏状态，具体存盘/同步由外部回调处理
			TrackItem& t = m_tracks[page][row];
			t.favorite = !t.favorite;
			InvalidateRect(m_hwnd, &m_listRect, FALSE);
			if (m_onFavoriteToggle)
				m_onFavoriteToggle(page, row);
		}
		else
		{
			m_selectedRow[page] = row;
			InvalidateRect(m_hwnd, &m_listRect, FALSE);
		}
		return true;
	}

	void WindowGUI::ScrollList(int deltaPixels)
	{
		if (!m_hwnd)
			return;
		const int page = m_sidebarSelected;
		int listH = m_listRect.bottom - m_listRect.top;
		int total = (int)m_tracks[page].size() * kRowHeight;
		int maxScroll = total > listH ? total - listH : 0;

		int newScroll = ClampInt(m_scroll[page] + deltaPixels, 0, maxScroll);
		if (newScroll == m_scroll[page])
			return;
		m_scroll[page] = newScroll;

		// 滚动后鼠标下面的行变了，重新算悬停行
		POINT pt;
		GetCursorPos(&pt);
		ScreenToClient(m_hwnd, &pt);
		m_hoverRow = HitTestRow(pt);

		RECT r = m_listRect;
		r.right = m_contentRect.right;   // 连同右侧滚动条一起刷新
		InvalidateRect(m_hwnd, &r, FALSE);
	}

	void WindowGUI::SetTracksInternal(int page, std::vector<TrackItem> tracks)
	{
		if (page < 0 || page >= kSidebarCount)
			return;
		m_tracks[page] = std::move(tracks);
		m_scroll[page] = 0;
		m_selectedRow[page] = -1;
		m_hoverRow = -1;
		if (m_hwnd)
			InvalidateRect(m_hwnd, &m_contentRect, FALSE);
	}
	void WindowGUI::SetOnlineTracks(std::vector<TrackItem> tracks) { SetTracksInternal(0, std::move(tracks)); }
	void WindowGUI::SetLocalTracks(std::vector<TrackItem> tracks) { SetTracksInternal(1, std::move(tracks)); }
	void WindowGUI::SetFavoriteTracks(std::vector<TrackItem> tracks)
	{
		m_tracks[2] = std::move(tracks);

		// 保留滚动位置（只按新的总高度夹一下），取消喜欢后列表不会跳回顶部
		const int listH = m_listRect.bottom - m_listRect.top;
		const int total = (int)m_tracks[2].size() * kRowHeight;
		const int maxScroll = total > listH ? total - listH : 0;
		m_scroll[2] = ClampInt(m_scroll[2], 0, maxScroll);
		if (m_selectedRow[2] >= (int)m_tracks[2].size())
			m_selectedRow[2] = -1;
		m_hoverRow = -1;
		if (m_hwnd)
			InvalidateRect(m_hwnd, &m_contentRect, FALSE);
	}

	const TrackItem* WindowGUI::GetTrack(int page, int row) const
	{
		if (page < 0 || page >= kSidebarCount || row < 0 || row >= (int)m_tracks[page].size())
			return nullptr;
		return &m_tracks[page][row];
	}

	void WindowGUI::SetTrackFavorite(const std::wstring& path, bool favorite)
	{
		bool changed = false;
		for (int p = 0; p < kSidebarCount; p++)
			for (TrackItem& t : m_tracks[p])
				if (t.path == path && t.favorite != favorite)
				{
					t.favorite = favorite;
					changed = true;
				}
		if (changed && m_hwnd)
			InvalidateRect(m_hwnd, &m_listRect, FALSE);
	}

	void WindowGUI::SetCategories(std::vector<std::wstring> categories)
	{
		m_categories = std::move(categories);
		m_categorySelected = 0;
		m_categoryHover = -1;
		if (m_hwnd)
		{
			RefreshLayout();
			InvalidateRect(m_hwnd, &m_contentRect, FALSE);
			LoadOnlineHome();   // categories changed: reload online tracks
		}
	}
	void WindowGUI::SetSearchCallback(std::function<void(const std::wstring&)> callback) { m_onSearch = std::move(callback); }
	void WindowGUI::SetCategoryCallback(std::function<void(int, const std::wstring&)> callback) { m_onCategory = std::move(callback); }
	void WindowGUI::SetTrackActivateCallback(std::function<void(int, int)> callback) { m_onTrackActivate = std::move(callback); }
	void WindowGUI::SetFavoriteToggleCallback(std::function<void(int, int)> callback) { m_onFavoriteToggle = std::move(callback); }

	void WindowGUI::SetSidebarSelected(int index)
	{
		if (index < 0 || index >= kSidebarCount || index == m_sidebarSelected)
			return;
		m_sidebarSelected = index;
		m_hoverRow = -1;
		m_categoryHover = -1;
		if (m_hwnd)
		{
			// 切页：重新布局（乐馆显示搜索框，其他页隐藏），再刷新侧边栏和内容区
			RefreshLayout();
			InvalidateRect(m_hwnd, &m_sidebarRect, FALSE);
			InvalidateRect(m_hwnd, &m_contentRect, FALSE);
		}
	}

	int WindowGUI::GetSidebarSelected() const
	{
		return m_sidebarSelected;
	}

	void WindowGUI::SetSidebarCallback(std::function<void(int)> callback)
	{
		m_onSidebarSelect = std::move(callback);
	}

	LRESULT WindowGUI::EventProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
	{
		switch (msg)
		{
		case WM_CREATE:
			// 窗口创建完成
			return 0;
		case WM_DESTROY:
			// 窗口销毁前的清理
			PostQuitMessage(0);
			return 0;
		case WM_CLOSE:
			// 用户点击关闭按钮
			// 这里可以选择隐藏而不是销毁
			CloseToMini();
			return 0;
		case WM_SIZE:
			// 窗口大小改变（拖动边缘/最大化/还原）后，region 必须跟着重建
			if (wParam != SIZE_MINIMIZED)
			{
				RefreshLayout();        // 重算各区域，并移动搜索框
				UpdateWindowRegion();
			}
			return 0;
		case WM_CTLCOLOREDIT:
			// 搜索框的文字/背景颜色，和搜索胶囊保持一致
			if (m_searchEdit && (HWND)lParam == m_searchEdit)
			{
				HDC hEditDc = (HDC)wParam;
				const COLORREF editBk = ThemeRef(44, 238);
				// 切换主题后，输入框背景刷子的颜色要跟着换
				LOGBRUSH lb = {};
				if (!m_searchBrush || GetObject(m_searchBrush, sizeof(lb), &lb) == 0 || lb.lbColor != editBk)
				{
					if (m_searchBrush)
						DeleteObject(m_searchBrush);
					m_searchBrush = CreateSolidBrush(editBk);
				}
				SetTextColor(hEditDc, ThemeRef(235, 30));
				SetBkColor(hEditDc, editBk);
				return (LRESULT)m_searchBrush;
			}
			return DefWindowProcW(hwnd, msg, wParam, lParam);
		case WM_COMMAND:
			if (LOWORD(wParam) == kSearchEditId)
			{
				// 输入框获得/失去焦点：胶囊边框变黄/变灰
				if (HIWORD(wParam) == EN_SETFOCUS || HIWORD(wParam) == EN_KILLFOCUS)
				{
					m_searchFocused = (HIWORD(wParam) == EN_SETFOCUS);
					RECT sr = m_searchRect;
					InflateRect(&sr, 2, 2);   // border/antialias pixels fall outside the exact rect
					InvalidateRect(hwnd, &sr, FALSE);
				}
				return 0;
			}
			return DefWindowProcW(hwnd, msg, wParam, lParam);
		case kMsgSearchSubmit:
			SubmitSearch();
			return 0;
		case kMsgTaskDone:
		{
			std::unique_ptr<TaskResult> r(reinterpret_cast<TaskResult*>(lParam));
			if (!r)
				return 0;
			if (r->kind == 0)
			{
				if (r->gen != m_audioGen)
					return 0;   // 中途又点了别的歌，这首丢掉
				if (!r->ok)
				{
					const std::wstring failMsg = L"\u8BD5\u542C\u5931\u8D25\uFF1A" + r->message;
					OnlineLog(failMsg);
					if (m_onOnlineMessage) m_onOnlineMessage(failMsg);
					MessageBoxW(m_hwnd, failMsg.c_str(), L"YuMediaPlayer", MB_OK | MB_ICONWARNING);   // 失败必须让人看见
				}
				else if (m_onOnlineAudioReady)
				{
					OnlineLog(L"-> m_onOnlineAudioReady: " + r->file);
					m_onOnlineAudioReady(r->track, r->file);
				}
				else
				{
					// 外部没有接 SetOnlineAudioReadyCallback：把缓存好的文件当成"本地歌曲"加到本地列表末尾，
					// 然后走本地歌曲的双击回调（m_onTrackActivate），这样就在你自己的播放器里播放。
					if (m_onTrackActivate)
					{
						TrackItem t = r->track;
						t.path = r->file;
						t.streamUrl.clear();
						t.downloadUrl.clear();
						int row = -1;
						for (size_t i = 0; i < m_tracks[1].size(); i++)
							if (_wcsicmp(m_tracks[1][i].path.c_str(), t.path.c_str()) == 0) { row = (int)i; break; }
						if (row < 0)
						{
							m_tracks[1].push_back(t);
							row = (int)m_tracks[1].size() - 1;
							InvalidateRect(m_hwnd, &m_listRect, FALSE);
						}
						OnlineLog(L"-> fallback m_onTrackActivate(1, " + std::to_wstring(row) + L") local list size="
							+ std::to_wstring(m_tracks[1].size()) + L" file=" + t.path
							+ L"  (\u5BBF\u4E3B\u5982\u679C\u6309\u81EA\u5DF1\u7684\u672C\u5730\u5217\u8868\u53D6\u6B4C\uFF0C\u8FD9\u91CC\u4F1A\u5BF9\u4E0D\u4E0A)");
						m_onTrackActivate(1, row);
					}
					else
					{
						OnlineLog(L"-> ShellExecute: " + r->file);
						ShellExecuteW(m_hwnd, L"open", r->file.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
					}
				}
			}
			else if (r->kind == 3)
			{
				// 自动下载歌词：不弹提示。成功且还是当前这首 -> 读进桌面歌词；失败就保持"暂无歌词"
				if (LyricKey(r->track) == s_autoLyricKey)
				{
					if (!(r->ok && DesktopLyric::Instance().LoadLrcFile(r->file)))
						DesktopLyric::Instance().ClearLyrics(L"\u6682\u65E0\u6B4C\u8BCD");
				}
				if (!r->ok)
					OnlineLog(L"auto lyric failed: " + r->message);
			}
			else if (r->ok && r->kind == 2)
			{
				// 歌词下载好了：如果就是正在播放的这首，马上读进桌面歌词
				if (r->track.title == m_nowPlayingTitle)
					DesktopLyric::Instance().LoadLrcFile(r->file);
				Notify(L"\u6B4C\u8BCD\u5DF2\u4FDD\u5B58\uFF1A" + r->file, true);
			}
			else if (r->ok)
				Notify((r->kind == 1 ? L"\u6B4C\u66F2\u5DF2\u4FDD\u5B58\uFF1A" : L"\u6B4C\u8BCD\u5DF2\u4FDD\u5B58\uFF1A") + r->file, true);
			else
				Notify((r->kind == 1 ? L"\u4E0B\u8F7D\u6B4C\u66F2\u5931\u8D25\uFF1A" : L"\u4E0B\u8F7D\u6B4C\u8BCD\u5931\u8D25\uFF1A") + r->message, true);
			return 0;
		}
		case kMsgOnlineResult:
		{
			std::unique_ptr<OnlineResult> r(reinterpret_cast<OnlineResult*>(lParam));
			// 只接受最新一次请求的结果；失败时保留当前列表不动
			if (r && r->gen == m_onlineGen)
			{
				if (r->ok)
				{
					m_onlineStatus = r->tracks.empty() ? L"\u6CA1\u6709\u627E\u5230\u76F8\u5173\u6B4C\u66F2" : L"";
					SetOnlineTracks(std::move(r->tracks));
				}
				else
				{
					// 失败：清空列表，把原因直接显示在列表区域
					m_onlineStatus = L"\u52A0\u8F7D\u5931\u8D25\uFF1A" + r->error;
					SetOnlineTracks(std::vector<TrackItem>());
				}
			}
			return 0;
		}
		case WM_LBUTTONDOWN:
		{
			// 点搜索框区域 -> 输入框拿焦点；点别处 -> 主窗口拿焦点（输入框失焦）
			POINT pt;
			pt.x = GET_X_LPARAM(lParam);
			pt.y = GET_Y_LPARAM(lParam);
			// 点进度条：开始拖动（拖动中只预览，松手才真正跳转）
			if (m_onSeek && m_totalSeconds > 0 && PtInRect(&m_progressBarRect, pt))
			{
				m_seeking = true;
				SetCapture(hwnd);
				m_currentSeconds = (int)(SeekRatioFromX(pt.x) * m_totalSeconds + 0.5f);
				InvalidateRect(hwnd, &m_playBarRect, FALSE);
				return 0;
			}
			// 点音量波浪线/圆点：开始拖动，实时改变音量
			if (m_volumeSliderRect.right > m_volumeSliderRect.left && PtInRect(&m_volumeSliderRect, pt))
			{
				m_volDragging = true;
				SetCapture(hwnd);
				SetVolume(VolumeFromX(pt.x));
				if (m_onVolume)
					m_onVolume(m_volume);
				return 0;
			}
			if (m_searchEdit && IsWindowVisible(m_searchEdit) && PtInRect(&m_searchRect, pt))
				SetFocus(m_searchEdit);
			else
				SetFocus(hwnd);
			return 0;
		}
		case WM_LBUTTONDBLCLK:
		{
			// 双击歌曲 -> 通知外部开始播放
			POINT pt;
			pt.x = GET_X_LPARAM(lParam);
			pt.y = GET_Y_LPARAM(lParam);
			int row = HitTestRow(pt);
			if (row >= 0)
			{
				const ListColumns c = CalcColumns(m_listHeaderRect);
				bool onHeart = (pt.x >= c.heartX && pt.x < c.heartX + kColHeart);
				if (!onHeart)
				{
					m_selectedRow[m_sidebarSelected] = row;
					InvalidateRect(hwnd, &m_listRect, FALSE);
					if (const TrackItem* t = GetTrack(m_sidebarSelected, row))
					{
						const TrackItem copy = *t;
						PlayTrackItem(m_sidebarSelected, row, copy);   // 在线歌曲走试听；本地歌曲仍然回调外部
					}
				}
			}
			return 0;
		}
		case WM_RBUTTONUP:
		{
			// 右键歌曲 -> 弹出菜单（播放 / 下载歌曲 / 下载歌词）
			POINT pt;
			pt.x = GET_X_LPARAM(lParam);
			pt.y = GET_Y_LPARAM(lParam);
			const int row = HitTestRow(pt);
			if (row >= 0)
			{
				m_selectedRow[m_sidebarSelected] = row;
				InvalidateRect(hwnd, &m_listRect, FALSE);
				ShowTrackMenu(pt, m_sidebarSelected, row);
				return 0;
			}
			break;
		}
		case WM_MOUSEWHEEL:
		{
			// 滚轮坐标是屏幕坐标；一格滚 3 行
			POINT pt;
			pt.x = GET_X_LPARAM(lParam);
			pt.y = GET_Y_LPARAM(lParam);
			ScreenToClient(hwnd, &pt);
			if (PtInRect(&m_contentRect, pt))
			{
				int delta = GET_WHEEL_DELTA_WPARAM(wParam);
				ScrollList(-delta * (kRowHeight * 3) / WHEEL_DELTA);
			}
			return 0;
		}
		case WM_ERASEBKGND:
			// 背景由 WM_PAINT 里的双缓冲自己画，禁止系统擦背景，避免闪烁
			return 1;
		case WM_PAINT:
		{
			// 绘制窗口
			PAINTSTRUCT ps;
			HDC hdcWnd = BeginPaint(hwnd, &ps);   // 屏幕 DC

			RECT rc;
			GetClientRect(hwnd, &rc);
			int width = rc.right - rc.left;
			int height = rc.bottom - rc.top;

			// ★ 双缓冲：先画到内存 DC，再一次性 BitBlt 到屏幕，避免闪烁
			HDC hdc = CreateCompatibleDC(hdcWnd);
			HBITMAP hMemBmp = CreateCompatibleBitmap(hdcWnd, width, height);
			HBITMAP hOldBmp = (HBITMAP)SelectObject(hdc, hMemBmp);

			// 更新按钮位置
			UpdateButtonRects(width);

			// 更新播放栏位置
			UpdatePlayBarRect(width, height);
			UpdateSidebarRect(width, height);   // 侧边栏位置依赖播放栏位置
			UpdateContentRects(width, height);  // 内容区位置依赖侧边栏和播放栏

			// 绘制背景
			HBRUSH hBgBrush = CreateSolidBrush(ThemeRef(24, 240));
			FillRect(hdc, &rc, hBgBrush);
			DeleteObject(hBgBrush);

			// 绘制标题栏背景（深灰色）
			/*RECT titleBarRect = { 0, 0, width, 30 };
			HBRUSH hTitleBrush = CreateSolidBrush(RGB(32, 32, 32));
			FillRect(hdc, &titleBarRect, hTitleBrush);
			DeleteObject(hTitleBrush);*/

			//// 绘制标题栏下方的分割线
			//HPEN hLinePen = CreatePen(PS_SOLID, 1, RGB(50, 50, 50));
			//HPEN hOldPen = (HPEN)SelectObject(hdc, hLinePen);
			//MoveToEx(hdc, 0, 36, nullptr);
			//LineTo(hdc, width, 36);
			//SelectObject(hdc, hOldPen);
			//DeleteObject(hLinePen);

			//// 绘制标题文本
			//SetBkMode(hdc, TRANSPARENT);
			//SetTextColor(hdc, RGB(200, 200, 200));

			/*HFONT hFont = CreateFontW(14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
				DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
				DEFAULT_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei");
			HFONT hOldFont = (HFONT)SelectObject(hdc, hFont);*/

			/*RECT textRect = { 10, 0, width - 150, 36 };
			DrawTextW(hdc, L"YuMediaPlayer", -1, &textRect,
				DT_LEFT | DT_VCENTER | DT_SINGLELINE);*/

			/*	SelectObject(hdc, hOldFont);
			DeleteObject(hFont);*/

			// 绘制标题栏左上角 logo
			if (m_logoIcon)
				DrawIconEx(hdc, kLogoX, SidebarLogoY(m_sidebarRect), m_logoIcon, kLogoSize, kLogoSize, 0, nullptr, DI_NORMAL);

			// 绘制按钮
			for (int i = 0; i < 5; i++)
			{
				bool hovered = (m_hoverButton == i);
				DrawButton(hdc, m_buttonRects[i], hovered, i);
			}
			// 绘制播放栏
			DrawPlayBar(hdc, m_playBarRect);
			// 绘制左侧导航栏
			DrawSidebar(hdc, m_sidebarRect);
			// 绘制右侧内容区（乐馆/本地/喜欢）
			DrawContent(hdc);
			
			// ★ 只把需要刷新的区域贴到屏幕
			BitBlt(hdcWnd,
				ps.rcPaint.left, ps.rcPaint.top,
				ps.rcPaint.right - ps.rcPaint.left,
				ps.rcPaint.bottom - ps.rcPaint.top,
				hdc, ps.rcPaint.left, ps.rcPaint.top, SRCCOPY);

			SelectObject(hdc, hOldBmp);
			DeleteObject(hMemBmp);
			DeleteDC(hdc);

			EndPaint(hwnd, &ps);
			return 0;
		}
		case WM_MOUSEMOVE:
		{
			POINT pt;
			pt.x = GET_X_LPARAM(lParam);
			pt.y = GET_Y_LPARAM(lParam);

			// 拖动进度条：实时预览当前位置
			if (m_seeking)
			{
				m_currentSeconds = (int)(SeekRatioFromX(pt.x) * m_totalSeconds + 0.5f);
				InvalidateRect(hwnd, &m_playBarRect, FALSE);
				return 0;
			}

			// 拖动音量圆点
			if (m_volDragging)
			{
				SetVolume(VolumeFromX(pt.x));
				if (m_onVolume)
					m_onVolume(m_volume);
				return 0;
			}

			int newHoverButton = -1;
			for (int i = 0; i < 5; i++)
			{
				if (PtInRect(&m_buttonRects[i], pt))
				{
					newHoverButton = i;
					break;
				}
			}

			// 如果悬停状态改变，重绘
			if (newHoverButton != m_hoverButton)
			{
				int oldHoverButton = m_hoverButton;
				m_hoverButton = newHoverButton;
				// 只重绘状态变化的两个按钮，不刷新整个窗口
				if (oldHoverButton >= 0)
				{
					RECT r = m_buttonRects[oldHoverButton];
					InflateRect(&r, 2, 2);
					InvalidateRect(hwnd, &r, FALSE);
				}
				if (newHoverButton >= 0)
				{
					RECT r = m_buttonRects[newHoverButton];
					InflateRect(&r, 2, 2);
					InvalidateRect(hwnd, &r, FALSE);
				}

				// 启用鼠标离开事件
				if (newHoverButton >= 0)
				{
					TRACKMOUSEEVENT tme = {};
					tme.cbSize = sizeof(TRACKMOUSEEVENT);
					tme.hwndTrack = hwnd;
					tme.dwFlags = TME_LEAVE;
					TrackMouseEvent(&tme);
				}
			}

			// 侧边栏悬停
			int newHoverSidebar = -1;
			for (int i = 0; i < kSidebarCount; i++)
			{
				if (PtInRect(&m_sidebarItemRects[i], pt))
				{
					newHoverSidebar = i;
					break;
				}
			}
			if (newHoverSidebar != m_sidebarHover)
			{
				m_sidebarHover = newHoverSidebar;
				InvalidateRect(hwnd, &m_sidebarRect, FALSE);

				if (newHoverSidebar >= 0)
				{
					TRACKMOUSEEVENT sideTme = {};
					sideTme.cbSize = sizeof(TRACKMOUSEEVENT);
					sideTme.hwndTrack = hwnd;
					sideTme.dwFlags = TME_LEAVE;
					TrackMouseEvent(&sideTme);
				}
			}

			UpdateContentHover(pt);   // 列表行 / 分类标签悬停

			return 0;
		}
		case WM_MOUSELEAVE:
		{
			if (m_hoverRow >= 0 || m_categoryHover >= 0)
			{
				m_hoverRow = -1;
				m_categoryHover = -1;
				InvalidateRect(hwnd, &m_contentRect, FALSE);
			}
			if (m_sidebarHover >= 0)
			{
				m_sidebarHover = -1;
				InvalidateRect(hwnd, &m_sidebarRect, FALSE);
			}
			if (m_hoverButton >= 0)
			{
				RECT r = m_buttonRects[m_hoverButton];
				InflateRect(&r, 2, 2);
				m_hoverButton = -1;
				InvalidateRect(hwnd, &r, FALSE);
			}
			return 0;
		}
		case WM_LBUTTONUP:
		{
			POINT pt;
			pt.x = GET_X_LPARAM(lParam);
			pt.y = GET_Y_LPARAM(lParam);

			// 进度条拖动结束：真正跳转
			if (m_seeking)
			{
				m_seeking = false;
				ReleaseCapture();
				const float ratio = SeekRatioFromX(pt.x);
				m_currentSeconds = (int)(ratio * m_totalSeconds + 0.5f);
				InvalidateRect(hwnd, &m_playBarRect, FALSE);
				if (m_onSeek)
					m_onSeek(ratio);
				return 0;
			}

			// 音量拖动结束
			if (m_volDragging)
			{
				m_volDragging = false;
				ReleaseCapture();
				SetVolume(VolumeFromX(pt.x));
				if (m_onVolume)
					m_onVolume(m_volume);
				return 0;
			}

			// 检查按钮点击
			if (PtInRect(&m_buttonRects[static_cast<int>(ButtonType::Close)], pt))
			{
				// 关闭按钮
				CloseToMini();
				return 0;
			}
			else if (PtInRect(&m_buttonRects[static_cast<int>(ButtonType::Maximize)], pt))
			{
				// 最大化按钮
				if (m_isMaximized)
				{
					// 恢复窗口
					ShowWindow(hwnd, SW_RESTORE);
					m_isMaximized = false;
				}
				else
				{
					// 最大化窗口
					ShowWindow(hwnd, SW_MAXIMIZE);
					m_isMaximized = true;
				}
				return 0;
			}
			else if (PtInRect(&m_buttonRects[static_cast<int>(ButtonType::Minimize)], pt))
			{
				// 最小化按钮
				ShowWindow(hwnd, SW_MINIMIZE);
				return 0;
			}
			else if (PtInRect(&m_buttonRects[static_cast<int>(ButtonType::Settings)], pt))
			{
				YuMediaPlayer::SettingPage& settingPage = GetSettingPage();   // 改成成员：MainWindow 也要给它挂目录回调

				if (!settingPage.Show(hwnd))
				{
					MessageBoxW(
						hwnd,
						L"设置窗口创建失败。",
						L"YuMediaPlayer",
						MB_OK | MB_ICONERROR
					);
				}
				if (m_onCheckSettings)
				{
					m_onCheckSettings();
				}
				// 设置按钮：功能待实现
				return 0;
			}
			else if (PtInRect(&m_buttonRects[static_cast<int>(ButtonType::Update)], pt))
			{
				// 更新按钮
				if (m_onCheckUpdate)
				{
					m_onCheckUpdate();
				}
				return 0;
			}
			// 播放栏：播放/暂停、上一曲、下一曲（和迷你窗口共用同一套逻辑，由外部回调实现）
			if (PtInRect(&m_playPauseRect, pt))
			{
				if (m_onPlayPause)
					m_onPlayPause();
				return 0;
			}
			if (PtInRect(&m_prevRect, pt))
			{
				if (m_onPrev)
					m_onPrev();
				return 0;
			}
			if (PtInRect(&m_nextRect, pt))
			{
				if (m_onNext)
					m_onNext();
				return 0;
			}
			// 循环模式按钮：点一下切换到下一个模式（列表循环 -> 单曲循环 -> 随机播放 -> 心动循环 -> ...）
			if (PtInRect(&m_loopRect, pt))
			{
				if (m_onLoop)
					m_onLoop();
				return 0;
			}
			// 侧边栏按钮（乐馆 / 本地 / 喜欢）
			for (int i = 0; i < kSidebarCount; i++)
			{
				if (PtInRect(&m_sidebarItemRects[i], pt))
				{
					SetSidebarSelected(i);
					if (m_onSidebarSelect)
						m_onSidebarSelect(i);
					return 0;
				}
			}
			// 右侧内容区：分类标签 / 放大镜 / 爱心 / 选中歌曲
			if (HandleContentClick(pt))
				return 0;
			// 标题栏拖动（如果点击不在按钮上）
			if (pt.y < 30)
			{
				ReleaseCapture();
				SendMessageW(hwnd, WM_NCLBUTTONDOWN, HTCAPTION, lParam);
				return 0;
			}

			return 0;
		}
		case WM_NCHITTEST:
		{
			const LONG border = 6;

			POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
			RECT wr;
			GetWindowRect(hwnd, &wr);
			// 检查是否在按钮上
			POINT clientPt = { pt.x - wr.left, pt.y - wr.top };
			for (int i = 0; i <	5; i++)
			{
				if (PtInRect(&m_buttonRects[i], clientPt))
					return HTCLIENT;  // 让客户区处理
			}
			bool left = pt.x < wr.left + border;
			bool right = pt.x >= wr.right - border;
			bool top = pt.y < wr.top + border;
			bool bottom = pt.y >= wr.bottom - border;
			/*bool middle = !left && !right && !top && !bottom;
			bool middleTop = !left && !right && top;
			bool middleBottom = !left && !right && bottom;
			bool center = pt.x >= wr.left + (wr.right - wr.left) / 2 && pt.y >= wr.top + (wr.bottom - wr.top) / 2;*/

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

			// 标题栏（用于拖动） - 卡片顶部区域
			// 标题栏（用于拖动）
			if (clientPt.y >= 0 && clientPt.y < 30)
			{
				return HTCAPTION;
			}
			/*if (pt.y >= wr.top && pt.y < wr.top + 30)
			{
				return HTCAPTION;
			}*/
			return HTCLIENT;
		}
		default:
			return DefWindowProcW(hwnd, msg, wParam, lParam);
		}
	}
	bool WindowGUI::InitWindow(const wchar_t* title, int width, int height)
	{
		// 步骤 1：注册窗口类
		static const wchar_t CLASS_NAME[] = L"YuMediaPlayerWindowGUIClass";
		static bool classRegistered = false;

		if (!classRegistered)
		{
			WNDCLASSEXW wc = {};
			wc.cbSize = sizeof(WNDCLASSEXW);
			wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;   // CS_DBLCLKS：支持双击歌曲播放
			wc.lpfnWndProc = WndProc;        // ★ 使用静态回调函数
			wc.cbClsExtra = 0;
			wc.cbWndExtra = 0;
			wc.hInstance = GetModuleHandleW(nullptr);
			wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
			wc.hbrBackground = nullptr;      // 不要背景刷，由我们自己绘制
			wc.lpszClassName = CLASS_NAME;
			wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);

			if (!RegisterClassExW(&wc))
			{
				return false;
			}

			classRegistered = true;
		}

		// 步骤 2：创建分层窗口（Layered Window）
		// WS_EX_LAYERED - 分层窗口，支持透明和半透明
		// WS_EX_TOPMOST - 总在最前
		// WS_EX_TOOLWINDOW - 隐藏任务栏（初始状态）
		m_hwnd = CreateWindowExW(
			WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
			CLASS_NAME,
			title,
			WS_POPUP | WS_CLIPCHILDREN,       // 弹出式窗口，无标题栏；CLIPCHILDREN：重绘时不覆盖搜索框子窗口
			100, 100,                         // 初始位置
			width = 1200, height = 720,           // 宽高
			nullptr,                          // 父窗口
			nullptr,                          // 菜单
			GetModuleHandleW(nullptr),
			this                              // 传入 this 指针给 WM_CREATE
		);

		if (!m_hwnd)
			return false;
		// 加载 Settings 和 Refresh 图标
		{
			wchar_t exePath[MAX_PATH] = { 0 };
			GetModuleFileNameW(nullptr, exePath, MAX_PATH);
			wchar_t* lastSlash = wcsrchr(exePath, L'\\');
			if (lastSlash)
				*lastSlash = L'\0';

			wchar_t iconPath[MAX_PATH] = { 0 };
			swprintf_s(iconPath, MAX_PATH, L"%s\\ui\\window\\Window10Settings.png", exePath);
			LoadSettingsIcon(iconPath); 
			swprintf_s(iconPath, MAX_PATH, L"%s\\ui\\window\\Window10Refresh.png", exePath);
			LoadRefreshIcon(iconPath);
			LoadLogoIcon();       // ui\\window\\logo.ico
			LoadSidebarIcons();   // ui\\window\\Sidebar_Music/Local/Heart.png

		}

		// 步骤 3：居中到屏幕中央
		{
			HMONITOR hMon = MonitorFromWindow(m_hwnd, MONITOR_DEFAULTTONEAREST);
			MONITORINFO mi = { sizeof(mi) };
			GetMonitorInfoW(hMon, &mi);

			int monW = mi.rcMonitor.right - mi.rcMonitor.left;
			int monH = mi.rcMonitor.bottom - mi.rcMonitor.top;

			int posX = mi.rcMonitor.left + (monW - width) / 2;
			int posY = mi.rcMonitor.top + (monH - height) / 2;

			SetWindowPos(m_hwnd, nullptr, posX, posY, width, height,
				SWP_NOZORDER | SWP_NOACTIVATE);
		}
		
		// 创建乐馆页的搜索框，并计算布局
		CreateSearchEdit();
		RefreshLayout();

		// 应用圆角（窗口尺寸已确定）
		UpdateWindowRegion();

		// 步骤 4：显示窗口
		ShowWindow(m_hwnd, SW_SHOW);
		UpdateWindow(m_hwnd);

		// 自动从互联网获取乐馆首页歌曲
		LoadOnlineHome();

		return true;
	}
	void WindowGUI::Run()
	{
		MSG msg = {};
		bool needsRedraw = true;

		while (msg.message != WM_QUIT)
		{
			if (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
			{
				TranslateMessage(&msg);
				DispatchMessageW(&msg);
				needsRedraw = true;
			}
			else
			{
				if (needsRedraw)
				{
					// 这里可以放置绘制逻辑
					// RenderFrame();
					needsRedraw = false;
				}
				else
				{
					WaitMessage();
				}
			}
		}
	}
	HWND WindowGUI::GetHWND() const
	{
		return m_hwnd;
	}

	void WindowGUI::ShowWindowGUI()
	{
		if (!m_hwnd)
			return;
		LONG_PTR exStyle = GetWindowLongPtrW(m_hwnd, GWL_EXSTYLE);

		// 显示任务栏按钮
		exStyle &= ~WS_EX_TOOLWINDOW;
		exStyle |= WS_EX_APPWINDOW;

		SetWindowLongPtrW(m_hwnd, GWL_EXSTYLE, exStyle);

		// 让 Shell 重新识别窗口的任务栏状态
		SetWindowPos(
			m_hwnd,
			HWND_TOPMOST,
			0, 0, 0, 0,
			SWP_NOMOVE |
			SWP_NOSIZE |
			SWP_NOACTIVATE |
			SWP_FRAMECHANGED
		);

		ShowWindow(m_hwnd, SW_SHOW);
		SetForegroundWindow(m_hwnd);
	}
	void WindowGUI::HideWindowGUI()
	{
		LONG_PTR exStyle = GetWindowLongPtrW(m_hwnd, GWL_EXSTYLE);

		// 隐藏任务栏按钮
		exStyle |= WS_EX_TOOLWINDOW;
		exStyle &= ~WS_EX_APPWINDOW;

		SetWindowLongPtrW(m_hwnd, GWL_EXSTYLE, exStyle);

		SetWindowPos(
			m_hwnd,
			HWND_TOPMOST,
			0, 0, 0, 0,
			SWP_NOMOVE |
			SWP_NOSIZE |
			SWP_NOACTIVATE |
			SWP_FRAMECHANGED
		);

		ShowWindow(m_hwnd, SW_HIDE);
	}

	// Close main window: hide it, then bring the mini player window back.
	void WindowGUI::SetMiniWindow(HWND miniHwnd)
	{
		m_miniHwnd = miniHwnd;
	}

	void WindowGUI::CloseToMini()
	{
		HideWindowGUI();

		if (m_miniHwnd && ::IsWindow(m_miniHwnd))
		{
			if (::IsIconic(m_miniHwnd))
				::ShowWindow(m_miniHwnd, SW_RESTORE);
			::ShowWindow(m_miniHwnd, SW_SHOW);
			::SetWindowPos(m_miniHwnd, HWND_TOPMOST, 0, 0, 0, 0,
				SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
		}
	}

	void WindowGUI::SetUpdateCallback(std::function<void()> callback)
	{
		m_onCheckUpdate = std::move(callback);
	}
	void WindowGUI::SetSettingsCallback(std::function<void()> callback)
	{
		m_onCheckSettings = std::move(callback);
	}

	// ---- 播放栏联动 ----
	void WindowGUI::SetPrevCallback(std::function<void()> callback) { m_onPrev = std::move(callback); }
	void WindowGUI::SetNextCallback(std::function<void()> callback) { m_onNext = std::move(callback); }
	void WindowGUI::SetPlayPauseCallback(std::function<void()> callback) { m_onPlayPause = std::move(callback); }
	void WindowGUI::SetSeekCallback(std::function<void(float)> callback) { m_onSeek = std::move(callback); }

	float WindowGUI::SeekRatioFromX(int x) const
	{
		const int l = m_progressBarRect.left;
		const int r = m_progressBarRect.right;
		if (r <= l)
			return 0.0f;
		float v = (float)(x - l) / (float)(r - l);
		if (v < 0.0f) v = 0.0f;
		if (v > 1.0f) v = 1.0f;
		return v;
	}

	SettingPage& WindowGUI::GetSettingPage()
	{
		if (!m_settingPage)
			m_settingPage = std::make_unique<SettingPage>();
		return *m_settingPage;
	}
}