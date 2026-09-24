#pragma once
#include <string>
#include <vector>

namespace YuMediaPlayer
{
	// 一首歌的信息。歌名/歌手/封面都由文件名推导出来，不需要额外的数据库或标签解析。
	struct Track
	{
		std::wstring audioPath;  // 音频文件的完整路径
		std::wstring title;      // 歌名（"周杰伦-七里香.mp3" -> "七里香"）
		std::wstring artist;     // 歌手（"周杰伦-七里香.mp3" -> "周杰伦"），文件名里没有 '-' 时为空
		std::wstring coverPath;  // 封面图完整路径；没找到同名图片时为空（界面会显示黑胶占位）
	};

	// 播放列表：只负责"有哪些歌、当前是哪一首、上一首/下一首是哪一首"，
	// 不碰任何 UI 和音频播放，所以可以单独测试。
	class Playlist
	{
	public:
		// 用默认位置建立列表：依次找 "song\local"（相对当前工作目录）、
		// "<exe 目录>\song\local"，第一个里面有音频文件的目录就用它。
		// 返回曲目数（0 表示一首都没找到）。
		int LoadDefault();

		// 扫描 audioDir 下的音频文件（mp3/wav/m4a/wma/flac/aac），按文件名排序。
		// 封面：对每首歌，按顺序在 audioDir 和 coverDirs 里找"同名 + .png/.jpg/.jpeg/.bmp"，
		// 第一个存在的就是它的封面。
		int LoadFromDirectory(const std::wstring& audioDir, const std::vector<std::wstring>& coverDirs);

		bool Empty() const { return m_tracks.empty(); }
		int  Count() const { return static_cast<int>(m_tracks.size()); }
		int  CurrentIndex() const { return m_current; }

		// 当前曲目；列表为空时返回 nullptr。
		const Track* Current() const;

		// 第 index 首；越界返回 nullptr。
		const Track* At(int index) const;

		// 直接跳到某一首。index 越界返回 false，当前曲目不变。
		bool SetCurrent(int index);

		// 移动到下一首 / 上一首，返回新的当前索引（列表为空返回 -1）。
		//  - 顺序模式：到头后循环。
		//  - shuffle=true：下一首随机挑一首（保证不跟当前这首重复），
		//    上一首则按"刚才实际播过的顺序"往回退，退完了再退回顺序模式。
		//  - 列表里只有一首歌时：返回同一个索引（调用者重新播放这首即可）。
		int MoveNext(bool shuffle);
		int MovePrevious(bool shuffle);

		// 下面两个是纯字符串逻辑，单独暴露出来方便测试。
		static void ParseTitleArtist(const std::wstring& fileStem, std::wstring& title, std::wstring& artist);
		static bool IsAudioExtension(const std::wstring& extWithDot);

	private:
		std::vector<Track> m_tracks;
		int m_current = 0;

		std::vector<int> m_history;  // 随机模式下已经播过的曲目索引（用于"上一首"）

		// 随机数：一个很小的 xorshift 生成器，够"随机挑歌"用了。
		// 故意不用 <random>——这个头文件会被到处 include，而 <windows.h> 在没定义
		// NOMINMAX 时会把 min/max 变成宏，容易跟标准库头文件互相打架。
		unsigned int m_rngState = 0;
		int RandomBelow(int n);  // 返回 [0, n) 的随机整数，n 必须 > 0
	};
}
