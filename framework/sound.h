// sound.h
#pragma once

#include <xaudio2.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <string>

#pragma comment(lib, "xaudio2.lib")
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "mfreadwrite.lib")

// サウンドデータ構造体
struct SoundData {
    IXAudio2SourceVoice* pSourceVoice = nullptr;
    IMFSourceReader* pReader = nullptr;
    WAVEFORMATEX* pWfx = nullptr;
    BYTE* pBuffer = nullptr;
    UINT32 bufferSize = 0;
    bool loop = false;
    bool isBGM = false;
    std::wstring path;
    UINT64 startSamples = 0;
    double seekSec = 0.0;
};

// 初期化・終了
void InitSound();
void UninitSound();

// MP3読み込み・解放
SoundData* LoadMP3(const wchar_t* filename);
SoundData* LoadMP3(const std::string& filename);
void UnloadSound(SoundData* data);

// 再生・停止
void PlaySound(SoundData* data, bool loop = false, float volumeScale = 1.0f, float startSec = 0.0f);
void StopSound(SoundData* data);

// 再生位置取得（XAudio2 SamplesPlayed ベース、単位：秒）
double GetPlaybackPositionSec(const SoundData* data);

//音量
void SetMasterVolume(float volume);

#include "scene.h"
void ReleaseSoundsForScene(SCENE scene);
void ReleaseAllSounds();
void UpdateSoundCache();
