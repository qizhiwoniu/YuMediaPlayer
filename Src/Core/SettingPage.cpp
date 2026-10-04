#include "SettingPage.h"
#include "OnlineMusic.h"   // 切换音乐源时通知在线歌曲模块
#include "DesktopLyric.h"   // 桌面歌词：设置页"歌词"页直接驱动它
#include "Theme.h"   // UITheme：设置页改主题后，主窗口/迷你窗口跟着变
#include <shlobj.h>
#include <math.h>
#include <wchar.h>
#include <set>
#include <algorithm>
#include <initializer_list>

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "advapi32.lib")

namespace YuMediaPlayer
{
	// ===================== PlayerSettings =====================
	void PlayerSettings::Reset()
	{
		*this = PlayerSettings();
	}

	void PlayerSettings::Clamp()
	{
		auto clampInt = [](int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); };
		loopMode = clampInt(loopMode, 0, 3);
		language = clampInt(language, 0, 4);
		theme = clampInt(theme, 0, 1);
		volume = clampInt(volume, 0, 100);
		fadeSeconds = clampInt(fadeSeconds, 0, 10);
		cornerRadius = clampInt(cornerRadius, 0, 30);
		musicSource = clampInt(musicSource, 0, 2);   // 0酷狗 1iTunes 2自建服务器
		lyricFontSize = clampInt(lyricFontSize, 6, 72);
		lyricColorMode = clampInt(lyricColorMode, 0, 1);
		lyricRainbowSpeed = clampInt(lyricRainbowSpeed, 1, 10);
	}

	// ===================== 辅助代码 =====================
	namespace
	{
		// 窗口布局（想调整界面大小/间距改这里）
		const int kWinW = 720;
		const int kWinH = 628;
		const int kCorner = 12;          // 设置窗口圆角半径
		const int kTitleH = 52;          // 标题栏高度
		const int kNavLeft = 14;
		const int kNavTop = 64;
		const int kNavW = 156;
		const int kNavItemH = 40;
		const int kNavGap = 6;
		const int kContentLeft = 190;
		const int kContentRight = kWinW - 28;
		const int kContentTop = 60;
		const int kRowH = 64;            // 普通设置项行高
		const int kChoiceRowH = 96;      // 选项组行高（控件在第二行）
		const int kInfoRowH = 40;        // 纯文字行高
		const int kOptionW = 100;         // 选项组里每个选项的宽度
		const int kComboW = 190;          // 下拉框默认宽度
		const int kComboRowH = 30;        // 下拉列表每行高度
		const int kComboMaxRows = 8;      // 下拉列表最多同时显示几行（超出用滚轮滚动）
		const int kComboPad = 4;          // 下拉列表上下内边距

		const wchar_t* kRunKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
		const wchar_t* kRunValue = L"YuMediaPlayer";

		std::wstring IniPath()
		{
			wchar_t path[MAX_PATH] = { 0 };
			GetModuleFileNameW(nullptr, path, MAX_PATH);
			wchar_t* slash = wcsrchr(path, L'\\');
			if (slash)
				*slash = L'\0';
			return std::wstring(path) + L"\\userprofile\\ConfigSettings.ini";
		}

		// 先创建一个带 BOM 的 UTF-16 空文件，这样 WritePrivateProfileString 会按 Unicode 保存，
		// 目录路径里有中文/特殊字符也不会丢
		void EnsureUnicodeIni(const std::wstring& path)
		{
			if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES)
				return;
			HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
			if (h == INVALID_HANDLE_VALUE)
				return;
			const WORD bom = 0xFEFF;
			DWORD written = 0;
			WriteFile(h, &bom, sizeof(bom), &written, nullptr);
			CloseHandle(h);
		}

		void WriteInt(const wchar_t* section, const wchar_t* key, int value, const std::wstring& path)
		{
			wchar_t buf[32];
			swprintf_s(buf, 32, L"%d", value);
			WritePrivateProfileStringW(section, key, buf, path.c_str());
		}

		void BuildRoundRectPath(Gdiplus::GraphicsPath& path, const Gdiplus::Rect& rect, int radius)
		{
			int d = radius * 2;
			if (d > rect.Width)  d = rect.Width;
			if (d > rect.Height) d = rect.Height;
			if (d < 1) d = 1;

			path.Reset();
			path.AddArc(rect.X, rect.Y, d, d, 180, 90);
			path.AddArc(rect.X + rect.Width - d, rect.Y, d, d, 270, 90);
			path.AddArc(rect.X + rect.Width - d, rect.Y + rect.Height - d, d, d, 0, 90);
			path.AddArc(rect.X, rect.Y + rect.Height - d, d, d, 90, 90);
			path.CloseFigure();
		}

		// 配色：0=深色 1=浅色
		struct Palette
		{
			Gdiplus::Color bg, border, accent, onAccent, text, textSub, textDim, line,
				navSel, navHover, navText, navSelText, closeX,
				trackOff, trackOffHover, knobOff, sliderTrack, thumb, thumbBorder,
				choiceBg, choiceHover, btn, btnHover, btnPress, listBg;
		};

		Palette GetPalette(int theme)
		{
			using Gdiplus::Color;
			Palette p;
			p.accent = Color(255, 255, 204, 0);      // 和主窗口一致的黄色强调色
			p.onAccent = Color(255, 28, 28, 28);     // 强调色上的文字/圆点
			if (theme == 1)
			{
				p.bg = Color(255, 245, 245, 247);
				p.border = Color(255, 208, 208, 212);
				p.text = Color(255, 32, 32, 32);
				p.textSub = Color(255, 96, 96, 96);
				p.textDim = Color(255, 128, 128, 128);
				p.line = Color(255, 224, 224, 228);
				p.navSel = Color(255, 228, 228, 232);
				p.navHover = Color(255, 236, 236, 240);
				p.navText = Color(255, 70, 70, 70);
				p.navSelText = Color(255, 184, 128, 0);   // 浅色背景上黄色字看不清，用深一点的琥珀色
				p.closeX = Color(255, 110, 110, 110);
				p.trackOff = Color(255, 196, 196, 200);
				p.trackOffHover = Color(255, 176, 176, 182);
				p.knobOff = Color(255, 255, 255, 255);
				p.sliderTrack = Color(255, 208, 208, 212);
				p.thumb = Color(255, 255, 255, 255);
				p.thumbBorder = Color(255, 190, 190, 194);
				p.choiceBg = Color(255, 232, 232, 236);
				p.choiceHover = Color(255, 214, 214, 220);
				p.btn = Color(255, 225, 225, 230);
				p.btnHover = Color(255, 214, 214, 220);
				p.btnPress = Color(255, 200, 200, 206);
				p.listBg = Color(255, 255, 255, 255);
			}
			else
			{
				p.bg = Color(255, 30, 30, 30);
				p.border = Color(255, 64, 64, 64);
				p.text = Color(255, 240, 240, 240);
				p.textSub = Color(255, 160, 160, 160);
				p.textDim = Color(255, 120, 120, 120);
				p.line = Color(255, 48, 48, 48);
				p.navSel = Color(255, 46, 46, 46);
				p.navHover = Color(255, 38, 38, 38);
				p.navText = Color(255, 200, 200, 200);
				p.navSelText = p.accent;
				p.closeX = Color(255, 190, 190, 190);
				p.trackOff = Color(255, 78, 78, 78);
				p.trackOffHover = Color(255, 94, 94, 94);
				p.knobOff = Color(255, 235, 235, 235);
				p.sliderTrack = Color(255, 70, 70, 70);
				p.thumb = Color(255, 245, 245, 245);
				p.thumbBorder = Color(0, 0, 0, 0);        // 深色下不画描边（全透明）
				p.choiceBg = Color(255, 44, 44, 44);
				p.choiceHover = Color(255, 62, 62, 62);
				p.btn = Color(255, 56, 56, 56);
				p.btnHover = Color(255, 72, 72, 72);
				p.btnPress = Color(255, 84, 84, 84);
				p.listBg = Color(255, 40, 40, 40);
			}
			return p;
		}

		// 枚举系统里安装的字体（只枚举一次）：去掉竖排(@开头)、符号字体、点阵字体，去重后按名字排序
		int CALLBACK EnumFontProc(const LOGFONTW* lf, const TEXTMETRICW*, DWORD fontType, LPARAM lParam)
		{
			if (fontType & RASTER_FONTTYPE)
				return 1;
			if (lf->lfCharSet == SYMBOL_CHARSET || lf->lfFaceName[0] == L'@' || lf->lfFaceName[0] == L'\0')
				return 1;
			reinterpret_cast<std::set<std::wstring>*>(lParam)->insert(lf->lfFaceName);
			return 1;
		}

		const std::vector<std::wstring>& InstalledFonts()
		{
			static std::vector<std::wstring> fonts;
			static bool loaded = false;
			if (!loaded)
			{
				loaded = true;
				std::set<std::wstring> names;
				HDC hdc = GetDC(nullptr);
				LOGFONTW lf = {};
				lf.lfCharSet = DEFAULT_CHARSET;
				EnumFontFamiliesExW(hdc, &lf, EnumFontProc, reinterpret_cast<LPARAM>(&names), 0);
				ReleaseDC(nullptr, hdc);
				fonts.assign(names.begin(), names.end());
				std::sort(fonts.begin(), fonts.end(), [](const std::wstring& a, const std::wstring& b)
					{ return _wcsicmp(a.c_str(), b.c_str()) < 0; });
			}
			return fonts;
		}

		// 翻译表：第 0 列是简体中文原文（同时作为 key），1=English 2=日本語 3=한국어 4=繁體中文
		// 想加新语言：这里加一列、PlayerSettings::Clamp 放宽 language 范围、BuildItems 里语言下拉加一项
		// 某一列留空会退回英文；表里找不到的文字原样显示中文
		struct TrEntry { const wchar_t* t[5]; };
		const TrEntry kTrTable[] =
		{
			{ { L"设置", L"Settings", L"設定", L"설정", L"設定" } },
			{ { L"歌词", L"Lyrics", L"歌詞", L"가사", L"歌詞" } },
			{ { L"显示桌面歌词", L"Show desktop lyrics", L"デスクトップ歌詞を表示", L"데스크톱 가사 표시", L"顯示桌面歌詞" } },
			{ { L"在桌面上显示悬浮的歌词，可拖动", L"Floating lyrics on the desktop; drag to move", L"デスクトップに歌詞を浮かせて表示（ドラッグで移動）", L"데스크톱에 가사를 띄워 표시 (드래그로 이동)", L"在桌面上顯示懸浮歌詞，可拖曳" } },
			{ { L"锁定歌词", L"Lock lyrics", L"歌詞をロック", L"가사 잠금", L"鎖定歌詞" } },
			{ { L"锁定后鼠标可穿透歌词，不能拖动", L"Mouse clicks pass through; lyrics cannot be dragged", L"クリックが歌詞を通過し、ドラッグできません", L"클릭이 가사를 통과하며 드래그할 수 없습니다", L"鎖定後滑鼠可穿透歌詞，不能拖曳" } },
			{ { L"歌词字体", L"Lyric font", L"歌詞フォント", L"가사 글꼴", L"歌詞字型" } },
			{ { L"默认使用胡敬礼字体（放入 fonts 文件夹或安装到系统）", L"Default is the Hu Jingli font (put it in the fonts folder or install it)", L"既定は胡敬礼フォント（fonts フォルダーに入れるかインストール）", L"기본값은 후징리 글꼴 (fonts 폴더에 넣거나 설치)", L"預設使用胡敬禮字型（放入 fonts 資料夾或安裝到系統）" } },
			{ { L"胡敬礼字体（默认）", L"Hu Jingli (default)", L"胡敬礼（既定）", L"후징리 (기본값)", L"胡敬禮字型（預設）" } },
			{ { L"歌词字号", L"Lyric size", L"歌詞サイズ", L"가사 크기", L"歌詞字級" } },
			{ { L"桌面歌词的文字大小", L"Text size of the desktop lyrics", L"デスクトップ歌詞の文字サイズ", L"데스크톱 가사의 글자 크기", L"桌面歌詞的文字大小" } },
			{ { L"歌词颜色", L"Lyric color", L"歌詞の色", L"가사 색상", L"歌詞顏色" } },
			{ { L"固定黄色，或随时间流动变化的七彩色", L"Solid yellow, or rainbow colors that flow over time", L"固定のイエロー、または流れるレインボー", L"고정 노란색 또는 흐르는 무지개색", L"固定黃色，或隨時間流動變化的七彩色" } },
			{ { L"黄色", L"Yellow", L"イエロー", L"노란색", L"黃色" } },
			{ { L"七彩", L"Rainbow", L"レインボー", L"무지개", L"七彩" } },
			{ { L"七彩变化速度", L"Rainbow speed", L"レインボー速度", L"무지개 속도", L"七彩變化速度" } },
			{ { L"七彩颜色流动的快慢（仅七彩模式有效）", L"How fast the colors flow (rainbow mode only)", L"色が流れる速さ（レインボーのみ）", L"색이 흐르는 속도 (무지개 모드 전용)", L"七彩顏色流動的快慢（僅七彩模式有效）" } },
			{ { L"通用", L"General", L"一般", L"일반", L"一般" } },
			{ { L"界面", L"Interface", L"インターフェース", L"인터페이스", L"介面" } },
			{ { L"系统", L"System", L"システム", L"시스템", L"系統" } },
			{ { L"关于", L"About", L"このアプリについて", L"정보", L"關於" } },
			{ { L"音乐源", L"Music Source", L"音楽ソース", L"음악 소스", L"音樂來源" } },
			{ { L"在线歌曲的来源接口", L"Source of online songs", L"オンライン曲の取得元", L"온라인 곡 소스", L"線上歌曲的來源介面" } },
			{ { L"酷狗音乐（仅歌曲列表）", L"Kugou (song list only)", L"KuGou（曲リストのみ）", L"쿠거우 (곡 목록만)", L"酷狗音樂（僅歌曲列表）" } },
			{ { L"iTunes 试听（国内可用）", L"iTunes previews (works in China)", L"iTunes 試聴（中国から利用可）", L"iTunes 미리듣기 (중국 내 사용 가능)", L"iTunes 試聽（國內可用）" } },
			{ { L"自建服务器（Navidrome/Subsonic）", L"Own server (Navidrome/Subsonic)", L"自前サーバー（Navidrome/Subsonic）", L"자체 서버 (Navidrome/Subsonic)", L"自建伺服器（Navidrome/Subsonic）" } },
			{ { L"仅 30 秒试听 · 国内可用 · 免 Key", L"30-second previews · works in China · no key", L"30秒試聴のみ · 中国から利用可 · キー不要", L"30초 미리듣기 · 중국 내 사용 가능 · 키 불필요", L"僅 30 秒試聽 · 國內可用 · 免 Key" } },
			{ { L"完整试听/下载 · 你自己的音乐库", L"Full playback / download · your own library", L"フル再生／DL可 · 自分のライブラリ", L"전체 재생/다운로드 · 내 음악 라이브러리", L"完整試聽／下載 · 你自己的音樂庫" } },
			{ { L"服务器", L"Server", L"サーバー", L"서버", L"伺服器" } },
			{ { L"在 [Music] 里填写 SubsonicUrl / SubsonicUser / SubsonicPassword 并保存", L"Set SubsonicUrl / SubsonicUser / SubsonicPassword under [Music] and save", L"[Music] に SubsonicUrl / SubsonicUser / SubsonicPassword を入力して保存", L"[Music]에 SubsonicUrl / SubsonicUser / SubsonicPassword를 입력하고 저장", L"在 [Music] 填寫 SubsonicUrl / SubsonicUser / SubsonicPassword 並儲存" } },
			{ { L"当前来源", L"Current source", L"現在のソース", L"현재 소스", L"目前來源" } },
			{ { L"仅列表，不可试听", L"List only, no playback", L"リストのみ（再生不可）", L"목록만 (재생 불가)", L"僅列表，不可試聽" } },
			{ { L"可试听/下载 · 需 client_id", L"Play / download · needs client_id", L"再生／DL可 · client_id 必要", L"재생/다운로드 · client_id 필요", L"可試聽／下載 · 需 client_id" } },
			{ { L"可试听/下载 · CC · 免 Key", L"Play / download · CC · no key", L"再生／DL可 · CC · キー不要", L"재생/다운로드 · CC · 키 불필요", L"可試聽／下載 · CC · 免 Key" } },
			{ { L"可试听/下载 · CC · 免 Key · 有限速", L"Play / download · CC · no key · rate-limited", L"再生／DL可 · CC · キー不要 · 回数制限あり", L"재생/다운로드 · CC · 키 불필요 · 속도 제한", L"可試聽／下載 · CC · 免 Key · 有限速" } },
			{ { L"刷新列表", L"Refresh list", L"リストを更新", L"목록 새로고침", L"重新整理列表" } },
			{ { L"重新读取配置并重新加载当前音乐源的歌曲", L"Reload settings and fetch songs from the current source", L"設定を再読み込みして現在のソースの曲を取得", L"설정을 다시 읽고 현재 소스의 곡을 불러옵니다", L"重新讀取設定並重新載入目前來源的歌曲" } },
			{ { L"已配置", L"Configured", L"設定済み", L"설정됨", L"已設定" } },
			{ { L"未配置", L"Not configured", L"未設定", L"설정 안 됨", L"未設定" } },
			{ { L"打开配置文件", L"Open config file", L"設定ファイルを開く", L"설정 파일 열기", L"開啟設定檔" } },
			{ { L"打开", L"Open", L"開く", L"열기", L"開啟" } },
			{ { L"重新读取", L"Reload", L"再読み込み", L"다시 읽기", L"重新讀取" } },
			{ { L"改完配置文件后点右侧按钮生效", L"After editing, click the button to apply", L"編集後、右のボタンで反映します", L"수정 후 오른쪽 버튼을 눌러 적용", L"修改設定檔後按右側按鈕生效" } },
			{ { L"语言", L"Language", L"言語", L"언어", L"語言" } },
			{ { L"界面显示语言", L"Display language of the interface", L"表示言語を選択します", L"인터페이스 표시 언어", L"介面顯示語言" } },
			{ { L"皮肤主题", L"Theme", L"テーマ", L"테마", L"外觀主題" } },
			{ { L"深色或浅色界面", L"Dark or light appearance", L"ダークまたはライトの外観", L"어두운 또는 밝은 모양", L"深色或淺色介面" } },
			{ { L"深色", L"Dark", L"ダーク", L"어두운", L"深色" } },
			{ { L"浅色", L"Light", L"ライト", L"밝은", L"淺色" } },
			{ { L"字体", L"Font", L"フォント", L"글꼴", L"字型" } },
			{ { L"界面文字使用的字体", L"Font used for interface text", L"画面の文字に使うフォント", L"인터페이스 글꼴", L"介面文字使用的字型" } },
			{ { L"默认字体", L"Default", L"既定", L"기본값", L"預設字型" } },
			{ { L"默认播放模式", L"Default play mode", L"既定の再生モード", L"기본 재생 모드", L"預設播放模式" } },
			{ { L"启动时使用的播放顺序", L"Play order used at startup", L"起動時に使う再生順", L"시작 시 사용할 재생 순서", L"啟動時使用的播放順序" } },
			{ { L"列表循环", L"Repeat all", L"全曲リピート", L"전체 반복", L"清單循環" } },
			{ { L"单曲循环", L"Repeat one", L"1曲リピート", L"한 곡 반복", L"單曲循環" } },
			{ { L"随机播放", L"Shuffle", L"シャッフル", L"셔플", L"隨機播放" } },
			{ { L"心动循环", L"Heartbeat", L"ハートビート", L"하트비트", L"心動循環" } },
			{ { L"默认音量", L"Default volume", L"既定の音量", L"기본 볼륨", L"預設音量" } },
			{ { L"启动时的初始音量", L"Initial volume at startup", L"起動時の初期音量", L"시작 시 초기 볼륨", L"啟動時的初始音量" } },
			{ { L"淡入淡出", L"Crossfade", L"フェード", L"페이드", L"淡入淡出" } },
			{ { L"切歌时的渐变时长，0 表示关闭", L"Fade between tracks; 0 = off", L"曲の切り替え時のフェード時間（0 でオフ）", L"곡 전환 시 페이드 시간 (0 = 끔)", L"切歌時的漸變時長，0 表示關閉" } },
			{ { L" 秒", L" s", L" 秒", L" 초", L" 秒" } },
			{ { L"启动后自动播放", L"Auto-play on start", L"起動時に自動再生", L"시작 시 자동 재생", L"啟動後自動播放" } },
			{ { L"打开播放器后立即开始播放", L"Start playing as soon as the player opens", L"プレーヤーを開いたらすぐに再生を開始します", L"플레이어를 열면 바로 재생합니다", L"開啟播放器後立即開始播放" } },
			{ { L"记住上次播放的歌曲", L"Resume last track", L"前回の曲を再開", L"마지막 곡 이어 재생", L"記住上次播放的歌曲" } },
			{ { L"下次启动时恢复到上次播放的歌曲", L"Restore the last played track on startup", L"次回起動時に前回再生した曲に戻します", L"다음 실행 시 마지막으로 재생한 곡으로 복원합니다", L"下次啟動時恢復到上次播放的歌曲" } },
			{ { L"窗口圆角", L"Corner radius", L"ウィンドウの角丸", L"창 모서리 반경", L"視窗圓角" } },
			{ { L"主窗口的圆角大小，0 为直角", L"Corner radius of the main window; 0 = square", L"メインウィンドウの角の丸み（0 で直角）", L"메인 창의 모서리 둥글기 (0 = 직각)", L"主視窗的圓角大小，0 為直角" } },
			{ { L"窗口置顶", L"Always on top", L"常に手前に表示", L"항상 위에 표시", L"視窗置頂" } },
			{ { L"播放器主窗口始终显示在其他窗口之上", L"Keep the player window above other windows", L"プレーヤーを他のウィンドウより前面に表示します", L"플레이어 창을 다른 창 위에 항상 표시합니다", L"播放器主視窗始終顯示在其他視窗之上" } },
			{ { L"开机自动启动", L"Start with Windows", L"Windows と同時に起動", L"Windows 시작 시 실행", L"開機自動啟動" } },
			{ { L"登录 Windows 后自动运行播放器", L"Run the player when you sign in to Windows", L"Windows にサインインしたら自動で起動します", L"Windows에 로그인하면 플레이어를 자동 실행합니다", L"登入 Windows 後自動執行播放器" } },
			{ { L"自动检查更新", L"Check for updates", L"更新を自動確認", L"업데이트 자동 확인", L"自動檢查更新" } },
			{ { L"启动时检查是否有新版本", L"Look for a new version on startup", L"起動時に新しいバージョンを確認します", L"시작 시 새 버전을 확인합니다", L"啟動時檢查是否有新版本" } },
			{ { L"本地音乐目录", L"Music folder", L"音楽フォルダー", L"음악 폴더", L"本機音樂資料夾" } },
			{ { L"选择...", L"Browse...", L"選択...", L"선택...", L"選擇..." } },
			{ { L"YuMediaPlayer 版本", L"YuMediaPlayer version", L"YuMediaPlayer のバージョン", L"YuMediaPlayer 버전", L"YuMediaPlayer 版本" } },
			{ { L"配置文件", L"Config file", L"設定ファイル", L"설정 파일", L"設定檔" } },
			{ { L"检查更新", L"Check for updates", L"更新を確認", L"업데이트 확인", L"檢查更新" } },
			{ { L"查看是否有新版本", L"See if a new version is available", L"新しいバージョンがあるか確認します", L"새 버전이 있는지 확인합니다", L"查看是否有新版本" } },
			{ { L"立即检查", L"Check now", L"今すぐ確認", L"지금 확인", L"立即檢查" } },
			{ { L"恢复默认设置", L"Reset settings", L"設定をリセット", L"설정 초기화", L"還原預設設定" } },
			{ { L"将所有设置恢复为初始值", L"Restore all settings to their defaults", L"すべての設定を初期値に戻します", L"모든 설정을 초기값으로 되돌립니다", L"將所有設定還原為初始值" } },
			{ { L"恢复默认", L"Reset", L"リセット", L"초기화", L"還原預設" } },
			{ { L"确定要将所有设置恢复为默认值吗？", L"Restore all settings to their defaults?", L"すべての設定を初期値に戻しますか？", L"모든 설정을 기본값으로 되돌리시겠습니까?", L"確定要將所有設定還原為預設值嗎？" } },
			{ { L"选择本地音乐目录", L"Choose your music folder", L"音楽フォルダーを選択", L"음악 폴더 선택", L"選擇本機音樂資料夾" } },
			{ { L"未设置，请点击右侧按钮选择", L"Not set - click the button on the right", L"未設定です。右のボタンで選択してください", L"설정되지 않음 - 오른쪽 버튼을 눌러 선택하세요", L"未設定，請點擊右側按鈕選擇" } },
		};

		RECT CloseButtonRect()
		{
			RECT r = { kWinW - 48, 0, kWinW, 40 };
			return r;
		}

		RECT NavItemRect(int index)
		{
			int y = kNavTop + index * (kNavItemH + kNavGap);
			RECT r = { kNavLeft, y, kNavLeft + kNavW, y + kNavItemH };
			return r;
		}
	}

	// ===================== 构造 / 析构 =====================
	SettingPage::SettingPage()
	{
		Gdiplus::GdiplusStartupInput input;
		Gdiplus::GdiplusStartup(&m_gdiplusToken, &input, nullptr);

		BuildItems();
		LayoutItems();
		Load();

		// 用户拖动桌面歌词后记住位置；在歌词右键菜单里关闭/锁定后同步设置页的开关
		DesktopLyric::Instance().SetMovedCallback([this](int x, int y)
			{
				m_settings.lyricHasPos = true;
				m_settings.lyricX = x;
				m_settings.lyricY = y;
				Save();
			});
		DesktopLyric::Instance().SetUserChangedCallback([this](bool visible, bool locked)
			{
				m_settings.lyricEnabled = visible;
				m_settings.lyricLocked = locked;
				Save();
				if (m_hwnd)
					InvalidateRect(m_hwnd, nullptr, FALSE);
			});
	}

	SettingPage::~SettingPage()
	{
		DesktopLyric::Instance().SetMovedCallback(nullptr);          // 回调里捕获了 this，先清掉
		DesktopLyric::Instance().SetUserChangedCallback(nullptr);
		if (m_hwnd)
		{
			DestroyWindow(m_hwnd);
			m_hwnd = nullptr;
		}
		if (m_gdiplusToken)
			Gdiplus::GdiplusShutdown(m_gdiplusToken);
	}

	// ===================== 多语言 =====================
	const wchar_t* SettingPage::Tr(const wchar_t* zh) const
	{
		const int lang = m_settings.language;
		if (lang <= 0 || lang >= 5)
			return zh;
		for (const TrEntry& e : kTrTable)
		{
			if (wcscmp(e.t[0], zh) == 0)
			{
				const wchar_t* s = e.t[lang];
				return (s && *s) ? s : e.t[1];   // 没翻译就用英文
			}
		}
		return zh;
	}

	// ===================== 下拉列表（自绘）=====================
	RECT SettingPage::ComboListRect(const Item& it) const
	{
		const int n = (int)it.options.size();
		const int rows = n < kComboMaxRows ? n : kComboMaxRows;
		const int h = rows * kComboRowH + kComboPad * 2;
		int top = it.ctrlRect.bottom + 4;
		if (top + h > kWinH - 8)                 // 下面放不下就往上展开
			top = it.ctrlRect.top - 4 - h;
		RECT r = { it.ctrlRect.left, top, it.ctrlRect.right, top + h };
		return r;
	}

	int SettingPage::ComboSelectedIndex(const Item& it) const
	{
		if (it.intValue)
			return *it.intValue;
		if (it.comboStr)
		{
			if (it.fontList && it.comboStr->empty())
				return 0;                        // 空字符串 = 默认字体
			for (int i = 0; i < (int)it.options.size(); i++)
				if (_wcsicmp(it.options[i].c_str(), it.comboStr->c_str()) == 0)
					return i;
		}
		return -1;
	}

	void SettingPage::OpenCombo(int itemIndex)
	{
		const Item& it = m_pages[m_tab][itemIndex];
		const int n = (int)it.options.size();
		const int rows = n < kComboMaxRows ? n : kComboMaxRows;
		int scroll = ComboSelectedIndex(it) - rows / 2;   // 让当前选中项大致显示在中间
		if (scroll > n - rows) scroll = n - rows;
		if (scroll < 0) scroll = 0;
		m_openCombo = itemIndex;
		m_comboScroll = scroll;
	}

	void SettingPage::CloseCombo()
	{
		m_openCombo = -1;
		m_comboScroll = 0;
	}

	void SettingPage::PickComboRow(int itemIndex, int row)
	{
		if (itemIndex < 0 || itemIndex >= (int)m_pages[m_tab].size())
			return;
		Item& it = m_pages[m_tab][itemIndex];
		if (row < 0 || row >= (int)it.options.size())
			return;
		if (it.intValue)
			*it.intValue = row;
		else if (it.comboStr)
			*it.comboStr = (it.fontList && row == 0) ? std::wstring() : it.options[row];
		NotifyChanged(true);      // 可能会重建界面项（换语言），之后不要再用 it
	}

	void SettingPage::RebuildIfLanguageChanged()
	{
		if (m_settings.language == m_lastLanguage)
			return;
		m_lastLanguage = m_settings.language;
		BuildItems();     // 界面项的文字是复制进 Item 里的，换语言要重建
		LayoutItems();
	}

	// ===================== 设置项定义（想加设置就在这里加一行）=====================
	void SettingPage::BuildItems()
	{
		for (int i = 0; i < kTabCount; i++)
			m_pages[i].clear();

		auto toggle = [](const wchar_t* label, const wchar_t* desc, bool* value)
			{
				Item it;
				it.type = ItemToggle;
				it.label = label;
				it.desc = desc;
				it.boolValue = value;
				return it;
			};
		auto slider = [](const wchar_t* label, const wchar_t* desc, int* value, int minV, int maxV, const wchar_t* unit)
			{
				Item it;
				it.type = ItemSlider;
				it.label = label;
				it.desc = desc;
				it.intValue = value;
				it.minValue = minV;
				it.maxValue = maxV;
				it.unit = unit;
				return it;
			};
		auto choice = [](const wchar_t* label, const wchar_t* desc, int* value, std::vector<std::wstring> options)
			{
				Item it;
				it.type = ItemChoice;
				it.label = label;
				it.desc = desc;
				it.intValue = value;
				it.options = std::move(options);
				return it;
			};
		auto combo = [](const wchar_t* label, const wchar_t* desc, int* value, std::vector<std::wstring> options, int width)
			{
				Item it;
				it.type = ItemCombo;
				it.label = label;
				it.desc = desc;
				it.intValue = value;
				it.options = std::move(options);
				it.optionW = width;     // 下拉框宽度
				return it;
			};
		auto button = [](const wchar_t* label, const wchar_t* desc, const wchar_t* text, Action action)
			{
				Item it;
				it.type = ItemButton;
				it.label = label;
				it.desc = desc;
				it.buttonText = text;
				it.action = action;
				return it;
			};
		auto info = [](const wchar_t* label, const std::wstring& value)
			{
				Item it;
				it.type = ItemInfo;
				it.label = label;
				it.infoValue = value;
				return it;
			};
		m_openCombo = -1;

		// ---- 页 0：通用（语言 / 皮肤 / 字体 + 播放）----
		m_pages[0].push_back(combo(Tr(L"语言"), Tr(L"界面显示语言"), &m_settings.language,
			{ L"简体中文", L"English", L"日本語", L"한국어", L"繁體中文" }, kComboW));
		{
			Item it = choice(Tr(L"皮肤主题"), Tr(L"深色或浅色界面"), &m_settings.theme, { Tr(L"深色"), Tr(L"浅色") });
			it.compact = true;
			m_pages[0].push_back(it);
		}
		{
			std::vector<std::wstring> fonts;
			fonts.push_back(Tr(L"默认字体"));                 // 第 0 项 = 默认
			for (const std::wstring& f : InstalledFonts())    // 其余 = 系统里安装的字体
				fonts.push_back(f);
			Item it = combo(Tr(L"字体"), Tr(L"界面文字使用的字体"), nullptr, fonts, 240);
			it.comboStr = &m_settings.fontName;
			it.fontList = true;
			m_pages[0].push_back(it);
		}
		m_pages[0].push_back(choice(Tr(L"默认播放模式"), Tr(L"启动时使用的播放顺序"), &m_settings.loopMode,
			{ Tr(L"列表循环"), Tr(L"单曲循环"), Tr(L"随机播放"), Tr(L"心动循环") }));
		m_pages[0].push_back(slider(Tr(L"默认音量"), Tr(L"启动时的初始音量"), &m_settings.volume, 0, 100, L"%"));
		m_pages[0].push_back(slider(Tr(L"淡入淡出"), Tr(L"切歌时的渐变时长，0 表示关闭"), &m_settings.fadeSeconds, 0, 10, Tr(L" 秒")));
		m_pages[0].push_back(toggle(Tr(L"启动后自动播放"), Tr(L"打开播放器后立即开始播放"), &m_settings.autoPlayOnStart));
		m_pages[0].push_back(toggle(Tr(L"记住上次播放的歌曲"), Tr(L"下次启动时恢复到上次播放的歌曲"), &m_settings.resumeLastTrack));

		// ---- 页 1：界面 ----
		m_pages[1].push_back(slider(Tr(L"窗口圆角"), Tr(L"主窗口的圆角大小，0 为直角"), &m_settings.cornerRadius, 0, 30, L" px"));
		m_pages[1].push_back(toggle(Tr(L"窗口置顶"), Tr(L"播放器主窗口始终显示在其他窗口之上"), &m_settings.alwaysOnTop));

		// ---- 页 2：系统 ----
		m_pages[2].push_back(toggle(Tr(L"开机自动启动"), Tr(L"登录 Windows 后自动运行播放器"), &m_settings.startWithWindows));
		m_pages[2].push_back(toggle(Tr(L"自动检查更新"), Tr(L"启动时检查是否有新版本"), &m_settings.autoCheckUpdate));
		{
			Item it = button(Tr(L"本地音乐目录"), L"", Tr(L"选择..."), ActPickFolder);
			it.strValue = &m_settings.musicFolder;   // 当前目录显示在描述行
			m_pages[2].push_back(it);
		}

		// ---- 页 3：歌词（桌面歌词）----
		m_pages[3].push_back(toggle(Tr(L"显示桌面歌词"), Tr(L"在桌面上显示悬浮的歌词，可拖动"), &m_settings.lyricEnabled));
		m_pages[3].push_back(toggle(Tr(L"锁定歌词"), Tr(L"锁定后鼠标可穿透歌词，不能拖动"), &m_settings.lyricLocked));
		{
			std::vector<std::wstring> fonts;
			fonts.push_back(Tr(L"胡敬礼字体（默认）"));       // 第 0 项 = 默认（空字符串 -> 胡敬礼字体）
			for (const std::wstring& f : InstalledFonts())
				fonts.push_back(f);
			Item it = combo(Tr(L"歌词字体"), Tr(L"默认使用胡敬礼字体（放入 fonts 文件夹或安装到系统）"), nullptr, fonts, 240);
			it.comboStr = &m_settings.lyricFontName;
			it.fontList = true;
			m_pages[3].push_back(it);
		}
		m_pages[3].push_back(slider(Tr(L"歌词字号"), Tr(L"桌面歌词的文字大小"), &m_settings.lyricFontSize, 6, 72, L" pt"));
		{
			Item it = choice(Tr(L"歌词颜色"), Tr(L"固定黄色，或随时间流动变化的七彩色"), &m_settings.lyricColorMode, { Tr(L"黄色"), Tr(L"七彩") });
			it.compact = true;
			m_pages[3].push_back(it);
		}
		m_pages[3].push_back(slider(Tr(L"七彩变化速度"), Tr(L"七彩颜色流动的快慢（仅七彩模式有效）"), &m_settings.lyricRainbowSpeed, 1, 10, L""));

		// ---- 页 4：音乐源 ----（下标要和 OnlineMusic::Source 一致：0酷狗 1iTunes 2自建服务器）
		m_pages[4].push_back(combo(Tr(L"音乐源"), Tr(L"在线歌曲的来源接口"), &m_settings.musicSource,
			{ Tr(L"酷狗音乐（仅歌曲列表）"),
			  Tr(L"iTunes 试听（国内可用）"), Tr(L"自建服务器（Navidrome/Subsonic）") }, 320));
		{
			static const wchar_t* kCaps[OnlineMusic::SourceCount] = { L"仅列表，不可试听",
				L"仅 30 秒试听 · 国内可用 · 免 Key", L"完整试听/下载 · 你自己的音乐库" };
			int src = m_settings.musicSource;
			if (src < 0) src = 0;
			if (src >= OnlineMusic::SourceCount) src = OnlineMusic::SourceCount - 1;
			m_pages[4].push_back(info(Tr(L"当前来源"), Tr(kCaps[src])));
			if (src == OnlineMusic::SourceSubsonic)   // 自建服务器需要地址/账号
			{
				m_pages[4].push_back(info(Tr(L"服务器"), OnlineMusic::SourceCanPlay(OnlineMusic::SourceSubsonic) ? Tr(L"已配置") : Tr(L"未配置")));
				m_pages[4].push_back(button(Tr(L"打开配置文件"), Tr(L"在 [Music] 里填写 SubsonicUrl / SubsonicUser / SubsonicPassword 并保存"), Tr(L"打开"), ActOpenIni));
			}
		}
		m_pages[4].push_back(button(Tr(L"刷新列表"), Tr(L"重新读取配置并重新加载当前音乐源的歌曲"), Tr(L"刷新列表"), ActReloadMusicSource));

		// ---- 页 5：关于 ----
		m_pages[5].push_back(info(Tr(L"YuMediaPlayer 版本"), m_version));
		m_pages[5].push_back(info(Tr(L"配置文件"), IniPath()));
		m_pages[5].push_back(button(Tr(L"检查更新"), Tr(L"查看是否有新版本"), Tr(L"立即检查"), ActCheckUpdate));
		m_pages[5].push_back(button(Tr(L"恢复默认设置"), Tr(L"将所有设置恢复为初始值"), Tr(L"恢复默认"), ActReset));
	}

	// 计算每个设置项的行区域和控件区域（窗口大小固定，所以只需算一次）
	void SettingPage::LayoutItems()
	{
		for (int t = 0; t < kTabCount; t++)
		{
			int y = kContentTop;
			for (Item& it : m_pages[t])
			{
				int h = (it.type == ItemChoice && !it.compact) ? kChoiceRowH : (it.type == ItemInfo ? kInfoRowH : kRowH);
				it.rowRect = { kContentLeft, y, kContentRight, y + h };
				const RECT& r = it.rowRect;

				switch (it.type)
				{
				case ItemToggle:
					it.ctrlRect = { r.right - 44, r.top + 20, r.right, r.top + 44 };
					break;
				case ItemSlider:
					it.ctrlRect = { r.right - 264, r.top + 20, r.right - 68, r.top + 44 };
					break;
				case ItemChoice:
				{
					int n = (int)it.options.size();
					const int ow = it.optionW > 0 ? it.optionW : kOptionW;
					if (it.compact)   // 控件靠右、垂直居中
						it.ctrlRect = { r.right - ow * n, r.top + 17, r.right, r.top + 17 + 30 };
					else
						it.ctrlRect = { r.left, r.top + 58, r.left + ow * n, r.top + 58 + 30 };
					break;
				}
				case ItemCombo:
				{
					const int w = it.optionW > 0 ? it.optionW : kComboW;
					it.ctrlRect = { r.right - w, r.top + 17, r.right, r.top + 17 + 30 };
					break;
				}
				case ItemButton:
					it.ctrlRect = { r.right - 104, r.top + 16, r.right, r.top + 48 };
					break;
				default:
					it.ctrlRect = { 0, 0, 0, 0 };
					break;
				}
				y += h;
			}
		}
	}

	// 把歌词设置同步给桌面歌词窗口（DesktopLyric 是全局单例，所以不依赖外部回调也能生效）
	static void ApplyLyricSettings(const PlayerSettings& s)
	{
		DesktopLyric& d = DesktopLyric::Instance();
		LyricStyle st;
		st.fontName = s.lyricFontName;
		st.fontSize = s.lyricFontSize;
		st.colorMode = s.lyricColorMode;
		st.rainbowSpeed = s.lyricRainbowSpeed;
		st.locked = s.lyricLocked;
		d.SetStyle(st);

		if (s.lyricEnabled)
		{
			if (!d.IsVisible())
			{
				if (s.lyricHasPos)   // 记住的位置还在某块屏幕里才恢复（拔掉副屏后不会跑到屏幕外）
				{
					const int vx = GetSystemMetrics(SM_XVIRTUALSCREEN), vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
					const int vw = GetSystemMetrics(SM_CXVIRTUALSCREEN), vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
					if (s.lyricX > vx - 200 && s.lyricX < vx + vw - 100 && s.lyricY > vy - 20 && s.lyricY < vy + vh - 20)
						d.SetPlacement(s.lyricX, s.lyricY);
				}
				d.Show();
			}
		}
		else if (d.IsVisible())
			d.Hide();
	}

	// 把当前音乐源 / client_id 同步给在线歌曲模块（OnlineMusic），这样不依赖外部回调也能生效
	static void ApplyMusicBackend(const PlayerSettings& s)
	{
	
		// 自建服务器：用户手动编辑 settings.ini 的 [Music]，每次都重新读
		const std::wstring ini = IniPath();
		wchar_t url[512] = { 0 }, user[128] = { 0 }, pass[128] = { 0 };
		GetPrivateProfileStringW(L"Music", L"SubsonicUrl", L"", url, 512, ini.c_str());
		GetPrivateProfileStringW(L"Music", L"SubsonicUser", L"", user, 128, ini.c_str());
		GetPrivateProfileStringW(L"Music", L"SubsonicPassword", L"", pass, 128, ini.c_str());
		OnlineMusic::SetSubsonic(url, user, pass);
		OnlineMusic::SetSource(s.musicSource);
	}

	// ===================== 读写 settings.ini =====================
	void SettingPage::Load()
	{
		const std::wstring ini = IniPath();
		const PlayerSettings d;   // 默认值
		m_settings.language = GetPrivateProfileIntW(L"Language", L"Language", d.language, ini.c_str());
		
		m_settings.theme = GetPrivateProfileIntW(L"Theme", L"Skin", d.theme, ini.c_str());
		wchar_t fontName[128] = { 0 };
		GetPrivateProfileStringW(L"Theme", L"FontName", L"", fontName, 128, ini.c_str());
		m_settings.fontName = fontName;

		m_settings.loopMode = GetPrivateProfileIntW(L"Player", L"LoopMode", d.loopMode, ini.c_str());
		m_settings.volume = GetPrivateProfileIntW(L"Player", L"Volume", d.volume, ini.c_str());
		m_settings.fadeSeconds = GetPrivateProfileIntW(L"Player", L"FadeSeconds", d.fadeSeconds, ini.c_str());
		m_settings.autoPlayOnStart = GetPrivateProfileIntW(L"Player", L"AutoPlayOnStart", d.autoPlayOnStart, ini.c_str()) != 0;
		m_settings.resumeLastTrack = GetPrivateProfileIntW(L"Player", L"ResumeLastTrack", d.resumeLastTrack, ini.c_str()) != 0;

		m_settings.cornerRadius = GetPrivateProfileIntW(L"UI", L"CornerRadius", d.cornerRadius, ini.c_str());
		m_settings.alwaysOnTop = GetPrivateProfileIntW(L"UI", L"AlwaysOnTop", d.alwaysOnTop, ini.c_str()) != 0;

		m_settings.autoCheckUpdate = GetPrivateProfileIntW(L"System", L"AutoCheckUpdate", d.autoCheckUpdate, ini.c_str()) != 0;
		wchar_t folder[MAX_PATH] = { 0 };
		GetPrivateProfileStringW(L"System", L"MusicFolder", L"", folder, MAX_PATH, ini.c_str());
		m_settings.musicFolder = folder;
		m_lastMusicFolder = m_settings.musicFolder;

		m_settings.musicSource = GetPrivateProfileIntW(L"Music", L"Source", d.musicSource, ini.c_str());

		m_settings.lyricEnabled = GetPrivateProfileIntW(L"Lyric", L"Enabled", d.lyricEnabled, ini.c_str()) != 0;
		m_settings.lyricLocked = GetPrivateProfileIntW(L"Lyric", L"Locked", d.lyricLocked, ini.c_str()) != 0;
		wchar_t lyricFont[128] = { 0 };
		GetPrivateProfileStringW(L"Lyric", L"FontName", L"", lyricFont, 128, ini.c_str());
		m_settings.lyricFontName = lyricFont;
		m_settings.lyricFontSize = GetPrivateProfileIntW(L"Lyric", L"FontSize", d.lyricFontSize, ini.c_str());
		m_settings.lyricColorMode = GetPrivateProfileIntW(L"Lyric", L"ColorMode", d.lyricColorMode, ini.c_str());
		m_settings.lyricRainbowSpeed = GetPrivateProfileIntW(L"Lyric", L"RainbowSpeed", d.lyricRainbowSpeed, ini.c_str());
		m_settings.lyricHasPos = GetPrivateProfileIntW(L"Lyric", L"HasPos", 0, ini.c_str()) != 0;
		m_settings.lyricX = (int)GetPrivateProfileIntW(L"Lyric", L"X", 0, ini.c_str());
		m_settings.lyricY = (int)GetPrivateProfileIntW(L"Lyric", L"Y", 0, ini.c_str());

		// 开机自启以注册表里的实际状态为准
		m_settings.startWithWindows = IsAutoStartEnabled();
		m_lastAutoStart = m_settings.startWithWindows;

		m_settings.Clamp();
		m_lastMusicSource = m_settings.musicSource;
		ApplyMusicBackend(m_settings);
		ApplyLyricSettings(m_settings);
		UITheme::SetMode(m_settings.theme);
		BuildItems();      // "音乐源"页的 client_id 状态要跟着刷新
		LayoutItems();
		RebuildIfLanguageChanged();
		if (m_hwnd)
			InvalidateRect(m_hwnd, nullptr, FALSE);
	}

	void SettingPage::Save() const
	{
		const std::wstring ini = IniPath();
		EnsureUnicodeIni(ini);

		WriteInt(L"Language", L"Language", m_settings.language, ini);

		WriteInt(L"Theme", L"Skin", m_settings.theme, ini);
		WritePrivateProfileStringW(L"Theme", L"FontName", m_settings.fontName.c_str(), ini.c_str());

		WriteInt(L"Player", L"LoopMode", m_settings.loopMode, ini);
		WriteInt(L"Player", L"Volume", m_settings.volume, ini);
		WriteInt(L"Player", L"FadeSeconds", m_settings.fadeSeconds, ini);
		WriteInt(L"Player", L"AutoPlayOnStart", m_settings.autoPlayOnStart ? 1 : 0, ini);
		WriteInt(L"Player", L"ResumeLastTrack", m_settings.resumeLastTrack ? 1 : 0, ini);

		WriteInt(L"UI", L"CornerRadius", m_settings.cornerRadius, ini);
		WriteInt(L"UI", L"AlwaysOnTop", m_settings.alwaysOnTop ? 1 : 0, ini);

		WriteInt(L"System", L"AutoCheckUpdate", m_settings.autoCheckUpdate ? 1 : 0, ini);
		WritePrivateProfileStringW(L"System", L"MusicFolder", m_settings.musicFolder.c_str(), ini.c_str());

		WriteInt(L"Music", L"Source", m_settings.musicSource, ini);

		WriteInt(L"Lyric", L"Enabled", m_settings.lyricEnabled ? 1 : 0, ini);
		WriteInt(L"Lyric", L"Locked", m_settings.lyricLocked ? 1 : 0, ini);
		WritePrivateProfileStringW(L"Lyric", L"FontName", m_settings.lyricFontName.c_str(), ini.c_str());
		WriteInt(L"Lyric", L"FontSize", m_settings.lyricFontSize, ini);
		WriteInt(L"Lyric", L"ColorMode", m_settings.lyricColorMode, ini);
		WriteInt(L"Lyric", L"RainbowSpeed", m_settings.lyricRainbowSpeed, ini);
		WriteInt(L"Lyric", L"HasPos", m_settings.lyricHasPos ? 1 : 0, ini);
		WriteInt(L"Lyric", L"X", m_settings.lyricX, ini);
		WriteInt(L"Lyric", L"Y", m_settings.lyricY, ini);
		
	}

	const PlayerSettings& SettingPage::GetSettings() const
	{
		return m_settings;
	}

	void SettingPage::SetSettings(const PlayerSettings& settings)
	{
		m_settings = settings;
		m_settings.Clamp();
		UITheme::SetMode(m_settings.theme);
		m_lastMusicFolder = m_settings.musicFolder;   // 代码里设置的不触发回调
		m_lastMusicSource = m_settings.musicSource;
		ApplyMusicBackend(m_settings);
		ApplyLyricSettings(m_settings);
		RebuildIfLanguageChanged();
		if (m_settings.startWithWindows != m_lastAutoStart)
		{
			ApplyAutoStart(m_settings.startWithWindows);
			m_lastAutoStart = m_settings.startWithWindows;
		}
		if (m_hwnd)
			InvalidateRect(m_hwnd, nullptr, FALSE);
	}

	void SettingPage::SetChangedCallback(std::function<void(const PlayerSettings&)> callback)
	{
		m_onChanged = std::move(callback);
	}

	void SettingPage::SetCheckUpdateCallback(std::function<void()> callback)
	{
		m_onCheckUpdate = std::move(callback);
	}

	void SettingPage::SetCheckSettingsCallback(std::function<void()> callback)
	{
		m_onCheckSettings = std::move(callback);
	}

	void SettingPage::SetMusicFolderChangedCallback(std::function<void(const std::wstring&)> callback)
	{
		m_onMusicFolder = std::move(callback);
	}

	void SettingPage::SetMusicSourceChangedCallback(std::function<void()> callback)
	{
		m_onMusicSource = std::move(callback);
	}

	void SettingPage::SetVersionText(const wchar_t* text)
	{
		m_version = text ? text : L"";
		BuildItems();    // "关于"页里的版本号是复制进去的，重建一遍
		LayoutItems();
		if (m_hwnd)
			InvalidateRect(m_hwnd, nullptr, FALSE);
	}

	// ===================== 开机自启（HKCU\...\Run）=====================
	bool SettingPage::IsAutoStartEnabled()
	{
		HKEY key = nullptr;
		if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_READ, &key) != ERROR_SUCCESS)
			return false;
		bool exists = (RegQueryValueExW(key, kRunValue, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS);
		RegCloseKey(key);
		return exists;
	}

	void SettingPage::ApplyAutoStart(bool enable)
	{
		HKEY key = nullptr;
		if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_SET_VALUE, &key) != ERROR_SUCCESS)
			return;

		if (enable)
		{
			wchar_t exe[MAX_PATH] = { 0 };
			GetModuleFileNameW(nullptr, exe, MAX_PATH);
			std::wstring cmd = L"\"" + std::wstring(exe) + L"\"";   // 路径带空格时必须加引号
			RegSetValueExW(key, kRunValue, 0, REG_SZ,
				reinterpret_cast<const BYTE*>(cmd.c_str()), (DWORD)((cmd.size() + 1) * sizeof(wchar_t)));
		}
		else
		{
			RegDeleteValueW(key, kRunValue);
		}
		RegCloseKey(key);
	}

	// ===================== 窗口 =====================
	bool SettingPage::CreateSettingWindow(HWND owner)
	{
		static const wchar_t kClassName[] = L"YuMediaPlayerSettingPageClass";
		static bool registered = false;

		if (!registered)
		{
			WNDCLASSEXW wc = {};
			wc.cbSize = sizeof(WNDCLASSEXW);
			wc.style = CS_HREDRAW | CS_VREDRAW;
			wc.lpfnWndProc = WndProc;
			wc.hInstance = GetModuleHandleW(nullptr);
			wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
			wc.hbrBackground = nullptr;          // 背景自己画
			wc.lpszClassName = kClassName;
			if (!RegisterClassExW(&wc))
				return false;
			registered = true;
		}

		// 带 owner 的弹出窗口：始终盖在主窗口上面，主窗口隐藏时它也跟着隐藏，不占任务栏
		m_hwnd = CreateWindowExW(WS_EX_TOOLWINDOW, kClassName, L"设置",
			WS_POPUP, 0, 0, kWinW, kWinH, owner, nullptr, GetModuleHandleW(nullptr), this);
		if (!m_hwnd)
			return false;

		HRGN rgn = CreateRoundRectRgn(0, 0, kWinW + 1, kWinH + 1, kCorner * 2, kCorner * 2);
		if (rgn)
			SetWindowRgn(m_hwnd, rgn, TRUE);   // 成功后 region 归系统管理
		return true;
	}

	bool SettingPage::Show(HWND owner)
	{
		if (!m_hwnd && !CreateSettingWindow(owner))
			return false;

		if (owner && owner != m_owner && IsWindow(owner))
			SetWindowLongPtrW(m_hwnd, GWLP_HWNDPARENT, (LONG_PTR)owner);   // 换 owner
		m_owner = owner;

		if (IsWindowVisible(m_hwnd))
		{
			SetForegroundWindow(m_hwnd);
			return true;
		}

		// 居中到主窗口上，并限制在屏幕工作区内
		RECT area = {};
		if (owner && IsWindow(owner))
			GetWindowRect(owner, &area);
		else
			SystemParametersInfoW(SPI_GETWORKAREA, 0, &area, 0);

		int x = area.left + ((area.right - area.left) - kWinW) / 2;
		int y = area.top + ((area.bottom - area.top) - kWinH) / 2;

		HMONITOR mon = MonitorFromRect(&area, MONITOR_DEFAULTTONEAREST);
		MONITORINFO mi = {};
		mi.cbSize = sizeof(mi);
		if (GetMonitorInfoW(mon, &mi))
		{
			if (x + kWinW > mi.rcWork.right)  x = mi.rcWork.right - kWinW;
			if (y + kWinH > mi.rcWork.bottom) y = mi.rcWork.bottom - kWinH;
			if (x < mi.rcWork.left) x = mi.rcWork.left;
			if (y < mi.rcWork.top)  y = mi.rcWork.top;
		}

		// 主窗口是置顶窗口时，设置页也必须置顶，否则会被主窗口盖住
		bool ownerTopmost = owner && IsWindow(owner)
			&& (GetWindowLongPtrW(owner, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0;

		m_hover = Hit();
		m_pressed = Hit();
		SetWindowPos(m_hwnd, ownerTopmost ? HWND_TOPMOST : HWND_TOP, x, y, 0, 0,
			SWP_NOSIZE | SWP_SHOWWINDOW);
		SetForegroundWindow(m_hwnd);
		return true;
	}

	void SettingPage::Hide()
	{
		if (!m_hwnd)
			return;
		if (m_dragItem >= 0)
			ReleaseCapture();
		m_dragItem = -1;
		CloseCombo();
		Save();
		ShowWindow(m_hwnd, SW_HIDE);
	}

	bool SettingPage::IsVisible() const
	{
		return m_hwnd && IsWindowVisible(m_hwnd);
	}

	HWND SettingPage::GetHWND() const
	{
		return m_hwnd;
	}

	LRESULT CALLBACK SettingPage::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
	{
		SettingPage* self = nullptr;
		if (msg == WM_NCCREATE)
		{
			auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
			self = reinterpret_cast<SettingPage*>(cs->lpCreateParams);
			SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
		}
		else
		{
			self = reinterpret_cast<SettingPage*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
		}

		if (self)
			return self->EventProc(hwnd, msg, wParam, lParam);
		return DefWindowProcW(hwnd, msg, wParam, lParam);
	}

	LRESULT SettingPage::EventProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
	{
		switch (msg)
		{
		case WM_ERASEBKGND:
			return 1;   // 背景由 Paint 画，避免闪烁

		case WM_PAINT:
		{
			PAINTSTRUCT ps;
			HDC hdcWnd = BeginPaint(hwnd, &ps);
			RECT rc;
			GetClientRect(hwnd, &rc);
			int w = rc.right - rc.left;
			int h = rc.bottom - rc.top;

			// 双缓冲：先画到内存位图，再一次性贴到屏幕
			HDC hdc = CreateCompatibleDC(hdcWnd);
			HBITMAP bmp = CreateCompatibleBitmap(hdcWnd, w, h);
			HBITMAP oldBmp = (HBITMAP)SelectObject(hdc, bmp);

			Paint(hdc, w, h);

			BitBlt(hdcWnd, ps.rcPaint.left, ps.rcPaint.top,
				ps.rcPaint.right - ps.rcPaint.left, ps.rcPaint.bottom - ps.rcPaint.top,
				hdc, ps.rcPaint.left, ps.rcPaint.top, SRCCOPY);

			SelectObject(hdc, oldBmp);
			DeleteObject(bmp);
			DeleteDC(hdc);
			EndPaint(hwnd, &ps);
			return 0;
		}

		case WM_NCHITTEST:
		{
			// 标题栏区域（除关闭按钮）可以拖动窗口
			POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
			ScreenToClient(hwnd, &pt);
			RECT closeRc = CloseButtonRect();
			if (pt.y >= 0 && pt.y < kTitleH && !PtInRect(&closeRc, pt))
				return HTCAPTION;
			return HTCLIENT;
		}

		case WM_SETCURSOR:
			if (LOWORD(lParam) == HTCLIENT)
			{
				POINT pt;
				GetCursorPos(&pt);
				ScreenToClient(hwnd, &pt);
				SetCursor(LoadCursorW(nullptr, HitTest(pt).kind != HitNone ? IDC_HAND : IDC_ARROW));
				return TRUE;
			}
			break;

		case WM_MOUSEMOVE:
		{
			POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
			if (m_dragItem >= 0)
			{
				// 拖动滑块：实时更新数值
				SetSliderFromX(m_pages[m_tab][m_dragItem], pt.x);
				NotifyChanged(false);
				return 0;
			}
			Hit h = HitTest(pt);
			if (h != m_hover)
			{
				m_hover = h;
				InvalidateRect(hwnd, nullptr, FALSE);
			}
			if (!m_trackingLeave)
			{
				TRACKMOUSEEVENT tme = {};
				tme.cbSize = sizeof(tme);
				tme.dwFlags = TME_LEAVE;
				tme.hwndTrack = hwnd;
				TrackMouseEvent(&tme);
				m_trackingLeave = true;
			}
			return 0;
		}

		case WM_MOUSELEAVE:
			m_trackingLeave = false;
			if (m_hover.kind != HitNone)
			{
				m_hover = Hit();
				InvalidateRect(hwnd, nullptr, FALSE);
			}
			return 0;

		case WM_LBUTTONDOWN:
			OnLButtonDown(POINT{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) });
			return 0;

		case WM_LBUTTONUP:
			OnLButtonUp(POINT{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) });
			return 0;

		case WM_CAPTURECHANGED:
			m_dragItem = -1;
			return 0;

		case WM_MOUSEWHEEL:
			if (m_openCombo >= 0 && m_openCombo < (int)m_pages[m_tab].size())
			{
				// 下拉列表展开时，滚轮用来滚动列表
				const int n = (int)m_pages[m_tab][m_openCombo].options.size();
				const int rows = n < kComboMaxRows ? n : kComboMaxRows;
				const int delta = GET_WHEEL_DELTA_WPARAM(wParam);
				m_comboScroll += (delta > 0 ? -3 : 3);
				if (m_comboScroll > n - rows) m_comboScroll = n - rows;
				if (m_comboScroll < 0) m_comboScroll = 0;
				POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
				ScreenToClient(hwnd, &pt);
				m_hover = HitTest(pt);
				InvalidateRect(hwnd, nullptr, FALSE);
				return 0;
			}
			break;

		case WM_KEYDOWN:
			if (wParam == VK_ESCAPE)
			{
				if (m_openCombo >= 0)       // 先关下拉列表，再按一次才关设置页
				{
					CloseCombo();
					InvalidateRect(hwnd, nullptr, FALSE);
					return 0;
				}
				Hide();
				return 0;
			}
			break;

		case WM_CLOSE:
			Hide();      // 只隐藏，不销毁，下次打开更快
			return 0;

		case WM_DESTROY:
			m_hwnd = nullptr;
			return 0;     // 注意：这里不能 PostQuitMessage，否则关设置页会退出整个程序
		}
		return DefWindowProcW(hwnd, msg, wParam, lParam);
	}

	// ===================== 鼠标交互 =====================
	SettingPage::Hit SettingPage::HitTest(POINT pt) const
	{
		Hit h;

		// 下拉列表展开时它盖在最上面，优先命中
		if (m_openCombo >= 0 && m_openCombo < (int)m_pages[m_tab].size())
		{
			const Item& open = m_pages[m_tab][m_openCombo];
			RECT lr = ComboListRect(open);
			if (PtInRect(&lr, pt))
			{
				const int n = (int)open.options.size();
				const int rows = n < kComboMaxRows ? n : kComboMaxRows;
				const int rel = pt.y - lr.top - kComboPad;
				h.kind = HitComboList;
				h.a = m_openCombo;
				h.b = -1;
				if (rel >= 0 && rel < rows * kComboRowH)
				{
					const int idx = m_comboScroll + rel / kComboRowH;
					if (idx < n)
						h.b = idx;
				}
				return h;
			}
		}

		RECT closeRc = CloseButtonRect();
		if (PtInRect(&closeRc, pt))
		{
			h.kind = HitClose;
			return h;
		}

		for (int i = 0; i < kTabCount; i++)
		{
			RECT r = NavItemRect(i);
			if (PtInRect(&r, pt))
			{
				h.kind = HitNav;
				h.a = i;
				return h;
			}
		}

		const std::vector<Item>& items = m_pages[m_tab];
		for (int i = 0; i < (int)items.size(); i++)
		{
			const Item& it = items[i];
			if (it.type == ItemChoice)
			{
				for (int k = 0; k < (int)it.options.size(); k++)
				{
					RECT r = { it.ctrlRect.left + k * (it.optionW > 0 ? it.optionW : kOptionW), it.ctrlRect.top,
						it.ctrlRect.left + (k + 1) * (it.optionW > 0 ? it.optionW : kOptionW), it.ctrlRect.bottom };
					if (PtInRect(&r, pt))
					{
						h.kind = HitItem;
						h.a = i;
						h.b = k;
						return h;
					}
				}
			}
			else if (it.type != ItemInfo && PtInRect(&it.ctrlRect, pt))
			{
				h.kind = HitItem;
				h.a = i;
				return h;
			}
		}
		return h;
	}

	void SettingPage::SetSliderFromX(const Item& item, int x)
	{
		if (!item.intValue)
			return;
		int trackL = item.ctrlRect.left + 8;
		int trackR = item.ctrlRect.right - 8;
		float t = (float)(x - trackL) / (float)(trackR - trackL);
		if (t < 0.0f) t = 0.0f;
		if (t > 1.0f) t = 1.0f;
		*item.intValue = item.minValue + (int)floorf(t * (item.maxValue - item.minValue) + 0.5f);
	}

	void SettingPage::OnLButtonDown(POINT pt)
	{
		Hit h = HitTest(pt);
		m_pressed = Hit();

		if (m_openCombo >= 0)
		{
			const int openIdx = m_openCombo;
			if (h.kind == HitComboList)
			{
				if (h.b >= 0)
				{
					CloseCombo();
					PickComboRow(openIdx, h.b);
				}
				InvalidateRect(m_hwnd, nullptr, FALSE);
				return;
			}
			CloseCombo();
			// 点在别处：只关闭列表；点在分类/关闭按钮上则继续处理；再点同一个下拉框相当于收起
			if (h.kind != HitNav && h.kind != HitClose)
			{
				InvalidateRect(m_hwnd, nullptr, FALSE);
				return;
			}
		}

		switch (h.kind)
		{
		case HitClose:
			m_pressed = h;          // 松开鼠标时才真正关闭
			break;
		case HitNav:
			if (m_tab != h.a)
			{
				m_tab = h.a;
				m_hover = Hit();
				InvalidateRect(m_hwnd, nullptr, FALSE);
			}
			break;
		case HitItem:
		{
			Item& it = m_pages[m_tab][h.a];
			switch (it.type)
			{
			case ItemToggle:
				if (it.boolValue)
				{
					*it.boolValue = !*it.boolValue;
					NotifyChanged(true);
				}
				break;
			case ItemChoice:
				if (it.intValue && h.b >= 0)
				{
					*it.intValue = h.b;
					NotifyChanged(true);
				}
				break;
			case ItemSlider:
				m_dragItem = h.a;
				SetCapture(m_hwnd);
				SetSliderFromX(it, pt.x);
				NotifyChanged(false);
				break;
			case ItemCombo:
				OpenCombo(h.a);
				break;
			case ItemButton:
				m_pressed = h;      // 松开鼠标时才触发
				break;
			default:
				break;
			}
			break;
		}
		default:
			break;
		}
		InvalidateRect(m_hwnd, nullptr, FALSE);
	}

	void SettingPage::OnLButtonUp(POINT pt)
	{
		if (m_dragItem >= 0)
		{
			// 滑块拖完了：现在才存盘（拖动过程中只实时通知，不频繁写文件）
			m_dragItem = -1;
			ReleaseCapture();
			Save();
			InvalidateRect(m_hwnd, nullptr, FALSE);
			return;
		}

		Hit pressed = m_pressed;
		m_pressed = Hit();
		InvalidateRect(m_hwnd, nullptr, FALSE);

		if (pressed.kind == HitNone || pressed != HitTest(pt))
			return;     // 按下和松开不在同一个控件上，视为取消

		if (pressed.kind == HitClose)
			Hide();
		else if (pressed.kind == HitItem)
			RunAction(m_pages[m_tab][pressed.a].action);
	}

	void SettingPage::NotifyChanged(bool save)
	{
		RebuildIfLanguageChanged();   // 注意：重建后 Item 引用会失效，调用方之后不要再用
		if (m_settings.startWithWindows != m_lastAutoStart)
		{
			ApplyAutoStart(m_settings.startWithWindows);
			m_lastAutoStart = m_settings.startWithWindows;
		}
		if (save)
			Save();
		if (m_settings.musicFolder != m_lastMusicFolder)   // 目录变了（选择新目录 / 恢复默认）
		{
			m_lastMusicFolder = m_settings.musicFolder;
			if (m_onMusicFolder)
				m_onMusicFolder(m_lastMusicFolder);
		}
		if (m_settings.musicSource != m_lastMusicSource)   // 音乐源变了：重新读 client_id，通知外部重新加载歌曲
		{
			m_lastMusicSource = m_settings.musicSource;
			ApplyMusicBackend(m_settings);
			BuildItems();
			LayoutItems();
			if (m_onMusicSource)
				m_onMusicSource();
		}
		ApplyLyricSettings(m_settings);       // 歌词设置变了：桌面歌词立即刷新（拖动字号滑块时实时预览）
		UITheme::SetMode(m_settings.theme);   // 主题变了：主窗口、迷你窗口、设置页一起重绘
		if (m_onChanged)
			m_onChanged(m_settings);
		if (m_hwnd)
			InvalidateRect(m_hwnd, nullptr, FALSE);
	}

	void SettingPage::RunAction(Action action)
	{
		switch (action)
		{
		case ActCheckUpdate:
			if (m_onCheckUpdate)
				m_onCheckUpdate();
			break;

		case ActReset:
			if (MessageBoxW(m_hwnd, Tr(L"确定要将所有设置恢复为默认值吗？"), Tr(L"恢复默认设置"),
				MB_YESNO | MB_ICONQUESTION) == IDYES)
			{
				m_settings.Reset();
				NotifyChanged(true);
			}
			break;

		case ActOpenIni:
		{
			const std::wstring ini = IniPath();
			Save();   // 保证文件存在
			wchar_t probe[32] = { 0 };

			if (wcscmp(probe, L"<unset>") == 0)   // 第一次：先写一个空的键，用户打开就能看到该填哪里
			for (const wchar_t* key : { L"SubsonicUrl", L"SubsonicUser", L"SubsonicPassword" })
			{
				GetPrivateProfileStringW(L"Music", key, L"<unset>", probe, 32, ini.c_str());
				if (wcscmp(probe, L"<unset>") == 0)
					WritePrivateProfileStringW(L"Music", key, L"", ini.c_str());
			}
			ShellExecuteW(m_hwnd, L"open", ini.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
			break;
		}

		case ActReloadMusicSource:
			ApplyMusicBackend(m_settings);
			BuildItems();
			LayoutItems();
			if (m_onMusicSource)
				m_onMusicSource();
			if (m_hwnd)
				InvalidateRect(m_hwnd, nullptr, FALSE);
			break;

		case ActPickFolder:
		{
			// SHBrowseForFolder 的新式对话框需要 COM；已经初始化过（返回 S_FALSE）也要配对 Uninitialize
			HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
			BROWSEINFOW bi = {};
			bi.hwndOwner = m_hwnd;
			bi.lpszTitle = Tr(L"选择本地音乐目录");
			bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;

			bool changed = false;
			LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
			if (pidl)
			{
				wchar_t path[MAX_PATH] = { 0 };
				if (SHGetPathFromIDListW(pidl, path))
				{
					m_settings.musicFolder = path;
					changed = true;
				}
				CoTaskMemFree(pidl);
			}
			if (SUCCEEDED(hr))
				CoUninitialize();
			if (changed)
				NotifyChanged(true);
			break;
		}
		default:
			break;
		}
	}

	// ===================== 绘制 =====================
	void SettingPage::Paint(HDC hdc, int width, int height)
	{
		using namespace Gdiplus;

		Graphics g(hdc);
		g.SetSmoothingMode(SmoothingModeAntiAlias);
		g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);

		const Palette pal = GetPalette(m_settings.theme);
		const Color bgColor = pal.bg;
		const Color accent = pal.accent;
		const Color white = pal.text;            // 主文字色（浅色主题下其实是深色）
		const Color gray = pal.textSub;
		const Color dimGray = pal.textDim;
		const Color lineColor = pal.line;
		const Color darkText = pal.onAccent;

		g.Clear(bgColor);

		// 外边框
		{
			GraphicsPath path;
			BuildRoundRectPath(path, Rect(0, 0, width - 1, height - 1), kCorner);
			Pen pen(pal.border, 1.0f);
			g.DrawPath(&pen, &path);
		}

		FontFamily family(m_settings.fontName.empty() ? L"Microsoft YaHei" : m_settings.fontName.c_str());
		const FontFamily* fam = &family;
		if (family.GetLastStatus() != Ok)      // 系统里没装这个字体就退回默认无衬线字体
			fam = FontFamily::GenericSansSerif();
		Font f12(fam, 12, FontStyleRegular, UnitPixel);
		Font f14(fam, 14, FontStyleRegular, UnitPixel);
		Font f14b(fam, 14, FontStyleBold, UnitPixel);
		Font f15(fam, 15, FontStyleRegular, UnitPixel);
		Font f18b(fam, 18, FontStyleBold, UnitPixel);

		StringFormat fmtLeft;
		fmtLeft.SetAlignment(StringAlignmentNear);
		fmtLeft.SetLineAlignment(StringAlignmentCenter);
		fmtLeft.SetTrimming(StringTrimmingEllipsisCharacter);
		fmtLeft.SetFormatFlags(StringFormatFlagsNoWrap);
		StringFormat fmtCenter;
		fmtCenter.SetAlignment(StringAlignmentCenter);
		fmtCenter.SetLineAlignment(StringAlignmentCenter);
		fmtCenter.SetFormatFlags(StringFormatFlagsNoWrap);
		StringFormat fmtRight;
		fmtRight.SetAlignment(StringAlignmentFar);
		fmtRight.SetLineAlignment(StringAlignmentCenter);
		fmtRight.SetTrimming(StringTrimmingEllipsisPath);
		fmtRight.SetFormatFlags(StringFormatFlagsNoWrap);

		auto drawText = [&](const wchar_t* text, const Font& font, const Color& color,
			float x, float y, float w, float h, const StringFormat& fmt)
			{
				if (w <= 0 || h <= 0)
					return;
				SolidBrush brush(color);
				g.DrawString(text, -1, &font, RectF(x, y, w, h), &fmt, &brush);
			};
		auto fillRound = [&](const RECT& r, int radius, const Color& color)
			{
				GraphicsPath path;
				BuildRoundRectPath(path, Rect(r.left, r.top, r.right - r.left, r.bottom - r.top), radius);
				SolidBrush brush(color);
				g.FillPath(&brush, &path);
			};

		// ---- 标题栏：标题 + 关闭按钮 ----
		drawText(Tr(L"设置"), f18b, white, 24.0f, 0.0f, 200.0f, (float)kTitleH - 4.0f, fmtLeft);
		{
			RECT cr = CloseButtonRect();
			bool hover = (m_hover.kind == HitClose);
			if (hover)
			{
				SolidBrush closeBg(Color(255, 232, 17, 35));
				//g.FillRectangle(&closeBg, cr.left, cr.top, cr.right - cr.left, cr.bottom - cr.top);
				g.FillRectangle(
					&closeBg,
					static_cast<Gdiplus::REAL>(cr.left),
					static_cast<Gdiplus::REAL>(cr.top),
					static_cast<Gdiplus::REAL>(cr.right - cr.left),
					static_cast<Gdiplus::REAL>(cr.bottom - cr.top)
				);
			}
			float cx = (cr.left + cr.right) / 2.0f;
			float cy = (cr.top + cr.bottom) / 2.0f;
			Pen xPen(hover ? Color(255, 255, 255, 255) : pal.closeX, 1.3f);
			g.DrawLine(&xPen, cx - 5.0f, cy - 5.0f, cx + 5.0f, cy + 5.0f);
			g.DrawLine(&xPen, cx + 5.0f, cy - 5.0f, cx - 5.0f, cy + 5.0f);
		}

		// ---- 左侧分类 ----
		const wchar_t* kTabNames[kTabCount] = { Tr(L"通用"), Tr(L"界面"), Tr(L"系统"), Tr(L"歌词"), Tr(L"音乐源"), Tr(L"关于") };
		for (int i = 0; i < kTabCount; i++)
		{
			RECT r = NavItemRect(i);
			bool selected = (i == m_tab);
			bool hover = (m_hover.kind == HitNav && m_hover.a == i);
			if (selected)
				fillRound(r, 10, pal.navSel);
			else if (hover)
				fillRound(r, 10, pal.navHover);
			drawText(kTabNames[i], selected ? f15 : f15, selected ? pal.navSelText : (hover ? white : pal.navText),
				(float)r.left + 18.0f, (float)r.top, (float)(r.right - r.left) - 24.0f, (float)(r.bottom - r.top), fmtLeft);
		}

		// 分隔线：左侧分类 | 右侧内容
		{
			Pen divider(lineColor, 1.0f);
			g.DrawLine(&divider, kContentLeft - 14, kTitleH, kContentLeft - 14, height - 16);
		}

		// ---- 右侧设置项 ----
		const std::vector<Item>& items = m_pages[m_tab];
		for (int i = 0; i < (int)items.size(); i++)
		{
			const Item& it = items[i];
			const RECT& rr = it.rowRect;
			const RECT& cr = it.ctrlRect;
			const float rowW = (float)(rr.right - rr.left);

			bool isHoverItem = (m_hover.kind == HitItem && m_hover.a == i);

			// 每行底部分隔线（最后一行不画）
			if (i + 1 < (int)items.size())
			{
				Pen divider(lineColor, 1.0f);
				//g.DrawLine(&divider, rr.left, rr.bottom - 1, rr.right, rr.bottom - 1);
				g.DrawLine(
					&divider,
					static_cast<Gdiplus::REAL>(rr.left),
					static_cast<Gdiplus::REAL>(rr.bottom - 1),
					static_cast<Gdiplus::REAL>(rr.right),
					static_cast<Gdiplus::REAL>(rr.bottom - 1)
				);
			}

			if (it.type == ItemInfo)
			{
				drawText(it.label.c_str(), f14, gray, (float)rr.left, (float)rr.top, 200.0f, (float)(rr.bottom - rr.top), fmtLeft);
				drawText(it.infoValue.c_str(), f14, dimGray, (float)rr.left + 210.0f, (float)rr.top,
					rowW - 210.0f, (float)(rr.bottom - rr.top), fmtRight);
				continue;
			}

			// 文字区域宽度：右侧有控件时要让开
			float textW = rowW;
			if (it.type == ItemToggle || it.type == ItemSlider || it.type == ItemButton || (it.type == ItemChoice && it.compact) || it.type == ItemCombo)
				textW = (float)(cr.left - rr.left) - 16.0f;
			if (it.type == ItemSlider)
				textW = (float)(cr.left - rr.left) - 16.0f;

			drawText(it.label.c_str(), f15, white, (float)rr.left, (float)rr.top + 10.0f, textW, 24.0f, fmtLeft);

			// 描述行：Button 带 strValue 时显示当前值（目录路径）
			std::wstring desc = it.desc;
			if (it.strValue)
				desc = it.strValue->empty() ? Tr(L"未设置，请点击右侧按钮选择") : *it.strValue;
			drawText(desc.c_str(), f12, dimGray, (float)rr.left, (float)rr.top + 34.0f, textW, 20.0f, fmtLeft);

			switch (it.type)
			{
			case ItemToggle:
			{
				bool on = it.boolValue && *it.boolValue;
				Color track = on ? (isHoverItem ? Color(255, 255, 220, 64) : accent)
					: (isHoverItem ? pal.trackOffHover : pal.trackOff);
				fillRound(cr, 12, track);
				float knobX = on ? (float)cr.right - 3.0f - 18.0f : (float)cr.left + 3.0f;
				SolidBrush knob(on ? darkText : pal.knobOff);
				g.FillEllipse(&knob, knobX, (float)cr.top + 3.0f, 18.0f, 18.0f);
				break;
			}
			case ItemSlider:
			{
				int value = it.intValue ? *it.intValue : it.minValue;
				float trackL = (float)cr.left + 8.0f;
				float trackR = (float)cr.right - 8.0f;
				float cy = (cr.top + cr.bottom) / 2.0f;
				float t = (it.maxValue > it.minValue) ? (float)(value - it.minValue) / (float)(it.maxValue - it.minValue) : 0.0f;
				float thumbX = trackL + (trackR - trackL) * t;

				SolidBrush trackBg(pal.sliderTrack);
				g.FillRectangle(&trackBg, trackL, cy - 2.0f, trackR - trackL, 4.0f);
				SolidBrush trackFill(accent);
				g.FillRectangle(&trackFill, trackL, cy - 2.0f, thumbX - trackL, 4.0f);

				bool active = isHoverItem || (m_dragItem == i);
				float rad = active ? 9.0f : 8.0f;
				SolidBrush thumb(pal.thumb);
				g.FillEllipse(&thumb, thumbX - rad, cy - rad, rad * 2, rad * 2);
				Pen thumbPen(pal.thumbBorder, 1.0f);
				g.DrawEllipse(&thumbPen, thumbX - rad, cy - rad, rad * 2, rad * 2);

				wchar_t buf[32];
				swprintf_s(buf, 32, L"%d%s", value, it.unit.c_str());
				drawText(buf, f14, gray, (float)rr.right - 62.0f, (float)rr.top, 62.0f, (float)(rr.bottom - rr.top), fmtRight);
				break;
			}
			case ItemChoice:
			{
				fillRound(cr, 15, pal.choiceBg);
				int selectedIdx = it.intValue ? *it.intValue : -1;
				for (int k = 0; k < (int)it.options.size(); k++)
				{
					const int ow = it.optionW > 0 ? it.optionW : kOptionW;
					RECT opt = { cr.left + k * ow, cr.top, cr.left + (k + 1) * ow, cr.bottom };
					bool sel = (k == selectedIdx);
					bool hov = (m_hover.kind == HitItem && m_hover.a == i && m_hover.b == k);
					RECT inner = { opt.left + 2, opt.top + 2, opt.right - 2, opt.bottom - 2 };
					if (sel)
						fillRound(inner, 13, accent);
					else if (hov)
						fillRound(inner, 13, pal.choiceHover);
					drawText(it.options[k].c_str(), sel ? f14b : f14,
						sel ? darkText : (hov ? white : pal.navText),
						(float)opt.left, (float)opt.top, (float)(opt.right - opt.left), (float)(opt.bottom - opt.top), fmtCenter);
				}
				break;
			}
			case ItemCombo:
			{
				const bool open = (m_openCombo == i);
				fillRound(cr, 8, (open || isHoverItem) ? pal.choiceHover : pal.choiceBg);
				const int sel = ComboSelectedIndex(it);
				std::wstring shown;
				if (sel >= 0 && sel < (int)it.options.size())
					shown = it.options[sel];
				else if (it.comboStr)
					shown = *it.comboStr;      // 字体没在列表里（比如已被卸载）也照样显示名字
				drawText(shown.c_str(), f14, white, (float)cr.left + 12.0f, (float)cr.top,
					(float)(cr.right - cr.left) - 36.0f, (float)(cr.bottom - cr.top), fmtLeft);
				// 右侧小箭头：收起时朝下，展开时朝上
				const float ax = (float)cr.right - 18.0f;
				const float ay = (cr.top + cr.bottom) / 2.0f;
				PointF pts[3];
				if (open)
				{
					pts[0] = PointF(ax - 4.0f, ay + 2.0f); pts[1] = PointF(ax, ay - 2.0f); pts[2] = PointF(ax + 4.0f, ay + 2.0f);
				}
				else
				{
					pts[0] = PointF(ax - 4.0f, ay - 2.0f); pts[1] = PointF(ax, ay + 2.0f); pts[2] = PointF(ax + 4.0f, ay - 2.0f);
				}
				Pen arrowPen(pal.textSub, 1.6f);
				g.DrawLines(&arrowPen, pts, 3);
				break;
			}
			case ItemButton:
			{
				bool pressed = (m_pressed.kind == HitItem && m_pressed.a == i);
				Color bg = pressed ? pal.btnPress : (isHoverItem ? pal.btnHover : pal.btn);
				fillRound(cr, 8, bg);
				drawText(it.buttonText.c_str(), f14, white,
					(float)cr.left, (float)cr.top, (float)(cr.right - cr.left), (float)(cr.bottom - cr.top), fmtCenter);
				break;
			}
			default:
				break;
			}
		}

		// ---- 展开的下拉列表（最后画，盖在所有设置项上面）----
		if (m_openCombo >= 0 && m_openCombo < (int)items.size())
		{
			const Item& it = items[m_openCombo];
			const RECT lr = ComboListRect(it);
			const int n = (int)it.options.size();
			const int rows = n < kComboMaxRows ? n : kComboMaxRows;
			const int sel = ComboSelectedIndex(it);

			GraphicsPath lp;
			BuildRoundRectPath(lp, Rect(lr.left, lr.top, lr.right - lr.left, lr.bottom - lr.top), 8);
			SolidBrush listBrush(pal.listBg);
			g.FillPath(&listBrush, &lp);
			Pen listPen(pal.border, 1.0f);
			g.DrawPath(&listPen, &lp);

			g.SetClip(&lp);
			for (int r = 0; r < rows; r++)
			{
				const int idx = m_comboScroll + r;
				if (idx >= n)
					break;
				RECT rowRc = { lr.left + 4, lr.top + kComboPad + r * kComboRowH,
					lr.right - 4, lr.top + kComboPad + (r + 1) * kComboRowH };
				const bool selRow = (idx == sel);
				const bool hovRow = (m_hover.kind == HitComboList && m_hover.b == idx);
				if (selRow)
					fillRound(rowRc, 6, accent);
				else if (hovRow)
					fillRound(rowRc, 6, pal.choiceHover);
				const Color tc = selRow ? darkText : (hovRow ? white : pal.navText);

				// 字体列表：每一项用它自己的字体显示（第 0 项"默认"除外）
				FontFamily rowFamily(it.fontList && idx > 0 ? it.options[idx].c_str() : L"");
				const FontFamily* useFam = fam;
				if (it.fontList && idx > 0 && rowFamily.GetLastStatus() == Ok && rowFamily.IsStyleAvailable(FontStyleRegular))
					useFam = &rowFamily;
				Font rowFont(useFam, 14, FontStyleRegular, UnitPixel);

				drawText(it.options[idx].c_str(), rowFont, tc, (float)rowRc.left + 10.0f, (float)rowRc.top,
					(float)(rowRc.right - rowRc.left) - 10.0f - (n > rows ? 10.0f : 0.0f),
					(float)(rowRc.bottom - rowRc.top), fmtLeft);
			}

			// 滚动条（只显示，用滚轮滚动）
			if (n > rows)
			{
				const float trackH = (float)(rows * kComboRowH);
				float thumbH = trackH * rows / n;
				if (thumbH < 24.0f) thumbH = 24.0f;
				const float thumbY = (float)(lr.top + kComboPad) + (trackH - thumbH) * m_comboScroll / (float)(n - rows);
				RECT tr = { lr.right - 9, (int)thumbY, lr.right - 5, (int)(thumbY + thumbH) };
				fillRound(tr, 2, pal.textDim);
			}
			g.ResetClip();
		}
	}
}
