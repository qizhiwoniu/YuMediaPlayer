#pragma once
#include <wrl.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <windows.h>
#include <string>

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "wmcodecdspuuid.lib")
#pragma comment(lib, "mf.lib")
#pragma comment(lib, "strmiids.lib")

using Microsoft::WRL::ComPtr;

namespace YuMediaPlayer
{
	enum class PlaybackState
	{
		Stopped,
		Playing,
		Paused
	};

	class AudioPlayer
	{
	public:
		AudioPlayer();
		~AudioPlayer();

		// 初始化 Media Foundation
		bool Initialize();

		// 打开并播放音频文件
		bool Play(const std::wstring& filePath);

		// 暂停播放
		bool Pause();

		// 恢复播放
		bool Resume();

		// 停止播放
		bool Stop();

		// 获取播放状态
		PlaybackState GetPlaybackState() const { return m_playbackState; }

		// 获取当前播放进度 (0.0 - 1.0)
		float GetPlayProgress() const;

		// 获取总时长（毫秒）
		long long GetDuration() const;

		// 获取当前播放位置（毫秒）
		long long GetCurrentPosition() const;

		// 设置播放位置
		bool SetPosition(long long positionMs);

		// 获取音量 (0.0 - 1.0)
		float GetVolume() const;

		// 设置音量 (0.0 - 1.0)
		bool SetVolume(float volume);

	private:
		ComPtr<IMFMediaSession> m_mediaSession;
		ComPtr<IMFMediaSource> m_mediaSource;
		ComPtr<IMFTopology> m_topology;
		ComPtr<IMFSimpleAudioVolume> m_audioVolume;

		PlaybackState m_playbackState;
		long long m_duration;

		// 是否是本对象负责初始化的 COM（用于析构时决定要不要 CoUninitialize）
		bool m_comInitializedByUs = false;

		// 创建媒体拓扑
		bool CreateTopology(IMFMediaSource* mediaSource, IMFMediaSession* mediaSession);

		// 把传入的文件路径解析成一个真实存在的绝对路径：
		// 依次尝试"原样路径（相对于当前工作目录）"和"exe 所在目录 + 传入路径"，
		// 哪个能通过 GetFileAttributesW 判断为存在就用哪个；都不存在则返回空串。
		// 之前 Play() 直接把调用者传进来的相对路径丢给 CreateObjectFromURL，
		// 一旦程序不是从 exe 所在目录启动（比如 VS 调试时工作目录设成了项目目录），
		// 相对路径就找不到文件，而且几乎没有任何提示。
		std::wstring ResolveExistingFilePath(const std::wstring& path) const;

		// Start()/Resume() 都是异步的：调用之后立刻返回 S_OK 不代表真的开始出声了，
		// 音频渲染器（SAR）的实际创建/激活是在 MF 自己的工作线程上异步完成的，
		// 一旦失败，如果不监听事件，代码是完全看不到的——按钮照样会被当成"正在播放"。
		// 这个函数同步等待会话事件，收到 MEError 或者带错误状态的事件就判定失败，
		// 并把具体 HRESULT 打到 OutputDebugString 里，方便定位到底是哪一步炸了。
		bool WaitForSessionEvent(MediaEventType expectedType, int maxEventsToCheck = 10);

		// 只重置跟上一首歌绑定的对象（媒体源/拓扑/音量接口），保留
		// m_mediaSession 不动。Play() 每次播放新文件时调用这个，而不是
		// 调用会把 m_mediaSession 一并干掉的 Cleanup()。
		void ResetForNewTrack();

		// 清理资源（含 m_mediaSession），只应该在析构时调用一次
		void Cleanup();
	};
}
