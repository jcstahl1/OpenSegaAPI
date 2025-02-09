// opensegaapi.cpp - Complete OpenAL Conversion of the OpenSEGA API
//
// This file is part of the OpenParrot project - https://teknoparrot.com / https://github.com/teknogods


extern "C" {
#include "opensegaapi.h"
}

#include <AL/al.h>
#include <AL/alc.h>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <cstdarg>
#include <cstdio>

// ======================================================================
// Global status and helper functions
// ======================================================================
static OPEN_SEGASTATUS g_lastStatus = OPEN_SEGA_SUCCESS;
static OPEN_SEGASTATUS SetStatus(OPEN_SEGASTATUS status) {
    g_lastStatus = status;
    return status;
}

// Debug logging (only if _DEBUG is defined)
#ifdef _DEBUG
void info(const char* format, ...) {
    char buffer[1024];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    strcat(buffer, "\n");
    OutputDebugStringA(buffer);
}
#else
#define info(x) {}
#endif

// ======================================================================
// Global OpenAL device and context
// ======================================================================
static ALCdevice* g_alDevice = nullptr;
static ALCcontext* g_alContext = nullptr;

// ======================================================================
// Internal Buffer Structure (converted from XAudio2 version)
// ======================================================================
struct OPEN_segaapiBuffer_t {
    // OpenAL objects
    ALuint alBuffer;
    ALuint alSource;
    
    // Audio parameters
    unsigned int sampleRate;
    unsigned int channels;
    unsigned int size;      // size in bytes
    uint8_t* data;          // pointer to audio data
    
    // Playback state
    bool playing;
    bool loop;
    unsigned int currentPosition; // byte offset
    
    // Looping offsets
    unsigned int startLoop;
    unsigned int endLoop;
    unsigned int endOffset;
    
    // Additional properties
    unsigned int priority;
    void* userData;
    
    // Routing / volume parameters (stored only; not used by OpenAL directly)
    float sendVolumes[MAX_ROUTES];
    int sendChannels[MAX_ROUTES];
    OPEN_HAROUTING sendRoutes[MAX_ROUTES];
    float channelVolumes[6];
    
    // (Synthesizer and deferred callback members from the original are omitted or stubbed.)  
};

// ======================================================================
// Utility: Calculate frame (sample) size (assuming 16-bit PCM)
// ======================================================================
static unsigned int bufferSampleSize(OPEN_segaapiBuffer_t* buffer) {
    // Assumes 16-bit PCM (2 bytes per sample per channel)
    return buffer->channels * 2;
}

// ======================================================================
// SEGAAPI_Init / SEGAAPI_Exit
// ======================================================================
extern "C" __declspec(dllexport) OPEN_SEGASTATUS SEGAAPI_Init(void) {
    info("SEGAAPI_Init (OpenAL)");
    g_alDevice = alcOpenDevice(nullptr);
    if (!g_alDevice) return SetStatus(OPEN_SEGAERR_UNKNOWN);
    g_alContext = alcCreateContext(g_alDevice, nullptr);
    if (!g_alContext) {
        alcCloseDevice(g_alDevice);
        return SetStatus(OPEN_SEGAERR_UNKNOWN);
    }
    alcMakeContextCurrent(g_alContext);
    return SetStatus(OPEN_SEGA_SUCCESS);
}

extern "C" __declspec(dllexport) OPEN_SEGASTATUS SEGAAPI_Exit(void) {
    info("SEGAAPI_Exit (OpenAL)");
    alcMakeContextCurrent(nullptr);
    if (g_alContext) { alcDestroyContext(g_alContext); g_alContext = nullptr; }
    if (g_alDevice) { alcCloseDevice(g_alDevice); g_alDevice = nullptr; }
    return SetStatus(OPEN_SEGA_SUCCESS);
}

// ======================================================================
// SEGAAPI_CreateBuffer
// ======================================================================
extern "C" __declspec(dllexport) OPEN_SEGASTATUS SEGAAPI_CreateBuffer(
    OPEN_HAWOSEBUFFERCONFIG* pConfig, 
    OPEN_HAWOSEGABUFFERCALLBACK pCallback, 
    unsigned int dwFlags, 
    void** phHandle) 
{
    if (!pConfig || !phHandle) {
        info("SEGAAPI_CreateBuffer: Bad pointer");
        return SetStatus(OPEN_SEGAERR_BAD_POINTER);
    }
    try {
        auto* buffer = new OPEN_segaapiBuffer_t();
        info("SEGAAPI_CreateBuffer: Creating buffer at %p", (void*)buffer);
        
        // Initialize basic properties from configuration
        buffer->sampleRate = pConfig->dwSampleRate;
        buffer->channels   = pConfig->byNumChans;
        buffer->size       = pConfig->mapData.dwSize;
        if (dwFlags & OPEN_HABUF_ALLOC_USER_MEM || dwFlags & OPEN_HABUF_USE_MAPPED_MEM) {
            buffer->data = static_cast<uint8_t*>(pConfig->mapData.hBufferHdr);
        } else {
            buffer->data = static_cast<uint8_t*>(malloc(buffer->size));
            if (!buffer->data) { delete buffer; return SetStatus(OPEN_SEGAERR_OUT_OF_MEMORY); }
        }
        pConfig->mapData.hBufferHdr = buffer->data;
        
        buffer->playing = false;
        buffer->loop = false;
        buffer->currentPosition = 0;
        buffer->startLoop = 0;
        buffer->endLoop = buffer->size;
        buffer->endOffset = buffer->size;
        buffer->priority = pConfig->dwPriority;
        buffer->userData = pConfig->hUserData;
        
        // Initialize routing defaults
        for (int i = 0; i < MAX_ROUTES; i++) {
            buffer->sendVolumes[i] = 0.0f;
            buffer->sendChannels[i] = 0;
            buffer->sendRoutes[i] = OPEN_HA_UNUSED_PORT;
        }
        for (int i = 0; i < 6; i++) {
            buffer->channelVolumes[i] = 1.0f;
        }
        
        // Generate OpenAL objects
        alGenBuffers(1, &buffer->alBuffer);
        alGenSources(1, &buffer->alSource);
        
        // Choose format – assuming 16-bit PCM (adjust if necessary)
        ALenum format = (buffer->channels == 1) ? AL_FORMAT_MONO16 : AL_FORMAT_STEREO16;
        alBufferData(buffer->alBuffer, format, buffer->data, buffer->size, buffer->sampleRate);
        alSourcei(buffer->alSource, AL_BUFFER, buffer->alBuffer);
        
        *phHandle = buffer;
        return SetStatus(OPEN_SEGA_SUCCESS);
    } catch (...) {
        return SetStatus(OPEN_SEGAERR_UNKNOWN);
    }
}

// ======================================================================
// SEGAAPI_DestroyBuffer
// ======================================================================
extern "C" __declspec(dllexport) OPEN_SEGASTATUS SEGAAPI_DestroyBuffer(void* hHandle) {
    if (!hHandle) {
        info("SEGAAPI_DestroyBuffer: Bad handle");
        return SetStatus(OPEN_SEGAERR_BAD_HANDLE);
    }
    info("SEGAAPI_DestroyBuffer: Handle %p", hHandle);
    try {
        auto* buffer = static_cast<OPEN_segaapiBuffer_t*>(hHandle);
        alSourceStop(buffer->alSource);
        alDeleteSources(1, &buffer->alSource);
        alDeleteBuffers(1, &buffer->alBuffer);
        // Free audio data if it was allocated by this API
        if (!(buffer->priority & (OPEN_HABUF_ALLOC_USER_MEM | OPEN_HABUF_USE_MAPPED_MEM))) {
            free(buffer->data);
        }
        delete buffer;
        return SetStatus(OPEN_SEGA_SUCCESS);
    } catch (...) {
        return SetStatus(OPEN_SEGAERR_UNKNOWN);
    }
}

// ======================================================================
// SEGAAPI_SetUserData / SEGAAPI_GetUserData
// ======================================================================
extern "C" __declspec(dllexport) OPEN_SEGASTATUS SEGAAPI_SetUserData(void* hHandle, void* hUserData) {
    if (!hHandle) {
        info("SEGAAPI_SetUserData: Bad handle");
        return SetStatus(OPEN_SEGAERR_BAD_HANDLE);
    }
    auto* buffer = static_cast<OPEN_segaapiBuffer_t*>(hHandle);
    buffer->userData = hUserData;
    return SetStatus(OPEN_SEGA_SUCCESS);
}

extern "C" __declspec(dllexport) void* SEGAAPI_GetUserData(void* hHandle) {
    if (!hHandle) {
        info("SEGAAPI_GetUserData: Bad handle");
        SetStatus(OPEN_SEGAERR_BAD_HANDLE);
        return nullptr;
    }
    auto* buffer = static_cast<OPEN_segaapiBuffer_t*>(hHandle);
    return buffer->userData;
}

// ======================================================================
// SEGAAPI_SetFormat / SEGAAPI_GetFormat
// ======================================================================
extern "C" __declspec(dllexport) OPEN_SEGASTATUS SEGAAPI_SetFormat(void* hHandle, OPEN_HAWOSEFORMAT* pFormat) {
    if (!hHandle || !pFormat) return SetStatus(OPEN_SEGAERR_BAD_POINTER);
    auto* buffer = static_cast<OPEN_segaapiBuffer_t*>(hHandle);
    buffer->sampleRate = pFormat->dwSampleRate;
    buffer->channels   = pFormat->byNumChans;
    ALenum formatEnum = (buffer->channels == 1) ? AL_FORMAT_MONO16 : AL_FORMAT_STEREO16;
    alBufferData(buffer->alBuffer, formatEnum, buffer->data, buffer->size, buffer->sampleRate);
    return SetStatus(OPEN_SEGA_SUCCESS);
}

extern "C" __declspec(dllexport) OPEN_SEGASTATUS SEGAAPI_GetFormat(void* hHandle, OPEN_HAWOSEFORMAT* pFormat) {
    if (!hHandle || !pFormat) return SetStatus(OPEN_SEGAERR_BAD_POINTER);
    auto* buffer = static_cast<OPEN_segaapiBuffer_t*>(hHandle);
    pFormat->dwSampleRate = buffer->sampleRate;
    pFormat->byNumChans = buffer->channels;
    pFormat->dwSampleFormat = OPEN_HASF_SIGNED_16PCM; // assumed format
    return SetStatus(OPEN_SEGA_SUCCESS);
}

// ======================================================================
// SEGAAPI_SetSampleRate / SEGAAPI_GetSampleRate
// ======================================================================
extern "C" __declspec(dllexport) OPEN_SEGASTATUS SEGAAPI_SetSampleRate(void* hHandle, unsigned int dwSampleRate) {
    if (!hHandle) return SetStatus(OPEN_SEGAERR_BAD_HANDLE);
    if (dwSampleRate < 8000 || dwSampleRate > 192000) return SetStatus(OPEN_SEGAERR_BAD_PARAM);
    auto* buffer = static_cast<OPEN_segaapiBuffer_t*>(hHandle);
    buffer->sampleRate = dwSampleRate;
    ALenum formatEnum = (buffer->channels == 1) ? AL_FORMAT_MONO16 : AL_FORMAT_STEREO16;
    alBufferData(buffer->alBuffer, formatEnum, buffer->data, buffer->size, buffer->sampleRate);
    return SetStatus(OPEN_SEGA_SUCCESS);
}

extern "C" __declspec(dllexport) unsigned int SEGAAPI_GetSampleRate(void* hHandle) {
    if (!hHandle) { SetStatus(OPEN_SEGAERR_BAD_HANDLE); return 0; }
    auto* buffer = static_cast<OPEN_segaapiBuffer_t*>(hHandle);
    return buffer->sampleRate;
}

// ======================================================================
// SEGAAPI_SetPriority / SEGAAPI_GetPriority
// ======================================================================
extern "C" __declspec(dllexport) OPEN_SEGASTATUS SEGAAPI_SetPriority(void* hHandle, unsigned int dwPriority) {
    if (!hHandle) return SetStatus(OPEN_SEGAERR_BAD_HANDLE);
    auto* buffer = static_cast<OPEN_segaapiBuffer_t*>(hHandle);
    buffer->priority = dwPriority;
    return SetStatus(OPEN_SEGA_SUCCESS);
}

extern "C" __declspec(dllexport) unsigned int SEGAAPI_GetPriority(void* hHandle) {
    if (!hHandle) { SetStatus(OPEN_SEGAERR_BAD_HANDLE); return 0; }
    auto* buffer = static_cast<OPEN_segaapiBuffer_t*>(hHandle);
    return buffer->priority;
}

// ======================================================================
// SEGAAPI_SetSendRouting / SEGAAPI_GetSendRouting
// (Routing data is stored only; OpenAL does not support submix routing.) 
// ======================================================================
extern "C" __declspec(dllexport) OPEN_SEGASTATUS SEGAAPI_SetSendRouting(void* hHandle, unsigned int dwChannel, unsigned int dwSend, OPEN_HAROUTING dwDest) {
    if (!hHandle) return SetStatus(OPEN_SEGAERR_BAD_HANDLE);
    auto* buffer = static_cast<OPEN_segaapiBuffer_t*>(hHandle);
    if (dwSend >= MAX_ROUTES || dwChannel >= buffer->channels) return SetStatus(OPEN_SEGAERR_INVALID_PARAM);
    buffer->sendRoutes[dwSend] = dwDest;
    buffer->sendChannels[dwSend] = dwChannel;
    return SetStatus(OPEN_SEGA_SUCCESS);
}

extern "C" __declspec(dllexport) OPEN_HAROUTING SEGAAPI_GetSendRouting(void* hHandle, unsigned int dwChannel, unsigned int dwSend) {
    if (!hHandle) { SetStatus(OPEN_SEGAERR_BAD_HANDLE); return OPEN_HA_UNUSED_PORT; }
    auto* buffer = static_cast<OPEN_segaapiBuffer_t*>(hHandle);
    if (dwSend >= MAX_ROUTES || dwChannel >= buffer->channels) { SetStatus(OPEN_SEGAERR_INVALID_PARAM); return OPEN_HA_UNUSED_PORT; }
    return buffer->sendRoutes[dwSend];
}

// ======================================================================
// SEGAAPI_SetSendLevel / SEGAAPI_GetSendLevel
// ======================================================================
extern "C" __declspec(dllexport) OPEN_SEGASTATUS SEGAAPI_SetSendLevel(void* hHandle, unsigned int dwChannel, unsigned int dwSend, unsigned int dwLevel) {
    if (!hHandle) return SetStatus(OPEN_SEGAERR_BAD_HANDLE);
    auto* buffer = static_cast<OPEN_segaapiBuffer_t*>(hHandle);
    if (dwSend >= MAX_ROUTES || dwChannel >= buffer->channels) return SetStatus(OPEN_SEGAERR_INVALID_PARAM);
    constexpr float MAX_LEVEL = static_cast<float>(0xFFFFFFFF);
    buffer->sendVolumes[dwSend] = dwLevel / MAX_LEVEL;
    buffer->sendChannels[dwSend] = dwChannel;
    return SetStatus(OPEN_SEGA_SUCCESS);
}

extern "C" __declspec(dllexport) unsigned int SEGAAPI_GetSendLevel(void* hHandle, unsigned int dwChannel, unsigned int dwSend) {
    if (!hHandle) { SetStatus(OPEN_SEGAERR_BAD_HANDLE); return 0; }
    auto* buffer = static_cast<OPEN_segaapiBuffer_t*>(hHandle);
    if (dwSend >= MAX_ROUTES || dwChannel >= buffer->channels) { SetStatus(OPEN_SEGAERR_INVALID_PARAM); return 0; }
    constexpr float MAX_LEVEL = static_cast<float>(0xFFFFFFFF);
    return static_cast<unsigned int>(buffer->sendVolumes[dwSend] * MAX_LEVEL);
}

// ======================================================================
// SEGAAPI_SetChannelVolume / SEGAAPI_GetChannelVolume
// (Stored only; per-channel mixing not directly implemented in OpenAL.)
// ======================================================================
extern "C" __declspec(dllexport) OPEN_SEGASTATUS SEGAAPI_SetChannelVolume(void* hHandle, unsigned int dwChannel, unsigned int dwVolume) {
    if (!hHandle) return SetStatus(OPEN_SEGAERR_BAD_HANDLE);
    auto* buffer = static_cast<OPEN_segaapiBuffer_t*>(hHandle);
    if (dwChannel >= buffer->channels) return SetStatus(OPEN_SEGAERR_INVALID_PARAM);
    constexpr float MAX_VOLUME = static_cast<float>(0xFFFFFFFF);
    buffer->channelVolumes[dwChannel] = dwVolume / MAX_VOLUME;
    return SetStatus(OPEN_SEGA_SUCCESS);
}

extern "C" __declspec(dllexport) unsigned int SEGAAPI_GetChannelVolume(void* hHandle, unsigned int dwChannel) {
    if (!hHandle) { SetStatus(OPEN_SEGAERR_BAD_HANDLE); return 0; }
    auto* buffer = static_cast<OPEN_segaapiBuffer_t*>(hHandle);
    if (dwChannel >= buffer->channels) { SetStatus(OPEN_SEGAERR_INVALID_PARAM); return 0; }
    constexpr float MAX_VOLUME = static_cast<float>(0xFFFFFFFF);
    return static_cast<unsigned int>(buffer->channelVolumes[dwChannel] * MAX_VOLUME);
}

// ======================================================================
// SEGAAPI_SetPlaybackPosition / SEGAAPI_GetPlaybackPosition
// ======================================================================
extern "C" __declspec(dllexport) OPEN_SEGASTATUS SEGAAPI_SetPlaybackPosition(void* hHandle, unsigned int dwPlaybackPos) {
    if (!hHandle) return SetStatus(OPEN_SEGAERR_BAD_HANDLE);
    auto* buffer = static_cast<OPEN_segaapiBuffer_t*>(hHandle);
    if (dwPlaybackPos > buffer->size) return SetStatus(OPEN_SEGAERR_BAD_PARAM);
    unsigned int sampleSize = bufferSampleSize(buffer);
    unsigned int sampleOffset = dwPlaybackPos / sampleSize;
    alSourceStop(buffer->alSource);
    alSourcei(buffer->alSource, AL_SAMPLE_OFFSET, sampleOffset);
    buffer->currentPosition = dwPlaybackPos;
    if (buffer->playing) alSourcePlay(buffer->alSource);
    return SetStatus(OPEN_SEGA_SUCCESS);
}

extern "C" __declspec(dllexport) unsigned int SEGAAPI_GetPlaybackPosition(void* hHandle) {
    if (!hHandle) { SetStatus(OPEN_SEGAERR_BAD_HANDLE); return 0; }
    auto* buffer = static_cast<OPEN_segaapiBuffer_t*>(hHandle);
    ALint sampleOffset = 0;
    alGetSourcei(buffer->alSource, AL_SAMPLE_OFFSET, &sampleOffset);
    unsigned int byteOffset = sampleOffset * bufferSampleSize(buffer);
    return byteOffset;
}

// ======================================================================
// Notification functions (stubs since OpenAL does not provide callbacks)
// ======================================================================
extern "C" __declspec(dllexport) OPEN_SEGASTATUS SEGAAPI_SetNotificationFrequency(void* hHandle, unsigned int dwFrameCount) {
    return SetStatus(OPEN_SEGA_SUCCESS);
}

extern "C" __declspec(dllexport) OPEN_SEGASTATUS SEGAAPI_SetNotificationPoint(void* hHandle, unsigned int dwBufferOffset) {
    return SetStatus(OPEN_SEGA_SUCCESS);
}

extern "C" __declspec(dllexport) OPEN_SEGASTATUS SEGAAPI_ClearNotificationPoint(void* hHandle, unsigned int dwBufferOffset) {
    return SetStatus(OPEN_SEGA_SUCCESS);
}

// ======================================================================
// Loop offsets
// ======================================================================
extern "C" __declspec(dllexport) OPEN_SEGASTATUS SEGAAPI_SetStartLoopOffset(void* hHandle, unsigned int dwOffset) {
    if (!hHandle) return SetStatus(OPEN_SEGAERR_BAD_HANDLE);
    auto* buffer = static_cast<OPEN_segaapiBuffer_t*>(hHandle);
    if (dwOffset > buffer->size) return SetStatus(OPEN_SEGAERR_BAD_PARAM);
    buffer->startLoop = dwOffset;
    return SetStatus(OPEN_SEGA_SUCCESS);
}

extern "C" __declspec(dllexport) unsigned int SEGAAPI_GetStartLoopOffset(void* hHandle) {
    if (!hHandle) { SetStatus(OPEN_SEGAERR_BAD_HANDLE); return 0; }
    auto* buffer = static_cast<OPEN_segaapiBuffer_t*>(hHandle);
    return buffer->startLoop;
}

extern "C" __declspec(dllexport) OPEN_SEGASTATUS SEGAAPI_SetEndLoopOffset(void* hHandle, unsigned int dwOffset) {
    if (!hHandle) return SetStatus(OPEN_SEGAERR_BAD_HANDLE);
    auto* buffer = static_cast<OPEN_segaapiBuffer_t*>(hHandle);
    if (dwOffset > buffer->size) return SetStatus(OPEN_SEGAERR_BAD_PARAM);
    buffer->endLoop = dwOffset;
    return SetStatus(OPEN_SEGA_SUCCESS);
}

extern "C" __declspec(dllexport) unsigned int SEGAAPI_GetEndLoopOffset(void* hHandle) {
    if (!hHandle) { SetStatus(OPEN_SEGAERR_BAD_HANDLE); return 0; }
    auto* buffer = static_cast<OPEN_segaapiBuffer_t*>(hHandle);
    return buffer->endLoop;
}

extern "C" __declspec(dllexport) OPEN_SEGASTATUS SEGAAPI_SetEndOffset(void* hHandle, unsigned int dwOffset) {
    if (!hHandle) return SetStatus(OPEN_SEGAERR_BAD_HANDLE);
    auto* buffer = static_cast<OPEN_segaapiBuffer_t*>(hHandle);
    if (dwOffset > buffer->size) return SetStatus(OPEN_SEGAERR_BAD_PARAM);
    buffer->endOffset = dwOffset;
    return SetStatus(OPEN_SEGA_SUCCESS);
}

extern "C" __declspec(dllexport) unsigned int SEGAAPI_GetEndOffset(void* hHandle) {
    if (!hHandle) { SetStatus(OPEN_SEGAERR_BAD_HANDLE); return 0; }
    auto* buffer = static_cast<OPEN_segaapiBuffer_t*>(hHandle);
    return buffer->endOffset;
}

// ======================================================================
// Loop state
// ======================================================================
extern "C" __declspec(dllexport) OPEN_SEGASTATUS SEGAAPI_SetLoopState(void* hHandle, int bDoContinuousLooping) {
    if (!hHandle) return SetStatus(OPEN_SEGAERR_BAD_HANDLE);
    auto* buffer = static_cast<OPEN_segaapiBuffer_t*>(hHandle);
    buffer->loop = (bDoContinuousLooping != 0);
    alSourcei(buffer->alSource, AL_LOOPING, buffer->loop ? AL_TRUE : AL_FALSE);
    return SetStatus(OPEN_SEGA_SUCCESS);
}

extern "C" __declspec(dllexport) int SEGAAPI_GetLoopState(void* hHandle) {
    if (!hHandle) { SetStatus(OPEN_SEGAERR_BAD_HANDLE); return 0; }
    auto* buffer = static_cast<OPEN_segaapiBuffer_t*>(hHandle);
    return buffer->loop ? 1 : 0;
}

// ======================================================================
// SEGAAPI_UpdateBuffer
// (For streaming updates, re-buffer the entire data)
// ======================================================================
extern "C" __declspec(dllexport) OPEN_SEGASTATUS SEGAAPI_UpdateBuffer(void* hHandle, unsigned int dwStartOffset, unsigned int dwLength) {
    if (!hHandle) return SetStatus(OPEN_SEGAERR_BAD_HANDLE);
    auto* buffer = static_cast<OPEN_segaapiBuffer_t*>(hHandle);
    if (dwStartOffset + dwLength > buffer->size) return SetStatus(OPEN_SEGAERR_BAD_PARAM);
    ALenum formatEnum = (buffer->channels == 1) ? AL_FORMAT_MONO16 : AL_FORMAT_STEREO16;
    alBufferData(buffer->alBuffer, formatEnum, buffer->data, buffer->size, buffer->sampleRate);
    return SetStatus(OPEN_SEGA_SUCCESS);
}

// ======================================================================
// Synth Parameters (only attenuation and pitch are implemented)
// ======================================================================
extern "C" __declspec(dllexport) OPEN_SEGASTATUS SEGAAPI_SetSynthParam(void* hHandle, OPEN_HASYNTHPARAMSEXT param, int lPARWValue) {
    if (!hHandle) return SetStatus(OPEN_SEGAERR_BAD_HANDLE);
    auto* buffer = static_cast<OPEN_segaapiBuffer_t*>(hHandle);
    if (param == OPEN_HAVP_ATTENUATION) {
        // Convert dB*10 to gain (example conversion)
        float volume = powf(10.0f, -lPARWValue / 200.0f);
        alSourcef(buffer->alSource, AL_GAIN, volume);
        info("SEGAAPI_SetSynthParam: Attenuation set, gain = %f", volume);
    } else if (param == OPEN_HAVP_PITCH) {
        float semitones = lPARWValue / 100.0f;
        float pitchFactor = powf(2.0f, semitones / 12.0f);
        alSourcef(buffer->alSource, AL_PITCH, pitchFactor);
        info("SEGAAPI_SetSynthParam: Pitch set, factor = %f", pitchFactor);
    } 
    // Additional parameters can be handled as needed.
    return SetStatus(OPEN_SEGA_SUCCESS);
}

extern "C" __declspec(dllexport) int SEGAAPI_GetSynthParam(void* hHandle, OPEN_HASYNTHPARAMSEXT param) {
    if (!hHandle) { SetStatus(OPEN_SEGAERR_BAD_HANDLE); return 0; }
    auto* buffer = static_cast<OPEN_segaapiBuffer_t*>(hHandle);
    float value = 0.0f;
    if (param == OPEN_HAVP_ATTENUATION) {
        alGetSourcef(buffer->alSource, AL_GAIN, &value);
        float dB = -20.0f * log10f(value);
        return static_cast<int>(dB * 10);
    } else if (param == OPEN_HAVP_PITCH) {
        alGetSourcef(buffer->alSource, AL_PITCH, &value);
        float semitones = 12.0f * logf(value) / logf(2.0f);
        return static_cast<int>(semitones * 100);
    }
    return 0;
}

extern "C" __declspec(dllexport) OPEN_SEGASTATUS SEGAAPI_SetSynthParamMultiple(void* hHandle, unsigned int dwNumParams, OPEN_SynthParamSet* pSynthParams) {
    if (!hHandle || !pSynthParams || dwNumParams == 0) return SetStatus(OPEN_SEGAERR_INVALID_PARAM);
    for (unsigned int i = 0; i < dwNumParams; i++) {
        OPEN_SEGASTATUS status = SEGAAPI_SetSynthParam(hHandle, pSynthParams[i].param, pSynthParams[i].lPARWValue);
        if (status != OPEN_SEGA_SUCCESS) return SetStatus(status);
    }
    return SetStatus(OPEN_SEGA_SUCCESS);
}

extern "C" __declspec(dllexport) OPEN_SEGASTATUS SEGAAPI_GetSynthParamMultiple(void* hHandle, unsigned int dwNumParams, OPEN_SynthParamSet* pSynthParams) {
    if (!hHandle || !pSynthParams || dwNumParams == 0) return SetStatus(OPEN_SEGAERR_INVALID_PARAM);
    for (unsigned int i = 0; i < dwNumParams; i++) {
        pSynthParams[i].lPARWValue = SEGAAPI_GetSynthParam(hHandle, pSynthParams[i].param);
    }
    return SetStatus(OPEN_SEGA_SUCCESS);
}

// ======================================================================
// SEGAAPI_SetReleaseState
// ======================================================================
extern "C" __declspec(dllexport) OPEN_SEGASTATUS SEGAAPI_SetReleaseState(void* hHandle, int bSet) {
    if (!hHandle) return SetStatus(OPEN_SEGAERR_BAD_HANDLE);
    auto* buffer = static_cast<OPEN_segaapiBuffer_t*>(hHandle);
    if (bSet) {
        buffer->playing = false;
        alSourceStop(buffer->alSource);
    }
    return SetStatus(OPEN_SEGA_SUCCESS);
}

// ======================================================================
// SEGAAPI_PlayWithSetup
// (Apply send routing, voice parameters, and synth parameters, then play)
// ======================================================================
extern "C" __declspec(dllexport) OPEN_SEGASTATUS SEGAAPI_PlayWithSetup(
    void* hHandle,
    unsigned int dwNumSendRouteParams, OPEN_SendRouteParamSet* pSendRouteParams,
    unsigned int dwNumSendLevelParams, OPEN_SendLevelParamSet* pSendLevelParams,
    unsigned int dwNumVoiceParams, OPEN_VoiceParamSet* pVoiceParams,
    unsigned int dwNumSynthParams, OPEN_SynthParamSet* pSynthParams
) {
    if (!hHandle) return SetStatus(OPEN_SEGAERR_BAD_HANDLE);
    if (pSendRouteParams && dwNumSendRouteParams > 0) {
        for (unsigned int i = 0; i < dwNumSendRouteParams; i++) {
            OPEN_SEGASTATUS status = SEGAAPI_SetSendRouting(hHandle, pSendRouteParams[i].dwChannel, pSendRouteParams[i].dwSend, pSendRouteParams[i].dwDest);
            if (status != OPEN_SEGA_SUCCESS) return SetStatus(status);
        }
    }
    if (pSendLevelParams && dwNumSendLevelParams > 0) {
        for (unsigned int i = 0; i < dwNumSendLevelParams; i++) {
            OPEN_SEGASTATUS status = SEGAAPI_SetSendLevel(hHandle, pSendLevelParams[i].dwChannel, pSendLevelParams[i].dwSend, pSendLevelParams[i].dwLevel);
            if (status != OPEN_SEGA_SUCCESS) return SetStatus(status);
        }
    }
    if (pVoiceParams && dwNumVoiceParams > 0) {
        for (unsigned int i = 0; i < dwNumVoiceParams; i++) {
            OPEN_SEGASTATUS status = OPEN_SEGA_SUCCESS;
            switch (pVoiceParams[i].VoiceIoctl) {
                case OPEN_VOICEIOCTL_SET_START_LOOP_OFFSET:
                    status = SEGAAPI_SetStartLoopOffset(hHandle, pVoiceParams[i].dwParam1);
                    break;
                case OPEN_VOICEIOCTL_SET_END_LOOP_OFFSET:
                    status = SEGAAPI_SetEndLoopOffset(hHandle, pVoiceParams[i].dwParam1);
                    break;
                case OPEN_VOICEIOCTL_SET_END_OFFSET:
                    status = SEGAAPI_SetEndOffset(hHandle, pVoiceParams[i].dwParam1);
                    break;
                case OPEN_VOICEIOCTL_SET_LOOP_STATE:
                    status = SEGAAPI_SetLoopState(hHandle, pVoiceParams[i].dwParam1);
                    break;
                default:
                    info("SEGAAPI_PlayWithSetup: Unimplemented VoiceIoctl %d", pVoiceParams[i].VoiceIoctl);
                    break;
            }
            if (status != OPEN_SEGA_SUCCESS) return SetStatus(status);
        }
    }
    if (pSynthParams && dwNumSynthParams > 0) {
        OPEN_SEGASTATUS status = SEGAAPI_SetSynthParamMultiple(hHandle, dwNumSynthParams, pSynthParams);
        if (status != OPEN_SEGA_SUCCESS) return SetStatus(status);
    }
    return SEGAAPI_Play(hHandle);
}

// ======================================================================
// Global EAX Property Functions (stubs for OpenAL)
// ======================================================================
extern "C" __declspec(dllexport) int SEGAAPI_SetGlobalEAXProperty(GUID* guid, unsigned long ulProperty, void* pData, unsigned long ulDataSize) {
    return TRUE;
}

extern "C" __declspec(dllexport) int SEGAAPI_GetGlobalEAXProperty(GUID* guid, unsigned long ulProperty, void* pData, unsigned long ulDataSize) {
    return TRUE;
}

// ======================================================================
// SPDIF Out functions (stubs for OpenAL)
// ======================================================================
extern "C" __declspec(dllexport) OPEN_SEGASTATUS SEGAAPI_SetSPDIFOutChannelStatus(unsigned int dwChannelStatus, unsigned int dwExtChannelStatus) {
    return SetStatus(OPEN_SEGA_SUCCESS);
}

extern "C" __declspec(dllexport) OPEN_SEGASTATUS SEGAAPI_GetSPDIFOutChannelStatus(unsigned int* pdwChannelStatus, unsigned int* pdwExtChannelStatus) {
    if (!pdwChannelStatus || !pdwExtChannelStatus) return SetStatus(OPEN_SEGAERR_BAD_POINTER);
    *pdwChannelStatus = 0;
    *pdwExtChannelStatus = 0;
    return SetStatus(OPEN_SEGA_SUCCESS);
}

extern "C" __declspec(dllexport) OPEN_SEGASTATUS SEGAAPI_SetSPDIFOutSampleRate(OPEN_HASPDIFOUTRATE dwSamplingRate) {
    return SetStatus(OPEN_SEGA_SUCCESS);
}

extern "C" __declspec(dllexport) OPEN_HASPDIFOUTRATE SEGAAPI_GetSPDIFOutSampleRate(void) {
    return OPEN_HASPDIFOUT_44_1KHZ;
}

extern "C" __declspec(dllexport) OPEN_SEGASTATUS SEGAAPI_SetSPDIFOutChannelRouting(unsigned int dwChannel, OPEN_HAROUTING dwSource) {
    return SetStatus(OPEN_SEGA_SUCCESS);
}

extern "C" __declspec(dllexport) OPEN_HAROUTING SEGAAPI_GetSPDIFOutChannelRouting(unsigned int dwChannel) {
    return OPEN_HA_UNUSED_PORT;
}

// ======================================================================
// IO Volume functions (mapped to listener gain)
// ======================================================================
extern "C" __declspec(dllexport) OPEN_SEGASTATUS SEGAAPI_SetIOVolume(OPEN_HAPHYSICALIO dwPhysIO, unsigned int dwVolume) {
    constexpr float MAX_VOLUME = static_cast<float>(0xFFFFFFFF);
    float normalized = std::clamp(dwVolume / MAX_VOLUME, 0.0f, 1.0f);
    alListenerf(AL_GAIN, normalized);
    return SetStatus(OPEN_SEGA_SUCCESS);
}

extern "C" __declspec(dllexport) unsigned int SEGAAPI_GetIOVolume(OPEN_HAPHYSICALIO dwPhysIO) {
    float gain = 1.0f;
    alGetListenerf(AL_GAIN, &gain);
    constexpr float MAX_VOLUME = static_cast<float>(0xFFFFFFFF);
    return static_cast<unsigned int>(gain * MAX_VOLUME);
}

// ======================================================================
// Set/Get Last Status
// ======================================================================
extern "C" __declspec(dllexport) void SEGAAPI_SetLastStatus(OPEN_SEGASTATUS LastStatus) {
    g_lastStatus = LastStatus;
}

extern "C" __declspec(dllexport) OPEN_SEGASTATUS SEGAAPI_GetLastStatus(void) {
    return g_lastStatus;
}

// ======================================================================
// SEGAAPI_Reset
// (Reset listener gain and flush any internal states.)
// ======================================================================
extern "C" __declspec(dllexport) OPEN_SEGASTATUS SEGAAPI_Reset(void) {
    alListenerf(AL_GAIN, 1.0f);
    return SetStatus(OPEN_SEGA_SUCCESS);
}

// End of opensegaapi.cpp
