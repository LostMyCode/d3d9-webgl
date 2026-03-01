#include "d3d9.h"
#include <emscripten.h>
#include <emscripten/html5.h>
#include <cmath>
#include <cstdio>
#include <vector>

// Global variables
IDirect3D9* g_d3d = nullptr;
IDirect3DDevice9* g_device = nullptr;

// Lit vertex: position + normal (for FFP lighting)
struct LitVertex {
    float x, y, z;
    float nx, ny, nz;
};

// Colored vertex: position + diffuse (for unlit indicators)
struct ColorVertex {
    float x, y, z;
    D3DCOLOR color;
};

#define PI 3.14159265f

// ---- Math helpers ----

void MatrixIdentity(D3DMATRIX* m) {
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            m->m[i][j] = (i == j) ? 1.0f : 0.0f;
}

void MatrixTranslation(D3DMATRIX* m, float x, float y, float z) {
    MatrixIdentity(m);
    m->m[3][0] = x;
    m->m[3][1] = y;
    m->m[3][2] = z;
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

    D3DVECTOR xaxis;
    xaxis.x = up->y * zaxis.z - up->z * zaxis.y;
    xaxis.y = up->z * zaxis.x - up->x * zaxis.z;
    xaxis.z = up->x * zaxis.y - up->y * zaxis.x;
    len = sqrtf(xaxis.x*xaxis.x + xaxis.y*xaxis.y + xaxis.z*xaxis.z);
    xaxis.x /= len; xaxis.y /= len; xaxis.z /= len;

    D3DVECTOR yaxis;
    yaxis.x = zaxis.y * xaxis.z - zaxis.z * xaxis.y;
    yaxis.y = zaxis.z * xaxis.x - zaxis.x * xaxis.z;
    yaxis.z = zaxis.x * xaxis.y - zaxis.y * xaxis.x;

    MatrixIdentity(m);
    m->m[0][0] = xaxis.x; m->m[0][1] = yaxis.x; m->m[0][2] = zaxis.x;
    m->m[1][0] = xaxis.y; m->m[1][1] = yaxis.y; m->m[1][2] = zaxis.y;
    m->m[2][0] = xaxis.z; m->m[2][1] = yaxis.z; m->m[2][2] = zaxis.z;
    m->m[3][0] = -(xaxis.x*eye->x + xaxis.y*eye->y + xaxis.z*eye->z);
    m->m[3][1] = -(yaxis.x*eye->x + yaxis.y*eye->y + yaxis.z*eye->z);
    m->m[3][2] = -(zaxis.x*eye->x + zaxis.y*eye->y + zaxis.z*eye->z);
}

// ---- Geometry data ----

std::vector<LitVertex> g_sphereVerts;
std::vector<uint16_t> g_sphereIdx;

LitVertex g_floor[6];

void GenerateSphere(float radius, int slices, int stacks) {
    g_sphereVerts.clear();
    g_sphereIdx.clear();

    for (int i = 0; i <= stacks; i++) {
        float phi = PI * (float)i / stacks;
        float sinPhi = sinf(phi), cosPhi = cosf(phi);
        for (int j = 0; j <= slices; j++) {
            float theta = 2.0f * PI * (float)j / slices;
            float x = sinPhi * cosf(theta);
            float y = cosPhi;
            float z = sinPhi * sinf(theta);
            LitVertex v;
            v.x = radius * x; v.y = radius * y; v.z = radius * z;
            v.nx = x; v.ny = y; v.nz = z;
            g_sphereVerts.push_back(v);
        }
    }

    for (int i = 0; i < stacks; i++) {
        for (int j = 0; j < slices; j++) {
            uint16_t a = (uint16_t)(i * (slices + 1) + j);
            uint16_t b = (uint16_t)(a + slices + 1);
            // CW winding for D3D9 front face
            g_sphereIdx.push_back(a);
            g_sphereIdx.push_back(a + 1);
            g_sphereIdx.push_back(b);

            g_sphereIdx.push_back(a + 1);
            g_sphereIdx.push_back(b + 1);
            g_sphereIdx.push_back(b);
        }
    }
}

void GenerateFloor(float size, float y) {
    // Normal pointing up, CW winding for D3D9
    g_floor[0] = { -size, y, -size, 0, 1, 0 };
    g_floor[1] = {  size, y, -size, 0, 1, 0 };
    g_floor[2] = {  size, y,  size, 0, 1, 0 };
    g_floor[3] = { -size, y, -size, 0, 1, 0 };
    g_floor[4] = {  size, y,  size, 0, 1, 0 };
    g_floor[5] = { -size, y,  size, 0, 1, 0 };
}

// Draw a small colored cube at (px,py,pz) using DrawPrimitiveUP
// Vertices are pre-transformed (world matrix should be identity)
void DrawLightCube(float px, float py, float pz, D3DCOLOR color) {
    float s = 0.1f;
    ColorVertex v[] = {
        // Front (CW from outside)
        {px-s, py+s, pz-s, color}, {px+s, py+s, pz-s, color}, {px+s, py-s, pz-s, color},
        {px-s, py+s, pz-s, color}, {px+s, py-s, pz-s, color}, {px-s, py-s, pz-s, color},
        // Back
        {px+s, py+s, pz+s, color}, {px-s, py+s, pz+s, color}, {px-s, py-s, pz+s, color},
        {px+s, py+s, pz+s, color}, {px-s, py-s, pz+s, color}, {px+s, py-s, pz+s, color},
        // Top
        {px-s, py+s, pz+s, color}, {px+s, py+s, pz+s, color}, {px+s, py+s, pz-s, color},
        {px-s, py+s, pz+s, color}, {px+s, py+s, pz-s, color}, {px-s, py+s, pz-s, color},
        // Bottom
        {px-s, py-s, pz-s, color}, {px+s, py-s, pz-s, color}, {px+s, py-s, pz+s, color},
        {px-s, py-s, pz-s, color}, {px+s, py-s, pz+s, color}, {px-s, py-s, pz+s, color},
        // Left
        {px-s, py+s, pz+s, color}, {px-s, py+s, pz-s, color}, {px-s, py-s, pz-s, color},
        {px-s, py+s, pz+s, color}, {px-s, py-s, pz-s, color}, {px-s, py-s, pz+s, color},
        // Right
        {px+s, py+s, pz-s, color}, {px+s, py+s, pz+s, color}, {px+s, py-s, pz+s, color},
        {px+s, py+s, pz-s, color}, {px+s, py-s, pz+s, color}, {px+s, py-s, pz-s, color},
    };
    g_device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 12, v, sizeof(ColorVertex));
}

// ---- Main loop ----

void main_loop() {
    if (!g_device) return;

    g_device->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER,
                    D3DCOLOR_ARGB(255, 8, 8, 24), 1.0f, 0);

    static float time = 0.0f;
    time += 0.02f;

    // Camera
    D3DVECTOR eye = { 0.0f, 3.0f, -6.0f };
    D3DVECTOR at  = { 0.0f, 0.0f,  0.0f };
    D3DVECTOR up  = { 0.0f, 1.0f,  0.0f };
    D3DMATRIX matView;
    MatrixLookAtLH(&matView, &eye, &at, &up);
    g_device->SetTransform(D3DTS_VIEW, &matView);

    D3DMATRIX matProj;
    MatrixPerspectiveFovLH(&matProj, PI / 4.0f, 800.0f / 600.0f, 0.1f, 100.0f);
    g_device->SetTransform(D3DTS_PROJECTION, &matProj);

    // 3 orbiting point lights (Red, Green, Blue)
    float orbitRadius = 3.5f;
    struct LightInfo {
        float angle;
        float y;
        D3DCOLORVALUE diffuse;
        D3DCOLOR indicator;
    };
    LightInfo lightInfos[3] = {
        { time,              2.0f, {1.0f, 0.1f, 0.1f, 1.0f}, D3DCOLOR_ARGB(255, 255, 60, 60) },
        { time + 2.094395f,  2.5f, {0.1f, 1.0f, 0.1f, 1.0f}, D3DCOLOR_ARGB(255, 60, 255, 60) },
        { time + 4.188790f,  1.5f, {0.3f, 0.3f, 1.0f, 1.0f}, D3DCOLOR_ARGB(255, 80, 80, 255) },
    };

    D3DVECTOR lightPos[3];
    for (int i = 0; i < 3; i++) {
        lightPos[i].x = orbitRadius * cosf(lightInfos[i].angle);
        lightPos[i].y = lightInfos[i].y;
        lightPos[i].z = orbitRadius * sinf(lightInfos[i].angle);

        D3DLIGHT9 light = {};
        light.Type = D3DLIGHT_POINT;
        light.Diffuse = lightInfos[i].diffuse;
        light.Position = lightPos[i];
        light.Range = 20.0f;
        light.Attenuation0 = 1.0f;
        light.Attenuation1 = 0.09f;
        light.Attenuation2 = 0.02f;
        g_device->SetLight(i, &light);
        g_device->LightEnable(i, TRUE);
    }

    g_device->BeginScene();

    // ======== Lit geometry (sphere + floor) ========
    g_device->SetRenderState(D3DRS_LIGHTING, TRUE);
    g_device->SetFVF(D3DFVF_XYZ | D3DFVF_NORMAL);

    // -- Sphere: DrawIndexedPrimitiveUP --
    D3DMATERIAL9 sphereMat = {};
    sphereMat.Diffuse = { 0.9f, 0.9f, 0.9f, 1.0f };
    sphereMat.Ambient = { 0.1f, 0.1f, 0.1f, 1.0f };
    g_device->SetMaterial(&sphereMat);

    D3DMATRIX matWorld;
    MatrixTranslation(&matWorld, 0.0f, 0.5f, 0.0f);
    g_device->SetTransform(D3DTS_WORLD, &matWorld);

    g_device->DrawIndexedPrimitiveUP(
        D3DPT_TRIANGLELIST,
        0,
        (UINT)g_sphereVerts.size(),
        (UINT)g_sphereIdx.size() / 3,
        g_sphereIdx.data(),
        D3DFMT_INDEX16,
        g_sphereVerts.data(),
        sizeof(LitVertex)
    );

    // -- Floor: DrawPrimitiveUP --
    D3DMATERIAL9 floorMat = {};
    floorMat.Diffuse = { 0.7f, 0.7f, 0.7f, 1.0f };
    floorMat.Ambient = { 0.1f, 0.1f, 0.1f, 1.0f };
    g_device->SetMaterial(&floorMat);

    MatrixIdentity(&matWorld);
    g_device->SetTransform(D3DTS_WORLD, &matWorld);

    g_device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 2, g_floor, sizeof(LitVertex));

    // ======== Unlit light indicators ========
    g_device->SetRenderState(D3DRS_LIGHTING, FALSE);
    g_device->SetFVF(D3DFVF_XYZ | D3DFVF_DIFFUSE);

    // World = identity (positions are pre-transformed in vertex data)
    // matWorld is already identity from the floor draw above

    for (int i = 0; i < 3; i++) {
        DrawLightCube(lightPos[i].x, lightPos[i].y, lightPos[i].z,
                      lightInfos[i].indicator);
    }

    g_device->EndScene();
    g_device->Present(nullptr, nullptr, nullptr, nullptr);
}

// ---- Entry point ----

int main() {
    printf("Initializing D3D9 Lighting Demo...\n");

    g_d3d = Direct3DCreate9(D3D_SDK_VERSION);
    if (!g_d3d) {
        printf("Failed to create D3D9\n");
        return 1;
    }

    D3DPRESENT_PARAMETERS pp = {};
    pp.BackBufferWidth = 800;
    pp.BackBufferHeight = 600;
    pp.BackBufferFormat = D3DFMT_X8R8G8B8;

    if (FAILED(g_d3d->CreateDevice(0, D3DDEVTYPE_HAL, nullptr,
                                   D3DCREATE_HARDWARE_VERTEXPROCESSING,
                                   &pp, &g_device))) {
        printf("Failed to create device\n");
        return 1;
    }

    // Render states
    g_device->SetRenderState(D3DRS_ZENABLE, TRUE);
    g_device->SetRenderState(D3DRS_LIGHTING, TRUE);
    g_device->SetRenderState(D3DRS_AMBIENT, D3DCOLOR_ARGB(255, 15, 15, 25));

    // Generate geometry
    GenerateSphere(1.2f, 32, 24);
    GenerateFloor(5.0f, -1.0f);

    printf("Sphere: %zu vertices, %zu triangles\n",
           g_sphereVerts.size(), g_sphereIdx.size() / 3);
    printf("Starting render loop...\n");

    emscripten_set_main_loop(main_loop, 0, 1);
    return 0;
}
