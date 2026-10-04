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
		const wchar_t* kDefaultFontName = L"胡敬礼毛笔行书简";   // 默认字体的完整名字
		const wchar_t* kDefaultFontKeyword = L"胡敬礼";          // 名字对不上时，按名字里含"胡敬礼"来找
		const wchar_t* kFallbackFont = L"Microsoft YaHei UI";
		const float kBarH = 30.0f;       // 歌词上方给"锁定/关闭"小图标留的一条高度
		const int   kBtnSize = 26;       // 小图标直径
		const int   kBtnGap = 10;

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
		std::wstring LyricDir()
		{
			return ExeDir() + L"\\lrc";
		}

		// 老版本代码实际用的是 exe\lrc，以前下载的歌词可能还在那里，找歌词时也看一眼
		std::wstring LegacyLyricDir()
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
	//  2) 默认字体"胡敬礼毛笔行书简"：先看系统里是否已安装（安装程序会把 font 目录里的字体装进系统）
	//  3) exe 目录下 font\（或 fonts\）里的字体文件，不用安装也能直接加载
	//  4) 系统里名字含"胡敬礼"的字体
	//  5) 都没有 -> 微软雅黑
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

		// 2) 已安装的默认字体（按完整名字）
		{
			FontFamily f(kDefaultFontName);
			if (f.GetLastStatus() == Ok)
			{
				m_family.reset(f.Clone());
				return;
			}
		}

		// 3) font / fonts 目录里的字体文件
		if (!m_privateFonts)
		{
			m_privateFonts.reset(new PrivateFontCollection());
			for (const wchar_t* sub : { L"\\font\\", L"\\fonts\\" })
			{
				const std::wstring dir = ExeDir() + sub;
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
		}
		{
			const int count = m_privateFonts->GetFamilyCount();
			if (count > 0)
			{
				std::vector<FontFamily> fams(count);
				int found = 0;
				m_privateFonts->GetFamilies(count, fams.data(), &found);
				int pick = 0;   // 名字对得上优先；字体内部名字是英文时对不上，就取第一个（目录里只放这一个字体）
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

		// 4) 系统已安装、名字里含"胡敬礼"
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

		// 5) 回退
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

	void DesktopLyric::ParseLrc(const std::wstring& text, std::vector<Line>& out, LrcMeta* meta)
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
				else if (meta)
				{
					// [ti:] [ar:] [al:] [au:] [by:] 元数据，前奏信息会用到
					const size_t cc = tag.find(L':');
					if (cc != std::wstring::npos)
					{
						std::wstring key = tag.substr(0, cc), val = tag.substr(cc + 1);
						for (auto& ch : key) ch = (wchar_t)towlower(ch);
						const size_t a0 = val.find_first_not_of(L" \t"), z0 = val.find_last_not_of(L" \t");
						val = (a0 == std::wstring::npos) ? L"" : val.substr(a0, z0 - a0 + 1);
						if (key == L"ti") meta->ti = val;
						else if (key == L"ar") meta->ar = val;
						else if (key == L"al") meta->al = val;
						else if (key == L"au") meta->au = val;
						else if (key == L"by") meta->by = val;
					}
				}
				// 其它标签直接跳过
				i = close + 1;
			}
			if (times.empty()) continue;

			std::wstring content = line.substr(i);
			// 增强 LRC 的逐字时间戳 <mm:ss.xx>：从文字里去掉，同时记下"第几个字开始唱的时间"
			std::wstring clean;
			std::vector<std::pair<int, int>> marks;
			for (size_t k = 0; k < content.size(); k++)
			{
				if (content[k] == L'<')
				{
					size_t c = content.find(L'>', k);
					if (c != std::wstring::npos)
					{
						const std::wstring tg = content.substr(k + 1, c - k - 1);
						int mm = 0, ss = 0;
						if (swscanf_s(tg.c_str(), L"%d:%d", &mm, &ss) == 2 && tg.find_first_not_of(L"0123456789:.") == std::wstring::npos)
						{
							int ms = 0;
							size_t sep = tg.find_first_of(L".", tg.find(L':') + 1);
							if (sep == std::wstring::npos) sep = tg.find(L':', tg.find(L':') + 1);
							if (sep != std::wstring::npos)
							{
								std::wstring f = tg.substr(sep + 1);
								while (f.size() < 3) f += L'0';
								ms = _wtoi(f.substr(0, 3).c_str());
							}
							marks.push_back({ (int)clean.size(), mm * 60000 + ss * 1000 + ms });
						}
						k = c;
						continue;
					}
				}
				clean += content[k];
			}
			// 首尾空白（字符下标跟着前移）
			size_t a = clean.find_first_not_of(L" \t");
			size_t z = clean.find_last_not_of(L" \t");
			const int shift = (a == std::wstring::npos) ? 0 : (int)a;
			clean = (a == std::wstring::npos) ? L"" : clean.substr(a, z - a + 1);
			for (auto& m : marks)
			{
				m.first -= shift;
				if (m.first < 0) m.first = 0;
				if (m.first > (int)clean.size()) m.first = (int)clean.size();
			}
			if (times.size() > 1 || clean.empty()) marks.clear();   // 一行多个时间戳的写法不带逐字时间

			for (int t : times)
				out.push_back({ t, clean, marks });
		}
		for (Line& l : out)
		{
			l.timeMs -= offsetMs; if (l.timeMs < 0) l.timeMs = 0;
			for (auto& m : l.marks) m.second -= offsetMs;
		}
		std::stable_sort(out.begin(), out.end(), [](const Line& a, const Line& b) { return a.timeMs < b.timeMs; });
	}

	// 前奏信息：很多歌第一句歌词要等 15~30 秒，这期间歌词栏是空的。
	//  - 歌词开头如果有"作词 : xx / 作曲 : xx / 编曲 : xx / 演唱 : xx"之类的信息行（网易云、QQ 音乐的 LRC 常见），
	//    把它们重新排时间，均匀铺在第一句歌词之前，一条一条轮流显示
	//  - 没有的话，用"歌名 - 歌手"补一行
	void DesktopLyric::BuildIntro(std::vector<Line>& lines, const std::wstring& title, const std::wstring& artist)
	{
		if (lines.empty()) return;

		static const wchar_t* kCreditKeys[] = {
			L"\u4F5C\u8BCD", L"\u4F5C\u66F2", L"\u7F16\u66F2", L"\u586B\u8BCD", L"\u8BCD", L"\u66F2",   // 作词 作曲 编曲 填词 词 曲
			L"\u6F14\u5531", L"\u6B4C\u624B", L"\u539F\u5531", L"\u7FFB\u5531", L"\u914D\u5531",         // 演唱 歌手 原唱 翻唱 配唱
			L"\u5236\u4F5C", L"\u76D1\u5236", L"\u5236\u7247", L"\u51FA\u54C1", L"\u7B56\u5212", L"\u7EDF\u7B79", L"\u53D1\u884C",   // 制作 监制 制片 出品 策划 统筹 发行
			L"\u6DF7\u97F3", L"\u6BCD\u5E26", L"\u5F55\u97F3", L"\u548C\u58F0", L"\u5409\u4ED6", L"\u8D1D\u65AF", L"\u9F13", L"\u94A2\u7434", L"\u5F26\u4E50", L"\u7F16\u5199", L"\u97F3\u4E50",
			L"lyric", L"compos", L"arrang", L"produc", L"vocal", L"mix", L"master", L"music", L"writ", L"sing", L"guitar", L"bass", L"drum", L"piano", L"string", L"record", L"engineer", L"publish", L"label"
		};

		auto isCredit = [&](const std::wstring& t) -> bool
		{
			if (t.empty()) return false;
			const size_t c = t.find_first_of(L":\uFF1A");     // 半角/全角冒号
			if (c == std::wstring::npos || c == 0 || c > 24) return false;
			const std::wstring key = t.substr(0, c);
			for (const wchar_t* k : kCreditKeys)
				if (ContainsNoCase(key, k)) return true;
			return false;
		};

		// 开头连续的信息行
		size_t n = 0;
		while (n < lines.size() && isCredit(lines[n].text)) n++;
		// 有些歌词第一行是"歌名 - 歌手"
		bool hasHeader = false;
		if (n == 0 && !title.empty() && lines[0].timeMs <= 3000 && lines[0].text.find(L" - ") != std::wstring::npos
			&& ContainsNoCase(lines[0].text, title.c_str()))
		{
			n = 1; hasHeader = true;
			while (n < lines.size() && isCredit(lines[n].text)) n++;
		}
		if (n >= lines.size()) return;                 // 整首都是信息行，不处理

		const int firstReal = lines[n].timeMs;         // 第一句真正的歌词的开始时间
		if (firstReal < 3000) return;                  // 前奏很短，不用填

		std::vector<std::wstring> intro;
		if (n > 0)
		{
			if (!hasHeader && !title.empty())
				intro.push_back(artist.empty() ? title : title + L" - " + artist);
			for (size_t i = 0; i < n; i++) intro.push_back(lines[i].text);
		}
		else if (!title.empty())
			intro.push_back(artist.empty() ? title : title + L" - " + artist);
		else if (!artist.empty())
			intro.push_back(artist);
		if (intro.empty()) return;

		// 每条至少显示 1.8 秒、最多 4.5 秒；前奏太短放不下就只留前面几条
		const int kMinSlot = 1800, kMaxSlot = 4500;
		size_t count = intro.size();
		const size_t fit = (firstReal / kMinSlot) > 1 ? (size_t)(firstReal / kMinSlot) : 1;
		if (count > fit) count = fit;
		const int slot = (firstReal / (int)count) < kMaxSlot ? (firstReal / (int)count) : kMaxSlot;

		std::vector<Line> out;
		out.reserve(count + lines.size() - n);
		for (size_t i = 0; i < count; i++)
			out.push_back({ (int)i * slot, intro[i], {} });
		for (size_t i = n; i < lines.size(); i++)
			out.push_back(std::move(lines[i]));
		lines = std::move(out);
	}

	bool DesktopLyric::SetLyricsFromLrc(const std::wstring& lrcText)
	{
		std::vector<Line> lines;
		LrcMeta meta;
		ParseLrc(lrcText, lines, &meta);
		BuildIntro(lines,
			m_trackTitle.empty() ? meta.ti : m_trackTitle,
			m_trackArtist.empty() ? meta.ar : m_trackArtist);
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
		m_trackTitle = title;
		m_trackArtist = artist;
		std::vector<std::wstring> candidates;

		// 1) 与音频同名的 .lrc
		const size_t dot = audioPath.find_last_of(L'.');
		const size_t sep = audioPath.find_last_of(L"\\/");
		if (dot != std::wstring::npos && (sep == std::wstring::npos || dot > sep))
			candidates.push_back(audioPath.substr(0, dot) + L".lrc");

		// 2) lyrics 目录 + 额外目录：标题.lrc / 歌手 - 标题.lrc / 标题 - 歌手.lrc
		std::vector<std::wstring> dirs = { LyricDir(), LegacyLyricDir() };
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

		// 3) 模糊匹配：目录里 .lrc 文件名包含歌名（同时包含歌手的优先）。
		//    在线歌曲的歌名常带 (Live)、feat. 之类的后缀，或者歌手/歌名顺序不一样，精确文件名对不上
		if (!title.empty())
		{
			std::wstring best;
			int bestScore = 0;
			for (const std::wstring& d : dirs)
			{
				WIN32_FIND_DATAW fd;
				HANDLE h = FindFirstFileW((d + L"\\*.lrc").c_str(), &fd);
				if (h == INVALID_HANDLE_VALUE)
					continue;
				do
				{
					const std::wstring fn = fd.cFileName;
					int score = 0;
					if (ContainsNoCase(fn, SafeName(title).c_str())) score += 2;
					if (score > 0 && !artist.empty() && ContainsNoCase(fn, SafeName(artist).c_str())) score += 1;
					if (score > bestScore) { bestScore = score; best = d + L"\\" + fn; }
				} while (FindNextFileW(h, &fd));
				FindClose(h);
			}
			if (!best.empty() && LoadLrcFile(best))
				return true;
		}

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
		if (positionMs < 0) positionMs = 0;
		// 外部只给整秒时：真实位置在 [s, s+1) 之间。内部插值的位置只要落在这个区间里就不用校正，
		// 否则每次都被拉回整秒，歌词会比歌声慢最多 1 秒
		if (m_playing && (positionMs % 1000) == 0)
		{
			const int est = EstimatePositionMs();
			if (est >= positionMs && est < positionMs + 1000)
				return;
		}
		m_basePosMs = positionMs;
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

	// 这一句在 posMs 时"唱到第几个字"（0 ~ 字数，带小数）。
	//  - 增强 LRC（带 <mm:ss.xx>）：按逐字时间戳，字与字之间线性插值
	//  - 普通 LRC：只有整句的起止时间，按字数把整句时长摊开（空格/标点占的时间短一些），
	//    网易云对没有逐字歌词的歌也是这样做的
	float DesktopLyric::PlayedChars(int index, int posMs) const
	{
		if (index < 0 || index >= (int)m_lines.size()) return 0.0f;
		const Line& ln = m_lines[index];
		const int n = (int)ln.text.size();
		if (n == 0) return 0.0f;

		// 这一句的结束时间：下一句开始，但不超过"按字数估的最长演唱时间"（后面是长间奏时不会把字拖得很慢）
		const int maxDur = n * 450 + 800;
		int end = (index + 1 < (int)m_lines.size()) ? m_lines[index + 1].timeMs : ln.timeMs + maxDur;
		int dur = end - ln.timeMs;
		if (dur > maxDur) dur = maxDur;
		if (dur < 200) dur = 200;
		const int lineEnd = ln.timeMs + dur;

		if (posMs <= ln.timeMs) return 0.0f;

		if (ln.marks.size() >= 2)
		{
			const auto& mk = ln.marks;
			if (posMs < mk[0].second) return 0.0f;
			int i = (int)mk.size() - 1;
			while (i > 0 && mk[i].second > posMs) i--;
			const int c0 = mk[i].first;
			const int c1 = (i + 1 < (int)mk.size()) ? mk[i + 1].first : n;
			const int t0 = mk[i].second;
			int t1 = (i + 1 < (int)mk.size()) ? mk[i + 1].second : (lineEnd > t0 + 1 ? lineEnd : t0 + 1);
			if (t1 <= t0) t1 = t0 + 1;
			float f = (float)(posMs - t0) / (float)(t1 - t0);
			if (f > 1.0f) f = 1.0f;
			return (float)c0 + f * (float)(c1 - c0);
		}

		if (posMs >= lineEnd) return (float)n;
		auto weight = [](wchar_t ch) { return (ch == L' ' || ch == L'\t' || wcschr(L",.!?;:'\"()-~\u3001\u3002\uFF0C\uFF01\uFF1F", ch)) ? 0.35f : 1.0f; };
		float total = 0.0f;
		for (wchar_t ch : ln.text) total += weight(ch);
		const float target = total * (float)(posMs - ln.timeMs) / (float)dur;
		float acc = 0.0f;
		for (int k = 0; k < n; k++)
		{
			const float w = weight(ln.text[k]);
			if (acc + w >= target)
				return (float)k + (target - acc) / w;
			acc += w;
		}
		return (float)n;
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
		m_hover = false;
		m_hotBtn = BtnNone;
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

	// 锁定 = 鼠标穿透（WS_EX_TRANSPARENT），点击会直接落到下面的窗口。
	// 唯一的例外：鼠标正压在"锁"小图标上时临时取消穿透，这样才点得到它来解锁
	void DesktopLyric::ApplyLocked()
	{
		UpdateClickThrough(m_hotBtn != BtnNone);
	}

	void DesktopLyric::UpdateClickThrough(bool overButton)
	{
		if (!m_hwnd) return;
		LONG_PTR ex = GetWindowLongPtrW(m_hwnd, GWL_EXSTYLE);
		const bool wantThrough = m_style.locked && !overButton;
		LONG_PTR want = wantThrough ? (ex | WS_EX_TRANSPARENT) : (ex & ~(LONG_PTR)WS_EX_TRANSPARENT);
		if (want != ex)
			SetWindowLongPtrW(m_hwnd, GWL_EXSTYLE, want);
	}

	// 小图标：居中排在窗口顶部那条里。解锁状态 = [锁][关闭]，锁定状态 = 只有[锁]（点它解锁）
	void DesktopLyric::LayoutButtons(int width)
	{
		const int count = m_style.locked ? 1 : 2;
		const int total = count * kBtnSize + (count - 1) * kBtnGap;
		int x = (width - total) / 2;
		const int top = ((int)kBarH - kBtnSize) / 2;
		m_btnLock = { x, top, x + kBtnSize, top + kBtnSize };
		if (!m_style.locked)
		{
			x += kBtnSize + kBtnGap;
			m_btnClose = { x, top, x + kBtnSize, top + kBtnSize };
		}
		else
			m_btnClose = { 0, 0, 0, 0 };
	}

	DesktopLyric::Button DesktopLyric::HitButton(POINT pt) const
	{
		if (!m_hover) return BtnNone;     // 小图标只在鼠标悬停时才显示，也只在那时才能点
		if (PtInRect(&m_btnLock, pt)) return BtnLock;
		if (!m_style.locked && PtInRect(&m_btnClose, pt)) return BtnClose;
		return BtnNone;
	}

	void DesktopLyric::OnButtonClick(Button b)
	{
		if (b == BtnLock)
		{
			m_style.locked = !m_style.locked;
			m_dirty = true;
			ApplyLocked();
			Render();
			if (m_onUserChanged) m_onUserChanged(IsVisible(), m_style.locked);
		}
		else if (b == BtnClose)
		{
			const bool locked = m_style.locked;
			Hide();
			if (m_onUserChanged) m_onUserChanged(false, locked);
		}
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
		const float lineH = emPx * 1.35f;
		const float padX = 14.0f, padY = 4.0f;

		// 窗口大小：只显示一行歌词。宽度取工作区 42%（最小 360、最大 900），高度 = 图标条 + 一行字
		RECT work = {};
		SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
		int w = (int)((work.right - work.left) * 0.42f);
		if (w < 360) w = 360;
		if (w > 900) w = 900;
		if (w > work.right - work.left) w = work.right - work.left;
		const int h = (int)(kBarH + lineH + padY * 2 + 2);

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
			LayoutButtons(w);
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
			std::wstring cur1;
			const int idx = CurrentIndex();
			if (m_lines.empty())
				cur1 = m_hint;
			else
			{
				if (idx >= 0) cur1 = m_lines[idx].text;
				if (cur1.empty() && idx >= 0) cur1 = L"\u266A";          // 间奏
			}

			StringFormat fmt;
			fmt.SetAlignment(StringAlignmentNear);
			fmt.SetLineAlignment(StringAlignmentNear);
			fmt.SetFormatFlags(StringFormatFlagsNoWrap | StringFormatFlagsMeasureTrailingSpaces);

			const int style = FontStyleRegular;
			const float maxW = (float)w - padX * 2;

			// 画一行：居中；太宽就等比缩小；先画描边再画填充
			// playedChars：这一句已经唱到第几个字；< 0 = 一个字都没唱（全黄），>= 1e8 = 整句都算唱过
			auto drawLine = [&](const std::wstring& text, float em, float top, bool primary, float playedChars)
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
				const float ty = top + ((lineH - b.Height) / 2.0f) - b.Y;
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

				// 先整句铺黄色（没唱到的部分）
				{
					SolidBrush yellow(Color(alpha, 255, 204, 0));    // 黄色，和播放器强调色一致
					g.FillPath(&yellow, &path);
				}

				// 再把已经唱过的部分用流动的七彩盖上去：按"唱到哪个字"算出横向位置，裁剪后再填七彩渐变
				if (m_style.colorMode == 1 && playedChars >= 0.0f && b.Width > 1.0f)
				{
					float frac = 1.0f;
					if (playedChars < 1.0e8f)
					{
						const int n = (int)text.size();
						const Font font(m_family.get(), useEm, FontStyleRegular, UnitPixel);
						StringFormat tf(StringFormat::GenericTypographic());
						tf.SetFormatFlags(tf.GetFormatFlags() | StringFormatFlagsMeasureTrailingSpaces | StringFormatFlagsNoWrap);
						auto adv = [&](int count) -> float
						{
							if (count <= 0) return 0.0f;
							RectF r;
							g.MeasureString(text.c_str(), count, &font, PointF(0, 0), &tf, &r);
							return r.Width;
						};
						const float whole = adv(n);
						if (whole > 1.0f)
						{
							int k = (int)playedChars;
							if (k > n) k = n;
							const float fr = playedChars - (float)k;
							float x = adv(k);
							if (k < n && fr > 0.0f) x += fr * (adv(k + 1) - x);
							frac = x / whole;
						}
						if (frac < 0.0f) frac = 0.0f;
						if (frac > 1.0f) frac = 1.0f;
					}

					if (frac > 0.0f)
					{
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
						// 裁剪到"已唱"的宽度（frac=1 时留一点余量，免得最后一个字的笔画被切掉）
						const float clipR = (frac >= 0.999f) ? (float)w : b.X + frac * b.Width;
						g.SetClip(RectF(0.0f, 0.0f, clipR, (REAL)h), CombineModeReplace);
						g.FillPath(&br, &path);
						g.ResetClip();
					}
				}
			};

			// 当前句唱到哪儿了：没有歌词（提示文字）或间奏时整句直接七彩
			float played = -1.0f;
			if (m_style.colorMode == 1)
			{
				if (m_lines.empty() || (idx >= 0 && m_lines[idx].text.empty()))
					played = 1.0e9f;
				else if (idx >= 0)
					played = PlayedChars(idx, EstimatePositionMs());
			}
			drawLine(cur1, emPx, kBarH + padY, true, played);

			// 悬停时在顶部画 锁定 / 关闭 小图标（像网易云、QQ音乐的桌面歌词）
			if (m_hover)
			{
				auto drawBtn = [&](const RECT& r, Button id)
				{
					const bool hot = (m_hotBtn == id);
					const float cx = (r.left + r.right) / 2.0f, cy = (r.top + r.bottom) / 2.0f;
					SolidBrush bgBrush(hot ? Color(235, 255, 204, 0) : Color(150, 20, 20, 20));
					g.FillEllipse(&bgBrush, (REAL)r.left, (REAL)r.top, (REAL)(r.right - r.left), (REAL)(r.bottom - r.top));
					const Color fg = hot ? Color(255, 30, 30, 30) : Color(240, 255, 255, 255);
					Pen pen(fg, 1.8f);
					pen.SetStartCap(LineCapRound);
					pen.SetEndCap(LineCapRound);
					SolidBrush fgBrush(fg);
					if (id == BtnClose)
					{
						const float d = 4.5f;
						g.DrawLine(&pen, cx - d, cy - d, cx + d, cy + d);
						g.DrawLine(&pen, cx - d, cy + d, cx + d, cy - d);
					}
					else
					{
						// 锁：下面一个实心小方块，上面一个半圆锁环；解锁状态锁环抬起、左脚悬空
						g.FillRectangle(&fgBrush, cx - 5.0f, cy - 0.5f, 10.0f, 7.5f);
						if (m_style.locked)
						{
							g.DrawArc(&pen, cx - 3.5f, cy - 6.5f, 7.0f, 7.0f, 180.0f, 180.0f);
							g.DrawLine(&pen, cx - 3.5f, cy - 3.0f, cx - 3.5f, cy - 0.5f);
							g.DrawLine(&pen, cx + 3.5f, cy - 3.0f, cx + 3.5f, cy - 0.5f);
						}
						else
						{
							g.DrawArc(&pen, cx - 3.5f, cy - 9.5f, 7.0f, 7.0f, 180.0f, 180.0f);
							g.DrawLine(&pen, cx + 3.5f, cy - 6.0f, cx + 3.5f, cy - 0.5f);
						}
					}
				};
				drawBtn(m_btnLock, BtnLock);
				if (!m_style.locked)
					drawBtn(m_btnClose, BtnClose);
			}
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
		{
			// 小图标上算客户区（能点击）；其它地方整个窗口当作标题栏，按住就能拖动。
			// 锁定时除了锁图标，其它地方鼠标穿透（带 WS_EX_TRANSPARENT，见 UpdateClickThrough）
			RECT wr;
			GetWindowRect(hwnd, &wr);
			POINT c = { GET_X_LPARAM(lParam) - wr.left, GET_Y_LPARAM(lParam) - wr.top };
			if (HitButton(c) != BtnNone)
				return HTCLIENT;
			return m_style.locked ? HTTRANSPARENT : HTCAPTION;
		}

		case WM_SETCURSOR:
			if (LOWORD(lParam) == HTCLIENT)
			{
				SetCursor(LoadCursorW(nullptr, IDC_HAND));
				return TRUE;
			}
			break;

		case WM_MOUSEACTIVATE:
			return MA_NOACTIVATE;

		case WM_LBUTTONUP:
		{
			POINT c = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
			const Button b = HitButton(c);
			if (b != BtnNone)
				OnButtonClick(b);
			return 0;
		}

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

				// 悬停：鼠标在窗口范围内就显示小图标（锁定时也一样，这样才能解锁）
				{
					POINT pt; GetCursorPos(&pt);
					RECT r; GetWindowRect(hwnd, &r);
					const bool hover = PtInRect(&r, pt) != FALSE;
					if (hover != m_hover) { m_hover = hover; redraw = true; }
					Button hot = BtnNone;
					if (hover)
					{
						POINT cp = { pt.x - r.left, pt.y - r.top };
						hot = HitButton(cp);
					}
					if (hot != m_hotBtn) { m_hotBtn = hot; redraw = true; }
					UpdateClickThrough(hot != BtnNone);
				}

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
