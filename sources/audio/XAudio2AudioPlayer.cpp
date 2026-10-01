#include "audio/XAudio2AudioPlayer.h"
#include "audio/PcmLayout.h"
#include "audio/services.h"
#include <algorithm>
#include <cstring>
#include <limits.h>

namespace Ship {
// Selected before engine creation; other consumers retain the SDK default.
static volatile LONG engineProcessor = XAUDIO2_DEFAULT_PROCESSOR;
extern "C" void ShipyardAudioSetProcessor(unsigned processorMask) {
    if (processorMask == 0 || (processorMask & ~0x3fu) != 0)
        ShipyardAudioFatal("XAudio2 processor mask", E_INVALIDARG);
    InterlockedExchange(&engineProcessor, static_cast<LONG>(processorMask));
}

static_assert(ShipyardAudio::PcmMinimumRate == XAUDIO2_MIN_SAMPLE_RATE &&
                  ShipyardAudio::PcmMaximumRate == XAUDIO2_MAX_SAMPLE_RATE,
              "PCM validation bounds must match the installed XAudio2 SDK");

XAudio2AudioPlayer::XAudio2AudioPlayer(AudioSettings settings, bool allowRaw51)
    : AudioPlayer(settings), engine_(NULL), master_(NULL), voice_(NULL), voiceError_(0),
      submittedFrames_(0), openedRate_(0), openedChannels_(0), frameBytes_(0), capacityFrames_(0),
      started_(false), allowRaw51_(allowRaw51) {
}

XAudio2AudioPlayer::~XAudio2AudioPlayer() {
    DoClose();
}

void XAudio2AudioPlayer::Check(HRESULT result, const char* operation) {
    if (FAILED(result))
        ShipyardAudioFatal(operation, result);
}

void XAudio2AudioPlayer::CheckVoice() {
    Check(static_cast<HRESULT>(InterlockedCompareExchange(&voiceError_, 0, 0)),
          "XAudio2 voice callback");
}

void __stdcall XAudio2AudioPlayer::OnBufferEnd(void* context) {
    ShipyardAudio::PcmBufferPool::Block* block =
        static_cast<ShipyardAudio::PcmBufferPool::Block*>(context);
    try {
        block->owner->Release(block);
    } catch (...) {
        ShipyardAudioFatal("XAudio2 completion notification", E_FAIL);
    }
}

void __stdcall XAudio2AudioPlayer::OnVoiceError(void*, HRESULT result) {
    InterlockedExchange(&voiceError_, result);
    try {
        storage_.Wake();
    } catch (...) {
        ShipyardAudioFatal("XAudio2 error notification", E_FAIL);
    }
}

bool XAudio2AudioPlayer::DoInit() {
    DoClose();
    const int32_t rate = GetSampleRate();
    const int32_t channels = GetNumOutputChannels();
    const int32_t sampleLength = GetSampleLength();
    const int32_t desired = GetDesiredBuffered();
    ShipyardAudio::PcmLayout layout;
    if (!ShipyardAudio::ComputePcmLayout(rate, channels, GetAudioChannels(), sampleLength, desired,
                                         layout, allowRaw51_)) {
        ShipyardAudioLog(
            "XAudio2 rejected settings rate=%d channels=%d mode=%d length=%d desired=%d", rate,
            channels, static_cast<int>(GetAudioChannels()), sampleLength, desired);
        return false;
    }
    openedRate_ = layout.sampleRate;
    openedChannels_ = layout.channels;
    frameBytes_ = layout.frameBytes;
    capacityFrames_ = layout.capacityFrames;
    if (!storage_.Initialize(layout.blockFrames * frameBytes_)) {
        DoClose();
        return false;
    }
    HRESULT result = XAudio2Create(&engine_, 0, static_cast<XAUDIO2_PROCESSOR>(
        InterlockedCompareExchange(&engineProcessor, 0, 0)));
    if (SUCCEEDED(result))
        result = engine_->CreateMasteringVoice(&master_);
    WAVEFORMATEXTENSIBLE format = {0};
    format.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
    format.Format.nChannels = static_cast<WORD>(channels);
    format.Format.nSamplesPerSec = rate;
    format.Format.wBitsPerSample = 16;
    format.Format.nBlockAlign = static_cast<WORD>(frameBytes_);
    format.Format.nAvgBytesPerSec = rate * frameBytes_;
    format.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
    format.Samples.wValidBitsPerSample = 16;
    format.dwChannelMask = channels == 2 ? SPEAKER_STEREO : SPEAKER_5POINT1_SURROUND;
    format.SubFormat = KSDATAFORMAT_SUBTYPE_PCM;
    if (SUCCEEDED(result))
        result = engine_->CreateSourceVoice(&voice_, &format.Format, 0, 1.0f, this);
    if (FAILED(result)) {
        ShipyardAudioLog("XAudio2 initialization failed HRESULT=%08x",
                         static_cast<unsigned>(result));
        DoClose();
        return false;
    }
    ShipyardAudioLog("XAudio2 PCM rate=%u channels=%u frame_bytes=%u storage=%u reserve_frames=%d",
                     openedRate_, openedChannels_, frameBytes_,
                     storage_.CapacityBytes() * ShipyardAudio::PcmBufferPool::BlockCount, desired);
    return true;
}

int32_t XAudio2AudioPlayer::Buffered() {
    if (!voice_)
        return 0;
    CheckVoice();
    XAUDIO2_VOICE_STATE state;
    voice_->GetState(&state);
    const UINT64 queued =
        state.SamplesPlayed < submittedFrames_ ? submittedFrames_ - state.SamplesPlayed : 0;
    return queued > INT_MAX ? INT_MAX : static_cast<int32_t>(queued);
}

void XAudio2AudioPlayer::StartVoice() {
    if (!started_) {
        Check(voice_->Start(), "XAudio2 primed start");
        started_ = true;
    }
}

void XAudio2AudioPlayer::DoPlay(const uint8_t* buffer, size_t length) {
    if (!voice_ || (length && !buffer) || length % frameBytes_ ||
        GetSampleRate() != static_cast<int32_t>(openedRate_) ||
        GetNumOutputChannels() != static_cast<int32_t>(openedChannels_) ||
        GetDesiredBuffered() < 0 || static_cast<unsigned>(GetDesiredBuffered()) > capacityFrames_) {
        ShipyardAudioFatal("XAudio2 PCM/settings contract", E_INVALIDARG);
    }
    while (length) {
        CheckVoice();
        const int32_t queued = Buffered();
        if (started_ && !queued) {
            Check(voice_->Stop(), "XAudio2 rebuffer stop");
            started_ = false;
        }
        ShipyardAudio::PcmBufferPool::Block* block = storage_.TryAcquire();
        while (!block) {
            CheckVoice();
            // Small submissions can occupy every block before meeting the start reserve.
            StartVoice();
            if (!storage_.WaitAvailable(2000))
                ShipyardAudioFatal("XAudio2 device stalled", E_FAIL);
            block = storage_.TryAcquire();
        }
        CheckVoice();
        const unsigned bytes = static_cast<unsigned>(
            (std::min)(length, static_cast<size_t>(storage_.CapacityBytes())));
        std::memcpy(block->samples, buffer, bytes);
        XAUDIO2_BUFFER packet = {0};
        packet.AudioBytes = bytes;
        packet.pAudioData = block->samples;
        packet.pContext = block;
        const HRESULT result = voice_->SubmitSourceBuffer(&packet);
        if (FAILED(result)) {
            storage_.Release(block);
            Check(result, "XAudio2 submit");
        }
        submittedFrames_ += bytes / frameBytes_;
        if (!started_ && Buffered() >= GetDesiredBuffered())
            StartVoice();
        buffer += bytes;
        length -= bytes;
    }
}

void XAudio2AudioPlayer::DoClose() {
    // DestroyVoice completes callbacks before the storage events are released.
    if (voice_) {
        voice_->Stop();
        voice_->DestroyVoice();
        voice_ = NULL;
    }
    if (master_) {
        master_->DestroyVoice();
        master_ = NULL;
    }
    if (engine_) {
        engine_->Release();
        engine_ = NULL;
    }
    storage_.Close();
    submittedFrames_ = 0;
    voiceError_ = 0;
    started_ = false;
    openedRate_ = 0;
    openedChannels_ = 0;
    frameBytes_ = 0;
    capacityFrames_ = 0;
}
} // namespace Ship
