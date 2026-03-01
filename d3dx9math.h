#ifndef _D3DX9MATH_H_
#define _D3DX9MATH_H_

#include "d3d9.h"
#include <math.h>

// D3DX Math types stub
typedef struct D3DXVECTOR2 : public D3DVECTOR {
    float x, y;
    D3DXVECTOR2() {}
    D3DXVECTOR2(const float *pf) {}
    D3DXVECTOR2(float x, float y) : x(x), y(y) {}
    operator float* () { return (float *) &x; }
    operator const float* () const { return (const float *) &x; }
} D3DXVECTOR2, *LPD3DXVECTOR2;

typedef struct D3DXVECTOR3 : public D3DVECTOR {
    D3DXVECTOR3() {}
    D3DXVECTOR3(const float *pf) {}
    D3DXVECTOR3(const D3DVECTOR& v) { x = v.x; y = v.y; z = v.z; }
    D3DXVECTOR3(float x, float y, float z) { this->x = x; this->y = y; this->z = z; }
    operator float* () { return (float *) &x; }
    operator const float* () const { return (const float *) &x; }
    D3DXVECTOR3& operator += (const D3DXVECTOR3& v) { x+=v.x; y+=v.y; z+=v.z; return *this; }
    D3DXVECTOR3& operator -= (const D3DXVECTOR3& v) { x-=v.x; y-=v.y; z-=v.z; return *this; }
} D3DXVECTOR3, *LPD3DXVECTOR3;

typedef struct D3DXVECTOR4 {
    float x, y, z, w;
    D3DXVECTOR4() {}
    D3DXVECTOR4(const float *pf) {}
    D3DXVECTOR4(float x, float y, float z, float w) : x(x), y(y), z(z), w(w) {}
    operator float* () { return (float *) &x; }
    operator const float* () const { return (const float *) &x; }
} D3DXVECTOR4, *LPD3DXVECTOR4;

typedef struct D3DXMATRIX : public D3DMATRIX {
    D3DXMATRIX() {}
    D3DXMATRIX(const float *pf) {}
    // Need more operators?
    float& operator () (UINT i, UINT j) { return m[i][j]; }
    const float& operator () (UINT i, UINT j) const { return m[i][j]; }
    operator float* () { return (float *) &m[0][0]; }
    operator const float* () const { return (const float *) &m[0][0]; }
} D3DXMATRIX, *LPD3DXMATRIX;

typedef struct D3DXQUATERNION {
    float x, y, z, w;
} D3DXQUATERNION, *LPD3DXQUATERNION;

typedef struct D3DXPLANE {
    float a, b, c, d;
} D3DXPLANE, *LPD3DXPLANE;

typedef struct D3DXCOLOR {
    float r, g, b, a;
    D3DXCOLOR() {}
    D3DXCOLOR(DWORD dw) {
        r = ((dw >> 16) & 0xff) / 255.0f;
        g = ((dw >> 8) & 0xff) / 255.0f;
        b = (dw & 0xff) / 255.0f;
        a = ((dw >> 24) & 0xff) / 255.0f;
    }
    D3DXCOLOR(float r, float g, float b, float a) : r(r), g(g), b(b), a(a) {}
    operator DWORD () const {
        return ((DWORD)(a * 255.0f) << 24) | ((DWORD)(r * 255.0f) << 16) | ((DWORD)(g * 255.0f) << 8) | (DWORD)(b * 255.0f);
    }
} D3DXCOLOR, *LPD3DXCOLOR;

// Functions stub
inline D3DXMATRIX* D3DXMatrixIdentity(D3DXMATRIX* pOut) {
    for(int i=0; i<4; i++) for(int j=0; j<4; j++) pOut->m[i][j] = (i==j ? 1.0f : 0.0f);
    return pOut;
}

inline D3DXVECTOR3* D3DXVec3Normalize(D3DXVECTOR3 *pOut, const D3DXVECTOR3 *pV) {
    float mag = sqrt(pV->x*pV->x + pV->y*pV->y + pV->z*pV->z);
    if (mag > 0.00001f) {
        pOut->x = pV->x / mag;
        pOut->y = pV->y / mag;
        pOut->z = pV->z / mag;
    } else {
        *pOut = *pV;
    }
    return pOut;
}

inline D3DXVECTOR3* D3DXVec3Cross(D3DXVECTOR3 *pOut, const D3DXVECTOR3 *pV1, const D3DXVECTOR3 *pV2) {
    D3DXVECTOR3 v;
    v.x = pV1->y * pV2->z - pV1->z * pV2->y;
    v.y = pV1->z * pV2->x - pV1->x * pV2->z;
    v.z = pV1->x * pV2->y - pV1->y * pV2->x;
    *pOut = v;
    return pOut;
}

inline float D3DXVec3Length(const D3DXVECTOR3 *pV) {
    return sqrt(pV->x*pV->x + pV->y*pV->y + pV->z*pV->z);
}

inline float D3DXVec3Dot(const D3DXVECTOR3 *pV1, const D3DXVECTOR3 *pV2) {
    return pV1->x * pV2->x + pV1->y * pV2->y + pV1->z * pV2->z;
}

inline D3DXMATRIX* D3DXMatrixMultiply(D3DXMATRIX *pOut, const D3DXMATRIX *pM1, const D3DXMATRIX *pM2) {
    D3DXMATRIX out;
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            out.m[i][j] = pM1->m[i][0] * pM2->m[0][j] + pM1->m[i][1] * pM2->m[1][j] + pM1->m[i][2] * pM2->m[2][j] + pM1->m[i][3] * pM2->m[3][j];
        }
    }
    *pOut = out;
    return pOut;
}

inline D3DXMATRIX* D3DXMatrixTranspose(D3DXMATRIX *pOut, const D3DXMATRIX *pM) {
    D3DXMATRIX out;
    for (int i = 0; i < 4; i++) for (int j = 0; j < 4; j++) out.m[i][j] = pM->m[j][i];
    *pOut = out;
    return pOut;
}

inline D3DXMATRIX* D3DXMatrixInverse(D3DXMATRIX *pOut, float *pDeterminant, const D3DXMATRIX *pM) {
    // Stub: return identity or something for now? 
    // Implementing 4x4 inverse is long.
    D3DXMatrixIdentity(pOut);
    return pOut;
}

inline D3DXVECTOR3* D3DXVec3TransformCoord(D3DXVECTOR3 *pOut, const D3DXVECTOR3 *pV, const D3DXMATRIX *pM) {
    float x = pV->x * pM->m[0][0] + pV->y * pM->m[1][0] + pV->z * pM->m[2][0] + pM->m[3][0];
    float y = pV->x * pM->m[0][1] + pV->y * pM->m[1][1] + pV->z * pM->m[2][1] + pM->m[3][1];
    float z = pV->x * pM->m[0][2] + pV->y * pM->m[1][2] + pV->z * pM->m[2][2] + pM->m[3][2];
    float w = pV->x * pM->m[0][3] + pV->y * pM->m[1][3] + pV->z * pM->m[2][3] + pM->m[3][3];
    if (w != 0.0f) {
        pOut->x = x / w;
        pOut->y = y / w;
        pOut->z = z / w;
    } else {
        pOut->x = x; pOut->y = y; pOut->z = z;
    }
    return pOut;
}

inline D3DXQUATERNION* D3DXQuaternionRotationAxis(D3DXQUATERNION *pOut, const D3DXVECTOR3 *pV, float Angle) {
    float s = sin(Angle / 2.0f);
    pOut->x = pV->x * s;
    pOut->y = pV->y * s;
    pOut->z = pV->z * s;
    pOut->w = cos(Angle / 2.0f);
    return pOut;
}

inline D3DXQUATERNION* D3DXQuaternionIdentity(D3DXQUATERNION *pOut) {
    pOut->x = pOut->y = pOut->z = 0.0f; pOut->w = 1.0f;
    return pOut;
}

inline D3DXMATRIX* D3DXMatrixRotationQuaternion(D3DXMATRIX *pOut, const D3DXQUATERNION *pQ) {
    D3DXMatrixIdentity(pOut);
    float xx = pQ->x * pQ->x; float yy = pQ->y * pQ->y; float zz = pQ->z * pQ->z;
    float xy = pQ->x * pQ->y; float xz = pQ->x * pQ->z; float yz = pQ->y * pQ->z;
    float wx = pQ->w * pQ->x; float wy = pQ->w * pQ->y; float wz = pQ->w * pQ->z;
    pOut->m[0][0] = 1.0f - 2.0f * (yy + zz); pOut->m[0][1] = 2.0f * (xy + wz); pOut->m[0][2] = 2.0f * (xz - wy);
    pOut->m[1][0] = 2.0f * (xy - wz); pOut->m[1][1] = 1.0f - 2.0f * (xx + zz); pOut->m[1][2] = 2.0f * (yz + wx);
    pOut->m[2][0] = 2.0f * (xz + wy); pOut->m[2][1] = 2.0f * (yz - wx); pOut->m[2][2] = 1.0f - 2.0f * (xx + yy);
    return pOut;
}

inline D3DXMATRIX* D3DXMatrixPerspectiveFovLH(D3DXMATRIX *pOut, float fovy, float Aspect, float zn, float zf) {
    D3DXMatrixIdentity(pOut);
    float h = 1.0f / tan(fovy / 2.0f);
    float w = h / Aspect;
    pOut->m[0][0] = w;
    pOut->m[1][1] = h;
    pOut->m[2][2] = zf / (zf - zn);
    pOut->m[2][3] = 1.0f;
    pOut->m[3][2] = -zn * zf / (zf - zn);
    pOut->m[3][3] = 0.0f;
    return pOut;
}

inline D3DXMATRIX* D3DXMatrixLookAtLH(D3DXMATRIX *pOut, const D3DXVECTOR3 *pEye, const D3DXVECTOR3 *pAt, const D3DXVECTOR3 *pUp) {
    D3DXVECTOR3 diff(pAt->x - pEye->x, pAt->y - pEye->y, pAt->z - pEye->z);
    D3DXVECTOR3 zaxis; D3DXVec3Normalize(&zaxis, &diff);
    D3DXVECTOR3 xaxis; D3DXVec3Cross(&xaxis, pUp, &zaxis); D3DXVec3Normalize(&xaxis, &xaxis);
    D3DXVECTOR3 yaxis; D3DXVec3Cross(&yaxis, &zaxis, &xaxis);
    D3DXMatrixIdentity(pOut);
    pOut->m[0][0] = xaxis.x; pOut->m[1][0] = xaxis.y; pOut->m[2][0] = xaxis.z;
    pOut->m[0][1] = yaxis.x; pOut->m[1][1] = yaxis.y; pOut->m[2][1] = yaxis.z;
    pOut->m[0][2] = zaxis.x; pOut->m[1][2] = zaxis.y; pOut->m[2][2] = zaxis.z;
    pOut->m[3][0] = -D3DXVec3Dot(&xaxis, pEye);
    pOut->m[3][1] = -D3DXVec3Dot(&yaxis, pEye);
    pOut->m[3][2] = -D3DXVec3Dot(&zaxis, pEye);
    return pOut;
}

#endif
