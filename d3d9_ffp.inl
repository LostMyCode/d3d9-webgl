// Direct3D 9 fixed-function pipeline on WebGL 2.
//
// Included by d3d9.cpp (it uses the device state kept there). Every draw call
// describes the current state as an FFPKey - vertex format, lighting setup,
// texture stages, fog, alpha test - and gets a GLSL program generated for
// exactly that state (and cached), the way drivers implement the FFP:
//
//   - transform: world/view/projection, pre-transformed (XYZRHW) vertices,
//     D3D9's half-pixel offset, its [0,w] clip-space depth
//   - lighting: up to 8 directional, point and spot lights with attenuation,
//     range, cone falloff and specular; global ambient; emissive; material
//     colour sources (D3DRS_*MATERIALSOURCE) and D3DRS_COLORVERTEX
//   - texture coordinates: D3DTSS_TEXCOORDINDEX with the generated camera-space
//     normal, position, reflection vector and sphere map; texture transforms
//     (D3DTTFF_COUNT1..4, PROJECTED) with D3D9's handling of 2D coordinates
//   - texture stages: up to 4 stages with all D3DTOP colour/alpha operations
//     (except bump mapping), all D3DTA arguments and modifiers, RESULTARG TEMP
//   - specular add, fog (vertex/table, linear/exp/exp2, range), alpha test
//     with every D3DCMP function, one user clip plane

#define FFP_MAX_STAGES 4
#define FFP_MAX_LIGHTS 8

// D3D9 values the header may not declare
#define FFP_RS_COLORVERTEX 141
#define FFP_RS_LOCALVIEWER 142
#define FFP_RS_NORMALIZENORMALS 143
#define FFP_RS_DIFFUSEMATERIALSOURCE 145
#define FFP_RS_SPECULARMATERIALSOURCE 146
#define FFP_RS_AMBIENTMATERIALSOURCE 147
#define FFP_RS_EMISSIVEMATERIALSOURCE 148
#define FFP_TCI_SPHEREMAP 0x00040000
#define FFP_TCI_REFLECTION 0x00030000
#define FFP_TA_COMPLEMENT 0x10
#define FFP_TA_ALPHAREPLICATE 0x20

// Fixed attribute locations shared by all generated programs
enum { FFP_ATTR_POS = 0, FFP_ATTR_NORMAL, FFP_ATTR_DIFFUSE, FFP_ATTR_SPECULAR, FFP_ATTR_TEX0, FFP_ATTR_COUNT = FFP_ATTR_TEX0 + 8 };

struct FFPStageKey {
    uint8_t colorop, carg0, carg1, carg2;
    uint8_t alphaop, aarg0, aarg1, aarg2;
    uint8_t tc_index;   // texture coordinate set
    uint8_t tc_gen;     // 0 passthru, 1 normal, 2 position, 3 reflection, 4 sphere map
    uint8_t ttff;       // D3DTTFF_COUNTn (0 = no transform)
    uint8_t projected;
    uint8_t has_texture;
    uint8_t result_temp;
};

struct FFPKey {
    uint8_t rhw, has_normal, has_diffuse, has_specular, tex_count;
    uint8_t tex_size[8];
    uint8_t lighting, normalize, local_viewer, specular;
    uint8_t mat_src[4];             // diffuse, specular, ambient, emissive: 0 material, 1 diffuse, 2 specular
    uint8_t light_type[FFP_MAX_LIGHTS];  // 0 off, D3DLIGHT_POINT/SPOT/DIRECTIONAL
    uint8_t fog;                    // 0 none, D3DFOG_EXP/EXP2/LINEAR, 4 = specular alpha
    uint8_t fog_range;
    uint8_t fog_table;              // table fog: computed per pixel from the eye depth
    uint8_t alpha_func;             // 0 = no alpha test
    uint8_t clip;
    uint8_t stage_count;
    FFPStageKey stage[FFP_MAX_STAGES];
};

struct FFPProgram {
    GLuint program;
    GLint wv, proj, nrm, world, rhw, pixel, flip;
    GLint mdiff, mamb, mspec, memis, mpower, gamb;
    GLint lpos, ldir, ldiff, lspec, lamb, latt, lspot;
    GLint texmat[FFP_MAX_STAGES];
    GLint tfactor, alpharef, fog, fogcolor, clipplane, lodbias;
};

static std::map<std::string, FFPProgram*> g_ffp_programs;
static GLuint g_ffp_current_program = 0;
static unsigned g_ffp_enabled_attribs = 0;

// ---------------------------------------------------------------------------
// Vertex format

static int FFP_TexSize(DWORD fvf, int i) {
    static const int sizes[4] = {2, 3, 4, 1}; // D3DFVF_TEXTUREFORMAT2/3/4/1
    return sizes[(fvf >> (16 + i * 2)) & 3];
}

static GLsizei FFP_Stride(DWORD fvf) {
    GLsizei s = 0;
    switch (fvf & 0x00E) {
        case 0x002: s += 12; break;                   // XYZ
        case 0x004: s += 16; break;                   // XYZRHW
        case 0x006: case 0x008: case 0x00A: case 0x00C: case 0x00E:
            s += 12 + 4 * (((fvf & 0x00E) - 0x004) / 2); break; // XYZB1..B5
    }
    if (fvf & 0x010) s += 12;   // NORMAL
    if (fvf & 0x020) s += 4;    // PSIZE
    if (fvf & 0x040) s += 4;    // DIFFUSE
    if (fvf & 0x080) s += 4;    // SPECULAR
    int n = (fvf >> 8) & 0xF;
    for (int i = 0; i < n; i++) s += 4 * FFP_TexSize(fvf, i);
    return s;
}

// Point the attributes at the bound GL_ARRAY_BUFFER, vertex 0 at byte `base`
static void FFP_SetupAttribs(DWORD fvf, size_t base, GLsizei stride) {
    if (stride <= 0) stride = FFP_Stride(fvf);
    unsigned enabled = 0;
    size_t off = base;
    GLint pos_size = 3;
    switch (fvf & 0x00E) {
        case 0x002: pos_size = 3; break;
        case 0x004: pos_size = 4; break;
        default: pos_size = 3; break;
    }
    glVertexAttribPointer(FFP_ATTR_POS, pos_size, GL_FLOAT, GL_FALSE, stride, (void*)off);
    enabled |= 1u << FFP_ATTR_POS;
    off += FFP_Stride(fvf & 0x00E);
    if (fvf & 0x010) {
        glVertexAttribPointer(FFP_ATTR_NORMAL, 3, GL_FLOAT, GL_FALSE, stride, (void*)off);
        enabled |= 1u << FFP_ATTR_NORMAL;
        off += 12;
    }
    if (fvf & 0x020) off += 4;
    if (fvf & 0x040) {
        glVertexAttribPointer(FFP_ATTR_DIFFUSE, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride, (void*)off);
        enabled |= 1u << FFP_ATTR_DIFFUSE;
        off += 4;
    }
    if (fvf & 0x080) {
        glVertexAttribPointer(FFP_ATTR_SPECULAR, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride, (void*)off);
        enabled |= 1u << FFP_ATTR_SPECULAR;
        off += 4;
    }
    int n = (fvf >> 8) & 0xF;
    for (int i = 0; i < n && i < 8; i++) {
        int size = FFP_TexSize(fvf, i);
        glVertexAttribPointer(FFP_ATTR_TEX0 + i, size, GL_FLOAT, GL_FALSE, stride, (void*)off);
        enabled |= 1u << (FFP_ATTR_TEX0 + i);
        off += 4 * size;
    }
    for (int a = 0; a < FFP_ATTR_COUNT; a++) {
        unsigned bit = 1u << a;
        if ((enabled & bit) && !(g_ffp_enabled_attribs & bit)) glEnableVertexAttribArray(a);
        if (!(enabled & bit) && (g_ffp_enabled_attribs & bit)) glDisableVertexAttribArray(a);
    }
    g_ffp_enabled_attribs = enabled;
}

// ---------------------------------------------------------------------------
// Shader generation

static std::string FFP_Itos(int i) { char b[16]; snprintf(b, sizeof b, "%d", i); return b; }

static bool FFP_StageUsesArg(const FFPStageKey& s, int arg) {
    bool colour = s.colorop != D3DTOP_DISABLE, alpha = s.alphaop != D3DTOP_DISABLE;
    if (colour && ((s.carg1 & 15) == arg || (s.carg2 & 15) == arg || (s.carg0 & 15) == arg)) return true;
    if (alpha && ((s.aarg1 & 15) == arg || (s.aarg2 & 15) == arg || (s.aarg0 & 15) == arg)) return true;
    return false;
}

static bool FFP_StageSamples(const FFPStageKey& s) {
    return s.has_texture && (FFP_StageUsesArg(s, D3DTA_TEXTURE) ||
        s.colorop == D3DTOP_BLENDTEXTUREALPHA || s.colorop == D3DTOP_BLENDTEXTUREALPHAPM ||
        s.alphaop == D3DTOP_BLENDTEXTUREALPHA || s.alphaop == D3DTOP_BLENDTEXTUREALPHAPM);
}

static std::string FFP_FogFactor(int mode, const char* d) {
    std::string x = d;
    if (mode == D3DFOG_LINEAR) return "clamp((u_fog.y - " + x + ") / max(u_fog.y - u_fog.x, 1e-20), 0.0, 1.0)";
    if (mode == D3DFOG_EXP) return "clamp(exp(-u_fog.z * " + x + "), 0.0, 1.0)";
    return "clamp(exp(-(u_fog.z * " + x + ") * (u_fog.z * " + x + ")), 0.0, 1.0)";
}

static std::string FFP_GenVS(const FFPKey& k) {
    std::string s = "#version 300 es\nprecision highp float;\n";
    s += "layout(location=0) in vec4 a_pos;\nlayout(location=1) in vec3 a_normal;\n"
         "layout(location=2) in vec4 a_diffuse;\nlayout(location=3) in vec4 a_specular;\n";
    for (int i = 0; i < k.tex_count; i++) s += "layout(location=" + FFP_Itos(FFP_ATTR_TEX0 + i) + ") in vec4 a_tc" + FFP_Itos(i) + ";\n";
    s += "uniform mat4 u_wv, u_proj, u_world;\nuniform mat3 u_nrm;\nuniform vec4 u_rhw;\nuniform vec2 u_pixel;\nuniform float u_flip;\n"
         "uniform vec4 u_mdiff, u_mamb, u_mspec, u_memis, u_gamb;\nuniform float u_mpower;\n"
         "uniform vec3 u_lpos[8], u_ldir[8];\nuniform vec4 u_ldiff[8], u_lspec[8], u_lamb[8], u_latt[8], u_lspot[8];\n"
         "uniform mat4 u_texmat[4];\nuniform vec4 u_fog;\n"
         "out vec4 v_diffuse, v_specular;\nout float v_fog, v_clipdist;\nuniform vec4 u_clipplane;\n";
    for (int i = 0; i < k.stage_count; i++) s += "out vec4 v_tc" + FFP_Itos(i) + ";\n";
    s += "void main() {\n";

    bool need_normal = k.has_normal && !k.rhw;
    s += "  vec4 vpos = vec4(0.0);\n  vec3 N = vec3(0.0, 0.0, 1.0);\n";
    if (k.rhw) {
        // screen-space position: pixel centres sit on integer coordinates in D3D9
        s += "  float w = a_pos.w != 0.0 ? 1.0 / a_pos.w : 1.0;\n"
             "  vec2 ndc = (a_pos.xy + 0.5 - u_rhw.xy) * u_rhw.zw + vec2(-1.0, 1.0);\n"
             "  gl_Position = vec4(ndc, a_pos.z * 2.0 - 1.0, 1.0) * w;\n";
    } else {
        s += "  vpos = u_wv * vec4(a_pos.xyz, 1.0);\n  gl_Position = u_proj * vpos;\n"
             "  gl_Position.xy += u_pixel * gl_Position.w;\n"
             "  gl_Position.z = gl_Position.z * 2.0 - gl_Position.w;\n";
        if (need_normal) s += k.normalize ? "  N = normalize(u_nrm * a_normal);\n" : "  N = u_nrm * a_normal;\n";
    }
    s += "  gl_Position.y *= u_flip;\n";
    s += "  vec3 P = vpos.xyz;\n";
    s += k.local_viewer ? "  vec3 V = -normalize(P);\n" : "  vec3 V = vec3(0.0, 0.0, -1.0);\n";

    const char* vdiff = k.has_diffuse ? "a_diffuse.bgra" : "vec4(1.0)";
    const char* vspec = k.has_specular ? "a_specular.bgra" : "vec4(0.0, 0.0, 0.0, 1.0)";
    if (k.lighting && !k.rhw) {
        const char* src[3] = {nullptr, vdiff, vspec};
        const char* mats[4] = {"u_mdiff", "u_mspec", "u_mamb", "u_memis"};
        const char* names[4] = {"md", "ms", "ma", "me"};
        for (int i = 0; i < 4; i++) {
            int m = k.mat_src[i];
            s += std::string("  vec4 ") + names[i] + " = " + (m ? src[m] : mats[i]) + ";\n";
        }
        s += "  vec3 amb = u_gamb.rgb, dif = vec3(0.0), spe = vec3(0.0);\n";
        for (int i = 0; i < FFP_MAX_LIGHTS; i++) {
            int t = k.light_type[i];
            if (!t) continue;
            std::string n = FFP_Itos(i);
            s += "  {\n    vec3 L; float att = 1.0;\n";
            if (t == D3DLIGHT_DIRECTIONAL) {
                s += "    L = -u_ldir[" + n + "];\n";
            } else {
                s += "    vec3 d = u_lpos[" + n + "] - P; float dist = length(d); L = d / max(dist, 1e-20);\n"
                     "    vec4 a = u_latt[" + n + "];\n"
                     "    att = dist > a.w ? 0.0 : 1.0 / max(a.x + a.y * dist + a.z * dist * dist, 1e-20);\n";
                if (t == D3DLIGHT_SPOT) {
                    s += "    vec4 sp = u_lspot[" + n + "]; float rho = dot(-u_ldir[" + n + "], L);\n"
                         "    if (rho <= sp.y) att = 0.0;\n"
                         "    else if (rho < sp.x) att *= pow(clamp((rho - sp.y) / max(sp.x - sp.y, 1e-20), 0.0, 1.0), sp.z);\n";
                }
            }
            s += "    float ndl = max(dot(N, L), 0.0);\n"
                 "    amb += u_lamb[" + n + "].rgb * att;\n"
                 "    dif += u_ldiff[" + n + "].rgb * ndl * att;\n";
            if (k.specular)
                s += "    if (ndl > 0.0) spe += u_lspec[" + n + "].rgb * pow(max(dot(N, normalize(V + L)), 0.0), u_mpower) * att;\n";
            s += "  }\n";
        }
        s += "  v_diffuse = clamp(vec4(me.rgb + ma.rgb * amb + md.rgb * dif, md.a), 0.0, 1.0);\n"
             "  v_specular = clamp(vec4(ms.rgb * spe, ms.a), 0.0, 1.0);\n";
    } else {
        s += std::string("  v_diffuse = ") + vdiff + ";\n  v_specular = " + vspec + ";\n";
    }

    // texture coordinates
    for (int i = 0; i < k.stage_count; i++) {
        const FFPStageKey& st = k.stage[i];
        std::string n = FFP_Itos(i), in;
        switch (st.tc_gen) {
            case 1: in = "vec4(N, 1.0)"; break;
            case 2: in = "vec4(P, 1.0)"; break;
            case 3: in = "vec4(reflect(-V, N), 1.0)"; break;
            case 4: in = "vec4(reflect(-V, N), 1.0)"; break;
            default:
                if (st.tc_index < k.tex_count) {
                    std::string a = "a_tc" + FFP_Itos(st.tc_index);
                    // D3D9 feeds the texture matrix the missing coordinate as 1
                    // right after the given ones: (u, v) becomes (u, v, 1, 0)
                    switch (k.tex_size[st.tc_index]) {
                        case 1: in = "vec4(" + a + ".x, 1.0, 0.0, 0.0)"; break;
                        case 2: in = "vec4(" + a + ".xy, 1.0, 0.0)"; break;
                        case 3: in = "vec4(" + a + ".xyz, 1.0)"; break;
                        default: in = a; break;
                    }
                } else in = "vec4(0.0, 0.0, 1.0, 0.0)";
        }
        if (st.ttff) s += "  v_tc" + n + " = u_texmat[" + n + "] * " + in + ";\n";
        else s += "  v_tc" + n + " = " + in + ";\n";
        if (st.tc_gen == 4) // sphere map from the reflection vector
            s += "  { vec3 r = v_tc" + n + ".xyz; float m = 2.0 * length(vec3(r.xy, r.z - 1.0));"
                 " v_tc" + n + " = vec4(r.x / m + 0.5, -r.y / m + 0.5, 0.0, 1.0); }\n";
    }

    // fog: the factor (1 = no fog), or for table fog the eye depth it is computed from per pixel
    s += "  v_fog = 1.0;\n";
    if (k.fog == 4) s += "  v_fog = v_specular.a;\n";
    else if (k.fog_table) s += "  v_fog = abs(P.z);\n";
    else if (k.fog) {
        s += k.fog_range ? "  float fd = length(P);\n" : "  float fd = abs(P.z);\n";
        s += std::string("  v_fog = ") + FFP_FogFactor(k.fog, "fd") + ";\n";
    }
    s += "  v_clipdist = 1.0;\n";
    if (k.clip && !k.rhw) s += "  v_clipdist = dot(u_world * vec4(a_pos.xyz, 1.0), u_clipplane);\n";
    s += "}\n";
    return s;
}

static std::string FFP_Arg(int arg, int stage, bool alpha) {
    std::string v;
    switch (arg & 15) {
        case D3DTA_DIFFUSE: v = "v_diffuse"; break;
        case D3DTA_CURRENT: v = "cur"; break;
        case D3DTA_TEXTURE: v = "t" + FFP_Itos(stage); break;
        case D3DTA_TFACTOR: v = "u_tfactor"; break;
        case D3DTA_SPECULAR: v = "v_specular"; break;
        case D3DTA_TEMP: v = "tmp"; break;
        default: v = "vec4(1.0)"; break; // D3DTA_CONSTANT: per-stage constants are not supported
    }
    if (arg & FFP_TA_ALPHAREPLICATE) v = v + ".aaaa";
    if (arg & FFP_TA_COMPLEMENT) v = "(1.0 - " + v + ")";
    return v + (alpha ? ".a" : ".rgb");
}

// One D3DTOP on rgb (alpha = false) or a; a0..a2 are expressions of that type
static std::string FFP_Op(int op, const std::string& a0, const std::string& a1, const std::string& a2,
                          int stage, bool alpha, const std::string& a1_alpha) {
    std::string t = "t" + FFP_Itos(stage);
    std::string one = alpha ? "1.0" : "vec3(1.0)";
    switch (op) {
        case D3DTOP_SELECTARG1: return a1;
        case D3DTOP_SELECTARG2: return a2;
        case D3DTOP_MODULATE: return a1 + " * " + a2;
        case D3DTOP_MODULATE2X: return a1 + " * " + a2 + " * 2.0";
        case D3DTOP_MODULATE4X: return a1 + " * " + a2 + " * 4.0";
        case D3DTOP_ADD: return a1 + " + " + a2;
        case D3DTOP_ADDSIGNED: return a1 + " + " + a2 + " - 0.5";
        case D3DTOP_ADDSIGNED2X: return "(" + a1 + " + " + a2 + " - 0.5) * 2.0";
        case D3DTOP_SUBTRACT: return a1 + " - " + a2;
        case D3DTOP_ADDSMOOTH: return a1 + " + " + a2 + " - " + a1 + " * " + a2;
        case D3DTOP_BLENDDIFFUSEALPHA: return "mix(" + a2 + ", " + a1 + ", v_diffuse.a)";
        case D3DTOP_BLENDTEXTUREALPHA: return "mix(" + a2 + ", " + a1 + ", " + t + ".a)";
        case D3DTOP_BLENDFACTORALPHA: return "mix(" + a2 + ", " + a1 + ", u_tfactor.a)";
        case D3DTOP_BLENDTEXTUREALPHAPM: return a1 + " + " + a2 + " * (1.0 - " + t + ".a)";
        case D3DTOP_BLENDCURRENTALPHA: return "mix(" + a2 + ", " + a1 + ", cur.a)";
        case D3DTOP_PREMODULATE: return a1;
        case D3DTOP_MODULATEALPHA_ADDCOLOR: return alpha ? a1 : a1 + " + " + a1_alpha + " * " + a2;
        case D3DTOP_MODULATECOLOR_ADDALPHA: return alpha ? a1 : a1 + " * " + a2 + " + " + a1_alpha;
        case D3DTOP_MODULATEINVALPHA_ADDCOLOR: return alpha ? a1 : "(1.0 - " + a1_alpha + ") * " + a2 + " + " + a1;
        case D3DTOP_MODULATEINVCOLOR_ADDALPHA: return alpha ? a1 : "(" + one + " - " + a1 + ") * " + a2 + " + " + a1_alpha;
        case D3DTOP_MULTIPLYADD: return a0 + " + " + a1 + " * " + a2;
        case D3DTOP_LERP: return "mix(" + a2 + ", " + a1 + ", " + a0 + ")";
        default: return alpha ? "cur.a" : "cur.rgb"; // bump mapping: pass through
    }
}

static std::string FFP_GenFS(const FFPKey& k) {
    std::string s = "#version 300 es\nprecision highp float;\n";
    s += "in vec4 v_diffuse, v_specular;\nin float v_fog, v_clipdist;\n"
         "uniform vec4 u_tfactor;\nuniform float u_alpharef;\nuniform vec3 u_fogcolor;\nuniform vec4 u_fog;\nuniform float u_lodbias[4];\n"
         "out vec4 o_color;\n";
    for (int i = 0; i < k.stage_count; i++) {
        s += "in vec4 v_tc" + FFP_Itos(i) + ";\n";
        if (FFP_StageSamples(k.stage[i])) s += "uniform sampler2D u_tex" + FFP_Itos(i) + ";\n";
    }
    s += "void main() {\n";
    if (k.clip && !k.rhw) s += "  if (v_clipdist < 0.0) discard;\n";
    s += "  vec4 cur = v_diffuse, tmp = vec4(0.0);\n";
    for (int i = 0; i < k.stage_count; i++) {
        const FFPStageKey& st = k.stage[i];
        std::string n = FFP_Itos(i), tc = "v_tc" + n;
        if (FFP_StageSamples(st)) {
            std::string uv = tc + ".xy";
            if (st.projected) {
                int c = st.ttff ? st.ttff - 1 : 3;
                uv = tc + ".xy / " + tc + "." + "xyzw"[c];
            }
            s += "  vec4 t" + n + " = texture(u_tex" + n + ", " + uv + ", u_lodbias[" + n + "]);\n";
        } else {
            s += "  vec4 t" + n + " = vec4(1.0);\n";
        }
        std::string col = "cur.rgb", alp = "cur.a";
        // like D3D9 (and wined3d): an operation that takes the texture of a
        // stage without one passes the current value on unchanged
        auto uses = [](int a0, int a1, int a2, int op) {
            return (a0 & 15) == D3DTA_TEXTURE || (a1 & 15) == D3DTA_TEXTURE || (a2 & 15) == D3DTA_TEXTURE ||
                   op == D3DTOP_BLENDTEXTUREALPHA || op == D3DTOP_BLENDTEXTUREALPHAPM;
        };
        bool col_off = !st.has_texture && uses(st.carg0, st.carg1, st.carg2, st.colorop);
        bool alp_off = !st.has_texture && uses(st.aarg0, st.aarg1, st.aarg2, st.alphaop);
        if (st.colorop != D3DTOP_DISABLE && !col_off) {
            if (st.colorop == D3DTOP_DOTPRODUCT3)
                col = "vec3(clamp(dot((" + FFP_Arg(st.carg1, i, false) + " - 0.5) * 2.0, (" + FFP_Arg(st.carg2, i, false) + " - 0.5) * 2.0), 0.0, 1.0))";
            else
                col = FFP_Op(st.colorop, FFP_Arg(st.carg0, i, false), FFP_Arg(st.carg1, i, false), FFP_Arg(st.carg2, i, false),
                             i, false, FFP_Arg(st.carg1, i, true));
        }
        if (st.colorop == D3DTOP_DOTPRODUCT3 && !col_off) alp = col + ".r"; // replicated to alpha
        else if (st.alphaop != D3DTOP_DISABLE && !alp_off)
            alp = FFP_Op(st.alphaop, FFP_Arg(st.aarg0, i, true), FFP_Arg(st.aarg1, i, true), FFP_Arg(st.aarg2, i, true),
                         i, true, FFP_Arg(st.aarg1, i, true));
        std::string dst = st.result_temp ? "tmp" : "cur";
        s += "  " + dst + " = clamp(vec4(" + col + ", " + alp + "), 0.0, 1.0);\n";
    }
    if (k.specular) s += "  cur.rgb = clamp(cur.rgb + v_specular.rgb, 0.0, 1.0);\n";
    if (k.fog_table) s += "  cur.rgb = mix(u_fogcolor, cur.rgb, " + FFP_FogFactor(k.fog, "v_fog") + ");\n";
    else if (k.fog) s += "  cur.rgb = mix(u_fogcolor, cur.rgb, v_fog);\n";
    if (k.alpha_func && k.alpha_func != D3DCMP_ALWAYS) {
        // compare in the 0..255 steps of D3DRS_ALPHAREF
        const char* cmp = "";
        switch (k.alpha_func) {
            case D3DCMP_NEVER: cmp = "false"; break;
            case D3DCMP_LESS: cmp = "a < u_alpharef"; break;
            case D3DCMP_EQUAL: cmp = "a == u_alpharef"; break;
            case D3DCMP_LESSEQUAL: cmp = "a <= u_alpharef"; break;
            case D3DCMP_GREATER: cmp = "a > u_alpharef"; break;
            case D3DCMP_NOTEQUAL: cmp = "a != u_alpharef"; break;
            case D3DCMP_GREATEREQUAL: cmp = "a >= u_alpharef"; break;
            default: cmp = "true"; break;
        }
        s += std::string("  { float a = floor(cur.a * 255.0 + 0.5); if (!(") + cmp + ")) discard; }\n";
    }
    s += "  o_color = cur;\n}\n";
    return s;
}

static GLuint FFP_Compile(GLenum type, const std::string& src) {
    GLuint sh = glCreateShader(type);
    const char* p = src.c_str();
    glShaderSource(sh, 1, &p, nullptr);
    glCompileShader(sh);
    GLint ok = 0;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048];
        glGetShaderInfoLog(sh, sizeof log, nullptr, log);
        printf("[D3D9 FFP] shader compile failed: %s\n%s\n", log, p);
    }
    return sh;
}

static FFPProgram* FFP_GetProgram(const FFPKey& k) {
    std::string key((const char*)&k, sizeof k);
    auto it = g_ffp_programs.find(key);
    if (it != g_ffp_programs.end()) return it->second;

    FFPProgram* p = new FFPProgram();
    GLuint vs = FFP_Compile(GL_VERTEX_SHADER, FFP_GenVS(k));
    GLuint fs = FFP_Compile(GL_FRAGMENT_SHADER, FFP_GenFS(k));
    p->program = glCreateProgram();
    glAttachShader(p->program, vs);
    glAttachShader(p->program, fs);
    glLinkProgram(p->program);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint ok = 0;
    glGetProgramiv(p->program, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[2048];
        glGetProgramInfoLog(p->program, sizeof log, nullptr, log);
        printf("[D3D9 FFP] program link failed: %s\n", log);
    }
    GLuint pr = p->program;
    p->wv = glGetUniformLocation(pr, "u_wv");
    p->proj = glGetUniformLocation(pr, "u_proj");
    p->nrm = glGetUniformLocation(pr, "u_nrm");
    p->world = glGetUniformLocation(pr, "u_world");
    p->rhw = glGetUniformLocation(pr, "u_rhw");
    p->pixel = glGetUniformLocation(pr, "u_pixel");
    p->flip = glGetUniformLocation(pr, "u_flip");
    p->mdiff = glGetUniformLocation(pr, "u_mdiff");
    p->mamb = glGetUniformLocation(pr, "u_mamb");
    p->mspec = glGetUniformLocation(pr, "u_mspec");
    p->memis = glGetUniformLocation(pr, "u_memis");
    p->mpower = glGetUniformLocation(pr, "u_mpower");
    p->gamb = glGetUniformLocation(pr, "u_gamb");
    p->lpos = glGetUniformLocation(pr, "u_lpos");
    p->ldir = glGetUniformLocation(pr, "u_ldir");
    p->ldiff = glGetUniformLocation(pr, "u_ldiff");
    p->lspec = glGetUniformLocation(pr, "u_lspec");
    p->lamb = glGetUniformLocation(pr, "u_lamb");
    p->latt = glGetUniformLocation(pr, "u_latt");
    p->lspot = glGetUniformLocation(pr, "u_lspot");
    for (int i = 0; i < FFP_MAX_STAGES; i++)
        p->texmat[i] = glGetUniformLocation(pr, ("u_texmat[" + FFP_Itos(i) + "]").c_str());
    p->tfactor = glGetUniformLocation(pr, "u_tfactor");
    p->alpharef = glGetUniformLocation(pr, "u_alpharef");
    p->fog = glGetUniformLocation(pr, "u_fog");
    p->fogcolor = glGetUniformLocation(pr, "u_fogcolor");
    p->clipplane = glGetUniformLocation(pr, "u_clipplane");
    p->lodbias = glGetUniformLocation(pr, "u_lodbias");
    glUseProgram(pr);
    g_ffp_current_program = pr;
    for (int i = 0; i < FFP_MAX_STAGES; i++) {
        GLint loc = glGetUniformLocation(pr, ("u_tex" + FFP_Itos(i)).c_str());
        if (loc != -1) glUniform1i(loc, i);
    }
    g_ffp_programs[key] = p;
    return p;
}

// ---------------------------------------------------------------------------
// Per-draw state

static void FFP_Mul(D3DMATRIX* out, const D3DMATRIX* a, const D3DMATRIX* b) {
    D3DMATRIX r;
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++)
            r.m[i][j] = a->m[i][0] * b->m[0][j] + a->m[i][1] * b->m[1][j] + a->m[i][2] * b->m[2][j] + a->m[i][3] * b->m[3][j];
    *out = r;
}

static void FFP_ColorVec(DWORD c, float* v) {
    v[0] = ((c >> 16) & 255) / 255.0f; v[1] = ((c >> 8) & 255) / 255.0f;
    v[2] = (c & 255) / 255.0f; v[3] = (c >> 24) / 255.0f;
}

static float FFP_DwordFloat(DWORD d) { float f; memcpy(&f, &d, 4); return f; }

static void FFP_ApplySampler(int stage, GLuint tex) {
    const DWORD* ss = g_sampler_states[stage];
    auto wrap = [](DWORD a) -> GLint {
        switch (a) {
            case D3DTADDRESS_CLAMP: case D3DTADDRESS_BORDER: return GL_CLAMP_TO_EDGE;
            case D3DTADDRESS_MIRROR: return GL_MIRRORED_REPEAT;
            default: return GL_REPEAT;
        }
    };
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap(ss[D3DSAMP_ADDRESSU]));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap(ss[D3DSAMP_ADDRESSV]));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, ss[D3DSAMP_MAGFILTER] == D3DTEXF_POINT ? GL_NEAREST : GL_LINEAR);
    bool mips = g_mipmapped_textures.count(tex) && ss[D3DSAMP_MIPFILTER] != D3DTEXF_NONE;
    bool min_point = ss[D3DSAMP_MINFILTER] == D3DTEXF_POINT;
    GLint min;
    if (!mips) min = min_point ? GL_NEAREST : GL_LINEAR;
    else if (ss[D3DSAMP_MIPFILTER] == D3DTEXF_POINT) min = min_point ? GL_NEAREST_MIPMAP_NEAREST : GL_LINEAR_MIPMAP_NEAREST;
    else min = min_point ? GL_NEAREST_MIPMAP_LINEAR : GL_LINEAR_MIPMAP_LINEAR;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, min);
    if (g_max_anisotropy > 1.0f)
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, mips ? g_max_anisotropy : 1.0f);
}

// What the device passes in for a draw
struct FFPDrawContext {
    DWORD fvf;
    const DWORD* rs;                       // render states
    const D3DMATRIX *world, *view, *proj;
    bool fbo;                              // rendering into a texture (stored upside down)
    GLuint render_target;                  // its texture, which no stage may sample
    bool clip_enabled;
    const float* clip_plane;
};

static void FFP_BuildKey(const FFPDrawContext& c, FFPKey& k, GLuint* stage_tex) {
    memset(&k, 0, sizeof k);
    const DWORD* rs = c.rs;
    DWORD fvf = c.fvf;
    k.rhw = (fvf & 0x00E) == 0x004;
    k.has_normal = (fvf & 0x010) != 0;
    k.has_diffuse = (fvf & 0x040) != 0;
    k.has_specular = (fvf & 0x080) != 0;
    k.tex_count = (fvf >> 8) & 0xF;
    if (k.tex_count > 8) k.tex_count = 8;
    for (int i = 0; i < k.tex_count; i++) k.tex_size[i] = FFP_TexSize(fvf, i);

    k.lighting = !k.rhw && rs[D3DRS_LIGHTING] != 0;
    if (k.lighting) {
        k.normalize = rs[FFP_RS_NORMALIZENORMALS] != 0;
        k.local_viewer = rs[FFP_RS_LOCALVIEWER] != 0;
        bool cv = rs[FFP_RS_COLORVERTEX] != 0;
        DWORD srcs[4] = {rs[FFP_RS_DIFFUSEMATERIALSOURCE], rs[FFP_RS_SPECULARMATERIALSOURCE],
                         rs[FFP_RS_AMBIENTMATERIALSOURCE], rs[FFP_RS_EMISSIVEMATERIALSOURCE]};
        for (int i = 0; i < 4; i++) {
            DWORD m = cv ? srcs[i] : 0;
            // a colour the vertex does not have falls back to the material
            if ((m == 1 && !k.has_diffuse) || (m == 2 && !k.has_specular) || m > 2) m = 0;
            k.mat_src[i] = (uint8_t)m;
        }
        for (int i = 0; i < FFP_MAX_LIGHTS; i++)
            if (g_light_enabled[i]) k.light_type[i] = (uint8_t)g_lights[i].Type;
    } else {
        k.local_viewer = 1;
    }
    k.specular = rs[D3DRS_SPECULARENABLE] != 0;

    if (rs[D3DRS_FOGENABLE]) {
        DWORD table = rs[D3DRS_FOGTABLEMODE], vert = rs[D3DRS_FOGVERTEXMODE];
        if (table != D3DFOG_NONE && !k.rhw) { k.fog = (uint8_t)table; k.fog_table = 1; }
        else if (vert != D3DFOG_NONE && !k.rhw) { k.fog = (uint8_t)vert; k.fog_range = rs[D3DRS_RANGEFOGENABLE] != 0; }
        else k.fog = 4; // fog factor from the specular alpha
    }
    if (rs[D3DRS_ALPHATESTENABLE]) k.alpha_func = (uint8_t)(rs[D3DRS_ALPHAFUNC] ? rs[D3DRS_ALPHAFUNC] : D3DCMP_ALWAYS);
    k.clip = c.clip_enabled;

    int n = 0;
    for (; n < FFP_MAX_STAGES; n++) {
        const DWORD* ts = g_texture_stage_states[n];
        if (ts[D3DTSS_COLOROP] == D3DTOP_DISABLE || ts[D3DTSS_COLOROP] == 0) break;
        FFPStageKey& st = k.stage[n];
        st.colorop = (uint8_t)ts[D3DTSS_COLOROP];
        st.carg0 = (uint8_t)ts[D3DTSS_COLORARG0]; st.carg1 = (uint8_t)ts[D3DTSS_COLORARG1]; st.carg2 = (uint8_t)ts[D3DTSS_COLORARG2];
        st.alphaop = (uint8_t)(ts[D3DTSS_ALPHAOP] ? ts[D3DTSS_ALPHAOP] : D3DTOP_DISABLE);
        st.aarg0 = (uint8_t)ts[D3DTSS_ALPHAARG0]; st.aarg1 = (uint8_t)ts[D3DTSS_ALPHAARG1]; st.aarg2 = (uint8_t)ts[D3DTSS_ALPHAARG2];
        DWORD tci = ts[D3DTSS_TEXCOORDINDEX];
        st.tc_index = (uint8_t)(tci & 0xFFFF);
        switch (tci & 0xFFFF0000) {
            case D3DTSS_TCI_CAMERASPACENORMAL: st.tc_gen = 1; break;
            case D3DTSS_TCI_CAMERASPACEPOSITION: st.tc_gen = 2; break;
            case FFP_TCI_REFLECTION: st.tc_gen = 3; break;
            case FFP_TCI_SPHEREMAP: st.tc_gen = 4; break;
        }
        if (k.rhw && st.tc_gen) st.tc_gen = 0;
        DWORD ttff = ts[D3DTSS_TEXTURETRANSFORMFLAGS];
        st.ttff = k.rhw ? 0 : (uint8_t)(ttff & 0xFF);
        st.projected = (ttff & D3DTTFF_PROJECTED) != 0;
        st.result_temp = ts[D3DTSS_RESULTARG] == D3DTA_TEMP;
        GLuint t = g_texture_stages[n];
        if (t && t == c.render_target) t = 0; // feedback loop: WebGL refuses the draw
        stage_tex[n] = t;
        st.has_texture = t != 0;
    }
    k.stage_count = (uint8_t)n;
}

// Select and feed the program for the current state; bind the stage textures
static void FFP_Prepare(const FFPDrawContext& c, float vp_x, float vp_y, float vp_w, float vp_h) {
    FFPKey k;
    GLuint stage_tex[FFP_MAX_STAGES] = {0};
    FFP_BuildKey(c, k, stage_tex);
    FFPProgram* p = FFP_GetProgram(k);
    if (g_ffp_current_program != p->program) {
        glUseProgram(p->program);
        g_ffp_current_program = p->program;
    }
    const DWORD* rs = c.rs;

    // transform
    D3DMATRIX wv;
    FFP_Mul(&wv, c.world, c.view);
    glUniformMatrix4fv(p->wv, 1, GL_FALSE, &wv.m[0][0]);
    glUniformMatrix4fv(p->proj, 1, GL_FALSE, &c.proj->m[0][0]);
    if (p->world != -1) glUniformMatrix4fv(p->world, 1, GL_FALSE, &c.world->m[0][0]);
    if (p->nrm != -1) {
        // normals go through the inverse transpose of the world-view 3x3
        const float (*m)[4] = wv.m;
        float a = m[0][0], b = m[0][1], cc = m[0][2], d = m[1][0], e = m[1][1], f = m[1][2], g = m[2][0], h = m[2][1], i = m[2][2];
        float co[9] = { e * i - f * h, -(d * i - f * g), d * h - e * g,
                        -(b * i - cc * h), a * i - cc * g, -(a * h - b * g),
                        b * f - cc * e, -(a * f - cc * d), a * e - b * d };
        float det = a * co[0] + b * co[1] + cc * co[2];
        float s = det != 0.0f ? 1.0f / det : 0.0f;
        // inverse transpose = cofactor matrix / det, row-major like the D3D matrices
        float n[9];
        for (int j = 0; j < 9; j++) n[j] = co[j] * s;
        glUniformMatrix3fv(p->nrm, 1, GL_FALSE, n);
    }
    if (p->rhw != -1) glUniform4f(p->rhw, vp_x, vp_y, 2.0f / vp_w, -2.0f / vp_h);
    // D3D9 samples pixels at integer coordinates, GL half a pixel further on
    if (p->pixel != -1) glUniform2f(p->pixel, 1.0f / vp_w, -1.0f / vp_h);
    glUniform1f(p->flip, c.fbo ? -1.0f : 1.0f);

    // lighting
    if (k.lighting) {
        const D3DMATERIAL9& mt = g_material;
        glUniform4f(p->mdiff, mt.Diffuse.r, mt.Diffuse.g, mt.Diffuse.b, mt.Diffuse.a);
        glUniform4f(p->mamb, mt.Ambient.r, mt.Ambient.g, mt.Ambient.b, mt.Ambient.a);
        glUniform4f(p->mspec, mt.Specular.r, mt.Specular.g, mt.Specular.b, mt.Specular.a);
        glUniform4f(p->memis, mt.Emissive.r, mt.Emissive.g, mt.Emissive.b, mt.Emissive.a);
        glUniform1f(p->mpower, mt.Power);
        float amb[4];
        FFP_ColorVec(rs[D3DRS_AMBIENT], amb);
        glUniform4fv(p->gamb, 1, amb);
        float pos[8][3], dir[8][3], diff[8][4], spec[8][4], lamb[8][4], att[8][4], spot[8][4];
        const D3DMATRIX* v = c.view;
        for (int l = 0; l < FFP_MAX_LIGHTS; l++) {
            const D3DLIGHT9& L = g_lights[l];
            const float* P = &L.Position.x;
            const float* D = &L.Direction.x;
            for (int j = 0; j < 3; j++) {
                pos[l][j] = P[0] * v->m[0][j] + P[1] * v->m[1][j] + P[2] * v->m[2][j] + v->m[3][j];
                dir[l][j] = D[0] * v->m[0][j] + D[1] * v->m[1][j] + D[2] * v->m[2][j];
            }
            float len = sqrtf(dir[l][0] * dir[l][0] + dir[l][1] * dir[l][1] + dir[l][2] * dir[l][2]);
            if (len > 0) for (int j = 0; j < 3; j++) dir[l][j] /= len;
            memcpy(diff[l], &L.Diffuse, 16);
            memcpy(spec[l], &L.Specular, 16);
            memcpy(lamb[l], &L.Ambient, 16);
            att[l][0] = L.Attenuation0; att[l][1] = L.Attenuation1; att[l][2] = L.Attenuation2;
            att[l][3] = L.Range > 0 ? L.Range : 3.4e38f;
            spot[l][0] = cosf(L.Theta * 0.5f); spot[l][1] = cosf(L.Phi * 0.5f);
            spot[l][2] = L.Falloff; spot[l][3] = 0;
        }
        glUniform3fv(p->lpos, 8, &pos[0][0]);
        glUniform3fv(p->ldir, 8, &dir[0][0]);
        glUniform4fv(p->ldiff, 8, &diff[0][0]);
        glUniform4fv(p->lspec, 8, &spec[0][0]);
        glUniform4fv(p->lamb, 8, &lamb[0][0]);
        glUniform4fv(p->latt, 8, &att[0][0]);
        glUniform4fv(p->lspot, 8, &spot[0][0]);
    }

    // texture stages
    if (p->lodbias != -1) {
        // D3DSAMP_MIPMAPLODBIAS; WebGL selects coarser mip levels than D3D9
        // drivers for the same texture, which the extra -0.75 makes up for
        float bias[FFP_MAX_STAGES];
        for (int i = 0; i < FFP_MAX_STAGES; i++) bias[i] = FFP_DwordFloat(g_sampler_states[i][D3DSAMP_MIPMAPLODBIAS]) - 0.75f;
        glUniform1fv(p->lodbias, FFP_MAX_STAGES, bias);
    }
    for (int i = 0; i < k.stage_count; i++) {
        if (k.stage[i].ttff && p->texmat[i] != -1)
            glUniformMatrix4fv(p->texmat[i], 1, GL_FALSE, &g_texture_matrices[i].m[0][0]);
        if (FFP_StageSamples(k.stage[i])) {
            glActiveTexture(GL_TEXTURE0 + i);
            glBindTexture(GL_TEXTURE_2D, stage_tex[i]);
            FFP_ApplySampler(i, stage_tex[i]);
        }
    }
    glActiveTexture(GL_TEXTURE0);
    // the texture caches of the old draw paths no longer describe the units
    g_last_bound_texture_id = 0xFFFFFFFF;
    g_last_bound_lightmap_id = 0xFFFFFFFF;

    float tf[4];
    FFP_ColorVec(rs[D3DRS_TEXTUREFACTOR], tf);
    if (p->tfactor != -1) glUniform4fv(p->tfactor, 1, tf);
    if (p->alpharef != -1) glUniform1f(p->alpharef, (float)(rs[D3DRS_ALPHAREF] & 255));
    if (k.fog) {
        float fc[4];
        FFP_ColorVec(rs[D3DRS_FOGCOLOR], fc);
        if (p->fogcolor != -1) glUniform3fv(p->fogcolor, 1, fc);
        if (p->fog != -1) glUniform4f(p->fog, FFP_DwordFloat(rs[D3DRS_FOGSTART]), FFP_DwordFloat(rs[D3DRS_FOGEND]),
                                     FFP_DwordFloat(rs[D3DRS_FOGDENSITY]), 0.0f);
    }
    if (k.clip && p->clipplane != -1) glUniform4fv(p->clipplane, 1, c.clip_plane);
}

static GLenum FFP_Primitive(D3DPRIMITIVETYPE type, UINT count, GLsizei* vertices) {
    switch (type) {
        case D3DPT_POINTLIST: *vertices = count; return GL_POINTS;
        case D3DPT_LINELIST: *vertices = count * 2; return GL_LINES;
        case D3DPT_LINESTRIP: *vertices = count + 1; return GL_LINE_STRIP;
        case D3DPT_TRIANGLESTRIP: *vertices = count + 2; return GL_TRIANGLE_STRIP;
        case D3DPT_TRIANGLEFAN: *vertices = count + 2; return GL_TRIANGLE_FAN;
        default: *vertices = count * 3; return GL_TRIANGLES;
    }
}
