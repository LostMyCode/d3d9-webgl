#include "d3d9.h"
#include <emscripten.h>
#include <emscripten/html5.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>

// Helper declaration from d3d9.cpp
IDirect3DTexture9* LoadTextureFromFile(IDirect3DDevice9* device, const char* filename);

// Global variables
IDirect3D9* g_d3d = nullptr;
IDirect3DDevice9* g_device = nullptr;
IDirect3DTexture9* g_texture = nullptr;
IDirect3DVertexBuffer9* g_vb = nullptr;
IDirect3DIndexBuffer9* g_ib = nullptr;

// Vertex structure
struct TexturedVertex {
    float x, y, z;
    D3DCOLOR color;
    float u, v;
};

// Math helpers
#define PI 3.14159265f

void MatrixIdentity(D3DMATRIX* m) {
    for(int i=0; i<4; ++i)
        for(int j=0; j<4; ++j)
            m->m[i][j] = (i==j) ? 1.0f : 0.0f;
}

void MatrixTranslation(D3DMATRIX* m, float x, float y, float z) {
    MatrixIdentity(m);
    m->m[3][0] = x;
    m->m[3][1] = y;
    m->m[3][2] = z;
}

void MatrixRotationY(D3DMATRIX* m, float angle) {
    MatrixIdentity(m);
    float c = cosf(angle);
    float s = sinf(angle);
    m->m[0][0] = c;
    m->m[0][2] = -s;
    m->m[2][0] = s;
    m->m[2][2] = c;
}

void MatrixPerspectiveFovLH(D3DMATRIX* m, float fovY, float aspect, float zn, float zf) {
    MatrixIdentity(m);
    float yScale = 1.0f / tanf(fovY / 2.0f);
    float xScale = yScale / aspect;
    m->m[0][0] = xScale;
    m->m[1][1] = yScale;
    m->m[2][2] = zf / (zf - zn);
    m->m[2][3] = 1.0f;
    m->m[3][2] = -zn * zf / (zf - zn);
    m->m[3][3] = 0.0f;
}

void MatrixLookAtLH(D3DMATRIX* m, const D3DVECTOR* eye, const D3DVECTOR* at, const D3DVECTOR* up) {
    D3DVECTOR zaxis = { at->x - eye->x, at->y - eye->y, at->z - eye->z };
    float len = sqrtf(zaxis.x*zaxis.x + zaxis.y*zaxis.y + zaxis.z*zaxis.z);
    zaxis.x /= len; zaxis.y /= len; zaxis.z /= len;

    D3DVECTOR xaxis; // cross(up, zaxis)
    xaxis.x = up->y * zaxis.z - up->z * zaxis.y;
    xaxis.y = up->z * zaxis.x - up->x * zaxis.z;
    xaxis.z = up->x * zaxis.y - up->y * zaxis.x;
    len = sqrtf(xaxis.x*xaxis.x + xaxis.y*xaxis.y + xaxis.z*xaxis.z);
    xaxis.x /= len; xaxis.y /= len; xaxis.z /= len;

    D3DVECTOR yaxis; // cross(zaxis, xaxis)
    yaxis.x = zaxis.y * xaxis.z - zaxis.z * xaxis.y;
    yaxis.y = zaxis.z * xaxis.x - zaxis.x * xaxis.z;
    yaxis.z = zaxis.x * xaxis.y - zaxis.y * xaxis.x;

    MatrixIdentity(m);
    m->m[0][0] = xaxis.x; m->m[0][1] = yaxis.x; m->m[0][2] = zaxis.x;
    m->m[1][0] = xaxis.y; m->m[1][1] = yaxis.y; m->m[1][2] = zaxis.y;
    m->m[2][0] = xaxis.z; m->m[2][1] = yaxis.z; m->m[2][2] = zaxis.z;
    
    // dot products
    m->m[3][0] = -(xaxis.x*eye->x + xaxis.y*eye->y + xaxis.z*eye->z);
    m->m[3][1] = -(yaxis.x*eye->x + yaxis.y*eye->y + yaxis.z*eye->z);
    m->m[3][2] = -(zaxis.x*eye->x + zaxis.y*eye->y + zaxis.z*eye->z);
}

// Cube data
// 24 vertices (4 per face, unique UVs per face)
TexturedVertex g_cubeVertices[] = {
    // Front (z = -1)
    { -1.0f,  1.0f, -1.0f, 0xFFFFFFFF, 0.0f, 0.0f }, //  0
    {  1.0f,  1.0f, -1.0f, 0xFFFFFFFF, 1.0f, 0.0f }, //  1
    {  1.0f, -1.0f, -1.0f, 0xFFFFFFFF, 1.0f, 1.0f }, //  2
    { -1.0f, -1.0f, -1.0f, 0xFFFFFFFF, 0.0f, 1.0f }, //  3
    // Back (z = 1)
    {  1.0f,  1.0f,  1.0f, 0xFFFFFFFF, 0.0f, 0.0f }, //  4
    { -1.0f,  1.0f,  1.0f, 0xFFFFFFFF, 1.0f, 0.0f }, //  5
    { -1.0f, -1.0f,  1.0f, 0xFFFFFFFF, 1.0f, 1.0f }, //  6
    {  1.0f, -1.0f,  1.0f, 0xFFFFFFFF, 0.0f, 1.0f }, //  7
    // Top (y = 1)
    { -1.0f,  1.0f,  1.0f, 0xFFFFFFFF, 0.0f, 0.0f }, //  8
    {  1.0f,  1.0f,  1.0f, 0xFFFFFFFF, 1.0f, 0.0f }, //  9
    {  1.0f,  1.0f, -1.0f, 0xFFFFFFFF, 1.0f, 1.0f }, // 10
    { -1.0f,  1.0f, -1.0f, 0xFFFFFFFF, 0.0f, 1.0f }, // 11
    // Bottom (y = -1)
    { -1.0f, -1.0f, -1.0f, 0xFFFFFFFF, 0.0f, 0.0f }, // 12
    {  1.0f, -1.0f, -1.0f, 0xFFFFFFFF, 1.0f, 0.0f }, // 13
    {  1.0f, -1.0f,  1.0f, 0xFFFFFFFF, 1.0f, 1.0f }, // 14
    { -1.0f, -1.0f,  1.0f, 0xFFFFFFFF, 0.0f, 1.0f }, // 15
    // Left (x = -1)
    { -1.0f,  1.0f, -1.0f, 0xFFFFFFFF, 0.0f, 0.0f }, // 16
    { -1.0f,  1.0f,  1.0f, 0xFFFFFFFF, 1.0f, 0.0f }, // 17
    { -1.0f, -1.0f,  1.0f, 0xFFFFFFFF, 1.0f, 1.0f }, // 18
    { -1.0f, -1.0f, -1.0f, 0xFFFFFFFF, 0.0f, 1.0f }, // 19
    // Right (x = 1)
    {  1.0f,  1.0f,  1.0f, 0xFFFFFFFF, 0.0f, 0.0f }, // 20
    {  1.0f,  1.0f, -1.0f, 0xFFFFFFFF, 1.0f, 0.0f }, // 21
    {  1.0f, -1.0f, -1.0f, 0xFFFFFFFF, 1.0f, 1.0f }, // 22
    {  1.0f, -1.0f,  1.0f, 0xFFFFFFFF, 0.0f, 1.0f }, // 23
};

// 12 triangles (6 faces * 2)
uint16_t g_cubeIndices[] = {
    // Front
     0,  1,  2,   0,  2,  3,
    // Back
     4,  5,  6,   4,  6,  7,
    // Top
     8,  9, 10,   8, 10, 11,
    // Bottom
    12, 13, 14,  12, 14, 15,
    // Left
    16, 17, 18,  16, 18, 19,
    // Right
    20, 21, 22,  20, 22, 23,
};

void main_loop() {
    if (!g_device) return;

    // Clear
    g_device->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, D3DCOLOR_ARGB(255, 0, 0, 64), 1.0f, 0);

    // Setup Matrices
    static float angle = 0.0f;
    angle += 0.02f;

    D3DMATRIX matWorld;
    MatrixRotationY(&matWorld, angle);
    g_device->SetTransform(D3DTS_WORLD, &matWorld);

    D3DVECTOR eye = { 0.0f, 3.0f, -5.0f };
    D3DVECTOR at = { 0.0f, 0.0f, 0.0f };
    D3DVECTOR up = { 0.0f, 1.0f, 0.0f };
    D3DMATRIX matView;
    MatrixLookAtLH(&matView, &eye, &at, &up);
    g_device->SetTransform(D3DTS_VIEW, &matView);

    D3DMATRIX matProj;
    MatrixPerspectiveFovLH(&matProj, PI / 4.0f, 640.0f/480.0f, 0.1f, 100.0f);
    g_device->SetTransform(D3DTS_PROJECTION, &matProj);
    
    g_device->BeginScene();

    // Draw Cube
    g_device->SetFVF(D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1);
    g_device->SetTexture(0, g_texture);
    g_device->SetStreamSource(0, g_vb, 0, sizeof(TexturedVertex));
    g_device->SetIndices(g_ib);

    // DrawIndexedPrimitive(Type, BaseVertexIndex, MinIndex, NumVertices, StartIndex, PrimCount)
    // 24 vertices (4 per face), 12 triangles
    g_device->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, 24, 0, 12);

    g_device->EndScene();
    g_device->Present(nullptr, nullptr, nullptr, nullptr);
}

int main() {
    printf("Initializing D3D9...\n");
    g_d3d = Direct3DCreate9(D3D_SDK_VERSION);
    if (!g_d3d) {
        printf("Failed to create D3D9 object\n");
        return 1;
    }

    D3DPRESENT_PARAMETERS pp = {};
    pp.BackBufferWidth = 800;
    pp.BackBufferHeight = 600;
    pp.BackBufferFormat = D3DFMT_X8R8G8B8;

    if (FAILED(g_d3d->CreateDevice(0, D3DDEVTYPE_HAL, nullptr,
                                   D3DCREATE_HARDWARE_VERTEXPROCESSING,
                                   &pp, &g_device))) {
        printf("Failed to create D3D9 Device\n");
        return 1;
    }

    // Load Texture from File
    g_texture = LoadTextureFromFile(g_device, "test.png");
    if (!g_texture) {
        printf("Failed to load test.png. Fallback to procedural texture.\n");
        // Create Texture (Red & White Checkerboard)
        if (FAILED(g_device->CreateTexture(64, 64, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &g_texture, nullptr))) {
            printf("Failed to create Texture\n");
            return 1;
        }
        D3DLOCKED_RECT rect;
        if (SUCCEEDED(g_texture->LockRect(0, &rect, nullptr, 0))) {
            uint32_t* pixels = (uint32_t*)rect.pBits;
            for (int y = 0; y < 64; y++) {
                for (int x = 0; x < 64; x++) {
                    bool checker = ((x / 8) + (y / 8)) % 2 == 0;
                    // Semi-transparent checkerboard
                    // White (alpha 128) and Red (alpha 128)
                    uint32_t alpha = 128; 
                    pixels[y * 64 + x] = checker ? D3DCOLOR_ARGB(alpha, 255, 255, 255) : D3DCOLOR_ARGB(alpha, 255, 0, 0);
                }
            }
            g_texture->UnlockRect(0);
        }
    }
    
    // Enable Alpha Blending
    g_device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    g_device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    g_device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    
    // Create Vertex Buffer
    if (FAILED(g_device->CreateVertexBuffer(sizeof(g_cubeVertices), 0, 0, D3DPOOL_MANAGED, &g_vb, nullptr))) {
        printf("Failed to create Vertex Buffer\n");
        return 1;
    }
    void* vbData;
    if (SUCCEEDED(g_vb->Lock(0, 0, &vbData, 0))) {
        memcpy(vbData, g_cubeVertices, sizeof(g_cubeVertices));
        g_vb->Unlock();
    }
    
    // Create Index Buffer
    if (FAILED(g_device->CreateIndexBuffer(sizeof(g_cubeIndices), 0, D3DFMT_INDEX16, D3DPOOL_MANAGED, &g_ib, nullptr))) {
        printf("Failed to create Index Buffer\n");
        return 1;
    }
    void* ibData;
    if (SUCCEEDED(g_ib->Lock(0, 0, &ibData, 0))) {
        memcpy(ibData, g_cubeIndices, sizeof(g_cubeIndices));
        g_ib->Unlock();
    }

    printf("Starting render loop...\n");
    emscripten_set_main_loop(main_loop, 0, 1);

    return 0;
}
