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

static int noiseHash(int x, int y, int seed) {
    unsigned int h = unsigned(seed);
    h = h * 374761393u + unsigned(x) * 3266489917u + unsigned(y) * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return int(h & 0x7fffffffu);
}

static float smoothNoise(float x, float y, int seed) {
    int ix = int(floor(x));
    int iy = int(floor(y));
    float fx = x - float(ix);
    float fy = y - float(iy);
    fx = fx * fx * (3.0f - 2.0f * fx);
    fy = fy * fy * (3.0f - 2.0f * fy);
    float n00 = float(noiseHash(ix, iy, seed)) / float(0x7fffffffu);
    float n10 = float(noiseHash(ix+1, iy, seed)) / float(0x7fffffffu);
    float n01 = float(noiseHash(ix, iy+1, seed)) / float(0x7fffffffu);
    float n11 = float(noiseHash(ix+1, iy+1, seed)) / float(0x7fffffffu);
    return n00 + (n10 - n00) * fx + ((n01 + (n11 - n01) * fx) - (n00 + (n10 - n00) * fx)) * fy;
}

static float fbm(float x, float y, int seed) {
    float v = 0.0f, a = 1.0f, f = 1.0f;
    for (int i = 0; i < 3; i++) { v += a * smoothNoise(x*f, y*f, seed+i*100); a *= 0.5f; f *= 2.0f; }
    return v * 2.0f - 1.0f;
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
    { 2.8f,  1.0f, 0.25f },
    { 15.0f, 0.7f, 0.04f },
    { 30.0f, 0.5f, 0.01f },
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
    m_displayVertexBuf.reset();

    m_passNoise.srb.reset();
    for (auto &p : m_passAdvection) p.srb.reset();
    m_passAdvectionRev.srb.reset();
    for (auto &p : m_passAdjust) p.srb.reset();
    for (auto &p : m_passDiffuse) p.srb.reset();
    for (auto &p : m_passInjectNoise) p.srb.reset();
    for (auto &p : m_passDivergence) p.srb.reset();
    for (auto &p : m_passPressure) p.srb.reset();
    for (auto &a : m_passSubtract) for (auto &p : a) p.srb.reset();
    m_passDummy.srb.reset();

    for (auto &rt : m_velRT) rt.reset();
    for (auto &rt : m_pressureRT) rt.reset();
    m_advectionFwdRT.reset(); m_advectionRevRT.reset();
    m_divergenceRT.reset(); m_noiseRT.reset();

    m_passNoise.pipeline.reset();
    for (auto &p : m_passAdvection) p.pipeline.reset();
    m_passAdvectionRev.pipeline.reset();
    for (auto &p : m_passAdjust) p.pipeline.reset();
    for (auto &p : m_passDiffuse) p.pipeline.reset();
    for (auto &p : m_passInjectNoise) p.pipeline.reset();
    for (auto &p : m_passDivergence) p.pipeline.reset();
    for (auto &p : m_passPressure) p.pipeline.reset();
    for (auto &a : m_passSubtract) for (auto &p : a) p.pipeline.reset();
    m_passDummy.pipeline.reset();
    m_displayPipeline.reset();

    m_rpDescRGBA16F.reset();
    m_rpDescR32F.reset();
    m_displayRPDesc.reset();

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
    createDisplayPipeline(rhi);

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

    // Inject noise [vi] reads noiseTex + vel[vi]
    for (int i = 0; i < 2; i++) {
        m_passInjectNoise[i].srb.reset(buildBinding({
            QRhiShaderResourceBinding::sampledTexture(0, QRhiShaderResourceBinding::FragmentStage, m_noiseTex.get(), nearest),
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

    // Subtract [vi][pi] reads pressureTex[pi] + vel[vi]
    for (int vi = 0; vi < 2; vi++) {
        for (int pi = 0; pi < 2; pi++) {
            m_passSubtract[vi][pi].srb.reset(buildBinding({
                QRhiShaderResourceBinding::sampledTexture(0, QRhiShaderResourceBinding::FragmentStage, m_pressureTex[pi].get(), nearest),
                QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage, m_velocityTex[vi].get(), nearest),
            }));
            makePipeline("pass_subtract", m_passSubtract[vi][pi].srb.get(), m_rpDescRGBA16F.get(), m_passSubtract[vi][pi].pipeline);
        }
    }

    // Dummy (checkerboard)
    m_passDummy.srb.reset(buildBinding({ }));
    makePipeline("solid_red", m_passDummy.srb.get(), m_rpDescRGBA16F.get(), m_passDummy.pipeline);

    fprintf(stderr, "  All solver pipelines created\n");
}

void FluidSimEngine::createDisplayPipeline(QRhi *rhi)
{
    Q_UNUSED(rhi)
    QShader displayVs = FluidSimShaders::loadShader("display_vert");
    QShader displayFs = FluidSimShaders::loadShader("display_frag");

    m_displayVertexBuf.reset(m_rhi->newBuffer(
        QRhiBuffer::Immutable, QRhiBuffer::VertexBuffer, 4 * 4 * sizeof(float)));
    m_displayVertexBuf->setName("displayQuad");
    m_displayVertexBuf->create();

    float verts[] = {
        -1, -1,  0, 0,
         1, -1,  1, 0,
        -1,  1,  0, 1,
         1,  1,  1, 1,
    };
    m_pendingDisplayUploadBatch = m_rhi->nextResourceUpdateBatch();
    m_pendingDisplayUploadBatch->uploadStaticBuffer(m_displayVertexBuf.get(),
        QByteArray((const char*)verts, sizeof(verts)));
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

    // Upload CPU-generated noise at each frame start (deferred to next beginPass)
    if (m_stepPhase == 0) {
        QByteArray noiseData(ns * ns * 8, Qt::Uninitialized);
        uint16_t *np = reinterpret_cast<uint16_t*>(noiseData.data());
        static int noiseSeed = 0;
        noiseSeed++;
        for (int y = 0; y < ns; y++) {
            for (int x = 0; x < ns; x++) {
                float fx = float(x) / float(ns);
                float fy = float(y) / float(ns);
                float nx = fbm(fx, fy, noiseSeed) * m_noiseMultiplier;
                float ny = fbm(fx + 100.0f, fy + 100.0f, noiseSeed + 50) * m_noiseMultiplier;
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

        // Bypass: no frame-boundary dummy draw

        if (m_frameCount % 5 == 1) {
            // Read from current output (should be latest written)
            QRhiTexture *tex = m_velocityTex[m_velocityIndex].get();
            auto *ub = m_rhi->nextResourceUpdateBatch();
            QRhiReadbackDescription rb;
            rb.setTexture(tex);
            QRhiReadbackResult *res = new QRhiReadbackResult();
            res->completed = [res]() {
                if (res->data.size() >= 8) {
                    const uint16_t *p = (const uint16_t*)res->data.constData();
                    auto h2f = [](uint16_t h) -> float {
                        int s=(h>>15)&1, e=(h>>10)&0x1f, m=h&0x3ff;
                        if (e==0) return m?(s?-1:1)*(m/1024.0f)*0x1p-24f:(s?-0.0f:0.0f);
                        if (e==31) return s?-INFINITY:INFINITY;
                        float base = (s?-1:1) * (1.0f + float(m)/1024.0f);
                        float result = base * ldexpf(1.0f, e - 15);
                        return result;
                    };
                    const int S = 128;
                    auto rawAt = [&](int x, int y, int ch) { return p[(y * S + x) * 4 + ch]; };
                    fprintf(stderr, "  velI) [0,0]=(%.3f,%.3f) raw=(0x%04x,0x%04x)"
                            " [4,4]=(%.3f,%.3f) raw=(0x%04x,0x%04x)"
                            " [16,16]=(%.3f,%.3f) raw=(0x%04x,0x%04x)\n",
                        (double)h2f(p[0]), (double)h2f(p[1]), rawAt(0,0,0), rawAt(0,0,1),
                        (double)h2f(p[(4*S+4)*4]), (double)h2f(p[(4*S+4)*4+1]), rawAt(4,4,0), rawAt(4,4,1),
                        (double)h2f(p[(16*S+16)*4]), (double)h2f(p[(16*S+16)*4+1]), rawAt(16,16,0), rawAt(16,16,1));
                }
            };
            ub->readBackTexture(rb, res);
            cb->resourceUpdate(ub);
        }
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
    cb->beginPass(rt, QColor(0, 0, 0, 0), QRhiDepthStencilClearValue{1.0f, 0}, ub);
    cb->setGraphicsPipeline(pass.pipeline.get());
    cb->setShaderResources(pass.srb.get());
    cb->setViewport(QRhiViewport(0, 0, float(w), float(h)));
    QRhiCommandBuffer::VertexInput vi(m_quadVertexBuf.get(), 0);
    cb->setVertexInput(0, 1, &vi);
    cb->draw(4);
    cb->endPass();
}

QRhiShaderResourceBindings *FluidSimEngine::createDisplayBindings()
{
    QRhiSampler *nearest = m_nearestSampler.get();
    auto *srb = m_rhi->newShaderResourceBindings();
    srb->setBindings({
        QRhiShaderResourceBinding::sampledTexture(0, QRhiShaderResourceBinding::FragmentStage,
            m_velocityTex[1 - m_velocityIndex].get(), nearest),
    });
    srb->create();
    return srb;
}
