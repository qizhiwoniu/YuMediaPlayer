#pragma once
#include "WindowGUI.h"      // TrackItem
#include <atomic>
#include <thread>
#include <string>
#include <vector>
#include <functional>

namespace YuMediaPlayer
{
	// 本地音乐扫描器：递归扫描一个目录里的音频文件，读取标签（标题/歌手/专辑/时长），去重，
	// 结果在 UI 线程里通过回调交出来，可直接传给 WindowGUI::SetLocalTracks。
	//
	// 用法：
	//   LocalMusicScanner scanner;                       // 必须在 UI 线程创建（内部有一个消息窗口）
	//   scanner.SetResultCallback([&](std::vector<TrackItem> t) { gui.SetLocalTracks(std::move(t)); });
	//   scanner.Start(L"D:\\Music");                      // 后台线程扫描，不卡界面；再次 Start 会取消上一次
	class LocalMusicScanner
	{
	public:
		LocalMusicScanner();
		~LocalMusicScanner();
		LocalMusicScanner(const LocalMusicScanner&) = delete;
		LocalMusicScanner& operator=(const LocalMusicScanner&) = delete;

		// 扫描完成时回调（在创建扫描器的 UI 线程里调用）。folder 为空时得到空列表（用来清空本地列表）
		void SetResultCallback(std::function<void(std::vector<TrackItem>)> callback);

		// 开始后台扫描。force=false 时，目录和上次相同就忽略（设置页每次改动都可以放心调用）
		void Start(const std::wstring& folder, bool force = false);
		void Cancel();

		// 同步扫描（会阻塞当前线程；想自己放到线程里用）。cancelled 返回 true 时尽快结束
		static std::vector<TrackItem> ScanFolder(const std::wstring& folder,
			const std::function<bool()>& cancelled = nullptr);

	private:
		static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
		void StopWorker();

	private:
		HWND m_hwnd = nullptr;
		std::thread m_thread;
		std::atomic<bool> m_cancel{ false };
		int m_generation = 0;             // 每次 Start 加 1，过期的结果直接丢弃
		bool m_started = false;
		std::wstring m_folder;
		std::function<void(std::vector<TrackItem>)> m_onResult;
	};
}
