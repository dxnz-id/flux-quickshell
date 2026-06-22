#include "FluidSimEngine.h"
#include "FluidSimShaders.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <random>
#include <vector>
#include <QDebug>

#pragma pack(push, 1)

static uint16_t f32to16(float val) {
    uint32_t u;
    memcpy(&u, &val, 4);
    uint16_t s = (u >> 16) & 0x8000;
    uint32_t abs_u = u & 0x7fffffffu;
    if (abs_u == 0) return s;
    if (abs_u > 0x7f800000u) return s | 0x7e00;
    if (abs_u == 0x7f800000u) return s | 0x7c00;
    int e = int((u >> 23) & 0xff) - 127 + 15;
    uint32_t m = (u >> 13) & 0x3ff;
    if (e <= 0) {
        if (e < -10) return s;
        m = ((u & 0x7fffffu) | 0x800000u) >> (13 - e + 1);
        e = 0;
    } else if (e >= 31) {
        e = 31; m = 0;
    }
    return uint16_t(s | uint16_t(e << 10) | uint16_t(m & 0x3ff));
}

// 3D simplex noise — 1:1 port of WGSL generate_noise.comp.wgsl
struct S3 { float x, y, z; };
struct S4 { float x, y, z, w; };
static S3 s3f(float x, float y, float z) { return {x, y, z}; }
static S4 s4f(float x, float y, float z, float w) { return {x, y, z, w}; }
static S4 add4(S4 a, S4 b) { return {a.x+b.x, a.y+b.y, a.z+b.z, a.w+b.w}; }
static S4 add4s(S4 a, float s) { return {a.x+s, a.y+s, a.z+s, a.w+s}; }
static S4 sub4(S4 a, S4 b) { return {a.x-b.x, a.y-b.y, a.z-b.z, a.w-b.w}; }
static S4 mul4(S4 a, S4 b) { return {a.x*b.x, a.y*b.y, a.z*b.z, a.w*b.w}; }
static S4 mul4s(S4 a, float s) { return {a.x*s, a.y*s, a.z*s, a.w*s}; }
static S4 abs4(S4 v) { return {fabsf(v.x), fabsf(v.y), fabsf(v.z), fabsf(v.w)}; }
static float dot4(S4 a, S4 b) { return a.x*b.x + a.y*b.y + a.z*b.z + a.w*b.w; }
static S3 min3(S3 a, S3 b) { return {fminf(a.x,b.x), fminf(a.y,b.y), fminf(a.z,b.z)}; }
static S3 max3(S3 a, S3 b) { return {fmaxf(a.x,b.x), fmaxf(a.y,b.y), fmaxf(a.z,b.z)}; }

static S4 mod289v4(S4 v) {
    return sub4(v, mul4s({floorf(v.x*(1.f/289.f)), floorf(v.y*(1.f/289.f)),
                          floorf(v.z*(1.f/289.f)), floorf(v.w*(1.f/289.f))}, 289.f));
}
static S4 permutev4(S4 x) { return mod289v4(mul4(add4(mul4s(x, 34.f), {1,1,1,1}), x)); }

static float snoise3D(float vx, float vy, float vz) {
    const float C1 = 1.f/6.f, C2 = 1.f/3.f;
    float d = vx + vy + vz;
    float i0 = floorf(vx + d*C2), i1 = floorf(vy + d*C2), i2 = floorf(vz + d*C2);
    d = i0 + i1 + i2;
    float x0x = vx - i0 + d*C1, x0y = vy - i1 + d*C1, x0z = vz - i2 + d*C1;

    S3 g = {x0y < x0x ? 0.f : 1.f, x0z < x0y ? 0.f : 1.f, x0x < x0z ? 0.f : 1.f};
    S3 l = {1-g.x, 1-g.y, 1-g.z};
    S3 i1v = min3(g, {l.z, l.x, l.y});
    S3 i2v = max3(g, {l.z, l.x, l.y});

    float x1x = x0x - i1v.x + C1, x1y = x0y - i1v.y + C1, x1z = x0z - i1v.z + C1;
    float x2x = x0x - i2v.x + C2, x2y = x0y - i2v.y + C2, x2z = x0z - i2v.z + C2;
    float x3x = x0x - .5f, x3y = x0y - .5f, x3z = x0z - .5f;

    S4 ii = mod289v4({i0, i1, i2, 0});

    S4 p = add4({0, i1v.z, i2v.z, 1}, s4f(ii.z, ii.z, ii.z, ii.z));
    p = permutev4(p);
    p = add4(p, add4(s4f(ii.y, ii.y, ii.y, ii.y), {0, i1v.y, i2v.y, 1}));
    p = permutev4(p);
    p = add4(p, add4(s4f(ii.x, ii.x, ii.x, ii.x), {0, i1v.x, i2v.x, 1}));
    p = permutev4(p);

    S4 j = sub4(p, mul4s({floorf(p.x*(1.f/49.f)), floorf(p.y*(1.f/49.f)),
                          floorf(p.z*(1.f/49.f)), floorf(p.w*(1.f/49.f))}, 49.f));
    S4 x_ = {floorf(j.x*(1.f/7.f)), floorf(j.y*(1.f/7.f)), floorf(j.z*(1.f/7.f)), floorf(j.w*(1.f/7.f))};
    S4 y_ = sub4(j, mul4s(x_, 7.f));

    S4 xx = add4s(mul4s(x_, 2.f/7.f), 0.5f/7.f - 1.f);
    S4 yy = add4s(mul4s(y_, 2.f/7.f), 0.5f/7.f - 1.f);
    S4 hh = sub4({1,1,1,1}, add4(abs4(xx), abs4(yy)));

    S4 b0 = {xx.x, xx.y, yy.x, yy.y};
    S4 b1 = {xx.z, xx.w, yy.z, yy.w};
    S4 s0 = add4s(mul4s({floorf(b0.x), floorf(b0.y), floorf(b0.z), floorf(b0.w)}, 2.f), 1.f);
    S4 s1 = add4s(mul4s({floorf(b1.x), floorf(b1.y), floorf(b1.z), floorf(b1.w)}, 2.f), 1.f);
    S4 sh = {hh.x <= 0.f ? -1.f : 0.f, hh.y <= 0.f ? -1.f : 0.f,
             hh.z <= 0.f ? -1.f : 0.f, hh.w <= 0.f ? -1.f : 0.f};

    S4 a0 = {b0.x + s0.x*sh.x, b0.z + s0.z*sh.x, b0.y + s0.y*sh.y, b0.w + s0.w*sh.y};
    S4 a1 = {b1.x + s1.x*sh.z, b1.z + s1.z*sh.z, b1.y + s1.y*sh.w, b1.w + s1.w*sh.w};

    float gx[] = {a0.x, a0.z, a1.x, a1.z};
    float gy[] = {a0.y, a0.w, a1.y, a1.w};
    float gz[] = {hh.x, hh.y, hh.z, hh.w};
    float nrm[4];
    for (int i = 0; i < 4; i++) nrm[i] = 1.f / sqrtf(gx[i]*gx[i] + gy[i]*gy[i] + gz[i]*gz[i]);
    for (int i = 0; i < 4; i++) { gx[i] *= nrm[i]; gy[i] *= nrm[i]; gz[i] *= nrm[i]; }

    float xd[] = {x0x, x1x, x2x, x3x};
    float yd[] = {x0y, x1y, x2y, x3y};
    float zd[] = {x0z, x1z, x2z, x3z};
    S4 mm = {
        fmaxf(.6f - (x0x*x0x + x0y*x0y + x0z*x0z), 0.f),
        fmaxf(.6f - (x1x*x1x + x1y*x1y + x1z*x1z), 0.f),
        fmaxf(.6f - (x2x*x2x + x2y*x2y + x2z*x2z), 0.f),
        fmaxf(.6f - (x3x*x3x + x3y*x3y + x3z*x3z), 0.f),
    };
    mm.x = mm.x*mm.x*mm.x*mm.x;
    mm.y = mm.y*mm.y*mm.y*mm.y;
    mm.z = mm.z*mm.z*mm.z*mm.z;
    mm.w = mm.w*mm.w*mm.w*mm.w;

    float px = xd[0]*gx[0] + yd[0]*gy[0] + zd[0]*gz[0];
    float py = xd[1]*gx[1] + yd[1]*gy[1] + zd[1]*gz[1];
    float pz = xd[2]*gx[2] + yd[2]*gy[2] + zd[2]*gz[2];
    float pw = xd[3]*gx[3] + yd[3]*gy[3] + zd[3]*gz[3];
    return 42.f * (mm.x*px + mm.y*py + mm.z*pz + mm.w*pw);
}

static void simplexNoisePair(float vx, float vy, float vz, float &ox, float &oy) {
    ox = snoise3D(vx, vy, vz);
    oy = snoise3D(vx + 8.f, vy - 8.f, vz);
}

#define TAU 6.283185307179586f

struct FluidUniforms {
    float timestep;
    float dissipation;
    float alpha;
    float r_beta;
    float center_factor;
    float stencil_factor;
};
static_assert(sizeof(FluidUniforms) == 24, "FluidUniforms must be 24 bytes");

struct Direction {
    float dir;
    float _pad[3];
};
static_assert(sizeof(Direction) == 16, "Direction must be 16 bytes for std140");

struct NoiseUniforms {
    float multiplier;
    float noiseSize;
    float _pad[2];
};
static_assert(sizeof(NoiseUniforms) == 16, "NoiseUniforms must be 16 bytes for std140");

struct PushConstants {
    float timestep;
    float _pad[3];
};
static_assert(sizeof(PushConstants) == 16, "PushConstants must be 16 bytes for std140");

struct NoiseChannelBlock {
    float ch0_scale_x;    float ch0_scale_y;    float ch0_off1;    float ch0_off2;
    float ch0_blend;      float ch0_mult;       float _pad0[2];
    float ch1_scale_x;    float ch1_scale_y;    float ch1_off1;    float ch1_off2;
    float ch1_blend;      float ch1_mult;       float _pad1[2];
    float ch2_scale_x;    float ch2_scale_y;    float ch2_off1;    float ch2_off2;
    float ch2_blend;      float ch2_mult;       float _pad2[2];
};
static_assert(sizeof(NoiseChannelBlock) == 96, "NoiseChannelBlock must be 96 bytes");
#pragma pack(pop)

struct ChannelCfg {
    float scale;
    float mult;
    float inc;
};

static constexpr ChannelCfg CHANNEL_CFG[3] = {
    { 2.8f,  1.0f, 0.001f },
    { 15.0f, 0.7f, 0.006f },
    { 30.0f, 0.5f, 0.012f },
};

static constexpr float MAX_ELAPSED = 100000.0f;

FluidSimEngine::FluidSimEngine(QObject *parent)
    : QObject(parent)
{
    initNoiseChannels();
}

FluidSimEngine::~FluidSimEngine()
{
    releaseResources();
}

void FluidSimEngine::initNoiseChannels()
{
    for (int i = 0; i < NUM_CHANNELS; i++) {
        float initOffset = (float(std::rand()) / float(RAND_MAX)) * 1000.0f;
        m_channels[i] = {
            .scale = {CHANNEL_CFG[i].scale, CHANNEL_CFG[i].scale},
            .offset_1 = initOffset,
            .offset_2 = 0.0f,
            .blend_factor = 0.0f,
            .multiplier = CHANNEL_CFG[i].mult,
            ._padding = {0, 0}
        };
    }
}

void FluidSimEngine::releaseResources()
{
    m_velocityTex[0].reset(); m_velocityTex[1].reset();
    m_pressureTex[0].reset(); m_pressureTex[1].reset();
    m_noiseTex.reset();
    m_advectionFwdTex.reset();
    m_advectionRevTex.reset();
    m_divergenceTex.reset();

    m_linearSampler.reset();
    m_nearestSampler.reset();

    m_fluidUniformBuf.reset();
    m_directionBuf.reset();
    m_noiseUniformBuf.reset();
    m_noiseChannelBuf.reset();
    m_pushConstantBuf.reset();

    m_quadVertexBuf.reset();

    m_passNoise.srb.reset();
    for (auto &p : m_passAdvection) p.srb.reset();
    m_passAdvectionRev.srb.reset();
    for (auto &p : m_passAdjust) p.srb.reset();
    for (auto &p : m_passDiffuse) p.srb.reset();
    for (auto &p : m_passInjectNoise) p.srb.reset();
    for (auto &p : m_passDivergence) p.srb.reset();
    for (auto &p : m_passPressure) p.srb.reset();
    for (auto &a : m_passSubtract) for (auto &p : a) p.srb.reset();
    m_passDisplay.srb.reset();

    for (auto &rt : m_velRT) rt.reset();
    for (auto &rt : m_pressureRT) rt.reset();
    m_advectionFwdRT.reset(); m_advectionRevRT.reset();
    m_divergenceRT.reset(); m_noiseRT.reset();
    m_displayRT.reset();

    m_passNoise.pipeline.reset();
    for (auto &p : m_passAdvection) p.pipeline.reset();
    m_passAdvectionRev.pipeline.reset();
    for (auto &p : m_passAdjust) p.pipeline.reset();
    for (auto &p : m_passDiffuse) p.pipeline.reset();
    for (auto &p : m_passInjectNoise) p.pipeline.reset();
    for (auto &p : m_passDivergence) p.pipeline.reset();
    for (auto &p : m_passPressure) p.pipeline.reset();
    for (auto &a : m_passSubtract) for (auto &p : a) p.pipeline.reset();
    m_passDisplay.pipeline.reset();

    m_rpDescRGBA16F.reset();
    m_rpDescR32F.reset();
    m_rpDescRGBA8.reset();

    m_rhi = nullptr;
    m_initialized = false;
}

void FluidSimEngine::init(QRhi *rhi, int fluidSize)
{
    m_rhi = rhi;
    m_fluidSize = fluidSize;

    fprintf(stderr, "FluidSimEngine::init: rhi=%p backend=%d\n",
        (void*)rhi, (int)rhi->backend());

    if (!m_rhi->isTextureFormatSupported(QRhiTexture::RGBA16F, QRhiTexture::RenderTarget)) {
        fprintf(stderr, "  ERROR: RGBA16F render targets not supported!\n");
        return;
    }

    createTextures();
    createSamplers();
    createBuffers();
    createRenderTargets();
    createGraphicsPipelines();
    createDisplayPass();

    m_initialized = true;
    fprintf(stderr, "FluidSimEngine::init DONE\n");
}

void FluidSimEngine::createTextures()
{
    auto makeTex = [&](const char *name, int w, int h,
                       QRhiTexture::Format fmt = QRhiTexture::RGBA16F,
                       QRhiTexture::Flags flags = {})
    {
        auto t = std::unique_ptr<QRhiTexture>(m_rhi->newTexture(fmt, {w, h}, 1, flags));
        t->setName(name);
        t->create();
        return t;
    };

    QRhiTexture::Flags texFlags = QRhiTexture::RenderTarget;
    fprintf(stderr, "  RGBA16F supported=%d R32F supported=%d\n",
        (int)m_rhi->isTextureFormatSupported(QRhiTexture::RGBA16F, texFlags),
        (int)m_rhi->isTextureFormatSupported(QRhiTexture::R32F, texFlags));

    int s = m_fluidSize;
    int ns = 2 * m_fluidSize;

    m_velocityTex[0] = makeTex("velocity0", s, s, QRhiTexture::RGBA16F, texFlags);
    m_velocityTex[1] = makeTex("velocity1", s, s, QRhiTexture::RGBA16F, texFlags);
    m_displayTex = makeTex("display", s, s, QRhiTexture::RGBA8, texFlags);
    m_pressureTex[0] = makeTex("pressure0", s, s, QRhiTexture::R32F, texFlags);
    m_pressureTex[1] = makeTex("pressure1", s, s, QRhiTexture::R32F, texFlags);
    m_noiseTex = makeTex("noise", ns, ns, QRhiTexture::RGBA16F, texFlags);
    m_advectionFwdTex = makeTex("advectionFwd", s, s, QRhiTexture::RGBA16F, texFlags);
    m_advectionRevTex = makeTex("advectionRev", s, s, QRhiTexture::RGBA16F, texFlags);
    m_divergenceTex = makeTex("divergence", s, s, QRhiTexture::R32F, texFlags);

    QRhiResourceUpdateBatch *ub = m_rhi->nextResourceUpdateBatch();
    auto clearTex = [&](QRhiTexture *tex, int pixelCount, int bytesPerPixel) {
        QByteArray zero(pixelCount * bytesPerPixel, '\0');
        QRhiTextureSubresourceUploadDescription subDesc(zero);
        QRhiTextureUploadEntry entry(0, 0, subDesc);
        QRhiTextureUploadDescription desc({entry});
        ub->uploadTexture(tex, desc);
    };
    int bpp = 8;
    for (auto *tex : {m_velocityTex[0].get(), m_velocityTex[1].get(),
                      m_pressureTex[0].get(), m_pressureTex[1].get(),
                      m_advectionFwdTex.get(), m_advectionRevTex.get()}) {
        clearTex(tex, s * s, bpp);
    }
    clearTex(m_divergenceTex.get(), s * s, 4);
    clearTex(m_noiseTex.get(), ns * ns, bpp);

    m_pendingUploadBatch = ub;
}

void FluidSimEngine::createSamplers()
{
    m_linearSampler.reset(m_rhi->newSampler(
        QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::None,
        QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge));
    m_linearSampler->create();

    m_nearestSampler.reset(m_rhi->newSampler(
        QRhiSampler::Nearest, QRhiSampler::Nearest, QRhiSampler::None,
        QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge));
    m_nearestSampler->create();
}

void FluidSimEngine::createBuffers()
{
    auto makeBuf = [&](const char *name, int size, QRhiBuffer::UsageFlags usage) {
        auto b = std::unique_ptr<QRhiBuffer>(m_rhi->newBuffer(
            QRhiBuffer::Dynamic, usage, size));
        b->setName(name);
        b->create();
        return b;
    };

    m_fluidUniformBuf = makeBuf("fluidUniforms", (int)sizeof(FluidUniforms), QRhiBuffer::UniformBuffer);
    m_directionBuf = makeBuf("direction", (int)sizeof(Direction), QRhiBuffer::UniformBuffer);
    m_noiseUniformBuf = makeBuf("noiseUniforms", (int)sizeof(NoiseUniforms), QRhiBuffer::UniformBuffer);
    m_noiseChannelBuf = makeBuf("noiseChannels", (int)sizeof(NoiseChannelBlock), QRhiBuffer::UniformBuffer);
    m_pushConstantBuf = makeBuf("pushConstants", (int)sizeof(PushConstants), QRhiBuffer::UniformBuffer);

    float dt = m_fluidTimestep;
    float centerFactor = 1.0f / (m_viscosity * dt);
    float stencilFactor = 1.0f / (4.0f + centerFactor);
    fprintf(stderr, "  params: dt=%.6f viscosity=%.1f size=%d\n", dt, m_viscosity, m_fluidSize);
    fprintf(stderr, "  params: center=%.6f stencil=%.6f\n", centerFactor, stencilFactor);
    FluidUniforms fu = { dt, m_dissipation, -1.0f, 0.25f, centerFactor, stencilFactor };
    Direction dir = { 1.0f, {} };
    NoiseUniforms nu = { m_noiseMultiplier, float(2 * m_fluidSize), {} };
    PushConstants pc = { dt, {} };

    QRhiResourceUpdateBatch *ub = m_rhi->nextResourceUpdateBatch();
    ub->uploadStaticBuffer(m_fluidUniformBuf.get(), QByteArray((const char*)&fu, sizeof(fu)));
    ub->uploadStaticBuffer(m_directionBuf.get(), QByteArray((const char*)&dir, sizeof(dir)));
    ub->uploadStaticBuffer(m_noiseUniformBuf.get(), QByteArray((const char*)&nu, sizeof(nu)));
    ub->uploadStaticBuffer(m_pushConstantBuf.get(), QByteArray((const char*)&pc, sizeof(pc)));
    ub->release();
}

QRhiShaderResourceBindings *FluidSimEngine::buildBinding(
    std::initializer_list<QRhiShaderResourceBinding> list)
{
    auto srb = std::unique_ptr<QRhiShaderResourceBindings>(m_rhi->newShaderResourceBindings());
    srb->setBindings(list);
    srb->create();
    return srb.release();
}

void FluidSimEngine::createRenderTargets()
{
    // Create first RGBA16F render target to obtain a shared RPDesc
    m_velRT[0].reset(m_rhi->newTextureRenderTarget({m_velocityTex[0].get()}));
    m_rpDescRGBA16F.reset(m_velRT[0]->newCompatibleRenderPassDescriptor());
    m_rpDescRGBA16F->setName("rpRGBA16F");
    m_velRT[0]->setRenderPassDescriptor(m_rpDescRGBA16F.get());
    m_velRT[0]->setName("velRT0");
    m_velRT[0]->create();

    // Share the same RPDesc for all RGBA16F RTs
    auto makeSharedRT = [&](QRhiTexture *tex, QRhiRenderPassDescriptor *rpDesc, const char *name) {
        auto rt = m_rhi->newTextureRenderTarget({tex});
        rt->setRenderPassDescriptor(rpDesc);
        rt->setName(name);
        rt->create();
        return rt;
    };
    m_velRT[1].reset(makeSharedRT(m_velocityTex[1].get(), m_rpDescRGBA16F.get(), "velRT1"));
    m_advectionFwdRT.reset(makeSharedRT(m_advectionFwdTex.get(), m_rpDescRGBA16F.get(), "advectionFwdRT"));
    m_advectionRevRT.reset(makeSharedRT(m_advectionRevTex.get(), m_rpDescRGBA16F.get(), "advectionRevRT"));
    m_noiseRT.reset(makeSharedRT(m_noiseTex.get(), m_rpDescRGBA16F.get(), "noiseRT"));

    // R32F render target + shared RPDesc
    m_pressureRT[0].reset(m_rhi->newTextureRenderTarget({m_pressureTex[0].get()}));
    m_rpDescR32F.reset(m_pressureRT[0]->newCompatibleRenderPassDescriptor());
    m_rpDescR32F->setName("rpR32F");
    m_pressureRT[0]->setRenderPassDescriptor(m_rpDescR32F.get());
    m_pressureRT[0]->setName("pressRT0");
    m_pressureRT[0]->create();

    m_pressureRT[1].reset(makeSharedRT(m_pressureTex[1].get(), m_rpDescR32F.get(), "pressRT1"));
    m_divergenceRT.reset(makeSharedRT(m_divergenceTex.get(), m_rpDescR32F.get(), "divergenceRT"));
}

void FluidSimEngine::createGraphicsPipelines()
{
    QRhiSampler *nearest = m_nearestSampler.get();
    QRhiSampler *linear = m_linearSampler.get();
    QRhiBuffer *fluidUBuf = m_fluidUniformBuf.get();
    QRhiBuffer *dirBuf = m_directionBuf.get();
    QRhiBuffer *noiseUBuf = m_noiseUniformBuf.get();
    QRhiBuffer *noiseChBuf = m_noiseChannelBuf.get();
    QRhiBuffer *pushBuf = m_pushConstantBuf.get();

    QShader quadVs = FluidSimShaders::loadShader("fullscreen_quad");
    if (!quadVs.isValid())
        fprintf(stderr, "  CRITICAL: fullscreen_quad.vert missing!\n");

    auto makeQuad = [this]() -> QRhiBuffer* {
        struct QuadVertex { float x, y; };
        QuadVertex verts[4] = { {-1,-1}, {1,-1}, {-1,1}, {1,1} };
        auto buf = m_rhi->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::VertexBuffer, sizeof(verts));
        buf->setName("fullscreenQuad");
        buf->create();
        QRhiResourceUpdateBatch *ub = m_rhi->nextResourceUpdateBatch();
        ub->uploadStaticBuffer(buf, QByteArray((const char*)verts, sizeof(verts)));
        m_pendingQuadUploadBatch = ub;
        return buf;
    };
    m_quadVertexBuf.reset(makeQuad());

    auto makePipeline = [&](const char *fragName, QRhiShaderResourceBindings *srb,
                            QRhiRenderPassDescriptor *rpDesc,
                            std::unique_ptr<QRhiGraphicsPipeline> &outPipeline)
    {
        QShader fs = FluidSimShaders::loadShader(fragName);
        if (!fs.isValid()) {
            fprintf(stderr, "  CRITICAL: %s shader missing!\n", fragName);
            return;
        }
        auto *pp = m_rhi->newGraphicsPipeline();
        pp->setShaderStages({
            { QRhiShaderStage::Vertex, quadVs },
            { QRhiShaderStage::Fragment, fs }
        });
        pp->setRenderPassDescriptor(rpDesc);
        pp->setShaderResourceBindings(srb);
        QRhiVertexInputLayout inputLayout;
        inputLayout.setBindings({ QRhiVertexInputBinding(2 * sizeof(float)) });
        inputLayout.setAttributes({
            QRhiVertexInputAttribute(0, 0, QRhiVertexInputAttribute::Float2, 0)
        });
        pp->setVertexInputLayout(inputLayout);
        pp->setTopology(QRhiGraphicsPipeline::TriangleStrip);
        QRhiGraphicsPipeline::TargetBlend blend;
        blend.enable = false;
        pp->setTargetBlends({ blend });
        pp->create();
        outPipeline.reset(pp);
    };

    // Noise (procedural, no texture inputs)
    m_passNoise.srb.reset(buildBinding({ }));
    makePipeline("pass_noise", m_passNoise.srb.get(), m_rpDescRGBA16F.get(), m_passNoise.pipeline);

    // Advection [0] reads vel[0] (linear sampler for smooth backtracking)
    m_passAdvection[0].srb.reset(buildBinding({
        QRhiShaderResourceBinding::sampledTexture(0, QRhiShaderResourceBinding::FragmentStage, m_velocityTex[0].get(), linear),
    }));
    makePipeline("pass_advect", m_passAdvection[0].srb.get(), m_rpDescRGBA16F.get(), m_passAdvection[0].pipeline);

    // Advection [1] reads vel[1]
    m_passAdvection[1].srb.reset(buildBinding({
        QRhiShaderResourceBinding::sampledTexture(0, QRhiShaderResourceBinding::FragmentStage, m_velocityTex[1].get(), linear),
    }));
    makePipeline("pass_advect", m_passAdvection[1].srb.get(), m_rpDescRGBA16F.get(), m_passAdvection[1].pipeline);

    // Advection reverse (reads advectionFwd, linear sampler, direction=-1)
    m_passAdvectionRev.srb.reset(buildBinding({
        QRhiShaderResourceBinding::sampledTexture(0, QRhiShaderResourceBinding::FragmentStage, m_advectionFwdTex.get(), linear),
    }));
    makePipeline("pass_advect_rev", m_passAdvectionRev.srb.get(), m_rpDescRGBA16F.get(), m_passAdvectionRev.pipeline);

    // Adjust [vi] reads forwardTex, reverseTex, vel[vi] (nearest sampler for texelFetch)
    for (int i = 0; i < 2; i++) {
        m_passAdjust[i].srb.reset(buildBinding({
            QRhiShaderResourceBinding::sampledTexture(0, QRhiShaderResourceBinding::FragmentStage, m_advectionFwdTex.get(), nearest),
            QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage, m_advectionRevTex.get(), nearest),
            QRhiShaderResourceBinding::sampledTexture(2, QRhiShaderResourceBinding::FragmentStage, m_velocityTex[i].get(), nearest),
        }));
        makePipeline("pass_adjust", m_passAdjust[i].srb.get(), m_rpDescRGBA16F.get(), m_passAdjust[i].pipeline);
    }

    // Diffuse [vi] reads vel[vi]
    for (int i = 0; i < 2; i++) {
        m_passDiffuse[i].srb.reset(buildBinding({
            QRhiShaderResourceBinding::sampledTexture(0, QRhiShaderResourceBinding::FragmentStage, m_velocityTex[i].get(), nearest),
        }));
        makePipeline("pass_diffuse", m_passDiffuse[i].srb.get(), m_rpDescRGBA16F.get(), m_passDiffuse[i].pipeline);
    }

    // Inject noise [vi] reads noiseTex (linear sampler, matching reference) + vel[vi]
    for (int i = 0; i < 2; i++) {
        m_passInjectNoise[i].srb.reset(buildBinding({
            QRhiShaderResourceBinding::sampledTexture(0, QRhiShaderResourceBinding::FragmentStage, m_noiseTex.get(), linear),
            QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage, m_velocityTex[i].get(), nearest),
        }));
        makePipeline("pass_inject_noise", m_passInjectNoise[i].srb.get(), m_rpDescRGBA16F.get(), m_passInjectNoise[i].pipeline);
    }

    // Divergence [vi] reads vel[vi]
    for (int i = 0; i < 2; i++) {
        m_passDivergence[i].srb.reset(buildBinding({
            QRhiShaderResourceBinding::sampledTexture(0, QRhiShaderResourceBinding::FragmentStage, m_velocityTex[i].get(), nearest),
        }));
        makePipeline("pass_divergence", m_passDivergence[i].srb.get(), m_rpDescR32F.get(), m_passDivergence[i].pipeline);
    }

    // Pressure [pi] reads divergenceTex + pressureTex[pi]
    for (int i = 0; i < 2; i++) {
        m_passPressure[i].srb.reset(buildBinding({
            QRhiShaderResourceBinding::sampledTexture(0, QRhiShaderResourceBinding::FragmentStage, m_divergenceTex.get(), nearest),
            QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage, m_pressureTex[i].get(), nearest),
        }));
        makePipeline("pass_pressure", m_passPressure[i].srb.get(), m_rpDescR32F.get(), m_passPressure[i].pipeline);
    }

    // Subtract [vi][pi] reads pressureTex[pi] (linear for smooth gradient) + vel[vi]
    for (int vi = 0; vi < 2; vi++) {
        for (int pi = 0; pi < 2; pi++) {
            m_passSubtract[vi][pi].srb.reset(buildBinding({
                QRhiShaderResourceBinding::sampledTexture(0, QRhiShaderResourceBinding::FragmentStage, m_pressureTex[pi].get(), linear),
                QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage, m_velocityTex[vi].get(), nearest),
            }));
            makePipeline("pass_subtract", m_passSubtract[vi][pi].srb.get(), m_rpDescRGBA16F.get(), m_passSubtract[vi][pi].pipeline);
        }
    }

    // Dummy (checkerboard)
    // -- No more dummy pipelines needed

    fprintf(stderr, "  All solver pipelines created\n");
}

void FluidSimEngine::createDisplayPass()
{
    int ds = 2 * m_fluidSize;  // 256

    m_displayTex.reset(m_rhi->newTexture(QRhiTexture::RGBA8, {ds, ds}, 1, QRhiTexture::RenderTarget));
    m_displayTex->setName("displayTex");
    m_displayTex->create();

    m_displayRT.reset(m_rhi->newTextureRenderTarget({m_displayTex.get()}));
    m_rpDescRGBA8.reset(m_displayRT->newCompatibleRenderPassDescriptor());
    m_rpDescRGBA8->setName("rpRGBA8");
    m_displayRT->setRenderPassDescriptor(m_rpDescRGBA8.get());
    m_displayRT->setName("displayRT");
    m_displayRT->create();

    auto makeDisplayPipeline = [&](const QString &fragName, PassPipeline &out) {
        QShader vs = FluidSimShaders::loadShader("fullscreen_quad");
        QShader fs = FluidSimShaders::loadShader(fragName);
        // Temp SRB just for pipeline creation (will be replaced each frame)
        out.srb.reset(buildBinding({
            QRhiShaderResourceBinding::sampledTexture(0, QRhiShaderResourceBinding::FragmentStage,
                m_velocityTex[0].get(), m_nearestSampler.get()),
        }));
        auto *pp = m_rhi->newGraphicsPipeline();
        pp->setShaderStages({ { QRhiShaderStage::Vertex, vs }, { QRhiShaderStage::Fragment, fs } });
        pp->setRenderPassDescriptor(m_rpDescRGBA8.get());
        pp->setShaderResourceBindings(out.srb.get());
        QRhiVertexInputLayout inputLayout;
        inputLayout.setBindings({ QRhiVertexInputBinding(2 * sizeof(float)) });
        inputLayout.setAttributes({ QRhiVertexInputAttribute(0, 0, QRhiVertexInputAttribute::Float2, 0) });
        pp->setVertexInputLayout(inputLayout);
        QRhiGraphicsPipeline::TargetBlend blend;
        blend.enable = false;
        pp->setTargetBlends({ blend });
        pp->setTopology(QRhiGraphicsPipeline::TriangleStrip);
        pp->create();
        out.pipeline.reset(pp);
    };

    makeDisplayPipeline("display_frag", m_passDisplay);
    makeDisplayPipeline("display_debug", m_passDebug);

    fprintf(stderr, "  Display passes created (%dx%d RGBA8)\n", ds, ds);
}

static constexpr int PASSES_PER_FRAME = 32;
static constexpr int TOTAL_PHASES =
    1   // 0: advect forward (vel[vi] → advectionFwd)
    + 1   // 1: advect reverse (advectionFwd → advectionRev)
    + 1   // 2: adjust advection (fwd+rev+vel[vi] → vel[1-vi])
    + 3   // 3-5: diffuse ×3
    + 1   // 6: inject noise
    + 1   // 7: divergence
    + 19  // 8-26: pressure ×19
    + 1   // 27: subtract gradient
    ;

void FluidSimEngine::step(QRhiCommandBuffer *cb, float dt)
{
    if (!m_initialized) return;

    // Collect all pending uploads to process at first beginPass
    // Drop separately submitted batches; they'll execute at the first beginPass
    // since QRhi defers resourceUpdate() outside a pass to the next beginPass

    for (auto *batch : {m_pendingUploadBatch, m_pendingDisplayUploadBatch, m_pendingQuadUploadBatch}) {
        if (batch) {
            // Just submit here — QRhi defers to next beginPass internally
            cb->resourceUpdate(batch);
        }
    }
    m_pendingUploadBatch = nullptr;
    m_pendingDisplayUploadBatch = nullptr;
    m_pendingQuadUploadBatch = nullptr;

    int s = m_fluidSize;
    int ns = 2 * m_fluidSize;

    // Upload CPU-generated noise at each frame start
    if (m_stepPhase == 0) {
        updateNoiseChannels(dt);
        QByteArray noiseData(ns * ns * 8, Qt::Uninitialized);
        uint16_t *np = reinterpret_cast<uint16_t*>(noiseData.data());
        for (int y = 0; y < ns; y++) {
            for (int x = 0; x < ns; x++) {
                float tu = (float(x) + 0.5f) / float(ns);
                float tv = (float(y) + 0.5f) / float(ns);
                float nx = 0, ny = 0;
                for (int ci = 0; ci < NUM_CHANNELS; ci++) {
                    auto &c = m_channels[ci];
                    float sx = c.scale[0] * tu;
                    float sy = c.scale[1] * tv;
                    float n1x, n1y;
                    simplexNoisePair(sx, sy, c.offset_1, n1x, n1y);
                    if (c.blend_factor > 0) {
                        float n2x, n2y;
                        simplexNoisePair(sx, sy, c.offset_2, n2x, n2y);
                        n1x += c.blend_factor * (n2x - n1x);
                        n1y += c.blend_factor * (n2y - n1y);
                    }
                    nx += c.multiplier * n1x;
                    ny += c.multiplier * n1y;
                }
                nx *= m_noiseMultiplier;
                ny *= m_noiseMultiplier;
                int idx = (y * ns + x) * 4;
                np[idx+0] = f32to16(nx);
                np[idx+1] = f32to16(ny);
                np[idx+2] = f32to16(0.0f);
                np[idx+3] = f32to16(1.0f);
            }
        }
        auto *noiseUb = m_rhi->nextResourceUpdateBatch();
        QRhiTextureSubresourceUploadDescription noiseSubDesc(noiseData);
        noiseUb->uploadTexture(m_noiseTex.get(), QRhiTextureUploadDescription({{0, 0, noiseSubDesc}}));
        cb->resourceUpdate(noiseUb);
    }

    int vi = m_velocityIndex;
    int pi = m_pressureIndex;
    int &ph = m_stepPhase;

    for (int done = 0; done < PASSES_PER_FRAME && ph < TOTAL_PHASES; done++, ph++) {
        switch (ph) {
        case 0: // Advect forward — reads vel[vi], writes to advectionFwd
            drawPass(cb, m_advectionFwdRT.get(), m_passAdvection[vi], s, s, nullptr);
            break;

        case 1: // Advect reverse — reads advectionFwd, writes to advectionRev (direction=-1)
            drawPass(cb, m_advectionRevRT.get(), m_passAdvectionRev, s, s, nullptr);
            break;

        case 2: // Adjust advection (MacCormack) — reads fwd+rev+vel[vi], writes to vel[1-vi]
            drawPass(cb, m_velRT[1 - vi].get(), m_passAdjust[vi], s, s, nullptr);
            vi = 1 - vi;
            break;

        case 3: case 4: case 5: // Diffuse ×3 — reads vel[vi]
            drawPass(cb, m_velRT[1 - vi].get(), m_passDiffuse[vi], s, s, nullptr);
            vi = 1 - vi;
            break;

        case 6: // Inject noise — reads noiseTex + vel[vi]
            drawPass(cb, m_velRT[1 - vi].get(), m_passInjectNoise[vi], s, s, nullptr);
            vi = 1 - vi;
            break;

        case 7: // Divergence — reads vel[vi], writes to divergenceTex
            drawPass(cb, m_divergenceRT.get(), m_passDivergence[vi], s, s, nullptr);
            break;

        default: // Pressure ×19 (ph 8-26)
            if (ph >= 8 && ph <= 26) {
                drawPass(cb, m_pressureRT[1 - pi].get(), m_passPressure[pi], s, s, nullptr);
                pi = 1 - pi;
                break;
            }
            if (ph == 27) { // Subtract gradient — reads press[pi] + vel[vi]
                drawPass(cb, m_velRT[1 - vi].get(), m_passSubtract[vi][pi], s, s, nullptr);
                vi = 1 - vi;
                break;
            }
            break;
        }
    }

    m_velocityIndex = vi;
    m_pressureIndex = pi;

    if (ph >= TOTAL_PHASES) {
        ph = 0;
        m_frameCount++;

        int ds = 2 * m_fluidSize;  // display texture size: 256

        // Select display pipeline + binding based on debug mode
        QRhiGraphicsPipeline *pipeline = nullptr;
        std::unique_ptr<QRhiShaderResourceBindings> srb;

        switch (m_debugMode) {
        case 0: // Normal — heatmap from velocity
            pipeline = m_passDisplay.pipeline.get();
            srb.reset(buildBinding({
                QRhiShaderResourceBinding::sampledTexture(0, QRhiShaderResourceBinding::FragmentStage,
                    m_velocityTex[m_velocityIndex].get(), m_nearestSampler.get()),
            }));
            break;
        case 1: // Noise — raw noise texture
            pipeline = m_passDebug.pipeline.get();
            srb.reset(buildBinding({
                QRhiShaderResourceBinding::sampledTexture(0, QRhiShaderResourceBinding::FragmentStage,
                    m_noiseTex.get(), m_nearestSampler.get()),
            }));
            break;
        case 2: // Fluid — raw velocity
            pipeline = m_passDebug.pipeline.get();
            srb.reset(buildBinding({
                QRhiShaderResourceBinding::sampledTexture(0, QRhiShaderResourceBinding::FragmentStage,
                    m_velocityTex[m_velocityIndex].get(), m_nearestSampler.get()),
            }));
            break;
        case 3: // Pressure
            pipeline = m_passDebug.pipeline.get();
            srb.reset(buildBinding({
                QRhiShaderResourceBinding::sampledTexture(0, QRhiShaderResourceBinding::FragmentStage,
                    m_pressureTex[m_pressureIndex].get(), m_nearestSampler.get()),
            }));
            break;
        case 4: // Divergence
            pipeline = m_passDebug.pipeline.get();
            srb.reset(buildBinding({
                QRhiShaderResourceBinding::sampledTexture(0, QRhiShaderResourceBinding::FragmentStage,
                    m_divergenceTex.get(), m_nearestSampler.get()),
            }));
            break;
        }
        drawPass(cb, m_displayRT.get(), pipeline, srb.get(), ds, ds, nullptr);
    }
}

void FluidSimEngine::updateNoiseChannels(float dt)
{
    m_elapsedTime += dt;
    if (m_elapsedTime >= MAX_ELAPSED)
        m_elapsedTime -= MAX_ELAPSED;
    for (int i = 0; i < NUM_CHANNELS; i++) {
        auto &ch = m_channels[i];
        float scale = CHANNEL_CFG[i].scale * (1.0f + 0.15f * std::sin(0.01f * m_elapsedTime * float(TAU)));
        ch.scale[0] = scale;
        ch.scale[1] = scale;
        ch.multiplier = CHANNEL_CFG[i].mult;
        ch.offset_1 += CHANNEL_CFG[i].inc;
        if (ch.offset_1 > 1000.0f) {
            ch.blend_factor += CHANNEL_CFG[i].inc;
            ch.offset_2 += CHANNEL_CFG[i].inc;
        }
        if (ch.blend_factor > 1.0f) {
            ch.offset_1 = ch.offset_2;
            ch.offset_2 = 0.0f;
            ch.blend_factor = 0.0f;
        }
    }
}

void FluidSimEngine::drawPass(QRhiCommandBuffer *cb, QRhiTextureRenderTarget *rt,
                              PassPipeline &pass, int w, int h,
                              QRhiResourceUpdateBatch *ub)
{
    drawPass(cb, rt, pass.pipeline.get(), pass.srb.get(), w, h, ub);
}

void FluidSimEngine::drawPass(QRhiCommandBuffer *cb, QRhiTextureRenderTarget *rt,
                              QRhiGraphicsPipeline *pipeline, QRhiShaderResourceBindings *srb,
                              int w, int h, QRhiResourceUpdateBatch *ub)
{
    cb->beginPass(rt, QColor(0, 0, 0, 0), QRhiDepthStencilClearValue{1.0f, 0}, ub);
    cb->setGraphicsPipeline(pipeline);
    cb->setShaderResources(srb);
    cb->setViewport(QRhiViewport(0, 0, float(w), float(h)));
    QRhiCommandBuffer::VertexInput vi(m_quadVertexBuf.get(), 0);
    cb->setVertexInput(0, 1, &vi);
    cb->draw(4);
    cb->endPass();
}
