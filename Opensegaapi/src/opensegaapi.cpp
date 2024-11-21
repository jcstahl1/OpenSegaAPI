/*
* This file is part of the OpenParrot project - https://teknoparrot.com / https://github.com/teknogods
*
* See LICENSE and MENTIONS in the root of the source tree for information
* regarding licensing.
 */
  
 #include "opensegaapi.h"
 #include <Windows.h>
 #include <math.h>
 #include <map>
 
 static FAudio* pFAudio = nullptr;
 static FAudioMasteringVoice* pMasterVoice = nullptr;
 static std::map<void*, FAudioSourceVoice*> sourceVoices;
 static OPEN_SEGASTATUS lastStatus = OPEN_SEGA_SUCCESS;
 static bool initialized = false;
 
struct VoiceState {
    uint32_t loopStart;
    uint32_t loopEnd;
    uint32_t endOffset;
    bool looping;
    FAudioWaveFormatEx format;
    float volume;
    float pitch;
    OPEN_HAWOSEGABUFFERCALLBACK callback;
    void* context;
};
 
static std::map<void*, VoiceState> voiceStates;
 
OPEN_SEGASTATUS SEGAAPI_Init(void)
{
     if (initialized) {
         return OPEN_SEGA_SUCCESS;
     }
 
     HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
     if (FAILED(hr)) {
         lastStatus = OPEN_SEGAERR_FAIL;
         return lastStatus;
     }

    uint32_t flags = 0;
    if (FAudioCreate(&pFAudio, flags, FAUDIO_DEFAULT_PROCESSOR) != 0) {
        CoUninitialize();
        lastStatus = OPEN_SEGAERR_FAIL;
        return lastStatus;
    }

    if (FAudioCreateMasteringVoice(pFAudio, &pMasterVoice, FAUDIO_DEFAULT_CHANNELS,
        FAUDIO_DEFAULT_SAMPLERATE, 0, 0, nullptr) != 0) {
        FAudio_Release(pFAudio);
        pFAudio = nullptr;
        CoUninitialize();
        lastStatus = OPEN_SEGAERR_FAIL;
        return lastStatus;
    }

    initialized = true;
    lastStatus = OPEN_SEGA_SUCCESS;
    return lastStatus;
}

OPEN_SEGASTATUS SEGAAPI_Exit(void)
{
    if (!initialized) {
        return OPEN_SEGA_SUCCESS;
    }

    for (auto& voice : sourceVoices) {
        if (voice.second) {
            FAudioSourceVoice_Stop(voice.second, 0, FAUDIO_COMMIT_NOW);
            FAudioSourceVoice_FlushSourceBuffers(voice.second);
            FAudioVoice_DestroyVoice(voice.second);
        }
    }
    sourceVoices.clear();
    voiceStates.clear();
 
    if (pMasterVoice) {
        FAudioVoice_DestroyVoice(pMasterVoice);
        pMasterVoice = nullptr;
    }

    if (pFAudio) {
        FAudio_Release(pFAudio);
        pFAudio = nullptr;
    }
 
    CoUninitialize();
    initialized = false;
    lastStatus = OPEN_SEGA_SUCCESS;
    return lastStatus;
}

OPEN_SEGASTATUS SEGAAPI_CreateBuffer(OPEN_HAWOSEBUFFERCONFIG* pConfig, 
    OPEN_HAWOSEGABUFFERCALLBACK pCallback, unsigned int dwFlags, void** phHandle)
{
    if (!pConfig || !phHandle || !pFAudio) {
        lastStatus = OPEN_SEGAERR_BAD_POINTER;
        return lastStatus;
    }

    VoiceState state = {};
    state.format.wFormatTag = 1;
    state.format.nChannels = pConfig->byNumChans;
    state.format.nSamplesPerSec = pConfig->dwSampleRate;
    state.format.wBitsPerSample = (pConfig->dwSampleFormat == OPEN_HASF_UNSIGNED_8PCM) ? 8 : 16;
    state.format.nBlockAlign = (state.format.wBitsPerSample / 8) * state.format.nChannels;
    state.format.nAvgBytesPerSec = state.format.nSamplesPerSec * state.format.nBlockAlign;
    state.volume = 1.0f;
    state.pitch = 1.0f;
    state.callback = pCallback;
    state.context = nullptr;

    FAudioSourceVoice* sourceVoice;
    if (FAudio_CreateSourceVoice(pFAudio, &sourceVoice, &state.format, 0, 2.0f, nullptr, nullptr, nullptr) != 0) {
        lastStatus = OPEN_SEGAERR_FAIL;
        return lastStatus;
    }

    *phHandle = sourceVoice;
    sourceVoices[*phHandle] = sourceVoice;
    voiceStates[*phHandle] = state;

    lastStatus = OPEN_SEGA_SUCCESS;
    return lastStatus;
}

OPEN_SEGASTATUS SEGAAPI_DestroyBuffer(void* hHandle)
{
    auto it = sourceVoices.find(hHandle);
    if (it == sourceVoices.end()) {
        lastStatus = OPEN_SEGAERR_BAD_HANDLE;
        return lastStatus;
    }

    FAudioVoice_DestroyVoice(it->second);
    sourceVoices.erase(it);
    voiceStates.erase(hHandle);

    lastStatus = OPEN_SEGA_SUCCESS;
    return lastStatus;
}

OPEN_SEGASTATUS SEGAAPI_Play(void* hHandle)
{
    auto it = sourceVoices.find(hHandle);
    if (it == sourceVoices.end()) {
        lastStatus = OPEN_SEGAERR_BAD_HANDLE;
        return lastStatus;
    }

    if (FAudioSourceVoice_Start(it->second, 0, FAUDIO_COMMIT_NOW) != 0) {
        lastStatus = OPEN_SEGAERR_FAIL;
        return lastStatus;
    }

    lastStatus = OPEN_SEGA_SUCCESS;
    return lastStatus;
}

OPEN_SEGASTATUS SEGAAPI_Stop(void* hHandle)
{
    auto it = sourceVoices.find(hHandle);
    if (it == sourceVoices.end()) {
        lastStatus = OPEN_SEGAERR_BAD_HANDLE;
        return lastStatus;
    }

    if (FAudioSourceVoice_Stop(it->second, 0, FAUDIO_COMMIT_NOW) != 0) {
        lastStatus = OPEN_SEGAERR_FAIL;
        return lastStatus;
    }

    lastStatus = OPEN_SEGA_SUCCESS;
    return lastStatus;
}

OPEN_SEGASTATUS SEGAAPI_Pause(void* hHandle)
{
    return SEGAAPI_Stop(hHandle);
}

OPEN_HAWOSTATUS SEGAAPI_GetPlaybackStatus(void* hHandle)
{
    auto it = sourceVoices.find(hHandle);
    if (it == sourceVoices.end()) {
        return OPEN_HAWOSTATUS_INVALID;
    }

    FAudioVoiceState state;
    FAudioSourceVoice_GetState(it->second, &state, 0);

    if (state.BuffersQueued > 0) {
        return OPEN_HAWOSTATUS_ACTIVE;
    }
    return OPEN_HAWOSTATUS_STOP;
}
 
OPEN_SEGASTATUS SEGAAPI_UpdateBuffer(void* hHandle, unsigned int dwStartOffset, unsigned int dwLength)
{
    auto it = sourceVoices.find(hHandle);
    auto stateIt = voiceStates.find(hHandle);
    if (it == sourceVoices.end() || stateIt == voiceStates.end()) {
        lastStatus = OPEN_SEGAERR_BAD_HANDLE;
        return lastStatus;
    }
    FAudioBuffer buffer = {};
    buffer.AudioBytes = dwLength;
    buffer.pAudioData = (const uint8_t*)dwStartOffset;
    buffer.LoopBegin = stateIt->second.loopStart;
    buffer.LoopLength = stateIt->second.loopEnd - stateIt->second.loopStart;
    buffer.LoopCount = stateIt->second.looping ? FAUDIO_LOOP_INFINITE : 0;

    if (FAudioSourceVoice_SubmitSourceBuffer(it->second, &buffer, nullptr) != 0) {
        lastStatus = OPEN_SEGAERR_FAIL;
        return lastStatus;
    }

    lastStatus = OPEN_SEGA_SUCCESS;
    return lastStatus;
}

OPEN_SEGASTATUS SEGAAPI_SetFormat(void* hHandle, OPEN_HAWOSEFORMAT* pFormat)
{
    auto it = sourceVoices.find(hHandle);
    auto stateIt = voiceStates.find(hHandle);
    if (it == sourceVoices.end() || stateIt == voiceStates.end() || !pFormat) {
        lastStatus = OPEN_SEGAERR_BAD_HANDLE;
        return lastStatus;
    }

    FAudioWaveFormatEx wfx = {};
    wfx.wFormatTag = 1;
    wfx.nChannels = pFormat->byNumChans;
    wfx.nSamplesPerSec = pFormat->dwSampleRate;
    wfx.wBitsPerSample = (pFormat->dwSampleFormat == OPEN_HASF_UNSIGNED_8PCM) ? 8 : 16;
    wfx.nBlockAlign = (wfx.wBitsPerSample / 8) * wfx.nChannels;
    wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign;

    FAudioVoice_DestroyVoice(it->second);
    
    if (FAudio_CreateSourceVoice(pFAudio, &it->second, &wfx, 0, 2.0f, nullptr, nullptr, nullptr) != 0) {
        lastStatus = OPEN_SEGAERR_FAIL;
        return lastStatus;
    }

    stateIt->second.format = wfx;
    lastStatus = OPEN_SEGA_SUCCESS;
    return lastStatus;
}

OPEN_SEGASTATUS SEGAAPI_GetFormat(void* hHandle, OPEN_HAWOSEFORMAT* pFormat)
{
    auto stateIt = voiceStates.find(hHandle);
    if (stateIt == voiceStates.end() || !pFormat) {
        lastStatus = OPEN_SEGAERR_BAD_HANDLE;
        return lastStatus;
    }

    pFormat->byNumChans = stateIt->second.format.nChannels;
    pFormat->dwSampleRate = stateIt->second.format.nSamplesPerSec;
    pFormat->dwSampleFormat = (stateIt->second.format.wBitsPerSample == 8) ?
        OPEN_HASF_UNSIGNED_8PCM : OPEN_HASF_SIGNED_16PCM;

    lastStatus = OPEN_SEGA_SUCCESS;
    return lastStatus;
}

OPEN_SEGASTATUS SEGAAPI_SetSampleRate(void* hHandle, unsigned int dwSampleRate)
{
    auto it = sourceVoices.find(hHandle);
    auto stateIt = voiceStates.find(hHandle);
    if (it == sourceVoices.end() || stateIt == voiceStates.end()) {
        lastStatus = OPEN_SEGAERR_BAD_HANDLE;
        return lastStatus;
    }

    FAudioWaveFormatEx wfx = stateIt->second.format;
    wfx.nSamplesPerSec = dwSampleRate;
    wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign;

    FAudioVoice_DestroyVoice(it->second);
    
    if (FAudio_CreateSourceVoice(pFAudio, &it->second, &wfx, 0, 2.0f, nullptr, nullptr, nullptr) != 0) {
        lastStatus = OPEN_SEGAERR_FAIL;
        return lastStatus;
    }

    stateIt->second.format = wfx;
    lastStatus = OPEN_SEGA_SUCCESS;
    return lastStatus;
}
unsigned int SEGAAPI_GetSampleRate(void* hHandle)
{
    auto stateIt = voiceStates.find(hHandle);
    if (stateIt == voiceStates.end()) {
        return 0;
    }

    return stateIt->second.format.nSamplesPerSec;
}

OPEN_SEGASTATUS SEGAAPI_SetPriority(void* hHandle, unsigned int dwPriority)
{
    lastStatus = OPEN_SEGA_SUCCESS;
    return lastStatus;
}

unsigned int SEGAAPI_GetPriority(void* hHandle)
{
    return 0;
}

OPEN_SEGASTATUS SEGAAPI_SetUserData(void* hHandle, void* hUserData)
{
    auto stateIt = voiceStates.find(hHandle);
    if (stateIt == voiceStates.end()) {
        lastStatus = OPEN_SEGAERR_BAD_HANDLE;
        return lastStatus;
    }

    stateIt->second.context = hUserData;
    lastStatus = OPEN_SEGA_SUCCESS;
    return lastStatus;
}

void* SEGAAPI_GetUserData(void* hHandle)
{
    auto stateIt = voiceStates.find(hHandle);
    if (stateIt == voiceStates.end()) {
        return nullptr;
    }

    return stateIt->second.context;
}

OPEN_SEGASTATUS SEGAAPI_SetChannelVolume(void* hHandle, unsigned int dwChannel, unsigned int dwVolume)
{
    auto it = sourceVoices.find(hHandle);
    auto stateIt = voiceStates.find(hHandle);
    if (it == sourceVoices.end() || stateIt == voiceStates.end()) {
        lastStatus = OPEN_SEGAERR_BAD_HANDLE;
        return lastStatus;
    }

    float volume = dwVolume / (float)OPEN_HAWOSEVOL_MAX;
    if (FAudioVoice_SetVolume(it->second, volume, FAUDIO_COMMIT_NOW) != 0) {
        lastStatus = OPEN_SEGAERR_FAIL;
        return lastStatus;
    }

    stateIt->second.volume = volume;
    lastStatus = OPEN_SEGA_SUCCESS;
    return lastStatus;
}
unsigned int SEGAAPI_GetChannelVolume(void* hHandle, unsigned int dwChannel)
{
    auto stateIt = voiceStates.find(hHandle);
    if (stateIt == voiceStates.end()) {
        return 0;
    }

    return (unsigned int)(stateIt->second.volume * OPEN_HAWOSEVOL_MAX);
}

OPEN_SEGASTATUS SEGAAPI_SetPlaybackPosition(void* hHandle, unsigned int dwPlaybackPos)
{
    auto it = sourceVoices.find(hHandle);
    if (it == sourceVoices.end()) {
        lastStatus = OPEN_SEGAERR_BAD_HANDLE;
        return lastStatus;
    }

    FAudioSourceVoice_FlushSourceBuffers(it->second);
    lastStatus = OPEN_SEGA_SUCCESS;
    return lastStatus;
}

unsigned int SEGAAPI_GetPlaybackPosition(void* hHandle)
{
    auto it = sourceVoices.find(hHandle);
    if (it == sourceVoices.end()) {
        return 0;
    }

    FAudioVoiceState state;
    FAudioSourceVoice_GetState(it->second, &state, 0);
    return state.SamplesPlayed;
}

OPEN_SEGASTATUS SEGAAPI_SetNotificationFrequency(void* hHandle, unsigned int dwFrameCount)
{
    lastStatus = OPEN_SEGA_SUCCESS;
    return lastStatus;
}

OPEN_SEGASTATUS SEGAAPI_SetNotificationPoint(void* hHandle, unsigned int dwBufferOffset)
{
    lastStatus = OPEN_SEGA_SUCCESS;
    return lastStatus;
}

OPEN_SEGASTATUS SEGAAPI_ClearNotificationPoint(void* hHandle, unsigned int dwBufferOffset)
{
    lastStatus = OPEN_SEGA_SUCCESS;
    return lastStatus;
}

OPEN_SEGASTATUS SEGAAPI_SetStartLoopOffset(void* hHandle, unsigned int dwOffset)
{
    auto stateIt = voiceStates.find(hHandle);
    if (stateIt == voiceStates.end()) {
        lastStatus = OPEN_SEGAERR_BAD_HANDLE;
        return lastStatus;
    }

    stateIt->second.loopStart = dwOffset;
    lastStatus = OPEN_SEGA_SUCCESS;
    return lastStatus;
}
unsigned int SEGAAPI_GetStartLoopOffset(void* hHandle)
{
    auto stateIt = voiceStates.find(hHandle);
    if (stateIt == voiceStates.end()) {
        return 0;
    }

    return stateIt->second.loopStart;
}

OPEN_SEGASTATUS SEGAAPI_SetEndLoopOffset(void* hHandle, unsigned int dwOffset)
{
    auto stateIt = voiceStates.find(hHandle);
    if (stateIt == voiceStates.end()) {
        lastStatus = OPEN_SEGAERR_BAD_HANDLE;
        return lastStatus;
    }

    stateIt->second.loopEnd = dwOffset;
    lastStatus = OPEN_SEGA_SUCCESS;
    return lastStatus;
}

unsigned int SEGAAPI_GetEndLoopOffset(void* hHandle)
{
    auto stateIt = voiceStates.find(hHandle);
    if (stateIt == voiceStates.end()) {
        return 0;
    }

    return stateIt->second.loopEnd;
}

OPEN_SEGASTATUS SEGAAPI_SetEndOffset(void* hHandle, unsigned int dwOffset)
{
    auto stateIt = voiceStates.find(hHandle);
    if (stateIt == voiceStates.end()) {
        lastStatus = OPEN_SEGAERR_BAD_HANDLE;
        return lastStatus;
    }

    stateIt->second.endOffset = dwOffset;
    lastStatus = OPEN_SEGA_SUCCESS;
    return lastStatus;
}

unsigned int SEGAAPI_GetEndOffset(void* hHandle)
{
    auto stateIt = voiceStates.find(hHandle);
    if (stateIt == voiceStates.end()) {
        return 0;
    }

    return stateIt->second.endOffset;
}

OPEN_SEGASTATUS SEGAAPI_SetLoopState(void* hHandle, int bDoContinuousLooping)
{
    auto stateIt = voiceStates.find(hHandle);
    if (stateIt == voiceStates.end()) {
        lastStatus = OPEN_SEGAERR_BAD_HANDLE;
        return lastStatus;
    }

    stateIt->second.looping = bDoContinuousLooping != 0;
    lastStatus = OPEN_SEGA_SUCCESS;
    return lastStatus;
}
int SEGAAPI_GetLoopState(void* hHandle)
{
    auto stateIt = voiceStates.find(hHandle);
    if (stateIt == voiceStates.end()) {
        return 0;
    }

    return stateIt->second.looping ? 1 : 0;
}

OPEN_SEGASTATUS SEGAAPI_SetPitch(void* hHandle, float fPitch)
{
    auto it = sourceVoices.find(hHandle);
    auto stateIt = voiceStates.find(hHandle);
    if (it == sourceVoices.end() || stateIt == voiceStates.end()) {
        lastStatus = OPEN_SEGAERR_BAD_HANDLE;
        return lastStatus;
    }

    if (FAudioSourceVoice_SetFrequencyRatio(it->second, fPitch, FAUDIO_COMMIT_NOW) != 0) {
        lastStatus = OPEN_SEGAERR_FAIL;
        return lastStatus;
    }

    stateIt->second.pitch = fPitch;
    lastStatus = OPEN_SEGA_SUCCESS;
    return lastStatus;
}

float SEGAAPI_GetPitch(void* hHandle)
{
    auto stateIt = voiceStates.find(hHandle);
    if (stateIt == voiceStates.end()) {
        return 1.0f;
    }

    return stateIt->second.pitch;
}

OPEN_SEGASTATUS SEGAAPI_SetIOVolume(OPEN_HAPHYSICALIO dwPhysIO, unsigned int dwVolume)
{
    lastStatus = OPEN_SEGA_SUCCESS;
    return lastStatus;
}

unsigned int SEGAAPI_GetIOVolume(OPEN_HAPHYSICALIO dwPhysIO)
{
    return OPEN_HAWOSEVOL_MAX;
}

OPEN_SEGASTATUS SEGAAPI_Reset(void)
{
    for (auto& voice : sourceVoices) {
        if (voice.second) {
            FAudioSourceVoice_Stop(voice.second, 0, FAUDIO_COMMIT_NOW);
            FAudioSourceVoice_FlushSourceBuffers(voice.second);
        }
    }
    lastStatus = OPEN_SEGA_SUCCESS;
    return lastStatus;
}

void SEGAAPI_SetLastStatus(OPEN_SEGASTATUS LastStatus)
{
    lastStatus = LastStatus;
}

OPEN_SEGASTATUS SEGAAPI_GetLastStatus(void)
{
    return lastStatus;
}

OPEN_SEGASTATUS SEGAAPI_SetSendRouting(void* hHandle, unsigned int dwChannel, unsigned int dwSend, OPEN_HAROUTING dwDest)
{
    auto it = sourceVoices.find(hHandle);
    if (it == sourceVoices.end()) {
        lastStatus = OPEN_SEGAERR_BAD_HANDLE;
        return lastStatus;
    }

    float outputMatrix[8] = {0};
    switch(dwDest) {
        case OPEN_HA_FRONT_LEFT_PORT:
            outputMatrix[0] = 1.0f;
            break;
        case OPEN_HA_FRONT_RIGHT_PORT:
            outputMatrix[1] = 1.0f;
            break;
    }

    FAudioVoice_SetOutputMatrix(it->second, pMasterVoice, 1, 2, outputMatrix, FAUDIO_COMMIT_NOW);
    lastStatus = OPEN_SEGA_SUCCESS;
    return lastStatus;
}
OPEN_HAROUTING SEGAAPI_GetSendRouting(void* hHandle, unsigned int dwChannel, unsigned int dwSend)
{
    return OPEN_HA_FRONT_LEFT_PORT;
}

OPEN_SEGASTATUS SEGAAPI_SetSendLevel(void* hHandle, unsigned int dwChannel, unsigned int dwSend, unsigned int dwLevel)
{
    auto it = sourceVoices.find(hHandle);
    if (it == sourceVoices.end()) {
        lastStatus = OPEN_SEGAERR_BAD_HANDLE;
        return lastStatus;
    }

    float level = dwLevel / (float)OPEN_HAWOSEVOL_MAX;
    float outputMatrix[8] = {level, level};
    FAudioVoice_SetOutputMatrix(it->second, pMasterVoice, 1, 2, outputMatrix, FAUDIO_COMMIT_NOW);
    
    lastStatus = OPEN_SEGA_SUCCESS;
    return lastStatus;
}

unsigned int SEGAAPI_GetSendLevel(void* hHandle, unsigned int dwChannel, unsigned int dwSend)
{
    auto it = sourceVoices.find(hHandle);
    if (it == sourceVoices.end()) {
        return 0;
    }

    float outputMatrix[8];
    FAudioVoice_GetOutputMatrix(it->second, pMasterVoice, 1, 2, outputMatrix);
    return (unsigned int)(outputMatrix[0] * OPEN_HAWOSEVOL_MAX);
}

OPEN_SEGASTATUS SEGAAPI_SetSendEAXProperty(void* hHandle, unsigned int dwChannel, unsigned int dwSend, GUID* guid, unsigned long ulProperty, void* pData, unsigned long ulDataSize)
{
    lastStatus = OPEN_SEGA_SUCCESS;
    return lastStatus;
}

OPEN_SEGASTATUS SEGAAPI_GetSendEAXProperty(void* hHandle, unsigned int dwChannel, unsigned int dwSend, GUID* guid, unsigned long ulProperty, void* pData, unsigned long ulDataSize)
{
    lastStatus = OPEN_SEGA_SUCCESS;
    return lastStatus;
}

OPEN_SEGASTATUS SEGAAPI_SetSendFilter(void* hHandle, unsigned int dwChannel, unsigned int dwSend, unsigned int dwFilterID)
{
    lastStatus = OPEN_SEGA_SUCCESS;
    return lastStatus;
}

unsigned int SEGAAPI_GetSendFilter(void* hHandle, unsigned int dwChannel, unsigned int dwSend)
{
    return 0;
}

OPEN_SEGASTATUS SEGAAPI_SetSendFilterParam(void* hHandle, unsigned int dwChannel, unsigned int dwSend, unsigned int dwFilterParam, int lValue)
{
    lastStatus = OPEN_SEGA_SUCCESS;
    return lastStatus;
}

int SEGAAPI_GetSendFilterParam(void* hHandle, unsigned int dwChannel, unsigned int dwSend, unsigned int dwFilterParam)
{
    return 0;
}
OPEN_SEGASTATUS SEGAAPI_SetSendFilterParamMultiple(void* hHandle, unsigned int dwChannel, unsigned int dwSend, unsigned int dwNumParams, OPEN_FilterParamSet* pFilterParams)
{
    lastStatus = OPEN_SEGA_SUCCESS;
    return lastStatus;
}

OPEN_SEGASTATUS SEGAAPI_GetSendFilterParamMultiple(void* hHandle, unsigned int dwChannel, unsigned int dwSend, unsigned int dwNumParams, OPEN_FilterParamSet* pFilterParams)
{
    lastStatus = OPEN_SEGA_SUCCESS;
    return lastStatus;
}

OPEN_SEGASTATUS SEGAAPI_SetSendFilterState(void* hHandle, unsigned int dwChannel, unsigned int dwSend, int bEnable)
{
    lastStatus = OPEN_SEGA_SUCCESS;
    return lastStatus;
}

int SEGAAPI_GetSendFilterState(void* hHandle, unsigned int dwChannel, unsigned int dwSend)
{
    return 0;
}

OPEN_SEGASTATUS SEGAAPI_SetSendFilterQFactor(void* hHandle, unsigned int dwChannel, unsigned int dwSend, int lQFactor)
{
    lastStatus = OPEN_SEGA_SUCCESS;
    return lastStatus;
}

int SEGAAPI_GetSendFilterQFactor(void* hHandle, unsigned int dwChannel, unsigned int dwSend)
{
    return 0;
}

OPEN_SEGASTATUS SEGAAPI_SetSendFilterFrequency(void* hHandle, unsigned int dwChannel, unsigned int dwSend, int lFrequency)
{
    lastStatus = OPEN_SEGA_SUCCESS;
    return lastStatus;
}

int SEGAAPI_GetSendFilterFrequency(void* hHandle, unsigned int dwChannel, unsigned int dwSend)
{
    return 0;
}

OPEN_SEGASTATUS SEGAAPI_SetSendFilterGain(void* hHandle, unsigned int dwChannel, unsigned int dwSend, int lGain)
{
    lastStatus = OPEN_SEGA_SUCCESS;
    return lastStatus;
}

int SEGAAPI_GetSendFilterGain(void* hHandle, unsigned int dwChannel, unsigned int dwSend)
{
    return 0;
}

OPEN_SEGASTATUS SEGAAPI_SetSendFilterBandwidth(void* hHandle, unsigned int dwChannel, unsigned int dwSend, int lBandwidth)
{
    lastStatus = OPEN_SEGA_SUCCESS;
    return lastStatus;
}

int SEGAAPI_GetSendFilterBandwidth(void* hHandle, unsigned int dwChannel, unsigned int dwSend)
{
    return 0;
}
OPEN_SEGASTATUS SEGAAPI_SetSendFilterCutoffFrequency(void* hHandle, unsigned int dwChannel, unsigned int dwSend, int lCutoffFrequency)
{
    lastStatus = OPEN_SEGA_SUCCESS;
    return lastStatus;
}

int SEGAAPI_GetSendFilterCutoffFrequency(void* hHandle, unsigned int dwChannel, unsigned int dwSend)
{
    return 0;
}

OPEN_SEGASTATUS SEGAAPI_SetSendFilterLowpassResonance(void* hHandle, unsigned int dwChannel, unsigned int dwSend, int lLowpassResonance)
{
    lastStatus = OPEN_SEGA_SUCCESS;
    return lastStatus;
}

int SEGAAPI_GetSendFilterLowpassResonance(void* hHandle, unsigned int dwChannel, unsigned int dwSend)
{
    return 0;
}

OPEN_SEGASTATUS SEGAAPI_SetSendFilterWetDryMix(void* hHandle, unsigned int dwChannel, unsigned int dwSend, int lWetDryMix)
{
    lastStatus = OPEN_SEGA_SUCCESS;
    return lastStatus;
}

int SEGAAPI_GetSendFilterWetDryMix(void* hHandle, unsigned int dwChannel, unsigned int dwSend)
{
    return 0;
}

OPEN_SEGASTATUS SEGAAPI_SetSendFilterDelay(void* hHandle, unsigned int dwChannel, unsigned int dwSend, int lDelay)
{
    lastStatus = OPEN_SEGA_SUCCESS;
    return lastStatus;
}

int SEGAAPI_GetSendFilterDelay(void* hHandle, unsigned int dwChannel, unsigned int dwSend)
{
    return 0;
}

OPEN_SEGASTATUS SEGAAPI_SetSendFilterFeedback(void* hHandle, unsigned int dwChannel, unsigned int dwSend, int lFeedback)
{
    lastStatus = OPEN_SEGA_SUCCESS;
    return lastStatus;
}

int SEGAAPI_GetSendFilterFeedback(void* hHandle, unsigned int dwChannel, unsigned int dwSend)
{
    return 0;
}

OPEN_SEGASTATUS SEGAAPI_SetSendFilterLeftDelay(void* hHandle, unsigned int dwChannel, unsigned int dwSend, int lLeftDelay)
{
    lastStatus = OPEN_SEGA_SUCCESS;
    return lastStatus;
}

int SEGAAPI_GetSendFilterLeftDelay(void* hHandle, unsigned int dwChannel, unsigned int dwSend)
{
    return 0;
}
OPEN_SEGASTATUS SEGAAPI_SetSendFilterRightDelay(void* hHandle, unsigned int dwChannel, unsigned int dwSend, int lRightDelay)
{
    lastStatus = OPEN_SEGA_SUCCESS;
    return lastStatus;
}

int SEGAAPI_GetSendFilterRightDelay(void* hHandle, unsigned int dwChannel, unsigned int dwSend)
{
    return 0;
}

OPEN_SEGASTATUS SEGAAPI_SetSendFilterPanDelay(void* hHandle, unsigned int dwChannel, unsigned int dwSend, int bPanDelay)
{
    lastStatus = OPEN_SEGA_SUCCESS;
    return lastStatus;
}

int SEGAAPI_GetSendFilterPanDelay(void* hHandle, unsigned int dwChannel, unsigned int dwSend)
{
    return 0;
}

OPEN_SEGASTATUS SEGAAPI_SetSendFilterModulationRate(void* hHandle, unsigned int dwChannel, unsigned int dwSend, int lModulationRate)
{
    lastStatus = OPEN_SEGA_SUCCESS;
    return lastStatus;
}

int SEGAAPI_GetSendFilterModulationRate(void* hHandle, unsigned int dwChannel, unsigned int dwSend)
{
    return 0;
}

OPEN_SEGASTATUS SEGAAPI_SetSendFilterModulationDepth(void* hHandle, unsigned int dwChannel, unsigned int dwSend, int lModulationDepth)
{
    lastStatus = OPEN_SEGA_SUCCESS;
    return lastStatus;
}

int SEGAAPI_GetSendFilterModulationDepth(void* hHandle, unsigned int dwChannel, unsigned int dwSend)
{
    return 0;
}

OPEN_SEGASTATUS SEGAAPI_SetSendFilterPhase(void* hHandle, unsigned int dwChannel, unsigned int dwSend, int lPhase)
{
    lastStatus = OPEN_SEGA_SUCCESS;
    return lastStatus;
}

int SEGAAPI_GetSendFilterPhase(void* hHandle, unsigned int dwChannel, unsigned int dwSend)
{
    return 0;
}

OPEN_SEGASTATUS SEGAAPI_SetSendFilterInGain(void* hHandle, unsigned int dwChannel, unsigned int dwSend, int lInGain)
{
    lastStatus = OPEN_SEGA_SUCCESS;
    return lastStatus;
}

int SEGAAPI_GetSendFilterInGain(void* hHandle, unsigned int dwChannel, unsigned int dwSend)
{
    return 0;
}

OPEN_SEGASTATUS SEGAAPI_SetSendFilterOutGain(void* hHandle, unsigned int dwChannel, unsigned int dwSend, int lOutGain)
{
    lastStatus = OPEN_SEGA_SUCCESS;
    return lastStatus;
}

int SEGAAPI_GetSendFilterOutGain(void* hHandle, unsigned int dwChannel, unsigned int dwSend)
{
    return 0;
}

OPEN_SEGASTATUS SEGAAPI_SetSendFilterEQGain(void* hHandle, unsigned int dwChannel, unsigned int dwSend, int lEQGain)
{
    lastStatus = OPEN_SEGA_SUCCESS;
    return lastStatus;
}

int SEGAAPI_GetSendFilterEQGain(void* hHandle, unsigned int dwChannel, unsigned int dwSend)
{
    return 0;
}

OPEN_SEGASTATUS SEGAAPI_SetSendFilterEQBandwidth(void* hHandle, unsigned int dwChannel, unsigned int dwSend, int lEQBandwidth)
{
    lastStatus = OPEN_SEGA_SUCCESS;
    return lastStatus;
}

int SEGAAPI_GetSendFilterEQBandwidth(void* hHandle, unsigned int dwChannel, unsigned int dwSend)
{
    return 0;
}

OPEN_SEGASTATUS SEGAAPI_SetSendFilterEQFrequency(void* hHandle, unsigned int dwChannel, unsigned int dwSend, int lEQFrequency)
{
    lastStatus = OPEN_SEGA_SUCCESS;
    return lastStatus;
}

int SEGAAPI_GetSendFilterEQFrequency(void* hHandle, unsigned int dwChannel, unsigned int dwSend)
{
    return 0;
}

OPEN_SEGASTATUS SEGAAPI_SetSendFilterDistortion(void* hHandle, unsigned int dwChannel, unsigned int dwSend, int lDistortion)
{
    lastStatus = OPEN_SEGA_SUCCESS;
    return lastStatus;
}

int SEGAAPI_GetSendFilterDistortion(void* hHandle, unsigned int dwChannel, unsigned int dwSend)
{
    return 0;
}
OPEN_SEGASTATUS SEGAAPI_SetSendFilterEcho(void* hHandle, unsigned int dwChannel, unsigned int dwSend, int lEcho)
{
    lastStatus = OPEN_SEGA_SUCCESS;
    return lastStatus;
}

int SEGAAPI_GetSendFilterEcho(void* hHandle, unsigned int dwChannel, unsigned int dwSend)
{
    return 0;
}

OPEN_SEGASTATUS SEGAAPI_SetSendFilterChorus(void* hHandle, unsigned int dwChannel, unsigned int dwSend, int lChorus)
{
    lastStatus = OPEN_SEGA_SUCCESS;
    return lastStatus;
}

int SEGAAPI_GetSendFilterChorus(void* hHandle, unsigned int dwChannel, unsigned int dwSend)
{
    return 0;
}

OPEN_SEGASTATUS SEGAAPI_SetSendFilterDecayTime(void* hHandle, unsigned int dwChannel, unsigned int dwSend, int lDecayTime)
{
    lastStatus = OPEN_SEGA_SUCCESS;
    return lastStatus;
}

int SEGAAPI_GetSendFilterDecayTime(void* hHandle, unsigned int dwChannel, unsigned int dwSend)
{
    return 0;
}

OPEN_SEGASTATUS SEGAAPI_SetSendFilterDensity(void* hHandle, unsigned int dwChannel, unsigned int dwSend, int lDensity)
{
    lastStatus = OPEN_SEGA_SUCCESS;
    return lastStatus;
}

int SEGAAPI_GetSendFilterDensity(void* hHandle, unsigned int dwChannel, unsigned int dwSend)
{
    return 0;
}

OPEN_SEGASTATUS SEGAAPI_SetSendFilterDiffusion(void* hHandle, unsigned int dwChannel, unsigned int dwSend, int lDiffusion)
{
    lastStatus = OPEN_SEGA_SUCCESS;
    return lastStatus;
}

int SEGAAPI_GetSendFilterDiffusion(void* hHandle, unsigned int dwChannel, unsigned int dwSend)
{
    return 0;
}

OPEN_SEGASTATUS SEGAAPI_SetSendFilterHFReference(void* hHandle, unsigned int dwChannel, unsigned int dwSend, int lHFReference)
{
    lastStatus = OPEN_SEGA_SUCCESS;
    return lastStatus;
}

int SEGAAPI_GetSendFilterHFReference(void* hHandle, unsigned int dwChannel, unsigned int dwSend)
{
    return 0;
}

OPEN_SEGASTATUS SEGAAPI_SetSendFilterReflectionsDelay(void* hHandle, unsigned int dwChannel, unsigned int dwSend, int lReflectionsDelay)
{
    lastStatus = OPEN_SEGA_SUCCESS;
    return lastStatus;
}

int SEGAAPI_GetSendFilterReflectionsDelay(void* hHandle, unsigned int dwChannel, unsigned int dwSend)
{
    return 0;
}

OPEN_SEGASTATUS SEGAAPI_SetSendFilterReflectionsGain(void* hHandle, unsigned int dwChannel, unsigned int dwSend, int lReflectionsGain)
{
    lastStatus = OPEN_SEGA_SUCCESS;
    return lastStatus;
}

int SEGAAPI_GetSendFilterReflectionsGain(void* hHandle, unsigned int dwChannel, unsigned int dwSend)
{
    return 0;
}
OPEN_SEGASTATUS SEGAAPI_SetSendFilterReverbDelay(void* hHandle, unsigned int dwChannel, unsigned int dwSend, int lReverbDelay)
{
    lastStatus = OPEN_SEGA_SUCCESS;
    return lastStatus;
}

int SEGAAPI_GetSendFilterReverbDelay(void* hHandle, unsigned int dwChannel, unsigned int dwSend)
{
    return 0;
}

OPEN_SEGASTATUS SEGAAPI_SetSendFilterReverbGain(void* hHandle, unsigned int dwChannel, unsigned int dwSend, int lReverbGain)
{
    lastStatus = OPEN_SEGA_SUCCESS;
    return lastStatus;
}

int SEGAAPI_GetSendFilterReverbGain(void* hHandle, unsigned int dwChannel, unsigned int dwSend)
{
    return 0;
}

OPEN_SEGASTATUS SEGAAPI_SetSendFilterRoomRolloffFactor(void* hHandle, unsigned int dwChannel, unsigned int dwSend, int lRoomRolloffFactor)
{
    lastStatus = OPEN_SEGA_SUCCESS;
    return lastStatus;
}

int SEGAAPI_GetSendFilterRoomRolloffFactor(void* hHandle, unsigned int dwChannel, unsigned int dwSend)
{
    return 0;
}

OPEN_SEGASTATUS SEGAAPI_SetSendFilterAirAbsorptionGainHF(void* hHandle, unsigned int dwChannel, unsigned int dwSend, int lAirAbsorptionGainHF)
{
    lastStatus = OPEN_SEGA_SUCCESS;
    return lastStatus;
}

int SEGAAPI_GetSendFilterAirAbsorptionGainHF(void* hHandle, unsigned int dwChannel, unsigned int dwSend)
{
    return 0;
}

OPEN_SEGASTATUS SEGAAPI_SetSendFilterRoomSize(void* hHandle, unsigned int dwChannel, unsigned int dwSend, int lRoomSize)
{
    lastStatus = OPEN_SEGA_SUCCESS;
    return lastStatus;
}

int SEGAAPI_GetSendFilterRoomSize(void* hHandle, unsigned int dwChannel, unsigned int dwSend)
{
    return 0;
}

OPEN_SEGASTATUS SEGAAPI_SetSendFilterPosition(void* hHandle, unsigned int dwChannel, unsigned int dwSend, int lPosition)
{
    lastStatus = OPEN_SEGA_SUCCESS;
    return lastStatus;
}

int SEGAAPI_GetSendFilterPosition(void* hHandle, unsigned int dwChannel, unsigned int dwSend)
{
    return 0;
}

OPEN_SEGASTATUS SEGAAPI_SetSendFilterVelocity(void* hHandle, unsigned int dwChannel, unsigned int dwSend, int lVelocity)
{
    lastStatus = OPEN_SEGA_SUCCESS;
    return lastStatus;
}

int SEGAAPI_GetSendFilterVelocity(void* hHandle, unsigned int dwChannel, unsigned int dwSend)
{
    return 0;
}

OPEN_SEGASTATUS SEGAAPI_SetSendFilterOrientation(void* hHandle, unsigned int dwChannel, unsigned int dwSend, int lOrientation)
{
    lastStatus = OPEN_SEGA_SUCCESS;
    return lastStatus;
}

int SEGAAPI_GetSendFilterOrientation(void* hHandle, unsigned int dwChannel, unsigned int dwSend)
{
    return 0;
}
OPEN_SEGASTATUS SEGAAPI_SetSendFilterEnvironmentSize(void* hHandle, unsigned int dwChannel, unsigned int dwSend, int lEnvironmentSize)
{
    lastStatus = OPEN_SEGA_SUCCESS;
    return lastStatus;
}

int SEGAAPI_GetSendFilterEnvironmentSize(void* hHandle, unsigned int dwChannel, unsigned int dwSend)
{
    return 0;
}

OPEN_SEGASTATUS SEGAAPI_SetSendFilterEnvironmentDiffusion(void* hHandle, unsigned int dwChannel, unsigned int dwSend, int lEnvironmentDiffusion)
{
    lastStatus = OPEN_SEGA_SUCCESS;
    return lastStatus;
}

int SEGAAPI_GetSendFilterEnvironmentDiffusion(void* hHandle, unsigned int dwChannel, unsigned int dwSend)
{
    return 0;
}

OPEN_SEGASTATUS SEGAAPI_SetSendFilterEnvironmentReflections(void* hHandle, unsigned int dwChannel, unsigned int dwSend, int lEnvironmentReflections)
{
    lastStatus = OPEN_SEGA_SUCCESS;
    return lastStatus;
}

int SEGAAPI_GetSendFilterEnvironmentReflections(void* hHandle, unsigned int dwChannel, unsigned int dwSend)
{
    return 0;
}

OPEN_SEGASTATUS SEGAAPI_SetSendFilterEnvironmentReverb(void* hHandle, unsigned int dwChannel, unsigned int dwSend, int lEnvironmentReverb)
{
    lastStatus = OPEN_SEGA_SUCCESS;
    return lastStatus;
}

int SEGAAPI_GetSendFilterEnvironmentReverb(void* hHandle, unsigned int dwChannel, unsigned int dwSend)
{
    return 0;
}

OPEN_SEGASTATUS SEGAAPI_SetSendFilterReflectionsScale(void* hHandle, unsigned int dwChannel, unsigned int dwSend, int lReflectionsScale)
{
    lastStatus = OPEN_SEGA_SUCCESS;
    return lastStatus;
}

int SEGAAPI_GetSendFilterReflectionsScale(void* hHandle, unsigned int dwChannel, unsigned int dwSend)
{
    return 0;
}

OPEN_SEGASTATUS SEGAAPI_SetSendFilterReverbScale(void* hHandle, unsigned int dwChannel, unsigned int dwSend, int lReverbScale)
{
    lastStatus = OPEN_SEGA_SUCCESS;
    return lastStatus;
}

int SEGAAPI_GetSendFilterReverbScale(void* hHandle, unsigned int dwChannel, unsigned int dwSend)
{
    return 0;
}

OPEN_SEGASTATUS SEGAAPI_SetSendFilterReflectionsDelayScale(void* hHandle, unsigned int dwChannel, unsigned int dwSend, int lReflectionsDelayScale)
{
    lastStatus = OPEN_SEGA_SUCCESS;
    return lastStatus;
}

int SEGAAPI_GetSendFilterReflectionsDelayScale(void* hHandle, unsigned int dwChannel, unsigned int dwSend)
{
    return 0;
}
OPEN_SEGASTATUS SEGAAPI_SetSendFilterReverbDelayScale(void* hHandle, unsigned int dwChannel, unsigned int dwSend, int lReverbDelayScale)
{
    lastStatus = OPEN_SEGA_SUCCESS;
    return lastStatus;
}

int SEGAAPI_GetSendFilterReverbDelayScale(void* hHandle, unsigned int dwChannel, unsigned int dwSend)
{
    return 0;
}

OPEN_SEGASTATUS SEGAAPI_SetSendFilterDecayHFRatio(void* hHandle, unsigned int dwChannel, unsigned int dwSend, int lDecayHFRatio)
{
    lastStatus = OPEN_SEGA_SUCCESS;
    return lastStatus;
}

int SEGAAPI_GetSendFilterDecayHFRatio(void* hHandle, unsigned int dwChannel, unsigned int dwSend)
{
    return 0;
}

OPEN_SEGASTATUS SEGAAPI_SetSendFilterModulationTime(void* hHandle, unsigned int dwChannel, unsigned int dwSend, int lModulationTime)
{
    lastStatus = OPEN_SEGA_SUCCESS;
    return lastStatus;
}

int SEGAAPI_GetSendFilterModulationTime(void* hHandle, unsigned int dwChannel, unsigned int dwSend)
{
    return 0;
}

OPEN_SEGASTATUS SEGAAPI_SetSendFilterModulationWaveform(void* hHandle, unsigned int dwChannel, unsigned int dwSend, int lModulationWaveform)
{
    lastStatus = OPEN_SEGA_SUCCESS;
    return lastStatus;
}

int SEGAAPI_GetSendFilterModulationWaveform(void* hHandle, unsigned int dwChannel, unsigned int dwSend)
{
    return 0;
}

OPEN_SEGASTATUS SEGAAPI_SetSendFilterHFGain(void* hHandle, unsigned int dwChannel, unsigned int dwSend, int lHFGain)
{
    lastStatus = OPEN_SEGA_SUCCESS;
    return lastStatus;
}

int SEGAAPI_GetSendFilterHFGain(void* hHandle, unsigned int dwChannel, unsigned int dwSend)
{
    return 0;
}

OPEN_SEGASTATUS SEGAAPI_SetSendFilterLFGain(void* hHandle, unsigned int dwChannel, unsigned int dwSend, int lLFGain)
{
    lastStatus = OPEN_SEGA_SUCCESS;
    return lastStatus;
}

int SEGAAPI_GetSendFilterLFGain(void* hHandle, unsigned int dwChannel, unsigned int dwSend)
{
    return 0;
}

OPEN_SEGASTATUS SEGAAPI_SetChannelRouting(void* hHandle, unsigned int dwChannel, OPEN_HAROUTING dwDest)
{
    lastStatus = OPEN_SEGA_SUCCESS;
    return lastStatus;
}

OPEN_HAROUTING SEGAAPI_GetChannelRouting(void* hHandle, unsigned int dwChannel)
{
    return OPEN_HA_FRONT_LEFT_PORT;
}
OPEN_SEGASTATUS SEGAAPI_SetChannelEAXProperty(void* hHandle, unsigned int dwChannel, GUID* guid, unsigned long ulProperty, void* pData, unsigned long ulDataSize)
{
    lastStatus = OPEN_SEGA_SUCCESS;
    return lastStatus;
}

OPEN_SEGASTATUS SEGAAPI_GetChannelEAXProperty(void* hHandle, unsigned int dwChannel, GUID* guid, unsigned long ulProperty, void* pData, unsigned long ulDataSize)
{
    lastStatus = OPEN_SEGA_SUCCESS;
    return lastStatus;
}

OPEN_SEGASTATUS SEGAAPI_SetChannelFilter(void* hHandle, unsigned int dwChannel, unsigned int dwFilterID)
{
    lastStatus = OPEN_SEGA_SUCCESS;
    return lastStatus;
}

unsigned int SEGAAPI_GetChannelFilter(void* hHandle, unsigned int dwChannel)
{
    return 0;
}

OPEN_SEGASTATUS SEGAAPI_SetChannelFilterParam(void* hHandle, unsigned int dwChannel, unsigned int dwFilterParam, int lValue)
{
    lastStatus = OPEN_SEGA_SUCCESS;
    return lastStatus;
}

int SEGAAPI_GetChannelFilterParam(void* hHandle, unsigned int dwChannel, unsigned int dwFilterParam)
{
    return 0;
}

OPEN_SEGASTATUS SEGAAPI_SetChannelFilterParamMultiple(void* hHandle, unsigned int dwChannel, unsigned int dwNumParams, OPEN_FilterParamSet* pFilterParams)
{
    lastStatus = OPEN_SEGA_SUCCESS;
    return lastStatus;
}

OPEN_SEGASTATUS SEGAAPI_GetChannelFilterParamMultiple(void* hHandle, unsigned int dwChannel, unsigned int dwNumParams, OPEN_FilterParamSet* pFilterParams)
{
    lastStatus = OPEN_SEGA_SUCCESS;
    return lastStatus;
}

OPEN_SEGASTATUS SEGAAPI_SetChannelFilterState(void* hHandle, unsigned int dwChannel, int bEnable)
{
    lastStatus = OPEN_SEGA_SUCCESS;
    return lastStatus;
}

int SEGAAPI_GetChannelFilterState(void* hHandle, unsigned int dwChannel)
{
    return 0;
}

// SPDIF Functions
OPEN_SEGASTATUS SEGAAPI_SetSPDIFMode(unsigned int dwMode)
{
    lastStatus = OPEN_SEGA_SUCCESS;
    return lastStatus;
}

unsigned int SEGAAPI_GetSPDIFMode(void)
{
    return 0;
}

OPEN_SEGASTATUS SEGAAPI_SetSPDIFWordSize(unsigned int dwWordSize)
{
    lastStatus = OPEN_SEGA_SUCCESS;
    return lastStatus;
}

unsigned int SEGAAPI_GetSPDIFWordSize(void)
{
    return 0;
}
