#include "AudioPlayer.h"
#include <cassert>
#include <cstdio>
#include <cstdarg>
#include <mferror.h>

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "wmcodecdspuuid.lib")
#pragma comment(lib, "mf.lib")

namespace
{
	// 取当前 exe 所在目录（末尾带 \），逻辑跟 CircularAvatar.cpp 里的
	// GetExeDir 一样：不依赖"当前工作目录"，而是找 exe 实际所在的位置。
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

	bool IsAbsoluteOrRootedPath(const std::wstring& path)
	{
		if (path.size() >= 2 && path[1] == L':')
			return true;
		if (path.size() >= 2 && path[0] == L'\\' && path[1] == L'\\')
			return true;
		if (!path.empty() && path[0] == L'\\')
			return true;
		return false;
	}

	bool FileExists(const std::wstring& path)
	{
		DWORD attr = GetFileAttributesW(path.c_str());
		return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
	}

	// printf 风格的 OutputDebugString，方便把 HRESULT 之类的值直接打进日志里。
	// 在 VS 里按 F5 调试时，"输出"窗口(或者装个 DebugView)就能看到这些内容。
	void DebugLogf(const wchar_t* fmt, ...)
	{
		wchar_t buf[512];
		va_list args;
		va_start(args, fmt);
		vswprintf_s(buf, fmt, args);
		va_end(args);
		OutputDebugStringW(buf);
	}

	// 诊断用：直接问系统"当前这个进程里，MF_E_TOPO_CODEC_NOT_FOUND 报错前
	// 到底能不能枚举到 MP3 解码器"。用 MFTEnumEx 去查 MFT_CATEGORY_AUDIO_DECODER
	// 分类下、输入类型是 MFAudioFormat_MP3 的解码器 MFT——这跟 SetTopology
	// 内部拓扑加载器实际查找解码器时用的是同一套注册表信息，所以枚举到几个、
	// 叫什么名字，能直接反映出"是不是压根没有解码器可用"还是"有解码器但
	// 别的原因导致用不了"（比如进程位数跟注册表项对不上，这种情况下
	// MFTEnumEx 在当前进程里也会直接枚举不到）。
	void LogAvailableMp3Decoders()
	{
		MFT_REGISTER_TYPE_INFO inputType = { MFMediaType_Audio, MFAudioFormat_MP3 };

		IMFActivate** activates = nullptr;
		UINT32 count = 0;

		HRESULT hr = MFTEnumEx(
			MFT_CATEGORY_AUDIO_DECODER,
			MFT_ENUM_FLAG_ALL,
			&inputType,
			nullptr, // 输出类型不限
			&activates,
			&count);

		if (FAILED(hr))
		{
			DebugLogf(L"[AudioPlayer][诊断] MFTEnumEx 调用本身失败, hr=0x%08X\n", hr);
			return;
		}

		DebugLogf(L"[AudioPlayer][诊断] 当前进程能枚举到 %u 个 MP3 音频解码器 MFT\n", count);

		for (UINT32 i = 0; i < count; ++i)
		{
			wchar_t name[256] = {};
			UINT32 nameLen = 0;
			if (activates[i])
			{
				activates[i]->GetString(MFT_FRIENDLY_NAME_Attribute, name, 256, &nameLen);
				DebugLogf(L"[AudioPlayer][诊断]   [%u] %s\n", i, nameLen > 0 ? name : L"(无名字)");
			}
		}

		if (count == 0)
		{
			DebugLogf(L"[AudioPlayer][诊断] 一个都没枚举到——这就是 MF_E_TOPO_CODEC_NOT_FOUND 的直接原因。"
				L"最常见的原因是当前进程的位数（x86/x64）跟系统里注册这个解码器 MFT 的位数不一致，"
				L"其次是系统媒体组件本身损坏/缺失。\n");
		}

		for (UINT32 i = 0; i < count; ++i)
		{
			if (activates[i])
				activates[i]->Release();
		}
		CoTaskMemFree(activates);
	}
}

namespace YuMediaPlayer
{
	AudioPlayer::AudioPlayer()
		: m_playbackState(PlaybackState::Stopped)
		, m_duration(0)
		, m_comInitializedByUs(false)
	{
	}

	AudioPlayer::~AudioPlayer()
	{
		Cleanup();
		MFShutdown();
		if (m_comInitializedByUs)
		{
			CoUninitialize();
		}
	}

	bool AudioPlayer::Initialize()
	{
		// ------------------------------------------------------------
		// 重点：Media Foundation 官方文档明确要求，调用任何 MF 函数之前
		// 必须先在当前线程上初始化 COM（CoInitializeEx）。
		// 之前这里完全没调用它——很多时候 MFStartup/MFCreateMediaSession
		// 这些"外层"调用看起来仍然会成功，但音频渲染器（SAR）真正被激活、
		// 用到 WASAPI 的那一步是在 MF 内部工作线程上异步发生的，一旦那里
		// 因为 COM 没初始化而失败，Start() 本身还是会返回 S_OK（因为它是
		// 异步排队的），于是就出现"按钮变成暂停图标、状态看着像在播放，
		// 但实际上完全没有声音"这种诡异现象——本质上是错误被悄悄吞掉了。
		// ------------------------------------------------------------
		HRESULT hrCom = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
		if (hrCom == S_OK || hrCom == S_FALSE)
		{
			// S_OK：这次是我们真正初始化的；S_FALSE：这个线程已经初始化过了
			// （比如调用者线程之前又调用了一次）。两种情况都记下由我们负责，
			// 保证以后 CoUninitialize 配对次数不会比 CoInitialize 多。
			m_comInitializedByUs = true;
		}
		else if (hrCom == RPC_E_CHANGED_MODE)
		{
			// 这个线程已经以不同的并发模型初始化过 COM 了（比如别的地方用了
			// COINIT_MULTITHREADED）。不是我们初始化的，不需要也不能由我们
			// 负责 Uninitialize，但 COM 已经可用，可以继续往下走。
			DebugLogf(L"[AudioPlayer] COM 已经以另一种模式初始化过（RPC_E_CHANGED_MODE），继续使用现有的 COM 状态\n");
		}
		else
		{
			DebugLogf(L"[AudioPlayer] CoInitializeEx failed, hr=0x%08X\n", hrCom);
			return false;
		}

		HRESULT hr = MFStartup(MF_VERSION);
		if (FAILED(hr))
		{
			DebugLogf(L"[AudioPlayer] MFStartup failed, hr=0x%08X\n", hr);
			return false;
		}

		hr = MFCreateMediaSession(nullptr, &m_mediaSession);
		if (FAILED(hr))
		{
			DebugLogf(L"[AudioPlayer] MFCreateMediaSession failed, hr=0x%08X\n", hr);
			MFShutdown();
			return false;
		}

		return true;
	}

	bool AudioPlayer::Play(const std::wstring& filePath)
	{
		if (!m_mediaSession)
		{
			DebugLogf(L"[AudioPlayer] m_mediaSession 为空，Initialize() 是不是没成功/没被调用？\n");
			return false;
		}

		ResetForNewTrack();

		// ------------------------------------------------------------
		// 先把路径解析成一个真正存在的绝对路径。之前是直接把调用者传进来的
		// 相对路径原样丢给 CreateObjectFromURL——如果程序运行时的"当前工作
		// 目录"跟 exe 所在目录不一样（比如在 VS 里按 F5 调试时，工作目录
		// 经常被设成项目目录而不是输出目录），相对路径就会找不到文件，
		// 而且 CreateObjectFromURL 失败得悄无声息，不看 Output 窗口根本
		// 发现不了。这里改成跟 CircularAvatar 加载封面图一样的做法：找不到
		// 就试 exe 所在目录，并且直接打印出最终到底用的是哪个路径、存不存在。
		// ------------------------------------------------------------
		std::wstring resolvedPath = ResolveExistingFilePath(filePath);
		if (resolvedPath.empty())
		{
			DebugLogf(L"[AudioPlayer] 找不到音频文件：%s（当前工作目录 + exe 所在目录都试过了）\n", filePath.c_str());
			return false;
		}
		DebugLogf(L"[AudioPlayer] 播放文件：%s\n", resolvedPath.c_str());

		HRESULT hr = S_OK;

		// 创建媒体源
		ComPtr<IMFSourceResolver> sourceResolver;
		hr = MFCreateSourceResolver(&sourceResolver);
		if (FAILED(hr))
		{
			DebugLogf(L"[AudioPlayer] MFCreateSourceResolver failed, hr=0x%08X\n", hr);
			return false;
		}

		MF_OBJECT_TYPE objectType = MF_OBJECT_INVALID;
		hr = sourceResolver->CreateObjectFromURL(
			resolvedPath.c_str(),
			MF_RESOLUTION_MEDIASOURCE | MF_RESOLUTION_READ,
			nullptr,
			&objectType,
			(IUnknown**)&m_mediaSource
		);

		if (FAILED(hr))
		{
			DebugLogf(L"[AudioPlayer] CreateObjectFromURL failed, hr=0x%08X（文件格式可能不受支持，或缺少对应解码器）\n", hr);
			return false;
		}

		// 创建拓扑
		if (!CreateTopology(m_mediaSource.Get(), m_mediaSession.Get()))
		{
			OutputDebugStringW(L"CreateTopology failed\n");
			return false;
		}

		// 获取音频输出节点以便控制音量
		ComPtr<IMFCollection> outputNodes;
		hr = m_topology->GetOutputNodeCollection(&outputNodes);
		if (SUCCEEDED(hr) && outputNodes)
		{
			DWORD nodeCount = 0;
			outputNodes->GetElementCount(&nodeCount);
			if (nodeCount > 0)
			{
				ComPtr<IUnknown> element;
				hr = outputNodes->GetElement(0, &element);
				if (SUCCEEDED(hr))
				{
					ComPtr<IMFTopologyNode> outputNode;
					hr = element.As(&outputNode);
					if (SUCCEEDED(hr))
					{
						ComPtr<IUnknown> audioRendererObject;
						hr = outputNode->GetObject(&audioRendererObject);
						if (SUCCEEDED(hr))
						{
							audioRendererObject.As(&m_audioVolume);
							if (m_audioVolume)
							{
								// 保险起见，每次新开始播放都强制拉满音量、取消静音，
								// 避免"代码逻辑一切正常，但这路音频会话恰好是静音/
								// 音量为 0"这种跟播放器逻辑无关、却看起来像没反应的情况。
								m_audioVolume->SetMasterVolume(1.0f);
								m_audioVolume->SetMute(FALSE);
							}
						}
					}
				}
			}
		}

		// 获取时长
		ComPtr<IMFPresentationDescriptor> presentationDesc;
		hr = m_mediaSource->CreatePresentationDescriptor(&presentationDesc);
		if (SUCCEEDED(hr))
		{
			MFTIME duration = 0;
			presentationDesc->GetUINT64(MF_PD_DURATION, (UINT64*)&duration);
			m_duration = duration / 10000; // 转换为毫秒
		}

		// 启动媒体会话
		PROPVARIANT startPosition;
		PropVariantInit(&startPosition);
		startPosition.vt = VT_EMPTY;

		hr = m_mediaSession->Start(&GUID_NULL, &startPosition);
		PropVariantClear(&startPosition);

		if (FAILED(hr))
		{
			DebugLogf(L"[AudioPlayer] MediaSession Start failed, hr=0x%08X\n", hr);
			return false;
		}

		// Start() 是异步的，返回 S_OK 只代表"启动请求已经排队"，不代表真的开始
		// 出声了。这里同步等一下 MESessionStarted 事件，真正确认成功/失败，
		// 这样如果底层（比如音频渲染器激活）出了问题，Output 窗口里能看到
		// 具体原因，而不是按钮悄悄变成"暂停图标"但完全没有声音。
		if (!WaitForSessionEvent(MESessionStarted))
		{
			DebugLogf(L"[AudioPlayer] 会话没能确认进入播放状态，当作播放失败处理\n");
			return false;
		}

		m_playbackState = PlaybackState::Playing;
		return true;
	}

	bool AudioPlayer::Pause()
	{
		if (m_playbackState != PlaybackState::Playing || !m_mediaSession)
			return false;

		HRESULT hr = m_mediaSession->Pause();
		if (SUCCEEDED(hr))
		{
			m_playbackState = PlaybackState::Paused;
			return true;
		}
		return false;
	}

	bool AudioPlayer::Resume()
	{
		if (m_playbackState != PlaybackState::Paused || !m_mediaSession)
			return false;

		PROPVARIANT startPosition;
		PropVariantInit(&startPosition);
		startPosition.vt = VT_EMPTY;

		HRESULT hr = m_mediaSession->Start(&GUID_NULL, &startPosition);
		PropVariantClear(&startPosition);

		if (FAILED(hr))
		{
			DebugLogf(L"[AudioPlayer] Resume: MediaSession Start failed, hr=0x%08X\n", hr);
			return false;
		}

		if (!WaitForSessionEvent(MESessionStarted))
		{
			DebugLogf(L"[AudioPlayer] Resume: 会话没能确认恢复播放\n");
			return false;
		}

		m_playbackState = PlaybackState::Playing;
		return true;
	}

	bool AudioPlayer::Stop()
	{
		if (m_playbackState == PlaybackState::Stopped || !m_mediaSession)
			return false;

		HRESULT hr = m_mediaSession->Stop();
		if (SUCCEEDED(hr))
		{
			m_playbackState = PlaybackState::Stopped;
			return true;
		}
		return false;
	}

	float AudioPlayer::GetPlayProgress() const
	{
		if (m_duration <= 0)
			return 0.0f;

		long long current = GetCurrentPosition();
		return static_cast<float>(current) / static_cast<float>(m_duration);
	}

	long long AudioPlayer::GetDuration() const
	{
		return m_duration;
	}

	long long AudioPlayer::GetCurrentPosition() const
	{
		if (!m_mediaSession)
			return 0;

		ComPtr<IMFClock> clock;
		HRESULT hr = m_mediaSession->GetClock(&clock);
		if (FAILED(hr))
			return 0;

		MFTIME currentTime = 0;
		DWORD dwClockState = 0;
		hr = clock->GetCorrelatedTime(0, &currentTime, nullptr);
		if (FAILED(hr))
			return 0;

		return currentTime / 10000; // 转换为毫秒
	}

	bool AudioPlayer::SetPosition(long long positionMs)
	{
		if (!m_mediaSession || positionMs < 0 || positionMs > m_duration)
			return false;

		// 暂停
		HRESULT hr = m_mediaSession->Pause();
		if (FAILED(hr))
			return false;

		// 设置位置
		MFTIME position = positionMs * 10000; // 转换为 MFTIME

		PROPVARIANT varPosition;
		PropVariantInit(&varPosition);
		varPosition.vt = VT_I8;
		varPosition.hVal.QuadPart = position;

		hr = m_mediaSession->Start(&GUID_NULL, &varPosition);
		PropVariantClear(&varPosition);

		if (SUCCEEDED(hr))
		{
			m_playbackState = PlaybackState::Playing;
			return true;
		}
		return false;
	}

	float AudioPlayer::GetVolume() const
	{
		if (!m_audioVolume)
			return 1.0f;

		float volume = 1.0f;
		m_audioVolume->GetMasterVolume(&volume);
		return volume;
	}

	bool AudioPlayer::SetVolume(float volume)
	{
		if (!m_audioVolume || volume < 0.0f || volume > 1.0f)
			return false;

		HRESULT hr = m_audioVolume->SetMasterVolume(volume);
		return SUCCEEDED(hr);
	}

	bool AudioPlayer::CreateTopology(IMFMediaSource* mediaSource, IMFMediaSession* mediaSession)
	{
		HRESULT hr = S_OK;

		// 创建空拓扑
		hr = MFCreateTopology(&m_topology);
		if (FAILED(hr))
		{
			DebugLogf(L"[AudioPlayer] MFCreateTopology failed, hr=0x%08X\n", hr);
			return false;
		}

		// 创建源节点
		ComPtr<IMFPresentationDescriptor> presentationDesc;
		hr = mediaSource->CreatePresentationDescriptor(&presentationDesc);
		if (FAILED(hr))
		{
			DebugLogf(L"[AudioPlayer] CreatePresentationDescriptor failed, hr=0x%08X\n", hr);
			return false;
		}

		DWORD streamCount = 0;
		presentationDesc->GetStreamDescriptorCount(&streamCount);
		DebugLogf(L"[AudioPlayer] streamCount=%u\n", streamCount);

		// 之前每一步失败都只是 continue，不打印任何原因，导致
		// "CreateTopology failed" 完全没法定位是哪一步、什么 HRESULT
		// 出的问题。现在把每一步的失败都记下来，并且统计到底成功连上了
		// 几路流——哪怕 SetTopology 本身碰巧返回成功，如果一路流都没连上，
		// 播放出来也肯定是没有声音的，这里直接当失败处理。
		int connectedStreams = 0;

		for (DWORD i = 0; i < streamCount; i++)
		{
			BOOL selected = FALSE;
			ComPtr<IMFStreamDescriptor> streamDesc;
			hr = presentationDesc->GetStreamDescriptorByIndex(i, &selected, &streamDesc);
			if (FAILED(hr))
			{
				DebugLogf(L"[AudioPlayer] stream[%u] GetStreamDescriptorByIndex failed, hr=0x%08X\n", i, hr);
				continue;
			}

			if (!selected)
			{
				DebugLogf(L"[AudioPlayer] stream[%u] not selected, skip\n", i);
				continue;
			}

			// 创建源节点
			ComPtr<IMFTopologyNode> sourceNode;
			hr = MFCreateTopologyNode(MF_TOPOLOGY_SOURCESTREAM_NODE, &sourceNode);
			if (FAILED(hr))
			{
				DebugLogf(L"[AudioPlayer] stream[%u] MFCreateTopologyNode(SOURCESTREAM) failed, hr=0x%08X\n", i, hr);
				continue;
			}

			hr = sourceNode->SetUnknown(MF_TOPONODE_SOURCE, mediaSource);
			if (FAILED(hr))
			{
				DebugLogf(L"[AudioPlayer] stream[%u] SetUnknown(SOURCE) failed, hr=0x%08X\n", i, hr);
				continue;
			}

			// 重点修复：源节点必须同时设置 SOURCE / PRESENTATION_DESCRIPTOR /
			// STREAM_DESCRIPTOR 这三个属性，之前只设置了前者和后者，唯独漏了
			// PRESENTATION_DESCRIPTOR。这正是 SetTopology 报
			// 0xC00D5217（MF_E_TOPO_MISSING_PRESENTATION_DESCRIPTOR，
			// "源节点缺少 Presentation Descriptor"）的直接原因，
			// 跟系统有没有装解码器完全无关。
			hr = sourceNode->SetUnknown(MF_TOPONODE_PRESENTATION_DESCRIPTOR, presentationDesc.Get());
			if (FAILED(hr))
			{
				DebugLogf(L"[AudioPlayer] stream[%u] SetUnknown(PRESENTATION_DESCRIPTOR) failed, hr=0x%08X\n", i, hr);
				continue;
			}

			hr = sourceNode->SetUnknown(MF_TOPONODE_STREAM_DESCRIPTOR, streamDesc.Get());
			if (FAILED(hr))
			{
				DebugLogf(L"[AudioPlayer] stream[%u] SetUnknown(STREAM_DESCRIPTOR) failed, hr=0x%08X\n", i, hr);
				continue;
			}

			hr = m_topology->AddNode(sourceNode.Get());
			if (FAILED(hr))
			{
				DebugLogf(L"[AudioPlayer] stream[%u] AddNode(sourceNode) failed, hr=0x%08X\n", i, hr);
				continue;
			}

			// 创建输出节点
			ComPtr<IMFTopologyNode> outputNode;
			hr = MFCreateTopologyNode(MF_TOPOLOGY_OUTPUT_NODE, &outputNode);
			if (FAILED(hr))
			{
				DebugLogf(L"[AudioPlayer] stream[%u] MFCreateTopologyNode(OUTPUT) failed, hr=0x%08X\n", i, hr);
				continue;
			}

			// 为音频创建音频渲染器
			ComPtr<IMFActivate> rendererActivate;
			hr = MFCreateAudioRendererActivate(&rendererActivate);
			if (FAILED(hr))
			{
				// 这一步失败最常见的原因：系统没有可用/默认的音频播放设备
				// （比如虚拟机没接声卡、或者所有播放设备都被禁用了）。
				DebugLogf(L"[AudioPlayer] stream[%u] MFCreateAudioRendererActivate failed, hr=0x%08X（很可能是没有可用的默认音频播放设备）\n", i, hr);
				continue;
			}

			hr = outputNode->SetObject(rendererActivate.Get());
			if (FAILED(hr))
			{
				DebugLogf(L"[AudioPlayer] stream[%u] outputNode->SetObject failed, hr=0x%08X\n", i, hr);
				continue;
			}

			hr = m_topology->AddNode(outputNode.Get());
			if (FAILED(hr))
			{
				DebugLogf(L"[AudioPlayer] stream[%u] AddNode(outputNode) failed, hr=0x%08X\n", i, hr);
				continue;
			}

			// 连接源节点和输出节点
			hr = sourceNode->ConnectOutput(0, outputNode.Get(), 0);
			if (FAILED(hr))
			{
				// 常见原因：这路流不是音频（比如视频流）却被接到了音频渲染器上，
				// 或者解码器缺失导致媒体类型协商失败。
				DebugLogf(L"[AudioPlayer] stream[%u] ConnectOutput failed, hr=0x%08X\n", i, hr);
				continue;
			}

			DebugLogf(L"[AudioPlayer] stream[%u] 连接成功\n", i);
			++connectedStreams;
		}

		if (connectedStreams == 0)
		{
			DebugLogf(L"[AudioPlayer] 没有任何一路流成功连接到渲染器，拓扑是空的\n");
			return false;
		}

		// 设置拓扑
		hr = mediaSession->SetTopology(MFSESSION_SETTOPOLOGY_IMMEDIATE, m_topology.Get());
		if (FAILED(hr))
		{
			DebugLogf(L"[AudioPlayer] SetTopology failed, hr=0x%08X\n", hr);
			if (hr == MF_E_TOPO_CODEC_NOT_FOUND)
			{
				LogAvailableMp3Decoders();
			}
		}
		return SUCCEEDED(hr);
	}

	std::wstring AudioPlayer::ResolveExistingFilePath(const std::wstring& path) const
	{
		if (FileExists(path))
			return path;

		if (!IsAbsoluteOrRootedPath(path))
		{
			std::wstring exeDir = GetExeDir();
			if (!exeDir.empty())
			{
				std::wstring candidate = exeDir + path;
				if (FileExists(candidate))
					return candidate;
			}
		}

		return L"";
	}

	bool AudioPlayer::WaitForSessionEvent(MediaEventType expectedType, int maxEventsToCheck)
	{
		if (!m_mediaSession)
			return false;

		for (int i = 0; i < maxEventsToCheck; ++i)
		{
			ComPtr<IMFMediaEvent> event;
			// GetEvent(0, ...) 会同步阻塞直到下一个事件到达（0 表示不带
			// MF_EVENT_FLAG_NO_WAIT）。正常情况下 MESessionStarted 几十
			// 毫秒内就会来，不会明显卡住界面；这是为了简单直接换来"能
			// 确认播放到底成没成功"，属于有意的取舍，不是长期最佳实践
			// （更完整的做法是用 IMFAsyncCallback 完全异步处理）。
			HRESULT hr = m_mediaSession->GetEvent(0, &event);
			if (FAILED(hr) || !event)
			{
				DebugLogf(L"[AudioPlayer] GetEvent failed, hr=0x%08X\n", hr);
				return false;
			}

			MediaEventType type = MEUnknown;
			event->GetType(&type);

			HRESULT eventStatus = S_OK;
			event->GetStatus(&eventStatus);

			if (type == MEError || FAILED(eventStatus))
			{
				DebugLogf(L"[AudioPlayer] 会话事件报告失败：type=%d, status=0x%08X\n", (int)type, eventStatus);
				return false;
			}

			if (type == expectedType)
			{
				return true;
			}
			// 其它无关事件（比如拓扑状态变化）直接忽略，继续等下一个。
		}

		DebugLogf(L"[AudioPlayer] 等了 %d 个事件还没等到期望的事件(type=%d)，按失败处理\n", maxEventsToCheck, (int)expectedType);
		return false;
	}

	void AudioPlayer::ResetForNewTrack()
	{
		// 只清掉"跟上一首歌绑定"的那些对象——媒体源、拓扑、音量接口，
		// 不动 m_mediaSession。IMFMediaSession 本来就是设计成可以反复
		// SetTopology/Start 来播放不同文件的，不需要每次播放新文件都
		// 把整个会话关掉重建。
		if (m_mediaSource)
		{
			m_mediaSource->Shutdown();
			m_mediaSource.Reset();
		}
		m_topology.Reset();
		m_audioVolume.Reset();
		m_playbackState = PlaybackState::Stopped;
		m_duration = 0;
	}

	void AudioPlayer::Cleanup()
	{
		// 真正的、完整的资源释放，只应该在对象销毁时调用一次。
		// ------------------------------------------------------------
		// 重点修复：这里之前被 Play() 在"每次播放新文件"时也调用了一遍
		// （Play() 的第一行就是 Cleanup()）。而 m_mediaSession 只在
		// Initialize() 里创建过一次，Cleanup() 却会把它 Close()+Reset()
		// 成空指针——也就是说，从第一次点击播放开始，Play() 函数自己
		// 先把 m_mediaSession 干掉了，然后函数后面还在用这个已经是空
		// 指针的 m_mediaSession 去 CreateTopology、去 Start()。
		// 这正是"点了播放、按钮看起来变成暂停图标，但完全没有声音"的
		// 根本原因。现在 Play() 改成调用 ResetForNewTrack()（不动
		// m_mediaSession），真正的 Cleanup() 只在析构时调用一次。
		// ------------------------------------------------------------
		if (m_mediaSource)
		{
			m_mediaSource->Shutdown();
		}
		if (m_mediaSession)
		{
			m_mediaSession->Close();
			m_mediaSession.Reset();
		}
		m_mediaSource.Reset();
		m_topology.Reset();
		m_audioVolume.Reset();
		m_playbackState = PlaybackState::Stopped;
		m_duration = 0;
	}
}
