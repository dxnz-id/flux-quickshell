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

struct PushConstants {
    float timestep;
    float _pad[3];
};
static_assert(sizeof(PushConstants) == 16, "PushConstants must be 16 bytes for std140");

struct GpuNoiseParams {
    float elapsedTime;
    float _pad0[3];
    float ch0_off1, ch0_off2, ch0_blend, _pad1;
    float ch1_off1, ch1_off2, ch1_blend, _pad2;
    float ch2_off1, ch2_off2, ch2_blend, _pad3;
    float noiseSize[2];
    float _pad4[2];
};
static_assert(sizeof(GpuNoiseParams) == 80, "GpuNoiseParams must be 80 bytes");
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

static constexpr float MAX_ELAPSED = 1000.0f;

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
    m_pushConstantBuf.reset();
    m_gpuNoiseBuf.reset();

    m_quadVertexBuf.reset();
    m_testQuadVbuf.reset();
    m_testInstanceBuf.reset();

    m_passNoise.srb.reset();
    m_passTestInstancing.srb.reset();
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
    m_displayTex.reset();

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
    m_passTestInstancing.pipeline.reset();

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

    testComputeAndSSBO();

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
    m_pushConstantBuf = makeBuf("pushConstants", (int)sizeof(PushConstants), QRhiBuffer::UniformBuffer);
    m_gpuNoiseBuf = makeBuf("gpuNoise", (int)sizeof(GpuNoiseParams), QRhiBuffer::UniformBuffer);

    // Test instancing buffers (upload as pendingQuadUploadBatch)
    {
        struct QuadV { float x, y; };
        QuadV quadVerts[6] = {
            {-0.4f, -0.4f}, {-0.4f, 0.4f}, {0.4f, 0.4f},
            {0.4f, 0.4f}, {0.4f, -0.4f}, {-0.4f, -0.4f},
        };
        m_testQuadVbuf.reset(m_rhi->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::VertexBuffer, sizeof(quadVerts)));
        m_testQuadVbuf->setName("testQuadVbuf");
        m_testQuadVbuf->create();
        if (!m_pendingQuadUploadBatch)
            m_pendingQuadUploadBatch = m_rhi->nextResourceUpdateBatch();
        m_pendingQuadUploadBatch->uploadStaticBuffer(m_testQuadVbuf.get(), QByteArray((const char*)quadVerts, sizeof(quadVerts)));

        struct Inst { float col[4]; float off[2]; };
        Inst instances[4] = {
            {{1,0,0,1}, {-0.5f, -0.5f}},
            {{0,1,0,1}, { 0.5f, -0.5f}},
            {{0,0,1,1}, {-0.5f,  0.5f}},
            {{1,1,0,1}, { 0.5f,  0.5f}},
        };
        m_testInstanceBuf.reset(m_rhi->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::VertexBuffer, sizeof(instances)));
        m_testInstanceBuf->setName("testInstanceBuf");
        m_testInstanceBuf->create();
        m_pendingQuadUploadBatch->uploadStaticBuffer(m_testInstanceBuf.get(), QByteArray((const char*)instances, sizeof(instances)));
    }

    float dt = m_fluidTimestep;
    float centerFactor = 1.0f / (m_viscosity * dt);
    float stencilFactor = 1.0f / (4.0f + centerFactor);
    fprintf(stderr, "  params: dt=%.6f viscosity=%.1f size=%d\n", dt, m_viscosity, m_fluidSize);
    fprintf(stderr, "  params: center=%.6f stencil=%.6f\n", centerFactor, stencilFactor);
    FluidUniforms fu = { dt, m_dissipation, -1.0f, 0.25f, centerFactor, stencilFactor };
    Direction dir = { 1.0f, {} };
    PushConstants pc = { dt, {} };

    QRhiResourceUpdateBatch *ub = m_rhi->nextResourceUpdateBatch();
    ub->uploadStaticBuffer(m_fluidUniformBuf.get(), QByteArray((const char*)&fu, sizeof(fu)));
    ub->uploadStaticBuffer(m_directionBuf.get(), QByteArray((const char*)&dir, sizeof(dir)));
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

    // Noise (procedural, reads UBO for animated params, writes to noiseTex)
    m_passNoise.srb.reset(buildBinding({
        QRhiShaderResourceBinding::uniformBuffer(0, QRhiShaderResourceBinding::FragmentStage, m_gpuNoiseBuf.get()),
    }));
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

    // Test instancing pipeline
    {
        QShader testVs = FluidSimShaders::loadShader("test_instancing_vs");
        QShader testFs = FluidSimShaders::loadShader("test_instancing_fs");
        m_passTestInstancing.srb.reset(buildBinding({}));
        auto *pp = m_rhi->newGraphicsPipeline();
        pp->setShaderStages({ { QRhiShaderStage::Vertex, testVs }, { QRhiShaderStage::Fragment, testFs } });
        pp->setRenderPassDescriptor(m_rpDescRGBA8.get());
        pp->setShaderResourceBindings(m_passTestInstancing.srb.get());
        QRhiVertexInputLayout inputLayout;
        inputLayout.setBindings({
            QRhiVertexInputBinding(2 * sizeof(float)),
            QRhiVertexInputBinding(24, QRhiVertexInputBinding::PerInstance),
        });
        inputLayout.setAttributes({
            QRhiVertexInputAttribute(0, 0, QRhiVertexInputAttribute::Float2, 0),
            QRhiVertexInputAttribute(1, 1, QRhiVertexInputAttribute::Float4, 0),
            QRhiVertexInputAttribute(1, 2, QRhiVertexInputAttribute::Float2, 16),
        });
        pp->setVertexInputLayout(inputLayout);
        QRhiGraphicsPipeline::TargetBlend blend;
        blend.enable = false;
        pp->setTargetBlends({ blend });
        pp->setTopology(QRhiGraphicsPipeline::Triangles);
        pp->create();
        m_passTestInstancing.pipeline.reset(pp);
        fprintf(stderr, "  Test instancing pipeline created\n");
    }

    fprintf(stderr, "  Display passes created (%dx%d RGBA8)\n", ds, ds);
}

static constexpr int PASSES_PER_FRAME = 32;
static constexpr int TOTAL_PHASES =
     1   // 0: GPU noise (writes noiseTex)
    + 1   // 1: advect forward (vel[vi] → advectionFwd)
    + 1   // 2: advect reverse (advectionFwd → advectionRev)
    + 1   // 3: adjust advection (fwd+rev+vel[vi] → vel[1-vi])
    + 3   // 4-6: diffuse ×3
    + 1   // 7: inject noise
    + 1   // 8: divergence
    + 19  // 9-27: pressure ×19
    + 1   // 28: subtract gradient
    ;

void FluidSimEngine::step(QRhiCommandBuffer *cb, float dt)
{
    if (!m_initialized) return;

    // Collect all pending uploads to process at first beginPass
    for (auto *batch : {m_pendingUploadBatch, m_pendingDisplayUploadBatch, m_pendingQuadUploadBatch}) {
        if (batch) {
            cb->resourceUpdate(batch);
        }
    }
    m_pendingUploadBatch = nullptr;
    m_pendingDisplayUploadBatch = nullptr;
    m_pendingQuadUploadBatch = nullptr;

    int s = m_fluidSize;

    // GPU noise generation at frame start
    if (m_stepPhase == 0) {
        updateNoiseChannels(dt);
        GpuNoiseParams gp;
        memset(&gp, 0, sizeof(gp));
        gp.elapsedTime = m_elapsedTime;
        for (int i = 0; i < NUM_CHANNELS; i++) {
            float *off1 = (i == 0) ? &gp.ch0_off1 : (i == 1 ? &gp.ch1_off1 : &gp.ch2_off1);
            float *off2 = (i == 0) ? &gp.ch0_off2 : (i == 1 ? &gp.ch1_off2 : &gp.ch2_off2);
            float *blend = (i == 0) ? &gp.ch0_blend : (i == 1 ? &gp.ch1_blend : &gp.ch2_blend);
            *off1 = m_channels[i].offset_1;
            *off2 = m_channels[i].offset_2;
            *blend = m_channels[i].blend_factor;
        }
        QSize noisePx = m_noiseTex->pixelSize();
        gp.noiseSize[0] = float(noisePx.width());
        gp.noiseSize[1] = float(noisePx.height());
        auto *ub = m_rhi->nextResourceUpdateBatch();
        ub->uploadStaticBuffer(m_gpuNoiseBuf.get(), QByteArray((const char*)&gp, sizeof(gp)));
        drawPass(cb, m_noiseRT.get(), m_passNoise, noisePx.width(), noisePx.height(), ub);
    }

    int vi = m_velocityIndex;
    int pi = m_pressureIndex;
    int &ph = m_stepPhase;

    QSize noisePx = m_noiseTex->pixelSize();
    for (int done = 0; done < PASSES_PER_FRAME && ph < TOTAL_PHASES; done++, ph++) {
        switch (ph) {
        case 0: // GPU noise — writes noiseTex
            drawPass(cb, m_noiseRT.get(), m_passNoise, noisePx.width(), noisePx.height(), nullptr);
            break;

        case 1: // Advect forward — reads vel[vi], writes to advectionFwd
            drawPass(cb, m_advectionFwdRT.get(), m_passAdvection[vi], s, s, nullptr);
            break;

        case 2: // Advect reverse — reads advectionFwd, writes to advectionRev (direction=-1)
            drawPass(cb, m_advectionRevRT.get(), m_passAdvectionRev, s, s, nullptr);
            break;

        case 3: // Adjust advection (MacCormack) — reads fwd+rev+vel[vi], writes to vel[1-vi]
            drawPass(cb, m_velRT[1 - vi].get(), m_passAdjust[vi], s, s, nullptr);
            vi = 1 - vi;
            break;

        case 4: case 5: case 6: // Diffuse ×3 — reads vel[vi]
            drawPass(cb, m_velRT[1 - vi].get(), m_passDiffuse[vi], s, s, nullptr);
            vi = 1 - vi;
            break;

        case 7: // Inject noise — reads noiseTex + vel[vi]
            drawPass(cb, m_velRT[1 - vi].get(), m_passInjectNoise[vi], s, s, nullptr);
            vi = 1 - vi;
            break;

        case 8: // Divergence — reads vel[vi], writes to divergenceTex
            drawPass(cb, m_divergenceRT.get(), m_passDivergence[vi], s, s, nullptr);
            break;

        default: // Pressure ×19 (ph 9-27)
            if (ph >= 9 && ph <= 27) {
                drawPass(cb, m_pressureRT[1 - pi].get(), m_passPressure[pi], s, s, nullptr);
                pi = 1 - pi;
                break;
            }
            if (ph == 28) { // Subtract gradient — reads press[pi] + vel[vi]
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
        case 5: // Test — instancing colored quads
            fprintf(stderr, "  MODE 5: drawing test quads\n");
        {
            cb->beginPass(m_displayRT.get(), QColor(0, 0, 0, 0), QRhiDepthStencilClearValue{1.0f, 0}, nullptr);
            cb->setGraphicsPipeline(m_passTestInstancing.pipeline.get());
            cb->setShaderResources(m_passTestInstancing.srb.get());
            cb->setViewport(QRhiViewport(0, 0, float(ds), float(ds)));
            QRhiCommandBuffer::VertexInput vbindings[2] = {
                { m_testQuadVbuf.get(), 0 },
                { m_testInstanceBuf.get(), 0 },
            };
            cb->setVertexInput(0, 2, vbindings);
            cb->draw(6, 4);
            cb->endPass();
        }
            break;
        }
        if (m_debugMode != 5) {
            drawPass(cb, m_displayRT.get(), pipeline, srb.get(), ds, ds, nullptr);
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

void FluidSimEngine::testComputeAndSSBO()
{
    if (!m_rhi->isFeatureSupported(QRhi::Compute)) {
        fprintf(stderr, "  Compute NOT supported by backend\n");
        return;
    }
    // Verify compute pipeline + imageStore works by writing gradient to 4x4 RGBA8
    // and reading back. This validates QRhiComputePipeline + UsedWithLoadStore.
    auto imgTex = std::unique_ptr<QRhiTexture>(
        m_rhi->newTexture(QRhiTexture::RGBA8, {4, 4}, 1,
            QRhiTexture::UsedWithLoadStore | QRhiTexture::UsedAsTransferSource));
    if (!imgTex->create()) return;
    QShader cs = FluidSimShaders::loadShader("test_compute");
    if (!cs.isValid()) return;
    auto computeSrb = std::unique_ptr<QRhiShaderResourceBindings>(
        m_rhi->newShaderResourceBindings());
    QVarLengthArray<QRhiShaderResourceBinding, 1> b;
    b.append(QRhiShaderResourceBinding::imageStore(0,
        QRhiShaderResourceBinding::ComputeStage, imgTex.get(), 0));
    computeSrb->setBindings(b.begin(), b.end());
    if (!computeSrb->create()) return;
    auto cp = std::unique_ptr<QRhiComputePipeline>(m_rhi->newComputePipeline());
    cp->setShaderStage(QRhiShaderStage(QRhiShaderStage::Compute, cs));
    cp->setShaderResourceBindings(computeSrb.get());
    if (!cp->create()) return;
    QRhiCommandBuffer *cb = nullptr;
    if (m_rhi->beginOffscreenFrame(&cb) != QRhi::FrameOpSuccess) return;
    cb->beginComputePass();
    cb->setComputePipeline(cp.get());
    cb->setShaderResources(computeSrb.get());
    cb->dispatch(4, 4, 1);
    cb->endComputePass();
    auto *result = new QRhiReadbackResult();
    bool readDone = false;
    result->completed = [&readDone]() { readDone = true; };
    QRhiResourceUpdateBatch *rub = m_rhi->nextResourceUpdateBatch();
    rub->readBackTexture(QRhiReadbackDescription(imgTex.get()), result);
    cb->resourceUpdate(rub);
    m_rhi->endOffscreenFrame();
    bool pass = false;
    if (readDone && result->data.size() >= 64) {
        pass = true;
        const uint8_t *d = (const uint8_t *)result->data.constData();
        for (int y = 0; y < 4 && pass; y++)
            for (int x = 0; x < 4 && pass; x++) {
                int off = (y * 4 + x) * 4;
                if (d[off] != x || d[off+1] != y || d[off+2] != 0 || d[off+3] != 255)
                    pass = false;
            }
    }
    fprintf(stderr, "  Compute + imageStore: %s\n", pass ? "PASSED ✅" : "FAILED ❌");
    delete result;
}
