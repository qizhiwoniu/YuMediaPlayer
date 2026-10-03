#include <windows.h>
#include <winhttp.h>
#include <wincrypt.h>
#include <string>
#include <vector>
#include "OnlineMusic.h"
#include "MiniJson.h"

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "advapi32.lib")


namespace YuMediaPlayer
{
	namespace OnlineMusic
	{
		namespace
		{
			static int g_source = SourceKugou;
			static std::wstring g_subUrl;
			static std::wstring g_subUser;
			static std::wstring g_subPass;

			std::string ToUtf8(const std::wstring& w)
			{
				if (w.empty()) return std::string();
				int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
				std::string s(n, '\0');
				WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, nullptr, nullptr);
				return s;
			}
			std::wstring FromUtf8(const std::string& s)
			{
				if (s.empty()) return std::wstring();
				int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
				std::wstring w(n, L'\0');
				MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
				return w;
			}
			// URL 参数编码：UTF-8 逐字节 %XX
			std::wstring UrlEncode(const std::wstring& text)
			{
				static const wchar_t* hex = L"0123456789ABCDEF";
				std::wstring out;
				for (unsigned char c : ToUtf8(text))
				{
					if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '-' || c == '_' || c == '.' || c == '~')
						out += (wchar_t)c;
					else
					{
						out += L'%';
						out += hex[c >> 4];
						out += hex[c & 15];
					}
				}
				return out;
			}

			// JSON 字段可能是字符串，也可能是数字，统一成字符串
			std::string GetIdString(const MiniJson::Value& obj, const char* key)
			{
				const MiniJson::Value* v = obj.Get(key);
				if (!v) return std::string();
				if (v->type == MiniJson::Value::String) return v->str;
				if (v->type == MiniJson::Value::Number) return std::to_string((long long)v->num);
				return std::string();
			}
			bool EndsWithNoCase(const std::string& s, const char* suffix)
			{
				size_t n = 0;
				while (suffix[n]) n++;
				if (s.size() < n) return false;
				for (size_t i = 0; i < n; i++)
				{
					char a = s[s.size() - n + i], b = suffix[i];
					if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
					if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
					if (a != b) return false;
				}
				return true;
			}

			// 通用 HTTP GET。sink 每收到一块数据就调用一次，返回 false 表示写入失败。
			template <class Sink>
			bool HttpGet(const std::wstring& url, Sink sink, std::wstring* error)
			{
				auto fail = [&](const wchar_t* msg) { if (error) *error = msg; return false; };

				URL_COMPONENTSW uc = {};
				uc.dwStructSize = sizeof(uc);
				uc.dwSchemeLength = (DWORD)-1;
				uc.dwHostNameLength = (DWORD)-1;
				uc.dwUrlPathLength = (DWORD)-1;
				uc.dwExtraInfoLength = (DWORD)-1;
				if (!WinHttpCrackUrl(url.c_str(), 0, 0, &uc))
					return fail(L"URL invalid");

				std::wstring host(uc.lpszHostName, uc.dwHostNameLength);
				std::wstring path(uc.lpszUrlPath, uc.dwUrlPathLength);
				if (uc.dwExtraInfoLength)
					path.append(uc.lpszExtraInfo, uc.dwExtraInfoLength);
				const bool https = (uc.nScheme == INTERNET_SCHEME_HTTPS);

				HINTERNET hSession = WinHttpOpen(L"YuMediaPlayer/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
					WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
				if (!hSession)   // 老系统不支持自动代理，退回系统默认代理
					hSession = WinHttpOpen(L"YuMediaPlayer/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
						WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
				if (!hSession) return fail(L"WinHttpOpen failed");
				WinHttpSetTimeouts(hSession, 5000, 5000, 10000, 15000);   // 解析/连接/发送/接收（毫秒）

				bool ok = false;
				HINTERNET hConnect = WinHttpConnect(hSession, host.c_str(), uc.nPort, 0);
				HINTERNET hRequest = hConnect ? WinHttpOpenRequest(hConnect, L"GET", path.c_str(), nullptr,
					WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, https ? WINHTTP_FLAG_SECURE : 0) : nullptr;

				if (hRequest
					&& WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0)
					&& WinHttpReceiveResponse(hRequest, nullptr))
				{
					DWORD status = 0, sz = sizeof(status);
					WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
						WINHTTP_HEADER_NAME_BY_INDEX, &status, &sz, WINHTTP_NO_HEADER_INDEX);
					if (status == 200)
					{
						ok = true;
						std::vector<char> buf(16 * 1024);
						for (;;)
						{
							DWORD got = 0;
							if (!WinHttpReadData(hRequest, buf.data(), (DWORD)buf.size(), &got)) { ok = false; break; }
							if (got == 0) break;
							if (!sink(buf.data(), got)) { ok = false; break; }
						}
						if (!ok && error) *error = L"read failed";
					}
					else if (error) *error = L"HTTP " + std::to_wstring(status);
				}
				else if (error) *error = L"request failed (network?)";

				if (hRequest) WinHttpCloseHandle(hRequest);
				if (hConnect) WinHttpCloseHandle(hConnect);
				WinHttpCloseHandle(hSession);
				return ok;
			}

			struct CategoryKeyword_ { const wchar_t* name; const wchar_t* keyword; };
			const CategoryKeyword_ kCategoryTable[] = {
				{ L"推荐",   L"周杰伦|陈奕迅|林俊杰|邓紫棋|薛之谦" },
				{ L"流行",   L"抖音热歌|流行|热歌" },
				{ L"国语",   L"华语经典|国语|经典老歌" },
				{ L"欧美",   L"Taylor Swift|Ed Sheeran|Adele|Billie Eilish" },
				{ L"日韩",   L"米津玄师|YOASOBI|BTS|IU" },
				{ L"民谣",   L"赵雷|宋冬野|房东的猫" },
				{ L"摇滚",   L"五月天|Beyond|汪峰" },
				{ L"电子",   L"电子音乐|EDM|Alan Walker" },
				{ L"古典",   L"古典音乐|钢琴曲|班得瑞" },
				{ L"纯音乐", L"纯音乐|轻音乐|钢琴曲" },
			};

			std::vector<std::wstring> SplitKeywords(const std::wstring& s)
			{
				std::vector<std::wstring> v;
				size_t start = 0;
				while (start <= s.size())
				{
					size_t bar = s.find(L'|', start);
					if (bar == std::wstring::npos) bar = s.size();
					std::wstring one = s.substr(start, bar - start);
					size_t b = one.find_first_not_of(L" \t");
					size_t e = one.find_last_not_of(L" \t");
					if (b != std::wstring::npos) v.push_back(one.substr(b, e - b + 1));
					start = bar + 1;
				}
				return v;
			}

			// 酷狗搜索：单个关键词。接口：mobilecdn.kugou.com/api/v3/search/song
			bool SearchKugouOne(const std::wstring& keyword, int pagesize, std::vector<TrackItem>& out, std::wstring* error)
			{
				std::wstring url = L"http://mobilecdn.kugou.com/api/v3/search/song?format=json&page=1&pagesize="
					+ std::to_wstring(pagesize) + L"&keyword=" + UrlEncode(keyword);

				std::string body;
				if (!HttpGet(url, [&](const char* p, DWORD n) { body.append(p, n); return body.size() < 8u * 1024 * 1024; }, error))
					return false;

				MiniJson::Value root;
				if (!MiniJson::Parse(body, root))
				{
					if (error) *error = L"JSON parse failed";
					return false;
				}
				const MiniJson::Value* data = root.Get("data");
				const MiniJson::Value* info = data ? data->Get("info") : nullptr;
				if (!info || info->type != MiniJson::Value::Array)
				{
					if (error) *error = L"unexpected response";
					return false;
				}

				for (const MiniJson::Value& it : info->arr)
				{
					std::string title = it.GetString("songname");
					std::string artist = it.GetString("singername");
					std::string fileName = it.GetString("filename");   // 形如 "歌手 - 歌名"
					if (title.empty() || artist.empty())
					{
						size_t sep = fileName.find(" - ");
						if (sep != std::string::npos)
						{
							if (artist.empty()) artist = fileName.substr(0, sep);
							if (title.empty())  title = fileName.substr(sep + 3);
						}
						else if (title.empty())
							title = fileName;
					}
					if (title.empty()) continue;

					std::string hash = it.GetString("hash");
					TrackItem t;
					t.title = FromUtf8(title);
					t.artist = FromUtf8(artist);
					t.album = FromUtf8(it.GetString("album_name"));
					t.durationSeconds = (int)it.GetNumber("duration");
					t.path = L"kugou:" + FromUtf8(hash);
					out.push_back(std::move(t));
				}
				return true;
			}


			// ---------------- 通用：取 JSON ----------------
			bool FetchJson(const std::wstring& url, MiniJson::Value& root, std::wstring* error)
			{
				std::string body;
				if (!HttpGet(url, [&](const char* p, DWORD n) { body.append(p, n); return body.size() < 8u * 1024 * 1024; }, error))
					return false;
				if (!MiniJson::Parse(body, root))
				{
					if (error) *error = L"JSON parse failed";
					return false;
				}
				return true;
			}

			// 分类表查询：没在表里的分类，直接拿分类名去搜
			template <size_t N>
			std::wstring LookupCategory(const CategoryKeyword_(&table)[N], const std::wstring& category)
			{
				for (const auto& e : table)
					if (category == e.name) return e.keyword;
				return category;
			}

			// ---------------- iTunes Search ----------------
			bool SearchItunesOne(const std::wstring& keyword, int pagesize, std::vector<TrackItem>& out, std::wstring* error)
			{
				const wchar_t* kCountry[] = { L"&country=CN", L"" };
				int rawCount = 0;
				for (const wchar_t* country : kCountry)
				{
					std::wstring url = L"https://itunes.apple.com/search?media=music&entity=song&limit="
						+ std::to_wstring(pagesize) + country + L"&term=" + UrlEncode(keyword);
					MiniJson::Value root;
					if (!FetchJson(url, root, error))
						return false;
					const MiniJson::Value* results = root.Get("results");
					if (!results || results->type != MiniJson::Value::Array)
					{
						if (error) *error = L"iTunes 返回了无法识别的内容";
						return false;
					}
					rawCount += (int)results->arr.size();
					const size_t before = out.size();
					for (const MiniJson::Value& it : results->arr)
					{
						std::string name = it.GetString("trackName");
						std::string preview = it.GetString("previewUrl");
						if (name.empty() || preview.empty()) continue;
						TrackItem t;
						t.title = FromUtf8(name);
						t.artist = FromUtf8(it.GetString("artistName"));
						t.album = FromUtf8(it.GetString("collectionName"));
						t.durationSeconds = (int)(it.GetNumber("trackTimeMillis") / 1000.0);
						t.path = L"itunes:" + FromUtf8(GetIdString(it, "trackId"));
						t.streamUrl = FromUtf8(preview);
						out.push_back(std::move(t));
					}
					if (out.size() > before)
						return true;
				}
				if (error)
					*error = L"iTunes 请求成功但没有可用歌曲（接口返回 " + std::to_wstring(rawCount) + L" 条，可能被网络拦截或限流）";
				return false;
			}

			// ---------------- 自建服务器：Subsonic / Navidrome / Airsonic ----------------
			std::string Md5Hex(const std::string& data)
			{
				std::string hex;
				HCRYPTPROV prov = 0;
				HCRYPTHASH h = 0;
				if (CryptAcquireContextW(&prov, nullptr, nullptr, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT))
				{
					if (CryptCreateHash(prov, CALG_MD5, 0, 0, &h))
					{
						if (CryptHashData(h, (const BYTE*)data.data(), (DWORD)data.size(), 0))
						{
							BYTE d[16];
							DWORD n = sizeof(d);
							if (CryptGetHashParam(h, HP_HASHVAL, d, &n, 0))
							{
								static const char* x = "0123456789abcdef";
								for (DWORD i = 0; i < n; i++) { hex += x[d[i] >> 4]; hex += x[d[i] & 15]; }
							}
						}
						CryptDestroyHash(h);
					}
					CryptReleaseContext(prov, 0);
				}
				return hex;
			}
			bool SubsonicConfigured()
			{
				return !g_subUrl.empty() && !g_subUser.empty();
			}
			std::wstring SubsonicBase()
			{
				std::wstring u = g_subUrl;
				while (!u.empty() && (u.back() == L'/' || u.back() == L'\\')) u.pop_back();
				return u;
			}
			std::wstring SubsonicAuth()
			{
				std::string salt;
				for (int i = 0; i < 8; i++) salt += (char)('a' + rand() % 26);
				std::string token = Md5Hex(ToUtf8(g_subPass) + salt);
				return L"u=" + UrlEncode(g_subUser) + L"&t=" + FromUtf8(token) + L"&s=" + FromUtf8(salt) + L"&v=1.16.1&c=YuMediaPlayer";
			}

			bool SearchSubsonic(const std::wstring& keyword, int limit, std::vector<TrackItem>& out, std::wstring* error)
			{
				if (!SubsonicConfigured())
				{
					if (error) *error = L"自建服务器未配置：在 settings.ini 的 [Music] 里填 SubsonicUrl / SubsonicUser / SubsonicPassword，再点“刷新列表”";
					return false;
				}
				const std::wstring base = SubsonicBase();
				const bool random = (keyword == L"*");
				std::wstring url = base + (random ? L"/rest/getRandomSongs.view?size=" + std::to_wstring(limit) + L"&"
					: L"/rest/search3.view?artistCount=0&albumCount=0&songCount=" + std::to_wstring(limit) + L"&query=" + UrlEncode(keyword) + L"&")
					+ SubsonicAuth();

				MiniJson::Value root;
				if (!FetchJson(url, root, error))
					return false;
				const MiniJson::Value* resp = root.Get("subsonic-response");
				if (!resp)
				{
					if (error) *error = L"这不是 Subsonic 服务器的响应，请检查 SubsonicUrl";
					return false;
				}
				if (resp->GetString("status") != "ok")
				{
					const MiniJson::Value* err = resp->Get("error");
					if (error) *error = L"服务器返回错误：" + FromUtf8(err ? err->GetString("message") : std::string("unknown"));
					return false;
				}
				const MiniJson::Value* box = resp->Get(random ? "randomSongs" : "searchResult3");
				const MiniJson::Value* songs = box ? box->Get("song") : nullptr;
				if (!songs || songs->type != MiniJson::Value::Array)
					return true;

				for (const MiniJson::Value& it : songs->arr)
				{
					std::string id = GetIdString(it, "id");
					std::string title = it.GetString("title");
					if (id.empty() || title.empty()) continue;
					const std::wstring idEnc = UrlEncode(FromUtf8(id));
					TrackItem t;
					t.title = FromUtf8(title);
					t.artist = FromUtf8(it.GetString("artist"));
					t.album = FromUtf8(it.GetString("album"));
					t.durationSeconds = (int)it.GetNumber("duration");
					t.path = L"subsonic:" + FromUtf8(id);
					t.streamUrl = base + L"/rest/stream.view?id=" + idEnc + L"&format=mp3&maxBitRate=320&" + SubsonicAuth();
					t.downloadUrl = t.streamUrl;
					out.push_back(std::move(t));
				}
				return true;
			}

			// 缓存/下载文件的扩展名：iTunes 试听是 .m4a，其他都是 .mp3
			const wchar_t* AudioExtFor(const std::wstring& url)
			{
				std::string u = ToUtf8(url);
				size_t q = u.find('?');
				if (q != std::string::npos) u.resize(q);
				return EndsWithNoCase(u, ".m4a") ? L".m4a" : L".mp3";
			}

			// ---------------- 文件工具 ----------------
			std::wstring SanitizeFileName(std::wstring s)
			{
				for (wchar_t& c : s)
					if (c < 32 || wcschr(L"\\/:*?\"<>|", c)) c = L'_';
				while (!s.empty() && (s.back() == L' ' || s.back() == L'.')) s.pop_back();
				if (s.size() > 120) s.resize(120);
				return s.empty() ? std::wstring(L"_") : s;
			}
			bool EnsureDir(const std::wstring& dir)
			{
				if (dir.empty()) return false;
				DWORD a = GetFileAttributesW(dir.c_str());
				if (a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY)) return true;
				size_t p = dir.find_last_of(L"\\/");
				if (p != std::wstring::npos && p > 0) EnsureDir(dir.substr(0, p));
				return CreateDirectoryW(dir.c_str(), nullptr) || GetLastError() == ERROR_ALREADY_EXISTS;
			}
			bool FileNotEmpty(const std::wstring& path)
			{
				WIN32_FILE_ATTRIBUTE_DATA fa;
				return GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fa) && (fa.nFileSizeLow > 0 || fa.nFileSizeHigh > 0);
			}
			bool WriteUtf8File(const std::wstring& path, const std::string& data)
			{
				HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
				if (h == INVALID_HANDLE_VALUE) return false;
				DWORD w = 0;
				bool ok = WriteFile(h, data.data(), (DWORD)data.size(), &w, nullptr) && w == data.size();
				CloseHandle(h);
				return ok;
			}
		}

		std::wstring CategoryKeyword(const std::wstring& category)
		{
			return LookupCategory(kCategoryTable, category);
		}

		bool Search(const std::wstring& keyword, int limit, std::vector<TrackItem>& out, std::wstring* error)
		{
			out.clear();
			if (limit < 1) limit = 1;
			if (limit > 100) limit = 100;

			if (g_source == SourceSubsonic)
			{
				return SearchSubsonic(keyword, limit, out, error);
			}

			std::vector<std::wstring> words = SplitKeywords(keyword);
			if (words.empty())
			{
				if (error) *error = L"empty keyword";
				return false;
			}

			const int per = words.size() == 1 ? limit : (limit / (int)words.size() + 4);
			std::vector<std::vector<TrackItem>> groups(words.size());
			bool anyOk = false;
			std::wstring lastErr;
			for (size_t i = 0; i < words.size(); i++)
			{
				std::wstring e;
				const bool ok = (g_source == SourceItunes) ? SearchItunesOne(words[i], per, groups[i], &e)
					: SearchKugouOne(words[i], per, groups[i], &e);
				if (ok) anyOk = true;
				else lastErr = e;
			}
			if (!anyOk)
			{
				if (error) *error = lastErr;
				return false;
			}

			// 交错合并 + 去重
			std::vector<std::wstring> seen;
			for (size_t round = 0; (int)out.size() < limit; round++)
			{
				bool any = false;
				for (auto& g : groups)
				{
					if (round >= g.size()) continue;
					any = true;
					const TrackItem& t = g[round];
					std::wstring key = t.title + L"\x01" + t.artist;
					bool dup = false;
					for (const auto& k : seen) if (k == key) { dup = true; break; }
					if (dup) continue;
					seen.push_back(key);
					out.push_back(t);
					if ((int)out.size() >= limit) break;
				}
				if (!any) break;
			}
			return true;
		}

		void SetSubsonic(const std::wstring& url, const std::wstring& user, const std::wstring& password)
		{
			g_subUrl = url; g_subUser = user; g_subPass = password;
		}
		void SetSource(int source) { g_source = (source >= 0 && source < SourceCount) ? source : (int)SourceKugou; }
		int  GetSource() { return g_source; }
		bool SourceCanPlay(int source)
		{
			switch (source)
			{
			case SourceKugou:    return false;
			case SourceItunes:   return true;
			case SourceSubsonic: return SubsonicConfigured();
			default:             return false;
			}
		}
		bool HasPlayableSource() { return SourceCanPlay(g_source); }

		bool DownloadToFile(const std::wstring& url, const std::wstring& filePath, std::wstring* error)
		{
			HANDLE h = CreateFileW(filePath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
			if (h == INVALID_HANDLE_VALUE)
			{
				if (error) *error = L"无法创建文件：" + filePath;
				return false;
			}
			bool ok = HttpGet(url, [&](const char* p, DWORD n) {
				DWORD w = 0;
				return WriteFile(h, p, n, &w, nullptr) && w == n;
			}, error);
			CloseHandle(h);
			if (!ok) DeleteFileW(filePath.c_str());
			return ok;
		}

		static bool DownloadAtomic(const std::wstring& url, const std::wstring& file, std::wstring* error)
		{
			std::wstring part = file + L".part";
			if (!DownloadToFile(url, part, error)) return false;
			if (!MoveFileExW(part.c_str(), file.c_str(), MOVEFILE_REPLACE_EXISTING))
			{
				DeleteFileW(part.c_str());
				if (error) *error = L"无法保存文件：" + file;
				return false;
			}
			return true;
		}

		bool CacheAudio(const TrackItem& track, std::wstring& localFile, std::wstring* error)
		{
			if (track.streamUrl.empty())
			{
				if (error) *error = L"这首歌没有可用的播放地址";
				return false;
			}
			wchar_t tmp[MAX_PATH] = {};
			GetTempPathW(MAX_PATH, tmp);
			std::wstring dir = std::wstring(tmp) + L"YuMediaPlayer\\cache";
			EnsureDir(dir);
			std::wstring file = dir + L"\\" + SanitizeFileName(track.path.empty() ? track.title : track.path) + AudioExtFor(track.streamUrl);
			if (!FileNotEmpty(file))
			{
				if (!DownloadAtomic(track.streamUrl, file, error))
					return false;
			}
			localFile = file;
			return true;
		}

		bool DownloadSong(const TrackItem& track, const std::wstring& dir, std::wstring& outFile, std::wstring* error)
		{
			if (track.downloadUrl.empty())
			{
				if (error) *error = L"这首歌的作者不允许下载（只能在线试听）";
				return false;
			}
			if (!EnsureDir(dir))
			{
				if (error) *error = L"无法创建下载目录：" + dir;
				return false;
			}
			std::wstring name = track.artist.empty() ? track.title : (track.artist + L" - " + track.title);
			std::wstring file = dir + L"\\" + SanitizeFileName(name) + L".mp3";
			if (!FileNotEmpty(file))
			{
				if (!DownloadAtomic(track.downloadUrl, file, error))
					return false;
			}
			outFile = file;
			return true;
		}

		bool DownloadLyrics(const TrackItem& track, const std::wstring& dir, const std::wstring& baseName,
			std::wstring& outFile, std::wstring* error)
		{
			std::wstring url = L"https://lrclib.net/api/search?track_name=" + UrlEncode(track.title);
			if (!track.artist.empty())
				url += L"&artist_name=" + UrlEncode(track.artist);

			std::string body;
			if (!HttpGet(url, [&](const char* p, DWORD n) { body.append(p, n); return body.size() < 4u * 1024 * 1024; }, error))
				return false;

			MiniJson::Value root;
			if (!MiniJson::Parse(body, root) || root.type != MiniJson::Value::Array)
			{
				if (error) *error = L"歌词服务返回了无法识别的内容";
				return false;
			}

			const MiniJson::Value* best = nullptr;
			int bestScore = 1 << 30;
			for (const MiniJson::Value& it : root.arr)
			{
				if (it.GetString("syncedLyrics").empty() && it.GetString("plainLyrics").empty())
					continue;
				int score = it.GetString("syncedLyrics").empty() ? 1000 : 0;
				double d = it.GetNumber("duration");
				if (track.durationSeconds > 0 && d > 0)
				{
					int diff = (int)(d > track.durationSeconds ? d - track.durationSeconds : track.durationSeconds - d);
					score += diff > 5 ? 100 + diff : diff;
				}
				if (score < bestScore) { bestScore = score; best = &it; }
			}
			if (!best)
			{
				if (error) *error = L"没有找到这首歌的歌词";
				return false;
			}

			std::string text = best->GetString("syncedLyrics");
			const bool synced = !text.empty();
			if (!synced) text = best->GetString("plainLyrics");

			if (!EnsureDir(dir))
			{
				if (error) *error = L"无法创建目录：" + dir;
				return false;
			}
			std::wstring name = !baseName.empty() ? baseName
				: (track.artist.empty() ? track.title : (track.artist + L" - " + track.title));
			std::wstring file = dir + L"\\" + SanitizeFileName(name) + (synced ? L".lrc" : L".txt");
			if (!WriteUtf8File(file, text))
			{
				if (error) *error = L"无法写入文件：" + file;
				return false;
			}
			outFile = file;
			return true;
		}
	}
}