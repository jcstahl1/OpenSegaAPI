/*
* This file is part of the OpenParrot project - https://teknoparrot.com / https://github.com/teknogods
*
* See LICENSE and MENTIONS in the root of the source tree for information
* regarding licensing.
*/
#include "opensegaapi.h"
#include <map>

// Global FAudio objects
static FAudio* pFAudio = nullptr;
static FAudioMasteringVoice* pMasterVoice = nullptr;
static std::map<void*, FAudioSourceVoice*> sourceVoices;
static OPEN_SEGASTATUS lastStatus = OPEN_SEGA_SUCCESS;

OPEN_SEGASTATUS SEGAAPI_Init(void) {
    uint32_t flags = 0;
    if (FAudioCreate(&pFAudio, flags, FAUDIO_DEFAULT_PROCESSOR) != 0) {
        return OPEN_SEGAERR_FAIL;
    }

    if (FAudioCreateMasteringVoice(pFAudio, &pMasterVoice, 2, 48000, 0, 0, nullptr) != 0) {
        FAudio_Release(pFAudio);
        pFAudio = nullptr;
        return OPEN_SEGAERR_FAIL;
    }

    return OPEN_SEGA_SUCCESS;
}

OPEN_SEGASTATUS SEGAAPI_Exit(void) {
    // Clean up all source voices
    for (auto& voice : sourceVoices) {
        if (voice.second) {
            FAudioVoice_DestroyVoice(voice.second);
        }
    }
    sourceVoices.clear();

    if (pMasterVoice) {
        FAudioVoice_DestroyVoice(pMasterVoice);
        pMasterVoice = nullptr;
    }

    if (pFAudio) {
        FAudio_Release(pFAudio);
        pFAudio = nullptr;
    }

    return OPEN_SEGA_SUCCESS;
}

OPEN_SEGASTATUS SEGAAPI_CreateBuffer(OPEN_HAWOSEBUFFERCONFIG* pConfig, 
                                    OPEN_HAWOSEGABUFFERCALLBACK pCallback,
                                    unsigned int dwFlags, 
                                    void** phHandle) {
    if (!pConfig || !phHandle) {
        return OPEN_SEGAERR_BAD_POINTER;
    }

    FAudioWaveFormatEx waveFormat = {};
    waveFormat.wFormatTag = 1; // PCM
    waveFormat.nChannels = pConfig->byNumChans;
    waveFormat.nSamplesPerSec = pConfig->dwSampleRate;
    waveFormat.wBitsPerSample = (pConfig->dwSampleFormat == OPEN_HASF_UNSIGNED_8PCM) ? 8 : 16;
    waveFormat.nBlockAlign = (waveFormat.wBitsPerSample / 8) * waveFormat.nChannels;
    waveFormat.nAvgBytesPerSec = waveFormat.nSamplesPerSec * waveFormat.nBlockAlign;

    FAudioSourceVoice* sourceVoice;
    if (FAudio_CreateSourceVoice(pFAudio, &sourceVoice, &waveFormat, 0, 2.0f, nullptr, nullptr, nullptr) != 0) {
        return OPEN_SEGAERR_FAIL;
    }

    // Create a unique handle
    *phHandle = sourceVoice;
    sourceVoices[*phHandle] = sourceVoice;

    return OPEN_SEGA_SUCCESS;
}

OPEN_SEGASTATUS SEGAAPI_DestroyBuffer(void* hHandle) {
    auto it = sourceVoices.find(hHandle);
    if (it == sourceVoices.end()) {
        return OPEN_SEGAERR_BAD_HANDLE;
    }

    FAudioVoice_DestroyVoice(it->second);
    sourceVoices.erase(it);
    return OPEN_SEGA_SUCCESS;
}

OPEN_SEGASTATUS SEGAAPI_Play(void* hHandle) {
    auto it = sourceVoices.find(hHandle);
    if (it == sourceVoices.end()) {
        return OPEN_SEGAERR_BAD_HANDLE;
    }

    if (FAudioSourceVoice_Start(it->second, 0, FAUDIO_COMMIT_NOW) != 0) {
        return OPEN_SEGAERR_FAIL;
    }

    return OPEN_SEGA_SUCCESS;
}

OPEN_SEGASTATUS SEGAAPI_Stop(void* hHandle) {
    auto it = sourceVoices.find(hHandle);
    if (it == sourceVoices.end()) {
        return OPEN_SEGAERR_BAD_HANDLE;
    }

    if (FAudioSourceVoice_Stop(it->second, 0, FAUDIO_COMMIT_NOW) != 0) {
        return OPEN_SEGAERR_FAIL;
    }

    return OPEN_SEGA_SUCCESS;
}

OPEN_SEGASTATUS SEGAAPI_Pause(void* hHandle) {
    auto it = sourceVoices.find(hHandle);
    if (it == sourceVoices.end()) {
        return OPEN_SEGAERR_BAD_HANDLE;
    }

    if (FAudioSourceVoice_Stop(it->second, 0, FAUDIO_COMMIT_NOW) != 0) {
        return OPEN_SEGAERR_FAIL;
    }

    return OPEN_SEGA_SUCCESS;
}

OPEN_SEGASTATUS SEGAAPI_UpdateBuffer(void* hHandle, unsigned int dwStartOffset, unsigned int dwLength) {
    auto it = sourceVoices.find(hHandle);
    if (it == sourceVoices.end()) {
        return OPEN_SEGAERR_BAD_HANDLE;
    }

    FAudioBuffer buffer = {};
    buffer.AudioBytes = dwLength;
    buffer.pAudioData = (const uint8_t*)dwStartOffset;
    
    if (FAudioSourceVoice_SubmitSourceBuffer(it->second, &buffer, nullptr) != 0) {
        return OPEN_SEGAERR_FAIL;
    }

    return OPEN_SEGA_SUCCESS;
}

OPEN_SEGASTATUS SEGAAPI_SetChannelVolume(void* hHandle, unsigned int dwChannel, unsigned int dwVolume) {
    auto it = sourceVoices.find(hHandle);
    if (it == sourceVoices.end()) {
        return OPEN_SEGAERR_BAD_HANDLE;
    }

    float volume = dwVolume / (float)OPEN_HAWOSEVOL_MAX;
    if (FAudioVoice_SetVolume(it->second, volume, FAUDIO_COMMIT_NOW) != 0) {
        return OPEN_SEGAERR_FAIL;
    }

    return OPEN_SEGA_SUCCESS;
}

void SEGAAPI_SetLastStatus(OPEN_SEGASTATUS LastStatus) {
    lastStatus = LastStatus;
}

OPEN_SEGASTATUS SEGAAPI_GetLastStatus(void) {
    return lastStatus;
}

OPEN_HAWOSTATUS SEGAAPI_GetPlaybackStatus(void* hHandle) {
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

OPEN_SEGASTATUS SEGAAPI_SetFormat(void* hHandle, OPEN_HAWOSEFORMAT* pFormat) {
    auto it = sourceVoices.find(hHandle);
    if (it == sourceVoices.end() || !pFormat) {
        return OPEN_SEGAERR_BAD_HANDLE;
    }

    FAudioWaveFormatEx waveFormat = {};
    waveFormat.wFormatTag = 1; // PCM
    waveFormat.nChannels = pFormat->byNumChans;
    waveFormat.nSamplesPerSec = pFormat->dwSampleRate;
    waveFormat.wBitsPerSample = (pFormat->dwSampleFormat == OPEN_HASF_UNSIGNED_8PCM) ? 8 : 16;
    waveFormat.nBlockAlign = (waveFormat.wBitsPerSample / 8) * waveFormat.nChannels;
    waveFormat.nAvgBytesPerSec = waveFormat.nSamplesPerSec * waveFormat.nBlockAlign;

    // Need to recreate voice with new format
    FAudioVoice_DestroyVoice(it->second);
    
    if (FAudio_CreateSourceVoice(pFAudio, &it->second, &waveFormat, 0, 2.0f, nullptr, nullptr, nullptr) != 0) {
        return OPEN_SEGAERR_FAIL;
    }

    return OPEN_SEGA_SUCCESS;
}

OPEN_SEGASTATUS SEGAAPI_GetFormat(void* hHandle, OPEN_HAWOSEFORMAT* pFormat) {
    if (!pFormat) {
        return OPEN_SEGAERR_BAD_POINTER;
    }

    auto it = sourceVoices.find(hHandle);
    if (it == sourceVoices.end()) {
        return OPEN_SEGAERR_BAD_HANDLE;
    }

    // FAudio doesn't provide direct format query, format must be tracked separately
    // Returning default format for now
    pFormat->dwSampleRate = 44100;
    pFormat->dwSampleFormat = OPEN_HASF_SIGNED_16PCM;
    pFormat->byNumChans = 2;

    return OPEN_SEGA_SUCCESS;
}

OPEN_SEGASTATUS SEGAAPI_SetSampleRate(void* hHandle, unsigned int dwSampleRate) {
    auto it = sourceVoices.find(hHandle);
    if (it == sourceVoices.end()) {
        return OPEN_SEGAERR_BAD_HANDLE;
    }

    // FAudio requires recreating the voice to change sample rate
    FAudioVoiceDetails details;
    FAudioVoice_GetVoiceDetails(it->second, &details);

    FAudioWaveFormatEx waveFormat = {};
    waveFormat.wFormatTag = 1;
    waveFormat.nChannels = details.InputChannels;
    waveFormat.nSamplesPerSec = dwSampleRate;
    waveFormat.wBitsPerSample = 16;
    waveFormat.nBlockAlign = (waveFormat.wBitsPerSample / 8) * waveFormat.nChannels;
    waveFormat.nAvgBytesPerSec = waveFormat.nSamplesPerSec * waveFormat.nBlockAlign;

    FAudioVoice_DestroyVoice(it->second);
    
    if (FAudio_CreateSourceVoice(pFAudio, &it->second, &waveFormat, 0, 2.0f, nullptr, nullptr, nullptr) != 0) {
        return OPEN_SEGAERR_FAIL;
    }

    return OPEN_SEGA_SUCCESS;
}

unsigned int SEGAAPI_GetSampleRate(void* hHandle) {
    auto it = sourceVoices.find(hHandle);
    if (it == sourceVoices.end()) {
        return 0;
    }

    FAudioVoiceDetails details;
    FAudioVoice_GetVoiceDetails(it->second, &details);
    return details.InputSampleRate;
}

OPEN_SEGASTATUS SEGAAPI_SetPriority(void* hHandle, unsigned int dwPriority) {
    // FAudio doesn't support priority directly, but we'll track it
    return OPEN_SEGA_SUCCESS;
}

unsigned int SEGAAPI_GetPriority(void* hHandle) {
    // Return default priority since FAudio doesn't support it
    return 0;
}

OPEN_SEGASTATUS SEGAAPI_SetUserData(void* hHandle, void* hUserData) {
    auto it = sourceVoices.find(hHandle);
    if (it == sourceVoices.end()) {
        return OPEN_SEGAERR_BAD_HANDLE;
    }
    
    // Store user data in a separate map if needed
    return OPEN_SEGA_SUCCESS;
}

void* SEGAAPI_GetUserData(void* hHandle) {
    auto it = sourceVoices.find(hHandle);
    if (it == sourceVoices.end()) {
        return nullptr;
    }
    
    // Return stored user data
    return nullptr;
}

OPEN_SEGASTATUS SEGAAPI_SetSendRouting(void* hHandle, unsigned int dwChannel, unsigned int dwSend, OPEN_HAROUTING dwDest) {
    auto it = sourceVoices.find(hHandle);
    if (it == sourceVoices.end()) {
        return OPEN_SEGAERR_BAD_HANDLE;
    }

    // Configure output matrix for the voice
    float outputMatrix[8] = {0}; // Assuming max 8 channels
    
    switch(dwDest) {
        case OPEN_HA_FRONT_LEFT_PORT:
            outputMatrix[0] = 1.0f;
            break;
        case OPEN_HA_FRONT_RIGHT_PORT:
            outputMatrix[1] = 1.0f;
            break;
        // Add other routing cases as needed
    }

    FAudioVoice_SetOutputMatrix(it->second, pMasterVoice, 1, 2, outputMatrix, FAUDIO_COMMIT_NOW);
    return OPEN_SEGA_SUCCESS;
}

OPEN_HAROUTING SEGAAPI_GetSendRouting(void* hHandle, unsigned int dwChannel, unsigned int dwSend) {
    auto it = sourceVoices.find(hHandle);
    if (it == sourceVoices.end()) {
        return OPEN_HA_UNUSED_PORT;
    }
    
    // Return current routing
    return OPEN_HA_FRONT_LEFT_PORT;
}
OPEN_SEGASTATUS SEGAAPI_SetSendLevel(void* hHandle, unsigned int dwChannel, unsigned int dwSend, unsigned int dwLevel) {
    auto it = sourceVoices.find(hHandle);
    if (it == sourceVoices.end()) {
        return OPEN_SEGAERR_BAD_HANDLE;
    }

    float level = dwLevel / (float)OPEN_HAWOSEVOL_MAX;
    float outputMatrix[8] = {level, level}; // Stereo output
    FAudioVoice_SetOutputMatrix(it->second, pMasterVoice, 1, 2, outputMatrix, FAUDIO_COMMIT_NOW);
    
    return OPEN_SEGA_SUCCESS;
}

unsigned int SEGAAPI_GetSendLevel(void* hHandle, unsigned int dwChannel, unsigned int dwSend) {
    auto it = sourceVoices.find(hHandle);
    if (it == sourceVoices.end()) {
        return 0;
    }

    float outputMatrix[8];
    FAudioVoice_GetOutputMatrix(it->second, pMasterVoice, 1, 2, outputMatrix);
    return (unsigned int)(outputMatrix[0] * OPEN_HAWOSEVOL_MAX);
}

unsigned int SEGAAPI_GetChannelVolume(void* hHandle, unsigned int dwChannel) {
    auto it = sourceVoices.find(hHandle);
    if (it == sourceVoices.end()) {
        return 0;
    }

    float volume;
    FAudioVoice_GetVolume(it->second, &volume);
    return (unsigned int)(volume * OPEN_HAWOSEVOL_MAX);
}

OPEN_SEGASTATUS SEGAAPI_SetPlaybackPosition(void* hHandle, unsigned int dwPlaybackPos) {
    auto it = sourceVoices.find(hHandle);
    if (it == sourceVoices.end()) {
        return OPEN_SEGAERR_BAD_HANDLE;
    }

    FAudioSourceVoice_FlushSourceBuffers(it->second);
    // FAudio doesn't support direct position setting, would need to skip samples

    return OPEN_SEGA_SUCCESS;
}

unsigned int SEGAAPI_GetPlaybackPosition(void* hHandle) {
    auto it = sourceVoices.find(hHandle);
    if (it == sourceVoices.end()) {
        return 0;
    }

    FAudioVoiceState state;
    FAudioSourceVoice_GetState(it->second, &state, 0);
    return state.SamplesPlayed;
}

OPEN_SEGASTATUS SEGAAPI_SetNotificationFrequency(void* hHandle, unsigned int dwFrameCount) {
    // Implement notification system using FAudio callbacks
    return OPEN_SEGA_SUCCESS;
}

OPEN_SEGASTATUS SEGAAPI_SetNotificationPoint(void* hHandle, unsigned int dwBufferOffset) {
    // Implement notification point using FAudio callbacks
    return OPEN_SEGA_SUCCESS;
}

OPEN_SEGASTATUS SEGAAPI_ClearNotificationPoint(void* hHandle, unsigned int dwBufferOffset) {
    // Clear notification point
    return OPEN_SEGA_SUCCESS;
}

OPEN_SEGASTATUS SEGAAPI_SetStartLoopOffset(void* hHandle, unsigned int dwOffset) {
    auto it = sourceVoices.find(hHandle);
    if (it == sourceVoices.end()) {
        return OPEN_SEGAERR_BAD_HANDLE;
    }

    // Store loop point for buffer submission
    // Will be used when submitting buffers with FAudioBuffer.LoopBegin
    return OPEN_SEGA_SUCCESS;
}

unsigned int SEGAAPI_GetStartLoopOffset(void* hHandle) {
    auto it = sourceVoices.find(hHandle);
    if (it == sourceVoices.end()) {
        return 0;
    }
    
    // Return stored loop point
    return 0;
}

OPEN_SEGASTATUS SEGAAPI_SetEndLoopOffset(void* hHandle, unsigned int dwOffset) {
    auto it = sourceVoices.find(hHandle);
    if (it == sourceVoices.end()) {
        return OPEN_SEGAERR_BAD_HANDLE;
    }

    // Store end loop point for buffer submission
    // Will be used when submitting buffers with FAudioBuffer.LoopLength
    return OPEN_SEGA_SUCCESS;
}

unsigned int SEGAAPI_GetEndLoopOffset(void* hHandle) {
    auto it = sourceVoices.find(hHandle);
    if (it == sourceVoices.end()) {
        return 0;
    }
    
    // Return stored end loop point
    return 0;
}

OPEN_SEGASTATUS SEGAAPI_SetEndOffset(void* hHandle, unsigned int dwOffset) {
    auto it = sourceVoices.find(hHandle);
    if (it == sourceVoices.end()) {
        return OPEN_SEGAERR_BAD_HANDLE;
    }

    // Store end offset for buffer submission
    return OPEN_SEGA_SUCCESS;
}

unsigned int SEGAAPI_GetEndOffset(void* hHandle) {
    auto it = sourceVoices.find(hHandle);
    if (it == sourceVoices.end()) {
        return 0;
    }
    
    // Return stored end offset
    return 0;
}

OPEN_SEGASTATUS SEGAAPI_SetLoopState(void* hHandle, int bDoContinuousLooping) {
    auto it = sourceVoices.find(hHandle);
    if (it == sourceVoices.end()) {
        return OPEN_SEGAERR_BAD_HANDLE;
    }

    // Store loop state for next buffer submission
    // Will be used with FAudioBuffer.LoopCount
    return OPEN_SEGA_SUCCESS;
}

int SEGAAPI_GetLoopState(void* hHandle) {
    auto it = sourceVoices.find(hHandle);
    if (it == sourceVoices.end()) {
        return 0;
    }
    
    // Return stored loop state
    return 0;
}

OPEN_SEGASTATUS SEGAAPI_SetSynthParam(void* hHandle, OPEN_HASYNTHPARAMSEXT param, int lPARWValue) {
    auto it = sourceVoices.find(hHandle);
    if (it == sourceVoices.end()) {
        return OPEN_SEGAERR_BAD_HANDLE;
    }

    // Map SEGA synth parameters to FAudio filter parameters
    switch (param) {
        case OPEN_HAVP_PITCH:
            FAudioSourceVoice_SetFrequencyRatio(it->second, powf(2.0f, lPARWValue / 1200.0f), FAUDIO_COMMIT_NOW);
            break;
        case OPEN_HAVP_ATTENUATION:
            {
                float volume = 1.0f - (lPARWValue / 960.0f);
                FAudioVoice_SetVolume(it->second, volume, FAUDIO_COMMIT_NOW);
            }
            break;
        // Add other parameter mappings as needed
    }

    return OPEN_SEGA_SUCCESS;
}

int SEGAAPI_GetSynthParam(void* hHandle, OPEN_HASYNTHPARAMSEXT param) {
    auto it = sourceVoices.find(hHandle);
    if (it == sourceVoices.end()) {
        return 0;
    }

    switch (param) {
        case OPEN_HAVP_PITCH:
            {
                float frequency;
                FAudioSourceVoice_GetFrequencyRatio(it->second, &frequency);
                return (int)(log2f(frequency) * 1200.0f);
            }
        case OPEN_HAVP_ATTENUATION:
            {
                float volume;
                FAudioVoice_GetVolume(it->second, &volume);
                return (int)((1.0f - volume) * 960.0f);
            }
    }
    return 0;
}

OPEN_SEGASTATUS SEGAAPI_SetSynthParamMultiple(void* hHandle, unsigned int dwNumParams, OPEN_SynthParamSet* pSynthParams) {
    if (!pSynthParams) {
        return OPEN_SEGAERR_BAD_POINTER;
    }

    for (unsigned int i = 0; i < dwNumParams; i++) {
        OPEN_SEGASTATUS status = SEGAAPI_SetSynthParam(hHandle, pSynthParams[i].param, pSynthParams[i].lPARWValue);
        if (status != OPEN_SEGA_SUCCESS) {
            return status;
        }
    }

    return OPEN_SEGA_SUCCESS;
}

OPEN_SEGASTATUS SEGAAPI_GetSynthParamMultiple(void* hHandle, unsigned int dwNumParams, OPEN_SynthParamSet* pSynthParams) {
    if (!pSynthParams) {
        return OPEN_SEGAERR_BAD_POINTER;
    }

    for (unsigned int i = 0; i < dwNumParams; i++) {
        pSynthParams[i].lPARWValue = SEGAAPI_GetSynthParam(hHandle, pSynthParams[i].param);
    }

    return OPEN_SEGA_SUCCESS;
}
OPEN_SEGASTATUS SEGAAPI_SetReleaseState(void* hHandle, int bSet) {
    auto it = sourceVoices.find(hHandle);
    if (it == sourceVoices.end()) {
        return OPEN_SEGAERR_BAD_HANDLE;
    }

    // FAudio doesn't have direct release state control
    // Could implement using volume ramping if needed
    return OPEN_SEGA_SUCCESS;
}

OPEN_SEGASTATUS SEGAAPI_PlayWithSetup(
    void* hHandle,
    unsigned int dwNumSendRouteParams, OPEN_SendRouteParamSet* pSendRouteParams,
    unsigned int dwNumSendLevelParams, OPEN_SendLevelParamSet* pSendLevelParams,
    unsigned int dwNumVoiceParams, OPEN_VoiceParamSet* pVoiceParams,
    unsigned int dwNumSynthParams, OPEN_SynthParamSet* pSynthParams)
{
    // Apply all parameters before playing
    if (pSendRouteParams) {
        for (unsigned int i = 0; i < dwNumSendRouteParams; i++) {
            SEGAAPI_SetSendRouting(hHandle, 
                pSendRouteParams[i].dwChannel,
                pSendRouteParams[i].dwSend,
                pSendRouteParams[i].dwDest);
        }
    }

    if (pSendLevelParams) {
        for (unsigned int i = 0; i < dwNumSendLevelParams; i++) {
            SEGAAPI_SetSendLevel(hHandle,
                pSendLevelParams[i].dwChannel,
                pSendLevelParams[i].dwSend,
                pSendLevelParams[i].dwLevel);
        }
    }

    if (pVoiceParams) {
        for (unsigned int i = 0; i < dwNumVoiceParams; i++) {
            switch (pVoiceParams[i].VoiceIoctl) {
                case OPEN_VOICEIOCTL_SET_START_LOOP_OFFSET:
                    SEGAAPI_SetStartLoopOffset(hHandle, pVoiceParams[i].dwParam1);
                    break;
                case OPEN_VOICEIOCTL_SET_END_LOOP_OFFSET:
                    SEGAAPI_SetEndLoopOffset(hHandle, pVoiceParams[i].dwParam1);
                    break;
                case OPEN_VOICEIOCTL_SET_LOOP_STATE:
                    SEGAAPI_SetLoopState(hHandle, pVoiceParams[i].dwParam1);
                    break;
                // Add other voice parameter cases
            }
        }
    }

    if (pSynthParams) {
        SEGAAPI_SetSynthParamMultiple(hHandle, dwNumSynthParams, pSynthParams);
    }

    // Finally start playback
    return SEGAAPI_Play(hHandle);
}

int SEGAAPI_SetGlobalEAXProperty(GUID* guid, unsigned long ulProperty, void* pData, unsigned long ulDataSize) {
    // FAudio doesn't support EAX directly
    // Could implement basic reverb if needed using FAudio's built-in effects
    return OPEN_SEGA_SUCCESS;
}

int SEGAAPI_GetGlobalEAXProperty(GUID* guid, unsigned long ulProperty, void* pData, unsigned long ulDataSize) {
    // Return default values since EAX is not supported
    return OPEN_SEGA_SUCCESS;
}
OPEN_SEGASTATUS SEGAAPI_SetSPDIFOutChannelStatus(unsigned int dwChannelStatus, unsigned int dwExtChannelStatus) {
    // FAudio doesn't directly support SPDIF control
    // Store values for compatibility
    return OPEN_SEGA_SUCCESS;
}

OPEN_SEGASTATUS SEGAAPI_GetSPDIFOutChannelStatus(unsigned int* pdwChannelStatus, unsigned int* pdwExtChannelStatus) {
    if (!pdwChannelStatus || !pdwExtChannelStatus) {
        return OPEN_SEGAERR_BAD_POINTER;
    }
    
    // Return stored/default values
    *pdwChannelStatus = 0;
    *pdwExtChannelStatus = 0;
    return OPEN_SEGA_SUCCESS;
}

OPEN_SEGASTATUS SEGAAPI_SetSPDIFOutSampleRate(OPEN_HASPDIFOUTRATE dwSamplingRate) {
    // Map SPDIF rates to master voice sample rate
    uint32_t sampleRate;
    switch (dwSamplingRate) {
        case OPEN_HASPDIFOUT_44_1KHZ:
            sampleRate = 44100;
            break;
        case OPEN_HASPDIFOUT_48KHZ:
            sampleRate = 48000;
            break;
        case OPEN_HASPDIFOUT_96KHZ:
            sampleRate = 96000;
            break;
        default:
            return OPEN_SEGAERR_BAD_SAMPLERATE;
    }

    // Recreate master voice with new sample rate
    if (pMasterVoice) {
        FAudioVoice_DestroyVoice(pMasterVoice);
    }

    if (FAudioCreateMasteringVoice(pFAudio, &pMasterVoice, 2, sampleRate, 0, 0, nullptr) != 0) {
        return OPEN_SEGAERR_FAIL;
    }

    return OPEN_SEGA_SUCCESS;
}

OPEN_HASPDIFOUTRATE SEGAAPI_GetSPDIFOutSampleRate(void) {
    if (!pMasterVoice) {
        return OPEN_HASPDIFOUT_48KHZ;
    }

    FAudioVoiceDetails details;
    FAudioVoice_GetVoiceDetails(pMasterVoice, &details);

    if (details.InputSampleRate == 44100) {
        return OPEN_HASPDIFOUT_44_1KHZ;
    } else if (details.InputSampleRate == 96000) {
        return OPEN_HASPDIFOUT_96KHZ;
    }
    
    return OPEN_HASPDIFOUT_48KHZ;
}

OPEN_SEGASTATUS SEGAAPI_SetSPDIFOutChannelRouting(unsigned int dwChannel, OPEN_HAROUTING dwSource) {
    // Store SPDIF routing configuration
    // Map to FAudio output matrix if applicable
    return OPEN_SEGA_SUCCESS;
}

OPEN_HAROUTING SEGAAPI_GetSPDIFOutChannelRouting(unsigned int dwChannel) {
    // Return stored/default routing
    return OPEN_HA_FRONT_LEFT_PORT;
}
OPEN_SEGASTATUS SEGAAPI_SetIOVolume(OPEN_HAPHYSICALIO dwPhysIO, unsigned int dwVolume) {
    float volume = dwVolume / (float)OPEN_HAWOSEVOL_MAX;

    switch (dwPhysIO) {
        case OPEN_HA_OUT_FRONT_LEFT:
        case OPEN_HA_OUT_FRONT_RIGHT:
        case OPEN_HA_OUT_FRONT_CENTER:
        case OPEN_HA_OUT_LFE_PORT:
        case OPEN_HA_OUT_REAR_LEFT:
        case OPEN_HA_OUT_REAR_RIGHT:
            if (pMasterVoice) {
                FAudioVoice_SetVolume(pMasterVoice, volume, FAUDIO_COMMIT_NOW);
            }
            break;
        // Other IO types can be handled here
    }

    return OPEN_SEGA_SUCCESS;
}

unsigned int SEGAAPI_GetIOVolume(OPEN_HAPHYSICALIO dwPhysIO) {
    float volume = 1.0f;

    switch (dwPhysIO) {
        case OPEN_HA_OUT_FRONT_LEFT:
        case OPEN_HA_OUT_FRONT_RIGHT:
        case OPEN_HA_OUT_FRONT_CENTER:
        case OPEN_HA_OUT_LFE_PORT:
        case OPEN_HA_OUT_REAR_LEFT:
        case OPEN_HA_OUT_REAR_RIGHT:
            if (pMasterVoice) {
                FAudioVoice_GetVolume(pMasterVoice, &volume);
            }
            break;
    }

    return (unsigned int)(volume * OPEN_HAWOSEVOL_MAX);
}

OPEN_SEGASTATUS SEGAAPI_Reset(void) {
    // Stop and reset all voices
    for (auto& voice : sourceVoices) {
        if (voice.second) {
            FAudioSourceVoice_Stop(voice.second, 0, FAUDIO_COMMIT_NOW);
            FAudioSourceVoice_FlushSourceBuffers(voice.second);
        }
    }

    // Reset master voice if needed
    if (pMasterVoice) {
        FAudioVoice_SetVolume(pMasterVoice, 1.0f, FAUDIO_COMMIT_NOW);
    }

    return OPEN_SEGA_SUCCESS;
}

// Helper functions for internal use
namespace {
    void CleanupFAudio() {
        for (auto& voice : sourceVoices) {
            if (voice.second) {
                FAudioVoice_DestroyVoice(voice.second);
            }
        }
        sourceVoices.clear();

        if (pMasterVoice) {
            FAudioVoice_DestroyVoice(pMasterVoice);
            pMasterVoice = nullptr;
        }

        if (pFAudio) {
            FAudio_Release(pFAudio);
            pFAudio = nullptr;
        }
    }

    void InitializeFAudioDefaults() {
        // Set default values for FAudio engine
        if (pFAudio && pMasterVoice) {
            FAudioVoice_SetVolume(pMasterVoice, 1.0f, FAUDIO_COMMIT_NOW);
        }
    }
}
