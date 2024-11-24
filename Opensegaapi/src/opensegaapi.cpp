/*
* This file is part of the OpenParrot project - https://teknoparrot.com / https://github.com/teknogods
*
* See LICENSE and MENTIONS in the root of the source tree for information
* regarding licensing.
*/
extern "C" {
#include "opensegaapi.h"
}

#include <vector>
#define XAUDIO2_HELPER_FUNCTIONS
#include <xaudio2.h>
#include <wrl.h>
#define TSF_IMPLEMENTATION
#include "tsf.h"
#define CHECK_HR(exp) { HRESULT hr = exp; if (FAILED(hr)) { printf("failed %s: %08x\n", #exp, hr); abort(); } }
#pragma comment(lib, "xaudio2.lib")
static OPEN_SEGASTATUS g_lastStatus = OPEN_SEGA_SUCCESS;

static OPEN_SEGASTATUS SetStatus(OPEN_SEGASTATUS status) {
    g_lastStatus = status;
    return status;
}

namespace WRL = Microsoft::WRL;

const GUID OPEN_EAX_NULL_GUID;
const GUID OPEN_EAX_FREQUENCYSHIFTER_EFFECT;
const GUID OPEN_EAX_ECHO_EFFECT;
const GUID OPEN_EAX_REVERB_EFFECT;
const GUID OPEN_EAX_EQUALIZER_EFFECT;
const GUID OPEN_EAX_DISTORTION_EFFECT;
const GUID OPEN_EAX_AGCCOMPRESSOR_EFFECT;
const GUID OPEN_EAX_PITCHSHIFTER_EFFECT;
const GUID OPEN_EAX_FLANGER_EFFECT;
const GUID OPEN_EAX_VOCALMORPHER_EFFECT;
const GUID OPEN_EAX_AUTOWAH_EFFECT;
const GUID OPEN_EAX_RINGMODULATOR_EFFECT;
const GUID OPEN_EAX_CHORUS_EFFECT;

const GUID OPEN_EAXPROPERTYID_EAX40_FXSlot0;
const GUID OPEN_EAXPROPERTYID_EAX40_FXSlot1;
const GUID OPEN_EAXPROPERTYID_EAX40_FXSlot2;
const GUID OPEN_EAXPROPERTYID_EAX40_FXSlot3;

#include <concurrent_queue.h>
#include <functional>

struct OPEN_segaapiBuffer_t;

#ifdef _DEBUG
void info(const char* format, ...)
{
	va_list args;
	char buffer[1024];

	va_start(args, format);
	int len = _vsnprintf_s(buffer, sizeof(buffer), format, args);
	va_end(args);

	buffer[len] = '\n';
	buffer[len + 1] = '\0';

	OutputDebugStringA(buffer);
}
#else
#define info(x) {}
#endif

class XA2Callback : public IXAudio2VoiceCallback
{
public:
	void __stdcall OnVoiceProcessingPassStart(UINT32 BytesRequired) override
	{
	}

	void __stdcall OnVoiceProcessingPassEnd() override
	{
	}

	void __stdcall OnStreamEnd() override
	{
	}

	void __stdcall OnBufferStart(void*) override
	{
	}

	void __stdcall OnLoopEnd(void*) override
	{
	}

	void __stdcall OnVoiceError(void*, HRESULT) override
	{
	}

	void __stdcall OnBufferEnd(void* cxt) override;

public:
	OPEN_segaapiBuffer_t * buffer;
};

struct OPEN_segaapiBuffer_t
{
	unsigned int currentPosition;
	unsigned int flags;
	void* userData;
	OPEN_HAWOSEGABUFFERCALLBACK callback;
	bool synthesizer;
	bool loop;
	unsigned int channels;
	unsigned int startLoop;
	unsigned int endLoop;
	unsigned int endOffset;
	unsigned int sampleRate;
	unsigned int sampleFormat;
	uint8_t* data;
	size_t size;
	bool playing;
	bool paused;
	bool playWithSetup;

	WAVEFORMATEX xaFormat;

	XAUDIO2_BUFFER xaBuffer;
	IXAudio2SourceVoice* xaVoice;

	float sendVolumes[7];
	int sendChannels[7];
	OPEN_HAROUTING sendRoutes[7];
	float channelVolumes[6];

	tsf* synth;
	tsf_region* region;

	XA2Callback xaCallback;

	concurrency::concurrent_queue<std::function<void()>> defers;
};

void XA2Callback::OnBufferEnd(void* cxt)
{
    std::function<void()> entry;
    XAUDIO2_VOICE_STATE vs;
    
    buffer->xaVoice->GetState(&vs);
    if (vs.BuffersQueued == 0)
    {
        while (buffer->defers.try_pop(entry))
        {
            entry();
        }
    }
}

template <typename TFn>
void defer_buffer_call(OPEN_segaapiBuffer_t* buffer, const TFn& fn)
{
    if (!buffer->xaVoice)
    {
        fn();
        return;
    }

    XAUDIO2_VOICE_STATE vs;
    buffer->xaVoice->GetState(&vs);
    
    if (vs.BuffersQueued == 0)
    {
        fn();
    }
    else 
    {
        buffer->defers.push(fn);
    }
}


static void dumpWaveBuffer(const char* path, unsigned int channels, unsigned int sampleRate, unsigned int sampleBits, void* data, size_t size)
{
	info("dumpWaveBuffer path %s channels %d sampleRate %d sampleBits %d size %d", path, channels, sampleRate, sampleBits, size);

	struct RIFF_Header
	{
		char chunkID[4];
		long chunkSize;
		char format[4];
	};

	struct WAVE_Format
	{
		char subChunkID[4];
		long subChunkSize;
		short audioFormat;
		short numChannels;
		long sampleRate;
		long byteRate;
		short blockAlign;
		short bitsPerSample;
	};

	struct WAVE_Data
	{
		char subChunkID[4];
		long subChunk2Size;
	};

	FILE* soundFile = NULL;
	struct WAVE_Format wave_format;
	struct RIFF_Header riff_header;
	struct WAVE_Data wave_data;

	soundFile = fopen(path, "wb");

	riff_header.chunkID[0] = 'R';
	riff_header.chunkID[1] = 'I';
	riff_header.chunkID[2] = 'F';
	riff_header.chunkID[3] = 'F';
	riff_header.format[0] = 'W';
	riff_header.format[1] = 'A';
	riff_header.format[2] = 'V';
	riff_header.format[3] = 'E';

	fwrite(&riff_header, sizeof(struct RIFF_Header), 1, soundFile);

	wave_format.subChunkID[0] = 'f';
	wave_format.subChunkID[1] = 'm';
	wave_format.subChunkID[2] = 't';
	wave_format.subChunkID[3] = ' ';

	wave_format.audioFormat = 1;
	wave_format.sampleRate = sampleRate;
	wave_format.numChannels = channels;
	wave_format.bitsPerSample = sampleBits;
	wave_format.byteRate = (sampleRate * sampleBits * channels) / 8;
	wave_format.blockAlign = (sampleBits * channels) / 8;
	wave_format.subChunkSize = 16;

	fwrite(&wave_format, sizeof(struct WAVE_Format), 1, soundFile);

	wave_data.subChunkID[0] = 'd';
	wave_data.subChunkID[1] = 'a';
	wave_data.subChunkID[2] = 't';
	wave_data.subChunkID[3] = 'a';

	wave_data.subChunk2Size = size;

	fwrite(&wave_data, sizeof(struct WAVE_Data), 1, soundFile);
	fwrite(data, wave_data.subChunk2Size, 1, soundFile);

	fclose(soundFile);
}

static unsigned int bufferSampleSize(OPEN_segaapiBuffer_t* buffer)
{
	return buffer->channels * ((buffer->sampleFormat == OPEN_HASF_SIGNED_16PCM) ? 2 : 1);
}

static void updateSynthOnPlay(OPEN_segaapiBuffer_t* buffer, unsigned int offset, size_t length)
{
	// TODO
	//// synth
	//if (buffer->synthesizer)
	//{
	//	if (!buffer->synth->voices)
	//	{
	//		struct tsf_voice *voice = new tsf_voice;
	//		memset(voice, 0, sizeof(tsf_voice));

	//		auto region = buffer->region;

	//		TSF_BOOL doLoop; float filterQDB;
	//		voice->playingPreset = -1;

	//		voice->region = region;
	//		voice->noteGainDB = 0.0f - region->volume;

	//		tsf_voice_calcpitchratio(voice, 0, buffer->synth->outSampleRate);
	//		// The SFZ spec is silent about the pan curve, but a 3dB pan law seems common. This sqrt() curve matches what Dimension LE does; Alchemy Free seems closer to sin(adjustedPan * pi/2).
	//		voice->panFactorLeft = TSF_SQRTF(0.5f - region->pan);
	//		voice->panFactorRight = TSF_SQRTF(0.5f + region->pan);

	//		// Offset/end.
	//		voice->sourceSamplePosition = region->offset;

	//		// Loop.
	//		doLoop = (region->loop_mode != TSF_LOOPMODE_NONE && region->loop_start < region->loop_end);
	//		voice->loopStart = (doLoop ? region->loop_start : 0);
	//		voice->loopEnd = (doLoop ? region->loop_end : 0);

	//		// Setup envelopes.
	//		tsf_voice_envelope_setup(&voice->ampenv, &region->ampenv, 0, 0, TSF_TRUE, buffer->synth->outSampleRate);
	//		tsf_voice_envelope_setup(&voice->modenv, &region->modenv, 0, 0, TSF_FALSE, buffer->synth->outSampleRate);

	//		// Setup lowpass filter.
	//		filterQDB = region->initialFilterQ / 10.0f;
	//		voice->lowpass.QInv = 1.0 / TSF_POW(10.0, (filterQDB / 20.0));
	//		voice->lowpass.z1 = voice->lowpass.z2 = 0;
	//		voice->lowpass.active = (region->initialFilterFc </*=*/ 13500);
	//		if (voice->lowpass.active) tsf_voice_lowpass_setup(&voice->lowpass, tsf_cents2Hertz((float)region->initialFilterFc) / buffer->synth->outSampleRate);

	//		// Setup LFO filters.
	//		tsf_voice_lfo_setup(&voice->modlfo, region->delayModLFO, region->freqModLFO, buffer->synth->outSampleRate);
	//		tsf_voice_lfo_setup(&voice->viblfo, region->delayVibLFO, region->freqVibLFO, buffer->synth->outSampleRate);

	//		voice->pitchInputTimecents = (log(1.0) / log(2.0) * 1200);
	//		voice->pitchOutputFactor = 1.0f;

	//		buffer->synth->voices = voice;
	//	}

	//	buffer->synth->voices->region = buffer->region;

	//	// make input
	//	buffer->synth->outputmode = TSF_MONO;

	//	auto soffset = offset;
	//	auto slength = length;

	//	if (offset == -1)
	//	{
	//		soffset = 0;
	//	}

	//	if (length == -1)
	//	{
	//		slength = buffer->size;
	//	}

	//	std::vector<float> fontSamples(slength / bufferSampleSize(buffer));
	//	buffer->synth->fontSamples = &fontSamples[0];

	//	buffer->region->end = double(fontSamples.size());

	//	for (int i = 0; i < fontSamples.size(); i++)
	//	{
	//		if (buffer->sampleFormat == OPEN_HASF_UNSIGNED_8PCM)
	//		{
	//			fontSamples[i] = (buffer->data[soffset + i] / 128.0f) - 1.0f;
	//		}
	//		else if (buffer->sampleFormat == OPEN_HASF_SIGNED_16PCM)
	//		{
	//			fontSamples[i] = (*(int16_t*)&buffer->data[soffset + (i * 2)]) / 32768.0f;
	//		}
	//	}

	//	std::vector<float> outSamples(slength / bufferSampleSize(buffer));
	//	tsf_voice_render(buffer->synth, buffer->synth->voices, &outSamples[0], outSamples.size());

	//	for (int i = 0; i < outSamples.size(); i++)
	//	{
	//		if (buffer->sampleFormat == OPEN_HASF_UNSIGNED_8PCM)
	//		{
	//			buffer->data[soffset + i] = (uint8_t)((outSamples[i] + 1.0f) * 128.0f);
	//		}
	//		else if (buffer->sampleFormat == OPEN_HASF_SIGNED_16PCM)
	//		{
	//			*(int16_t*)&buffer->data[soffset + (i * 2)] = outSamples[i] * 32768.0f;
	//		}
	//	}
	//}
}

static void resetBuffer(OPEN_segaapiBuffer_t* buffer)
{
	buffer->startLoop = 0;
	buffer->endOffset = buffer->size;
	buffer->endLoop = buffer->size;
	buffer->loop = false;
	buffer->paused = false;
	buffer->playWithSetup = false;
	buffer->sendRoutes[0] = OPEN_HA_FRONT_LEFT_PORT;
	buffer->sendRoutes[1] = OPEN_HA_FRONT_RIGHT_PORT;
	buffer->sendRoutes[2] = OPEN_HA_UNUSED_PORT;
	buffer->sendRoutes[3] = OPEN_HA_UNUSED_PORT;
	buffer->sendRoutes[4] = OPEN_HA_UNUSED_PORT;
	buffer->sendRoutes[5] = OPEN_HA_UNUSED_PORT;
	buffer->sendRoutes[6] = OPEN_HA_UNUSED_PORT;
	buffer->sendVolumes[0] = 0.0f;
	buffer->sendVolumes[1] = 0.0f;
	buffer->sendVolumes[2] = 0.0f;
	buffer->sendVolumes[3] = 0.0f;
	buffer->sendVolumes[4] = 0.0f;
	buffer->sendVolumes[5] = 0.0f;
	buffer->sendVolumes[6] = 0.0f;
	buffer->channelVolumes[0] = 1.0f;
	buffer->channelVolumes[1] = 1.0f;
	buffer->channelVolumes[2] = 1.0f;
	buffer->channelVolumes[3] = 1.0f;
	buffer->channelVolumes[4] = 1.0f;
	buffer->channelVolumes[5] = 1.0f;
	buffer->sendChannels[0] = 0;
	buffer->sendChannels[1] = 1;
	buffer->sendChannels[2] = 0;
	buffer->sendChannels[3] = 0;
	buffer->sendChannels[4] = 0;
	buffer->sendChannels[5] = 0;
	buffer->sendChannels[6] = 0;

	auto res = (tsf*)TSF_MALLOC(sizeof(tsf));
	TSF_MEMSET(res, 0, sizeof(tsf));
	res->presetNum = 0;
	res->outSampleRate = buffer->sampleRate;

	buffer->synth = res;

	tsf_region* region = new tsf_region;
	memset(region, 0, sizeof(tsf_region));

	tsf_region_clear(region, 0);

	region->ampenv.delay = 0;
	region->ampenv.hold = 300.0f;
	region->ampenv.attack = 0;
	region->ampenv.decay = 0;
	region->ampenv.release = 0;
	region->ampenv.sustain = 0;

	buffer->region = region;
}

static WRL::ComPtr<IXAudio2> g_xa2;
static IXAudio2MasteringVoice* g_masteringVoice; 
static IXAudio2SubmixVoice* g_submixVoices[6];

struct ChannelConfig {
    OPEN_HAROUTING port;
    float frontLeft;
    float frontRight;
    float frontCenter;
    float lfe;
    float rearLeft;
    float rearRight;
};

static void updateBufferNew(OPEN_segaapiBuffer_t* buffer, unsigned int offset, size_t length)
{
    // don't update with pending defers
    if (!buffer->defers.empty())
    {
        info("updateBufferNew: DEFER!");
        return;
    }

	CHECK_HR(buffer->xaVoice->FlushSourceBuffers());

	buffer->xaBuffer.Flags = 0;
	buffer->xaBuffer.AudioBytes = buffer->size;
	buffer->xaBuffer.pAudioData = buffer->data;

	if (buffer->loop)
	{
		info("updateBufferNew: loop");

		// Note: Sega uses byte offsets for begin and end
		//       Xaudio2 uses start sample and length in samples
		buffer->xaBuffer.PlayBegin = buffer->startLoop / bufferSampleSize(buffer);
		buffer->xaBuffer.PlayLength = (min(buffer->endLoop, buffer->endOffset) - buffer->startLoop) / bufferSampleSize(buffer);
		buffer->xaBuffer.LoopBegin = buffer->xaBuffer.PlayBegin;
		buffer->xaBuffer.LoopLength = buffer->xaBuffer.PlayLength;
		buffer->xaBuffer.LoopCount = XAUDIO2_LOOP_INFINITE;
		buffer->xaBuffer.pContext = NULL;
	}
	else
	{
		info("updateBufferNew: no loop");
		buffer->xaBuffer.PlayBegin = buffer->startLoop / bufferSampleSize(buffer);
		buffer->xaBuffer.PlayLength = (min(buffer->endLoop, buffer->endOffset) - buffer->startLoop) / bufferSampleSize(buffer);
		buffer->xaBuffer.LoopBegin = 0;
		buffer->xaBuffer.LoopLength = 0;
		buffer->xaBuffer.LoopCount = 0;
		buffer->xaBuffer.pContext = NULL;
	}

	buffer->xaVoice->SubmitSourceBuffer(&buffer->xaBuffer);

	// Uncomment to dump audio buffers to wav files (super slow)
	/*auto sampleBits = (buffer->sampleFormat == OPEN_HASF_SIGNED_16PCM) ? 16 : 8;
	char path[255];
	sprintf(path, "C:\\dump\\%08X.wav", &buffer);
	dumpWaveBuffer(path, buffer->channels, buffer->sampleRate, sampleBits, buffer->data, buffer->size);*/
}

extern "C" {
__declspec(dllexport) OPEN_SEGASTATUS SEGAAPI_CreateBuffer(OPEN_HAWOSEBUFFERCONFIG* pConfig, OPEN_HAWOSEGABUFFERCALLBACK pCallback, unsigned int dwFlags, void** phHandle)
{
    if (phHandle == NULL || pConfig == NULL)
    {
        info("SEGAAPI_CreateBuffer: Handle: %08X, Status: OPEN_SEGAERR_BAD_POINTER", phHandle);
        return SetStatus(OPEN_SEGAERR_BAD_POINTER);
    }

    try {
        std::unique_ptr<OPEN_segaapiBuffer_t> buffer = std::make_unique<OPEN_segaapiBuffer_t>();

        info("SEGAAPI_CreateBuffer: hHandle: %08X synth: %d, mem caller: %d, mem last: %d, mem alloc: %d, size: %d SampleRate: %d, byNumChans: %d, dwPriority: %d, dwSampleFormat: %d", 
            buffer.get(), (dwFlags & OPEN_HABUF_SYNTH_BUFFER), (dwFlags & OPEN_HABUF_ALLOC_USER_MEM) >> 1, 
            (dwFlags & OPEN_HABUF_USE_MAPPED_MEM) >> 2, dwFlags == 0, pConfig->mapData.dwSize, 
            pConfig->dwSampleRate, pConfig->byNumChans, pConfig->dwPriority, pConfig->dwSampleFormat);

        // Initialize buffer properties
        buffer->playing = false;
        buffer->callback = pCallback;
        buffer->synthesizer = dwFlags & OPEN_HABUF_SYNTH_BUFFER;
        buffer->sampleFormat = pConfig->dwSampleFormat;
        buffer->sampleRate = pConfig->dwSampleRate;
        buffer->channels = pConfig->byNumChans;
        buffer->userData = pConfig->hUserData;
        buffer->size = pConfig->mapData.dwSize;
        pConfig->mapData.dwOffset = 0;

        // Allocate buffer data
        if (dwFlags & OPEN_HABUF_ALLOC_USER_MEM || dwFlags & OPEN_HABUF_USE_MAPPED_MEM)
        {
            buffer->data = static_cast<uint8_t*>(pConfig->mapData.hBufferHdr);
        }
        else
        {
            buffer->data = static_cast<uint8_t*>(malloc(buffer->size));
            if (!buffer->data) {
                return SetStatus(OPEN_SEGAERR_OUT_OF_MEMORY);
            }
        }

        pConfig->mapData.hBufferHdr = buffer->data;

        // Setup XAudio2 format
        const uint32_t sampleBits = (pConfig->dwSampleFormat == OPEN_HASF_SIGNED_16PCM) ? 16 : 8;
        buffer->xaFormat = {
            .cbSize = sizeof(WAVEFORMATEX),
            .nAvgBytesPerSec = (pConfig->dwSampleRate * sampleBits * pConfig->byNumChans) / 8,
            .nSamplesPerSec = pConfig->dwSampleRate,
            .wBitsPerSample = static_cast<WORD>(sampleBits),
            .nChannels = pConfig->byNumChans,
            .wFormatTag = 1,
            .nBlockAlign = static_cast<WORD>((sampleBits * pConfig->byNumChans) / 8)
        };
        
        buffer->xaCallback.buffer = buffer.get();
        HRESULT hr = g_xa2->CreateSourceVoice(&buffer->xaVoice, &buffer->xaFormat, 0, 2.0f, &buffer->xaCallback);
        if (FAILED(hr)) {
            return SetStatus(OPEN_SEGAERR_UNKNOWN);
        }

        buffer->xaBuffer = { 0 };
        resetBuffer(buffer.get());

        *phHandle = buffer.release();
        return SetStatus(OPEN_SEGA_SUCCESS);
    }
    catch (...) {
        return SetStatus(OPEN_SEGAERR_UNKNOWN);
    }
}

__declspec(dllexport) OPEN_SEGASTATUS SEGAAPI_SetUserData(void* hHandle, void* hUserData)
{
    if (hHandle == NULL)
    {
        info("SEGAAPI_SetUserData: Handle: %08X, Status: OPEN_SEGAERR_BAD_HANDLE", hHandle);
        return SetStatus(OPEN_SEGAERR_BAD_HANDLE);
    }

    info("SEGAAPI_SetUserData: Handle: %08X UserData: %08X", hHandle, hUserData);

    try {
        OPEN_segaapiBuffer_t* buffer = static_cast<OPEN_segaapiBuffer_t*>(hHandle);
        buffer->userData = hUserData;
        return SetStatus(OPEN_SEGA_SUCCESS);
    }
    catch (...) {
        return SetStatus(OPEN_SEGAERR_UNKNOWN);
    }
}

__declspec(dllexport) void* SEGAAPI_GetUserData(void* hHandle)
{
    if (hHandle == NULL)
    {
        SetStatus(OPEN_SEGAERR_BAD_HANDLE);
        info("SEGAAPI_GetUserData: Handle: %08X, Status: OPEN_SEGAERR_BAD_HANDLE", hHandle);
        return nullptr;
    }

    info("SEGAAPI_GetUserData: Handle: %08X", hHandle);

    try {
        OPEN_segaapiBuffer_t* buffer = static_cast<OPEN_segaapiBuffer_t*>(hHandle);
        SetStatus(OPEN_SEGA_SUCCESS);
        return buffer->userData;
    }
    catch (...) {
        SetStatus(OPEN_SEGAERR_UNKNOWN);
        return nullptr;
    }
}

__declspec(dllexport) OPEN_SEGASTATUS SEGAAPI_UpdateBuffer(void* hHandle, unsigned int dwStartOffset, unsigned int dwLength)
{
    if (hHandle == NULL)
    {
        info("SEGAAPI_UpdateBuffer: Handle: %08X, Status: OPEN_SEGAERR_BAD_HANDLE", hHandle);
        return SetStatus(OPEN_SEGAERR_BAD_HANDLE);
    }

    info("SEGAAPI_UpdateBuffer: Handle: %08X dwStartOffset: %08X, dwLength: %08X", hHandle, dwStartOffset, dwLength);

    try {
        OPEN_segaapiBuffer_t* buffer = static_cast<OPEN_segaapiBuffer_t*>(hHandle);
        
        // Validate offset and length
        if (dwStartOffset + dwLength > buffer->size) {
            return SetStatus(OPEN_SEGAERR_BAD_PARAM);
        }

        updateBufferNew(buffer, dwStartOffset, dwLength);
        return SetStatus(OPEN_SEGA_SUCCESS);
    }
    catch (...) {
        return SetStatus(OPEN_SEGAERR_UNKNOWN);
    }
}

__declspec(dllexport) OPEN_SEGASTATUS SEGAAPI_SetEndOffset(void* hHandle, unsigned int dwOffset)
{
    if (hHandle == NULL)
    {
        info("SEGAAPI_SetEndOffset: Handle: %08X, Status: OPEN_SEGAERR_BAD_HANDLE", hHandle);
        return SetStatus(OPEN_SEGAERR_BAD_HANDLE);
    }

    info("SEGAAPI_SetEndOffset: Handle: %08X dwOffset: %08X", hHandle, dwOffset);

    try {
        OPEN_segaapiBuffer_t* buffer = static_cast<OPEN_segaapiBuffer_t*>(hHandle);
        
        // Validate offset is within buffer bounds
        if (dwOffset > buffer->size) {
            return SetStatus(OPEN_SEGAERR_BAD_PARAM);
        }

        buffer->endOffset = dwOffset;
        return SetStatus(OPEN_SEGA_SUCCESS);
    }
    catch (...) {
        return SetStatus(OPEN_SEGAERR_UNKNOWN);
    }
}

__declspec(dllexport) OPEN_SEGASTATUS SEGAAPI_SetEndLoopOffset(void* hHandle, unsigned int dwOffset)
{
    if (hHandle == NULL)
    {
        info("SEGAAPI_SetEndLoopOffset: Handle: %08X, Status: OPEN_SEGAERR_BAD_HANDLE", hHandle);
        return SetStatus(OPEN_SEGAERR_BAD_HANDLE);
    }

    info("SEGAAPI_SetEndLoopOffset: Handle: %08X dwOffset: %08X", hHandle, dwOffset);

    try {
        OPEN_segaapiBuffer_t* buffer = static_cast<OPEN_segaapiBuffer_t*>(hHandle);
        
        // Validate offset is within buffer bounds
        if (dwOffset > buffer->size) {
            return SetStatus(OPEN_SEGAERR_BAD_PARAM);
        }

        buffer->endLoop = dwOffset;
        return SetStatus(OPEN_SEGA_SUCCESS);
    }
    catch (...) {
        return SetStatus(OPEN_SEGAERR_UNKNOWN);
    }
}

__declspec(dllexport) OPEN_SEGASTATUS SEGAAPI_SetStartLoopOffset(void* hHandle, unsigned int dwOffset)
{
    if (hHandle == NULL)
    {
        info("SEGAAPI_SetStartLoopOffset: Handle: %08X, Status: OPEN_SEGAERR_BAD_HANDLE", hHandle);
        return SetStatus(OPEN_SEGAERR_BAD_HANDLE);
    }

    info("SEGAAPI_SetStartLoopOffset: Handle: %08X dwOffset: %08X", hHandle, dwOffset);

    try {
        OPEN_segaapiBuffer_t* buffer = static_cast<OPEN_segaapiBuffer_t*>(hHandle);
        
        // Validate offset is within buffer bounds
        if (dwOffset > buffer->size) {
            return SetStatus(OPEN_SEGAERR_BAD_PARAM);
        }

        buffer->startLoop = dwOffset;
        return SetStatus(OPEN_SEGA_SUCCESS);
    }
    catch (...) {
        return SetStatus(OPEN_SEGAERR_UNKNOWN);
    }
}

__declspec(dllexport) OPEN_SEGASTATUS SEGAAPI_SetSampleRate(void* hHandle, unsigned int dwSampleRate)
{
    if (hHandle == NULL)
    {
        info("SEGAAPI_SetSampleRate: Handle: %08X, Status: OPEN_SEGAERR_BAD_HANDLE", hHandle);
        return SetStatus(OPEN_SEGAERR_BAD_HANDLE);
    }

    info("SEGAAPI_SetSampleRate: Handle: %08X dwSampleRate: %08X", hHandle, dwSampleRate);

    try {
        OPEN_segaapiBuffer_t* buffer = static_cast<OPEN_segaapiBuffer_t*>(hHandle);
        
        // Validate sample rate (typical range check)
        if (dwSampleRate < 8000 || dwSampleRate > 192000) {
            return SetStatus(OPEN_SEGAERR_BAD_PARAM);
        }

        buffer->sampleRate = dwSampleRate;

        defer_buffer_call(buffer, [=]()
        {
            HRESULT hr = buffer->xaVoice->SetSourceSampleRate(dwSampleRate);
            if (FAILED(hr)) {
                throw std::runtime_error("Failed to set sample rate");
            }
        });

        return SetStatus(OPEN_SEGA_SUCCESS);
    }
    catch (...) {
        return SetStatus(OPEN_SEGAERR_UNKNOWN);
    }
}

__declspec(dllexport) OPEN_SEGASTATUS SEGAAPI_SetLoopState(void* hHandle, int bDoContinuousLooping)
{
    if (hHandle == NULL)
    {
        info("SEGAAPI_SetLoopState: Handle: %08X, Status: OPEN_SEGAERR_BAD_HANDLE", hHandle);
        return SetStatus(OPEN_SEGAERR_BAD_HANDLE);
    }

    info("SEGAAPI_SetLoopState: Handle: %08X bDoContinuousLooping: %d", hHandle, bDoContinuousLooping);

    try {
        OPEN_segaapiBuffer_t* buffer = static_cast<OPEN_segaapiBuffer_t*>(hHandle);
        buffer->loop = bDoContinuousLooping != 0;
        return SetStatus(OPEN_SEGA_SUCCESS);
    }
    catch (...) {
        return SetStatus(OPEN_SEGAERR_UNKNOWN);
    }
}

__declspec(dllexport) OPEN_SEGASTATUS SEGAAPI_SetPlaybackPosition(void* hHandle, unsigned int dwPlaybackPos)
{
    if (hHandle == NULL)
    {
        info("SEGAAPI_SetPlaybackPosition: Handle: %08X, Status: OPEN_SEGAERR_BAD_HANDLE", hHandle);
        return SetStatus(OPEN_SEGAERR_BAD_HANDLE);
    }

    info("SEGAAPI_SetPlaybackPosition: Handle: %08X dwPlaybackPos: %08X", hHandle, dwPlaybackPos);

    try {
        OPEN_segaapiBuffer_t* buffer = static_cast<OPEN_segaapiBuffer_t*>(hHandle);
        
        if (dwPlaybackPos > buffer->size) {
            return SetStatus(OPEN_SEGAERR_BAD_PARAM);
        }

        if (dwPlaybackPos != 0) {
            buffer->xaVoice->Stop();
            buffer->xaVoice->FlushSourceBuffers();
            buffer->currentPosition = dwPlaybackPos;
            
            if (buffer->playing) {
                buffer->xaVoice->Start();
            }
        }

        return SetStatus(OPEN_SEGA_SUCCESS);
    }
    catch (...) {
        return SetStatus(OPEN_SEGAERR_UNKNOWN);
    }
}

__declspec(dllexport) unsigned int SEGAAPI_GetPlaybackPosition(void* hHandle)
{
    if (hHandle == NULL)
    {
        SetStatus(OPEN_SEGAERR_BAD_HANDLE);
        return 0;
    }

    try {
        OPEN_segaapiBuffer_t* buffer = static_cast<OPEN_segaapiBuffer_t*>(hHandle);

        XAUDIO2_VOICE_STATE vs;
        buffer->xaVoice->GetState(&vs);

        unsigned int result = (vs.SamplesPlayed * (buffer->xaFormat.wBitsPerSample / 8) * buffer->xaFormat.nChannels) % buffer->size;
        
        info("SEGAAPI_GetPlaybackPosition: Handle: %08X Samples played: %08d BitsPerSample %08d/%08d nChannels %08d bufferSize %08d Result: %08X", 
            hHandle, vs.SamplesPlayed, buffer->xaFormat.wBitsPerSample, (buffer->xaFormat.wBitsPerSample / 8), 
            buffer->xaFormat.nChannels, buffer->size, result);

        SetStatus(OPEN_SEGA_SUCCESS);
        return result;
    }
    catch (...) {
        SetStatus(OPEN_SEGAERR_UNKNOWN);
        return 0;
    }
}

	static void updateRouting(OPEN_segaapiBuffer_t* buffer);

__declspec(dllexport) OPEN_SEGASTATUS SEGAAPI_Play(void* hHandle)
{
    if (hHandle == NULL)
    {
        info("SEGAAPI_Play: Handle: %08X, Status: OPEN_SEGAERR_BAD_HANDLE", hHandle);
        return SetStatus(OPEN_SEGAERR_BAD_HANDLE);
    }

    info("SEGAAPI_Play: Handle: %08X", hHandle);

    try {
        OPEN_segaapiBuffer_t* buffer = static_cast<OPEN_segaapiBuffer_t*>(hHandle);

        updateRouting(buffer);
        updateBufferNew(buffer, -1, -1);

        buffer->playing = true;
        buffer->paused = false;

        HRESULT hr = buffer->xaVoice->Start();
        if (FAILED(hr)) {
            return SetStatus(OPEN_SEGAERR_UNKNOWN);
        }

        return SetStatus(OPEN_SEGA_SUCCESS);
    }
    catch (...) {
        return SetStatus(OPEN_SEGAERR_UNKNOWN);
    }
}

__declspec(dllexport) OPEN_SEGASTATUS SEGAAPI_Stop(void* hHandle)
{
    if (hHandle == NULL)
    {
        info("SEGAAPI_Stop: Handle: %08X, Status: OPEN_SEGAERR_BAD_HANDLE", hHandle);
        return SetStatus(OPEN_SEGAERR_BAD_HANDLE);
    }

    info("SEGAAPI_Stop: Handle: %08X", hHandle);

    try {
        OPEN_segaapiBuffer_t* buffer = static_cast<OPEN_segaapiBuffer_t*>(hHandle);
        
        HRESULT hr = buffer->xaVoice->Stop();
        if (FAILED(hr)) {
            return SetStatus(OPEN_SEGAERR_UNKNOWN);
        }

        hr = buffer->xaVoice->FlushSourceBuffers();
        if (FAILED(hr)) {
            return SetStatus(OPEN_SEGAERR_UNKNOWN);
        }

        buffer->playing = false;
        buffer->paused = false;

        return SetStatus(OPEN_SEGA_SUCCESS);
    }
    catch (...) {
        return SetStatus(OPEN_SEGAERR_UNKNOWN);
    }
}

__declspec(dllexport) OPEN_HAWOSTATUS SEGAAPI_GetPlaybackStatus(void* hHandle)
{
    if (hHandle == NULL)
    {
        info("SEGAAPI_GetPlaybackStatus: Handle: %08X, Status: OPEN_HAWOSTATUS_INVALID", hHandle);
        SetStatus(OPEN_SEGAERR_BAD_HANDLE);
        return OPEN_HAWOSTATUS_INVALID;
    }

    try {
        OPEN_segaapiBuffer_t* buffer = static_cast<OPEN_segaapiBuffer_t*>(hHandle);

        if (buffer->paused)
        {
            info("SEGAAPI_GetPlaybackStatus: Handle: %08X, Status: OPEN_HAWOSTATUS_PAUSE, buffer is paused", hHandle);
            SetStatus(OPEN_SEGA_SUCCESS);
            return OPEN_HAWOSTATUS_PAUSE;
        }

        XAUDIO2_VOICE_STATE vs;
        buffer->xaVoice->GetState(&vs);

        if (vs.BuffersQueued == 0)
        {
            info("SEGAAPI_GetPlaybackStatus: Handle: %08X, Status: OPEN_HAWOSTATUS_STOP, buffersqueued is 0", hHandle);
            SetStatus(OPEN_SEGA_SUCCESS);
            return OPEN_HAWOSTATUS_STOP;
        }

        const uint32_t sampleSize = bufferSampleSize(buffer);
        const uint32_t endSamples = min(buffer->size, buffer->endOffset) / sampleSize;

        if (!buffer->loop && vs.SamplesPlayed >= endSamples)
        {
            info("SEGAAPI_GetPlaybackStatus: Handle: %08X, Status: OPEN_HAWOSTATUS_STOP, Loop false and samples played bigger", hHandle);
            SetStatus(OPEN_SEGA_SUCCESS);
            return OPEN_HAWOSTATUS_STOP;
        }

        if (buffer->playing)
        {
            info("SEGAAPI_GetPlaybackStatus: Handle: %08X, Status: OPEN_HAWOSTATUS_ACTIVE, playing true!", hHandle);
            SetStatus(OPEN_SEGA_SUCCESS);
            return OPEN_HAWOSTATUS_ACTIVE;
        }
        else
        {
            info("SEGAAPI_GetPlaybackStatus: Handle: %08X, Status: OPEN_HAWOSTATUS_STOP, Playing false!", hHandle);
            SetStatus(OPEN_SEGA_SUCCESS);
            return OPEN_HAWOSTATUS_STOP;
        }
    }
    catch (...) {
        SetStatus(OPEN_SEGAERR_UNKNOWN);
        return OPEN_HAWOSTATUS_INVALID;
    }
}

__declspec(dllexport) OPEN_SEGASTATUS SEGAAPI_SetReleaseState(void* hHandle, int bSet)
{
    if (hHandle == NULL)
    {
        info("SEGAAPI_SetReleaseState: Handle: %08X, Status: OPEN_SEGAERR_BAD_HANDLE", hHandle);
        return SetStatus(OPEN_SEGAERR_BAD_HANDLE);
    }

    info("SEGAAPI_SetReleaseState: Handle: %08X bSet: %08X", hHandle, bSet);

    try {
        OPEN_segaapiBuffer_t* buffer = static_cast<OPEN_segaapiBuffer_t*>(hHandle);

        if (bSet)
        {
            buffer->playing = false;
            HRESULT hr = buffer->xaVoice->FlushSourceBuffers();
            if (FAILED(hr)) {
                return SetStatus(OPEN_SEGAERR_UNKNOWN);
            }

            hr = buffer->xaVoice->Stop();
            if (FAILED(hr)) {
                return SetStatus(OPEN_SEGAERR_UNKNOWN);
            }
        }

        return SetStatus(OPEN_SEGA_SUCCESS);
    }
    catch (...) {
        return SetStatus(OPEN_SEGAERR_UNKNOWN);
    }
}

__declspec(dllexport) OPEN_SEGASTATUS SEGAAPI_DestroyBuffer(void* hHandle)
{
    if (hHandle == NULL)
    {
        info("SEGAAPI_DestroyBuffer: Handle: %08X, Status: OPEN_SEGAERR_BAD_HANDLE", hHandle);
        return SetStatus(OPEN_SEGAERR_BAD_HANDLE);
    }

    info("SEGAAPI_DestroyBuffer: Handle: %08X", hHandle);

    try {
        OPEN_segaapiBuffer_t* buffer = static_cast<OPEN_segaapiBuffer_t*>(hHandle);

        // Stop and flush before destroying
        buffer->xaVoice->Stop();
        buffer->xaVoice->FlushSourceBuffers();
        buffer->xaVoice->DestroyVoice();

        // Free allocated memory if it wasn't user-provided
        if (!(buffer->flags & (OPEN_HABUF_ALLOC_USER_MEM | OPEN_HABUF_USE_MAPPED_MEM))) {
            free(buffer->data);
        }

        delete buffer;
        return SetStatus(OPEN_SEGA_SUCCESS);
    }
    catch (...) {
        return SetStatus(OPEN_SEGAERR_UNKNOWN);
    }
}


__declspec(dllexport) int SEGAAPI_SetGlobalEAXProperty(GUID* guid, unsigned long ulProperty, void* pData, unsigned long ulDataSize)
{
    info("SEGAAPI_SetGlobalEAXProperty: Property: %lu, DataSize: %lu", ulProperty, ulDataSize);

    try {
        // Store the successful status
        SetStatus(OPEN_SEGA_SUCCESS);
        return TRUE;
    }
    catch (...) {
        SetStatus(OPEN_SEGAERR_UNKNOWN);
        return FALSE;
    }
}


__declspec(dllexport) OPEN_SEGASTATUS SEGAAPI_Init(void)
{
    info("SEGAAPI_Init");

    try {
        HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (FAILED(hr)) {
            return SetStatus(OPEN_SEGAERR_UNKNOWN);
        }

        hr = XAudio2Create(&g_xa2, 0, XAUDIO2_DEFAULT_PROCESSOR);
        if (FAILED(hr)) {
            return SetStatus(OPEN_SEGAERR_UNKNOWN);
        }

        constexpr XAUDIO2_DEBUG_CONFIGURATION cfg = { 
            .TraceMask = XAUDIO2_LOG_ERRORS | XAUDIO2_LOG_WARNINGS,
            .BreakMask = 0,
            .LogThreadID = TRUE,
            .LogTiming = TRUE,
            .LogFunctionName = TRUE
        };
        g_xa2->SetDebugConfiguration(&cfg);

        hr = g_xa2->CreateMasteringVoice(&g_masteringVoice, XAUDIO2_DEFAULT_CHANNELS, 
                                        XAUDIO2_DEFAULT_SAMPLERATE, 0, nullptr, nullptr, 
                                        AudioCategory_GameEffects);
        if (FAILED(hr)) {
            return SetStatus(OPEN_SEGAERR_UNKNOWN);
        }

        XAUDIO2_VOICE_DETAILS vd;
        g_masteringVoice->GetVoiceDetails(&vd);

        for (auto& submixVoice : g_submixVoices)
        {
            hr = g_xa2->CreateSubmixVoice(&submixVoice, 1, vd.InputSampleRate, 
                                         XAUDIO2_VOICE_USEFILTER, 0, nullptr, nullptr);
            if (FAILED(hr)) {
                return SetStatus(OPEN_SEGAERR_UNKNOWN);
            }
        }

        static constexpr std::array<ChannelConfig, 6> configs = {{
            {OPEN_HA_FRONT_LEFT_PORT,   1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f},
            {OPEN_HA_FRONT_RIGHT_PORT,  0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f},
            {OPEN_HA_FRONT_CENTER_PORT, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f},
            {OPEN_HA_REAR_LEFT_PORT,    0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f},
            {OPEN_HA_REAR_RIGHT_PORT,   0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f},
            {OPEN_HA_LFE_PORT,          0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f}
        }};

        const UINT32 numChannels = vd.InputChannels;
        for (const auto& config : configs)
        {
            float levelMatrix[12] = {};
            levelMatrix[0] = config.frontLeft + config.rearLeft;
            levelMatrix[1] = config.frontRight + config.rearRight;
            levelMatrix[2] = config.lfe;

            hr = g_submixVoices[config.port]->SetOutputMatrix(
                g_masteringVoice, 1, numChannels, levelMatrix);
            if (FAILED(hr)) {
                return SetStatus(OPEN_SEGAERR_UNKNOWN);
            }
        }

        return SetStatus(OPEN_SEGA_SUCCESS);
    }
    catch (...) {
        return SetStatus(OPEN_SEGAERR_UNKNOWN);
    }
}

__declspec(dllexport) OPEN_SEGASTATUS SEGAAPI_Exit(void)
{
    info("SEGAAPI_Exit");

    try {
        for (auto& g_submixVoice : g_submixVoices)
        {
            if (g_submixVoice) {
                g_submixVoice->DestroyVoice();
                g_submixVoice = nullptr;
            }
        }

        if (g_masteringVoice) {
            g_masteringVoice->DestroyVoice();
            g_masteringVoice = nullptr;
        }

        if (g_xa2) {
            g_xa2->StopEngine();
            g_xa2.Reset();
        }

        CoUninitialize();
        
        return SetStatus(OPEN_SEGA_SUCCESS);
    }
    catch (...) {
        return SetStatus(OPEN_SEGAERR_UNKNOWN);
    }
}

__declspec(dllexport) OPEN_SEGASTATUS SEGAAPI_Reset(void)
{
    info("SEGAAPI_Reset");
    
    try {
        for (auto& g_submixVoice : g_submixVoices)
        {
            if (g_submixVoice) {
                g_submixVoice->FlushSourceBuffers();
                g_submixVoice->SetVolume(1.0f);
            }
        }

        if (g_masteringVoice) {
            g_masteringVoice->SetVolume(1.0f);
        }

        if (g_xa2) {
            HRESULT hr = g_xa2->CommitChanges(XAUDIO2_COMMIT_NOW);
            if (FAILED(hr)) {
                return SetStatus(OPEN_SEGAERR_UNKNOWN);
            }
        }

        return SetStatus(OPEN_SEGA_SUCCESS);
    }
    catch (...) {
        return SetStatus(OPEN_SEGAERR_UNKNOWN);
    }
}

__declspec(dllexport) OPEN_SEGASTATUS SEGAAPI_SetIOVolume(OPEN_HAPHYSICALIO dwPhysIO, unsigned int dwVolume)
{
    info("SEGAAPI_SetIOVolume: dwPhysIO: %08X dwVolume: %08X", dwPhysIO, dwVolume);
    
    if (!g_masteringVoice) {
        return SetStatus(OPEN_SEGAERR_UNKNOWN);
    }

    try {
        constexpr float MAX_VOLUME = static_cast<float>(0xFFFFFFFF);
        const float normalizedVolume = std::clamp(dwVolume / MAX_VOLUME, 0.0f, 1.0f);
        
        HRESULT hr = g_masteringVoice->SetVolume(normalizedVolume);
        if (FAILED(hr)) {
            return SetStatus(OPEN_SEGAERR_UNKNOWN);
        }
        
        return SetStatus(OPEN_SEGA_SUCCESS);
    }
    catch (...) {
        return SetStatus(OPEN_SEGAERR_UNKNOWN);
    }
}

static void updateRouting(OPEN_segaapiBuffer_t* buffer)
{
    if (!buffer || !buffer->xaVoice) {
        return;
    }

    try {
        constexpr size_t MAX_ROUTES = 7;
        constexpr size_t MAX_CHANNELS = 2;
        
        float levels[MAX_ROUTES * MAX_CHANNELS] = {};
        IXAudio2SubmixVoice* outVoices[MAX_ROUTES] = {};
        int numRoutes = 0;

        for (int i = 0; i < MAX_ROUTES; i++)
        {
            if (buffer->sendRoutes[i] != OPEN_HA_UNUSED_PORT && 
                buffer->sendRoutes[i] < g_submixVoices.size() &&
                g_submixVoices[buffer->sendRoutes[i]])
            {
                outVoices[numRoutes] = g_submixVoices[buffer->sendRoutes[i]];
                const int levelOff = numRoutes * buffer->channels;
                const int sendChannel = std::min(buffer->sendChannels[i], buffer->channels - 1);
                const float level = std::clamp(
                    buffer->sendVolumes[i] * buffer->channelVolumes[sendChannel],
                    0.0f, 1.0f);
                levels[levelOff + sendChannel] = level;
                ++numRoutes;
            }
        }

        if (numRoutes == 0) {
            return;
        }

        std::array<XAUDIO2_SEND_DESCRIPTOR, MAX_ROUTES> sendDescs;
        for (int i = 0; i < numRoutes; i++) {
            sendDescs[i] = {0, outVoices[i]};
        }

        const XAUDIO2_VOICE_SENDS sends{static_cast<UINT32>(numRoutes), sendDescs.data()};
        HRESULT hr = buffer->xaVoice->SetOutputVoices(&sends);
        if (FAILED(hr)) {
            throw std::runtime_error("Failed to set output voices");
        }

        for (int i = 0; i < numRoutes; i++) {
            hr = buffer->xaVoice->SetOutputMatrix(
                outVoices[i], 
                buffer->channels, 
                1, 
                &levels[i * buffer->channels]);
            if (FAILED(hr)) {
                throw std::runtime_error("Failed to set output matrix");
            }
        }
    }
    catch (...) {
        SetStatus(OPEN_SEGAERR_UNKNOWN);
    }
}

__declspec(dllexport) OPEN_SEGASTATUS SEGAAPI_SetSendRouting(void* hHandle, unsigned int dwChannel, unsigned int dwSend, OPEN_HAROUTING dwDest)
{
    if (hHandle == NULL)
    {
        info("SEGAAPI_SetSendRouting: Handle: %08X, Status: OPEN_SEGAERR_BAD_HANDLE", hHandle);
        return SetStatus(OPEN_SEGAERR_BAD_HANDLE);
    }

    info("SEGAAPI_SetSendRouting: hHandle: %08X dwChannel: %08X dwSend: %08X dwDest: %08X", 
         hHandle, dwChannel, dwSend, dwDest);

    try {
        auto* buffer = static_cast<OPEN_segaapiBuffer_t*>(hHandle);
        
        if (dwSend >= MAX_ROUTES) {
            return SetStatus(OPEN_SEGAERR_INVALID_PARAM);
        }

        if (dwChannel >= buffer->channels) {
            return SetStatus(OPEN_SEGAERR_INVALID_PARAM);
        }

        if (dwDest != OPEN_HA_UNUSED_PORT && dwDest >= g_submixVoices.size()) {
            return SetStatus(OPEN_SEGAERR_INVALID_PARAM);
        }

        buffer->sendRoutes[dwSend] = dwDest;
        buffer->sendChannels[dwSend] = dwChannel;

        updateRouting(buffer);
        return SetStatus(OPEN_SEGA_SUCCESS);
    }
    catch (...) {
        return SetStatus(OPEN_SEGAERR_UNKNOWN);
    }
}


__declspec(dllexport) OPEN_SEGASTATUS SEGAAPI_SetSendLevel(void* hHandle, unsigned int dwChannel, unsigned int dwSend, unsigned int dwLevel)
{
    if (hHandle == NULL)
    {
        info("SEGAAPI_SetSendLevel: Handle: %08X, Status: OPEN_SEGAERR_BAD_HANDLE", hHandle);
        return SetStatus(OPEN_SEGAERR_BAD_HANDLE);
    }

    info("SEGAAPI_SetSendLevel: hHandle: %08X dwChannel: %08X dwSend: %08X dwLevel: %08X", 
         hHandle, dwChannel, dwSend, dwLevel);

    try {
        auto* buffer = static_cast<OPEN_segaapiBuffer_t*>(hHandle);

        if (dwSend >= MAX_ROUTES) {
            return SetStatus(OPEN_SEGAERR_INVALID_PARAM);
        }

        if (dwChannel >= buffer->channels) {
            return SetStatus(OPEN_SEGAERR_INVALID_PARAM);
        }

        constexpr float MAX_LEVEL = static_cast<float>(0xFFFFFFFF);
        buffer->sendVolumes[dwSend] = dwLevel / MAX_LEVEL;
        buffer->sendChannels[dwSend] = dwChannel;

        updateRouting(buffer);
        return SetStatus(OPEN_SEGA_SUCCESS);
    }
    catch (...) {
        return SetStatus(OPEN_SEGAERR_UNKNOWN);
    }
}

__declspec(dllexport) OPEN_SEGASTATUS SEGAAPI_SetSynthParam(void* hHandle, OPEN_HASYNTHPARAMSEXT param, int lPARWValue)
{
    if (hHandle == NULL)
    {
        info("SEGAAPI_SetSynthParam: Handle: %08X, Status: OPEN_SEGAERR_BAD_HANDLE", hHandle);
        return SetStatus(OPEN_SEGAERR_BAD_HANDLE);
    }

    info("SEGAAPI_SetSynthParam: hHandle: %08X OPEN_HASYNTHPARAMSEXT: %08X lPARWValue: %08X", 
         hHandle, param, lPARWValue);

    enum {
        StartAddrsOffset, EndAddrsOffset, StartloopAddrsOffset, EndloopAddrsOffset,
        StartAddrsCoarseOffset, ModLfoToPitch, VibLfoToPitch, ModEnvToPitch,
        InitialFilterFc, InitialFilterQ, ModLfoToFilterFc, ModEnvToFilterFc,
        EndAddrsCoarseOffset, ModLfoToVolume, Unused1, ChorusEffectsSend,
        ReverbEffectsSend, Pan, Unused2, Unused3, Unused4, DelayModLFO,
        FreqModLFO, DelayVibLFO, FreqVibLFO, DelayModEnv, AttackModEnv,
        HoldModEnv, DecayModEnv, SustainModEnv, ReleaseModEnv,
        KeynumToModEnvHold, KeynumToModEnvDecay, DelayVolEnv, AttackVolEnv,
        HoldVolEnv, DecayVolEnv, SustainVolEnv, ReleaseVolEnv,
        KeynumToVolEnvHold, KeynumToVolEnvDecay, Instrument, Reserved1,
        KeyRange, VelRange, StartloopAddrsCoarseOffset, Keynum, Velocity,
        InitialAttenuation, Reserved2, EndloopAddrsCoarseOffset, CoarseTune,
        FineTune, SampleID, SampleModes, Reserved3, ScaleTuning,
        ExclusiveClass, OverridingRootKey, Unused5, EndOper
    };

    static const int mapping[26] = {
        InitialAttenuation, FineTune, InitialFilterFc, InitialFilterQ,
        DelayVolEnv, AttackVolEnv, HoldVolEnv, DecayVolEnv,
        SustainVolEnv, ReleaseVolEnv, DelayModEnv, AttackModEnv,
        HoldModEnv, DecayModEnv, SustainModEnv, ReleaseModEnv,
        DelayModLFO, FreqModLFO, DelayVibLFO, FreqVibLFO,
        ModLfoToPitch, VibLfoToPitch, ModLfoToFilterFc, ModLfoToVolume,
        ModEnvToPitch, ModEnvToFilterFc
    };

    try {
        auto* buffer = static_cast<OPEN_segaapiBuffer_t*>(hHandle);
        
        if (!buffer->xaVoice) {
            return SetStatus(OPEN_SEGAERR_UNKNOWN);
        }

        const int realParam = mapping[param];

        if (param == OPEN_HAVP_ATTENUATION)
        {
            const float volume = tsf_decibelsToGain(0.0f - lPARWValue / 10.0f);
            HRESULT hr = buffer->xaVoice->SetVolume(volume);
            if (FAILED(hr)) {
                return SetStatus(OPEN_SEGAERR_UNKNOWN);
            }
            info("SEGAAPI_SetSynthParam: OPEN_HAVP_ATTENUATION gain: %f dB: %d", volume, lPARWValue);
        }
        else if (param == OPEN_HAVP_PITCH)
        {
            const float semiTones = lPARWValue / 100.0f;
            const float freqRatio = XAudio2SemitonesToFrequencyRatio(semiTones);
            HRESULT hr = buffer->xaVoice->SetFrequencyRatio(freqRatio);
            if (FAILED(hr)) {
                return SetStatus(OPEN_SEGAERR_UNKNOWN);
            }
            info("SEGAAPI_SetSynthParam: OPEN_HAVP_PITCH hHandle: %08X semitones: %f freqRatio: %f", 
                 hHandle, semiTones, freqRatio);
        }

        return SetStatus(OPEN_SEGA_SUCCESS);
    }
    catch (...) {
        return SetStatus(OPEN_SEGAERR_UNKNOWN);
    }
}

__declspec(dllexport) int SEGAAPI_GetSynthParam(void* hHandle, OPEN_HASYNTHPARAMSEXT param)
{
    if (hHandle == NULL)
    {
        info("SEGAAPI_GetSynthParam: Handle: %08X, Status: OPEN_SEGAERR_BAD_HANDLE", hHandle);
        SetStatus(OPEN_SEGAERR_BAD_HANDLE);
        return 0;
    }

    info("SEGAAPI_GetSynthParam: hHandle: %08X OPEN_HASYNTHPARAMSEXT: %08X", hHandle, param);

    try {
        auto* buffer = static_cast<OPEN_segaapiBuffer_t*>(hHandle);
        
        if (!buffer->xaVoice) {
            SetStatus(OPEN_SEGAERR_UNKNOWN);
            return 0;
        }

        float value = 0.0f;
        HRESULT hr;
        
        switch (param) {
            case OPEN_HAVP_ATTENUATION:
                hr = buffer->xaVoice->GetVolume(&value);
                if (FAILED(hr)) {
                    SetStatus(OPEN_SEGAERR_UNKNOWN);
                    return 0;
                }
                SetStatus(OPEN_SEGA_SUCCESS);
                return static_cast<int>(-tsf_gainToDecibels(value) * 10.0f);
                
            case OPEN_HAVP_PITCH:
                hr = buffer->xaVoice->GetFrequencyRatio(&value);
                if (FAILED(hr)) {
                    SetStatus(OPEN_SEGAERR_UNKNOWN);
                    return 0;
                }
                SetStatus(OPEN_SEGA_SUCCESS);
                return static_cast<int>(XAudio2FrequencyRatioToSemitones(value) * 100.0f);
        }

        SetStatus(OPEN_SEGA_SUCCESS);
        return 0;
    }
    catch (...) {
        SetStatus(OPEN_SEGAERR_UNKNOWN);
        return 0;
    }
}

__declspec(dllexport) OPEN_SEGASTATUS SEGAAPI_SetSynthParamMultiple(void* hHandle, unsigned int dwNumParams, OPEN_SynthParamSet* pSynthParams)
{
    if (hHandle == NULL)
    {
        info("SEGAAPI_SetSynthParamMultiple: Handle: %08X, Status: OPEN_SEGAERR_BAD_HANDLE", hHandle);
        return SetStatus(OPEN_SEGAERR_BAD_HANDLE);
    }

    if (!pSynthParams || dwNumParams == 0)
    {
        return SetStatus(OPEN_SEGAERR_INVALID_PARAM);
    }

    info("SEGAAPI_SetSynthParamMultiple: hHandle: %08X dwNumParams: %08X pSynthParams: %08X", 
         hHandle, dwNumParams, pSynthParams);

    try {
        OPEN_SEGASTATUS status = OPEN_SEGA_SUCCESS;
        
        for (unsigned int i = 0; i < dwNumParams; i++)
        {
            status = SEGAAPI_SetSynthParam(hHandle, pSynthParams[i].param, pSynthParams[i].lPARWValue);
            if (status != OPEN_SEGA_SUCCESS)
            {
                return SetStatus(status);
            }
        }

        return SetStatus(OPEN_SEGA_SUCCESS);
    }
    catch (...) {
        return SetStatus(OPEN_SEGAERR_UNKNOWN);
    }
}

__declspec(dllexport) OPEN_SEGASTATUS SEGAAPI_SetChannelVolume(void* hHandle, unsigned int dwChannel, unsigned int dwVolume)
{
    if (hHandle == NULL)
    {
        info("SEGAAPI_SetChannelVolume: Handle: %08X, Status: OPEN_SEGAERR_BAD_HANDLE", hHandle);
        return SetStatus(OPEN_SEGAERR_BAD_HANDLE);
    }

    info("SEGAAPI_SetChannelVolume: hHandle: %08X dwChannel: %08X dwVolume: %08X", 
         hHandle, dwChannel, dwVolume);

    try {
        auto* buffer = static_cast<OPEN_segaapiBuffer_t*>(hHandle);
        
        if (dwChannel >= buffer->channels) {
            return SetStatus(OPEN_SEGAERR_INVALID_PARAM);
        }

        constexpr float MAX_VOLUME = static_cast<float>(0xFFFFFFFF);
        buffer->channelVolumes[dwChannel] = dwVolume / MAX_VOLUME;
        
        updateRouting(buffer);
        return SetStatus(OPEN_SEGA_SUCCESS);
    }
    catch (...) {
        return SetStatus(OPEN_SEGAERR_UNKNOWN);
    }
}

__declspec(dllexport) unsigned int SEGAAPI_GetChannelVolume(void* hHandle, unsigned int dwChannel)
{
    if (hHandle == NULL)
    {
        info("SEGAAPI_GetChannelVolume: Handle: %08X, Status: OPEN_SEGAERR_BAD_HANDLE", hHandle);
        SetStatus(OPEN_SEGAERR_BAD_HANDLE);
        return 0;
    }

    info("SEGAAPI_GetChannelVolume: hHandle: %08X dwChannel: %08X", hHandle, dwChannel);

    try {
        auto* buffer = static_cast<OPEN_segaapiBuffer_t*>(hHandle);
        
        if (dwChannel >= buffer->channels) {
            SetStatus(OPEN_SEGAERR_INVALID_PARAM);
            return 0;
        }

        constexpr float MAX_VOLUME = static_cast<float>(0xFFFFFFFF);
        SetStatus(OPEN_SEGA_SUCCESS);
        return static_cast<unsigned int>(buffer->channelVolumes[dwChannel] * MAX_VOLUME);
    }
    catch (...) {
        SetStatus(OPEN_SEGAERR_UNKNOWN);
        return 0;
    }
}

__declspec(dllexport) OPEN_SEGASTATUS SEGAAPI_Pause(void* hHandle)
{
    if (hHandle == NULL)
    {
        info("SEGAAPI_Pause: Handle: %08X, Status: OPEN_SEGAERR_BAD_HANDLE", hHandle);
        return SetStatus(OPEN_SEGAERR_BAD_HANDLE);
    }

    info("SEGAAPI_Pause: Handle: %08X", hHandle);

    try {
        OPEN_segaapiBuffer_t* buffer = static_cast<OPEN_segaapiBuffer_t*>(hHandle);
        
        HRESULT hr = buffer->xaVoice->Stop();
        if (FAILED(hr)) {
            return SetStatus(OPEN_SEGAERR_UNKNOWN);
        }

        buffer->paused = true;
        return SetStatus(OPEN_SEGA_SUCCESS);
    }
    catch (...) {
        return SetStatus(OPEN_SEGAERR_UNKNOWN);
    }
}

__declspec(dllexport) OPEN_SEGASTATUS SEGAAPI_PlayWithSetup(
    void* hHandle,
    unsigned int dwNumSendRouteParams, OPEN_SendRouteParamSet* pSendRouteParams,
    unsigned int dwNumSendLevelParams, OPEN_SendLevelParamSet* pSendLevelParams,
    unsigned int dwNumVoiceParams, OPEN_VoiceParamSet* pVoiceParams,
    unsigned int dwNumSynthParams, OPEN_SynthParamSet* pSynthParams)
{
    if (hHandle == NULL)
    {
        info("SEGAAPI_PlayWithSetup: Handle: %08X, Status: OPEN_SEGAERR_BAD_HANDLE", hHandle);
        return SetStatus(OPEN_SEGAERR_BAD_HANDLE);
    }

    info("SEGAAPI_PlayWithSetup: hHandle: %08X dwNumSendRouteParams: %d pSendRouteParams: %08X dwNumSendLevelParams: %d pSendLevelParams: %08X dwNumVoiceParams: %d pVoiceParams: %08X dwNumSynthParams: %d pSynthParams: %08X", 
         hHandle, dwNumSendRouteParams, pSendRouteParams ? *pSendRouteParams : 0, 
         dwNumSendLevelParams, pSendLevelParams ? *pSendLevelParams : 0,
         dwNumVoiceParams, pVoiceParams ? *pVoiceParams : 0,
         dwNumSynthParams, pSynthParams ? *pSynthParams : 0);

    try {
        auto* buffer = static_cast<OPEN_segaapiBuffer_t*>(hHandle);
        buffer->playWithSetup = true;

        unsigned int loopStart = 0;
        unsigned int loopEnd = 0;
        unsigned int loopState = 0;
        unsigned int endOffset = 0;

        if (pSendRouteParams && dwNumSendRouteParams > 0)
        {
            for (unsigned int i = 0; i < dwNumSendRouteParams; i++)
            {
                OPEN_SEGASTATUS status = SEGAAPI_SetSendRouting(hHandle, pSendRouteParams[i].dwChannel, 
                                      pSendRouteParams[i].dwSend, pSendRouteParams[i].dwDest);
                if (status != OPEN_SEGA_SUCCESS) return SetStatus(status);
            }
        }

        if (pSendLevelParams && dwNumSendLevelParams > 0)
        {
            for (unsigned int i = 0; i < dwNumSendLevelParams; i++)
            {
                OPEN_SEGASTATUS status = SEGAAPI_SetSendLevel(hHandle, pSendLevelParams[i].dwChannel, 
                                    pSendLevelParams[i].dwSend, pSendLevelParams[i].dwLevel);
                if (status != OPEN_SEGA_SUCCESS) return SetStatus(status);
            }
        }

        if (pVoiceParams && dwNumVoiceParams > 0)
        {
            for (unsigned int i = 0; i < dwNumVoiceParams; i++)
            {
                OPEN_SEGASTATUS status = OPEN_SEGA_SUCCESS;
                switch (pVoiceParams[i].VoiceIoctl)
                {
                    case OPEN_VOICEIOCTL_SET_START_LOOP_OFFSET:
                        status = SEGAAPI_SetStartLoopOffset(hHandle, pVoiceParams[i].dwParam1);
                        loopStart = pVoiceParams[i].dwParam1;
                        break;
                    case OPEN_VOICEIOCTL_SET_END_LOOP_OFFSET:
                        status = SEGAAPI_SetEndLoopOffset(hHandle, pVoiceParams[i].dwParam1);
                        loopEnd = pVoiceParams[i].dwParam1;
                        break;
                    case OPEN_VOICEIOCTL_SET_END_OFFSET:
                        status = SEGAAPI_SetEndOffset(hHandle, pVoiceParams[i].dwParam1);
                        endOffset = pVoiceParams[i].dwParam1;
                        break;
                    case OPEN_VOICEIOCTL_SET_LOOP_STATE:
                        status = SEGAAPI_SetLoopState(hHandle, pVoiceParams[i].dwParam1);
                        loopState = pVoiceParams[i].dwParam1;
                        break;
                    default:
                        info("Unimplemented! OPEN_VOICEIOCTL_%d", pVoiceParams[i].VoiceIoctl);
                        break;
                }
                if (status != OPEN_SEGA_SUCCESS) return SetStatus(status);
            }
        }

        info("Loopdata: hHandle: %08X, loopStart: %08X, loopEnd: %08X, endOffset: %08X, loopState: %d, size: %d", 
             hHandle, loopStart, loopEnd, endOffset, loopState, buffer->size);

        if (pSynthParams && dwNumSynthParams > 0)
        {
            for (unsigned int i = 0; i < dwNumSynthParams; i++)
            {
                OPEN_SEGASTATUS status = SEGAAPI_SetSynthParam(hHandle, pSynthParams[i].param, pSynthParams[i].lPARWValue);
                if (status != OPEN_SEGA_SUCCESS) return SetStatus(status);
            }
        }

        return SEGAAPI_Play(hHandle);
    }
    catch (...) {
        return SetStatus(OPEN_SEGAERR_UNKNOWN);
    }
}

__declspec(dllexport) OPEN_SEGASTATUS SEGAAPI_GetLastStatus(void)
{
    info("SEGAAPI_GetLastStatus");
    return g_lastStatus;
}

}

#pragma optimize("", on)

