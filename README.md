# d3d9-webgl

A Direct3D 9 Fixed-Function Pipeline implementation targeting WebGL 2.0 via Emscripten/WebAssembly.

Drop-in D3D9 headers and a single `.cpp` file that translates D3D9 API calls to WebGL — enabling legacy D3D9 applications to run in the browser without rewriting their rendering code.

## ✨ Features

- **Full FFP Emulation** — Per-vertex lighting (3 point lights), materials, texture stage states, transform matrices
- **All Draw Paths** — `DrawIndexedPrimitive`, `DrawIndexedPrimitiveUP`, `DrawPrimitiveUP`, `DrawPrimitive`
- **FVF Parsing** — Automatic vertex layout from `D3DFVF_XYZ`, `D3DFVF_XYZRHW`, `D3DFVF_NORMAL`, `D3DFVF_DIFFUSE`, `D3DFVF_TEX1`–`TEX8`
- **Texture Formats** — DXT1/3/5 (via S3TC extension), A8R8G8B8, X8R8G8B8, R5G6B5, A4R4G4B4, A1R5G5B5, R8G8B8
- **Render States** — Alpha blending, alpha test, depth test, stencil operations, culling, scissor test, color write mask
- **Texture Stage States** — Stage 0 color/alpha ops (MODULATE, MODULATE2X, SELECTARG), Stage 1 lightmap blending (MODULATE, MODULATE2X, MODULATE4X, ADDSIGNED, ADDSMOOTH)
- **Render-to-Texture** — FBO-based with automatic Y-flip handling (D3D top-down vs GL bottom-up)
- **Clip Plane Emulation** — Fragment shader `discard` (WebGL has no hardware clip planes)
- **State Caching** — Texture bindings, shader programs, sampler states, viewport/scissor to minimize WebGL API overhead
- **GL Interop** — `extern "C"` API to access underlying GL texture IDs for hybrid rendering
- **Canvas Selector** — Configurable target canvas via overloaded `CreateDevice`

## 🏗 Architecture

```
   D3D9 Application Code (C++)
            │
   ┌──────────────┴──────────────┐
   │    d3d9.h            │  D3D9 type definitions, enums, interfaces
   │    d3dx9math.h       │  D3DX math (vectors, matrices, quaternions)
   │    d3dx9.h           │  D3DX stubs (ID3DXLine, ID3DXFont)
   │    windows_compat.h  │  Windows API stubs for non-Windows builds
   └──────────────┬──────────────┘
            │
   ┌────────┴────────┐
   │    d3d9.cpp     │  WebGL 2.0 implementation (~3,400 lines)
   │                 │  - GLSL vertex/fragment shaders (FFP emulation)
   │                 │  - Texture management (upload, format conversion)
   │                 │  - Render state → GL state mapping
   │                 │  - FBO render targets, clip planes, stencil
   └────────┬────────┘
            │
      WebGL 2.0 / OpenGL ES 3.0
         (Emscripten)
```

## 🚀 Quick Start

### Prerequisites

- [Emscripten SDK](https://emscripten.org/docs/getting_started/downloads.html)

### Build the Demo

```bash
cd examples
./build.sh
cd build && python3 -m http.server 8000
```

Open `http://localhost:8000/d3d9-poc.html` — you should see a textured rotating cube.

### Integrate into Your Project

1. Copy `d3d9.h`, `d3d9.cpp`, `d3dx9math.h`, `d3dx9.h`, `windows_compat.h` into your project
2. Add `d3d9.cpp` to your Emscripten build
3. Link with `-sUSE_WEBGL2=1 -sFULL_ES3=1`

```cmake
add_executable(my_app main.cpp d3d9.cpp)
target_link_options(my_app PRIVATE
    -sUSE_WEBGL2=1
    -sFULL_ES3=1
    -sWASM=1
    -sALLOW_MEMORY_GROWTH=1
)
```

## 💡 Usage

The wrapper implements standard D3D9 interfaces. Existing D3D9 code works with minimal changes:

```cpp
#include "d3d9.h"

IDirect3D9* d3d = Direct3DCreate9(D3D_SDK_VERSION);

// Set backbuffer resolution (canvas drawingBuffer will be set to this size)
D3DPRESENT_PARAMETERS pp = {};
pp.BackBufferWidth = 1024;
pp.BackBufferHeight = 768;
pp.BackBufferFormat = D3DFMT_X8R8G8B8;

IDirect3DDevice9* device;
d3d->CreateDevice(0, D3DDEVTYPE_HAL, nullptr,
                  D3DCREATE_HARDWARE_VERTEXPROCESSING,
                  &pp, &device);

// Standard D3D9 rendering
device->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER,
              D3DCOLOR_ARGB(255, 0, 0, 64), 1.0f, 0);
device->SetTransform(D3DTS_WORLD, &matWorld);
device->SetTransform(D3DTS_VIEW, &matView);
device->SetTransform(D3DTS_PROJECTION, &matProj);
device->SetTexture(0, texture);
device->SetStreamSource(0, vb, 0, stride);
device->SetIndices(ib);
device->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, numVerts, 0, numTris);
```

### Canvas Setup

The canvas element's `width`/`height` attributes should match the backbuffer resolution. The browser stretches the canvas to fill its CSS area (equivalent to D3D9's backbuffer-to-window stretch).

```html
<canvas id="canvas" width="1024" height="768"></canvas>
```

### Custom Canvas Selector

By default, the wrapper targets `#canvas`. To render to a different canvas element, use the extended `CreateDevice` overload:

```cpp
d3d->CreateDevice(0, D3DDEVTYPE_HAL, nullptr,
                  D3DCREATE_HARDWARE_VERTEXPROCESSING,
                  &pp, &device, "#my-canvas");
```

## 🔌 GL Interop

For hybrid rendering (mixing D3D9 wrapper calls with direct WebGL), the wrapper exposes the underlying GL resources:

```cpp
extern "C" {
    // Get the GL texture ID and dimensions from a D3D9 texture
    void D3D9_GetTextureInfo(IDirect3DTexture9* pTex,
                             unsigned int* gl_id,
                             unsigned int* w,
                             unsigned int* h);

    // Invalidate the wrapper's texture binding cache
    // (call after direct glBindTexture outside the wrapper)
    void D3D9_InvalidateTextureCache();
}
```

## 📋 Supported D3D9 API

<details>
<summary>Click to expand full API coverage</summary>

### IDirect3DDevice9

| Method | Status |
|--------|--------|
| `Clear` | Implemented (color, depth, stencil) |
| `BeginScene` / `EndScene` | No-op (WebGL has no scene concept) |
| `Present` | Implemented |
| `DrawIndexedPrimitive` | Implemented |
| `DrawIndexedPrimitiveUP` | Implemented |
| `DrawPrimitiveUP` | Implemented |
| `DrawPrimitive` | Implemented |
| `SetRenderState` | Implemented (see render states below) |
| `SetTextureStageState` | Implemented (stage 0 + stage 1) |
| `SetSamplerState` | Implemented (filter, address, LOD bias, anisotropy) |
| `SetTransform` | Implemented (world, view, projection, texture) |
| `SetTexture` | Implemented (stage 0 + stage 1 lightmap) |
| `SetStreamSource` | Implemented |
| `SetIndices` | Implemented |
| `SetFVF` | Implemented |
| `SetRenderTarget` | Implemented (FBO-based) |
| `SetDepthStencilSurface` | Implemented |
| `SetClipPlane` | Implemented (1 plane, shader-based) |
| `SetLight` / `LightEnable` | Implemented (3 point lights) |
| `SetMaterial` | Implemented |
| `SetScissorRect` | Implemented |
| `SetViewport` | Implemented |
| `CreateTexture` | Implemented |
| `CreateVertexBuffer` | Implemented |
| `CreateIndexBuffer` | Implemented |
| `CreateDepthStencilSurface` | Implemented |
| `StretchRect` | Implemented (FBO blit with Y-flip) |
| `GetDeviceCaps` | Implemented (reports FFP-only) |

### Render States (D3DRS_*)

Alpha blending (src/dest blend), depth test (func, write enable), stencil (func, ref, fail/zfail/pass ops), alpha test (ref), culling, scissor test, color write enable, lighting, ambient, texture factor, clip plane enable.

### Texture Formats

| Format | Type |
|--------|------|
| DXT1 / DXT3 / DXT5 | Compressed (S3TC extension) |
| A8R8G8B8 / X8R8G8B8 | 32-bit (BGRA to RGBA swizzle) |
| R5G6B5 / A4R4G4B4 / A1R5G5B5 | 16-bit (expanded to RGBA) |
| R8G8B8 | 24-bit |

</details>

## 🎮 Used In

This wrapper was developed as part of porting [GunZ: The Duel](https://en.wikipedia.org/wiki/GunZ:_The_Duel) (2003, MAIET Entertainment) to run entirely in the browser via WebAssembly. The original game's Direct3D 9 rendering code runs through this translation layer without modification.

## ⚠️ Limitations

- **FFP only** — No programmable vertex/pixel shaders. The wrapper reports `VertexShaderVersion = 0` to force applications into FFP code paths.
- **Single shader** — All FFP states are handled by one vertex + one fragment shader. Complex multi-pass techniques may need adaptation.
- **3 point lights max** — Hardcoded limit in the GLSL shader. Directional and spot lights are not implemented.
- **`D3DXMatrixInverse` is a stub** — Returns identity. Applications relying on matrix inversion should provide their own implementation.
- **Stream 0 only** — Multi-stream vertex buffer binding is not supported.
- **No GPU readback** — `LockRect` on render target textures is not supported.

## 📄 License

MIT
