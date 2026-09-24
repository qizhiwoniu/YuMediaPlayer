#include "Core/Playlist.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <algorithm>
#include <cwctype>
#include <utility>

namespace YuMediaPlayer
{
	namespace
	{
		// 取当前 exe 所在目录（末尾带 \）。逻辑跟 AudioPlayer.cpp / CircularAvatar.cpp
		// 里的 GetExeDir 一样：不依赖"当前工作目录"。
		std::wstring GetExeDir()
		{
			wchar_t buf[MAX_PATH] = {};
			DWORD len = GetModuleFileNameW(nullptr, buf, MAX_PATH);
			if (len == 0 || len == MAX_PATH)
				return L"";

			std::wstring exePath(buf, len);
			size_t pos = exePath.find_last_of(L"\\/");
			if (pos == std::wstring::npos)
				return L"";

			return exePath.substr(0, pos + 1);
		}

		bool FileExists(const std::wstring& path)
		{
			DWORD attr = GetFileAttributesW(path.c_str());
			return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
		}

		bool DirExists(const std::wstring& path)
		{
			DWORD attr = GetFileAttributesW(path.c_str());
			return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY);
		}

		std::wstring EnsureTrailingSlash(std::wstring dir)
		{
			if (!dir.empty() && dir.back() != L'\\' && dir.back() != L'/')
				dir += L'\\';
			return dir;
		}

		// 相对路径 -> 绝对路径（相对于当前工作目录）。失败就原样返回。
		std::wstring ToFullPath(const std::wstring& path)
		{
			wchar_t buf[MAX_PATH] = {};
			DWORD n = GetFullPathNameW(path.c_str(), MAX_PATH, buf, nullptr);
			if (n == 0 || n >= MAX_PATH)
				return path;
			return std::wstring(buf, n);
		}

		std::wstring ToLower(std::wstring s)
		{
			for (wchar_t& c : s)
				c = static_cast<wchar_t>(std::towlower(c));
			return s;
		}

		std::wstring Trim(const std::wstring& s)
		{
			size_t b = 0, e = s.size();
			while (b < e && std::iswspace(s[b])) ++b;
			while (e > b && std::iswspace(s[e - 1])) --e;
			return s.substr(b, e - b);
		}

		// 封面图支持的扩展名（GDI+ 原生支持），按优先级排列。
		const wchar_t* const kCoverExts[] = { L".png", L".jpg", L".jpeg", L".bmp" };
	}

	// ---------------------------------------------------------------
	// 纯字符串逻辑
	// ---------------------------------------------------------------

	bool Playlist::IsAudioExtension(const std::wstring& extWithDot)
	{
		const std::wstring ext = ToLower(extWithDot);
		return ext == L".mp3" || ext == L".wav" || ext == L".m4a"
			|| ext == L".wma" || ext == L".flac" || ext == L".aac";
	}

	void Playlist::ParseTitleArtist(const std::wstring& fileStem, std::wstring& title, std::wstring& artist)
	{
		// 约定：文件名是 "歌手-歌名"。按第一个 '-'（半角或全角）切开。
		// 歌名里自己带 '-' 没关系（"周杰伦-七里香-Live" -> 歌手=周杰伦，歌名=七里香-Live）。
		size_t pos = fileStem.find_first_of(L"-\uFF0D");
		if (pos != std::wstring::npos)
		{
			std::wstring left = Trim(fileStem.substr(0, pos));
			std::wstring right = Trim(fileStem.substr(pos + 1));
			if (!left.empty() && !right.empty())
			{
				artist = left;
				title = right;
				return;
			}
		}

		// 没有 '-'，或者 '-' 在开头/结尾：整个文件名当歌名，歌手留空。
		artist.clear();
		title = Trim(fileStem);
	}

	// ---------------------------------------------------------------
	// 加载
	// ---------------------------------------------------------------

	int Playlist::LoadDefault()
	{
		const std::wstring exeDir = GetExeDir();

		// 跟 AudioPlayer::ResolveExistingFilePath 保持同样的查找顺序：
		// 先当前工作目录，再 exe 所在目录。
		const std::wstring audioCandidates[] = {
			ToFullPath(L"song\\local"),
			exeDir + L"song\\local",
		};

		for (const std::wstring& audioDir : audioCandidates)
		{
			if (!DirExists(audioDir))
				continue;

			// 封面除了跟歌放在同一个目录，也会去这几个常见位置找
			// （CircularAvatar 之前默认封面就是放在 exe 旁边的 disk / Assets\disk 里）。
			const std::vector<std::wstring> coverDirs = {
				exeDir + L"Assets\\disk",
				exeDir + L"disk",
				exeDir + L"Assets",
				ToFullPath(L"Assets\\disk"),
			};

			if (LoadFromDirectory(audioDir, coverDirs) > 0)
				return Count();
		}

		m_tracks.clear();
		m_current = 0;
		m_history.clear();
		return 0;
	}

	int Playlist::LoadFromDirectory(const std::wstring& audioDir, const std::vector<std::wstring>& coverDirs)
	{
		m_tracks.clear();
		m_history.clear();
		m_current = 0;

		if (m_rngState == 0)
			m_rngState = (GetTickCount() ^ (GetCurrentProcessId() << 16)) | 1u;

		const std::wstring dir = EnsureTrailingSlash(audioDir);

		WIN32_FIND_DATAW fd = {};
		HANDLE hFind = FindFirstFileW((dir + L"*").c_str(), &fd);
		if (hFind == INVALID_HANDLE_VALUE)
			return 0;

		do
		{
			if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
				continue;

			const std::wstring fileName = fd.cFileName;
			const size_t dot = fileName.find_last_of(L'.');
			if (dot == std::wstring::npos)
				continue;
			if (!IsAudioExtension(fileName.substr(dot)))
				continue;

			Track track;
			track.audioPath = dir + fileName;

			const std::wstring stem = fileName.substr(0, dot);
			ParseTitleArtist(stem, track.title, track.artist);

			// 找封面：同名图片，先看歌所在目录，再看 coverDirs。
			std::vector<std::wstring> searchDirs;
			searchDirs.push_back(dir);
			for (const std::wstring& d : coverDirs)
				searchDirs.push_back(EnsureTrailingSlash(d));

			for (const std::wstring& d : searchDirs)
			{
				for (const wchar_t* ext : kCoverExts)
				{
					const std::wstring candidate = d + stem + ext;
					if (FileExists(candidate))
					{
						track.coverPath = candidate;
						break;
					}
				}
				if (!track.coverPath.empty())
					break;
			}

			m_tracks.push_back(std::move(track));
		} while (FindNextFileW(hFind, &fd));

		FindClose(hFind);

		std::sort(m_tracks.begin(), m_tracks.end(),
			[](const Track& a, const Track& b)
			{
				return lstrcmpiW(a.audioPath.c_str(), b.audioPath.c_str()) < 0;
			});

		return Count();
	}

	// ---------------------------------------------------------------
	// 当前曲目 / 上一首 / 下一首
	// ---------------------------------------------------------------

	const Track* Playlist::Current() const
	{
		if (m_current < 0 || m_current >= Count())
			return nullptr;
		return &m_tracks[m_current];
	}

	const Track* Playlist::At(int index) const
	{
		if (index < 0 || index >= Count())
			return nullptr;
		return &m_tracks[index];
	}

	bool Playlist::SetCurrent(int index)
	{
		if (index < 0 || index >= Count())
			return false;
		m_current = index;
		return true;
	}

	int Playlist::RandomBelow(int n)
	{
		// xorshift32
		unsigned int x = m_rngState;
		x ^= x << 13;
		x ^= x >> 17;
		x ^= x << 5;
		m_rngState = x;
		return static_cast<int>(x % static_cast<unsigned int>(n));
	}

	int Playlist::MoveNext(bool shuffle)
	{
		const int n = Count();
		if (n == 0)
			return -1;
		if (n == 1)
			return m_current;

		if (shuffle)
		{
			m_history.push_back(m_current);
			if (m_history.size() > 100)
				m_history.erase(m_history.begin());

			// 从"除了当前这首以外的 n-1 首"里随机挑，保证不会连着播同一首。
			int r = RandomBelow(n - 1);
			if (r >= m_current)
				++r;
			m_current = r;
		}
		else
		{
			m_current = (m_current + 1) % n;
		}
		return m_current;
	}

	int Playlist::MovePrevious(bool shuffle)
	{
		const int n = Count();
		if (n == 0)
			return -1;
		if (n == 1)
			return m_current;

		if (shuffle && !m_history.empty())
		{
			m_current = m_history.back();
			m_history.pop_back();
		}
		else
		{
			m_current = (m_current - 1 + n) % n;
		}
		return m_current;
	}
}
