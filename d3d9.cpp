#include "d3d9.h"
#include <emscripten.h>
#include <emscripten/html5.h>
#include <GLES3/gl3.h>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <vector>
#include <cstring>
#include <algorithm>
#include <unordered_set>
#include <cmath>

static const char* g_canvas_selector = "#canvas";


#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

// Map D3DBLEND to GL enum
static GLenum d3d_blend_to_gl(DWORD blend) {
    switch (blend) {
        case D3DBLEND_ZERO: return GL_ZERO;
        case D3DBLEND_ONE: return GL_ONE;
        case D3DBLEND_SRCCOLOR: return GL_SRC_COLOR;
        case D3DBLEND_INVSRCCOLOR: return GL_ONE_MINUS_SRC_COLOR;
        case D3DBLEND_SRCALPHA: return GL_SRC_ALPHA;
        case D3DBLEND_INVSRCALPHA: return GL_ONE_MINUS_SRC_ALPHA;
        case D3DBLEND_DESTALPHA: return GL_DST_ALPHA;
        case D3DBLEND_INVDESTALPHA: return GL_ONE_MINUS_DST_ALPHA;
        case D3DBLEND_DESTCOLOR: return GL_DST_COLOR;
        case D3DBLEND_INVDESTCOLOR: return GL_ONE_MINUS_DST_COLOR;
        default: return GL_ONE; 
    }
}

static GLenum d3d_cmp_to_gl(DWORD func) {
    switch (func) {
        case D3DCMP_NEVER: return GL_NEVER;
        case D3DCMP_LESS: return GL_LESS;
        case D3DCMP_EQUAL: return GL_EQUAL;
        case D3DCMP_LESSEQUAL: return GL_LEQUAL;
        case D3DCMP_GREATER: return GL_GREATER;
        case D3DCMP_NOTEQUAL: return GL_NOTEQUAL;
        case D3DCMP_GREATEREQUAL: return GL_GEQUAL;
        case D3DCMP_ALWAYS: return GL_ALWAYS;
        default: return GL_ALWAYS;
    }
}

static GLenum d3d_stencil_op_to_gl(DWORD op) {
    switch (op) {
        case D3DSTENCILOP_KEEP: return GL_KEEP;
        case D3DSTENCILOP_ZERO: return GL_ZERO;
        case D3DSTENCILOP_REPLACE: return GL_REPLACE;
        case D3DSTENCILOP_INCRSAT: return GL_INCR;
        case D3DSTENCILOP_DECRSAT: return GL_DECR;
        case D3DSTENCILOP_INVERT: return GL_INVERT;
        case D3DSTENCILOP_INCR: return GL_INCR_WRAP;
        case D3DSTENCILOP_DECR: return GL_DECR_WRAP;
        default: return GL_KEEP;
    }
}

// WebGL context
static EMSCRIPTEN_WEBGL_CONTEXT_HANDLE g_webgl_context = 0;

// Shader sources
static const char* vertex_shader_source = R"(
attribute vec4 a_position;
attribute vec3 a_normal;
attribute vec4 a_color;
attribute vec2 a_texcoord;
attribute vec2 a_texcoord1;
varying vec4 v_color;
varying vec2 v_texcoord;
varying vec2 v_texcoord1;
varying vec4 v_position_world;

uniform mat4 u_mvp;
uniform mat4 u_world;
uniform bool u_fbo_flip_y;

// D3D9 FFP Lighting
uniform bool u_lighting_enabled;
uniform vec4 u_material_diffuse;
uniform vec4 u_material_ambient;
uniform vec3 u_global_ambient;

// Light 0 (point light)
uniform bool u_light0_enabled;
uniform vec3 u_light0_position;
uniform vec4 u_light0_diffuse;
uniform vec3 u_light0_ambient;
uniform vec3 u_light0_attenuation; // (att0, att1, att2)
uniform float u_light0_range;

// Light 1 (point light)
uniform bool u_light1_enabled;
uniform vec3 u_light1_position;
uniform vec4 u_light1_diffuse;
uniform vec3 u_light1_ambient;
uniform vec3 u_light1_attenuation;
uniform float u_light1_range;

// Light 2 (point light)
uniform bool u_light2_enabled;
uniform vec3 u_light2_position;
uniform vec4 u_light2_diffuse;
uniform vec3 u_light2_ambient;
uniform vec3 u_light2_attenuation;
uniform float u_light2_range;

// D3D9 FFP point light contribution
vec3 calcPointLight(vec3 lightPos, vec4 lightDiffuse, vec3 lightAmbient,
                    vec3 lightAtten, float lightRange,
                    vec3 worldPos, vec3 worldNormal) {
    vec3 light_vec = lightPos - worldPos;
    float dist = length(light_vec);
    if (dist >= lightRange) return vec3(0.0);
    vec3 light_dir = normalize(light_vec);
    float NdotL = max(dot(worldNormal, light_dir), 0.0);
    float atten = 1.0 / (lightAtten.x + lightAtten.y * dist + lightAtten.z * dist * dist);
    return (u_material_ambient.rgb * lightAmbient
          + u_material_diffuse.rgb * lightDiffuse.rgb * NdotL) * atten;
}

void main() {
    // Note: v_position_world is always computed even for XYZRHW (where a_position.w = rhw, not 1.0).
    // Clip plane test uses this value. XYZRHW + clip plane is semantically invalid (XYZRHW coordinates
    // are screen-space, not world-space). If this becomes an issue, clip plane
    // test must skip for XYZRHW vertices (check D3DFVF_XYZRHW in C++ before enabling clip plane).
    v_position_world = u_world * a_position;
    gl_Position = u_mvp * a_position;
    if (u_fbo_flip_y) gl_Position.y = -gl_Position.y;
    v_texcoord = a_texcoord;
    v_texcoord1 = a_texcoord1;

    if (u_lighting_enabled) {
        // D3D9 FFP per-vertex lighting formula
        vec3 world_pos = (u_world * a_position).xyz;
        vec3 world_normal = normalize(mat3(u_world) * a_normal);

        // Global ambient: Material.Ambient * GlobalAmbient
        vec3 lit_color = u_material_ambient.rgb * u_global_ambient;

        // Per-light contributions (up to 3 point lights)
        if (u_light0_enabled) {
            lit_color += calcPointLight(u_light0_position, u_light0_diffuse, u_light0_ambient,
                                        u_light0_attenuation, u_light0_range, world_pos, world_normal);
        }
        if (u_light1_enabled) {
            lit_color += calcPointLight(u_light1_position, u_light1_diffuse, u_light1_ambient,
                                        u_light1_attenuation, u_light1_range, world_pos, world_normal);
        }
        if (u_light2_enabled) {
            lit_color += calcPointLight(u_light2_position, u_light2_diffuse, u_light2_ambient,
                                        u_light2_attenuation, u_light2_range, world_pos, world_normal);
        }

        v_color = vec4(clamp(lit_color, 0.0, 1.0), u_material_diffuse.a);
    } else {
        v_color = a_color;
    }
}
)";

static const char* fragment_shader_source = R"(
    precision mediump float;
    varying vec4 v_color;
    varying vec2 v_texcoord;
    varying vec2 v_texcoord1;
    varying vec4 v_position_world;
    uniform sampler2D u_texture;
    uniform bool u_clip_plane_enabled;
    uniform vec4 u_clip_plane;
    uniform sampler2D u_texture_lm;
    uniform bool u_use_texture;
    uniform bool u_use_lightmap;
    uniform int u_stage0_colorop;
    uniform int u_stage0_colorarg1;
    uniform int u_stage0_colorarg2;
    uniform int u_stage0_alphaop;
    uniform int u_stage0_alphaarg1;
    uniform int u_stage0_alphaarg2;
    uniform int u_stage1_op;
    uniform bool u_alpha_test_enabled;
    uniform float u_alpha_ref;
    uniform vec4 u_texture_factor;
    uniform float u_lod_bias;

    // Helper function to select texture argument source
    vec4 selectTextureArg(int arg) {
        // D3DTA_DIFFUSE = 0, D3DTA_CURRENT = 1, D3DTA_TEXTURE = 2, D3DTA_TFACTOR = 3
        // Note: In GLSL ES 2.0, we can't use bitwise operations, but D3DTA values are already 0-6
        if (arg == 0) return v_color; // D3DTA_DIFFUSE
        if (arg == 1) return v_color; // D3DTA_CURRENT (for stage 0, same as DIFFUSE)
        if (arg == 2) { // D3DTA_TEXTURE
            // Only sample texture if it's actually being used
            if (u_use_texture) {
                return texture2D(u_texture, v_texcoord, u_lod_bias);
            } else {
                return vec4(1.0);
            }
        }
        if (arg == 3) return u_texture_factor; // D3DTA_TFACTOR
        return vec4(1.0); // Default
    }

    void main() {
        // Clip plane test (shader-based, replaces hardware clip planes for WebGL)
        if (u_clip_plane_enabled) {
            float clip_dist = dot(v_position_world.xyz, u_clip_plane.xyz) + u_clip_plane.w;
            if (clip_dist < 0.0) discard;
        }

        vec4 tex_color = vec4(1.0);

        if (u_use_texture) {
            tex_color = texture2D(u_texture, v_texcoord, u_lod_bias);
        }

        // Stage 0: Always process texture stage states (faithful D3D9 conversion)
        vec4 stage0_color;
        float stage0_alpha;

        // Process COLOR operation
        vec4 colorArg1 = selectTextureArg(u_stage0_colorarg1);
        vec4 colorArg2 = selectTextureArg(u_stage0_colorarg2);

        // D3DTOP: SELECTARG1=2, SELECTARG2=3, MODULATE=4, MODULATE2X=5
        if (u_stage0_colorop == 2) {
            stage0_color.rgb = colorArg1.rgb;
        } else if (u_stage0_colorop == 3) {
            stage0_color.rgb = colorArg2.rgb;
        } else if (u_stage0_colorop == 4) {
            stage0_color.rgb = colorArg1.rgb * colorArg2.rgb;
        } else if (u_stage0_colorop == 5) {
            // MODULATE2X for character rendering
            stage0_color.rgb = clamp(colorArg1.rgb * colorArg2.rgb * 2.0, 0.0, 1.0);
        } else {
            // Default: MODULATE
            stage0_color.rgb = colorArg1.rgb * colorArg2.rgb;
        }

        // Process ALPHA operation
        vec4 alphaArg1 = selectTextureArg(u_stage0_alphaarg1);
        vec4 alphaArg2 = selectTextureArg(u_stage0_alphaarg2);

        if (u_stage0_alphaop == 2) {
            stage0_alpha = alphaArg1.a;
        } else if (u_stage0_alphaop == 3) {
            stage0_alpha = alphaArg2.a;
        } else if (u_stage0_alphaop == 4) {
            stage0_alpha = alphaArg1.a * alphaArg2.a;
        } else {
            // Default: MODULATE
            stage0_alpha = alphaArg1.a * alphaArg2.a;
        }

        vec4 final_color = vec4(stage0_color.rgb, stage0_alpha);

        // Apply lightmap if enabled (Stage 1)
        if (u_use_lightmap) {
            vec4 lightmap = texture2D(u_texture_lm, v_texcoord1, u_lod_bias);

            // D3DTOP values:
            // 4 = MODULATE (x1)
            // 5 = MODULATE2X (x2)
            // 6 = MODULATE4X (x4)
            // 8 = ADDSIGNED  (result = arg1 + arg2 - 0.5, signed additive blend)
            // 11 = ADDSMOOTH (result = arg1 + arg2 - arg1*arg2, reflection-like blend)

            if (u_stage1_op == 11) { // ADDSMOOTH
                 // AddSmooth: dest + src - dest*src = dest + src*(1-dest)
                 final_color.rgb = final_color.rgb + lightmap.rgb - (final_color.rgb * lightmap.rgb);
            } else if (u_stage1_op == 8) { // ADDSIGNED
                 // AddSigned: dest + src - 0.5 (biased additive blend)
                 final_color.rgb = clamp(final_color.rgb + lightmap.rgb - 0.5, 0.0, 1.0);
            } else if (u_stage1_op == 6) { // MODULATE4X
                 // Match original shader: saturate(SceneColor * Lightmap * 4)
                 // This prevents white-out in bright areas while keeping dark areas dark
                 final_color.rgb = clamp(final_color.rgb * lightmap.rgb * 4.0, 0.0, 1.0);
            } else if (u_stage1_op == 5) { // MODULATE2X
                 final_color.rgb = clamp(final_color.rgb * lightmap.rgb * 2.0, 0.0, 1.0);
            } else { // Default/MODULATE (4)
                 final_color.rgb *= lightmap.rgb;
            }
        }
        
        if (u_alpha_test_enabled) {
            if (final_color.a < (u_alpha_ref / 255.0)) {
                discard;
            }
        }

        gl_FragColor = final_color;
    }
)";

// Shader program
static GLuint g_shader_program = 0;
static GLint g_position_location = -1;
static GLint g_color_location = -1;
static GLint g_texcoord_location = -1;
static GLint g_texcoord1_location = -1;
static GLint g_texture_uniform = -1;
static GLint g_texture_lm_uniform = -1;
static GLint g_use_texture_uniform = -1;
static GLint g_use_lightmap_uniform = -1;
static GLint g_mvp_uniform = -1;
static GLint g_tex_matrix_uniform = -1;
static GLint g_use_tex_matrix_uniform = -1;
static GLint g_clip_plane_uniform = -1;
static GLint g_clip_plane_enabled_uniform = -1;
static GLint g_fbo_flip_y_uniform = -1;
static GLint g_stage0_colorop_uniform = -1;
static GLint g_stage0_colorarg1_uniform = -1;
static GLint g_stage0_colorarg2_uniform = -1;
static GLint g_stage0_alphaop_uniform = -1;
static GLint g_stage0_alphaarg1_uniform = -1;
static GLint g_stage0_alphaarg2_uniform = -1;
static GLint g_stage1_op_uniform = -1;
static GLint g_alpha_test_enabled_uniform = -1;
static GLint g_alpha_ref_uniform = -1;
static GLint g_texture_factor_uniform = -1;
static GLint g_lod_bias_uniform = -1;

// FFP Lighting uniforms
static GLint g_normal_location = -1;
static GLint g_world_uniform = -1;
static GLint g_lighting_enabled_uniform = -1;
static GLint g_material_diffuse_uniform = -1;
static GLint g_material_ambient_uniform = -1;
static GLint g_global_ambient_uniform = -1;
static GLint g_light0_enabled_uniform = -1;
static GLint g_light0_position_uniform = -1;
static GLint g_light0_diffuse_uniform = -1;
static GLint g_light0_ambient_uniform = -1;
static GLint g_light0_attenuation_uniform = -1;
static GLint g_light0_range_uniform = -1;
static GLint g_light1_enabled_uniform = -1;
static GLint g_light1_position_uniform = -1;
static GLint g_light1_diffuse_uniform = -1;
static GLint g_light1_ambient_uniform = -1;
static GLint g_light1_attenuation_uniform = -1;
static GLint g_light1_range_uniform = -1;
static GLint g_light2_enabled_uniform = -1;
static GLint g_light2_position_uniform = -1;
static GLint g_light2_diffuse_uniform = -1;
static GLint g_light2_ambient_uniform = -1;
static GLint g_light2_attenuation_uniform = -1;
static GLint g_light2_range_uniform = -1;

// Current FVF
static DWORD g_current_fvf = 0;

// Current texture
static GLuint g_texture_stages[8] = { 0 };
static GLuint g_last_bound_texture_id = 0xFFFFFFFF;
static GLuint g_last_bound_lightmap_id = 0xFFFFFFFF;

// Track which GL textures have a complete mipmap chain
static std::unordered_set<GLuint> g_mipmapped_textures;

// Anisotropic filtering (EXT_texture_filter_anisotropic)
#ifndef GL_TEXTURE_MAX_ANISOTROPY_EXT
#define GL_TEXTURE_MAX_ANISOTROPY_EXT 0x84FE
#endif
#ifndef GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT
#define GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT 0x84FF
#endif
static float g_max_anisotropy = 1.0f; // 1.0 = disabled (no extension)

// FFP State storage
static D3DLIGHT9 g_lights[8];
static bool g_light_enabled[8] = {false};
static D3DMATERIAL9 g_material;
static DWORD g_current_stencil_ref = 0;

static void init_shaders();
static void EnsureUPBuffers();
static GLuint g_up_vbo = 0;
static GLuint g_up_ibo = 0;
static size_t g_up_vbo_size = 0;  // Track VBO size for persistent buffer optimization
static size_t g_up_ibo_size = 0;  // Track IBO size for persistent buffer optimization

// State Caching for optimization
// NOTE: g_cached_texture is unified with g_last_bound_texture_id to prevent cache
// desync between DrawIndexedPrimitive (uses g_last_bound_texture_id) and
// DrawIndexedPrimitiveUP/DrawPrimitiveUP (used g_cached_texture). The desync caused
// "texParameter: no texture bound" errors and black screen when semi-transparent
// characters drew via DrawIndexedPrimitiveUP, unbinding the texture without
// updating g_last_bound_texture_id.
static GLuint& g_cached_texture = g_last_bound_texture_id;
static GLuint g_cached_program = 0xFFFFFFFF;  // Last used shader program
static DWORD g_cached_fvf = 0xFFFFFFFF;       // Last FVF
static bool g_cached_vertex_attribs[4] = {false};  // pos, color, tex0, tex1

// Viewport and Scissor state caching (to avoid glGet* calls)
static GLint g_cached_viewport[4] = {0, 0, 0, 0};  // Overwritten by first SetViewport
static GLboolean g_cached_scissor_test = GL_FALSE;
static GLint g_cached_scissor_box[4] = {0, 0, 0, 0};  // Overwritten by first SetScissorRect

static DWORD g_texture_stage_states[8][33];
static D3DVIEWPORT9 g_viewport;
static int g_logical_width = 0;    // App's backbuffer resolution (set from D3DPRESENT_PARAMETERS)
static int g_logical_height = 0;

// Texture matrices
static D3DMATRIX g_texture_matrices[8];
// Sampler states [Sampler][Type]
static DWORD g_sampler_states[8][14]; // Up to D3DSAMP_DMAPOFFSET

// Transform matrices (for GetTransform)
D3DMATRIX g_transform_world;
#ifndef GL_COMPRESSED_RGB_S3TC_DXT1_EXT
#define GL_COMPRESSED_RGB_S3TC_DXT1_EXT 0x83F0
#endif
#ifndef GL_COMPRESSED_RGBA_S3TC_DXT1_EXT
#define GL_COMPRESSED_RGBA_S3TC_DXT1_EXT 0x83F1
#endif
#ifndef GL_COMPRESSED_RGBA_S3TC_DXT3_EXT
#define GL_COMPRESSED_RGBA_S3TC_DXT3_EXT 0x83F2
#endif
#ifndef GL_COMPRESSED_RGBA_S3TC_DXT5_EXT
#define GL_COMPRESSED_RGBA_S3TC_DXT5_EXT 0x83F3
#endif

// GLI library maps RGB DXT1 to a custom FOURCC 'GLI1'; treat like DXT1
#define D3DFMT_GLI1 ((D3DFORMAT)0x31494C47)

// Helper to check for DXT format
static bool IsCompressedFormat(D3DFORMAT format) {
    return (format == D3DFMT_DXT1 || format == D3DFMT_DXT3 || format == D3DFMT_DXT5
            || format == D3DFMT_GLI1);
}

static UINT GetFormatBpp(D3DFORMAT format) {
    switch (format) {
        case D3DFMT_A8R8G8B8:
        case D3DFMT_X8R8G8B8:
        case D3DFMT_A8B8G8R8:
        case D3DFMT_X8B8G8R8:
            return 32;
        case D3DFMT_R8G8B8:
            return 24;
        case D3DFMT_R5G6B5:
        case D3DFMT_A4R4G4B4:
        case D3DFMT_A1R5G5B5:
        case D3DFMT_INDEX16:
            return 16;
        default:
            return 32;
    }
}

// Convert D3D DXT format to GL extension enum
// D3D9 DXT1 always returns alpha=1.0, so use GL_COMPRESSED_RGB (not RGBA) for DXT1
// to match D3D9 behavior. RGBA variant has 1-bit punch-through alpha which causes
// unexpected alpha=0 in WebGL for textures that were designed as opaque.
GLenum GetGLCompressedFormat(D3DFORMAT fmt) {
    switch (fmt) {
        case D3DFMT_DXT1: return GL_COMPRESSED_RGB_S3TC_DXT1_EXT;
        case D3DFMT_GLI1: return GL_COMPRESSED_RGB_S3TC_DXT1_EXT; // GLI's RGB DXT1
        case D3DFMT_DXT3: return GL_COMPRESSED_RGBA_S3TC_DXT3_EXT;
        case D3DFMT_DXT5: return GL_COMPRESSED_RGBA_S3TC_DXT5_EXT;
        default: return 0;
    }
}


// Debugging helper
#ifdef DEBUG_TEXTURE_TEST
#define CHECK_GL_ERROR(op) { op; CheckGLError(#op); }
#else
#define CHECK_GL_ERROR(op) op
#endif

// Correct CheckGLError implementation
void CheckGLError(const char* label);

// Manual UP buffers (Moved up for RestoreContext)

void EnsureUPBuffers() {
    if (g_up_vbo == 0) {
        glGenBuffers(1, &g_up_vbo);
    }
    if (g_up_ibo == 0) {
        glGenBuffers(1, &g_up_ibo);
    }
}

// Check for OpenGL errors (silent in release builds)
void CheckGLError(const char* label) {
    GLenum err = glGetError();
    while (err != GL_NO_ERROR) {
        // Silently consume errors
        err = glGetError();
    }
}


static D3DMATRIX g_transform_view;
static D3DMATRIX g_transform_projection;

// Helper: Compile shader
static GLuint compile_shader(GLenum type, const char* source) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    
    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char info_log[512];
        glGetShaderInfoLog(shader, 512, nullptr, info_log);
        printf("Shader compilation failed: %s\n", info_log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

// Helper: Initialize shaders
static void init_shaders() {
    if (g_shader_program != 0) return;
    
    // Initialize default sampler states
    for (int i = 0; i < 8; ++i) {
        g_sampler_states[i][D3DSAMP_ADDRESSU] = D3DTADDRESS_WRAP;
        g_sampler_states[i][D3DSAMP_ADDRESSV] = D3DTADDRESS_WRAP;
        g_sampler_states[i][D3DSAMP_ADDRESSW] = D3DTADDRESS_WRAP;
        g_sampler_states[i][D3DSAMP_MINFILTER] = D3DTEXF_LINEAR;
        g_sampler_states[i][D3DSAMP_MAGFILTER] = D3DTEXF_LINEAR;
        g_sampler_states[i][D3DSAMP_MIPFILTER] = D3DTEXF_NONE;  // D3D9 default; engine sets LINEAR for gameplay
    }

    GLuint vs = compile_shader(GL_VERTEX_SHADER, vertex_shader_source);
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, fragment_shader_source);
    
    g_shader_program = glCreateProgram();
    glAttachShader(g_shader_program, vs);
    glAttachShader(g_shader_program, fs);
    glLinkProgram(g_shader_program);
    
    GLint success;
    glGetProgramiv(g_shader_program, GL_LINK_STATUS, &success);
    if (!success) {
        char info_log[512];
        glGetProgramInfoLog(g_shader_program, 512, nullptr, info_log);
        printf("Shader linking failed: %s\n", info_log);
    }
    
    g_position_location = glGetAttribLocation(g_shader_program, "a_position");
    g_normal_location = glGetAttribLocation(g_shader_program, "a_normal");
    g_color_location = glGetAttribLocation(g_shader_program, "a_color");
    g_texcoord_location = glGetAttribLocation(g_shader_program, "a_texcoord");
    g_texcoord1_location = glGetAttribLocation(g_shader_program, "a_texcoord1");
    g_texture_uniform = glGetUniformLocation(g_shader_program, "u_texture");
    g_texture_lm_uniform = glGetUniformLocation(g_shader_program, "u_texture_lm");
    g_use_texture_uniform = glGetUniformLocation(g_shader_program, "u_use_texture");
    g_use_lightmap_uniform = glGetUniformLocation(g_shader_program, "u_use_lightmap");
    g_mvp_uniform = glGetUniformLocation(g_shader_program, "u_mvp");
    g_world_uniform = glGetUniformLocation(g_shader_program, "u_world");
    g_tex_matrix_uniform = glGetUniformLocation(g_shader_program, "u_tex_matrix");
    g_use_tex_matrix_uniform = glGetUniformLocation(g_shader_program, "u_use_tex_matrix");
    g_clip_plane_uniform = glGetUniformLocation(g_shader_program, "u_clip_plane");
    g_clip_plane_enabled_uniform = glGetUniformLocation(g_shader_program, "u_clip_plane_enabled");
    g_fbo_flip_y_uniform = glGetUniformLocation(g_shader_program, "u_fbo_flip_y");
    g_stage0_colorop_uniform = glGetUniformLocation(g_shader_program, "u_stage0_colorop");
    g_stage0_colorarg1_uniform = glGetUniformLocation(g_shader_program, "u_stage0_colorarg1");
    g_stage0_colorarg2_uniform = glGetUniformLocation(g_shader_program, "u_stage0_colorarg2");
    g_stage0_alphaop_uniform = glGetUniformLocation(g_shader_program, "u_stage0_alphaop");
    g_stage0_alphaarg1_uniform = glGetUniformLocation(g_shader_program, "u_stage0_alphaarg1");
    g_stage0_alphaarg2_uniform = glGetUniformLocation(g_shader_program, "u_stage0_alphaarg2");
    g_stage1_op_uniform = glGetUniformLocation(g_shader_program, "u_stage1_op");
    g_alpha_test_enabled_uniform = glGetUniformLocation(g_shader_program, "u_alpha_test_enabled");
    g_alpha_ref_uniform = glGetUniformLocation(g_shader_program, "u_alpha_ref");
    g_texture_factor_uniform = glGetUniformLocation(g_shader_program, "u_texture_factor");
    g_lod_bias_uniform = glGetUniformLocation(g_shader_program, "u_lod_bias");
    // FFP Lighting uniforms
    g_lighting_enabled_uniform = glGetUniformLocation(g_shader_program, "u_lighting_enabled");
    g_material_diffuse_uniform = glGetUniformLocation(g_shader_program, "u_material_diffuse");
    g_material_ambient_uniform = glGetUniformLocation(g_shader_program, "u_material_ambient");
    g_global_ambient_uniform = glGetUniformLocation(g_shader_program, "u_global_ambient");
    g_light0_enabled_uniform = glGetUniformLocation(g_shader_program, "u_light0_enabled");
    g_light0_position_uniform = glGetUniformLocation(g_shader_program, "u_light0_position");
    g_light0_diffuse_uniform = glGetUniformLocation(g_shader_program, "u_light0_diffuse");
    g_light0_ambient_uniform = glGetUniformLocation(g_shader_program, "u_light0_ambient");
    g_light0_attenuation_uniform = glGetUniformLocation(g_shader_program, "u_light0_attenuation");
    g_light0_range_uniform = glGetUniformLocation(g_shader_program, "u_light0_range");
    g_light1_enabled_uniform = glGetUniformLocation(g_shader_program, "u_light1_enabled");
    g_light1_position_uniform = glGetUniformLocation(g_shader_program, "u_light1_position");
    g_light1_diffuse_uniform = glGetUniformLocation(g_shader_program, "u_light1_diffuse");
    g_light1_ambient_uniform = glGetUniformLocation(g_shader_program, "u_light1_ambient");
    g_light1_attenuation_uniform = glGetUniformLocation(g_shader_program, "u_light1_attenuation");
    g_light1_range_uniform = glGetUniformLocation(g_shader_program, "u_light1_range");
    g_light2_enabled_uniform = glGetUniformLocation(g_shader_program, "u_light2_enabled");
    g_light2_position_uniform = glGetUniformLocation(g_shader_program, "u_light2_position");
    g_light2_diffuse_uniform = glGetUniformLocation(g_shader_program, "u_light2_diffuse");
    g_light2_ambient_uniform = glGetUniformLocation(g_shader_program, "u_light2_ambient");
    g_light2_attenuation_uniform = glGetUniformLocation(g_shader_program, "u_light2_attenuation");
    g_light2_range_uniform = glGetUniformLocation(g_shader_program, "u_light2_range");
    
    glDeleteShader(vs);
    glDeleteShader(fs);
}

// Helper: Determinant of upper-left 3x3 sub-matrix (for orientation/winding detection)
// D3D9 automatically detects mirrored transforms (negative determinant) and flips culling.
// WebGL doesn't, so we compute this per draw call to set glFrontFace correctly.
static float det3x3(const D3DMATRIX& m) {
    return m.m[0][0] * (m.m[1][1] * m.m[2][2] - m.m[1][2] * m.m[2][1])
         - m.m[0][1] * (m.m[1][0] * m.m[2][2] - m.m[1][2] * m.m[2][0])
         + m.m[0][2] * (m.m[1][0] * m.m[2][1] - m.m[1][1] * m.m[2][0]);
}

// Helper: Matrix multiplication
static void matrix_multiply(D3DMATRIX* result, const D3DMATRIX* a, const D3DMATRIX* b) {
    D3DMATRIX temp;
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            temp.m[i][j] = 0;
            for (int k = 0; k < 4; k++) {
                temp.m[i][j] += a->m[i][k] * b->m[k][j];
            }
        }
    }
    *result = temp;
}

static void matrix_identity(D3DMATRIX* m) {
    memset(m, 0, sizeof(D3DMATRIX));
    m->m[0][0] = 1.0f;
    m->m[1][1] = 1.0f;
    m->m[2][2] = 1.0f;
    m->m[3][3] = 1.0f;
}

// Helper: Set matrix uniform with CPU-side transpose
// Helper: Set matrix uniform. 
// [IMPORTANT] D3D memory (Row-Major, v*M) is binary-equivalent to GL (Column-Major, M*v).
// Do NOT transpose manually here if using standard D3D matrix math.
static void gl_set_matrix_uniform(GLint location, const D3DMATRIX* matrix) {
    if (location == -1 || !matrix) return;
    glUniformMatrix4fv(location, 1, GL_FALSE, (const float*)matrix);
}

// Helper: Set FFP lighting uniforms (material, ambient, lights 0-2)
static void SetFFPLightingUniforms(const D3DMATRIX* world, const DWORD* render_states) {
    gl_set_matrix_uniform(g_world_uniform, world);

    glUniform4f(g_material_diffuse_uniform,
        g_material.Diffuse.r, g_material.Diffuse.g, g_material.Diffuse.b, g_material.Diffuse.a);
    glUniform4f(g_material_ambient_uniform,
        g_material.Ambient.r, g_material.Ambient.g, g_material.Ambient.b, g_material.Ambient.a);

    DWORD amb = render_states[D3DRS_AMBIENT];
    glUniform3f(g_global_ambient_uniform,
        ((amb >> 16) & 0xFF) / 255.0f,
        ((amb >> 8) & 0xFF) / 255.0f,
        (amb & 0xFF) / 255.0f);

    // Light 0
    if (g_light_enabled[0]) {
        glUniform1i(g_light0_enabled_uniform, 1);
        glUniform3f(g_light0_position_uniform, g_lights[0].Position.x, g_lights[0].Position.y, g_lights[0].Position.z);
        glUniform4f(g_light0_diffuse_uniform, g_lights[0].Diffuse.r, g_lights[0].Diffuse.g, g_lights[0].Diffuse.b, g_lights[0].Diffuse.a);
        glUniform3f(g_light0_ambient_uniform, g_lights[0].Ambient.r, g_lights[0].Ambient.g, g_lights[0].Ambient.b);
        glUniform3f(g_light0_attenuation_uniform, g_lights[0].Attenuation0, g_lights[0].Attenuation1, g_lights[0].Attenuation2);
        glUniform1f(g_light0_range_uniform, g_lights[0].Range);
    } else {
        glUniform1i(g_light0_enabled_uniform, 0);
    }

    // Light 1
    if (g_light_enabled[1]) {
        glUniform1i(g_light1_enabled_uniform, 1);
        glUniform3f(g_light1_position_uniform, g_lights[1].Position.x, g_lights[1].Position.y, g_lights[1].Position.z);
        glUniform4f(g_light1_diffuse_uniform, g_lights[1].Diffuse.r, g_lights[1].Diffuse.g, g_lights[1].Diffuse.b, g_lights[1].Diffuse.a);
        glUniform3f(g_light1_ambient_uniform, g_lights[1].Ambient.r, g_lights[1].Ambient.g, g_lights[1].Ambient.b);
        glUniform3f(g_light1_attenuation_uniform, g_lights[1].Attenuation0, g_lights[1].Attenuation1, g_lights[1].Attenuation2);
        glUniform1f(g_light1_range_uniform, g_lights[1].Range);
    } else {
        glUniform1i(g_light1_enabled_uniform, 0);
    }

    // Light 2
    if (g_light_enabled[2]) {
        glUniform1i(g_light2_enabled_uniform, 1);
        glUniform3f(g_light2_position_uniform, g_lights[2].Position.x, g_lights[2].Position.y, g_lights[2].Position.z);
        glUniform4f(g_light2_diffuse_uniform, g_lights[2].Diffuse.r, g_lights[2].Diffuse.g, g_lights[2].Diffuse.b, g_lights[2].Diffuse.a);
        glUniform3f(g_light2_ambient_uniform, g_lights[2].Ambient.r, g_lights[2].Ambient.g, g_lights[2].Ambient.b);
        glUniform3f(g_light2_attenuation_uniform, g_lights[2].Attenuation0, g_lights[2].Attenuation1, g_lights[2].Attenuation2);
        glUniform1f(g_light2_range_uniform, g_lights[2].Range);
    } else {
        glUniform1i(g_light2_enabled_uniform, 0);
    }
}

// Global FBO for SetRenderTarget
static GLuint g_fbo = 0;

// D3D9Surface class
class D3D9Surface : public IDirect3DSurface9 {
public:
    GLuint m_gl_object;      // Texture ID or Renderbuffer ID
    bool m_is_renderbuffer;  // True if Renderbuffer (Depth), False if Texture
    bool m_is_backbuffer;    // True if this represents the default backbuffer (Canvas)
    UINT m_width;
    UINT m_height;
    ULONG m_refCount;        // Reference count for COM-style lifetime management

    D3D9Surface(UINT width, UINT height, GLuint gl_obj, bool is_rb, bool is_bb = false)
        : m_width(width), m_height(height), m_gl_object(gl_obj),
          m_is_renderbuffer(is_rb), m_is_backbuffer(is_bb), m_refCount(1) {}
    
    virtual ~D3D9Surface() {
        if (m_is_renderbuffer && m_gl_object != 0 && !m_is_backbuffer) {
            glDeleteRenderbuffers(1, &m_gl_object);
        }
        // If it's a texture surface, D3D9Texture owns the GL object, so we don't delete it here.
    }

    virtual ULONG AddRef() override {
        return ++m_refCount;
    }

    virtual ULONG Release() override {
        ULONG ref = --m_refCount;
        if (ref == 0) {
            delete this;
        }
        return ref;
    }
    
    virtual HRESULT LockRect(D3DLOCKED_RECT* pLockedRect, const RECT* pRect, DWORD Flags) override {
        // Not implemented for Surfaces yet (usually needed for CPU access to render targets)
        return D3DERR_INVALIDCALL;
    }
    
    virtual HRESULT UnlockRect() override {
        return D3DERR_INVALIDCALL;
    }
    
    virtual HRESULT GetDesc(D3DSURFACE_DESC* pDesc) override {
        if (!pDesc) return D3DERR_INVALIDCALL;
        memset(pDesc, 0, sizeof(D3DSURFACE_DESC));
        pDesc->Width = m_width;
        pDesc->Height = m_height;
        pDesc->Format = D3DFMT_X8R8G8B8; // Default format
        pDesc->Type = m_is_renderbuffer ? D3DRTYPE_SURFACE : D3DRTYPE_TEXTURE;
        pDesc->Pool = D3DPOOL_DEFAULT;
        return D3D_OK;
    }
};

// D3D9Texture class
class D3D9Texture : public IDirect3DTexture9 {
public:
    GLuint m_texture;
    UINT m_width;
    UINT m_height;
    UINT m_levels;
    D3DFORMAT m_format;
    void* m_locked_data;
    
public:
    D3D9Texture(UINT width, UINT height, UINT levels, D3DFORMAT format)
        : m_width(width), m_height(height), m_levels(levels), m_format(format), m_locked_data(nullptr) {
        glGenTextures(1, &m_texture);
        glBindTexture(GL_TEXTURE_2D, m_texture);
        // Invalidate ALL texture binding caches since we bound outside of draw calls
        g_last_bound_texture_id = 0xFFFFFFFF;
        g_last_bound_lightmap_id = 0xFFFFFFFF;
        g_cached_texture = 0xFFFFFFFF;

        // [WASM] Use GL_LINEAR as default to avoid "Texture Incomplete" if no mipmaps provided
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        // [WASM] Restrict to Level 0 only until mip levels are uploaded/generated.
        // Prevents "Texture Incomplete" when mipmap filter is applied to a texture
        // that hasn't had its full mip chain uploaded yet.
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);

        // [WASM] Use CLAMP_TO_EDGE which is required for NPOT textures in WebGL 1.0
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    
    virtual ~D3D9Texture() {
        if (m_texture) {
            // Invalidate caches if this texture was cached (ID may be recycled by GL)
            if (g_last_bound_texture_id == m_texture) g_last_bound_texture_id = 0xFFFFFFFF;
            if (g_last_bound_lightmap_id == m_texture) g_last_bound_lightmap_id = 0xFFFFFFFF;
            if (g_cached_texture == m_texture) g_cached_texture = 0xFFFFFFFF;
            g_mipmapped_textures.erase(m_texture);
            glDeleteTextures(1, &m_texture);
        }
        if (m_locked_data) {
            delete[] static_cast<uint8_t*>(m_locked_data);
        }
    }

    virtual ULONG AddRef() override {
        return 1; // Stub: no ref counting for textures (owned by game code)
    }

    virtual ULONG Release() override {
        delete this;
        return 0;
    }
    
    virtual HRESULT GetSurfaceLevel(UINT Level, IDirect3DSurface9** ppSurfaceLevel) override {
        if (!ppSurfaceLevel) return D3DERR_INVALIDCALL;
        if (Level != 0) return D3DERR_INVALIDCALL; // Only level 0 supported for now
        
        // Return a Surface wrapper for this texture
        *ppSurfaceLevel = new D3D9Surface(m_width, m_height, m_texture, false);
        return D3D_OK;
    }
    
    virtual HRESULT LockRect(UINT Level, D3DLOCKED_RECT* pLockedRect,
                            const void* pRect, DWORD Flags) override {
        if (!pLockedRect) return -1;
        if (m_locked_data) return -1; // Already locked - prevent memory leak

        // Calculate mip dimensions
        UINT mip_width = std::max(1u, m_width >> Level);
        UINT mip_height = std::max(1u, m_height >> Level);

        // Calculate size for allocation
        size_t data_size = 0;
        if (IsCompressedFormat(m_format)) {
            // DXT1/GLI1: 8 bytes per 4x4 block
            // DXT3/5: 16 bytes per 4x4 block
            size_t block_size = (m_format == D3DFMT_DXT1 || m_format == D3DFMT_GLI1) ? 8 : 16;
            size_t blocks_w = (mip_width + 3) / 4;
            size_t blocks_h = (mip_height + 3) / 4;
            data_size = blocks_w * blocks_h * block_size;
            pLockedRect->Pitch = blocks_w * block_size;
        } else {
            // Uncompressed
            UINT bpp = GetFormatBpp(m_format);
            pLockedRect->Pitch = (mip_width * bpp + 7) / 8;
            data_size = pLockedRect->Pitch * mip_height;
        }

        m_locked_data = new uint8_t[data_size];
        pLockedRect->pBits = m_locked_data;

        return D3D_OK;
    }
    
    virtual HRESULT UnlockRect(UINT Level) override {
        if (!m_locked_data) return -1;
        
        glBindTexture(GL_TEXTURE_2D, m_texture);
        // Invalidate ALL texture binding caches since we bound outside of draw calls
        g_last_bound_texture_id = 0xFFFFFFFF;
        g_last_bound_lightmap_id = 0xFFFFFFFF;
        g_cached_texture = 0xFFFFFFFF;

        // Calculate mip dimensions again
        UINT mip_width = std::max(1u, m_width >> Level);
        UINT mip_height = std::max(1u, m_height >> Level);

        if (IsCompressedFormat(m_format)) {
            // Compressed upload
            GLenum gl_fmt = GetGLCompressedFormat(m_format);

            size_t block_size = (m_format == D3DFMT_DXT1 || m_format == D3DFMT_GLI1) ? 8 : 16;
            size_t size = ((mip_width + 3) / 4) * ((mip_height + 3) / 4) * block_size;

            CHECK_GL_ERROR(glCompressedTexImage2D(GL_TEXTURE_2D, Level, gl_fmt, mip_width, mip_height, 0, size, m_locked_data));
        } 
        else 
        {
            // Uncompressed upload: Convert to RGBA32 for WebGL
            size_t pixel_count = mip_width * mip_height;
            std::vector<uint32_t> rgba_pixels(pixel_count);
            
            if (m_format == D3DFMT_A8R8G8B8 || m_format == D3DFMT_X8R8G8B8) {
                uint32_t* src = static_cast<uint32_t*>(m_locked_data);
                for (size_t i = 0; i < pixel_count; ++i) {
                    uint32_t color = src[i];
                    uint32_t b = (color >> 0) & 0xFF;
                    uint32_t g = (color >> 8) & 0xFF;
                    uint32_t r = (color >> 16) & 0xFF;
                    uint32_t a = (color >> 24) & 0xFF;
                    // Only force opaque for X8R8G8B8. A8R8G8B8 must respect actual alpha.
                    if (m_format == D3DFMT_X8R8G8B8) a = 0xFF;
                    rgba_pixels[i] = (r << 0) | (g << 8) | (b << 16) | (a << 24);
                }
            }
            else if (m_format == D3DFMT_A4R4G4B4) {
                uint16_t* src = static_cast<uint16_t*>(m_locked_data);
                for (size_t i = 0; i < pixel_count; ++i) {
                    uint16_t color = src[i];
                    uint32_t a = ((color >> 12) & 0xF); a = (a << 4) | a;
                    uint32_t r = ((color >>  8) & 0xF); r = (r << 4) | r;
                    uint32_t g = ((color >>  4) & 0xF); g = (g << 4) | g;
                    uint32_t b = ((color >>  0) & 0xF); b = (b << 4) | b;
                    rgba_pixels[i] = (r << 0) | (g << 8) | (b << 16) | (a << 24);
                }
            }
            else if (m_format == D3DFMT_A1R5G5B5 || m_format == 25 /* D3DFMT_X1R5G5B5 */) {
                uint16_t* src = static_cast<uint16_t*>(m_locked_data);
                for (size_t i = 0; i < pixel_count; ++i) {
                    uint16_t color = src[i];
                    uint32_t r = ((color >> 10) & 0x1F); r = (r << 3) | (r >> 2);
                    uint32_t g = ((color >> 5) & 0x1F);  g = (g << 3) | (g >> 2);
                    uint32_t b = ((color >> 0) & 0x1F);  b = (b << 3) | (b >> 2);
                    uint32_t a = (m_format == D3DFMT_A1R5G5B5) ? (((color >> 15) & 0x1) ? 255 : 0) : 255;
                    rgba_pixels[i] = (r << 0) | (g << 8) | (b << 16) | (a << 24);
                }
            }
            else if (m_format == D3DFMT_R5G6B5) {
                uint16_t* src = static_cast<uint16_t*>(m_locked_data);
                for (size_t i = 0; i < pixel_count; ++i) {
                    uint16_t color = src[i];
                    uint32_t r = ((color >> 11) & 0x1F) << 3;
                    uint32_t g = ((color >> 5) & 0x3F) << 2;
                    uint32_t b = ((color >> 0) & 0x1F) << 3;
                    rgba_pixels[i] = (r << 0) | (g << 8) | (b << 16) | 0xFF000000;
                }
            }
            else if (m_format == D3DFMT_R8G8B8) {
                // 24-bit BGR (D3D R8G8B8 stores B,G,R byte order)
                printf("[WARN] D3DFMT_R8G8B8 texture uploaded (%ux%u) - untested path\n",
                       mip_width, mip_height);
                uint8_t* src = static_cast<uint8_t*>(m_locked_data);
                for (size_t i = 0; i < pixel_count; ++i) {
                    uint32_t b = src[0];
                    uint32_t g = src[1];
                    uint32_t r = src[2];
                    rgba_pixels[i] = (r << 0) | (g << 8) | (b << 16) | 0xFF000000;
                    src += 3;
                }
            }
            else {
                // Fallback for other formats
                uint32_t* src = static_cast<uint32_t*>(m_locked_data);
                for (size_t i = 0; i < pixel_count; ++i) {
                    rgba_pixels[i] = src[i];
                }
            }
            
            glTexImage2D(GL_TEXTURE_2D, Level, GL_RGBA, mip_width, mip_height, 0,
                        GL_RGBA, GL_UNSIGNED_BYTE, rgba_pixels.data());


        }
        
        // Update mipmap state: expand GL_TEXTURE_MAX_LEVEL as levels are uploaded
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, Level);

        if (Level > 0) {
            // Compressed textures with pre-baked mip chain (DDS files)
            g_mipmapped_textures.insert(m_texture);
        } else if (!IsCompressedFormat(m_format)) {
            // Non-compressed Level 0: auto-generate full mip chain.
            // glGenerateMipmap cannot operate on compressed formats, but works
            // for RGBA/etc. This ensures UI textures and other non-DXT assets
            // also have mipmaps, preventing "Texture Incomplete" when the engine
            // sets MIPFILTER=LINEAR globally.
            int max_level = (int)std::floor(std::log2((double)std::max(m_width, m_height)));
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, max_level);
            glGenerateMipmap(GL_TEXTURE_2D);
            g_mipmapped_textures.insert(m_texture);
        }

        // Free the locked data
        delete[] static_cast<uint8_t*>(m_locked_data);
        m_locked_data = nullptr;

        return D3D_OK;
    }
    
    GLuint GetGLTexture() const { return m_texture; }
};

// D3D9IndexBuffer class
class D3D9IndexBuffer : public IDirect3DIndexBuffer9 {
private:
    GLuint m_buffer;
    UINT m_length;
    D3DFORMAT m_format;
    void* m_locked_data;
    UINT m_locked_size;
    UINT m_locked_offset;

public:
    D3D9IndexBuffer(UINT length, D3DFORMAT format) : m_length(length), m_format(format), m_locked_data(nullptr), m_locked_size(0), m_locked_offset(0) {
        glGenBuffers(1, &m_buffer);
        // Initialize buffer size
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_buffer);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, length, nullptr, GL_STATIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    }
    
    virtual ~D3D9IndexBuffer() {
        if (m_buffer) {
            glDeleteBuffers(1, &m_buffer);
        }
        if (m_locked_data) {
            delete[] static_cast<uint8_t*>(m_locked_data);
        }
    }

    virtual ULONG AddRef() override {
        return 1; // Stub: no ref counting for index buffers
    }

    virtual ULONG Release() override {
        delete this;
        return 0;
    }
    
    virtual HRESULT Lock(UINT OffsetToLock, UINT SizeToLock, void** ppbData, DWORD Flags) override {
        if (!ppbData) return -1;
        if (m_locked_data) return -1; // Already locked
        
        // Simple implementation: always lock the whole buffer or requested size
        UINT size = (SizeToLock == 0) ? (m_length - OffsetToLock) : SizeToLock;
        m_locked_data = new uint8_t[size];
        m_locked_size = size;
        m_locked_offset = OffsetToLock;
        *ppbData = m_locked_data;
        
        return D3D_OK;
    }
    
    virtual HRESULT Unlock() override {
        if (!m_locked_data) return -1;
        
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_buffer);
        // Note: This implementation re-uploads only the locked part to the correct offset.
        glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, m_locked_offset, m_locked_size, m_locked_data);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
        
        delete[] static_cast<uint8_t*>(m_locked_data);
        m_locked_data = nullptr;
        return D3D_OK;
    }

    GLuint GetGLBuffer() const { return m_buffer; }
    D3DFORMAT GetFormat() const { return m_format; }
};

// D3D9VertexBuffer class
class D3D9VertexBuffer : public IDirect3DVertexBuffer9 {
private:
    GLuint m_buffer;
    UINT m_length;
    void* m_locked_data;
    UINT m_locked_size;
    UINT m_locked_offset;
    
public:
    D3D9VertexBuffer(UINT length) : m_length(length), m_locked_data(nullptr), m_locked_size(0), m_locked_offset(0) {
        glGenBuffers(1, &m_buffer);
        glBindBuffer(GL_ARRAY_BUFFER, m_buffer);
        glBufferData(GL_ARRAY_BUFFER, length, nullptr, GL_STATIC_DRAW);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    }
    
    virtual ~D3D9VertexBuffer() {
        if (m_buffer) {
            glDeleteBuffers(1, &m_buffer);
        }
        if (m_locked_data) {
            delete[] static_cast<uint8_t*>(m_locked_data);
        }
    }

    virtual ULONG AddRef() override {
        return 1; // Stub: no ref counting for vertex buffers
    }

    virtual ULONG Release() override {
        delete this;
        return 0;
    }
    
    virtual HRESULT Lock(UINT OffsetToLock, UINT SizeToLock, void** ppbData, DWORD Flags) override {
        if (!ppbData) return -1;
        if (m_locked_data) return -1; // Already locked
        
        UINT size = (SizeToLock == 0) ? (m_length - OffsetToLock) : SizeToLock;
        m_locked_data = new uint8_t[size];
        m_locked_size = size;
        m_locked_offset = OffsetToLock;
        *ppbData = m_locked_data;
        
        return D3D_OK;
    }
    
    virtual HRESULT Unlock() override {
        if (!m_locked_data) return -1;
        
        glBindBuffer(GL_ARRAY_BUFFER, m_buffer);
        glBufferSubData(GL_ARRAY_BUFFER, m_locked_offset, m_locked_size, m_locked_data);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        
        delete[] static_cast<uint8_t*>(m_locked_data);
        m_locked_data = nullptr;
        return D3D_OK;
    }
    
    GLuint GetGLBuffer() const { return m_buffer; }
};

// D3D9StateBlock class
class D3D9StateBlock : public IDirect3DStateBlock9 {
public:
    virtual ~D3D9StateBlock() {}
    virtual ULONG AddRef() override {
        return 1; // Stub: no ref counting for state blocks
    }
    virtual ULONG Release() override {
        delete this;
        return 0;
    }
    virtual HRESULT Capture() override {
        return D3D_OK;
    }
    virtual HRESULT Apply() override {
        return D3D_OK;
    }
};

// Implementation classes
class D3D9Device : public IDirect3DDevice9 {
private:
    D3DMATRIX m_world;
    D3DMATRIX m_view;
    D3DMATRIX m_projection;
    GLuint m_current_index_buffer;
    D3DFORMAT m_current_ib_format;
    GLuint m_current_vertex_buffer;
    UINT m_current_stride;
    
    D3D9Surface* m_backbuffer_surface;
    D3D9Surface* m_current_rt;
    D3D9Surface* m_current_ds;
    
    // Clip Planes
    float m_clip_planes[1][4]; // Support 1 clip plane
    bool m_clip_plane_enabled = false;

    // FBO rendering state (for D3D9/OpenGL Y-convention fix)
    bool m_fbo_rendering = false;

    // State Tracking
    GLenum m_src_blend = GL_ONE;
    GLenum m_dst_blend = GL_ZERO;
    DWORD m_alpha_ref = 0;
    bool m_alpha_test_enabled = false;
    D3DCOLOR m_texture_factor;
    
    // Debug: Flags to suppress repetitive shader warnings (performance optimization)
    static bool s_pixel_shader_warning_shown;
    static bool s_vertex_shader_warning_shown;
    static bool s_vertex_shader_constant_warning_shown;
    RECT m_scissor_rect = {};

public:
    D3D9Device() : m_current_index_buffer(0), m_current_ib_format(D3DFMT_INDEX16), m_current_vertex_buffer(0), m_current_stride(0), m_texture_factor(0xFFFFFFFF) {
        init_shaders();
        
        g_viewport.X = 0;
        g_viewport.Y = 0;
        g_viewport.Width = g_logical_width;
        g_viewport.Height = g_logical_height;
        g_viewport.MinZ = 0.0f;
        g_viewport.MaxZ = 1.0f;

        if (g_fbo == 0) glGenFramebuffers(1, &g_fbo);

        m_scissor_rect = {0, 0, (LONG)g_logical_width, (LONG)g_logical_height};

        // Backbuffer surface matches the app's backbuffer resolution
        m_backbuffer_surface = new D3D9Surface(g_logical_width, g_logical_height, 0, false, true);
        m_current_rt = m_backbuffer_surface;
        m_current_ds = nullptr; // Default DS is implicit in WebGL context
        
        // Initialize matrices to identity
        matrix_identity(&m_world);
        matrix_identity(&m_view);
        matrix_identity(&m_projection);
        for (int i = 0; i < 8; i++) {
            matrix_identity(&g_texture_matrices[i]);
            // Default sampler states
            g_sampler_states[i][D3DSAMP_ADDRESSU] = D3DTADDRESS_WRAP;
            g_sampler_states[i][D3DSAMP_ADDRESSV] = D3DTADDRESS_WRAP;
            g_sampler_states[i][D3DSAMP_MAGFILTER] = D3DTEXF_LINEAR;
            g_sampler_states[i][D3DSAMP_MINFILTER] = D3DTEXF_LINEAR;
            g_sampler_states[i][D3DSAMP_MIPFILTER] = D3DTEXF_NONE; // D3D9 default: no mip filtering

            // Default texture stage states (D3D9 defaults)
            if (i == 0) {
                g_texture_stage_states[i][D3DTSS_COLOROP] = D3DTOP_MODULATE;
                g_texture_stage_states[i][D3DTSS_ALPHAOP] = D3DTOP_SELECTARG1;
            } else {
                g_texture_stage_states[i][D3DTSS_COLOROP] = D3DTOP_DISABLE;
                g_texture_stage_states[i][D3DTSS_ALPHAOP] = D3DTOP_DISABLE;
            }
            g_texture_stage_states[i][D3DTSS_COLORARG1] = D3DTA_TEXTURE;
            g_texture_stage_states[i][D3DTSS_COLORARG2] = D3DTA_DIFFUSE;
            g_texture_stage_states[i][D3DTSS_ALPHAARG1] = D3DTA_TEXTURE;
            g_texture_stage_states[i][D3DTSS_ALPHAARG2] = D3DTA_DIFFUSE;
        }

        // Initialize state
        m_alpha_ref = 0;
        m_alpha_test_enabled = false;
        m_src_blend = GL_ONE;
        m_dst_blend = GL_ZERO;

        // Initialize render states with D3D9 defaults
        memset(m_render_states, 0, sizeof(m_render_states));
        m_render_states[D3DRS_LIGHTING]         = TRUE;             // D3D9 default: lighting enabled
        m_render_states[D3DRS_ZWRITEENABLE]     = TRUE;             // D3D9 default: depth write enabled
        m_render_states[D3DRS_COLORWRITEENABLE] = 0x0000000F;       // D3D9 default: all channels (R|G|B|A)
        m_render_states[D3DRS_ZFUNC]            = D3DCMP_LESSEQUAL; // D3D9 default: LESSEQUAL (not GL's LESS)

        // Initial viewport call
        SetViewport(&g_viewport);

        // Enable depth test
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LEQUAL); // D3D9 default ZFUNC is LESSEQUAL, not LESS
        glDepthMask(GL_TRUE);
        
        // [WASM FIX] D3D9 uses CW winding for front faces (left-handed)
        // OpenGL uses CCW by default (right-handed), so we flip it
        glFrontFace(GL_CW);

        // Disable culling initially (will be set via SetRenderState)
        glDisable(GL_CULL_FACE);
    }
    
    virtual ~D3D9Device() {
        // Release owned backbuffer surface
        if (m_backbuffer_surface) {
            m_backbuffer_surface->Release();
            m_backbuffer_surface = nullptr;
        }
        m_current_rt = nullptr;
        m_current_ds = nullptr;

        // Clean up GL resources owned by this device
        if (g_fbo) {
            glDeleteFramebuffers(1, &g_fbo);
            g_fbo = 0;
        }
        if (g_up_vbo) {
            glDeleteBuffers(1, &g_up_vbo);
            g_up_vbo = 0;
            g_up_vbo_size = 0;
        }
        if (g_up_ibo) {
            glDeleteBuffers(1, &g_up_ibo);
            g_up_ibo = 0;
            g_up_ibo_size = 0;
        }
        if (g_shader_program) {
            glDeleteProgram(g_shader_program);
            g_shader_program = 0;
        }
    }

    virtual ULONG AddRef() override {
        return 1; // Stub: no ref counting for device
    }

    virtual ULONG Release() override {
        delete this;
        return 0;
    }
    
    // Render target and depth stencil methods
    virtual HRESULT GetRenderTarget(DWORD Index, IDirect3DSurface9** ppRenderTarget) override {
        if (Index != 0) return D3DERR_INVALIDCALL;
        if (ppRenderTarget) {
            *ppRenderTarget = m_current_rt;
            if (m_current_rt) {
                m_current_rt->AddRef(); // Proper COM semantics: caller must Release
            }
        }
        return D3D_OK;
    }
    
    virtual HRESULT SetRenderTarget(DWORD Index, IDirect3DSurface9* pRenderTarget) override {
        if (Index != 0 || !pRenderTarget) return D3DERR_INVALIDCALL;

        D3D9Surface* surf = static_cast<D3D9Surface*>(pRenderTarget);
        m_current_rt = surf;

        if (surf->m_is_backbuffer) {
            glBindFramebuffer(GL_FRAMEBUFFER, 0); // Bind default
            m_fbo_rendering = false;
        } else {
            glBindFramebuffer(GL_FRAMEBUFFER, g_fbo);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, surf->m_gl_object, 0);
            m_fbo_rendering = true;
        }
        // Note: glFrontFace is set per draw call based on det(world*view) and Y-flip state,
        // because D3D9 auto-detects mirrored transforms but WebGL doesn't.

        // Reset viewport to match target size
        D3DVIEWPORT9 vp;
        vp.X = 0; vp.Y = 0;
        vp.Width = surf->m_width;
        vp.Height = surf->m_height;
        vp.MinZ = 0.0f; vp.MaxZ = 1.0f;
        SetViewport(&vp);

        return D3D_OK;
    }
    
    virtual HRESULT GetDepthStencilSurface(IDirect3DSurface9** ppZStencilSurface) override {
        if (ppZStencilSurface) {
            *ppZStencilSurface = m_current_ds;
            if (m_current_ds) {
                m_current_ds->AddRef(); // Proper COM semantics: caller must Release
            }
        }
        return D3D_OK;
    }
    
    virtual HRESULT SetDepthStencilSurface(IDirect3DSurface9* pZStencilSurface) override {
        // If backbuffer is active, we assume default DS. If FBO is active, we attach DS.
        m_current_ds = static_cast<D3D9Surface*>(pZStencilSurface);
        
        if (!m_current_rt->m_is_backbuffer) {
            glBindFramebuffer(GL_FRAMEBUFFER, g_fbo);
            if (m_current_ds) {
                glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, m_current_ds->m_gl_object);
            } else {
                glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, 0);
            }
        }
        return D3D_OK;
    }
    
    // Shader methods
    virtual HRESULT GetVertexShader(IDirect3DVertexShader9** ppShader) override {
        if (ppShader) *ppShader = nullptr;
        return D3D_OK;
    }
    
    virtual HRESULT GetPixelShader(IDirect3DPixelShader9** ppShader) override {
        if (ppShader) *ppShader = nullptr;
        return D3D_OK;
    }
    
    virtual HRESULT CreateVertexShader(const DWORD* pFunction, IDirect3DVertexShader9** ppShader) override {
        if (ppShader) *ppShader = nullptr;
        return D3D_OK;
    }
    
    virtual HRESULT CreatePixelShader(const DWORD* pFunction, IDirect3DPixelShader9** ppShader) override {
        if (ppShader) *ppShader = nullptr;
        return D3D_OK;
    }
    
    virtual HRESULT SetPixelShaderConstantF(UINT StartRegister, const float* pConstantData, UINT Vector4fCount) override {
        return D3D_OK;
    }
    
    // Drawing methods
    virtual HRESULT DrawPrimitive(D3DPRIMITIVETYPE PrimitiveType, UINT StartVertex, UINT PrimitiveCount) override {
        // Non-indexed primitive drawing using hardware VB (used by cloth rendering)
        if (m_current_vertex_buffer == 0) return -1;
        if (g_shader_program == 0) init_shaders();
        glUseProgram(g_shader_program);

        // FBO Y-flip and clip plane uniforms
        glUniform1i(g_fbo_flip_y_uniform, m_fbo_rendering ? 1 : 0);
        glUniform1i(g_clip_plane_enabled_uniform, m_clip_plane_enabled ? 1 : 0);
        if (m_clip_plane_enabled) glUniform4fv(g_clip_plane_uniform, 1, m_clip_planes[0]);

        D3DMATRIX view_model, mvp;
        if (g_current_fvf & D3DFVF_XYZRHW) {
            float vw = (g_viewport.Width > 0) ? (float)g_viewport.Width : (float)g_logical_width;
            float vh = (g_viewport.Height > 0) ? (float)g_viewport.Height : (float)g_logical_height;
            memset(&mvp, 0, sizeof(D3DMATRIX));
            mvp.m[0][0] = 2.0f / vw;
            mvp.m[1][1] = -2.0f / vh;
            mvp.m[2][2] = 1.0f;
            mvp.m[3][0] = -1.0f;
            mvp.m[3][1] = 1.0f;
            mvp.m[3][3] = 1.0f;
        } else {
            matrix_multiply(&view_model, &m_world, &m_view);
            matrix_multiply(&mvp, &view_model, &m_projection);
        }

        // D3D9 auto-detects mirrored transforms (negative determinant) and flips culling.
        // WebGL doesn't, so we detect it per draw call. Y-flip also reverses winding.
        // XOR: exactly one flip → CCW, both or neither → CW.
        if (!(g_current_fvf & D3DFVF_XYZRHW)) {
            bool mirror = (det3x3(view_model) < 0.0f);
            glFrontFace((mirror != m_fbo_rendering) ? GL_CCW : GL_CW);
        } else {
            glFrontFace(GL_CW);
        }

        gl_set_matrix_uniform(g_mvp_uniform, &mvp);

        // Set texture factor
        if (g_texture_factor_uniform != -1) {
            float r = ((m_texture_factor >> 16) & 0xFF) / 255.0f;
            float g = ((m_texture_factor >> 8) & 0xFF) / 255.0f;
            float b = (m_texture_factor & 0xFF) / 255.0f;
            float a = ((m_texture_factor >> 24) & 0xFF) / 255.0f;
            glUniform4f(g_texture_factor_uniform, r, g, b, a);
        }

        // Set mipmap LOD bias (D3D9 stores float as DWORD via type-punning).
        // WebGL/OpenGL's LOD calculation uses a "max" rule that tends to select
        // lower-res mip levels compared to D3D9 on the same hardware. An additional
        // -0.75 offset compensates for this difference.
        if (g_lod_bias_uniform != -1) {
            DWORD bias_bits = g_sampler_states[0][D3DSAMP_MIPMAPLODBIAS];
            float lod_bias;
            memcpy(&lod_bias, &bias_bits, sizeof(float));
            lod_bias -= 0.75f;
            glUniform1f(g_lod_bias_uniform, lod_bias);
        }

        // Set Stage 0 texture stage states
        if (g_stage0_colorop_uniform != -1)
            glUniform1i(g_stage0_colorop_uniform, g_texture_stage_states[0][D3DTSS_COLOROP]);
        if (g_stage0_colorarg1_uniform != -1)
            glUniform1i(g_stage0_colorarg1_uniform, g_texture_stage_states[0][D3DTSS_COLORARG1]);
        if (g_stage0_colorarg2_uniform != -1)
            glUniform1i(g_stage0_colorarg2_uniform, g_texture_stage_states[0][D3DTSS_COLORARG2]);
        if (g_stage0_alphaop_uniform != -1)
            glUniform1i(g_stage0_alphaop_uniform, g_texture_stage_states[0][D3DTSS_ALPHAOP]);
        if (g_stage0_alphaarg1_uniform != -1)
            glUniform1i(g_stage0_alphaarg1_uniform, g_texture_stage_states[0][D3DTSS_ALPHAARG1]);
        if (g_stage0_alphaarg2_uniform != -1)
            glUniform1i(g_stage0_alphaarg2_uniform, g_texture_stage_states[0][D3DTSS_ALPHAARG2]);

        glBindBuffer(GL_ARRAY_BUFFER, m_current_vertex_buffer);

        // FVF Parsing
        bool has_xyz = (g_current_fvf & D3DFVF_XYZ) != 0;
        bool has_xyzrhw = (g_current_fvf & D3DFVF_XYZRHW) != 0;
        bool has_normal = (g_current_fvf & D3DFVF_NORMAL) != 0;
        bool has_diffuse = (g_current_fvf & D3DFVF_DIFFUSE) != 0;
        int tex_count = (g_current_fvf >> 8) & 0xF;

        // Calculate offsets accounting for all FVF components
        size_t offset = 0;
        size_t position_offset = 0;
        size_t normal_offset = 0;
        size_t color_offset = 0;
        size_t tex_offset = 0;

        if (has_xyz) { position_offset = offset; offset += 12; }
        if (has_xyzrhw) { position_offset = offset; offset += 16; }
        if (has_normal) { normal_offset = offset; offset += 12; }
        if (has_diffuse) { color_offset = offset; offset += 4; }
        tex_offset = offset;

        GLsizei stride = m_current_stride;

        if ((has_xyz || has_xyzrhw) && g_position_location != -1) {
            glEnableVertexAttribArray(g_position_location);
            glVertexAttribPointer(g_position_location, has_xyzrhw ? 4 : 3, GL_FLOAT, GL_FALSE, stride, (void*)position_offset);
        }

        // Normal attribute for FFP lighting
        if (has_normal && g_normal_location != -1) {
            glEnableVertexAttribArray(g_normal_location);
            glVertexAttribPointer(g_normal_location, 3, GL_FLOAT, GL_FALSE, stride, (void*)normal_offset);
        } else if (g_normal_location != -1) {
            glDisableVertexAttribArray(g_normal_location);
            glVertexAttrib3f(g_normal_location, 0.0f, 0.0f, 1.0f);
        }

        // D3D9 FFP Lighting
        bool lighting_enabled = has_normal && !has_xyzrhw && m_render_states[D3DRS_LIGHTING];
        if (lighting_enabled) {
            glUniform1i(g_lighting_enabled_uniform, 1);
            SetFFPLightingUniforms(&m_world, m_render_states);

            glDisableVertexAttribArray(g_color_location);
            glVertexAttrib4f(g_color_location, 1.0f, 1.0f, 1.0f, 1.0f);
        } else {
            glUniform1i(g_lighting_enabled_uniform, 0);
            // Always set world matrix for clip plane (v_position_world) even without lighting
            gl_set_matrix_uniform(g_world_uniform, &m_world);

            if (has_diffuse && g_color_location != -1) {
                glEnableVertexAttribArray(g_color_location);
                glVertexAttribPointer(g_color_location, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride, (void*)color_offset);
            } else if (g_color_location != -1) {
                glDisableVertexAttribArray(g_color_location);
                glVertexAttrib4f(g_color_location, 1.0f, 1.0f, 1.0f, 1.0f);
            }
        }

        if (tex_count > 0 && g_texcoord_location != -1) {
            glEnableVertexAttribArray(g_texcoord_location);
            glVertexAttribPointer(g_texcoord_location, 2, GL_FLOAT, GL_FALSE, stride, (void*)tex_offset);
        } else if (g_texcoord_location != -1) {
            glDisableVertexAttribArray(g_texcoord_location);
            glVertexAttrib2f(g_texcoord_location, 0.0f, 0.0f);
        }

        // Texcoord1 - explicitly disable to avoid stale state
        if (g_texcoord1_location != -1) {
            glDisableVertexAttribArray(g_texcoord1_location);
            glVertexAttrib2f(g_texcoord1_location, 0.0f, 0.0f);
        }

        // Texture setup
        if (g_texture_stages[0] != 0 && tex_count > 0) {
            glActiveTexture(GL_TEXTURE0);
            if (g_cached_texture != g_texture_stages[0]) {
                glBindTexture(GL_TEXTURE_2D, g_texture_stages[0]);
                g_cached_texture = g_texture_stages[0];
            }
            ApplySamplerStates(0);
            glUniform1i(g_texture_uniform, 0);
            glUniform1i(g_use_texture_uniform, 1);
        } else {
            glUniform1i(g_use_texture_uniform, 0);
        }

        // Alpha test
        glUniform1i(g_alpha_test_enabled_uniform, m_alpha_test_enabled ? 1 : 0);
        glUniform1f(g_alpha_ref_uniform, (float)m_alpha_ref);

        // Disable lightmap
        glUniform1i(g_use_lightmap_uniform, 0);

        GLenum type = GL_TRIANGLES;
        UINT vcount = 0;
        if (PrimitiveType == D3DPT_TRIANGLELIST) { type = GL_TRIANGLES; vcount = PrimitiveCount * 3; }
        else if (PrimitiveType == D3DPT_TRIANGLESTRIP) { type = GL_TRIANGLE_STRIP; vcount = PrimitiveCount + 2; }
        else if (PrimitiveType == D3DPT_TRIANGLEFAN) { type = GL_TRIANGLE_FAN; vcount = PrimitiveCount + 2; }

        glDrawArrays(type, StartVertex, vcount);

        return D3D_OK;
    }
    
    // Query and utility methods
    virtual HRESULT CreateQuery(D3DQUERYTYPE Type, IDirect3DQuery9** ppQuery) override {
        if (ppQuery) *ppQuery = nullptr;
        return D3D_OK;
    }
    
    virtual HRESULT StretchRect(IDirect3DSurface9* pSourceSurface, const void* pSourceRect,
                               IDirect3DSurface9* pDestSurface, const void* pDestRect,
                               D3DTEXTUREFILTERTYPE Filter) override {
        if (!pSourceSurface || !pDestSurface) return D3DERR_INVALIDCALL;

        D3D9Surface* src = static_cast<D3D9Surface*>(pSourceSurface);
        D3D9Surface* dst = static_cast<D3D9Surface*>(pDestSurface);

        // Bind source to read framebuffer
        glBindFramebuffer(GL_READ_FRAMEBUFFER, src->m_is_backbuffer ? 0 : g_fbo);
        if (!src->m_is_backbuffer) {
            glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, src->m_gl_object, 0);
        }

        // Bind dest to draw framebuffer
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, dst->m_is_backbuffer ? 0 : g_fbo);
        if (!dst->m_is_backbuffer) {
            glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, dst->m_gl_object, 0);
        }

        // Validate FBO status before blit
        if (glCheckFramebufferStatus(GL_READ_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE ||
            glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            printf("[D3D9 Debug] StretchRect FAILED: Framebuffer incomplete (Read=%X Draw=%X)\n", 
                   glCheckFramebufferStatus(GL_READ_FRAMEBUFFER), glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER));
        }

        // When FBO rendering uses Y-flip (u_fbo_flip_y), FBO textures store content
        // in D3D9 convention (Y=0 at top) while backbuffer uses OpenGL convention
        // (Y=0 at bottom). Flip Y when blitting between different conventions.
        bool conventions_differ = (src->m_is_backbuffer != dst->m_is_backbuffer);
        GLenum filter = (Filter == D3DTEXF_LINEAR) ? GL_LINEAR : GL_NEAREST;
        if (conventions_differ) {
            // Flip destination Y to compensate for convention mismatch
            glBlitFramebuffer(0, 0, src->m_width, src->m_height,
                             0, dst->m_height, dst->m_width, 0,
                             GL_COLOR_BUFFER_BIT, filter);
        } else {
            glBlitFramebuffer(0, 0, src->m_width, src->m_height,
                             0, 0, dst->m_width, dst->m_height,
                             GL_COLOR_BUFFER_BIT, filter);
        }

        // Restore original RT binding
        if (m_current_rt->m_is_backbuffer) {
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
        } else {
            glBindFramebuffer(GL_FRAMEBUFFER, g_fbo);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_current_rt->m_gl_object, 0);
        }
        
        return D3D_OK;
    }
    
    virtual HRESULT Clear(DWORD Count, const void* pRects, DWORD Flags,
                         D3DCOLOR Color, FLOAT Z, DWORD Stencil) override {
        float r = ((Color >> 16) & 0xFF) / 255.0f;
        float g = ((Color >> 8) & 0xFF) / 255.0f;
        float b = (Color & 0xFF) / 255.0f;
        float a = ((Color >> 24) & 0xFF) / 255.0f;
        
        glClearColor(r, g, b, a);
        glClearDepthf(Z);
        glClearStencil(Stencil);

        GLbitfield mask = 0;
        if (Flags & D3DCLEAR_TARGET) mask |= GL_COLOR_BUFFER_BIT;
        if (Flags & D3DCLEAR_ZBUFFER) mask |= GL_DEPTH_BUFFER_BIT;
        if (Flags & D3DCLEAR_STENCIL) mask |= GL_STENCIL_BUFFER_BIT;

        // Save current mask states from tracked render states (no GPU readback)
        GLboolean color_write_enabled_r = (m_render_states[D3DRS_COLORWRITEENABLE] & D3DCOLORWRITEENABLE_RED) ? GL_TRUE : GL_FALSE;
        GLboolean color_write_enabled_g = (m_render_states[D3DRS_COLORWRITEENABLE] & D3DCOLORWRITEENABLE_GREEN) ? GL_TRUE : GL_FALSE;
        GLboolean color_write_enabled_b = (m_render_states[D3DRS_COLORWRITEENABLE] & D3DCOLORWRITEENABLE_BLUE) ? GL_TRUE : GL_FALSE;
        GLboolean color_write_enabled_a = (m_render_states[D3DRS_COLORWRITEENABLE] & D3DCOLORWRITEENABLE_ALPHA) ? GL_TRUE : GL_FALSE;
        GLboolean depth_write_enabled = m_render_states[D3DRS_ZWRITEENABLE] ? GL_TRUE : GL_FALSE;

        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glDepthMask(GL_TRUE);

        CHECK_GL_ERROR(glClear(mask));

        // Restore masks from tracked state (no GPU readback)
        glColorMask(color_write_enabled_r, color_write_enabled_g, color_write_enabled_b, color_write_enabled_a);
        glDepthMask(depth_write_enabled);

        return D3D_OK;
    }
    
    virtual HRESULT BeginScene() override {
        return D3D_OK;
    }
    
    virtual HRESULT EndScene() override {
        return D3D_OK;
    }
    
    virtual HRESULT Present(const void* pSourceRect, const void* pDestRect,
                           HWND hDestWindowOverride, const void* pDirtyRegion) override {
        return D3D_OK;
    }
    
static void ApplySamplerStates(DWORD Sampler) {
    if (Sampler >= 8) return;

    // Address U
    GLint wrap_s = GL_REPEAT;
    if (g_sampler_states[Sampler][D3DSAMP_ADDRESSU] == D3DTADDRESS_CLAMP) wrap_s = GL_CLAMP_TO_EDGE;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap_s);
    
    // Address V
    GLint wrap_t = GL_REPEAT;
    if (g_sampler_states[Sampler][D3DSAMP_ADDRESSV] == D3DTADDRESS_CLAMP) wrap_t = GL_CLAMP_TO_EDGE;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap_t);
    
    // Mag Filter (mipmaps don't affect magnification)
    GLint mag_filter = GL_LINEAR;
    if (g_sampler_states[Sampler][D3DSAMP_MAGFILTER] == D3DTEXF_POINT) mag_filter = GL_NEAREST;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, mag_filter);

    // Min Filter: combine D3D9 MINFILTER + MIPFILTER into GL enum.
    // D3D9 has separate MINFILTER and MIPFILTER states, but OpenGL combines
    // them into a single GL_TEXTURE_MIN_FILTER value:
    //   MIN=LINEAR  + MIP=LINEAR  → GL_LINEAR_MIPMAP_LINEAR   (trilinear)
    //   MIN=LINEAR  + MIP=POINT   → GL_LINEAR_MIPMAP_NEAREST  (bilinear)
    //   MIN=POINT   + MIP=LINEAR  → GL_NEAREST_MIPMAP_LINEAR
    //   MIN=POINT   + MIP=POINT   → GL_NEAREST_MIPMAP_NEAREST
    //   MIN=*       + MIP=NONE    → GL_LINEAR / GL_NEAREST    (no mip)
    DWORD d3d_min = g_sampler_states[Sampler][D3DSAMP_MINFILTER];
    DWORD d3d_mip = g_sampler_states[Sampler][D3DSAMP_MIPFILTER];

    GLuint current_tex = g_texture_stages[Sampler];
    bool has_mipmaps = (current_tex != 0) && g_mipmapped_textures.count(current_tex);

    GLint min_filter;
    if (has_mipmaps && d3d_mip != D3DTEXF_NONE) {
        if (d3d_min == D3DTEXF_POINT) {
            min_filter = (d3d_mip == D3DTEXF_LINEAR) ? GL_NEAREST_MIPMAP_LINEAR : GL_NEAREST_MIPMAP_NEAREST;
        } else {
            min_filter = (d3d_mip == D3DTEXF_LINEAR) ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR_MIPMAP_NEAREST;
        }
    } else {
        min_filter = (d3d_min == D3DTEXF_POINT) ? GL_NEAREST : GL_LINEAR;
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, min_filter);

    // Anisotropic filtering: apply when mipmapping is active.
    // This dramatically improves quality on surfaces at oblique angles (floors,
    // walls receding into distance) by sampling higher mip levels along the
    // less-compressed texture axis instead of using the worst-case mip level.
    if (g_max_anisotropy > 1.0f && has_mipmaps && d3d_mip != D3DTEXF_NONE) {
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, g_max_anisotropy);
    } else if (g_max_anisotropy > 1.0f) {
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, 1.0f);
    }
}

    virtual HRESULT SetFVF(DWORD FVF) override {
        g_current_fvf = FVF;
        return D3D_OK;
    }
    
    virtual HRESULT DrawPrimitiveUP(D3DPRIMITIVETYPE PrimitiveType,
                                   UINT PrimitiveCount,
                                   const void* pVertexStreamZeroData,
                                   UINT VertexStreamZeroStride) override {
        if (!pVertexStreamZeroData) return -1;

        if (g_shader_program == 0) init_shaders();

        // State Caching: Only call glUseProgram if program changed
        if (g_cached_program != g_shader_program) {
            glUseProgram(g_shader_program);
            g_cached_program = g_shader_program;
        }

        // FBO Y-flip and clip plane uniforms
        glUniform1i(g_fbo_flip_y_uniform, m_fbo_rendering ? 1 : 0);
        glUniform1i(g_clip_plane_enabled_uniform, m_clip_plane_enabled ? 1 : 0);
        if (m_clip_plane_enabled) glUniform4fv(g_clip_plane_uniform, 1, m_clip_planes[0]);

        EnsureUPBuffers();
        CHECK_GL_ERROR(glBindBuffer(GL_ARRAY_BUFFER, g_up_vbo));
        CHECK_GL_ERROR(glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0)); // No indices for UP

        // Dynamic FVF Parsing
        bool has_xyz = (g_current_fvf & D3DFVF_XYZ) != 0;
        bool has_xyzrhw = (g_current_fvf & D3DFVF_XYZRHW) != 0;
        bool has_normal = (g_current_fvf & D3DFVF_NORMAL) != 0;
        bool has_diffuse = (g_current_fvf & D3DFVF_DIFFUSE) != 0;
        int tex_count = (g_current_fvf >> 8) & 0xF;

        GLsizei stride = 0;
        size_t position_offset = 0;
        size_t normal_offset = 0;
        size_t color_offset = 0;
        size_t tex_offset = 0;

        // XYZ and XYZRHW are mutually exclusive in D3D9 FVF
        if (has_xyzrhw) { position_offset = stride; stride += 16; }
        else if (has_xyz) { position_offset = stride; stride += 12; }
        if (has_normal) { normal_offset = stride; stride += 12; }
        if (has_diffuse) { color_offset = stride; stride += 4; }
        tex_offset = stride;
        stride += tex_count * 8;
        
        if (VertexStreamZeroStride > 0) stride = VertexStreamZeroStride;

        // Override Viewport for UI rendering to ensure it covers the full canvas
        GLint saved_viewport[4];
        bool override_viewport = false;
        
        // Restore state variables
        GLboolean saved_scissor_test = GL_FALSE;
        GLint saved_scissor_box[4];

        if ((g_current_fvf & D3DFVF_XYZRHW) && m_current_rt && m_current_rt->m_is_backbuffer) {
            // Use cached viewport instead of glGetIntegerv (performance optimization)
            saved_viewport[0] = g_cached_viewport[0];
            saved_viewport[1] = g_cached_viewport[1];
            saved_viewport[2] = g_cached_viewport[2];
            saved_viewport[3] = g_cached_viewport[3];

            glViewport(0, 0, g_logical_width, g_logical_height);

            // Disable Culling and Depth Test for 2D UI elements to ensure visibility
            glDisable(GL_CULL_FACE);
            glDisable(GL_DEPTH_TEST);
            glDepthMask(GL_FALSE);

            // Use cached scissor state instead of glGet* (performance optimization)
            saved_scissor_test = g_cached_scissor_test;
            saved_scissor_box[0] = g_cached_scissor_box[0];
            saved_scissor_box[1] = g_cached_scissor_box[1];
            saved_scissor_box[2] = g_cached_scissor_box[2];
            saved_scissor_box[3] = g_cached_scissor_box[3];

            glEnable(GL_SCISSOR_TEST);

            // Scissor to current viewport area (WebGL Y is bottom-to-top)
            int sy = g_logical_height - (g_viewport.Y + g_viewport.Height);
            glScissor(g_viewport.X, sy, g_viewport.Width, g_viewport.Height);
            
            override_viewport = true;
        }

        // Calculate MVP
        D3DMATRIX view_model, mvp;
        if (g_current_fvf & D3DFVF_XYZRHW) {
            // For XYZRHW, use the app's logical resolution
            float sw = (float)g_logical_width;
            float sh = (float)g_logical_height;
            // If rendering to texture, use texture dimensions
            if (m_current_rt && !m_current_rt->m_is_backbuffer) {
                 sw = (float)m_current_rt->m_width;
                 sh = (float)m_current_rt->m_height;
            }

            memset(&mvp, 0, sizeof(D3DMATRIX));
            mvp.m[0][0] = 2.0f / sw;
            mvp.m[1][1] = -2.0f / sh; 
            mvp.m[3][0] = -1.0f;
            mvp.m[3][1] = 1.0f;
            mvp.m[2][2] = 1.0f;
            mvp.m[3][3] = 1.0f;
            mvp.m[3][2] = 0.0f;
        } else {
            matrix_multiply(&view_model, &m_world, &m_view);
            matrix_multiply(&mvp, &view_model, &m_projection);
        }

        // Winding detection: D3D9 auto-flips culling for mirrored transforms; we emulate it
        if (!(g_current_fvf & D3DFVF_XYZRHW)) {
            bool mirror = (det3x3(view_model) < 0.0f);
            glFrontFace((mirror != m_fbo_rendering) ? GL_CCW : GL_CW);
        } else {
            glFrontFace(GL_CW);
        }

        gl_set_matrix_uniform(g_mvp_uniform, &mvp);

        if (g_texture_factor_uniform != -1) {
            float r = ((m_texture_factor >> 16) & 0xFF) / 255.0f;
            float g = ((m_texture_factor >> 8) & 0xFF) / 255.0f;
            float b = (m_texture_factor & 0xFF) / 255.0f;
            float a = ((m_texture_factor >> 24) & 0xFF) / 255.0f;
            glUniform4f(g_texture_factor_uniform, r, g, b, a);
        }

        // Set mipmap LOD bias (D3D9 stores float as DWORD via type-punning).
        // WebGL/OpenGL's LOD calculation uses a "max" rule that tends to select
        // lower-res mip levels compared to D3D9 on the same hardware. An additional
        // -0.75 offset compensates for this difference.
        if (g_lod_bias_uniform != -1) {
            DWORD bias_bits = g_sampler_states[0][D3DSAMP_MIPMAPLODBIAS];
            float lod_bias;
            memcpy(&lod_bias, &bias_bits, sizeof(float));
            lod_bias -= 0.75f;
            glUniform1f(g_lod_bias_uniform, lod_bias);
        }

        // Set Stage 0 texture stage states
        if (g_stage0_colorop_uniform != -1) {
            glUniform1i(g_stage0_colorop_uniform, g_texture_stage_states[0][D3DTSS_COLOROP]);
        }
        if (g_stage0_colorarg1_uniform != -1) {
            glUniform1i(g_stage0_colorarg1_uniform, g_texture_stage_states[0][D3DTSS_COLORARG1]);
        }
        if (g_stage0_colorarg2_uniform != -1) {
            glUniform1i(g_stage0_colorarg2_uniform, g_texture_stage_states[0][D3DTSS_COLORARG2]);
        }
        if (g_stage0_alphaop_uniform != -1) {
            glUniform1i(g_stage0_alphaop_uniform, g_texture_stage_states[0][D3DTSS_ALPHAOP]);
        }
        if (g_stage0_alphaarg1_uniform != -1) {
            glUniform1i(g_stage0_alphaarg1_uniform, g_texture_stage_states[0][D3DTSS_ALPHAARG1]);
        }
        if (g_stage0_alphaarg2_uniform != -1) {
            glUniform1i(g_stage0_alphaarg2_uniform, g_texture_stage_states[0][D3DTSS_ALPHAARG2]);
        }

        const uint8_t* vertex_data = static_cast<const uint8_t*>(pVertexStreamZeroData);
        
        // Calculate vertex count and total size
        UINT vertex_count = 0;
         switch (PrimitiveType) {
            case D3DPT_TRIANGLELIST: vertex_count = PrimitiveCount * 3; break;
            case D3DPT_TRIANGLESTRIP: vertex_count = PrimitiveCount + 2; break;
            case D3DPT_TRIANGLEFAN: vertex_count = PrimitiveCount + 2; break;
            case D3DPT_LINELIST: vertex_count = PrimitiveCount * 2; break;
            case D3DPT_LINESTRIP: vertex_count = PrimitiveCount + 1; break;
            case D3DPT_POINTLIST: vertex_count = PrimitiveCount; break;
        }
        
        size_t total_size = stride * vertex_count;

        // Convert BGRA vertex colors to RGBA if needed
        bool converted = false;
        static std::vector<uint8_t> converted_data;
        if (has_diffuse) {
            converted_data.resize(total_size);
            memcpy(converted_data.data(), pVertexStreamZeroData, total_size);
            
            // Swap R and B in each vertex's color (BGRA -> RGBA)
            // [WASM] Restore Red-Blue swap for vertex colors (BGRA -> RGBA)
            for (UINT i = 0; i < vertex_count; ++i) {
                uint8_t* color_ptr = converted_data.data() + (i * stride) + color_offset;
                std::swap(color_ptr[0], color_ptr[2]); 
                
                // Removed: Force Alpha to 255 if it's 0
                // This was breaking opacity functionality for UI elements (glow effects, etc.)
                // D3D9 FFP respects alpha=0 for transparency when D3DFVF_DIFFUSE is specified
            }
            vertex_data = converted_data.data();
            converted = true;
        }

        // Upload to VBO - Use persistent buffer for better performance
        if (total_size > g_up_vbo_size) {
            // Buffer too small, reallocate with 2x growth strategy
            size_t new_size = total_size * 2;
            CHECK_GL_ERROR(glBufferData(GL_ARRAY_BUFFER, new_size, nullptr, GL_DYNAMIC_DRAW));
            g_up_vbo_size = new_size;
        }
        // Just update the data (fast path - avoids buffer reallocation)
        CHECK_GL_ERROR(glBufferSubData(GL_ARRAY_BUFFER, 0, total_size, vertex_data));
        
        // Enable/Set Pointers (Use offsets!)
        if (has_xyzrhw) {
            CHECK_GL_ERROR(glEnableVertexAttribArray(g_position_location));
            CHECK_GL_ERROR(glVertexAttribPointer(g_position_location, 4, GL_FLOAT, GL_FALSE, stride, (void*)position_offset));
        } else if (has_xyz) {
            CHECK_GL_ERROR(glEnableVertexAttribArray(g_position_location));
            CHECK_GL_ERROR(glVertexAttribPointer(g_position_location, 3, GL_FLOAT, GL_FALSE, stride, (void*)position_offset));
        }

        // Normal attribute for FFP lighting
        if (has_normal && g_normal_location != -1) {
            CHECK_GL_ERROR(glEnableVertexAttribArray(g_normal_location));
            CHECK_GL_ERROR(glVertexAttribPointer(g_normal_location, 3, GL_FLOAT, GL_FALSE, stride, (void*)normal_offset));
        } else if (g_normal_location != -1) {
            CHECK_GL_ERROR(glDisableVertexAttribArray(g_normal_location));
            CHECK_GL_ERROR(glVertexAttrib3f(g_normal_location, 0.0f, 0.0f, 1.0f));
        }

        // D3D9 FFP Lighting: when FVF has NORMAL and D3DRS_LIGHTING is TRUE
        bool lighting_enabled = has_normal && !has_xyzrhw && m_render_states[D3DRS_LIGHTING];
        if (lighting_enabled) {
            CHECK_GL_ERROR(glUniform1i(g_lighting_enabled_uniform, 1));
            SetFFPLightingUniforms(&m_world, m_render_states);

            CHECK_GL_ERROR(glDisableVertexAttribArray(g_color_location));
            CHECK_GL_ERROR(glVertexAttrib4f(g_color_location, 1.0f, 1.0f, 1.0f, 1.0f));
        } else {
            CHECK_GL_ERROR(glUniform1i(g_lighting_enabled_uniform, 0));
            // Always set world matrix for clip plane (v_position_world) even without lighting
            gl_set_matrix_uniform(g_world_uniform, &m_world);

            if (has_diffuse) {
                CHECK_GL_ERROR(glEnableVertexAttribArray(g_color_location));
                CHECK_GL_ERROR(glVertexAttribPointer(g_color_location, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride, (void*)color_offset));
            } else {
                CHECK_GL_ERROR(glDisableVertexAttribArray(g_color_location));
                CHECK_GL_ERROR(glVertexAttrib4f(g_color_location, 1.0f, 1.0f, 1.0f, 1.0f));
            }
        }

        if (tex_count > 0) {
            CHECK_GL_ERROR(glEnableVertexAttribArray(g_texcoord_location));
            CHECK_GL_ERROR(glVertexAttribPointer(g_texcoord_location, 2, GL_FLOAT, GL_FALSE, stride, (void*)tex_offset));
        } else {
            CHECK_GL_ERROR(glDisableVertexAttribArray(g_texcoord_location));
            CHECK_GL_ERROR(glVertexAttrib2f(g_texcoord_location, 0.0f, 0.0f));
        }

        if (tex_count > 1) {
            CHECK_GL_ERROR(glEnableVertexAttribArray(g_texcoord1_location));
            CHECK_GL_ERROR(glVertexAttribPointer(g_texcoord1_location, 2, GL_FLOAT, GL_FALSE, stride, (void*)(tex_offset + 8)));
        } else {
            CHECK_GL_ERROR(glDisableVertexAttribArray(g_texcoord1_location));
            CHECK_GL_ERROR(glVertexAttrib2f(g_texcoord1_location, 0.0f, 0.0f));
        }
        
        // Texture setup
        if (g_texture_stages[0] != 0 && tex_count > 0) {
            CHECK_GL_ERROR(glActiveTexture(GL_TEXTURE0));

            // State Caching: Only bind texture if it changed
            if (g_cached_texture != g_texture_stages[0]) {
                CHECK_GL_ERROR(glBindTexture(GL_TEXTURE_2D, g_texture_stages[0]));
                g_cached_texture = g_texture_stages[0];
            }

            CHECK_GL_ERROR(glUniform1i(g_texture_uniform, 0));
            CHECK_GL_ERROR(glUniform1i(g_use_texture_uniform, 1));

            // Apply D3D sampler states (Filter, Wrap)
            ApplySamplerStates(0);
        } else {
            // No texture case - bind texture 0
            // IMPORTANT: Must set active texture unit to TEXTURE0 before unbinding,
            // otherwise we may accidentally unbind GL_TEXTURE1 (lightmap) if that was
            // the last active unit (e.g., after DrawIndexedPrimitive's lightmap setup).
            CHECK_GL_ERROR(glActiveTexture(GL_TEXTURE0));
            if (g_cached_texture != 0) {
                CHECK_GL_ERROR(glBindTexture(GL_TEXTURE_2D, 0));
                g_cached_texture = 0;
            }
            CHECK_GL_ERROR(glUniform1i(g_use_texture_uniform, 0));
        }

        CHECK_GL_ERROR(glUniform1i(g_alpha_test_enabled_uniform, m_alpha_test_enabled ? 1 : 0));
        CHECK_GL_ERROR(glUniform1f(g_alpha_ref_uniform, (float)m_alpha_ref));

        // Disable lightmap to avoid stale state from DrawIndexedPrimitive
        CHECK_GL_ERROR(glUniform1i(g_use_lightmap_uniform, 0));

        // Draw
        GLenum gl_primitive_type = GL_TRIANGLES;
        switch (PrimitiveType) {
            case D3DPT_TRIANGLELIST: gl_primitive_type = GL_TRIANGLES; break;
            case D3DPT_TRIANGLESTRIP: gl_primitive_type = GL_TRIANGLE_STRIP; break;
            case D3DPT_TRIANGLEFAN: gl_primitive_type = GL_TRIANGLE_FAN; break;
            case D3DPT_LINELIST: gl_primitive_type = GL_LINES; break;
            case D3DPT_LINESTRIP: gl_primitive_type = GL_LINE_STRIP; break;
            case D3DPT_POINTLIST: gl_primitive_type = GL_POINTS; break;
        }

        CHECK_GL_ERROR(glDrawArrays(gl_primitive_type, 0, vertex_count));

        // Cleanup pointers (unbind RB to be safe?)
        CHECK_GL_ERROR(glBindBuffer(GL_ARRAY_BUFFER, 0));

        // Restore Viewport/Scissor/DepthTest/CullFace if overridden
        if (override_viewport) {
            glViewport(saved_viewport[0], saved_viewport[1], saved_viewport[2], saved_viewport[3]);

            if (saved_scissor_test) {
                glScissor(saved_scissor_box[0], saved_scissor_box[1], saved_scissor_box[2], saved_scissor_box[3]);
            } else {
                glDisable(GL_SCISSOR_TEST);
            }

            // Restore depth test, depth mask, and cull face from tracked render states
            if (m_render_states[D3DRS_ZENABLE]) glEnable(GL_DEPTH_TEST);
            if (m_render_states[D3DRS_ZWRITEENABLE]) glDepthMask(GL_TRUE);
            if (m_render_states[D3DRS_CULLMODE] != D3DCULL_NONE) glEnable(GL_CULL_FACE);
        }

        return D3D_OK;
    }
    virtual HRESULT DrawIndexedPrimitiveUP(D3DPRIMITIVETYPE PrimitiveType, UINT MinVertexIndex,
                                           UINT NumVertices, UINT PrimitiveCount,
                                           const void* pIndexData, D3DFORMAT IndexDataFormat,
                                           const void* pVertexStreamZeroData,
                                           UINT VertexStreamZeroStride) override {
        if (!pVertexStreamZeroData || !pIndexData) return -1;

        if (g_shader_program == 0) init_shaders();
        glUseProgram(g_shader_program);

        // FBO Y-flip and clip plane uniforms
        glUniform1i(g_fbo_flip_y_uniform, m_fbo_rendering ? 1 : 0);
        glUniform1i(g_clip_plane_enabled_uniform, m_clip_plane_enabled ? 1 : 0);
        if (m_clip_plane_enabled) glUniform4fv(g_clip_plane_uniform, 1, m_clip_planes[0]);

        EnsureUPBuffers();
        CHECK_GL_ERROR(glBindBuffer(GL_ARRAY_BUFFER, g_up_vbo));
        CHECK_GL_ERROR(glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g_up_ibo));

        // Respect game's blend state (set via SetRenderState before this draw call)

        // Calculate MVP
        D3DMATRIX view_model, mvp;
        if (g_current_fvf & D3DFVF_XYZRHW) {
            float vw = (g_viewport.Width > 0) ? (float)g_viewport.Width : (float)g_logical_width;
            float vh = (g_viewport.Height > 0) ? (float)g_viewport.Height : (float)g_logical_height;

            memset(&mvp, 0, sizeof(D3DMATRIX));
            mvp.m[0][0] = 2.0f / vw;
            mvp.m[3][0] = -1.0f;
            mvp.m[1][1] = -2.0f / vh;
            mvp.m[3][1] = 1.0f;
            mvp.m[2][2] = 1.0f;
            mvp.m[3][3] = 1.0f;
        } else {
            matrix_multiply(&view_model, &m_world, &m_view);
            matrix_multiply(&mvp, &view_model, &m_projection);
        }

        // Winding detection: D3D9 auto-flips culling for mirrored transforms; we emulate it
        if (!(g_current_fvf & D3DFVF_XYZRHW)) {
            bool mirror = (det3x3(view_model) < 0.0f);
            glFrontFace((mirror != m_fbo_rendering) ? GL_CCW : GL_CW);
        } else {
            glFrontFace(GL_CW);
        }

        gl_set_matrix_uniform(g_mvp_uniform, &mvp);

        // Set texture factor
        if (g_texture_factor_uniform != -1) {
            float r = ((m_texture_factor >> 16) & 0xFF) / 255.0f;
            float g = ((m_texture_factor >> 8) & 0xFF) / 255.0f;
            float b = (m_texture_factor & 0xFF) / 255.0f;
            float a = ((m_texture_factor >> 24) & 0xFF) / 255.0f;
            glUniform4f(g_texture_factor_uniform, r, g, b, a);
        }

        // Set mipmap LOD bias (D3D9 stores float as DWORD via type-punning).
        // WebGL/OpenGL's LOD calculation uses a "max" rule that tends to select
        // lower-res mip levels compared to D3D9 on the same hardware. An additional
        // -0.75 offset compensates for this difference.
        if (g_lod_bias_uniform != -1) {
            DWORD bias_bits = g_sampler_states[0][D3DSAMP_MIPMAPLODBIAS];
            float lod_bias;
            memcpy(&lod_bias, &bias_bits, sizeof(float));
            lod_bias -= 0.75f;
            glUniform1f(g_lod_bias_uniform, lod_bias);
        }

        // Set Stage 0 texture stage states
        if (g_stage0_colorop_uniform != -1) {
            glUniform1i(g_stage0_colorop_uniform, g_texture_stage_states[0][D3DTSS_COLOROP]);
        }

        if (g_stage0_colorarg1_uniform != -1) {
            glUniform1i(g_stage0_colorarg1_uniform, g_texture_stage_states[0][D3DTSS_COLORARG1]);
        }
        if (g_stage0_colorarg2_uniform != -1) {
            glUniform1i(g_stage0_colorarg2_uniform, g_texture_stage_states[0][D3DTSS_COLORARG2]);
        }
        if (g_stage0_alphaop_uniform != -1) {
            glUniform1i(g_stage0_alphaop_uniform, g_texture_stage_states[0][D3DTSS_ALPHAOP]);
        }
        if (g_stage0_alphaarg1_uniform != -1) {
            glUniform1i(g_stage0_alphaarg1_uniform, g_texture_stage_states[0][D3DTSS_ALPHAARG1]);
        }
        if (g_stage0_alphaarg2_uniform != -1) {
            glUniform1i(g_stage0_alphaarg2_uniform, g_texture_stage_states[0][D3DTSS_ALPHAARG2]);
        }

        // Dynamic FVF Parsing
        bool has_xyz = (g_current_fvf & D3DFVF_XYZ) != 0;
        bool has_xyzrhw = (g_current_fvf & D3DFVF_XYZRHW) != 0;
        bool has_normal = (g_current_fvf & D3DFVF_NORMAL) != 0;
        bool has_diffuse = (g_current_fvf & D3DFVF_DIFFUSE) != 0;
        int tex_count = (g_current_fvf >> 8) & 0xF;

        GLsizei stride = 0;
        size_t position_offset = 0;
        size_t normal_offset = 0;
        size_t color_offset = 0;
        size_t tex_offset = 0;

        // XYZ and XYZRHW are mutually exclusive in D3D9 FVF
        if (has_xyzrhw) { position_offset = stride; stride += 16; }
        else if (has_xyz) { position_offset = stride; stride += 12; }
        if (has_normal) { normal_offset = stride; stride += 12; }
        if (has_diffuse) { color_offset = stride; stride += 4; }
        tex_offset = stride;
        stride += tex_count * 8;

        if (VertexStreamZeroStride > 0) stride = VertexStreamZeroStride;

        const uint8_t* vertex_data = static_cast<const uint8_t*>(pVertexStreamZeroData);

        // Upload Vertices
        // NumVertices isn't reliable for buffer size, but MinVertexIndex + NumVertices is max index.
        // For simplicity, we assume we need to upload consistent with PrimitiveCount, 
        // BUT indexed usage is tricky. We'll reuse NumVertices logic but be careful.
        // Actually, for direct pointer indexed drawing, we should upload the whole range used.
        // Simplest safe bet: upload everything provided... but we only have a pointer.
        // We HAVE to trust NumVertices.
        
        size_t total_v_size = stride * NumVertices;
        
        // BGRA -> RGBA conversion
        static std::vector<uint8_t> converted_v_data;
        if (has_diffuse) {
            converted_v_data.resize(total_v_size);
            memcpy(converted_v_data.data(), pVertexStreamZeroData, total_v_size);
             for (UINT i = 0; i < NumVertices; ++i) {
                uint8_t* color_ptr = converted_v_data.data() + (i * stride) + color_offset;
                std::swap(color_ptr[0], color_ptr[2]);
                // D3D9 FFP respects alpha=0 for transparency when D3DFVF_DIFFUSE is specified.
                // Do NOT force alpha=255 here; doing so breaks opacity for UI elements and glow effects.
                // (Aligned with DrawPrimitiveUP behavior.)
            }
            vertex_data = converted_v_data.data();
        }

        // Upload Vertices - Use persistent buffer for better performance
        if (total_v_size > g_up_vbo_size) {
            // Buffer too small, reallocate with 2x growth strategy
            size_t new_size = total_v_size * 2;
            CHECK_GL_ERROR(glBufferData(GL_ARRAY_BUFFER, new_size, nullptr, GL_DYNAMIC_DRAW));
            g_up_vbo_size = new_size;
        }
        // Just update the data (fast path - avoids buffer reallocation)
        CHECK_GL_ERROR(glBufferSubData(GL_ARRAY_BUFFER, 0, total_v_size, vertex_data));

        // Upload Indices
        // PrimitiveCount triangles/lines etc.
        size_t index_size = (IndexDataFormat == D3DFMT_INDEX16) ? 2 : 4;
        size_t index_count = 0;
        switch (PrimitiveType) {
            case D3DPT_TRIANGLELIST: index_count = PrimitiveCount * 3; break;
            case D3DPT_TRIANGLESTRIP: index_count = PrimitiveCount + 2; break; // Approximated
            case D3DPT_TRIANGLEFAN: index_count = PrimitiveCount + 2; break; // Approximated
            // Lines etc
            default: index_count = PrimitiveCount * 3; // Safe bet for alloc
        }
        
        // Actually, primitives define how many indices we read.
        size_t total_i_size = index_count * index_size;

        // Upload Indices - Use persistent buffer for better performance
        if (total_i_size > g_up_ibo_size) {
            // Buffer too small, reallocate with 2x growth strategy
            size_t new_size = total_i_size * 2;
            CHECK_GL_ERROR(glBufferData(GL_ELEMENT_ARRAY_BUFFER, new_size, nullptr, GL_DYNAMIC_DRAW));
            g_up_ibo_size = new_size;
        }
        // Just update the data (fast path - avoids buffer reallocation)
        CHECK_GL_ERROR(glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, total_i_size, pIndexData));

         // Enable/Set Pointers (Use offsets!)
        if (has_xyzrhw) {
            // [IMPORTANT] Pass all 4 components for XYZRHW to properly set gl_Position.w
            CHECK_GL_ERROR(glEnableVertexAttribArray(g_position_location));
            CHECK_GL_ERROR(glVertexAttribPointer(g_position_location, 4, GL_FLOAT, GL_FALSE, stride, (void*)position_offset));
        } else if (has_xyz) {
            CHECK_GL_ERROR(glEnableVertexAttribArray(g_position_location));
            CHECK_GL_ERROR(glVertexAttribPointer(g_position_location, 3, GL_FLOAT, GL_FALSE, stride, (void*)position_offset));
        }

        // Normal attribute for FFP lighting
        if (has_normal && g_normal_location != -1) {
            CHECK_GL_ERROR(glEnableVertexAttribArray(g_normal_location));
            CHECK_GL_ERROR(glVertexAttribPointer(g_normal_location, 3, GL_FLOAT, GL_FALSE, stride, (void*)normal_offset));
        } else if (g_normal_location != -1) {
            CHECK_GL_ERROR(glDisableVertexAttribArray(g_normal_location));
            CHECK_GL_ERROR(glVertexAttrib3f(g_normal_location, 0.0f, 0.0f, 1.0f));
        }

        // D3D9 FFP Lighting: when FVF has NORMAL and D3DRS_LIGHTING is TRUE
        bool lighting_enabled = has_normal && !has_xyzrhw && m_render_states[D3DRS_LIGHTING];
        if (lighting_enabled) {
            CHECK_GL_ERROR(glUniform1i(g_lighting_enabled_uniform, 1));
            SetFFPLightingUniforms(&m_world, m_render_states);

            CHECK_GL_ERROR(glDisableVertexAttribArray(g_color_location));
            CHECK_GL_ERROR(glVertexAttrib4f(g_color_location, 1.0f, 1.0f, 1.0f, 1.0f));
        } else {
            CHECK_GL_ERROR(glUniform1i(g_lighting_enabled_uniform, 0));
            // Always set world matrix for clip plane (v_position_world) even without lighting
            gl_set_matrix_uniform(g_world_uniform, &m_world);

            if (has_diffuse) {
                CHECK_GL_ERROR(glEnableVertexAttribArray(g_color_location));
                CHECK_GL_ERROR(glVertexAttribPointer(g_color_location, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride, (void*)color_offset));
            } else {
                CHECK_GL_ERROR(glDisableVertexAttribArray(g_color_location));
                CHECK_GL_ERROR(glVertexAttrib4f(g_color_location, 1.0f, 1.0f, 1.0f, 1.0f));
            }
        }

        if (tex_count > 0) {
            CHECK_GL_ERROR(glEnableVertexAttribArray(g_texcoord_location));
            CHECK_GL_ERROR(glVertexAttribPointer(g_texcoord_location, 2, GL_FLOAT, GL_FALSE, stride, (void*)tex_offset));
        } else {
            CHECK_GL_ERROR(glDisableVertexAttribArray(g_texcoord_location));
            CHECK_GL_ERROR(glVertexAttrib2f(g_texcoord_location, 0.0f, 0.0f));
        }

        // Texcoord1 (second UV set) - must explicitly handle to avoid stale state
        if (tex_count > 1 && g_texcoord1_location != -1) {
            CHECK_GL_ERROR(glEnableVertexAttribArray(g_texcoord1_location));
            CHECK_GL_ERROR(glVertexAttribPointer(g_texcoord1_location, 2, GL_FLOAT, GL_FALSE, stride, (void*)(tex_offset + 8)));
        } else if (g_texcoord1_location != -1) {
            CHECK_GL_ERROR(glDisableVertexAttribArray(g_texcoord1_location));
            CHECK_GL_ERROR(glVertexAttrib2f(g_texcoord1_location, 0.0f, 0.0f));
        }

        // Texture setup
        if (g_texture_stages[0] != 0 && tex_count > 0) {
            CHECK_GL_ERROR(glActiveTexture(GL_TEXTURE0));

            // State Caching: Only bind texture if it changed
            if (g_cached_texture != g_texture_stages[0]) {
                CHECK_GL_ERROR(glBindTexture(GL_TEXTURE_2D, g_texture_stages[0]));
                g_cached_texture = g_texture_stages[0];
            }

            CHECK_GL_ERROR(glUniform1i(g_texture_uniform, 0));
            CHECK_GL_ERROR(glUniform1i(g_use_texture_uniform, 1));

            // Apply D3D sampler states (Filter, Wrap)
            ApplySamplerStates(0);
        } else {
            // No texture case - bind texture 0
            // IMPORTANT: Must set active texture unit to TEXTURE0 before unbinding,
            // otherwise we may accidentally unbind GL_TEXTURE1 (lightmap) if that was
            // the last active unit (e.g., after DrawIndexedPrimitive's lightmap setup).
            CHECK_GL_ERROR(glActiveTexture(GL_TEXTURE0));
            if (g_cached_texture != 0) {
                CHECK_GL_ERROR(glBindTexture(GL_TEXTURE_2D, 0));
                g_cached_texture = 0;
            }
            CHECK_GL_ERROR(glUniform1i(g_use_texture_uniform, 0));
        }

        CHECK_GL_ERROR(glUniform1i(g_alpha_test_enabled_uniform, m_alpha_test_enabled ? 1 : 0));
        CHECK_GL_ERROR(glUniform1f(g_alpha_ref_uniform, (float)m_alpha_ref));

        // Stage 1 / Lightmap setup - must explicitly set to avoid stale state from DrawIndexedPrimitive
        if (g_texture_stages[1] != 0 && tex_count > 1) {
            CHECK_GL_ERROR(glActiveTexture(GL_TEXTURE1));
            CHECK_GL_ERROR(glBindTexture(GL_TEXTURE_2D, g_texture_stages[1]));
            g_last_bound_lightmap_id = g_texture_stages[1];
            CHECK_GL_ERROR(glUniform1i(g_texture_lm_uniform, 1));
            CHECK_GL_ERROR(glUniform1i(g_use_lightmap_uniform, 1));
            DWORD op = g_texture_stage_states[1][D3DTSS_COLOROP];
            CHECK_GL_ERROR(glUniform1i(g_stage1_op_uniform, op));
            CHECK_GL_ERROR(glActiveTexture(GL_TEXTURE0));
        } else {
            CHECK_GL_ERROR(glUniform1i(g_use_lightmap_uniform, 0));
        }

        GLenum gl_primitive_type = GL_TRIANGLES;
        switch (PrimitiveType) {
            case D3DPT_TRIANGLELIST: gl_primitive_type = GL_TRIANGLES; break;
            // Others ignored for simplicity as they are rare in indexed
            default: gl_primitive_type = GL_TRIANGLES;
        }

        GLenum gl_index_type = (IndexDataFormat == D3DFMT_INDEX16) ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT;

        CHECK_GL_ERROR(glDrawElements(gl_primitive_type, index_count, gl_index_type, 0));

        CHECK_GL_ERROR(glBindBuffer(GL_ARRAY_BUFFER, 0));
        CHECK_GL_ERROR(glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0));

        // Blend state is managed by game's SetRenderState calls

        return D3D_OK;
    }
    virtual HRESULT CreateTexture(UINT Width, UINT Height, UINT Levels,
                                  DWORD Usage, D3DFORMAT Format, D3DPOOL Pool,
                                  IDirect3DTexture9** ppTexture, void* pSharedHandle) override {
        if (!ppTexture) return D3DERR_INVALIDCALL;

        D3D9Texture* tex = new D3D9Texture(Width, Height, Levels, Format);

        // If this is a render target, allocate texture storage immediately
        // so it can be attached to an FBO without causing "zero size attachment" errors
        if (Usage & D3DUSAGE_RENDERTARGET) {
            glBindTexture(GL_TEXTURE_2D, tex->m_texture);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, Width, Height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
            // Invalidate texture binding cache
            g_last_bound_texture_id = 0xFFFFFFFF;
            g_last_bound_lightmap_id = 0xFFFFFFFF;
            g_cached_texture = 0xFFFFFFFF;
        }

        *ppTexture = tex;
        return D3D_OK;
    }
    
    virtual HRESULT SetTexture(DWORD Stage, IDirect3DTexture9* pTexture) override {
        if (Stage >= 8) return D3D_OK;
        
        GLuint texture_id = 0;
        if (pTexture) {
            D3D9Texture* tex = static_cast<D3D9Texture*>(pTexture);
            texture_id = tex->GetGLTexture();
        }
        
        // Optimization: return early if texture is already set for this stage
        if (g_texture_stages[Stage] == texture_id)
            return D3D_OK;

        g_texture_stages[Stage] = texture_id;
        return D3D_OK;
    }
    
    virtual HRESULT CreateIndexBuffer(UINT Length, DWORD Usage, D3DFORMAT Format,
                                     D3DPOOL Pool, IDirect3DIndexBuffer9** ppIndexBuffer,
                                     void* pSharedHandle) override {
        if (!ppIndexBuffer) return -1;
        *ppIndexBuffer = new D3D9IndexBuffer(Length, Format);
        return D3D_OK;
    }

    virtual HRESULT CreateVertexBuffer(UINT Length, DWORD Usage, DWORD FVF,
                                      D3DPOOL Pool, IDirect3DVertexBuffer9** ppVertexBuffer,
                                      void* pSharedHandle) override {
        if (!ppVertexBuffer) return -1;
        *ppVertexBuffer = new D3D9VertexBuffer(Length);
        return D3D_OK;
    }

    virtual HRESULT SetIndices(IDirect3DIndexBuffer9* pIndexData) override {
        if (pIndexData) {
            D3D9IndexBuffer* ib = static_cast<D3D9IndexBuffer*>(pIndexData);
            m_current_index_buffer = ib->GetGLBuffer();
            m_current_ib_format = ib->GetFormat();
        } else {
            m_current_index_buffer = 0;
            m_current_ib_format = D3DFMT_INDEX16;
        }
        return D3D_OK;
    }
    
    virtual HRESULT SetStreamSource(UINT StreamNumber, IDirect3DVertexBuffer9* pStreamData,
                                   UINT OffsetInBytes, UINT Stride) override {
        if (StreamNumber != 0) return -1; // Only stream 0 supported for now
        
        m_current_stride = Stride; // Store stride
        
        if (pStreamData) {
            D3D9VertexBuffer* vb = static_cast<D3D9VertexBuffer*>(pStreamData);
            m_current_vertex_buffer = vb->GetGLBuffer();
        } else {
            m_current_vertex_buffer = 0;
        }
        return D3D_OK;
    }
    
    virtual HRESULT SetTransform(D3DTRANSFORMSTATETYPE State, const D3DMATRIX* pMatrix) override {
        if (!pMatrix) return -1;
        switch (State) {
            case D3DTS_WORLD: 
                m_world = *pMatrix;
                g_transform_world = *pMatrix;
                break;
            case D3DTS_VIEW: 
                m_view = *pMatrix;
                g_transform_view = *pMatrix;
                break;
            case D3DTS_PROJECTION: 
                m_projection = *pMatrix;
                g_transform_projection = *pMatrix;
                break;
            case D3DTS_TEXTURE0: case D3DTS_TEXTURE1: case D3DTS_TEXTURE2: case D3DTS_TEXTURE3:
            case D3DTS_TEXTURE4: case D3DTS_TEXTURE5: case D3DTS_TEXTURE6: case D3DTS_TEXTURE7:
                g_texture_matrices[State - D3DTS_TEXTURE0] = *pMatrix;
                break;
            default: return -1;
        }
        return D3D_OK;
    }
    
    // D3D9Device methods continued...
    
    DWORD m_render_states[256];

    virtual HRESULT SetRenderState(D3DRENDERSTATETYPE State, DWORD Value) override {
        if ((UINT)State < 256) m_render_states[State] = Value;
        if ((int)State == 152) { // D3DRS_CLIPPLANEENABLE
             m_clip_plane_enabled = (Value != 0);
             return D3D_OK;
        }
        
        switch (State) {
            case D3DRS_ALPHABLENDENABLE:
                if (Value) glEnable(GL_BLEND);
                else glDisable(GL_BLEND);
                break;
                
            case D3DRS_SRCBLEND:
                m_src_blend = d3d_blend_to_gl(Value);
                glBlendFunc(m_src_blend, m_dst_blend);
                break;
                
            case D3DRS_DESTBLEND:
                m_dst_blend = d3d_blend_to_gl(Value);
                glBlendFunc(m_src_blend, m_dst_blend);
                break;

            case D3DRS_CULLMODE:
                // [WASM FIX] We set glFrontFace(GL_CW) to match D3D9's left-handed system
                // Now winding orders align:
                //   D3D9 & OpenGL: CW = front, CCW = back
                //
                // D3DCULL_NONE (1) -> Disable culling
                // D3DCULL_CW (2) -> Cull CW (front) faces -> glCullFace(GL_FRONT)
                // D3DCULL_CCW (3) -> Cull CCW (back) faces -> glCullFace(GL_BACK)

                if (Value == D3DCULL_NONE) {
                    glDisable(GL_CULL_FACE);
                } else {
                    glEnable(GL_CULL_FACE);
                    glCullFace(Value == D3DCULL_CW ? GL_FRONT : GL_BACK);
                }
                break;

            case D3DRS_ZENABLE:
                if (Value) glEnable(GL_DEPTH_TEST);
                else glDisable(GL_DEPTH_TEST);
                break;
                
            case D3DRS_ZWRITEENABLE:
                glDepthMask(Value ? GL_TRUE : GL_FALSE);
                break;

            case D3DRS_ZFUNC:
                glDepthFunc(d3d_cmp_to_gl(Value));
                break;

            case D3DRS_ALPHATESTENABLE:
                m_alpha_test_enabled = (Value != 0);
                break;

            case D3DRS_ALPHAREF:
                m_alpha_ref = Value;
                break;

            case D3DRS_SCISSORTESTENABLE:
                if (Value) {
                    glEnable(GL_SCISSOR_TEST);
                    g_cached_scissor_test = GL_TRUE;
                } else {
                    glDisable(GL_SCISSOR_TEST);
                    g_cached_scissor_test = GL_FALSE;
                }
                break;

            case D3DRS_STENCILENABLE:
                if (Value) glEnable(GL_STENCIL_TEST);
                else glDisable(GL_STENCIL_TEST);
                break;
            case D3DRS_STENCILFUNC: glStencilFunc(d3d_cmp_to_gl(Value), g_current_stencil_ref, 0xFFFFFFFF); break;
            case D3DRS_STENCILREF: g_current_stencil_ref = Value; break;
            case D3DRS_STENCILFAIL: glStencilOp(d3d_stencil_op_to_gl(Value), GL_KEEP, GL_KEEP); break;
            case D3DRS_STENCILZFAIL: glStencilOp(GL_KEEP, d3d_stencil_op_to_gl(Value), GL_KEEP); break;
            case D3DRS_STENCILPASS: glStencilOp(GL_KEEP, GL_KEEP, d3d_stencil_op_to_gl(Value)); break;
            
            case D3DRS_COLORWRITEENABLE:
                glColorMask((Value & D3DCOLORWRITEENABLE_RED) ? GL_TRUE : GL_FALSE,
                           (Value & D3DCOLORWRITEENABLE_GREEN) ? GL_TRUE : GL_FALSE,
                           (Value & D3DCOLORWRITEENABLE_BLUE) ? GL_TRUE : GL_FALSE,
                           (Value & D3DCOLORWRITEENABLE_ALPHA) ? GL_TRUE : GL_FALSE);
                break;
            case D3DRS_TEXTUREFACTOR:
                m_texture_factor = Value;
                break;
        }
        return D3D_OK;
    }

    virtual HRESULT SetScissorRect(const RECT* pRect) override {
        if (!pRect) return D3DERR_INVALIDCALL;
        m_scissor_rect = *pRect;
        
        if (!m_current_rt) return D3D_OK;

        // WebGL Y is bottom-to-top: flip Y origin
        int gl_y = m_current_rt->m_height - pRect->bottom;
        int gl_w = pRect->right - pRect->left;
        int gl_h = pRect->bottom - pRect->top;
        glScissor(pRect->left, gl_y, gl_w, gl_h);
        g_cached_scissor_box[0] = pRect->left;
        g_cached_scissor_box[1] = gl_y;
        g_cached_scissor_box[2] = gl_w;
        g_cached_scissor_box[3] = gl_h;
        return D3D_OK;
    }

    virtual HRESULT GetRenderState(D3DRENDERSTATETYPE State, DWORD* pValue) override {
        if (!pValue) return D3DERR_INVALIDCALL;
        if ((UINT)State < 256) *pValue = m_render_states[State];
        else *pValue = 0;
        return D3D_OK;
    }

    virtual HRESULT CreateDepthStencilSurface(UINT Width, UINT Height, D3DFORMAT Format,
                                             D3DMULTISAMPLE_TYPE MultiSample, DWORD MultisampleQuality,
                                             BOOL Discard, IDirect3DSurface9** ppSurface, void* pSharedHandle) override {
        if (!ppSurface) return D3DERR_INVALIDCALL;
        
        GLuint rb;
        glGenRenderbuffers(1, &rb);
        glBindRenderbuffer(GL_RENDERBUFFER, rb);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, Width, Height); // Use standard D24S8
        glBindRenderbuffer(GL_RENDERBUFFER, 0);
        
        *ppSurface = new D3D9Surface(Width, Height, rb, true);
        return D3D_OK;
    }

    virtual HRESULT SetClipPlane(DWORD Index, const float* pPlane) override {
        if (Index > 0 || !pPlane) return D3DERR_INVALIDCALL;
        memcpy(m_clip_planes[Index], pPlane, 4 * sizeof(float));
        return D3D_OK;
    }

    virtual HRESULT GetClipPlane(DWORD Index, float* pPlane) override {
        if (Index > 0 || !pPlane) return D3DERR_INVALIDCALL;
        memcpy(pPlane, m_clip_planes[Index], 4 * sizeof(float));
        return D3D_OK;
    }

    // FFP APIs Implementation
    virtual HRESULT GetDeviceCaps(D3DCAPS9* pCaps) override {
        if (!pCaps) return -1;
        
        memset(pCaps, 0, sizeof(D3DCAPS9));
        
#ifdef __EMSCRIPTEN__
        // CRITICAL: Lie about shader support to force FFP mode
        pCaps->VertexShaderVersion = 0;  // No vertex shader support
        pCaps->PixelShaderVersion = 0;   // No pixel shader support
#else
        // Native builds could report real capabilities
        pCaps->VertexShaderVersion = D3DVS_VERSION(2, 0);
        pCaps->PixelShaderVersion = D3DPS_VERSION(2, 0);
#endif
        
        // Additional capabilities
        pCaps->DevCaps = D3DDEVCAPS_HWTRANSFORMANDLIGHT; // Claim HW T&L support
        pCaps->MaxPrimitiveCount = 0xFFFFFFFF;
        pCaps->NumSimultaneousRTs = 1;
        pCaps->MaxStreams = 16;
        pCaps->MaxUserClipPlanes = 1; // Emulated via fragment shader discard
        pCaps->RasterCaps = 0; // No W-fog
        pCaps->MaxVertexShaderConst = 0; // No shader constants since VS version is 0
        
        return D3D_OK;
    }

    virtual UINT GetAvailableTextureMem() override {
        return 128 * 1024 * 1024; // 128MB
    }

    virtual HRESULT ValidateDevice(DWORD* pNumPasses) override {
        if (pNumPasses) *pNumPasses = 1;
        return D3D_OK;
    }
    
    virtual HRESULT SetLight(DWORD Index, const D3DLIGHT9* pLight) override {
        if (Index >= 8 || !pLight) return D3DERR_INVALIDCALL;
        g_lights[Index] = *pLight;
        return D3D_OK;
    }
    
    virtual HRESULT LightEnable(DWORD Index, BOOL Enable) override {
        if (Index >= 8) return D3DERR_INVALIDCALL;
        g_light_enabled[Index] = Enable != FALSE;
        return D3D_OK;
    }

    virtual HRESULT GetLightEnable(DWORD Index, BOOL* pEnable) override {
        if (Index >= 8 || !pEnable) return D3DERR_INVALIDCALL;
        *pEnable = g_light_enabled[Index] ? TRUE : FALSE;
        return D3D_OK;
    }

    virtual HRESULT GetLight(DWORD Index, D3DLIGHT9* pLight) override {
        if (Index >= 8 || !pLight) return D3DERR_INVALIDCALL;
        *pLight = g_lights[Index];
        return D3D_OK;
    }
    
    virtual HRESULT SetMaterial(const D3DMATERIAL9* pMaterial) override {
        if (!pMaterial) return D3DERR_INVALIDCALL;
        g_material = *pMaterial;
        return D3D_OK;
    }
    
    virtual HRESULT SetTextureStageState(DWORD Stage, D3DTEXTURESTAGESTATETYPE Type, DWORD Value) override {
        if (Stage >= 8 || Type >= 33) return D3DERR_INVALIDCALL;
        g_texture_stage_states[Stage][Type] = Value;
        return D3D_OK;
    }
    
    virtual HRESULT SetSamplerState(DWORD Sampler, D3DSAMPLERSTATETYPE Type, DWORD Value) override {
        if (Sampler >= 8) return D3DERR_INVALIDCALL;
        if (Type < 14) {
            g_sampler_states[Sampler][Type] = Value;
        }
        return D3D_OK;
    }
    
    virtual HRESULT GetViewport(D3DVIEWPORT9* pViewport) override {
        if (!pViewport) return D3DERR_INVALIDCALL;
        *pViewport = g_viewport;
        return D3D_OK;
    }
    
    virtual HRESULT SetViewport(const D3DVIEWPORT9* pViewport) override {
        if (!pViewport) return D3DERR_INVALIDCALL;
        g_viewport = *pViewport;

        if (!m_current_rt) return D3D_OK;

        // WebGL Y is bottom-to-top: flip Y origin
        int gl_y = m_current_rt->m_height - (pViewport->Y + pViewport->Height);
        glViewport(pViewport->X, gl_y, pViewport->Width, pViewport->Height);
        g_cached_viewport[0] = pViewport->X;
        g_cached_viewport[1] = gl_y;
        g_cached_viewport[2] = pViewport->Width;
        g_cached_viewport[3] = pViewport->Height;

        glDepthRangef(pViewport->MinZ, pViewport->MaxZ);
        return D3D_OK;
    }
    
    virtual HRESULT GetTransform(D3DTRANSFORMSTATETYPE State, D3DMATRIX* pMatrix) override {
        if (!pMatrix) return -1;
        
        switch (State) {
            case D3DTS_WORLD:
                *pMatrix = m_world;
                break;
            case D3DTS_VIEW:
                *pMatrix = m_view;
                break;
            case D3DTS_PROJECTION:
                *pMatrix = m_projection;
                break;
            default:
                return -1;
        }
        
        return D3D_OK;
    }
    
    // Shader stubs (log warnings if called)
    virtual HRESULT CreateVertexDeclaration(const void* pVertexElements, IDirect3DVertexDeclaration9** ppDecl) override {
        printf("[D3D9 Wrapper WARNING] CreateVertexDeclaration called - shaders should be disabled!\n");
        if (ppDecl) *ppDecl = nullptr;
        return D3D_OK; // Return success to avoid crashes
    }
    
    virtual HRESULT SetVertexDeclaration(void* pDecl) override {
        printf("[D3D9 Wrapper WARNING] SetVertexDeclaration called - shaders should be disabled!\n");
        return D3D_OK;
    }
    
    virtual HRESULT SetVertexShader(void* pShader) override {
        // Debug: Only log warning once to reduce log spam during loading
        if (!s_vertex_shader_warning_shown) {
            printf("[D3D9 Wrapper WARNING] SetVertexShader called - shaders should be disabled! (further warnings suppressed)\n");
            s_vertex_shader_warning_shown = true;
        }
        return D3D_OK;
    }
    
    virtual HRESULT SetPixelShader(void* pShader) override {
        // Debug: Only log warning once to reduce log spam during loading
        if (!s_pixel_shader_warning_shown) {
            printf("[D3D9 Wrapper WARNING] SetPixelShader called - shaders should be disabled! (further warnings suppressed)\n");
            s_pixel_shader_warning_shown = true;
        }
        return D3D_OK;
    }
    
    virtual HRESULT SetVertexShaderConstantF(UINT StartRegister, const float* pConstantData, UINT Vector4fCount) override {
        // Debug: Only log warning once to reduce log spam during loading
        if (!s_vertex_shader_constant_warning_shown) {
            printf("[D3D9 Wrapper WARNING] SetVertexShaderConstantF called - shaders should be disabled! (further warnings suppressed)\n");
            s_vertex_shader_constant_warning_shown = true;
        }
        return D3D_OK;
    }


    virtual HRESULT DrawIndexedPrimitive(D3DPRIMITIVETYPE PrimitiveType,
                                        INT BaseVertexIndex,
                                        UINT MinVertexIndex,
                                        UINT NumVertices,
                                        UINT startIndex,
                                        UINT primCount) override {
        if (g_shader_program == 0) init_shaders();
        glUseProgram(g_shader_program);

        // FBO Y-flip and clip plane uniforms
        glUniform1i(g_fbo_flip_y_uniform, m_fbo_rendering ? 1 : 0);
        glUniform1i(g_clip_plane_enabled_uniform, m_clip_plane_enabled ? 1 : 0);
        if (m_clip_plane_enabled) glUniform4fv(g_clip_plane_uniform, 1, m_clip_planes[0]);

        if (m_current_vertex_buffer == 0 || m_current_index_buffer == 0) return -1;

        // Calculate MVP
        D3DMATRIX view_model, mvp;
        if (g_current_fvf & D3DFVF_XYZRHW) {
            float sw = (m_current_rt) ? (float)m_current_rt->m_width : (float)g_logical_width;
            float sh = (m_current_rt) ? (float)m_current_rt->m_height : (float)g_logical_height;
            
            memset(&mvp, 0, sizeof(D3DMATRIX));
            mvp.m[0][0] = 2.0f / sw;
            mvp.m[1][1] = -2.0f / sh;
            mvp.m[2][2] = 1.0f; 
            mvp.m[3][0] = -1.0f;
            mvp.m[3][1] = 1.0f;
            mvp.m[3][3] = 1.0f;
            
            // 2D UI states
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glDisable(GL_DEPTH_TEST);
            glDisable(GL_CULL_FACE);
            glDepthMask(GL_FALSE);
        } else {
            matrix_multiply(&view_model, &m_world, &m_view);
            matrix_multiply(&mvp, &view_model, &m_projection);
        }

        // Winding detection: D3D9 auto-flips culling for mirrored transforms; we emulate it
        if (!(g_current_fvf & D3DFVF_XYZRHW)) {
            bool mirror = (det3x3(view_model) < 0.0f);
            glFrontFace((mirror != m_fbo_rendering) ? GL_CCW : GL_CW);
        } else {
            glFrontFace(GL_CW);
        }

        gl_set_matrix_uniform(g_mvp_uniform, &mvp);

        // Set texture factor
        if (g_texture_factor_uniform != -1) {
            float r = ((m_texture_factor >> 16) & 0xFF) / 255.0f;
            float g = ((m_texture_factor >> 8) & 0xFF) / 255.0f;
            float b = (m_texture_factor & 0xFF) / 255.0f;
            float a = ((m_texture_factor >> 24) & 0xFF) / 255.0f;
            glUniform4f(g_texture_factor_uniform, r, g, b, a);
        }

        // Set mipmap LOD bias (D3D9 stores float as DWORD via type-punning).
        // WebGL/OpenGL's LOD calculation uses a "max" rule that tends to select
        // lower-res mip levels compared to D3D9 on the same hardware. An additional
        // -0.75 offset compensates for this difference.
        if (g_lod_bias_uniform != -1) {
            DWORD bias_bits = g_sampler_states[0][D3DSAMP_MIPMAPLODBIAS];
            float lod_bias;
            memcpy(&lod_bias, &bias_bits, sizeof(float));
            lod_bias -= 0.75f;
            glUniform1f(g_lod_bias_uniform, lod_bias);
        }

        // Set Stage 0 texture stage states
        if (g_stage0_colorop_uniform != -1) {
            glUniform1i(g_stage0_colorop_uniform, g_texture_stage_states[0][D3DTSS_COLOROP]);
        }
        if (g_stage0_colorarg1_uniform != -1) {
            glUniform1i(g_stage0_colorarg1_uniform, g_texture_stage_states[0][D3DTSS_COLORARG1]);
        }
        if (g_stage0_colorarg2_uniform != -1) {
            glUniform1i(g_stage0_colorarg2_uniform, g_texture_stage_states[0][D3DTSS_COLORARG2]);
        }
        if (g_stage0_alphaop_uniform != -1) {
            glUniform1i(g_stage0_alphaop_uniform, g_texture_stage_states[0][D3DTSS_ALPHAOP]);
        }
        if (g_stage0_alphaarg1_uniform != -1) {
            glUniform1i(g_stage0_alphaarg1_uniform, g_texture_stage_states[0][D3DTSS_ALPHAARG1]);
        }
        if (g_stage0_alphaarg2_uniform != -1) {
            glUniform1i(g_stage0_alphaarg2_uniform, g_texture_stage_states[0][D3DTSS_ALPHAARG2]);
        }

        // Bind buffers
        glBindBuffer(GL_ARRAY_BUFFER, m_current_vertex_buffer);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_current_index_buffer);
        
        // Dynamic FVF Parsing
        bool has_xyz = (g_current_fvf & D3DFVF_XYZ) != 0;
        bool has_xyzrhw = (g_current_fvf & D3DFVF_XYZRHW) != 0;
        bool has_normal = (g_current_fvf & D3DFVF_NORMAL) != 0;
        bool has_diffuse = (g_current_fvf & D3DFVF_DIFFUSE) != 0;
        int tex_count = (g_current_fvf >> 8) & 0xF;

        GLsizei stride = 0;
        size_t position_offset = 0;
        size_t normal_offset = 0;
        size_t color_offset = 0;
        size_t tex_offset = 0;

        // XYZ and XYZRHW are mutually exclusive in D3D9 FVF
        if (has_xyzrhw) { position_offset = stride; stride += 16; }
        else if (has_xyz) { position_offset = stride; stride += 12; }
        if (has_normal) { normal_offset = stride; stride += 12; }
        if (has_diffuse) { color_offset = stride; stride += 4; }
        tex_offset = stride;
        stride += tex_count * 8;

        // Use stored stride if valid
        if (m_current_stride > 0) stride = m_current_stride;

        // BaseVertexIndex support
        size_t base_offset = BaseVertexIndex * stride;

        // Enable/Set Pointers with safety guards
        if ((has_xyz || has_xyzrhw) && g_position_location != -1) {
            glEnableVertexAttribArray(g_position_location);
            GLint pos_component_count = has_xyzrhw ? 4 : 3;
            glVertexAttribPointer(g_position_location, pos_component_count, GL_FLOAT, GL_FALSE, stride, (void*)(base_offset + position_offset));
        }

        // D3D9 FFP Lighting
        bool lighting_enabled = has_normal && !has_xyzrhw && m_render_states[D3DRS_LIGHTING];
        if (lighting_enabled) {
            // Normal attribute
            if (g_normal_location != -1) {
                glEnableVertexAttribArray(g_normal_location);
                glVertexAttribPointer(g_normal_location, 3, GL_FLOAT, GL_FALSE, stride, (void*)(base_offset + normal_offset));
            }

            glUniform1i(g_lighting_enabled_uniform, 1);
            SetFFPLightingUniforms(&m_world, m_render_states);

            glDisableVertexAttribArray(g_color_location);
            glVertexAttrib4f(g_color_location, 1.0f, 1.0f, 1.0f, 1.0f);
        } else {
            glUniform1i(g_lighting_enabled_uniform, 0);
            // Always set world matrix for clip plane (v_position_world) even without lighting
            gl_set_matrix_uniform(g_world_uniform, &m_world);
            if (g_normal_location != -1) {
                glDisableVertexAttribArray(g_normal_location);
            }

            if (has_diffuse && g_color_location != -1) {
                glEnableVertexAttribArray(g_color_location);
                glVertexAttribPointer(g_color_location, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride, (void*)(base_offset + color_offset));
            } else if (g_color_location != -1) {
                glDisableVertexAttribArray(g_color_location);
                glVertexAttrib4f(g_color_location, 1.0f, 1.0f, 1.0f, 1.0f);
            }
        }

        if (tex_count > 0 && g_texcoord_location != -1) {
            glEnableVertexAttribArray(g_texcoord_location);
            glVertexAttribPointer(g_texcoord_location, 2, GL_FLOAT, GL_FALSE, stride, (void*)(base_offset + tex_offset));
        } else if (g_texcoord_location != -1) {
            glDisableVertexAttribArray(g_texcoord_location);
            glVertexAttrib2f(g_texcoord_location, 0.0f, 0.0f);
        }

        if (tex_count > 1 && g_texcoord1_location != -1) {
            glEnableVertexAttribArray(g_texcoord1_location);
            glVertexAttribPointer(g_texcoord1_location, 2, GL_FLOAT, GL_FALSE, stride, (void*)(base_offset + tex_offset + 8));
        } else if (g_texcoord1_location != -1) {
            glDisableVertexAttribArray(g_texcoord1_location);
            glVertexAttrib2f(g_texcoord1_location, 0.0f, 0.0f);
        }
        
        // Texture setup
        if (g_texture_stages[0] != 0 && tex_count > 0) {
            glActiveTexture(GL_TEXTURE0);
            
            // Optimization: Avoid redundant texture binding
            if (g_last_bound_texture_id != g_texture_stages[0]) {
                glBindTexture(GL_TEXTURE_2D, g_texture_stages[0]);
                g_last_bound_texture_id = g_texture_stages[0];
            }
            
            // Apply cached sampler states
            ApplySamplerStates(0);

            glUniform1i(g_texture_uniform, 0);
            glUniform1i(g_use_texture_uniform, 1);

            // Texture transform
            if (g_texture_stage_states[0][D3DTSS_TEXTURETRANSFORMFLAGS] != D3DTTFF_DISABLE) {
                // D3D row-major matrices. Shader does u_tex_matrix * vec4(uv, 0, 1).
                // We need to pass the matrix correctly.
                glUniformMatrix4fv(g_tex_matrix_uniform, 1, GL_FALSE, &g_texture_matrices[0].m[0][0]);
                glUniform1i(g_use_tex_matrix_uniform, 1);
            } else {
                glUniform1i(g_use_tex_matrix_uniform, 0);
            }
        } else {
            glUniform1i(g_use_texture_uniform, 0);
            glUniform1i(g_use_tex_matrix_uniform, 0);
        }

        // Lightmap setup (Stage 1) - requires at least 2 texture coordinate sets
        if (g_texture_stages[1] != 0 && tex_count > 1) {
            glActiveTexture(GL_TEXTURE1);
            if (g_last_bound_lightmap_id != g_texture_stages[1]) {
                glBindTexture(GL_TEXTURE_2D, g_texture_stages[1]);
                g_last_bound_lightmap_id = g_texture_stages[1];
            }

            // Apply sampler states for Stage 1 (reuse shared logic for mipmap awareness)
            ApplySamplerStates(1);

            glUniform1i(g_texture_lm_uniform, 1);
            glUniform1i(g_use_lightmap_uniform, 1);

            // Apply texture stage operation mode
            DWORD op = g_texture_stage_states[1][D3DTSS_COLOROP];
            glUniform1i(g_stage1_op_uniform, op);

            // Restore active texture unit to TEXTURE0 so subsequent draw calls
            // (e.g., DrawIndexedPrimitiveUP/DrawPrimitiveUP) don't accidentally
            // operate on GL_TEXTURE1 when they assume TEXTURE0 is active.
            glActiveTexture(GL_TEXTURE0);
        } else {
            glUniform1i(g_use_lightmap_uniform, 0);
        }

        glUniform1i(g_alpha_test_enabled_uniform, m_alpha_test_enabled ? 1 : 0);
        glUniform1f(g_alpha_ref_uniform, (float)m_alpha_ref);

        // Draw
        GLenum gl_primitive_type = GL_TRIANGLES;
        UINT index_count = 0;
        switch (PrimitiveType) {
            case D3DPT_TRIANGLELIST: gl_primitive_type = GL_TRIANGLES; index_count = primCount * 3; break;
            case D3DPT_TRIANGLESTRIP: gl_primitive_type = GL_TRIANGLE_STRIP; index_count = primCount + 2; break;
            case D3DPT_TRIANGLEFAN: gl_primitive_type = GL_TRIANGLE_FAN; index_count = primCount + 2; break;
            case D3DPT_LINELIST: gl_primitive_type = GL_LINES; index_count = primCount * 2; break;
            case D3DPT_LINESTRIP: gl_primitive_type = GL_LINE_STRIP; index_count = primCount + 1; break;
            case D3DPT_POINTLIST: gl_primitive_type = GL_POINTS; index_count = primCount; break;
        }

        // Offset in index buffer (respects 16-bit vs 32-bit index format)
        bool is_index32 = (m_current_ib_format == D3DFMT_INDEX32);
        GLenum gl_index_type = is_index32 ? GL_UNSIGNED_INT : GL_UNSIGNED_SHORT;
        size_t index_stride = is_index32 ? 4 : 2;
        void* indices = (void*)(uintptr_t)(startIndex * index_stride);

        glDrawElements(gl_primitive_type, index_count, gl_index_type, indices);

        glDisableVertexAttribArray(g_position_location);
        glDisableVertexAttribArray(g_color_location);
        glDisableVertexAttribArray(g_texcoord_location);
        glDisableVertexAttribArray(g_texcoord1_location);
        if (g_normal_location != -1) glDisableVertexAttribArray(g_normal_location);

        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

        // Restore GL states if XYZRHW path overrode them
        if (g_current_fvf & D3DFVF_XYZRHW) {
            if (m_render_states[D3DRS_ZENABLE]) glEnable(GL_DEPTH_TEST);
            if (m_render_states[D3DRS_ZWRITEENABLE]) glDepthMask(GL_TRUE);
            if (m_render_states[D3DRS_CULLMODE] != D3DCULL_NONE) glEnable(GL_CULL_FACE);
            // Restore blend state from tracked render states
            if (!m_render_states[D3DRS_ALPHABLENDENABLE]) glDisable(GL_BLEND);
            else { glEnable(GL_BLEND); glBlendFunc(m_src_blend, m_dst_blend); }
        }

        return D3D_OK;
    }

    virtual HRESULT CreateOffscreenPlainSurface(UINT Width, UINT Height, D3DFORMAT Format, D3DPOOL Pool, IDirect3DSurface9** ppSurface, void* pSharedHandle) override {
        if (!ppSurface) return D3DERR_INVALIDCALL;
        *ppSurface = new D3D9Surface(Width, Height, 0, false); 
        return D3D_OK;
    }

    virtual HRESULT GetRenderTargetData(IDirect3DSurface9* pRenderTarget, IDirect3DSurface9* pDestSurface) override {
        return D3D_OK;
    }

    virtual HRESULT GetFrontBufferData(UINT iSwapChain, IDirect3DSurface9* pDestSurface) override {
        return D3D_OK;
    }

    virtual HRESULT TestCooperativeLevel() override {
        return D3D_OK;
    }

    virtual HRESULT Reset(D3DPRESENT_PARAMETERS* pPresentationParameters) override {
        return D3D_OK;
    }
    virtual void SetGammaRamp(UINT iSwapChain, DWORD Flags, const D3DGAMMARAMP* pRamp) override {
    }

    virtual void GetGammaRamp(UINT iSwapChain, D3DGAMMARAMP* pRamp) override {
        if(pRamp) memset(pRamp, 0, sizeof(D3DGAMMARAMP));
    }

    virtual HRESULT GetDisplayMode(UINT iSwapChain, D3DDISPLAYMODE* pMode) override {
        if (!pMode) return D3DERR_INVALIDCALL;
        pMode->Width = g_logical_width;
        pMode->Height = g_logical_height;
        pMode->RefreshRate = 60;
        pMode->Format = D3DFMT_X8R8G8B8;
        return D3D_OK;
    }

    virtual HRESULT CreateStateBlock(D3DSTATEBLOCKTYPE Type, IDirect3DStateBlock9** ppSB) override {
        if (ppSB) *ppSB = new D3D9StateBlock();
        return D3D_OK;
    }
};

// Initialize static member variables for shader warning suppression
bool D3D9Device::s_pixel_shader_warning_shown = false;
bool D3D9Device::s_vertex_shader_warning_shown = false;
bool D3D9Device::s_vertex_shader_constant_warning_shown = false;


class D3D9 : public IDirect3D9 {
public:
    virtual HRESULT CreateDevice(UINT Adapter, D3DDEVTYPE DeviceType,
                                HWND hFocusWindow, DWORD BehaviorFlags,
                                D3DPRESENT_PARAMETERS* pPresentationParameters,
                                IDirect3DDevice9** ppReturnedDeviceInterface) override {
        if (!ppReturnedDeviceInterface) return -1;
        
        // Create WebGL context
        EmscriptenWebGLContextAttributes attrs;
        emscripten_webgl_init_context_attributes(&attrs);
        attrs.majorVersion = 2;
        attrs.minorVersion = 0;
        
        // D3D9 backbuffer is D3DFMT_X8R8G8B8 (no alpha channel)
        // Canvas alpha must be disabled to match, otherwise DXT3 texture alpha
        // leaks into framebuffer and browser compositing darkens the output
        attrs.alpha = EM_FALSE;
        
        g_webgl_context = emscripten_webgl_create_context(g_canvas_selector, &attrs);
        if (g_webgl_context <= 0) {
            printf("Failed to create WebGL context\n");
            return -1;
        }
        emscripten_webgl_make_context_current(g_webgl_context);

        // [WASM] Enable S3TC extensions for compressed textures
        if (emscripten_webgl_enable_extension(g_webgl_context, "WEBGL_compressed_texture_s3tc")) {
        } else {
        }

        // [WASM] Enable anisotropic filtering extension
        if (emscripten_webgl_enable_extension(g_webgl_context, "EXT_texture_filter_anisotropic")) {
            glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &g_max_anisotropy);
            if (g_max_anisotropy > 16.0f) g_max_anisotropy = 16.0f;
            printf("[D3D9] Anisotropic filtering enabled (max %.0fx)\n", g_max_anisotropy);
        } else {
            g_max_anisotropy = 1.0f;
            printf("[D3D9] Anisotropic filtering not available\n");
        }

        // Determine backbuffer resolution: use app's request, fall back to canvas size
        if (pPresentationParameters &&
            pPresentationParameters->BackBufferWidth > 0 &&
            pPresentationParameters->BackBufferHeight > 0) {
            g_logical_width = pPresentationParameters->BackBufferWidth;
            g_logical_height = pPresentationParameters->BackBufferHeight;
        } else {
            int w, h;
            emscripten_get_canvas_element_size(g_canvas_selector, &w, &h);
            g_logical_width = w;
            g_logical_height = h;
        }

        // Set canvas drawingBuffer to match backbuffer size (D3D9-compliant: fixed at CreateDevice)
        emscripten_set_canvas_element_size(g_canvas_selector, g_logical_width, g_logical_height);

        *ppReturnedDeviceInterface = new D3D9Device();

        g_viewport.Width = g_logical_width;
        g_viewport.Height = g_logical_height;
        glViewport(0, 0, g_logical_width, g_logical_height);

        return D3D_OK;
    }

    // Extended overload: specify which canvas element to render to.
    // Falls back to the standard CreateDevice with g_canvas_selector set.
    HRESULT CreateDevice(UINT Adapter, D3DDEVTYPE DeviceType,
                         HWND hFocusWindow, DWORD BehaviorFlags,
                         D3DPRESENT_PARAMETERS* pPresentationParameters,
                         IDirect3DDevice9** ppReturnedDeviceInterface,
                         const char* canvasSelector) {
        if (canvasSelector && canvasSelector[0]) {
            g_canvas_selector = canvasSelector;
        }
        return CreateDevice(Adapter, DeviceType, hFocusWindow, BehaviorFlags,
                            pPresentationParameters, ppReturnedDeviceInterface);
    }

    virtual ULONG AddRef() override {
        return 1; // Stub: no ref counting for D3D9 interface
    }

    virtual ULONG Release() override {
        delete this;
        return 0;
    }
    
    virtual UINT GetAdapterCount() override {
        return 1; // Single adapter for WebGL
    }

    virtual HRESULT GetAdapterIdentifier(UINT Adapter, DWORD Flags, D3DADAPTER_IDENTIFIER9* pIdentifier) override {
        if (!pIdentifier) return D3DERR_INVALIDCALL;
        memset(pIdentifier, 0, sizeof(D3DADAPTER_IDENTIFIER9));
        strncpy(pIdentifier->Driver, "emscripten_webgl", MAX_DEVICE_IDENTIFIER_STRING);
        strncpy(pIdentifier->Description, "Emscripten WebGL Wrapper", MAX_DEVICE_IDENTIFIER_STRING);
        return D3D_OK;
    }



    virtual UINT GetAdapterModeCount(UINT Adapter, D3DFORMAT Format) override {
        return 1;
    }

    virtual HRESULT EnumAdapterModes(UINT Adapter, D3DFORMAT Format, UINT Mode, D3DDISPLAYMODE* pMode) override {
        if (!pMode) return D3DERR_INVALIDCALL;
        if (Mode >= 1) return D3DERR_INVALIDCALL;

        int w, h;
        emscripten_get_canvas_element_size(g_canvas_selector, &w, &h);
        pMode->Width = w;
        pMode->Height = h;
        pMode->RefreshRate = 60;
        pMode->Format = Format;
        return D3D_OK;
    }

    virtual HRESULT GetAdapterDisplayMode(UINT Adapter, D3DDISPLAYMODE* pMode) override {
        if (!pMode) return D3DERR_INVALIDCALL;
        int w, h;
        emscripten_get_canvas_element_size(g_canvas_selector, &w, &h);
        pMode->Width = w;
        pMode->Height = h;
        pMode->RefreshRate = 60;
        pMode->Format = D3DFMT_X8R8G8B8;
        return D3D_OK;
    }

    virtual HRESULT CheckDeviceFormat(UINT Adapter, D3DDEVTYPE DeviceType, D3DFORMAT AdapterFormat, DWORD Usage, D3DRESOURCETYPE RType, D3DFORMAT CheckFormat) override {
        return D3D_OK;
    }

    virtual HRESULT CheckDepthStencilMatch(UINT Adapter, D3DDEVTYPE DeviceType, D3DFORMAT AdapterFormat, D3DFORMAT RenderTargetFormat, D3DFORMAT DepthStencilFormat) override {
        return D3D_OK;
    }


    virtual HRESULT GetDeviceCaps(UINT Adapter, D3DDEVTYPE DeviceType, D3DCAPS9* pCaps) override {
        // Shared implementation "lie"
        if (!pCaps) return -1;
        memset(pCaps, 0, sizeof(D3DCAPS9));
#ifdef __EMSCRIPTEN__
        pCaps->VertexShaderVersion = 0;
        pCaps->PixelShaderVersion = 0;
#else
        pCaps->VertexShaderVersion = D3DVS_VERSION(2, 0);
        pCaps->PixelShaderVersion = D3DPS_VERSION(2, 0);
#endif
        pCaps->DevCaps = D3DDEVCAPS_HWTRANSFORMANDLIGHT;
        pCaps->MaxPrimitiveCount = 0xFFFFFFFF;
        pCaps->NumSimultaneousRTs = 1;
        pCaps->MaxStreams = 16;
        pCaps->MaxUserClipPlanes = 1; // Emulated via fragment shader discard
        pCaps->RasterCaps = D3DPRASTERCAPS_WFOG | D3DPRASTERCAPS_ZTEST;
        pCaps->MaxVertexShaderConst = 0;
        return D3D_OK;
    }
};

// Helper: Load texture from file
// Note: In a real D3DX scenario, this would be D3DXCreateTextureFromFile
IDirect3DTexture9* LoadTextureFromFile(IDirect3DDevice9* device, const char* filename) {
    int w, h, channels;
    unsigned char* data = stbi_load(filename, &w, &h, &channels, 4); // Force 4 channels (RGBA)
    if (!data) {
        printf("Failed to load image: %s\n", filename);
        return nullptr;
    }
    
    IDirect3DTexture9* texture = nullptr;
    if (FAILED(device->CreateTexture(w, h, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &texture, nullptr))) {
        stbi_image_free(data);
        printf("Failed to create texture for %s\n", filename);
        return nullptr;
    }
    
    D3DLOCKED_RECT rect;
    if (SUCCEEDED(texture->LockRect(0, &rect, nullptr, 0))) {
        // Data from stbi_load is RGBA. 
        // Our D3D9Texture::UnlockRect expects BGRA (ARGB in D3D terms) which it expects to SWIZZLE to RGBA.
        // ... (as before) ...
        uint32_t* src_pixels = (uint32_t*)data;
        uint32_t* dst_pixels = (uint32_t*)rect.pBits;
        int count = w * h;
        
        for (int i=0; i<count; i++) {
            uint32_t color = src_pixels[i];
            uint8_t* p = (uint8_t*)&color;
            uint8_t r = p[0];
            uint8_t g = p[1];
            uint8_t b = p[2];
            uint8_t a = p[3];
            
            // [WASM] Don't force alpha anymore to allow transparency
            // if (a == 0) a = 255;
            
            // Write B G R A (D3D ARGB Little Endian: 0xAARRGGBB)
            uint8_t* d = (uint8_t*)&dst_pixels[i];
            d[0] = b;
            d[1] = g;
            d[2] = r;
            d[3] = a;
        }
        
        texture->UnlockRect(0);
    }
    
    stbi_image_free(data);
    return texture;
}

// Entry point
extern "C" IDirect3D9* WINAPI Direct3DCreate9(UINT SDKVersion) {
    return new D3D9();
}

extern "C" void D3D9_RegisterTextureName(IDirect3DTexture9*, const char*) {}

// Accessor functions for external modules (e.g. text rendering bridge)
extern "C" {
    void D3D9_GetTextureInfo(IDirect3DTexture9* pTex, unsigned int* gl_id, unsigned int* w, unsigned int* h) {
        if (!pTex) return;
        D3D9Texture* tex = (D3D9Texture*)pTex;
        if (gl_id) *gl_id = tex->m_texture;
        if (w) *w = tex->m_width;
        if (h) *h = tex->m_height;
    }

    void D3D9_InvalidateTextureCache() {
        g_last_bound_texture_id = 0xFFFFFFFF;
        g_last_bound_lightmap_id = 0xFFFFFFFF;
        g_cached_texture = 0xFFFFFFFF;
    }
}



