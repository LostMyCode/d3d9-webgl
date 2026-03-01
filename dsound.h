#pragma once

#include <cstdint>
#include "d3d9.h" // For HRESULT, GUID, etc.

// DirectSound Constants
#define DS_OK                          0
#define DSBCAPS_CTRLPAN                0x00000010
#define DSBCAPS_CTRLVOLUME             0x00000020
#define DSBCAPS_CTRLFREQUENCY          0x00000040
#define DSBCAPS_STATIC                 0x00000002
#define DSBCAPS_CTRL3D                 0x00000010
#define DSBCAPS_LOCSOFTWARE            0x00000008
#define DSBCAPS_MUTE3DATMAXDISTANCE    0x00000020
#define DSBCAPS_CTRLFX                 0x00000200

#define DSBPLAY_LOOPING                0x00000001
#define DSBSTATUS_PLAYING              0x00000001
#define DSBSTATUS_BUFFERLOST           0x00000002
#define DSBSTATUS_LOOPING              0x00000004
#define DSBSTATUS_LOCSOFTWARE          0x00000008
#define DSBSTATUS_LOCHARDWARE          0x00000010
#define DSBSTATUS_TERMINATED           0x00000020

#define DS3D_IMMEDIATE                 0x00000000
#define DS3DMODE_NORMAL                0x00000000
#define DS3DMODE_HEADRELATIVE          0x00000001
#define DS3DMODE_DISABLE               0x00000002

#define WAVE_FORMAT_PCM                1

// GUIDs (Dummy)
static const GUID DS3DALG_NO_VIRTUALIZATION = { 0 };
static const GUID IID_IDirectSound3DBuffer = { 0 };
static const GUID IID_IDirectSoundBuffer8 = { 0 };
static const GUID GUID_DSFX_WAVES_REVERB = { 0 };
static const GUID IID_IDirectSoundFXWavesReverb8 = { 0 };

typedef struct _DSBUFFERDESC {
    DWORD dwSize;
    DWORD dwFlags;
    DWORD dwBufferBytes;
    DWORD dwReserved;
    void* lpwfxFormat;
    GUID  guid3DAlgorithm;
} DSBUFFERDESC;

typedef struct _DSEFFECTDESC {
    DWORD dwSize;
    DWORD dwFlags;
    GUID  guidDSFXClass;
    uintptr_t dwReserved1;
    uintptr_t dwReserved2;
} DSEFFECTDESC;

typedef struct _DSFXWavesReverb {
    float fInGain;
    float fReverbMix;
    float fReverbTime;
    float fHighFreqRTRatio;
} DSFXWavesReverb;

#define DSFX_WAVESREVERB_INGAIN_DEFAULT 0.0f
#define DSFX_WAVESREVERB_REVERBMIX_DEFAULT 0.0f
#define DSFX_WAVESREVERB_REVERBTIME_DEFAULT 1000.0f
#define DSFX_WAVESREVERB_HIGHFREQRTRATIO_DEFAULT 0.001f

// Interfaces
struct IDirectSoundBuffer : public IUnknown {
    virtual HRESULT Lock(DWORD dwOffset, DWORD dwBytes, LPVOID *ppvAudioPtr1, LPDWORD pdwAudioBytes1, LPVOID *ppvAudioPtr2, LPDWORD pdwAudioBytes2, DWORD dwFlags) = 0;
    virtual HRESULT Unlock(LPVOID pvAudioPtr1, DWORD dwBytes1, LPVOID pvAudioPtr2, DWORD dwBytes2) = 0;
    virtual HRESULT Play(DWORD dwReserved1, DWORD dwReserved2, DWORD dwFlags) = 0;
    virtual HRESULT Stop() = 0;
    virtual HRESULT GetStatus(LPDWORD pdwStatus) = 0;
    virtual HRESULT SetCurrentPosition(DWORD dwNewPosition) = 0;
    virtual HRESULT SetVolume(LONG lVolume) = 0;
    virtual HRESULT GetVolume(LPLONG plVolume) = 0;
    virtual HRESULT QueryInterface(const GUID& riid, void** ppvObj) = 0;
};

struct IDirectSoundBuffer8 : public IDirectSoundBuffer {
    virtual HRESULT SetFX(DWORD dwEffectsCount, DSEFFECTDESC* pEffectDesc, LPDWORD pdwResultCodes) = 0;
    virtual HRESULT GetObjectInPath(const GUID& guidObject, DWORD dwIndex, const GUID& iidInterface, void** ppObject) = 0;
};

struct IDirectSound3DBuffer : public IUnknown {
    virtual HRESULT SetPosition(float x, float y, float z, DWORD dwApply) = 0;
    virtual HRESULT SetMinDistance(float fMinDistance, DWORD dwApply) = 0;
    virtual HRESULT SetMaxDistance(float fMaxDistance, DWORD dwApply) = 0;
    virtual HRESULT SetMode(DWORD dwMode, DWORD dwApply) = 0;
};

struct IDirectSound : public IUnknown {
    virtual HRESULT CreateSoundBuffer(const DSBUFFERDESC* pcDSBufferDesc, IDirectSoundBuffer** ppDSBuffer, IUnknown* pUnkOuter) = 0;
    virtual HRESULT DuplicateSoundBuffer(IDirectSoundBuffer* pDSBufferOriginal, IDirectSoundBuffer** ppDSBufferDuplicate) = 0;
};

struct IDirectSoundFXWavesReverb8 : public IUnknown {
    virtual HRESULT SetAllParameters(const DSFXWavesReverb* pcDsFxWavesReverb) = 0;
};

typedef IDirectSound* LPDIRECTSOUND;
typedef IDirectSoundBuffer* LPDIRECTSOUNDBUFFER;
typedef IDirectSoundBuffer8* LPDIRECTSOUNDBUFFER8;
typedef IDirectSound3DBuffer* LPDIRECTSOUND3DBUFFER;
typedef IDirectSoundFXWavesReverb8* LPDIRECTSOUNDFXWAVESREVERB8;

#define DSBVOLUME_MIN -10000
#define DSBVOLUME_MAX 0
