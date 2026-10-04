#pragma once
#include <windows.h>
#include <gdiplus.h>
#include <memory>
#include <string>
#include <vector>
#include <functional>
#include <utility>
#pragma comment(lib, "gdiplus.lib")

namespace YuMediaPlayer
{
	// 桌面歌词的外观。由设置页的"歌词"页驱动（SettingPage 改动后会自动调用 ApplySettings）
	struct LyricStyle
	{
		std::wstring fontName;      // 空 = 默认的"胡敬礼毛笔行书简"（找不到就退回微软雅黑）
		int  fontSize = 30;         // 字号（pt）
		int  colorMode = 1;         // 0=黄色  1=七彩（颜色会随时间流动变化）
		int  rainbowSpeed = 10;      // 七彩流动速度 1~10
		bool locked = false;        // 锁定：鼠标穿透，不能拖动
	};

	// 桌面歌词：一个无边框、半透明、始终置顶的悬浮窗口，显示"当前句 + 下一句"。
	// 全局只有一个实例（DesktopLyric::Instance()），所以右键菜单 / 设置页 / 播放器进度回调
	// 都可以直接调用它，不用互相传指针。必须在 UI 线程使用。
	//
	// 用法（接入播放器只需要三步）：
	//   1) 开始播放一首歌时：   DesktopLyric::Instance().LoadForTrack(路径, 标题, 歌手);
	//   2) 播放进度更新时：     DesktopLyric::Instance().SetPosition(当前毫秒);
	//   3) 播放/暂停切换时：    DesktopLyric::Instance().SetPlaying(true/false);
	class DesktopLyric
	{
	public:
		static DesktopLyric& Instance();

		// ---- 显示 / 隐藏 ----
		bool Show();
		void Hide();
		void Toggle();
		bool IsVisible() const;
		HWND GetHWND() const { return m_hwnd; }

		// ---- 外观 ----
		void SetStyle(const LyricStyle& style);
		const LyricStyle& GetStyle() const { return m_style; }
		// 窗口左上角位置（屏幕坐标）。不调用 = 自动放在屏幕下方居中
		void SetPlacement(int x, int y);

		// ---- 歌词内容 ----
		// 解析 LRC 文本（支持一行多个时间戳、[offset:]、增强格式里的 <mm:ss.xx> 会被忽略）
		bool SetLyricsFromLrc(const std::wstring& lrcText);
		// 读取 .lrc 文件（自动识别 UTF-8 / UTF-16 / GBK）
		bool LoadLrcFile(const std::wstring& path);
		// 按歌曲查找歌词：1) 与音频同名的 .lrc  2) exe\Assets\lrc\（以及 exe\lrc\）与额外目录里的
		// "标题.lrc"/"歌手 - 标题.lrc"，还找不到就在这些目录里按"文件名包含歌名"模糊匹配
		// 找不到就显示"暂无歌词"
		bool LoadForTrack(const std::wstring& audioPath, const std::wstring& title, const std::wstring& artist);
		// 歌词默认目录：exe目录\Assets\lrc（不存在会自动创建）。下载歌词时也保存到这里
		static std::wstring GetLyricDir();
		// 额外的歌词目录（比如歌词下载目录）
		void AddSearchDir(const std::wstring& dir);
		// 清空歌词，可显示一行提示文字
		void ClearLyrics(const wchar_t* hint = nullptr);

		// ---- 播放进度 ----
		// 播放进度（毫秒）。进度只有秒级精度也没关系：播放中内部会按系统时钟插值，句子切换更准
		void SetPosition(int positionMs);
		void SetPlaying(bool playing);

		// ---- 回调 ----
		// 用户拖动窗口后回调新位置（设置页用它保存位置）
		void SetMovedCallback(std::function<void(int, int)> cb) { m_onMoved = std::move(cb); }
		// 用户点歌词上的"关闭"/"锁定"小图标后回调，让设置页同步开关状态
		void SetUserChangedCallback(std::function<void(bool visible, bool locked)> cb) { m_onUserChanged = std::move(cb); }

	private:
		DesktopLyric();
		~DesktopLyric();
		DesktopLyric(const DesktopLyric&) = delete;
		DesktopLyric& operator=(const DesktopLyric&) = delete;

		// marks：增强 LRC 里的逐字时间戳 (字符下标, 绝对毫秒)；普通 LRC 没有，为空，此时按整句时长估算每个字的时间
		struct Line { int timeMs; std::wstring text; std::vector<std::pair<int, int>> marks; };

		static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
		LRESULT EventProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
		bool EnsureWindow();
		void ResolveFont();
		int  CurrentIndex() const;
		float PlayedChars(int index, int posMs) const;   // 第 index 句在 posMs 时已经"唱到"第几个字（带小数）
		int  EstimatePositionMs() const;
		void Render();
		void ApplyLocked();
		// 悬停时出现在歌词上方的小图标：锁定 / 关闭（和网易云、QQ音乐一样，没有右键菜单）
		enum Button { BtnNone = 0, BtnLock = 1, BtnClose = 2 };
		void LayoutButtons(int width);
		Button HitButton(POINT clientPt) const;
		void UpdateClickThrough(bool overButton);
		void OnButtonClick(Button b);
		static std::wstring DecodeText(const std::string& bytes);
		// LRC 头部的元数据标签 [ti:] [ar:] [al:] [au:] [by:]
		struct LrcMeta { std::wstring ti, ar, al, au, by; };
		static void ParseLrc(const std::wstring& text, std::vector<Line>& out, LrcMeta* meta = nullptr);
		// 前奏填充：把开头的"作词/作曲/编曲/演唱…"信息行均匀铺到第一句歌词之前；
		// 歌词里没有这类信息时，用"歌名 - 歌手"补一行，避免前奏时歌词栏是空的
		static void BuildIntro(std::vector<Line>& lines, const std::wstring& title, const std::wstring& artist);

	private:
		HWND m_hwnd = nullptr;
		ULONG_PTR m_gdiplusToken = 0;
		LyricStyle m_style;

		bool m_hasPos = false;
		int  m_posX = 0, m_posY = 0;

		std::vector<Line> m_lines;
		std::wstring m_hint = L"\u266A YuMediaPlayer \u684C\u9762\u6B4C\u8BCD";   // ♪ YuMediaPlayer 桌面歌词
		std::vector<std::wstring> m_searchDirs;
		std::wstring m_trackTitle, m_trackArtist;   // 当前曲目的标题/歌手（LoadForTrack 时记录，前奏信息用）

		// 进度插值
		int  m_basePosMs = 0;
		ULONGLONG m_baseTick = 0;
		bool m_playing = false;

		// 渲染状态
		float m_hue = 0.0f;              // 七彩的相位 0~1
		int   m_lastIndex = -2;
		bool  m_hover = false;
		bool  m_dirty = true;
		RECT  m_btnLock{}, m_btnClose{};     // 小图标的位置（窗口客户区坐标），LayoutButtons 每次绘制时更新
		Button m_hotBtn = BtnNone;           // 鼠标正压在哪个图标上

		// 字体
		std::unique_ptr<Gdiplus::PrivateFontCollection> m_privateFonts;
		std::unique_ptr<Gdiplus::FontFamily> m_family;
		std::wstring m_resolvedFor;      // 上次解析字体时的设置，没变就不重复解析
		bool m_fontResolved = false;

		std::function<void(int, int)> m_onMoved;
		std::function<void(bool, bool)> m_onUserChanged;
	};
}
