#ifndef SHIPYARD_AUDIO_XAUDIO2_AUDIO_PLAYER_H
#define SHIPYARD_AUDIO_XAUDIO2_AUDIO_PLAYER_H

#include "ship/audio/AudioPlayer.h"
#include "audio/PcmBufferPool.h"
#include <xaudio2.h>

namespace Ship {
// Construct through shared_ptr<XAudio2AudioPlayer> before conversion to AudioPlayer;
// the upstream AudioPlayer destructor is nonvirtual.
class XAudio2AudioPlayer : public AudioPlayer, private IXAudio2VoiceCallback {
public:
    explicit XAudio2AudioPlayer(AudioSettings settings, bool allowRaw51 = true);
    ~XAudio2AudioPlayer();
    int32_t Buffered();
protected:
    bool DoInit();
    void DoClose();
    void DoPlay(const uint8_t* buffer, size_t length);
private:
    XAudio2AudioPlayer(const XAudio2AudioPlayer&);
    XAudio2AudioPlayer& operator=(const XAudio2AudioPlayer&);
    void CheckVoice();
    void Check(HRESULT result, const char* operation);
    void StartVoice();
    void __stdcall OnVoiceProcessingPassStart(UINT32) {}
    void __stdcall OnVoiceProcessingPassEnd() {}
    void __stdcall OnStreamEnd() {}
    void __stdcall OnBufferStart(void*) {}
    void __stdcall OnLoopEnd(void*) {}
    void __stdcall OnBufferEnd(void* context);
    void __stdcall OnVoiceError(void*, HRESULT result);
    IXAudio2* engine_;
    IXAudio2MasteringVoice* master_;
    IXAudio2SourceVoice* voice_;
    ShipyardAudio::PcmBufferPool storage_;
    volatile LONG voiceError_;
    UINT64 submittedFrames_;
    unsigned openedRate_, openedChannels_, frameBytes_, capacityFrames_;
    bool started_, allowRaw51_;
};
}

#endif
