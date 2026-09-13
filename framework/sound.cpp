// sound.cpp
#include "sound.h"
#include "define.h"
#include <vector>
#include <Windows.h>
#include <algorithm>
#include <cctype>
#include <cwctype>

namespace
{
    bool ContainsTokenLower(const std::string& text, const std::string& token)
    {
        return text.find(token) != std::string::npos;
    }

    bool IsBGMPath(const std::string& filename)
    {
        std::string lower = filename;
        std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) {
            return static_cast<char>(tolower(c));
        });
        return ContainsTokenLower(lower, "\\bgm\\") || ContainsTokenLower(lower, "/bgm/") ||
               ContainsTokenLower(lower, "\\music\\") || ContainsTokenLower(lower, "/music/") ||
               ContainsTokenLower(lower, "\\score\\") || ContainsTokenLower(lower, "/score/");
    }

    bool IsBGMPath(const wchar_t* filename)
    {
        if (!filename) return false;
        std::wstring lower = filename;
        std::transform(lower.begin(), lower.end(), lower.begin(), [](wchar_t c) {
            return static_cast<wchar_t>(towlower(c));
        });
        return (lower.find(L"\\bgm\\") != std::wstring::npos) ||
            (lower.find(L"/bgm/") != std::wstring::npos) ||
            (lower.find(L"\\music\\") != std::wstring::npos) ||
            (lower.find(L"/music/") != std::wstring::npos) ||
            (lower.find(L"\\score\\") != std::wstring::npos) ||
            (lower.find(L"/score/") != std::wstring::npos);
    }

    float ClampVolume(float volume)
    {
        if (volume < 0.0f) return 0.0f;
        if (volume > 1.0f) return 1.0f;
        return volume;
    }
}

#include "scene.h"
#include <unordered_map>
#include "debug_ostream.h"

// グローバルなBGMサウンドキャッシュのエントリー
struct GlobalSoundEntry {
	SoundData* data = nullptr;
	int refCount = 0;
};
static std::unordered_map<std::wstring, GlobalSoundEntry> g_GlobalSoundMap;

// シーンごとのBGMサウンドキャッシュ
static std::unordered_map<std::wstring, SoundData*> g_SoundCache[SCENE_MAX];

// Helper to convert wstring path to string for logging
static std::string SoundPathToString(const wchar_t* wstr)
{
	if (!wstr) return "";
	std::string str;
	for (int i = 0; wstr[i] != L'\0'; ++i) str += static_cast<char>(wstr[i]);
	return str;
}

// グローバル変数
static IXAudio2* g_pXAudio2 = nullptr;
static IXAudio2MasteringVoice* g_pMasterVoice = nullptr;

// SafeRelease
template<class T> void SafeRelease(T** pp) {
    if (*pp) {
        (*pp)->Release();
        *pp = nullptr;
    }
}

// 初期化
void InitSound() {
    HRESULT temp = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    MFStartup(MF_VERSION);
    XAudio2Create(&g_pXAudio2, 0, XAUDIO2_DEFAULT_PROCESSOR);
    g_pXAudio2->CreateMasteringVoice(&g_pMasterVoice);
}

// 終了
void UninitSound() {
    if (g_pMasterVoice) {
        g_pMasterVoice->DestroyVoice();
        g_pMasterVoice = nullptr;
    }
    SafeRelease(&g_pXAudio2);
    MFShutdown();
    CoUninitialize();
}

// プロトタイプ宣言
static void DestroySoundData(SoundData* data);

// MP3読み込み
SoundData* LoadMP3(const wchar_t* filename) {
	std::wstring path = filename;
	std::string pathStr = SoundPathToString(filename);
	bool isBGM = IsBGMPath(filename);

	// ファイル名（小文字）の抽出
	size_t lastSlash = path.find_last_of(L"\\/");
	std::wstring fileName = (lastSlash == std::wstring::npos) ? path : path.substr(lastSlash + 1);
	std::transform(fileName.begin(), fileName.end(), fileName.begin(), ::towlower);

	// ファイルサイズの取得 (Windows API)
	LONGLONG fileSize = 0;
	WIN32_FILE_ATTRIBUTE_DATA fad;
	if (GetFileAttributesExW(filename, GetFileExInfoStandard, &fad)) {
		LARGE_INTEGER size;
		size.LowPart = fad.nFileSizeLow;
		size.HighPart = fad.nFileSizeHigh;
		fileSize = size.QuadPart;
	}

	std::wstring cacheKey = fileName + L"_" + std::to_wstring(fileSize);

	if (isBGM)
	{
		SCENE currentScene = GetScene();
		if (currentScene >= 0 && currentScene < SCENE_MAX)
		{
			auto globIt = g_GlobalSoundMap.find(cacheKey);
			if (globIt != g_GlobalSoundMap.end())
			{
				SoundData* cachedData = globIt->second.data;
				
				auto sceneIt = g_SoundCache[currentScene].find(cacheKey);
				if (sceneIt == g_SoundCache[currentScene].end())
				{
					g_SoundCache[currentScene][cacheKey] = cachedData;
					globIt->second.refCount++;
					hal::dout << "[Sound Cache] SCENE " << currentScene << " BGM HIT (New scene ref): " << pathStr << " | refCount: " << globIt->second.refCount << std::endl;
				}
				else
				{
					hal::dout << "[Sound Cache] SCENE " << currentScene << " BGM HIT (Already ref): " << pathStr << " | refCount: " << globIt->second.refCount << std::endl;
				}
				return cachedData;
			}
		}
	}

	SoundData* data = new SoundData();
	data->isBGM = isBGM;
	data->path = path;

	// SourceReader作成
	HRESULT hr = MFCreateSourceReaderFromURL(filename, NULL, &data->pReader);
	if (FAILED(hr)) {
		delete data;
		return nullptr;
	}

	// オーディオストリームのみ選択
	hr = data->pReader->SetStreamSelection((DWORD)MF_SOURCE_READER_ALL_STREAMS, FALSE);
	if (SUCCEEDED(hr)) {
		hr = data->pReader->SetStreamSelection((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, TRUE);
	}
	if (FAILED(hr)) {
		DestroySoundData(data);
		return nullptr;
	}

	// PCM形式に設定
	IMFMediaType* pPartialType = nullptr;
	hr = MFCreateMediaType(&pPartialType);
	if (FAILED(hr)) {
		DestroySoundData(data);
		return nullptr;
	}
	pPartialType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
	pPartialType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
	hr = data->pReader->SetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, NULL, pPartialType);
	SafeRelease(&pPartialType);
	if (FAILED(hr)) {
		DestroySoundData(data);
		return nullptr;
	}

	// WAVEFORMAT取得
	IMFMediaType* pType = nullptr;
	hr = data->pReader->GetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, &pType);
	if (FAILED(hr) || !pType) {
		DestroySoundData(data);
		return nullptr;
	}
	UINT32 wfxSize = 0;
	hr = MFCreateWaveFormatExFromMFMediaType(pType, &data->pWfx, &wfxSize);
	SafeRelease(&pType);
	if (FAILED(hr) || !data->pWfx) {
		DestroySoundData(data);
		return nullptr;
	}

	// 全サンプル読み込み
	std::vector<BYTE> audioData;
	while (true) {
		DWORD streamFlags = 0;
		IMFSample* pSample = nullptr;
		hr = data->pReader->ReadSample(
			(DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM,
			0, NULL, &streamFlags, NULL, &pSample);

		if (FAILED(hr) || (streamFlags & MF_SOURCE_READERF_ENDOFSTREAM)) {
			if (pSample) pSample->Release();
			break;
		}

		if (pSample) {
			IMFMediaBuffer* pBuffer = nullptr;
			hr = pSample->ConvertToContiguousBuffer(&pBuffer);
			if (SUCCEEDED(hr) && pBuffer) {
				BYTE* pAudioData = nullptr;
				DWORD cbBuffer = 0;
				hr = pBuffer->Lock(&pAudioData, NULL, &cbBuffer);
				if (SUCCEEDED(hr) && pAudioData) {
					audioData.insert(audioData.end(), pAudioData, pAudioData + cbBuffer);
					pBuffer->Unlock();
				}
				SafeRelease(&pBuffer);
			}
			pSample->Release();
		}
	}

	// バッファコピー
	data->bufferSize = (UINT32)audioData.size();
	data->pBuffer = new BYTE[data->bufferSize];
	memcpy(data->pBuffer, audioData.data(), data->bufferSize);

	// SourceVoice作成
	hr = g_pXAudio2->CreateSourceVoice(&data->pSourceVoice, data->pWfx);
	if (FAILED(hr)) {
		DestroySoundData(data);
		return nullptr;
	}

	if (isBGM)
	{
		SCENE currentScene = GetScene();
		if (currentScene >= 0 && currentScene < SCENE_MAX)
		{
			g_GlobalSoundMap[cacheKey] = { data, 1 };
			g_SoundCache[currentScene][cacheKey] = data;
			hal::dout << "[Sound Cache] SCENE " << currentScene << " BGM MISS (Loaded & Registered): " << pathStr << " | refCount: 1" << std::endl;
		}
	}

	return data;
}

// MP3読み込み (string版)
SoundData* LoadMP3(const std::string& filename) {
    // stringからwstringへ変換
    int size = MultiByteToWideChar(CP_UTF8, 0, filename.c_str(), -1, NULL, 0);
    std::wstring wfilename(size - 1, 0);
    MultiByteToWideChar(CP_UTF8, 0, filename.c_str(), -1, &wfilename[0], size);
    SoundData* data = LoadMP3(wfilename.c_str());
    if (data)
    {
        data->isBGM = IsBGMPath(filename);
    }
    return data;
}

// 実際のサウンドデータの破棄を行うヘルパー関数
static void DestroySoundData(SoundData* data) {
	if (!data) return;
	if (data->pSourceVoice) {
		data->pSourceVoice->Stop();
		data->pSourceVoice->DestroyVoice();
	}
	SafeRelease(&data->pReader);
	if (data->pWfx) {
		CoTaskMemFree(data->pWfx);
	}
	if (data->pBuffer) {
		delete[] data->pBuffer;
	}
	delete data;
}

// サウンド解放
void UnloadSound(SoundData* data) {
	if (!data) return;

	if (data->isBGM)
	{
		std::wstring path = data->path;
		std::string pathStr = SoundPathToString(path.c_str());
		SCENE currentScene = GetScene();

		hal::dout << "[Sound Cache] UnloadSound called for BGM: " << pathStr << std::endl;

		if (data->pSourceVoice) {
			data->pSourceVoice->Stop();
			data->pSourceVoice->FlushSourceBuffers();
		}

		// キャッシュキーの生成
		size_t lastSlash = path.find_last_of(L"\\/");
		std::wstring fileName = (lastSlash == std::wstring::npos) ? path : path.substr(lastSlash + 1);
		std::transform(fileName.begin(), fileName.end(), fileName.begin(), ::towlower);

		LONGLONG fileSize = 0;
		WIN32_FILE_ATTRIBUTE_DATA fad;
		if (GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fad)) {
			LARGE_INTEGER size;
			size.LowPart = fad.nFileSizeLow;
			size.HighPart = fad.nFileSizeHigh;
			fileSize = size.QuadPart;
		}
		std::wstring cacheKey = fileName + L"_" + std::to_wstring(fileSize);

		if (currentScene >= 0 && currentScene < SCENE_MAX)
		{
			auto sceneIt = g_SoundCache[currentScene].find(cacheKey);
			if (sceneIt != g_SoundCache[currentScene].end())
			{
				g_SoundCache[currentScene].erase(sceneIt);

				auto globIt = g_GlobalSoundMap.find(cacheKey);
				if (globIt != g_GlobalSoundMap.end())
				{
					globIt->second.refCount--;
					hal::dout << "[Sound Cache] DecRef BGM via UnloadSound: " << pathStr << " | Remaining refCount: " << globIt->second.refCount << std::endl;

					if (globIt->second.refCount <= 0)
					{
						hal::dout << "[Sound Cache] Destroying BGM via UnloadSound: " << pathStr << std::endl;
						DestroySoundData(globIt->second.data);
						g_GlobalSoundMap.erase(globIt);
					}
				}
			}
		}
		return;
	}

	DestroySoundData(data);
}

void ReleaseSoundsForScene(SCENE scene) {
	if (scene < 0 || scene >= SCENE_MAX) return;

	hal::dout << "[Sound Cache] --- Releasing Scene " << scene << " Sounds ---" << std::endl;

	for (auto& pair : g_SoundCache[scene]) {
		std::wstring path = pair.first;
		std::string pathStr = SoundPathToString(path.c_str());

		auto globIt = g_GlobalSoundMap.find(path);
		if (globIt != g_GlobalSoundMap.end()) {
			globIt->second.refCount--;
			hal::dout << "  DecRef BGM: " << pathStr << " | Remaining refCount: " << globIt->second.refCount << std::endl;
			
			if (globIt->second.refCount <= 0) {
				hal::dout << "  Destroying BGM: " << pathStr << std::endl;
				DestroySoundData(globIt->second.data);
				g_GlobalSoundMap.erase(globIt);
			}
		}
	}
	g_SoundCache[scene].clear();
}

void ReleaseAllSounds() {
	hal::dout << "[Sound Cache] --- Releasing All Sounds (Shutdown) ---" << std::endl;
	for (int i = 0; i < SCENE_MAX; ++i) {
		ReleaseSoundsForScene(static_cast<SCENE>(i));
	}

	// 安全のため、残っているグローバル BGM があれば強制解放
	for (auto& pair : g_GlobalSoundMap) {
		if (pair.second.data) {
			hal::dout << "[Sound Cache] Force Destroying leftover BGM: " << SoundPathToString(pair.first.c_str()) << std::endl;
			DestroySoundData(pair.second.data);
		}
	}
	g_GlobalSoundMap.clear();
}

void UpdateSoundCache() {
	SCENE currentScene = GetScene();
	static SCENE prevScene = SCENE_NONE;
	if (currentScene != prevScene) {
		if (prevScene >= 0 && prevScene < SCENE_MAX) {
			ReleaseSoundsForScene(prevScene);
		}
		prevScene = currentScene;
	}
}

// 再生
void PlaySound(SoundData* data, bool loop, float volumeScale, float startSec) {
    if (!data || !data->pSourceVoice) return;

    data->loop = loop;
    data->pSourceVoice->Stop();
    data->pSourceVoice->FlushSourceBuffers();

    const float volume = (data->isBGM ? SOUND_BGM_VOLUME : SOUND_SE_VOLUME) * volumeScale;
    data->pSourceVoice->SetVolume(ClampVolume(volume));

    XAUDIO2_BUFFER buffer = { 0 };
    buffer.AudioBytes = data->bufferSize;
    buffer.pAudioData = data->pBuffer;
    buffer.Flags = XAUDIO2_END_OF_STREAM;
    if (loop) {
        buffer.LoopCount = XAUDIO2_LOOP_INFINITE;
    }

    data->seekSec = 0.0;

    if (startSec > 0.0f && data->pWfx) {
        UINT32 startSample = static_cast<UINT32>(startSec * data->pWfx->nSamplesPerSec);
        UINT32 totalSamples = data->bufferSize / (data->pWfx->nBlockAlign ? data->pWfx->nBlockAlign : 1);
        if (startSample < totalSamples) {
            buffer.PlayBegin = startSample;
            data->seekSec = static_cast<double>(startSample) / data->pWfx->nSamplesPerSec;
        }
    }

    XAUDIO2_VOICE_STATE voiceState = {};
    data->pSourceVoice->GetState(&voiceState);
    data->startSamples = voiceState.SamplesPlayed;

    data->pSourceVoice->SubmitSourceBuffer(&buffer);
    data->pSourceVoice->Start(0);
}

// 停止
void StopSound(SoundData* data) {
    if (!data || !data->pSourceVoice) return;

    data->pSourceVoice->Stop();
    data->pSourceVoice->FlushSourceBuffers();
}

// 再生位置取得（XAudio2 ハードウェアカウンタ基準、単位：秒）
double GetPlaybackPositionSec(const SoundData* data)
{
    if (!data || !data->pSourceVoice || !data->pWfx) return 0.0;
    XAUDIO2_VOICE_STATE state = {};
    data->pSourceVoice->GetState(&state);
    
    double elapsed = 0.0;
    if (state.SamplesPlayed >= data->startSamples) {
        elapsed = static_cast<double>(state.SamplesPlayed - data->startSamples) / data->pWfx->nSamplesPerSec;
    }
    return elapsed + data->seekSec;
}

// マスターボリューム設定
void SetMasterVolume(float volume)
{
	if (g_pMasterVoice)
	{
		// 0.0 ~ 1.0 の範囲にクランプ
		if (volume < 0.0f) volume = 0.0f;
		if (volume > 1.0f) volume = 1.0f;
		g_pMasterVoice->SetVolume(volume);
	}
}