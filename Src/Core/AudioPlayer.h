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

		// 设置"曲目自然播完"时要通知的窗口和消息。
		// 必须在 Initialize() 成功之后调用（回调对象是在 Initialize() 里创建的）。
		// 会话在 MF 工作线程上收到 MESessionEnded 时，用 PostMessage 给 hwnd 发 msg，
		// WPARAM = 当时的曲目编号（见 GetTrackGeneration）。
		void SetEndNotify(HWND hwnd, UINT msg);

		// 当前是"第几首歌"的编号，每次 Play() 加一。
		// 界面线程收到"播完了"通知后，拿通知里的编号跟这个值比较，
		// 不一致说明是上一首遗留的过期通知，应该直接忽略。
		unsigned int GetTrackGeneration() const;

		// 诊断用：返回"AudioPlayer.cpp 编译时看到的" sizeof(AudioPlayer)。
		// 调用方拿它跟自己的 sizeof(AudioPlayer) 比较，不一致说明不同 .cpp 用的头文件版本不一样
		// （重复的头文件，或者没重新编译的旧 .obj）。
		size_t DebugSizeOfSelf() const;

	private:
		ComPtr<IMFMediaSession> m_mediaSession;
		ComPtr<IMFMediaSource> m_mediaSource;
		ComPtr<IMFTopology> m_topology;
		ComPtr<IMFSimpleAudioVolume> m_audioVolume;

		// 会话事件回调对象（AudioPlayer.cpp 里匿名命名空间中的 SessionEventCallback）。
		// 故意声明成基类 IMFAsyncCallback*，这样头文件不用暴露具体实现类；
		// 由 Initialize() 创建（持有创建时的那 1 个引用），Cleanup() 里 Release()。
		IMFAsyncCallback* m_eventCallback = nullptr;

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
		// 音频渲染器（SAR）的实际创建/激活是在 MF 自己的工作线程上异步完成的。
		// 会话事件现在由 m_eventCallback（异步 BeginGetEvent）统一接收，
		// 这个函数带超时地等回调设置的标志位；收到错误事件或超时就判定失败，
		// 具体 HRESULT 会打到 OutputDebugString。
		// 调用前要先 Arm 对应的标志（见 AudioPlayer.cpp 里的 ArmSessionEvent）。
		// maxEventsToCheck 参数保留只是为了兼容旧签名，已经不再使用。
		bool WaitForSessionEvent(MediaEventType expectedType, int maxEventsToCheck = 10);

		// 播放新的一首之前调用：完整关闭旧会话（Close -> Shutdown 媒体源 -> Shutdown 会话），
		// 再创建一个全新的会话并重新订阅事件。不复用会话，是因为复用后从第二首歌开始
		// MESessionStarted 会带着 MF_E_SHUTDOWN 失败。Initialize() 刚创建、还没播放过的会话
		// 会被直接沿用。失败返回 false。
		bool ResetForNewTrack();

		// 清理资源（含 m_mediaSession），只应该在析构时调用一次
		void Cleanup();
	};
}
