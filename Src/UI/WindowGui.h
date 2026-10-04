#pragma once 
#include <memory>  
#include <windows.h>
#include <windowsx.h>
#include <gdiplus.h>
#include <string>
#include <functional>
#include <vector>
#pragma comment(lib, "gdiplus.lib")
namespace YuMediaPlayer
{
	// 歌曲列表中的一行（乐馆搜索结果 / 本地歌曲 / 喜欢的歌曲 共用）
	struct TrackItem
	{
		std::wstring title;
		std::wstring artist;
		std::wstring album;
		int durationSeconds = 0;
		bool favorite = false;
		std::wstring path;      // 本地文件路径，或在线歌曲 id，外部自己约定
		std::wstring streamUrl;     // 在线歌曲的完整播放地址（空 = 不能在线试听）
		std::wstring downloadUrl;   // 在线歌曲的下载地址（空 = 作者不允许下载）
	};

	class SettingPage;   // 设置页（完整定义在 Core/SettingPage.h）

	class WindowGUI
	{

	public:
		WindowGUI();
		~WindowGUI();
		bool InitWindow(const wchar_t* title, int width, int height);
		void Run();
		void ShowWindowGUI();
		void HideWindowGUI();
		HWND GetHWND() const;
		void DrawButton(HDC hdc, const RECT& rect, bool hovered, int btnType);
		void UpdateButtonRects(int width);
		void UpdateWindowRegion();
		void SetCornerRadius(int radius);	
		int GetCornerRadius() const; 
		void ApplyLegacyCornerRadius();
		void UpdatePlayBarRect(int width, int height);
		void DrawPlayBar(HDC hdc, const RECT& rect);
		void SetPlaying(bool playing);
		void SetLoopMode(int mode);   // 传 0/1/2，内部也会自动取模
		void SetVolume(float volume); // 0.0 ~ 1.0（只更新界面，不触发音量回调）
		// 用户拖动播放栏上的音量波浪线/圆点时回调，参数 0.0 ~ 1.0（拖动中会连续触发）
		void SetVolumeCallback(std::function<void(float)> callback);
		bool IsVolumeDragging() const { return m_volDragging; }
		// 点击播放栏上的"循环模式"按钮时回调（外部在这里切换到下一个循环模式，再调用 SetLoopMode 刷新图标）
		void SetLoopCallback(std::function<void()> callback);
		// 播放栏左侧的封面图（所有权交给窗口；传 nullptr 清空，回到灰色占位）
		void SetNowPlayingCover(std::unique_ptr<Gdiplus::Image> cover);
		void SetProgress(int currentSeconds, int totalSeconds);
		void SetNowPlaying(const wchar_t* title, const wchar_t* artist);
		// 告诉主窗口"迷你窗口是哪个"，关闭主窗口时用它把迷你窗口恢复显示
		void SetMiniWindow(HWND miniHwnd);
		// 关闭主窗口：隐藏主窗口，并把迷你窗口重新显示出来
		void CloseToMini();
		// 设置"检查更新"按钮被点击时要执行的回调（外部传入，比如绑定到 TrayIcon::CheckUpdate）
		void SetUpdateCallback(std::function<void()> callback);
		// 设置"设置"按钮被点击时要执行的回调（外部传入，比如绑定到 TrayIcon::ShowSettings）
		void SetSettingsCallback(std::function<void()> callback);
		// ---- 左侧导航栏：0=乐馆 1=本地 2=喜欢 ----
		void UpdateSidebarRect(int width, int height);
		void DrawSidebar(HDC hdc, const RECT& rect);
		void SetSidebarSelected(int index);              // 代码里切换选中项（不触发回调）
		int  GetSidebarSelected() const;
		// 用户点击侧边栏按钮时回调，参数是被点击的下标 0/1/2
		void SetSidebarCallback(std::function<void(int)> callback);

		// ---- 右侧内容区：页面 0=乐馆(搜索框+分类+列表) 1=本地(列表) 2=喜欢(列表) ----
		void UpdateContentRects(int width, int height);
		void DrawContent(HDC hdc);
		// 设置各页面的歌曲列表（会重置该页滚动位置）
		void SetOnlineTracks(std::vector<TrackItem> tracks);
		void SetLocalTracks(std::vector<TrackItem> tracks);
		// 喜欢页：和另外两页不同，更新时保留当前滚动位置（取消喜欢后行消失，列表不会跳回顶部）
		void SetFavoriteTracks(std::vector<TrackItem> tracks);
		// 取某页某行的歌曲，越界返回 nullptr（指针只在下次修改列表前有效）
		const TrackItem* GetTrack(int page, int row) const;
		// 按路径把所有页里同一首歌的爱心标记同步成 favorite（不重置滚动/选中）
		void SetTrackFavorite(const std::wstring& path, bool favorite);
		// 乐馆分类标签，默认已经有一组（推荐/流行/国语...）
		void SetCategories(std::vector<std::wstring> categories);
		// 用户在搜索框按回车或点放大镜时回调，参数是搜索框里的文字
		void SetSearchCallback(std::function<void(const std::wstring&)> callback);
		// 用户点击分类标签时回调：下标 + 分类名
		void SetCategoryCallback(std::function<void(int, const std::wstring&)> callback);
		// 双击歌曲时回调：page(0乐馆/1本地/2喜欢) + 行号，外部在这里开始播放
		void SetTrackActivateCallback(std::function<void(int, int)> callback);
		// 点击歌曲右侧爱心时回调：page + 行号（flag 已经在列表里翻转过了）
		void SetFavoriteToggleCallback(std::function<void(int, int)> callback);
		// ---- 播放栏联动（主窗口 <-> 迷你窗口 MainWindow）----
		// 主窗口播放栏上的 上一曲 / 下一曲 / 播放暂停 被点击时回调（外部在这里转给迷你窗口的同名逻辑）
		void SetPrevCallback(std::function<void()> callback);
		void SetNextCallback(std::function<void()> callback);
		void SetPlayPauseCallback(std::function<void()> callback);
		// 用户点击/拖动进度条并松手时回调，参数是 [0,1] 的比例
		void SetSeekCallback(std::function<void(float)> callback);
		// 正在拖动进度条（此时 SetProgress 不会覆盖拖动预览）
		bool IsSeeking() const { return m_seeking; }
		// ---- 在线歌曲（异步，不会卡界面；结果自动显示在乐馆列表）----
		void SearchOnline(const std::wstring& keyword);   // 搜索互联网歌曲；keyword 为空则加载当前分类
		void LoadOnlineCategory(int index);               // 加载某个分类的歌曲
		void LoadOnlineHome();                            // 加载当前分类（首页）
		// ---- 在线试听 / 下载（右键菜单：播放、下载歌曲、下载歌词）----
		// 双击/右键"播放"一首在线歌曲后，整首歌缓存完成时回调；localFile 是本地 mp3，按播放本地歌曲的方式播放即可
		void SetOnlineAudioReadyCallback(std::function<void(const TrackItem&, const std::wstring&)> callback);
		// 接收进度/结果提示（比如显示成 toast）。不设置的话，成功/失败结果会用消息框提示
		void SetOnlineMessageCallback(std::function<void(const std::wstring&)> callback);
		// 自动下载歌词（后台、不弹提示）：播放器换歌后找不到 .lrc 时调用。
		// 同一首歌（歌名+歌手）本次运行只会尝试一次；返回 true = 已经开始下载。
		// 下载成功后如果还是当前这首，会直接读进桌面歌词。
		bool AutoDownloadLyrics(const TrackItem& track);
		// 下载目录，默认 "音乐\YuMediaPlayer"
		void SetDownloadDir(const std::wstring& dir);
		// 设置页：由 WindowGUI 持有（第一次调用时创建，构造时会自己读 settings.ini）
		SettingPage& GetSettingPage();

	private:
		float SeekRatioFromX(int x) const;    // 鼠标 x -> 进度条比例 [0,1]
		float VolumeFromX(int x) const;       // 鼠标 x -> 音量比例 [0,1]
		static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
		LRESULT EventProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
		bool LoadSettingsIcon(const wchar_t* imagePath);
		bool LoadRefreshIcon(const wchar_t* imagePath);
		bool LoadLogoIcon();       // 加载 background\\logo.ico（标题栏左上角大图标）
		bool LoadSidebarIcons();   // 从 exe 目录下 background\\Sidebar_*.png 加载
		void RefreshLayout();                 // 重新计算所有区域，并摆放搜索框
		void CreateSearchEdit();              // 创建乐馆页的搜索输入框（子窗口 EDIT）
		void LayoutSearchEdit();              // 按搜索框区域移动/显示/隐藏 EDIT
		void SubmitSearch();                  // 读取输入框文字并触发搜索回调
		void UpdateContentHover(POINT pt);    // 鼠标移动：更新列表行/分类标签的悬停
		bool HandleContentClick(POINT pt);    // 鼠标点击：分类/放大镜/爱心/选中行
		void ScrollList(int deltaPixels);     // 滚动当前页列表
		int  HitTestRow(POINT pt) const;      // 命中当前页的第几行，没有返回 -1
		void SetTracksInternal(int page, std::vector<TrackItem> tracks);   // 三个 SetXxxTracks 共用
		void StartOnlineQuery(const std::wstring& keyword);   // 起后台线程取数据
		void PlayTrackItem(int page, int row, const TrackItem& track);   // 双击 / 菜单"播放"
		void RunTrackTask(int kind, int page, TrackItem track);          // kind: 0=缓存试听 1=下载歌曲 2=下载歌词（后台线程）
		void ShowTrackMenu(POINT clientPt, int page, int row);           // 右键菜单
		void Notify(const std::wstring& msg, bool important);
	private:
		// 窗口
		HWND						  m_hwnd;
		//Theme						  m_theme;
		
		int m_hoverButton;	
		bool m_isMaximized;
		// 按钮数量是 5（Close/Maximize/Minimize/Settings/Updata），WindowGUI.CPP 里所有循环都按 5 个访问。
		RECT m_buttonRects[5];
		int m_cornerRadius;
		HWND m_miniHwnd = nullptr;   // 迷你窗口（MainWindow）的句柄，不拥有它

		std::function<void()> m_onCheckSettings;
		std::function<void()> m_onCheckUpdate;

		bool  m_isPlaying = false;
		int   m_loopMode = 0;     // 0=列表循环 1=单曲循环 2=随机播放 3=心动循环
		float m_volume = 0.7f;

		int m_currentSeconds = 0;
		int m_totalSeconds = 0;

		RECT m_progressBarRect{};   // 整条滑轨的命中区域(带上下容差)，给以后拖动用
		RECT m_progressThumbRect{}; // 圆点本身的精确命中区域

		RECT m_playPauseRect{};
		RECT m_prevRect{};
		RECT m_nextRect{};
		RECT m_loopRect{};
		RECT m_volumeRect{};
		RECT m_playBarRect{};
		std::wstring m_nowPlayingTitle;
		std::wstring m_nowPlayingArtist;

		Gdiplus::Image* m_settingsIcon;
		Gdiplus::Image* m_refreshIcon;	
		ULONG_PTR m_gdiplusToken;

		HICON m_logoIcon = nullptr;   // 标题栏 logo，析构时 DestroyIcon

		// 左侧导航栏（乐馆 / 本地 / 喜欢）
		enum { kSidebarCount = 3 };
		RECT m_sidebarRect{};
		RECT m_sidebarItemRects[kSidebarCount]{};
		Gdiplus::Image* m_sidebarIcons[kSidebarCount] = {};   // 白色单色图标，绘制时按状态染色
		int m_sidebarSelected = 0;
		int m_sidebarHover = -1;
		std::function<void(int)> m_onSidebarSelect;

		// 右侧内容区
		RECT m_contentRect{};        // 内容卡片整体
		RECT m_searchRect{};         // 搜索框（仅乐馆页有效）
		RECT m_searchIconRect{};     // 搜索框左侧放大镜点击区
		RECT m_listHeaderRect{};     // 列表表头（# 标题 歌手 专辑 时长）
		RECT m_listRect{};           // 列表行区域
		std::vector<RECT> m_categoryRects;   // 与 m_categories 一一对应，放不下的是空矩形
		std::vector<std::wstring> m_categories;
		int m_categorySelected = 0;
		int m_categoryHover = -1;
		std::vector<TrackItem> m_tracks[kSidebarCount];
		int m_scroll[kSidebarCount] = {};
		int m_selectedRow[kSidebarCount] = { -1, -1, -1 };
		int m_hoverRow = -1;

		HWND m_searchEdit = nullptr;
		HBRUSH m_searchBrush = nullptr;
		HFONT m_searchFont = nullptr;
		bool m_searchFocused = false;

		std::function<void(const std::wstring&)> m_onSearch;
		std::function<void(int, const std::wstring&)> m_onCategory;
		std::function<void(int, int)> m_onTrackActivate;
		std::function<void(int, int)> m_onFavoriteToggle;

		// 播放栏联动
		std::function<void()> m_onPrev;
		std::function<void()> m_onNext;
		std::function<void()> m_onPlayPause;
		std::function<void(float)> m_onSeek;
		bool m_seeking = false;

		// 音量：波浪线 + 圆点（DrawPlayBar 里画，同时算出命中区域）
		RECT m_volumeSliderRect{};
		bool m_volDragging = false;
		std::function<void(float)> m_onVolume;
		std::function<void()> m_onLoop;

		// 播放栏封面（GDI+ 图像，必须在 GdiplusShutdown 之前释放）
		std::unique_ptr<Gdiplus::Image> m_coverImage;

		std::unique_ptr<SettingPage> m_settingPage;

		std::wstring m_onlineStatus;   // 乐馆列表为空时显示的提示（加载中 / 失败原因 / 无结果）
		std::function<void(const TrackItem&, const std::wstring&)> m_onOnlineAudioReady;
		std::function<void(const std::wstring&)> m_onOnlineMessage;
		std::wstring m_downloadDir;
		int m_audioGen = 0;    // 试听请求序号，只播最后一次点的那首
		int m_onlineGen = 0;   // 在线请求序号（只在 UI 线程读写），用来丢弃过期结果

	};
}
