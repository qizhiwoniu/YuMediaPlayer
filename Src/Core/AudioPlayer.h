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

		// 创建媒体拓扑
		bool CreateTopology(IMFMediaSource* mediaSource, IMFMediaSession* mediaSession);

		// 清理资源
		void Cleanup();
	};
}
