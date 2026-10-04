#pragma once
#include <windows.h>
#include <windowsx.h>
#include <gdiplus.h>
#include <string>
#include <vector>
#include <functional>
#include "../../version.h"		
#pragma comment(lib, "gdiplus.lib")

namespace YuMediaPlayer
{
	// 播放器的全部设置项。想加新设置：1) 这里加字段 2) Clamp() 里限制范围
	// 3) Load()/Save() 里读写 4) SettingPage::BuildItems() 里加一行界面项
	struct PlayerSettings
	{	
		// ---- 语言主题 ----
		int  language = 0;                // 0=简体中文 1=English 2=日本語 3=한국어 4=繁體中文
		int  theme = 0;                  // 0=深色 1=浅色
		std::wstring fontName;         // 界面字体名，空=默认（微软雅黑）
		// ---- 播放 ----
		int  loopMode = 0;              // 0=列表循环 1=单曲循环 2=随机播放 3=心动循环（和 WindowGUI::SetLoopMode 一致）
		int  volume = 50;               // 默认音量 0~100
		int  fadeSeconds = 0;           // 切歌淡入淡出时长（秒），0=关闭
		bool autoPlayOnStart = false;   // 启动后自动播放
		bool resumeLastTrack = true;    // 启动时恢复上次播放的歌曲

		// ---- 界面 ----
		int  cornerRadius = 13;         // 主窗口圆角半径 0~30
		bool alwaysOnTop = true;        // 主窗口置顶

		// ---- 系统 ----
		bool startWithWindows = false;  // 开机自动启动（写注册表 Run 项）
		bool autoCheckUpdate = true;    // 启动时自动检查更新
		std::wstring musicFolder;       // 本地音乐目录，空=未设置

		// ---- 桌面歌词（设置页"歌词"）----
		bool lyricEnabled = true;       // 是否显示桌面歌词（默认显示）
		bool lyricLocked = false;       // 锁定歌词（鼠标穿透，不能拖动）
		std::wstring lyricFontName;     // 歌词字体名，空=默认的"胡敬礼毛笔行书简"
		int  lyricFontSize = 30;        // 歌词字号（pt）6~72
		int  lyricColorMode = 1;        // 0=黄色 1=七彩（颜色流动变化），默认七彩
		int  lyricRainbowSpeed = 10;    // 七彩变化速度 1~10，默认最快
		bool lyricHasPos = false;       // 是否记住了窗口位置
		int  lyricX = 0;                // 歌词窗口左上角（屏幕坐标）
		int  lyricY = 0;

		// ---- 音乐源（设置页"音乐源"）----
		int  musicSource = 1;           // 0=酷狗(仅列表) 1=iTunes(30秒试听) 2=自建Subsonic服务器
		std::wstring jamendoClientId;   // Jamendo 的 client_id，写在 settings.ini 的 [Music] JamendoClientId（程序不会覆盖它）

		void Reset();                   // 恢复默认值
		void Clamp();                   // 把各字段限制在合法范围内
	};

	// 设置页：一个独立的深色圆角弹出窗口，左侧分类（通用/界面/系统/歌词/音乐源/关于），右侧是各设置项。
	// 设置保存在 exe 同目录的 settings.ini，改动即时生效并自动保存。
	class SettingPage
	{
	public:
		SettingPage();
		~SettingPage();
		SettingPage(const SettingPage&) = delete;
		SettingPage& operator=(const SettingPage&) = delete;

		// 显示设置页（第一次调用时创建窗口）。owner 是主窗口，设置页会居中在它上面，并且始终盖在它上面
		bool Show(HWND owner);
		void Hide();
		bool IsVisible() const;
		HWND GetHWND() const;

		// 从 / 向 settings.ini 读写
		void Load();
		void Save() const;

		const PlayerSettings& GetSettings() const;
		// 在代码里直接改设置（不触发 changed 回调），并刷新界面
		void SetSettings(const PlayerSettings& settings);

		// 任何设置被用户修改时回调（拖动滑块时会连续触发，方便实时预览）
		void SetChangedCallback(std::function<void(const PlayerSettings&)> callback);
		// 点击"检查更新"按钮时回调
		void SetCheckUpdateCallback(std::function<void()> callback);
		// 点击"检查设置"按钮时回调
		void SetCheckSettingsCallback(std::function<void()> callback);
		// 本地音乐目录被改动时回调（选了新目录 / 恢复默认清空目录），参数是新目录，空=未设置。
		// Load()/SetSettings() 不触发；启动时请自己用 GetSettings().musicFolder 扫描一次
		void SetMusicFolderChangedCallback(std::function<void(const std::wstring&)> callback);
		// 音乐源被切换 / 重新读取 client_id 时回调（外部在这里重新加载在线歌曲）。Load()/SetSettings() 不触发
		void SetMusicSourceChangedCallback(std::function<void()> callback);
		// "关于"页显示的版本号文字，默认 1.0.0
		void SetVersionText(const wchar_t* text);

	private:
		enum { kTabCount = 6 };   // 通用 / 界面 / 系统 / 歌词 / 音乐源 / 关于
		enum ItemType { ItemToggle, ItemSlider, ItemChoice, ItemButton, ItemInfo, ItemCombo };
		enum Action { ActNone, ActCheckUpdate, ActReset, ActPickFolder, ActOpenIni, ActReloadMusicSource };
		enum HitKind { HitNone, HitClose, HitNav, HitItem, HitComboList };

		struct Item
		{
			ItemType type = ItemInfo;
			std::wstring label;
			std::wstring desc;
			bool* boolValue = nullptr;          // Toggle
			int* intValue = nullptr;            // Slider / Choice
			const std::wstring* strValue = nullptr;   // Button：当前值显示在描述行（比如目录路径）
			int minValue = 0;
			int maxValue = 100;
			std::wstring unit;                  // Slider 数值后缀，如 L"%"
			std::vector<std::wstring> options;  // Choice 的选项
			Action action = ActNone;            // Button 点击后做什么
			std::wstring buttonText;
			std::wstring infoValue;             // Info 右侧文字
			std::wstring* comboStr = nullptr;   // Combo：按字符串保存选中项（字体名）；为空则用 intValue 保存下标
			bool fontList = false;              // Combo：第 0 项=默认（空字符串），其余是字体名，下拉里用各自的字体预览
			bool compact = false;               // Choice：true=控件放在行右侧（只占一行高），false=控件放在第二行
			int optionW = 0;                    // Choice：每个选项的宽度，0=默认
			RECT rowRect{};
			RECT ctrlRect{};
		};

		struct Hit
		{
			HitKind kind = HitNone;
			int a = -1;     // Nav：分类下标；Item/ComboList：设置项下标
			int b = -1;     // Choice：选项下标；ComboList：下拉里的第几项（-1=边距）
			bool operator==(const Hit& o) const { return kind == o.kind && a == o.a && b == o.b; }
			bool operator!=(const Hit& o) const { return !(*this == o); }
		};

	private:
		static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
		LRESULT EventProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
		bool CreateSettingWindow(HWND owner);
		void BuildItems();
		void LayoutItems();
		void Paint(HDC hdc, int width, int height);
		Hit HitTest(POINT pt) const;
		void OnLButtonDown(POINT pt);
		void OnLButtonUp(POINT pt);
		void SetSliderFromX(const Item& item, int x);
		void NotifyChanged(bool save);
		void RunAction(Action action);
		void ApplyAutoStart(bool enable);
		static bool IsAutoStartEnabled();
		const wchar_t* Tr(const wchar_t* zh) const;     // 以中文原文为 key，按当前语言取译文
		void RebuildIfLanguageChanged();               // 语言变了就重建界面项
		// 自绘下拉列表
		RECT ComboListRect(const Item& item) const;
		int ComboSelectedIndex(const Item& item) const;
		void OpenCombo(int itemIndex);
		void CloseCombo();
		void PickComboRow(int itemIndex, int row);

	private:
		HWND m_hwnd = nullptr;
		HWND m_owner = nullptr;
		ULONG_PTR m_gdiplusToken = 0;

		PlayerSettings m_settings;          // Item 里的指针都指向它的字段，所以 SettingPage 不能拷贝
		std::vector<Item> m_pages[kTabCount];
		int m_tab = 0;

		Hit m_hover;
		Hit m_pressed;
		int m_dragItem = -1;                // 正在拖动的滑块（设置项下标），没有则 -1
		bool m_trackingLeave = false;
		bool m_lastAutoStart = false;
		int m_lastLanguage = 0;
		std::wstring m_lastMusicFolder;     // 用来判断目录有没有真的变化
		int m_lastMusicSource = 0;          // 用来判断音乐源有没有真的变化
		int m_openCombo = -1;               // 当前展开的下拉列表（设置项下标），没有则 -1
		int m_comboScroll = 0;              // 下拉列表第一行显示的是第几项
		std::wstring m_version = APP_VERSION;

		std::function<void(const PlayerSettings&)> m_onChanged;
		std::function<void()> m_onCheckUpdate;
		std::function<void()> m_onCheckSettings;
		std::function<void(const std::wstring&)> m_onMusicFolder;
		std::function<void()> m_onMusicSource;
	};
}