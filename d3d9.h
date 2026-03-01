#pragma once

#include <cstdint>
#include <errno.h>
#include <stdio.h>

// Basic D3D9 types
#ifndef _WINDOWS_BASE_TYPES_
#define _WINDOWS_BASE_TYPES_
typedef uint16_t WORD;
#ifndef _DWORD_DEFINED
#define _DWORD_DEFINED
typedef uint32_t DWORD;
#endif
typedef uint32_t UINT;
typedef int32_t INT;
#ifndef _BOOL_DEFINED
#define _BOOL_DEFINED
typedef int BOOL;
#endif
typedef void* HWND;
#ifndef _HRESULT_DEFINED
#define _HRESULT_DEFINED
typedef long HRESULT;
#endif
typedef float FLOAT;
typedef char* LPSTR;
typedef const char* LPCSTR;
typedef const char* LPCTSTR;
typedef void* HINSTANCE;
typedef void* HMODULE;
typedef intptr_t LRESULT;
typedef intptr_t LONG_PTR;
typedef intptr_t INT_PTR;
typedef uintptr_t WPARAM;
typedef intptr_t LPARAM;
typedef unsigned long ULONG;
typedef uint8_t BYTE;
typedef long LONG;
typedef LONG* LPLONG;
typedef void VOID;
typedef void* LPVOID;
typedef DWORD* LPDWORD;
#endif
#ifndef WINAPI
#define WINAPI
#endif

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

#ifndef _RECT_DEFINED
typedef struct tagRECT {
    LONG left;
    LONG top;
    LONG right;
    LONG bottom;
} RECT;
#define _RECT_DEFINED
#endif

#ifndef _WIN32
#include <emscripten/emscripten.h>
#include <emscripten/html5.h>
#include "windows_compat.h"
#endif

typedef char TCHAR;
typedef int64_t INT64;
typedef uint64_t UINT64;



#define D3DVS_VERSION(major, minor) (0xFFFE0000 | ((major) << 8) | (minor))
#define D3DPS_VERSION(major, minor) (0xFFFF0000 | ((major) << 8) | (minor))
#define D3DSHADER_VERSION_MAJOR(_ver) (((_ver) >> 8) & 0xFF)
#define D3DSHADER_VERSION_MINOR(_ver) ((_ver) & 0xFF)

// NOTE: Returns a pointer to a static buffer. Subsequent calls overwrite the buffer.
// Safe for single-use in log statements (e.g. printf("%s", DXGetErrorString(hr))),
// but do NOT store or use two calls in the same expression (e.g. printf("%s %s", ..., ...)).
// Emscripten is single-threaded so there is no re-entrancy concern.
inline const char* DXGetErrorString(HRESULT hr) {
    static char buf[32];
    snprintf(buf, sizeof(buf), "0x%08X", (unsigned int)hr);
    return buf;
}

#ifndef _D3DRECT_DEFINED
typedef struct _D3DRECT {
    LONG x1;
    LONG y1;
    LONG x2;
    LONG y2;
} D3DRECT;
#define _D3DRECT_DEFINED
#endif

#define D3DCOLORWRITEENABLE_RED (1 << 0)
#define D3DCOLORWRITEENABLE_GREEN (1 << 1)
#define D3DCOLORWRITEENABLE_BLUE (1 << 2)
#define D3DCOLORWRITEENABLE_ALPHA (1 << 3)


#define D3DCOLOR_COLORVALUE(r,g,b,a) \
    ((DWORD)((((DWORD)((a)*255.f))<<24)|(((DWORD)((r)*255.f))<<16)|(((DWORD)((g)*255.f))<<8)|((DWORD)((b)*255.f))))

// FVF
#define D3DFVF_XYZ      0x002
#define D3DFVF_NORMAL   0x010
#define D3DFVF_DIFFUSE  0x040
#define D3DFVF_TEX1     0x100
#define D3DFVF_XYZB2    0x1000 // Just a placeholder value

// D3DTEXTUREOP
typedef enum _D3DTEXTUREOP {
    D3DTOP_DISABLE = 1,
    D3DTOP_SELECTARG1 = 2,
    D3DTOP_SELECTARG2 = 3,
    D3DTOP_MODULATE = 4,
    D3DTOP_MODULATE2X = 5,
    D3DTOP_MODULATE4X = 6,
    D3DTOP_ADD = 7,
    D3DTOP_ADDSIGNED = 8,
    D3DTOP_ADDSIGNED2X = 9,
    D3DTOP_SUBTRACT = 10,
    D3DTOP_ADDSMOOTH = 11,
    D3DTOP_BLENDDIFFUSEALPHA = 12,
    D3DTOP_BLENDTEXTUREALPHA = 13,
    D3DTOP_BLENDFACTORALPHA = 14,
    D3DTOP_BLENDTEXTUREALPHAPM = 15,
    D3DTOP_BLENDCURRENTALPHA = 16,
    D3DTOP_PREMODULATE = 17,
    D3DTOP_MODULATEALPHA_ADDCOLOR = 18,
    D3DTOP_MODULATECOLOR_ADDALPHA = 19,
    D3DTOP_MODULATEINVALPHA_ADDCOLOR = 20,
    D3DTOP_MODULATEINVCOLOR_ADDALPHA = 21,
    D3DTOP_BUMPENVMAP = 22,
    D3DTOP_BUMPENVMAPLUMINANCE = 23,
    D3DTOP_DOTPRODUCT3 = 24,
    D3DTOP_MULTIPLYADD = 25,
    D3DTOP_LERP = 26,
    D3DTOP_FORCE_DWORD = 0x7fffffff,
} D3DTEXTUREOP;

// D3DTEXTUREARG
#define D3DTA_DIFFUSE 0x00000000
#define D3DTA_CURRENT 0x00000001
#define D3DTA_TEXTURE 0x00000002
#define D3DTA_TFACTOR 0x00000003
#define D3DTA_SPECULAR 0x00000004
#define D3DTA_TEMP 0x00000005
#define D3DTA_CONSTANT 0x00000006
#define D3DTA_SELECTMASK 0x0000000f

// D3DTEXTURESTAGESTATETYPE
enum D3DTEXTURESTAGESTATETYPE {
    D3DTSS_COLOROP = 1,
    D3DTSS_COLORARG1 = 2,
    D3DTSS_COLORARG2 = 3,
    D3DTSS_ALPHAOP = 4,
    D3DTSS_ALPHAARG1 = 5,
    D3DTSS_ALPHAARG2 = 6,
    D3DTSS_BUMPENVMAT00 = 7,
    D3DTSS_BUMPENVMAT01 = 8,
    D3DTSS_BUMPENVMAT10 = 9,
    D3DTSS_BUMPENVMAT11 = 10,
    D3DTSS_TEXCOORDINDEX = 11,
    D3DTSS_BUMPENVLSCALE = 22,
    D3DTSS_BUMPENVLOFFSET = 23,
    D3DTSS_TEXTURETRANSFORMFLAGS = 24,
    D3DTSS_COLORARG0 = 26,
    D3DTSS_ALPHAARG0 = 27,
    D3DTSS_RESULTARG = 28,
    D3DTSS_CONSTANT = 32,
};

// D3DTEXTURECOORDINDEX
#define D3DTSS_TCI_PASSTHRU 0
#define D3DTSS_TCI_CAMERASPACENORMAL 0x00010000
#define D3DTSS_TCI_CAMERASPACEPOSITION 0x00020000
#define D3DTSS_TCI_CAMERASPACEREFLECTIONVECTOR 0x00040000

// D3DTEXTURETRANSFORMFLAGS
#define D3DTTFF_DISABLE 0
#define D3DTTFF_COUNT1 1
#define D3DTTFF_COUNT2 2
#define D3DTTFF_COUNT3 3
#define D3DTTFF_COUNT4 4
#define D3DTTFF_PROJECTED 256

// D3DSHADE
#define D3DSHADE_FLAT 1
#define D3DSHADE_GOURAUD 2
#define D3DSHADE_PHONG 3

struct PALETTEENTRY {
    BYTE peRed;
    BYTE peGreen;
    BYTE peBlue;
    BYTE peFlags;
};

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif
#ifndef S_FALSE
#define S_FALSE 1
#endif
#ifndef S_OK
#define S_OK 0
#endif
#ifndef D3D_OK
#define D3D_OK 0
#endif
#define D3D_SDK_VERSION 32
#define D3DADAPTER_DEFAULT 0
#ifndef MAX_PATH
#define MAX_PATH 260
#endif
#ifndef FAILED
#define FAILED(hr) ((HRESULT)(hr) < 0)
#endif
#ifndef SUCCEEDED
#define SUCCEEDED(hr) ((HRESULT)(hr) >= 0)
#endif
#define D3DERR_INVALIDCALL -1
#define D3DERR_UNSUPPORTEDTEXTUREFILTER -2
#define D3DERR_NOTAVAILABLE -3
#define D3DERR_DEVICELOST -4
#define D3DERR_DEVICENOTRESET -5
#define D3DDEVCAPS_HWTRANSFORMANDLIGHT 0x00000001
#define D3DOK_NOAUTOGEN 0

// D3D9 Color type
typedef DWORD D3DCOLOR;
#define D3DCOLOR_ARGB(a,r,g,b) \
    ((D3DCOLOR)((((a)&0xff)<<24)|(((r)&0xff)<<16)|(((g)&0xff)<<8)|((b)&0xff)))

// D3D9 types
typedef struct _D3DMATRIX {
    union {
        struct {
            float        _11, _12, _13, _14;
            float        _21, _22, _23, _24;
            float        _31, _32, _33, _34;
            float        _41, _42, _43, _44;

        };
        float m[4][4];
    };
} D3DMATRIX;


typedef struct _D3DVECTOR {
    float x;
    float y;
    float z;
} D3DVECTOR;


// Primitive types
enum D3DPRIMITIVETYPE {
    D3DPT_POINTLIST = 1,
    D3DPT_LINELIST = 2,
    D3DPT_LINESTRIP = 3,
    D3DPT_TRIANGLELIST = 4,
    D3DPT_TRIANGLESTRIP = 5,
    D3DPT_TRIANGLEFAN = 6,
};

enum D3DFILLMODE {
    D3DFILL_POINT = 1,
    D3DFILL_WIREFRAME = 2,
    D3DFILL_SOLID = 3,
};

// Fog modes
enum D3DFOGMODE {
    D3DFOG_NONE = 0,
    D3DFOG_EXP = 1,
    D3DFOG_EXP2 = 2,
    D3DFOG_LINEAR = 3,
    D3DFOG_FORCE_DWORD = 0x7fffffff,
};

enum D3DCULL {
    D3DCULL_NONE = 1,
    D3DCULL_CW = 2,
    D3DCULL_CCW = 3,
};

enum D3DCMPFUNC {
    D3DCMP_NEVER = 1,
    D3DCMP_LESS = 2,
    D3DCMP_EQUAL = 3,
    D3DCMP_LESSEQUAL = 4,
    D3DCMP_GREATER = 5,
    D3DCMP_NOTEQUAL = 6,
    D3DCMP_GREATEREQUAL = 7,
    D3DCMP_ALWAYS = 8,
};

// Flexible Vertex Format flags
#define D3DFVF_XYZ          0x002
#define D3DFVF_DIFFUSE      0x040
#define D3DFVF_XYZRHW       0x004
#define D3DFVF_NORMAL       0x010
#define D3DFVF_TEX1         0x100

#define D3DFVF_TEX2         0x200

// Clear flags
#define D3DCLEAR_TARGET     0x00000001L
#define D3DCLEAR_ZBUFFER    0x00000002L
#define D3DCLEAR_STENCIL    0x00000004L

// Format types
typedef DWORD D3DFORMAT;
enum {
    D3DFMT_UNKNOWN = 0,
    D3DFMT_R8G8B8 = 20,
    D3DFMT_A8R8G8B8 = 21,
    D3DFMT_X8R8G8B8 = 22,
    D3DFMT_R5G6B5 = 23,
    D3DFMT_A1R5G5B5 = 25,
    D3DFMT_A4R4G4B4 = 26,
    D3DFMT_A8B8G8R8 = 32,
    D3DFMT_X8B8G8R8 = 33,
    D3DFMT_INDEX16 = 101,
    D3DFMT_INDEX32 = 102,
    D3DFMT_D24S8 = 75,
    D3DFMT_D16 = 80,
    D3DFMT_DXT1 = 0x31545844, // 'DXT1'
    D3DFMT_DXT2 = 0x32545844, // 'DXT2'
    D3DFMT_DXT3 = 0x33545844, // 'DXT3'
    D3DFMT_DXT4 = 0x34545844, // 'DXT4'
    D3DFMT_DXT5 = 0x35545844, // 'DXT5'
};

// Device types
enum D3DDEVTYPE {
    D3DDEVTYPE_HAL = 1,
    D3DDEVTYPE_REF = 2,
};

// Transform states
enum D3DTRANSFORMSTATETYPE {
    D3DTS_VIEW = 2,
    D3DTS_PROJECTION = 3,
    D3DTS_TEXTURE0 = 16,
    D3DTS_TEXTURE1 = 17,
    D3DTS_TEXTURE2 = 18,
    D3DTS_TEXTURE3 = 19,
    D3DTS_TEXTURE4 = 20,
    D3DTS_TEXTURE5 = 21,
    D3DTS_TEXTURE6 = 22,
    D3DTS_TEXTURE7 = 23,
    D3DTS_WORLD = 256,
    D3DTS_WORLDMATRIX = 256,
};

// Render states
enum D3DRENDERSTATETYPE {
    D3DRS_FILLMODE = 8,
    D3DRS_SHADEMODE = 9,
    D3DRS_ZWRITEENABLE = 14,
    D3DRS_ALPHATESTENABLE = 15,
    D3DRS_SRCBLEND = 19,
    D3DRS_DESTBLEND = 20,
    D3DRS_CULLMODE = 22,
    D3DRS_ZFUNC = 23,
    D3DRS_ALPHAREF = 24,
    D3DRS_ALPHAFUNC = 25,
    D3DRS_ALPHABLENDENABLE = 27,
    D3DRS_FOGENABLE = 28,
    D3DRS_FOGCOLOR = 34,
    D3DRS_FOGTABLEMODE = 35,
    D3DRS_FOGSTART = 36,
    D3DRS_FOGEND = 37,
    D3DRS_FOGDENSITY = 38,
    D3DRS_RANGEFOGENABLE = 48,
    D3DRS_SPECULARENABLE = 29,
    D3DRS_TEXTUREFACTOR = 60,
    D3DRS_LIGHTING = 137,
    D3DRS_AMBIENT = 139,
    D3DRS_ZENABLE = 7,
    D3DRS_NORMALIZENORMALS = 43,
    
    // Point Sprites
    D3DRS_POINTSPRITEENABLE = 154,
    D3DRS_POINTSCALEENABLE = 155,
    D3DRS_POINTSCALE_A = 156,
    D3DRS_POINTSCALE_B = 157,
    D3DRS_POINTSCALE_C = 158,
    D3DRS_POINTSIZE = 159,
    D3DRS_POINTSIZE_MIN = 160,
    D3DRS_POINTSIZE_MAX = 161,

    D3DRS_VERTEXBLEND = 151,
    D3DRS_CLIPPLANEENABLE = 152,
    D3DRS_INDEXEDVERTEXBLENDENABLE = 167,
    D3DRS_COLORWRITEENABLE = 168,
    D3DRS_CLIPPING = 136,

    // Stencil states
    D3DRS_STENCILENABLE = 52,
    D3DRS_STENCILFAIL = 53,
    D3DRS_STENCILZFAIL = 54,
    D3DRS_STENCILPASS = 55,
    D3DRS_STENCILFUNC = 56,
    D3DRS_STENCILREF = 57,
    D3DRS_STENCILMASK = 58,
    D3DRS_STENCILWRITEMASK = 59,
    D3DRS_TWOSIDEDSTENCILMODE = 185,
    D3DRS_CCW_STENCILFAIL = 186,
    D3DRS_CCW_STENCILZFAIL = 187,
    D3DRS_CCW_STENCILPASS = 188,
    D3DRS_CCW_STENCILFUNC = 189,
    D3DRS_SCISSORTESTENABLE = 174,
};

enum D3DSTENCILOP {
    D3DSTENCILOP_KEEP = 1,
    D3DSTENCILOP_ZERO = 2,
    D3DSTENCILOP_REPLACE = 3,
    D3DSTENCILOP_INCRSAT = 4,
    D3DSTENCILOP_DECRSAT = 5,
    D3DSTENCILOP_INVERT = 6,
    D3DSTENCILOP_INCR = 7,
    D3DSTENCILOP_DECR = 8,
};

#define D3DLOCK_NOOVERWRITE 0x00001000L
#define D3DUSAGE_POINTS 0x00000040L
#define D3DFVF_SPECULAR 0x00000040L



typedef struct _D3DDEVICE_CREATION_PARAMETERS {
    UINT AdapterOrdinal;
    D3DDEVTYPE DeviceType;
    HWND hFocusWindow;
    DWORD BehaviorFlags;
} D3DDEVICE_CREATION_PARAMETERS;


#define D3DLOCK_DISCARD     0x00002000L

// Blend modes
enum D3DBLEND {
    D3DBLEND_ZERO = 1,
    D3DBLEND_ONE = 2,
    D3DBLEND_SRCCOLOR = 3,
    D3DBLEND_INVSRCCOLOR = 4,
    D3DBLEND_SRCALPHA = 5,
    D3DBLEND_INVSRCALPHA = 6,
    D3DBLEND_DESTALPHA = 7,
    D3DBLEND_INVDESTALPHA = 8,
    D3DBLEND_DESTCOLOR = 9,
    D3DBLEND_INVDESTCOLOR = 10,
};

// Create flags
#define D3DCREATE_SOFTWARE_VERTEXPROCESSING 0x00000020L
#define D3DCREATE_HARDWARE_VERTEXPROCESSING 0x00000040L
#define D3DCREATE_FPU_PRESERVE 0x00000002L
#define D3DCREATE_MULTITHREADED 0x00000004L
#define D3DPRASTERCAPS_WFOG 0x00002000L
#define D3DPRASTERCAPS_ZTEST 0x00000010L
#define D3DVS_VERSION(major, minor) (0xFFFE0000 | ((major) << 8) | (minor))

// Swap effects
typedef enum _D3DSWAPEFFECT {
    D3DSWAPEFFECT_DISCARD = 1,
    D3DSWAPEFFECT_FLIP = 2,
    D3DSWAPEFFECT_COPY = 3,
} D3DSWAPEFFECT;

// MultiSample types
typedef enum _D3DMULTISAMPLE_TYPE {
    D3DMULTISAMPLE_NONE = 0,
    D3DMULTISAMPLE_NONMASKABLE = 1,
    D3DMULTISAMPLE_2_SAMPLES = 2,
    D3DMULTISAMPLE_3_SAMPLES = 3,
    D3DMULTISAMPLE_4_SAMPLES = 4,
    D3DMULTISAMPLE_5_SAMPLES = 5,
    D3DMULTISAMPLE_6_SAMPLES = 6,
    D3DMULTISAMPLE_7_SAMPLES = 7,
    D3DMULTISAMPLE_8_SAMPLES = 8,
    D3DMULTISAMPLE_9_SAMPLES = 9,
    D3DMULTISAMPLE_10_SAMPLES = 10,
    D3DMULTISAMPLE_11_SAMPLES = 11,
    D3DMULTISAMPLE_12_SAMPLES = 12,
    D3DMULTISAMPLE_13_SAMPLES = 13,
    D3DMULTISAMPLE_14_SAMPLES = 14,
    D3DMULTISAMPLE_15_SAMPLES = 15,
    D3DMULTISAMPLE_16_SAMPLES = 16,
} D3DMULTISAMPLE_TYPE;

// Resource types
typedef enum _D3DRESOURCETYPE {
    D3DRTYPE_SURFACE = 1,
    D3DRTYPE_VOLUME = 2,
    D3DRTYPE_TEXTURE = 3,
    D3DRTYPE_VOLUMETEXTURE = 4,
    D3DRTYPE_CUBETEXTURE = 5,
    D3DRTYPE_VERTEXBUFFER = 6,
    D3DRTYPE_INDEXBUFFER = 7,
    D3DRTYPE_FORCE_DWORD = 0x7fffffff
} D3DRESOURCETYPE;

// Present parameters
struct D3DPRESENT_PARAMETERS {
    UINT BackBufferWidth;
    UINT BackBufferHeight;
    D3DFORMAT BackBufferFormat;
    UINT BackBufferCount;
    D3DMULTISAMPLE_TYPE MultiSampleType;
    DWORD MultiSampleQuality;
    D3DSWAPEFFECT SwapEffect;
    HWND hDeviceWindow;
    BOOL Windowed;
    BOOL EnableAutoDepthStencil;
    D3DFORMAT AutoDepthStencilFormat;
    DWORD Flags;
    UINT FullScreen_RefreshRateInHz;
    UINT PresentationInterval;
};

#define D3DPRESENT_INTERVAL_DEFAULT     0x00000000L
#define D3DPRESENT_INTERVAL_ONE         0x00000001L
#define D3DPRESENT_INTERVAL_TWO         0x00000002L
#define D3DPRESENT_INTERVAL_THREE       0x00000004L
#define D3DPRESENT_INTERVAL_FOUR        0x00000008L
#define D3DPRESENT_INTERVAL_IMMEDIATE   0x80000000L

#define D3DPRESENT_RATE_DEFAULT         0x00000000L

// Z/Enable constants
#define D3DZB_FALSE 0
#define D3DZB_TRUE 1
#define D3DZB_USEW 2

// Gamma Ramp
typedef struct _D3DGAMMARAMP {
    WORD red[256];
    WORD green[256];
    WORD blue[256];
} D3DGAMMARAMP;

#define D3DSGR_NO_CALIBRATION 0x00000000L
#define D3DSGR_CALIBRATE      0x00000001L

typedef struct _D3DDISPLAYMODE {
    UINT Width;
    UINT Height;
    UINT RefreshRate;
    D3DFORMAT Format;
} D3DDISPLAYMODE;



#define MAX_DEVICE_IDENTIFIER_STRING 512
#ifndef _GUID_DEFINED_
#define _GUID_DEFINED_
typedef struct _GUID {
    unsigned long  Data1;
    unsigned short Data2;
    unsigned short Data3;
    unsigned char  Data4[8];
} GUID;
#endif

typedef struct _D3DADAPTER_IDENTIFIER9 {
    char Driver[MAX_DEVICE_IDENTIFIER_STRING];
    char Description[MAX_DEVICE_IDENTIFIER_STRING];
    char DeviceName[32];
    LARGE_INTEGER DriverVersion;
    DWORD VendorId;
    DWORD DeviceId;
    DWORD SubSysId;
    DWORD Revision;
    GUID DeviceIdentifier;
    DWORD WHQLLevel;
} D3DADAPTER_IDENTIFIER9;

#define D3DCAPS2_FULLSCREENGAMMA        0x00020000L
#define D3DPRASTERCAPS_WBUFFER          0x00020000L

// Pool types
enum D3DPOOL {
    D3DPOOL_DEFAULT = 0,
    D3DPOOL_MANAGED = 1,
    D3DPOOL_SYSTEMMEM = 2,
    D3DPOOL_SCRATCH = 3,
};

typedef struct _D3DSURFACE_DESC {
    D3DFORMAT           Format;
    D3DRESOURCETYPE     Type;
    DWORD               Usage;
    D3DPOOL             Pool;
    D3DMULTISAMPLE_TYPE MultiSampleType;
    DWORD               MultiSampleQuality;
    UINT                Width;
    UINT                Height;
} D3DSURFACE_DESC;

// Usage flags
#define D3DUSAGE_RENDERTARGET  0x00000001L
#define D3DUSAGE_DEPTHSTENCIL  0x00000002L
#define D3DUSAGE_WRITEONLY     0x00000008L
#define D3DUSAGE_SOFTWAREPROCESSING 0x00000010L
#define D3DUSAGE_DYNAMIC       0x00000200L

// Locked rect structure
struct D3DLOCKED_RECT {
    INT Pitch;
    void* pBits;
};

// Color value structure
struct _D3DCOLORVALUE {
    float r;
    float g;
    float b;
    float a;
};
typedef struct _D3DCOLORVALUE D3DCOLORVALUE;

// Device caps structure (minimal for FFP fallback)
struct D3DCAPS9 {
    DWORD DevCaps;
    DWORD VertexShaderVersion;
    DWORD PixelShaderVersion;
    DWORD MaxPrimitiveCount;
    DWORD NumSimultaneousRTs;
    DWORD MaxStreams;
    DWORD MaxUserClipPlanes;
    DWORD RasterCaps;
    DWORD Caps2;
    DWORD MaxVertexShaderConst;
};

#define D3DDECLUSAGE_POSITIONT 9
#define D3DFMT_R32F 114
#define D3DRS_BLENDOP (D3DRENDERSTATETYPE)171
#define D3DBLENDOP_ADD 1
#define D3DZB_FALSE 0
#define D3DZB_TRUE 1

// Light types
enum D3DLIGHTTYPE {
    D3DLIGHT_POINT = 1,
    D3DLIGHT_SPOT = 2,
    D3DLIGHT_DIRECTIONAL = 3,
};

// Light structure
struct D3DLIGHT9 {
    D3DLIGHTTYPE Type;
    D3DCOLORVALUE Diffuse;
    D3DCOLORVALUE Specular;
    D3DCOLORVALUE Ambient;
    D3DVECTOR Position;
    D3DVECTOR Direction;
    float Range;
    float Falloff;
    float Attenuation0;
    float Attenuation1;
    float Attenuation2;
    float Theta;
    float Phi;
};

// Material structure
struct D3DMATERIAL9 {
    D3DCOLORVALUE Diffuse;
    D3DCOLORVALUE Ambient;
    D3DCOLORVALUE Specular;
    D3DCOLORVALUE Emissive;
    float Power;
};

// Forward declaration
struct IDirect3DSurface9;
class IDirect3DDevice9; // Forward declaration matches definition (class)

// Base Interface
struct IUnknown {
    virtual ~IUnknown() {}
    virtual ULONG AddRef() = 0;
    virtual ULONG Release() = 0;
};

// Base Resource Interface
struct IDirect3DResource9 : public IUnknown {
    virtual ~IDirect3DResource9() {}
    // Release is inherited from IUnknown
    virtual HRESULT GetDevice(class IDirect3DDevice9** ppDevice) { return D3D_OK; }
};

// Surface Interface
struct IDirect3DSurface9 : public IDirect3DResource9 {
    virtual HRESULT LockRect(D3DLOCKED_RECT* pLockedRect, const RECT* pRect, DWORD Flags) = 0;
    virtual HRESULT UnlockRect() = 0;
    virtual HRESULT GetDesc(D3DSURFACE_DESC* pDesc) = 0;
};

// Texture Interface
struct IDirect3DBaseTexture9 : public IDirect3DResource9 {
    virtual DWORD SetLOD(DWORD LODNew) { return 0; }
    virtual DWORD GetLOD() { return 0; }
    virtual DWORD GetLevelCount() { return 1; }
    virtual D3DRESOURCETYPE GetType() { return D3DRTYPE_TEXTURE; }
};

struct IDirect3DTexture9 : public IDirect3DBaseTexture9 {
    virtual HRESULT GetLevelDesc(UINT Level, D3DSURFACE_DESC* pDesc) { return D3D_OK; }
    virtual HRESULT GetSurfaceLevel(UINT Level, IDirect3DSurface9** ppSurfaceLevel) = 0;
    virtual HRESULT LockRect(UINT Level, D3DLOCKED_RECT* pLockedRect, const void* pRect, DWORD Flags) = 0;
    virtual HRESULT UnlockRect(UINT Level) = 0;
    virtual HRESULT AddDirtyRect(const void* pDirtyRect) { return D3D_OK; }
};

// Viewport structure
struct D3DVIEWPORT9 {
    DWORD X;
    DWORD Y;
    DWORD Width;
    DWORD Height;
    float MinZ;
    float MaxZ;
};

// Sampler state types
enum D3DSAMPLERSTATETYPE {
    D3DSAMP_ADDRESSU = 1,
    D3DSAMP_ADDRESSV = 2,
    D3DSAMP_ADDRESSW = 3,
    D3DSAMP_MINFILTER = 5,
    D3DSAMP_MAGFILTER = 6,
    D3DSAMP_MIPFILTER = 7,
    D3DSAMP_MIPMAPLODBIAS = 8,
};

// Texture filter types
enum D3DTEXTUREFILTERTYPE {
    D3DTEXF_NONE = 0,
    D3DTEXF_POINT = 1,
    D3DTEXF_LINEAR = 2,
};

enum D3DDECLTYPE {
    D3DDECLTYPE_FLOAT1 = 0,
    D3DDECLTYPE_FLOAT2 = 1,
    D3DDECLTYPE_FLOAT3 = 2,
    D3DDECLTYPE_FLOAT4 = 3,
    D3DDECLTYPE_D3DCOLOR = 4,
    D3DDECLTYPE_UBYTE4 = 5,
    D3DDECLTYPE_SHORT2 = 6,
    D3DDECLTYPE_SHORT4 = 7,
    D3DDECLTYPE_UNUSED = 17,
};

enum D3DDECLMETHOD {
    D3DDECLMETHOD_DEFAULT = 0,
};

enum D3DDECLUSAGE {
    D3DDECLUSAGE_POSITION = 0,
    D3DDECLUSAGE_BLENDWEIGHT = 1,
    D3DDECLUSAGE_BLENDINDICES = 2,
    D3DDECLUSAGE_NORMAL = 3,
    D3DDECLUSAGE_PSIZE = 4,
    D3DDECLUSAGE_TEXCOORD = 5,
    D3DDECLUSAGE_TANGENT = 6,
    D3DDECLUSAGE_BINORMAL = 7,
};

#define D3DDECL_END() {0xFF,0,D3DDECLTYPE_UNUSED,0,0,0}

struct D3DVERTEXELEMENT9 {
    WORD Stream;
    WORD Offset;
    uint8_t Type;
    uint8_t Method;
    uint8_t Usage;
    uint8_t UsageIndex;
};

// Texture address modes
enum D3DTEXTUREADDRESS {
    D3DTADDRESS_WRAP = 1,
    D3DTADDRESS_CLAMP = 3,
};

// --- Interfaces ---

class IDirect3DIndexBuffer9 : public IUnknown {
public:
    virtual HRESULT Lock(UINT OffsetToLock, UINT SizeToLock, void** ppbData, DWORD Flags) = 0;
    virtual HRESULT Unlock() = 0;
};

class IDirect3DVertexBuffer9 : public IUnknown {
public:
    virtual HRESULT Lock(UINT OffsetToLock, UINT SizeToLock, void** ppbData, DWORD Flags) = 0;
    virtual HRESULT Unlock() = 0;
};

enum D3DQUERYTYPE {
    D3DQUERYTYPE_VCACHE = 4,
    D3DQUERYTYPE_ResourceManager = 5,
    D3DQUERYTYPE_VERTEXSTATS = 6,
    D3DQUERYTYPE_EVENT = 8,
    D3DQUERYTYPE_OCCLUSION = 9,
    D3DQUERYTYPE_TIMESTAMP = 10,
    D3DQUERYTYPE_TIMESTAMPDISJOINT = 11,
    D3DQUERYTYPE_TIMESTAMPFREQ = 12,
    D3DQUERYTYPE_PIPELINETIMINGS = 13,
    D3DQUERYTYPE_INTERFACETIMINGS = 14,
    D3DQUERYTYPE_VERTEXTIMINGS = 15,
    D3DQUERYTYPE_PIXELTIMINGS = 16,
    D3DQUERYTYPE_BANDWIDTHTIMINGS = 17,
    D3DQUERYTYPE_CACHEUTILIZATION = 18,
};

#define D3DISSUE_END (1 << 0)
#define D3DISSUE_BEGIN (1 << 1)
#define D3DGETDATA_FLUSH (1 << 0)

class IDirect3DQuery9 : public IUnknown {
public:
    virtual HRESULT GetData(void* pData, DWORD dwSize, DWORD dwGetDataFlags) = 0;
    virtual HRESULT Issue(DWORD dwIssueFlags) = 0;
};

class IDirect3DVertexShader9 : public IUnknown {};
class IDirect3DVertexDeclaration9 : public IUnknown {};
class IDirect3DPixelShader9 : public IUnknown {};

typedef enum _D3DSTATEBLOCKTYPE {
    D3DSBT_ALL = 1,
    D3DSBT_PIXELSTATE = 2,
    D3DSBT_VERTEXSTATE = 3,
    D3DSBT_FORCE_DWORD = 0x7fffffff
} D3DSTATEBLOCKTYPE;

class IDirect3DStateBlock9 : public IUnknown {
public:
    virtual HRESULT Capture() = 0;
    virtual HRESULT Apply() = 0;
};

class IDirect3DDevice9 : public IUnknown {
public:
    virtual HRESULT Clear(DWORD Count, const void* pRects, DWORD Flags, D3DCOLOR Color, FLOAT Z, DWORD Stencil) = 0;
    virtual HRESULT BeginScene() = 0;
    virtual HRESULT EndScene() = 0;
    virtual HRESULT Present(const void* pSourceRect, const void* pDestRect, HWND hDestWindowOverride, const void* pDirtyRegion) = 0;
    virtual HRESULT SetFVF(DWORD FVF) = 0;
    virtual HRESULT DrawPrimitiveUP(D3DPRIMITIVETYPE PrimitiveType, UINT PrimitiveCount, const void* pVertexStreamZeroData, UINT VertexStreamZeroStride) = 0;
    virtual HRESULT DrawIndexedPrimitiveUP(D3DPRIMITIVETYPE PrimitiveType, UINT MinVertexIndex, UINT NumVertices, UINT PrimitiveCount, const void* pIndexData, D3DFORMAT IndexDataFormat, const void* pVertexStreamZeroData, UINT VertexStreamZeroStride) = 0;
    virtual HRESULT CreateTexture(UINT Width, UINT Height, UINT Levels, DWORD Usage,
                                 D3DFORMAT Format, D3DPOOL Pool,
                                 IDirect3DTexture9** ppTexture, void* pSharedHandle) = 0;
                                 
    virtual HRESULT CreateDepthStencilSurface(UINT Width, UINT Height, D3DFORMAT Format,
                                             D3DMULTISAMPLE_TYPE MultiSample, DWORD MultisampleQuality,
                                             BOOL Discard, IDirect3DSurface9** ppSurface, void* pSharedHandle) = 0;
                                             
    virtual HRESULT SetClipPlane(DWORD Index, const float* pPlane) = 0;
    virtual HRESULT GetClipPlane(DWORD Index, float* pPlane) = 0;
    virtual HRESULT SetTexture(DWORD Stage, IDirect3DTexture9* pTexture) = 0;
    virtual HRESULT CreateIndexBuffer(UINT Length, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool, IDirect3DIndexBuffer9** ppIndexBuffer, void* pSharedHandle) = 0;
    virtual HRESULT CreateVertexBuffer(UINT Length, DWORD Usage, DWORD FVF, D3DPOOL Pool, IDirect3DVertexBuffer9** ppVertexBuffer, void* pSharedHandle) = 0;
    virtual HRESULT SetIndices(IDirect3DIndexBuffer9* pIndexData) = 0;
    virtual HRESULT SetStreamSource(UINT StreamNumber, IDirect3DVertexBuffer9* pStreamData, UINT OffsetInBytes, UINT Stride) = 0;
    virtual HRESULT SetTransform(D3DTRANSFORMSTATETYPE State, const D3DMATRIX* pMatrix) = 0;
    virtual HRESULT SetRenderState(D3DRENDERSTATETYPE State, DWORD Value) = 0;
    virtual HRESULT GetRenderState(D3DRENDERSTATETYPE State, DWORD* pValue) = 0;
    virtual HRESULT GetDeviceCaps(D3DCAPS9* pCaps) = 0;
    virtual HRESULT SetLight(DWORD Index, const D3DLIGHT9* pLight) = 0;
    virtual HRESULT GetLight(DWORD Index, D3DLIGHT9* pLight) = 0;
    virtual HRESULT LightEnable(DWORD Index, BOOL Enable) = 0;
    virtual HRESULT GetLightEnable(DWORD Index, BOOL* pEnable) = 0;
    virtual HRESULT SetMaterial(const D3DMATERIAL9* pMaterial) = 0;
    virtual HRESULT SetTextureStageState(DWORD Stage, D3DTEXTURESTAGESTATETYPE Type, DWORD Value) = 0;
    virtual HRESULT SetSamplerState(DWORD Sampler, D3DSAMPLERSTATETYPE Type, DWORD Value) = 0;
    virtual HRESULT GetViewport(D3DVIEWPORT9* pViewport) = 0;
    virtual HRESULT SetViewport(const D3DVIEWPORT9* pViewport) = 0;
    virtual HRESULT GetTransform(D3DTRANSFORMSTATETYPE State, D3DMATRIX* pMatrix) = 0;
    virtual HRESULT SetScissorRect(const RECT* pRect) = 0;
    virtual HRESULT GetRenderTarget(DWORD Index, IDirect3DSurface9** ppRenderTarget) = 0;
    virtual HRESULT SetRenderTarget(DWORD Index, IDirect3DSurface9* pRenderTarget) = 0;
    virtual HRESULT CreateOffscreenPlainSurface(UINT Width, UINT Height, D3DFORMAT Format, D3DPOOL Pool, IDirect3DSurface9** ppSurface, void* pSharedHandle) = 0;
    virtual HRESULT GetRenderTargetData(IDirect3DSurface9* pRenderTarget, IDirect3DSurface9* pDestSurface) = 0;
    virtual HRESULT GetFrontBufferData(UINT iSwapChain, IDirect3DSurface9* pDestSurface) = 0;
    virtual HRESULT GetDepthStencilSurface(IDirect3DSurface9** ppZStencilSurface) = 0;
    virtual HRESULT SetDepthStencilSurface(IDirect3DSurface9* pZStencilSurface) = 0;
    virtual HRESULT GetVertexShader(IDirect3DVertexShader9** ppShader) = 0;
    virtual HRESULT GetPixelShader(IDirect3DPixelShader9** ppShader) = 0;
    virtual HRESULT CreateVertexShader(const DWORD* pFunction, IDirect3DVertexShader9** ppShader) = 0;
    virtual HRESULT CreatePixelShader(const DWORD* pFunction, IDirect3DPixelShader9** ppShader) = 0;
    virtual HRESULT CreateVertexDeclaration(const void* pVertexElements, IDirect3DVertexDeclaration9** ppDecl) = 0;
    virtual void SetGammaRamp(UINT iSwapChain, DWORD Flags, const D3DGAMMARAMP* pRamp) = 0;
    virtual void GetGammaRamp(UINT iSwapChain, D3DGAMMARAMP* pRamp) = 0;
    virtual HRESULT SetVertexDeclaration(void* pDecl) = 0;
    virtual HRESULT SetVertexShader(void* pShader) = 0;
    virtual HRESULT SetPixelShader(void* pShader) = 0;
    virtual HRESULT SetVertexShaderConstantF(UINT StartRegister, const float* pConstantData, UINT Vector4fCount) = 0;
    virtual HRESULT SetPixelShaderConstantF(UINT StartRegister, const float* pConstantData, UINT Vector4fCount) = 0;
    virtual HRESULT DrawIndexedPrimitive(D3DPRIMITIVETYPE PrimitiveType, INT BaseVertexIndex, UINT MinVertexIndex, UINT NumVertices, UINT startIndex, UINT primCount) = 0;
    virtual HRESULT DrawPrimitive(D3DPRIMITIVETYPE PrimitiveType, UINT StartVertex, UINT PrimitiveCount) = 0;
    virtual HRESULT CreateQuery(D3DQUERYTYPE Type, IDirect3DQuery9** ppQuery) = 0;
    virtual HRESULT StretchRect(IDirect3DSurface9* pSourceSurface, const void* pSourceRect, IDirect3DSurface9* pDestSurface, const void* pDestRect, D3DTEXTUREFILTERTYPE Filter) = 0;
    virtual UINT GetAvailableTextureMem() = 0;
    virtual HRESULT ValidateDevice(DWORD* pNumPasses) = 0;
    virtual HRESULT TestCooperativeLevel() = 0;
    virtual HRESULT Reset(D3DPRESENT_PARAMETERS* pPresentationParameters) = 0;
    virtual HRESULT GetDisplayMode(UINT iSwapChain, D3DDISPLAYMODE* pMode) = 0;
    virtual HRESULT CreateStateBlock(D3DSTATEBLOCKTYPE Type, IDirect3DStateBlock9** ppSB) = 0;
};



class IDirect3D9 : public IUnknown {
public:
    virtual HRESULT CreateDevice(UINT Adapter, D3DDEVTYPE DeviceType, HWND hFocusWindow, DWORD BehaviorFlags, D3DPRESENT_PARAMETERS* pPresentationParameters, IDirect3DDevice9** ppReturnedDeviceInterface) = 0;
    virtual UINT GetAdapterCount() = 0;
    virtual HRESULT GetAdapterIdentifier(UINT Adapter, DWORD Flags, D3DADAPTER_IDENTIFIER9* pIdentifier) = 0;
    virtual HRESULT GetAdapterDisplayMode(UINT Adapter, D3DDISPLAYMODE* pMode) = 0;
    virtual HRESULT CheckDeviceFormat(UINT Adapter, D3DDEVTYPE DeviceType, D3DFORMAT AdapterFormat, DWORD Usage, D3DRESOURCETYPE RType, D3DFORMAT CheckFormat) = 0;
    virtual HRESULT CheckDepthStencilMatch(UINT Adapter, D3DDEVTYPE DeviceType, D3DFORMAT AdapterFormat, D3DFORMAT RenderTargetFormat, D3DFORMAT DepthStencilFormat) = 0;
    virtual HRESULT GetDeviceCaps(UINT Adapter, D3DDEVTYPE DeviceType, D3DCAPS9* pCaps) = 0;
    virtual UINT GetAdapterModeCount(UINT Adapter, D3DFORMAT Format) = 0;
    virtual HRESULT EnumAdapterModes(UINT Adapter, D3DFORMAT Format, UINT Mode, D3DDISPLAYMODE* pMode) = 0;
};

// Pointer types
typedef IDirect3D9* LPDIRECT3D9;
typedef IDirect3DDevice9* LPDIRECT3DDEVICE9;
typedef IDirect3DTexture9* LPDIRECT3DTEXTURE9;
typedef IDirect3DSurface9* LPDIRECT3DSURFACE9;
typedef IDirect3DIndexBuffer9* LPDIRECT3DINDEXBUFFER9;
typedef IDirect3DVertexBuffer9* LPDIRECT3DVERTEXBUFFER9;
typedef IDirect3DVertexShader9* LPDIRECT3DVERTEXSHADER9;
typedef IDirect3DVertexDeclaration9* LPDIRECT3DVERTEXDECLARATION9;
typedef IDirect3DVertexDeclaration9* LPDIRECT3DVERTEXDECLARATION9;
typedef IDirect3DPixelShader9* LPDIRECT3DPIXELSHADER9;
typedef IDirect3DQuery9* LPDIRECT3DQUERY9;

// Factory function
extern "C" IDirect3D9* Direct3DCreate9(UINT SDKVersion);
