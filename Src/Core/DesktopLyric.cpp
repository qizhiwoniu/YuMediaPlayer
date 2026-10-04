#include "DesktopLyric.h"
#include <windowsx.h>
#include <shlobj.h>
#include <math.h>
#include <wchar.h>
#include <algorithm>

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "shell32.lib")

namespace YuMediaPlayer
{
	namespace
	{
		const wchar_t kClassName[] = L"YuMediaPlayerDesktopLyricClass";
		const UINT_PTR kTimerId = 1;
		const UINT kTimerMs = 40;                       // 25 帧/秒，七彩流动足够顺滑
		const wchar_t* kDefaultFontKeyword = L"胡敬礼";    // 默认字体：名字里含"胡敬礼"的字体
		const wchar_t* kFallbackFont = L"Microsoft YaHei UI";
		const int kCmdLock = 1;
		const int kCmdClose = 2;

		std::wstring ExeDir()
		{
			wchar_t path[MAX_PATH] = { 0 };
			GetModuleFileNameW(nullptr, path, MAX_PATH);
			wchar_t* slash = wcsrchr(path, L'\\');
			if (slash) *slash = L'\0';
			return path;
		}

		// 色相 h(0~1) -> 不透明 RGB（S=0.85, V=1）
		Gdiplus::Color HueColor(float h, BYTE alpha = 255)
		{
			h = h - floorf(h);
			const float s = 0.85f, v = 1.0f;
			float r = 0, g = 0, b = 0;
			const float h6 = h * 6.0f;
			const int i = (int)floorf(h6) % 6;
			const float f = h6 - floorf(h6);
			const float p = v * (1 - s), q = v * (1 - s * f), t = v * (1 - s * (1 - f));
			switch (i)
			{
			case 0: r = v; g = t; b = p; break;
			case 1: r = q; g = v; b = p; break;
			case 2: r = p; g = v; b = t; break;
			case 3: r = p; g = q; b = v; break;
			case 4: r = t; g = p; b = v; break;
			default: r = v; g = p; b = q; break;
			}
			return Gdiplus::Color(alpha, (BYTE)(r * 255), (BYTE)(g * 255), (BYTE)(b * 255));
		}

		bool ContainsNoCase(const std::wstring& s, const wchar_t* key)
		{
			std::wstring a = s, b = key;
			std::transform(a.begin(), a.end(), a.begin(), ::towlower);
			std::transform(b.begin(), b.end(), b.begin(), ::towlower);
			return a.find(b) != std::wstring::npos;
		}

		bool FileExists(const std::wstring& p)
		{
			DWORD a = GetFileAttributesW(p.c_str());
			return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
		}

		// 歌词目录：exe 同目录下的 Assets\lrc（下载的歌词保存在这里，播放时也优先从这里找）
		//   即 YuMediaPlayer\Assets\lrc
		std::wstring LyricDir()
		{
			return ExeDir() + L"\\lrc";
		}

		// 文件名里不能出现的字符替换成下划线（和常见下载器的命名习惯一致）
		std::wstring SafeName(std::wstring s)
		{
			for (wchar_t& c : s)
				if (wcschr(L"\\/:*?\"<>|", c)) c = L'_';
			return s;
		}
	}

	DesktopLyric& DesktopLyric::Instance()
	{
		static DesktopLyric inst;
		return inst;
	}

	DesktopLyric::DesktopLyric()
	{
		Gdiplus::GdiplusStartupInput input;
		Gdiplus::GdiplusStartup(&m_gdiplusToken, &input, nullptr);
	}

	DesktopLyric::~DesktopLyric()
	{
		if (m_hwnd && IsWindow(m_hwnd))
			DestroyWindow(m_hwnd);
		m_hwnd = nullptr;
		m_family.reset();          // GDI+ 对象必须在 GdiplusShutdown 之前释放
		m_privateFonts.reset();
		if (m_gdiplusToken)
			Gdiplus::GdiplusShutdown(m_gdiplusToken);
	}

	// ===================== 字体 =====================
	// 查找顺序：
	//  1) 用户在设置页里选了具体字体 -> 直接用
	//  2) 默认："exe目录\fonts\" 里放的字体文件（把胡敬礼字体 .ttf/.otf 丢进去即可，不用安装）
	//  3) 系统已安装的、名字里含"胡敬礼"的字体
	//  4) 都没有 -> 微软雅黑
	void DesktopLyric::ResolveFont()
	{
		const std::wstring key = m_style.fontName;
		if (m_fontResolved && key == m_resolvedFor)
			return;
		m_fontResolved = true;
		m_resolvedFor = key;
		m_family.reset();

		using namespace Gdiplus;

		if (!key.empty())
		{
			FontFamily f(key.c_str());
			if (f.GetLastStatus() == Ok)
			{
				m_family.reset(f.Clone());
				return;
			}
		}

		// 2) fonts 目录
		if (!m_privateFonts)
		{
			m_privateFonts.reset(new PrivateFontCollection());
			const std::wstring dir = ExeDir() + L"\\fonts\\";
			for (const wchar_t* ext : { L"*.ttf", L"*.otf", L"*.ttc" })
			{
				WIN32_FIND_DATAW fd;
				HANDLE h = FindFirstFileW((dir + ext).c_str(), &fd);
				if (h == INVALID_HANDLE_VALUE)
					continue;
				do
				{
					m_privateFonts->AddFontFile((dir + fd.cFileName).c_str());
				} while (FindNextFileW(h, &fd));
				FindClose(h);
			}
		}
		{
			const int count = m_privateFonts->GetFamilyCount();
			if (count > 0)
			{
				std::vector<FontFamily> fams(count);
				int found = 0;
				m_privateFonts->GetFamilies(count, fams.data(), &found);
				int pick = 0;                          // 默认取第一个；名字含"胡敬礼"的优先
				for (int i = 0; i < found; i++)
				{
					WCHAR name[LF_FACESIZE] = { 0 };
					fams[i].GetFamilyName(name);
					if (ContainsNoCase(name, kDefaultFontKeyword)) { pick = i; break; }
				}
				if (found > 0)
				{
					m_family.reset(fams[pick].Clone());
					return;
				}
			}
		}

		// 3) 系统已安装
		{
			InstalledFontCollection installed;
			const int count = installed.GetFamilyCount();
			if (count > 0)
			{
				std::vector<FontFamily> fams(count);
				int found = 0;
				installed.GetFamilies(count, fams.data(), &found);
				for (int i = 0; i < found; i++)
				{
					WCHAR name[LF_FACESIZE] = { 0 };
					fams[i].GetFamilyName(name);
					if (ContainsNoCase(name, kDefaultFontKeyword))
					{
						m_family.reset(fams[i].Clone());
						return;
					}
				}
			}
		}

		// 4) 回退
		FontFamily fb(kFallbackFont);
		if (fb.GetLastStatus() == Ok)
			m_family.reset(fb.Clone());
		else
			m_family.reset(FontFamily::GenericSansSerif()->Clone());
	}

	// ===================== LRC 解析 =====================
	std::wstring DesktopLyric::DecodeText(const std::string& b)
	{
		if (b.empty()) return L"";
		if (b.size() >= 2 && (unsigned char)b[0] == 0xFF && (unsigned char)b[1] == 0xFE)   // UTF-16 LE
			return std::wstring((const wchar_t*)(b.data() + 2), (b.size() - 2) / 2);
		size_t off = 0;
		if (b.size() >= 3 && (unsigned char)b[0] == 0xEF && (unsigned char)b[1] == 0xBB && (unsigned char)b[2] == 0xBF)
			off = 3;
		// 先按严格 UTF-8 解，失败再按系统 ANSI（GBK）
		int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, b.data() + off, (int)(b.size() - off), nullptr, 0);
		UINT cp = CP_UTF8;
		DWORD flags = MB_ERR_INVALID_CHARS;
		if (n <= 0) { cp = CP_ACP; flags = 0; n = MultiByteToWideChar(cp, flags, b.data() + off, (int)(b.size() - off), nullptr, 0); }
		if (n <= 0) return L"";
		std::wstring w(n, L'\0');
		MultiByteToWideChar(cp, flags, b.data() + off, (int)(b.size() - off), &w[0], n);
		return w;
	}

	void DesktopLyric::ParseLrc(const std::wstring& text, std::vector<Line>& out)
	{
		out.clear();
		int offsetMs = 0;
		size_t pos = 0;
		while (pos <= text.size())
		{
			size_t end = text.find_first_of(L"\r\n", pos);
			std::wstring line = text.substr(pos, end == std::wstring::npos ? std::wstring::npos : end - pos);
			pos = (end == std::wstring::npos) ? text.size() + 1 : end + 1;

			std::vector<int> times;
			size_t i = 0;
			while (i < line.size() && line[i] == L'[')
			{
				size_t close = line.find(L']', i);
				if (close == std::wstring::npos) break;
				std::wstring tag = line.substr(i + 1, close - i - 1);
				int mm = 0, ss = 0;
				// [mm:ss] / [mm:ss.xx] / [mm:ss:xx]
				if (swscanf_s(tag.c_str(), L"%d:%d", &mm, &ss) == 2 && tag.find_first_not_of(L"0123456789:.") == std::wstring::npos)
				{
					int ms = 0;
					size_t sep = tag.find_first_of(L".", tag.find(L':') + 1);
					if (sep == std::wstring::npos) sep = tag.find(L':', tag.find(L':') + 1);
					if (sep != std::wstring::npos)
					{
						std::wstring f = tag.substr(sep + 1);
						while (f.size() < 3) f += L'0';
						f = f.substr(0, 3);
						ms = _wtoi(f.c_str());
					}
					times.push_back(mm * 60000 + ss * 1000 + ms);
				}
				else if (_wcsnicmp(tag.c_str(), L"offset:", 7) == 0)
					offsetMs = _wtoi(tag.c_str() + 7);
				// 其它 [ar:] [ti:] 等元数据直接跳过
				i = close + 1;
			}
			if (times.empty()) continue;

			std::wstring content = line.substr(i);
			// 去掉增强 LRC 的逐字时间戳 <mm:ss.xx>
			std::wstring clean;
			for (size_t k = 0; k < content.size(); k++)
			{
				if (content[k] == L'<')
				{
					size_t c = content.find(L'>', k);
					if (c != std::wstring::npos) { k = c; continue; }
				}
				clean += content[k];
			}
			// 首尾空白
			size_t a = clean.find_first_not_of(L" \t");
			size_t z = clean.find_last_not_of(L" \t");
			clean = (a == std::wstring::npos) ? L"" : clean.substr(a, z - a + 1);

			for (int t : times)
				out.push_back({ t, clean });
		}
		for (Line& l : out) { l.timeMs -= offsetMs; if (l.timeMs < 0) l.timeMs = 0; }
		std::stable_sort(out.begin(), out.end(), [](const Line& a, const Line& b) { return a.timeMs < b.timeMs; });
	}

	bool DesktopLyric::SetLyricsFromLrc(const std::wstring& lrcText)
	{
		std::vector<Line> lines;
		ParseLrc(lrcText, lines);
		m_lines = std::move(lines);
		m_lastIndex = -2;
		m_dirty = true;
		if (m_hwnd) Render();
		return !m_lines.empty();
	}

	bool DesktopLyric::LoadLrcFile(const std::wstring& path)
	{
		HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (h == INVALID_HANDLE_VALUE)
			return false;
		LARGE_INTEGER sz = {};
		GetFileSizeEx(h, &sz);
		if (sz.QuadPart <= 0 || sz.QuadPart > 4 * 1024 * 1024)   // 歌词文件不会超过 4MB
		{
			CloseHandle(h);
			return false;
		}
		std::string bytes((size_t)sz.QuadPart, '\0');
		DWORD read = 0;
		ReadFile(h, &bytes[0], (DWORD)bytes.size(), &read, nullptr);
		CloseHandle(h);
		bytes.resize(read);
		return SetLyricsFromLrc(DecodeText(bytes));
	}

	std::wstring DesktopLyric::GetLyricDir()
	{
		const std::wstring dir = LyricDir();
		SHCreateDirectoryExW(nullptr, dir.c_str(), nullptr);   // 已存在时返回错误码，忽略即可
		return dir;
	}

	void DesktopLyric::AddSearchDir(const std::wstring& dir)
	{
		if (dir.empty()) return;
		for (const std::wstring& d : m_searchDirs)
			if (_wcsicmp(d.c_str(), dir.c_str()) == 0) return;
		m_searchDirs.push_back(dir);
	}

	bool DesktopLyric::LoadForTrack(const std::wstring& audioPath, const std::wstring& title, const std::wstring& artist)
	{
		std::vector<std::wstring> candidates;

		// 1) 与音频同名的 .lrc
		const size_t dot = audioPath.find_last_of(L'.');
		const size_t sep = audioPath.find_last_of(L"\\/");
		if (dot != std::wstring::npos && (sep == std::wstring::npos || dot > sep))
			candidates.push_back(audioPath.substr(0, dot) + L".lrc");

		// 2) lyrics 目录 + 额外目录：标题.lrc / 歌手 - 标题.lrc / 标题 - 歌手.lrc
		std::vector<std::wstring> dirs = { LyricDir() };
		for (const std::wstring& d : m_searchDirs) dirs.push_back(d);
		std::vector<std::wstring> names;
		if (!title.empty())
		{
			names.push_back(SafeName(title));
			if (!artist.empty())
			{
				names.push_back(SafeName(artist + L" - " + title));
				names.push_back(SafeName(title + L" - " + artist));
			}
		}
		// 音频文件名（不含扩展名）本身也试一下
		if (dot != std::wstring::npos && sep != std::wstring::npos && dot > sep)
			names.push_back(audioPath.substr(sep + 1, dot - sep - 1));
		for (const std::wstring& d : dirs)
			for (const std::wstring& n : names)
				candidates.push_back(d + L"\\" + n + L".lrc");

		for (const std::wstring& c : candidates)
			if (FileExists(c) && LoadLrcFile(c))
				return true;

		ClearLyrics(L"\u6682\u65E0\u6B4C\u8BCD");   // 暂无歌词
		return false;
	}

	void DesktopLyric::ClearLyrics(const wchar_t* hint)
	{
		m_lines.clear();
		if (hint) m_hint = hint;
		m_lastIndex = -2;
		m_dirty = true;
		if (m_hwnd) Render();
	}

	// ===================== 进度 =====================
	void DesktopLyric::SetPosition(int positionMs)
	{
		m_basePosMs = positionMs < 0 ? 0 : positionMs;
		m_baseTick = GetTickCount64();
	}

	void DesktopLyric::SetPlaying(bool playing)
	{
		if (m_playing == playing) return;
		// 切换前先把当前估计位置固定下来，暂停后不再前进
		m_basePosMs = EstimatePositionMs();
		m_baseTick = GetTickCount64();
		m_playing = playing;
	}

	int DesktopLyric::EstimatePositionMs() const
	{
		if (!m_playing) return m_basePosMs;
		return m_basePosMs + (int)(GetTickCount64() - m_baseTick);
	}

	int DesktopLyric::CurrentIndex() const
	{
		if (m_lines.empty()) return -1;
		const int pos = EstimatePositionMs();
		int lo = 0, hi = (int)m_lines.size();   // 找最后一个 timeMs <= pos 的行
		while (lo < hi)
		{
			int mid = (lo + hi) / 2;
			if (m_lines[mid].timeMs <= pos) lo = mid + 1; else hi = mid;
		}
		return lo - 1;
	}

	// ===================== 窗口 =====================
	bool DesktopLyric::EnsureWindow()
	{
		if (m_hwnd && IsWindow(m_hwnd))
			return true;

		static bool registered = false;
		if (!registered)
		{
			WNDCLASSEXW wc = {};
			wc.cbSize = sizeof(wc);
			wc.lpfnWndProc = WndProc;
			wc.hInstance = GetModuleHandleW(nullptr);
			wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
			wc.lpszClassName = kClassName;
			if (!RegisterClassExW(&wc))
				return false;
			registered = true;
		}

		// 分层 + 置顶 + 工具窗口（不占任务栏）+ 不抢焦点
		m_hwnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
			kClassName, L"YuMediaPlayer Lyric", WS_POPUP, 0, 0, 400, 80,
			nullptr, nullptr, GetModuleHandleW(nullptr), this);
		return m_hwnd != nullptr;
	}

	bool DesktopLyric::Show()
	{
		if (!EnsureWindow())
			return false;
		ApplyLocked();
		m_dirty = true;
		Render();
		ShowWindow(m_hwnd, SW_SHOWNOACTIVATE);
		SetTimer(m_hwnd, kTimerId, kTimerMs, nullptr);
		return true;
	}

	void DesktopLyric::Hide()
	{
		if (!m_hwnd) return;
		KillTimer(m_hwnd, kTimerId);
		ShowWindow(m_hwnd, SW_HIDE);
	}

	void DesktopLyric::Toggle()
	{
		if (IsVisible()) Hide(); else Show();
	}

	bool DesktopLyric::IsVisible() const
	{
		return m_hwnd && IsWindow(m_hwnd) && IsWindowVisible(m_hwnd);
	}

	void DesktopLyric::SetStyle(const LyricStyle& style)
	{
		const bool fontChanged = (style.fontName != m_style.fontName);
		m_style = style;
		if (m_style.fontSize < 6) m_style.fontSize = 6;
		if (m_style.fontSize > 72) m_style.fontSize = 72;
		if (m_style.rainbowSpeed < 1) m_style.rainbowSpeed = 1;
		if (m_style.rainbowSpeed > 10) m_style.rainbowSpeed = 10;
		if (fontChanged) m_fontResolved = false;
		if (m_hwnd)
		{
			ApplyLocked();
			m_dirty = true;
			if (IsVisible()) Render();
		}
	}

	void DesktopLyric::SetPlacement(int x, int y)
	{
		m_hasPos = true;
		m_posX = x;
		m_posY = y;
		if (m_hwnd && IsVisible())
		{
			SetWindowPos(m_hwnd, HWND_TOPMOST, x, y, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE);
			m_dirty = true;
			Render();
		}
	}

	// 锁定 = 鼠标穿透（WS_EX_TRANSPARENT），点击会直接落到下面的窗口
	void DesktopLyric::ApplyLocked()
	{
		if (!m_hwnd) return;
		LONG_PTR ex = GetWindowLongPtrW(m_hwnd, GWL_EXSTYLE);
		LONG_PTR want = m_style.locked ? (ex | WS_EX_TRANSPARENT) : (ex & ~(LONG_PTR)WS_EX_TRANSPARENT);
		if (want != ex)
			SetWindowLongPtrW(m_hwnd, GWL_EXSTYLE, want);
	}

	// ===================== 绘制 =====================
	void DesktopLyric::Render()
	{
		if (!m_hwnd) return;
		using namespace Gdiplus;

		ResolveFont();
		if (!m_family) return;

		HDC screen = GetDC(nullptr);
		const float dpi = (float)GetDeviceCaps(screen, LOGPIXELSY);
		const float emPx = (float)m_style.fontSize * dpi / 72.0f;       // pt -> 像素
		const float subPx = emPx * 0.8f;                                // 下一句略小
		const float lineH = emPx * 1.45f;
		const float subH = subPx * 1.45f;
		const float padX = 18.0f, padY = 8.0f;

		// 窗口大小：宽度取工作区 60%（最小 420、最大 1400），高度按字号算
		RECT work = {};
		SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
		int w = (int)((work.right - work.left) * 0.6f);
		if (w < 420) w = 420;
		if (w > 1400) w = 1400;
		if (w > work.right - work.left) w = work.right - work.left;
		const int h = (int)(lineH + subH + padY * 2 + 2);

		// 位置：用户拖过/设置过就用，否则屏幕下方居中
		RECT cur = {};
		GetWindowRect(m_hwnd, &cur);
		int x = cur.left, y = cur.top;
		if (m_hasPos) { x = m_posX; y = m_posY; m_hasPos = false; }
		else if (!IsWindowVisible(m_hwnd) && cur.left == 0 && cur.top == 0)
		{
			x = work.left + ((work.right - work.left) - w) / 2;
			y = work.bottom - h - 60;
		}

		Bitmap bmp(w, h, PixelFormat32bppPARGB);
		{
			Graphics g(&bmp);
			g.SetSmoothingMode(SmoothingModeAntiAlias);
			g.SetTextRenderingHint(TextRenderingHintAntiAlias);
			g.Clear(Color(0, 0, 0, 0));

			// 鼠标悬停（且没锁定）时显示一层淡淡的底，提示可以拖动
			if (m_hover && !m_style.locked)
			{
				GraphicsPath bg;
				const int r = 12, d = r * 2;
				bg.AddArc(0, 0, d, d, 180, 90);
				bg.AddArc(w - 1 - d, 0, d, d, 270, 90);
				bg.AddArc(w - 1 - d, h - 1 - d, d, d, 0, 90);
				bg.AddArc(0, h - 1 - d, d, d, 90, 90);
				bg.CloseAllFigures();
				SolidBrush bgBrush(Color(90, 0, 0, 0));
				g.FillPath(&bgBrush, &bg);
			}

			// 取当前句 / 下一句文字
			std::wstring cur1, next1;
			const int idx = CurrentIndex();
			if (m_lines.empty())
				cur1 = m_hint;
			else
			{
				if (idx >= 0) cur1 = m_lines[idx].text;
				if (idx + 1 < (int)m_lines.size()) next1 = m_lines[idx + 1].text;
				if (idx < 0 && !m_lines.empty()) next1 = m_lines[0].text;   // 还没唱到第一句
				if (cur1.empty() && idx >= 0) cur1 = L"\u266A";          // 间奏
			}

			StringFormat fmt;
			fmt.SetAlignment(StringAlignmentNear);
			fmt.SetLineAlignment(StringAlignmentNear);
			fmt.SetFormatFlags(StringFormatFlagsNoWrap | StringFormatFlagsMeasureTrailingSpaces);

			const int style = FontStyleRegular;
			const float maxW = (float)w - padX * 2;

			// 画一行：居中；太宽就等比缩小；先画描边再画填充
			auto drawLine = [&](const std::wstring& text, float em, float top, bool primary)
			{
				if (text.empty()) return;
				float useEm = em;
				GraphicsPath path;
				for (int attempt = 0; attempt < 2; attempt++)
				{
					path.Reset();
					path.AddString(text.c_str(), -1, m_family.get(), style, useEm, PointF(0, 0), &fmt);
					RectF b;
					path.GetBounds(&b);
					if (b.Width > maxW && attempt == 0 && b.Width > 1)
					{
						useEm = em * maxW / b.Width;       // 等比缩小到刚好放下
						continue;
					}
					break;
				}
				RectF b;
				path.GetBounds(&b);
				const float tx = ((float)w - b.Width) / 2.0f - b.X;
				const float ty = top + (((primary ? lineH : subH) - b.Height) / 2.0f) - b.Y;
				Matrix mtx;
				mtx.Translate(tx, ty);
				path.Transform(&mtx);
				path.GetBounds(&b);

				const BYTE alpha = primary ? 255 : 170;

				// 描边（深色），宽度随字号缩放，小字号时不会把字糊住
				float pw = useEm / 7.0f;
				if (pw < 1.2f) pw = 1.2f;
				Pen outline(Color((BYTE)(alpha * 0.85f), 0, 0, 0), pw);
				outline.SetLineJoin(LineJoinRound);
				g.DrawPath(&outline, &path);

				if (m_style.colorMode == 1)
				{
					// 七彩：横向渐变，色相随 m_hue 滚动，循环无缝
					const float period = (b.Width > 160.0f) ? b.Width : 160.0f;
					LinearGradientBrush br(PointF(b.X, 0.0f), PointF(b.X + period, 0.0f),
						Color(alpha, 255, 0, 0), Color(alpha, 255, 0, 0));
					const int N = 8;
					Color cols[N + 1];
					REAL pos[N + 1];
					for (int i = 0; i <= N; i++)
					{
						cols[i] = HueColor(m_hue + (float)i / (float)N, alpha);
						pos[i] = (float)i / (float)N;
					}
					br.SetInterpolationColors(cols, pos, N + 1);
					br.SetWrapMode(WrapModeTile);
					g.FillPath(&br, &path);
				}
				else
				{
					SolidBrush br(Color(alpha, 255, 204, 0));    // 黄色，和播放器强调色一致
					g.FillPath(&br, &path);
				}
			};

			drawLine(cur1, emPx, padY, true);
			drawLine(next1, subPx, padY + lineH, false);
		}

		HBITMAP hbm = nullptr;
		bmp.GetHBITMAP(Color(0, 0, 0, 0), &hbm);
		if (hbm)
		{
			HDC mem = CreateCompatibleDC(screen);
			HGDIOBJ old = SelectObject(mem, hbm);
			POINT dst = { x, y };
			SIZE size = { w, h };
			POINT src = { 0, 0 };
			BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
			UpdateLayeredWindow(m_hwnd, screen, &dst, &size, mem, &src, 0, &bf, ULW_ALPHA);
			SelectObject(mem, old);
			DeleteDC(mem);
			DeleteObject(hbm);
		}
		ReleaseDC(nullptr, screen);
		m_dirty = false;
	}

	// ===================== 右键菜单 =====================
	void DesktopLyric::ShowContextMenu()
	{
		HMENU menu = CreatePopupMenu();
		AppendMenuW(menu, MF_STRING | (m_style.locked ? MF_CHECKED : 0), kCmdLock, L"\u9501\u5B9A\u6B4C\u8BCD");   // 锁定歌词
		AppendMenuW(menu, MF_STRING, kCmdClose, L"\u5173\u95ED\u6B4C\u8BCD");                                    // 关闭歌词
		POINT pt;
		GetCursorPos(&pt);
		SetForegroundWindow(m_hwnd);
		const int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, m_hwnd, nullptr);
		DestroyMenu(menu);
		if (cmd == kCmdLock)
		{
			m_style.locked = true;
			ApplyLocked();
			m_dirty = true;
			Render();
			if (m_onUserChanged) m_onUserChanged(true, true);
		}
		else if (cmd == kCmdClose)
		{
			Hide();
			if (m_onUserChanged) m_onUserChanged(false, m_style.locked);
		}
	}

	// ===================== 消息 =====================
	LRESULT CALLBACK DesktopLyric::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
	{
		DesktopLyric* self = nullptr;
		if (msg == WM_NCCREATE)
		{
			auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
			self = reinterpret_cast<DesktopLyric*>(cs->lpCreateParams);
			SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
		}
		else
			self = reinterpret_cast<DesktopLyric*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
		if (self)
			return self->EventProc(hwnd, msg, wParam, lParam);
		return DefWindowProcW(hwnd, msg, wParam, lParam);
	}

	LRESULT DesktopLyric::EventProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
	{
		switch (msg)
		{
		case WM_NCHITTEST:
			// 整个窗口当作标题栏，按住就能拖动（锁定时有 WS_EX_TRANSPARENT，鼠标本来就穿透）
			return m_style.locked ? HTTRANSPARENT : HTCAPTION;

		case WM_MOUSEACTIVATE:
			return MA_NOACTIVATE;

		case WM_NCRBUTTONUP:
			ShowContextMenu();
			return 0;

		case WM_EXITSIZEMOVE:
		{
			RECT r;
			GetWindowRect(hwnd, &r);
			m_posX = r.left;
			m_posY = r.top;
			m_hasPos = false;
			if (m_onMoved) m_onMoved(r.left, r.top);
			return 0;
		}

		case WM_TIMER:
			if (wParam == kTimerId)
			{
				bool redraw = m_dirty;

				// 七彩：相位每帧前进一点，速度 1~10 -> 约 6~60 秒转一圈
				if (m_style.colorMode == 1)
				{
					m_hue += (float)m_style.rainbowSpeed * 0.0006f;
					if (m_hue >= 1.0f) m_hue -= 1.0f;
					redraw = true;
				}

				// 句子切换
				const int idx = CurrentIndex();
				if (idx != m_lastIndex) { m_lastIndex = idx; redraw = true; }

				// 悬停提示
				if (!m_style.locked)
				{
					POINT pt; GetCursorPos(&pt);
					RECT r; GetWindowRect(hwnd, &r);
					const bool hover = PtInRect(&r, pt) != FALSE;
					if (hover != m_hover) { m_hover = hover; redraw = true; }
				}
				else if (m_hover) { m_hover = false; redraw = true; }

				if (redraw) Render();
			}
			return 0;

		case WM_DESTROY:
			KillTimer(hwnd, kTimerId);
			return 0;
		}
		return DefWindowProcW(hwnd, msg, wParam, lParam);
	}
}
