#pragma once
// 在线歌曲数据源（在设置页 "音乐源" 里切换，设置保存在 settings.ini 的 [Music] Source）：
//  0 酷狗音乐      ：只有列表（歌名/歌手/专辑/时长），不能播放/下载
//  1 Jamendo      ：Creative Commons 授权，可完整试听/下载（需要免费的 client_id）
//  2 Internet Archive：公共领域 / CC 授权的音乐（netlabels、opensource_audio），免 Key，可试听/下载
//  3 ccMixter     ：CC 授权的原创/混音音乐社区，免 Key，可试听/下载
//  4 Openverse    ：WordPress 的 CC 开放授权音乐聚合库（含 Jamendo 等），免 Key，可试听/下载
//  5 iTunes       ：Apple 官方公开接口，国内一般可直连，只有合法的 30 秒试听片段，不可下载
//  6 自建服务器    ：Subsonic / Navidrome 等（你自己的音乐库），完整试听/下载，需在 [Music] 里配置
// 歌词统一来自 LRCLIB（开放歌词库）。
// 想再加一个源：1) 加到下面的 Source 枚举  2) 在 OnlineMusic.cpp 里写一个 SearchXxx()
// 3) 在 Search()/CategoryKeyword()/SourceCanPlay() 的 switch 里各加一行  4) 设置页下拉里加一项。
#include <string>
#include <vector>
#include "WindowGUI.h"   // TrackItem

namespace YuMediaPlayer
{
	namespace OnlineMusic
	{
		enum Source
		{
			SourceKugou = 0,
			SourceItunes = 1,
			SourceSubsonic = 2,
			SourceCount = 3
		};

		// 切换当前音乐源（设置页会自己调用；越界按酷狗处理）
		void SetSource(int source);
		int  GetSource();
		// 某个源是否可以试听/下载（酷狗 = false；Jamendo 还要求已配置 client_id）
		bool SourceCanPlay(int source);

		// 同步搜索（会阻塞，必须在后台线程调用）。keyword 里可以用 | 分隔多个词（仅酷狗）。成功返回 true。
		bool Search(const std::wstring& keyword, int limit, std::vector<TrackItem>& out, std::wstring* error = nullptr);

		// 乐馆分类名 -> 搜索关键词（想换推荐内容就改 OnlineMusic.cpp 里各源的表）
		std::wstring CategoryKeyword(const std::wstring& category);
		// 设置自建 Subsonic/Navidrome 服务器（url 如 http://192.168.1.10:4533；password 是明文，内部做 MD5 token）
		void SetSubsonic(const std::wstring& url, const std::wstring& user, const std::wstring& password);
		// 当前选中的源能不能试听/下载
		bool HasPlayableSource();

		// 以下函数都是同步的，必须在后台线程调用。失败返回 false，error 里是原因（中文）。
		bool DownloadToFile(const std::wstring& url, const std::wstring& filePath, std::wstring* error = nullptr);
		// 试听：把整首歌缓存到临时目录，localFile 是可以直接当本地歌曲播放的 mp3
		bool CacheAudio(const TrackItem& track, std::wstring& localFile, std::wstring* error = nullptr);
		// 下载歌曲到 dir（文件名：歌手 - 歌名.mp3）。作者不允许下载时返回 false
		bool DownloadSong(const TrackItem& track, const std::wstring& dir, std::wstring& outFile, std::wstring* error = nullptr);
		// 下载歌词到 dir。baseName 为空时用"歌手 - 歌名"；有时间轴存 .lrc，否则存 .txt
		bool DownloadLyrics(const TrackItem& track, const std::wstring& dir, const std::wstring& baseName,
			std::wstring& outFile, std::wstring* error = nullptr);
	}
}
