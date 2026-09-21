#include "AudioPlayer.h"
#include <cassert>

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "wmcodecdspuuid.lib")
#pragma comment(lib, "mf.lib")

namespace YuMediaPlayer
{
	AudioPlayer::AudioPlayer()
		: m_playbackState(PlaybackState::Stopped)
		, m_duration(0)
	{
	}

	AudioPlayer::~AudioPlayer()
	{
		Cleanup();
		MFShutdown();
	}

	bool AudioPlayer::Initialize()
	{
		HRESULT hr = MFStartup(MF_VERSION);
		if (FAILED(hr))
		{
			OutputDebugStringW(L"MFStartup failed\n");
			return false;
		}

		hr = MFCreateMediaSession(nullptr, &m_mediaSession);
		if (FAILED(hr))
		{
			OutputDebugStringW(L"MFCreateMediaSession failed\n");
			MFShutdown();
			return false;
		}

		return true;
	}

	bool AudioPlayer::Play(const std::wstring& filePath)
	{
		Cleanup();

		HRESULT hr = S_OK;

		// 创建媒体源
		ComPtr<IMFSourceResolver> sourceResolver;
		hr = MFCreateSourceResolver(&sourceResolver);
		if (FAILED(hr))
		{
			OutputDebugStringW(L"MFCreateSourceResolver failed\n");
			return false;
		}

		MF_OBJECT_TYPE objectType = MF_OBJECT_INVALID;
		hr = sourceResolver->CreateObjectFromURL(
			filePath.c_str(),
			MF_RESOLUTION_MEDIASOURCE | MF_RESOLUTION_READ,
			nullptr,
			&objectType,
			(IUnknown**)&m_mediaSource
		);

		if (FAILED(hr))
		{
			OutputDebugStringW(L"CreateObjectFromURL failed\n");
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
			OutputDebugStringW(L"MediaSession Start failed\n");
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

		if (SUCCEEDED(hr))
		{
			m_playbackState = PlaybackState::Playing;
			return true;
		}
		return false;
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
			return false;

		// 创建源节点
		ComPtr<IMFPresentationDescriptor> presentationDesc;
		hr = mediaSource->CreatePresentationDescriptor(&presentationDesc);
		if (FAILED(hr))
			return false;

		DWORD streamCount = 0;
		presentationDesc->GetStreamDescriptorCount(&streamCount);

		for (DWORD i = 0; i < streamCount; i++)
		{
			BOOL selected = FALSE;
			ComPtr<IMFStreamDescriptor> streamDesc;
			hr = presentationDesc->GetStreamDescriptorByIndex(i, &selected, &streamDesc);
			if (FAILED(hr))
				continue;

			if (!selected)
				continue;

			// 创建源节点
			ComPtr<IMFTopologyNode> sourceNode;
			hr = MFCreateTopologyNode(MF_TOPOLOGY_SOURCESTREAM_NODE, &sourceNode);
			if (FAILED(hr))
				continue;

			hr = sourceNode->SetUnknown(MF_TOPONODE_SOURCE, mediaSource);
			if (FAILED(hr))
				continue;

			hr = sourceNode->SetUnknown(MF_TOPONODE_STREAM_DESCRIPTOR, streamDesc.Get());
			if (FAILED(hr))
				continue;

			m_topology->AddNode(sourceNode.Get());

			// 创建输出节点
			ComPtr<IMFTopologyNode> outputNode;
			hr = MFCreateTopologyNode(MF_TOPOLOGY_OUTPUT_NODE, &outputNode);
			if (FAILED(hr))
				continue;

			// 为音频创建音频渲染器
			ComPtr<IMFActivate> rendererActivate;
			hr = MFCreateAudioRendererActivate(&rendererActivate);
			if (FAILED(hr))
				continue;

			hr = outputNode->SetObject(rendererActivate.Get());
			if (FAILED(hr))
				continue;

			m_topology->AddNode(outputNode.Get());

			// 连接源节点和输出节点
			hr = sourceNode->ConnectOutput(0, outputNode.Get(), 0);
			if (FAILED(hr))
				continue;
		}

		// 设置拓扑
		hr = mediaSession->SetTopology(MFSESSION_SETTOPOLOGY_IMMEDIATE, m_topology.Get());
		return SUCCEEDED(hr);
	}

	void AudioPlayer::Cleanup()
	{
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
