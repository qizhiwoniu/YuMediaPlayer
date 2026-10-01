#include "LocalMusicScanner.h"
#include <shobjidl.h>
#include <propsys.h>
#include <algorithm>
#include <memory>
#include <set>
#include <wchar.h>

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "propsys.lib")

namespace YuMediaPlayer
{
	namespace
	{
		const UINT kMsgScanDone = WM_APP + 21;
		const wchar_t kWndClass[] = L"YuMediaPlayerLocalScanWnd";

		// 要扫描的音频格式（想支持更多格式加在这里；注意要和播放器实际能播放的格式一致）
		// 和 Playlist::IsAudioExtension / AudioPlayer（Media Foundation）实际能播的格式保持一致，
		// 否则列表里会出现点了没声音的歌（ogg/opus/ape 等 MF 默认不支持）。
		const wchar_t* kAudioExts[] = {
			L".mp3", L".flac", L".m4a", L".aac", L".wma"
		};

		// Windows 属性系统的几个键（自己定义，免得链接 PKEY_* 的符号）
		const PROPERTYKEY kKeyTitle = { { 0xF29F85E0, 0x4FF9, 0x1068, { 0xAB, 0x91, 0x08, 0x00, 0x2B, 0x27, 0xB3, 0xD9 } }, 2 };   // System.Title
		const PROPERTYKEY kKeyArtist = { { 0x56A3372E, 0xCE9C, 0x11D2, { 0x9F, 0x0E, 0x00, 0x60, 0x97, 0xC6, 0x86, 0xF6 } }, 2 };  // System.Music.Artist
		const PROPERTYKEY kKeyAlbum = { { 0x56A3372E, 0xCE9C, 0x11D2, { 0x9F, 0x0E, 0x00, 0x60, 0x97, 0xC6, 0x86, 0xF6 } }, 4 };   // System.Music.AlbumTitle
		const PROPERTYKEY kKeyDuration = { { 0x64440490, 0x4C8B, 0x11D1, { 0x8B, 0x70, 0x08, 0x00, 0x36, 0xB1, 0x1A, 0x03 } }, 3 }; // System.Media.Duration（100ns）

		struct ScanResult
		{
			int generation = 0;
			std::vector<TrackItem> tracks;
		};

		struct FileEntry
		{
			std::wstring path;
			ULONGLONG size = 0;
		};

		struct ComScope
		{
			HRESULT hr;
			ComScope() : hr(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)) {}
			~ComScope() { if (SUCCEEDED(hr)) CoUninitialize(); }
		};

		bool IsAudioFile(const wchar_t* name)
		{
			const wchar_t* dot = wcsrchr(name, L'.');
			if (!dot)
				return false;
			for (const wchar_t* ext : kAudioExts)
				if (_wcsicmp(dot, ext) == 0)
					return true;
			return false;
		}

		std::wstring Lower(std::wstring s)
		{
			if (!s.empty())
				CharLowerBuffW(&s[0], (DWORD)s.size());
			return s;
		}

		std::wstring Trim(const std::wstring& s)
		{
			size_t b = 0, e = s.size();
			while (b < e && iswspace(s[b])) b++;
			while (e > b && iswspace(s[e - 1])) e--;
			return s.substr(b, e - b);
		}

		std::wstring FullPath(const std::wstring& p)
		{
			wchar_t buf[MAX_PATH * 4] = { 0 };
			DWORD n = GetFullPathNameW(p.c_str(), (DWORD)(sizeof(buf) / sizeof(buf[0])), buf, nullptr);
			if (n == 0 || n >= sizeof(buf) / sizeof(buf[0]))
				return p;
			return std::wstring(buf, n);
		}

		// 递归收集音频文件
		void CollectFiles(const std::wstring& dir, int depth, std::vector<FileEntry>& out,
			const std::function<bool()>& cancelled)
		{
			if (depth > 32)
				return;

			WIN32_FIND_DATAW fd;
			HANDLE h = FindFirstFileExW((dir + L"\\*").c_str(), FindExInfoBasic, &fd,
				FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);
			if (h == INVALID_HANDLE_VALUE)
				return;

			do
			{
				if (cancelled && cancelled())
					break;
				const wchar_t* name = fd.cFileName;
				if (name[0] == L'.' && (name[1] == 0 || (name[1] == L'.' && name[2] == 0)))
					continue;

				const std::wstring full = dir + L"\\" + name;
				if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
				{
					// 跳过符号链接/联接点（防止死循环）和系统目录（回收站、System Volume Information）
					if (fd.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_SYSTEM))
						continue;
					CollectFiles(full, depth + 1, out, cancelled);
				}
				else if (IsAudioFile(name))
				{
					FileEntry fe;
					fe.path = full;
					fe.size = ((ULONGLONG)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
					if (fe.size > 0)
						out.push_back(std::move(fe));
				}
			} while (FindNextFileW(h, &fd));
			FindClose(h);
		}

		std::wstring PropToString(const PROPVARIANT& pv)
		{
			if (pv.vt == VT_LPWSTR && pv.pwszVal)
				return pv.pwszVal;
			if (pv.vt == VT_BSTR && pv.bstrVal)
				return pv.bstrVal;
			if (pv.vt == (VT_VECTOR | VT_LPWSTR))      // 歌手可能有多个
			{
				std::wstring s;
				for (ULONG i = 0; i < pv.calpwstr.cElems; i++)
				{
					if (!pv.calpwstr.pElems[i])
						continue;
					if (!s.empty())
						s += L"/";
					s += pv.calpwstr.pElems[i];
				}
				return s;
			}
			return std::wstring();
		}

		// 用 Windows 属性系统读标签（系统自带的解析器，mp3/flac/m4a/wma 等都支持，不需要第三方库）
		void ReadTags(const std::wstring& path, std::wstring& title, std::wstring& artist,
			std::wstring& album, int& seconds)
		{
			IPropertyStore* ps = nullptr;
			HRESULT hr = SHGetPropertyStoreFromParsingName(path.c_str(), nullptr,
				GPS_DEFAULT | GPS_BESTEFFORT, IID_PPV_ARGS(&ps));
			if (FAILED(hr) || !ps)
				return;

			PROPVARIANT pv;
			PropVariantInit(&pv);
			if (SUCCEEDED(ps->GetValue(kKeyTitle, &pv)))
				title = Trim(PropToString(pv));
			PropVariantClear(&pv);
			if (SUCCEEDED(ps->GetValue(kKeyArtist, &pv)))
				artist = Trim(PropToString(pv));
			PropVariantClear(&pv);
			if (SUCCEEDED(ps->GetValue(kKeyAlbum, &pv)))
				album = Trim(PropToString(pv));
			PropVariantClear(&pv);
			if (SUCCEEDED(ps->GetValue(kKeyDuration, &pv)) && pv.vt == VT_UI8)
				seconds = (int)((pv.uhVal.QuadPart + 5000000ULL) / 10000000ULL);
			PropVariantClear(&pv);
			ps->Release();
		}

		// 标签里没有标题时用文件名：支持 "歌手 - 歌名.mp3"
		void TitleFromFileName(const std::wstring& path, std::wstring& title, std::wstring& artist)
		{
			size_t slash = path.find_last_of(L"\\/");
			std::wstring name = (slash == std::wstring::npos) ? path : path.substr(slash + 1);
			size_t dot = name.find_last_of(L'.');
			if (dot != std::wstring::npos)
				name = name.substr(0, dot);

			size_t sep = name.find(L" - ");
			if (artist.empty() && sep != std::wstring::npos && sep > 0)
			{
				artist = Trim(name.substr(0, sep));
				title = Trim(name.substr(sep + 3));
			}
			else
			{
				title = Trim(name);
			}
			if (title.empty())
				title = name;
		}

		int CompareText(const std::wstring& a, const std::wstring& b)
		{
			return CompareStringW(LOCALE_USER_DEFAULT, NORM_IGNORECASE | SORT_DIGITSASNUMBERS,
				a.c_str(), (int)a.size(), b.c_str(), (int)b.size()) - CSTR_EQUAL;
		}
	}

	// ===================== 同步扫描 =====================
	std::vector<TrackItem> LocalMusicScanner::ScanFolder(const std::wstring& folder,
		const std::function<bool()>& cancelled)
	{
		std::vector<TrackItem> result;
		if (folder.empty())
			return result;

		std::wstring root = FullPath(folder);
		while (root.size() > 3 && (root.back() == L'\\' || root.back() == L'/'))
			root.pop_back();
		DWORD attr = GetFileAttributesW(root.c_str());
		if (attr == INVALID_FILE_ATTRIBUTES || !(attr & FILE_ATTRIBUTE_DIRECTORY))
			return result;

		ComScope com;

		std::vector<FileEntry> files;
		CollectFiles(root, 0, files, cancelled);
		if (cancelled && cancelled())
			return result;

		// 去重 1：同一个文件（规范化路径、不区分大小写）只留一份；顺便按路径排序，保证"留哪一份"是确定的
		std::vector<std::pair<std::wstring, FileEntry>> keyed;
		keyed.reserve(files.size());
		for (FileEntry& f : files)
			keyed.emplace_back(Lower(FullPath(f.path)), std::move(f));
		std::sort(keyed.begin(), keyed.end(),
			[](const std::pair<std::wstring, FileEntry>& a, const std::pair<std::wstring, FileEntry>& b)
			{ return a.first < b.first; });
		keyed.erase(std::unique(keyed.begin(), keyed.end(),
			[](const std::pair<std::wstring, FileEntry>& a, const std::pair<std::wstring, FileEntry>& b)
			{ return a.first == b.first; }), keyed.end());

		// 去重 2：不同位置的同一首歌（标题+歌手+时长相同；读不到时长就用文件大小）只留第一份
		std::set<std::wstring> seen;
		result.reserve(keyed.size());
		for (const auto& kv : keyed)
		{
			if (cancelled && cancelled())
				return std::vector<TrackItem>();

			TrackItem t;
			t.path = kv.second.path;
			std::wstring title, artist, album;
			int seconds = 0;
			ReadTags(t.path, title, artist, album, seconds);
			if (title.empty())
				TitleFromFileName(t.path, title, artist);
			t.title = title;
			t.artist = artist;
			t.album = album;
			t.durationSeconds = seconds;

			std::wstring key = Lower(t.title) + L"\x1f" + Lower(t.artist) + L"\x1f";
			key += (seconds > 0) ? std::to_wstring(seconds) : (L"s" + std::to_wstring(kv.second.size));
			if (!seen.insert(key).second)
				continue;
			result.push_back(std::move(t));
		}

		std::sort(result.begin(), result.end(), [](const TrackItem& a, const TrackItem& b)
			{
				int c = CompareText(a.title, b.title);
				if (c != 0)
					return c < 0;
				return CompareText(a.artist, b.artist) < 0;
			});
		return result;
	}

	// ===================== 异步包装 =====================
	LocalMusicScanner::LocalMusicScanner()
	{
		static bool registered = false;
		if (!registered)
		{
			WNDCLASSEXW wc = {};
			wc.cbSize = sizeof(wc);
			wc.lpfnWndProc = WndProc;
			wc.hInstance = GetModuleHandleW(nullptr);
			wc.lpszClassName = kWndClass;
			if (RegisterClassExW(&wc))
				registered = true;
		}
		// 消息专用窗口：工作线程用 PostMessage 把结果交回创建它的线程（UI 线程）
		m_hwnd = CreateWindowExW(0, kWndClass, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr,
			GetModuleHandleW(nullptr), this);
	}

	LocalMusicScanner::~LocalMusicScanner()
	{
		StopWorker();
		if (m_hwnd)
		{
			DestroyWindow(m_hwnd);
			m_hwnd = nullptr;
		}
	}

	void LocalMusicScanner::SetResultCallback(std::function<void(std::vector<TrackItem>)> callback)
	{
		m_onResult = std::move(callback);
	}

	void LocalMusicScanner::StopWorker()
	{
		m_cancel = true;
		if (m_thread.joinable())
			m_thread.join();
	}

	void LocalMusicScanner::Cancel()
	{
		StopWorker();
		++m_generation;       // 已经在路上的结果也作废
		m_started = false;
	}

	void LocalMusicScanner::Start(const std::wstring& folder, bool force)
	{
		if (!m_hwnd)
			return;
		if (!force && m_started && _wcsicmp(folder.c_str(), m_folder.c_str()) == 0)
			return;

		StopWorker();
		m_started = true;
		m_folder = folder;
		m_cancel = false;
		const int gen = ++m_generation;
		const HWND hwnd = m_hwnd;
		const std::wstring f = folder;

		m_thread = std::thread([this, f, gen, hwnd]()
			{
				std::unique_ptr<ScanResult> r(new ScanResult());
				r->generation = gen;
				r->tracks = ScanFolder(f, [this]() { return m_cancel.load(); });
				if (!m_cancel && PostMessageW(hwnd, kMsgScanDone, 0, reinterpret_cast<LPARAM>(r.get())))
					r.release();      // 所有权交给消息处理
			});
	}

	LRESULT CALLBACK LocalMusicScanner::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
	{
		if (msg == WM_NCCREATE)
		{
			auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
			SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
		}
		else if (msg == kMsgScanDone)
		{
			std::unique_ptr<ScanResult> r(reinterpret_cast<ScanResult*>(lParam));
			auto* self = reinterpret_cast<LocalMusicScanner*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
			if (self && r && r->generation == self->m_generation && self->m_onResult)
				self->m_onResult(std::move(r->tracks));
			return 0;
		}
		return DefWindowProcW(hwnd, msg, wParam, lParam);
	}
}
