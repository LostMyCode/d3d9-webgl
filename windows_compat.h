#pragma once

// Windows API compatibility stubs for Emscripten / non-Windows builds.
//
// This header provides minimal definitions for Windows types, constants, and
// API functions that are commonly referenced when porting Windows (D3D9) code
// to Emscripten. On native Windows (_WIN32), <windows.h> supplies all of this
// and this header is not included.
//
// Included automatically by d3d9.h on non-Windows targets.

#ifndef _WIN32

#include <stdio.h>
#include <string.h>

// NOTE: Basic Windows types (DWORD, HWND, BOOL, HRESULT, LPCSTR, etc.) are
// defined in d3d9.h, which includes this header. Do not redefine them here.

// ---------------------------------------------------------------------------
// COM calling convention and types
// ---------------------------------------------------------------------------
#ifndef STDMETHODCALLTYPE
#define STDMETHODCALLTYPE
#endif

#ifndef _GUID_DEFINED_
#define _GUID_DEFINED_
typedef struct _GUID {
    uint32_t Data1;
    uint16_t Data2;
    uint16_t Data3;
    uint8_t  Data4[8];
} GUID;
typedef GUID IID;
typedef GUID REFIID;
#endif

// ---------------------------------------------------------------------------
// Window styles
// ---------------------------------------------------------------------------
#define WS_POPUP            0x80000000L
#define WS_CAPTION          0x00C00000L
#define WS_SYSMENU          0x00080000L
#define WS_THICKFRAME       0x00040000L
#define WS_MINIMIZEBOX      0x00020000L
#define WS_MAXIMIZEBOX      0x00010000L
#define WS_VISIBLE          0x10000000L
#define WS_CHILD            0x40000000L
#define WS_OVERLAPPED       0x00000000L
#define WS_OVERLAPPEDWINDOW (WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX)

// ---------------------------------------------------------------------------
// Virtual key codes
// ---------------------------------------------------------------------------
#define VK_ESCAPE           0x1B
#define VK_SPACE            0x20
#define VK_RETURN           0x0D
#define VK_TAB              0x09
#define VK_SHIFT            0x10
#define VK_CONTROL          0x11

// ---------------------------------------------------------------------------
// GDI handle types
// ---------------------------------------------------------------------------
typedef void* HDC;
typedef void* HBITMAP;
typedef void* HFONT;
typedef void* HGDIOBJ;
typedef void* HPEN;
typedef void* HBRUSH;

// ---------------------------------------------------------------------------
// BMP / DIB structures
// ---------------------------------------------------------------------------
#pragma pack(push, 1)
typedef struct tagBITMAPFILEHEADER {
    WORD  bfType;
    DWORD bfSize;
    WORD  bfReserved1;
    WORD  bfReserved2;
    DWORD bfOffBits;
} BITMAPFILEHEADER, *PBITMAPFILEHEADER;
#pragma pack(pop)

typedef struct tagBITMAPINFOHEADER {
    DWORD biSize;
    LONG  biWidth;
    LONG  biHeight;
    WORD  biPlanes;
    WORD  biBitCount;
    DWORD biCompression;
    DWORD biSizeImage;
    LONG  biXPelsPerMeter;
    LONG  biYPelsPerMeter;
    DWORD biClrUsed;
    DWORD biClrImportant;
} BITMAPINFOHEADER;

typedef struct tagRGBQUAD {
    BYTE rgbBlue;
    BYTE rgbGreen;
    BYTE rgbRed;
    BYTE rgbReserved;
} RGBQUAD;

typedef struct tagBITMAPINFO {
    BITMAPINFOHEADER bmiHeader;
    RGBQUAD          bmiColors[1];
} BITMAPINFO;

#define BI_RGB          0L
#define DIB_RGB_COLORS  0

// ---------------------------------------------------------------------------
// Font / GDI constants
// ---------------------------------------------------------------------------
#define FW_BOLD              700
#define FW_NORMAL            400
#define ANSI_CHARSET         0
#define DEFAULT_CHARSET      1
#define OUT_DEFAULT_PRECIS   0
#define CLIP_DEFAULT_PRECIS  0
#define DEFAULT_QUALITY      0
#define DEFAULT_PITCH        0
#define FF_DONTCARE          0
#define MM_TEXT              1
#define LOGPIXELSY           90

// ---------------------------------------------------------------------------
// LARGE_INTEGER
// ---------------------------------------------------------------------------
#ifndef _LARGE_INTEGER_DEFINED
#define _LARGE_INTEGER_DEFINED
typedef union _LARGE_INTEGER {
    struct { DWORD LowPart; LONG HighPart; };
    struct { DWORD LowPart; LONG HighPart; } u;
    long long QuadPart;
} LARGE_INTEGER;
#endif

// ---------------------------------------------------------------------------
// TCHAR / string macros (Windows CRT extensions)
// ---------------------------------------------------------------------------
#define _tcslen     strlen
#define _tcscpy     strcpy
#define _tcscmp     strcmp
#define _tcsncpy    strncpy
#define _tcsncmp    strncmp
#define _tprintf    printf
#define _stprintf   sprintf
#define _vsntprintf vsnprintf
#define _T(x)       x

#define _strnicmp   strncasecmp
#define _stricmp    strcasecmp

// ---------------------------------------------------------------------------
// Path constants
// ---------------------------------------------------------------------------
#ifndef _MAX_PATH
#define _MAX_PATH 260
#endif
#define _MAX_DRIVE 3
#define _MAX_DIR   256
#define _MAX_FNAME 256
#define _MAX_EXT   256

// ---------------------------------------------------------------------------
// errno_t
// ---------------------------------------------------------------------------
#ifndef _ERRNO_T_DEFINED
#define _ERRNO_T_DEFINED
typedef int errno_t;
#endif

// ---------------------------------------------------------------------------
// _splitpath_s
// ---------------------------------------------------------------------------
inline errno_t _splitpath_s(
    const char* path,
    char* drive, size_t driveNumberOfElements,
    char* dir,   size_t dirNumberOfElements,
    char* fname, size_t nameNumberOfElements,
    char* ext,   size_t extNumberOfElements)
{
    if (drive) *drive = 0;
    if (dir)   *dir   = 0;
    if (fname) *fname = 0;
    if (ext)   *ext   = 0;

    if (!path) return 0;

    const char* lastSlash     = strrchr(path, '/');
    const char* lastBackSlash = strrchr(path, '\\');
    const char* filenameStart = path;

    if (lastSlash || lastBackSlash) {
        if (lastSlash > lastBackSlash) filenameStart = lastSlash + 1;
        else                           filenameStart = lastBackSlash + 1;
    }

    if (dir && dirNumberOfElements > 0) {
        size_t dirLen = filenameStart - path;
        if (dirLen >= dirNumberOfElements) dirLen = dirNumberOfElements - 1;
        strncpy(dir, path, dirLen);
        dir[dirLen] = 0;
    }

    const char* lastDot = strrchr(filenameStart, '.');

    if (lastDot) {
        if (ext && extNumberOfElements > 0) {
            strncpy(ext, lastDot, extNumberOfElements - 1);
            ext[extNumberOfElements - 1] = 0;
        }
        if (fname && nameNumberOfElements > 0) {
            size_t len = lastDot - filenameStart;
            if (len >= nameNumberOfElements) len = nameNumberOfElements - 1;
            strncpy(fname, filenameStart, len);
            fname[len] = 0;
        }
    } else {
        if (fname && nameNumberOfElements > 0) {
            strncpy(fname, filenameStart, nameNumberOfElements - 1);
            fname[nameNumberOfElements - 1] = 0;
        }
    }
    return 0;
}

inline void _splitpath_s(const char* path, char* drive, char* dir, char* fname, char* ext) {
    _splitpath_s(path, drive, _MAX_DRIVE, dir, _MAX_DIR, fname, _MAX_FNAME, ext, _MAX_EXT);
}

// ---------------------------------------------------------------------------
// sscanf_s
// ---------------------------------------------------------------------------
#ifndef sscanf_s
#define sscanf_s sscanf
#endif

// ---------------------------------------------------------------------------
// Sleep
// ---------------------------------------------------------------------------
#ifndef Sleep
#define Sleep(x)
#endif

// ---------------------------------------------------------------------------
// Window management constants and stubs
// ---------------------------------------------------------------------------
#define SW_SHOW          5
#define SW_RESTORE       9
#define SW_SHOWMINIMIZED 2
#define SW_SHOWNORMAL    1

#define GWL_STYLE        (-16)
#define HWND_NOTOPMOST   ((HWND)-2)
#define SWP_SHOWWINDOW   0x0040
#define GW_HWNDNEXT      2

#define MB_OK            0
#define MB_YESNO         4
#define IDYES            6
#define IDNO             7

inline BOOL ShowWindow(HWND, int)                                          { return TRUE; }
inline BOOL UpdateWindow(HWND)                                             { return TRUE; }
inline LONG GetWindowLong(HWND, int)                                       { return 0; }
inline LONG SetWindowLong(HWND, int, LONG)                                 { return 0; }
inline BOOL SetWindowPos(HWND, HWND, int, int, int, int, UINT)             { return TRUE; }
inline BOOL AdjustWindowRect(RECT*, DWORD, BOOL)                           { return TRUE; }
inline int  ShowCursor(BOOL)                                               { return 0; }

inline int MessageBox(HWND, LPCSTR lpText, LPCSTR lpCaption, UINT) {
    printf("MessageBox: %s - %s\n", lpCaption ? lpCaption : "", lpText ? lpText : "");
    return IDYES;
}

inline HINSTANCE ShellExecute(HWND, LPCSTR, LPCSTR, LPCSTR, LPCSTR, INT) {
    return (HINSTANCE)33;
}

// ---------------------------------------------------------------------------
// Memory utilities
// ---------------------------------------------------------------------------
#define ZeroMemory(p, s) memset(p, 0, s)

#endif // !_WIN32
