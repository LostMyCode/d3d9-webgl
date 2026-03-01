#pragma once
#include "d3d9.h"
#ifndef __D3DX9_H__
#define __D3DX9_H__

#include "d3dx9math.h"

// ID3DXLine stub
struct ID3DXLine {
    virtual HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObj) = 0;
    virtual ULONG STDMETHODCALLTYPE AddRef() = 0;
    virtual ULONG STDMETHODCALLTYPE Release() = 0;
    virtual HRESULT STDMETHODCALLTYPE Begin() = 0;
    virtual HRESULT STDMETHODCALLTYPE Draw(const D3DXVECTOR2 *pVertexList, DWORD dwVertexListCount, D3DCOLOR Color) = 0;
    virtual HRESULT STDMETHODCALLTYPE DrawTransform(const D3DXVECTOR3 *pVertexList, DWORD dwVertexListCount, const D3DXMATRIX *pMatrix, D3DCOLOR Color) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetWidth(float fWidth) = 0;
    virtual float STDMETHODCALLTYPE GetWidth() = 0;
    virtual HRESULT STDMETHODCALLTYPE SetAntialias(BOOL bAntialias) = 0;
    virtual BOOL STDMETHODCALLTYPE GetAntialias() = 0;
    virtual HRESULT STDMETHODCALLTYPE SetGLLines(BOOL bGLLines) = 0;
    virtual BOOL STDMETHODCALLTYPE GetGLLines() = 0;
    virtual HRESULT STDMETHODCALLTYPE End() = 0;
    virtual HRESULT STDMETHODCALLTYPE OnLostDevice() = 0;
    virtual HRESULT STDMETHODCALLTYPE OnResetDevice() = 0;
};

// ID3DXFont stub
struct ID3DXFont {
    virtual HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObj) = 0;
    virtual ULONG STDMETHODCALLTYPE AddRef() = 0;
    virtual ULONG STDMETHODCALLTYPE Release() = 0;
    virtual HRESULT STDMETHODCALLTYPE DrawTextA(void* pSprite, const char* pString, int Count, RECT* pRect, DWORD Format, D3DCOLOR Color) = 0;
    virtual HRESULT STDMETHODCALLTYPE DrawTextW(void* pSprite, const wchar_t* pString, int Count, RECT* pRect, DWORD Format, D3DCOLOR Color) = 0;
    virtual HRESULT STDMETHODCALLTYPE OnLostDevice() = 0;
    virtual HRESULT STDMETHODCALLTYPE OnResetDevice() = 0;
};

inline HRESULT D3DXCreateFontIndirectA(LPDIRECT3DDEVICE9 pDevice, const void* pDesc, ID3DXFont** ppFont) { return E_FAIL; }
inline HRESULT D3DXCreateFontIndirectW(LPDIRECT3DDEVICE9 pDevice, const void* pDesc, ID3DXFont** ppFont) { return E_FAIL; }

#endif

// D3DX9 stub
