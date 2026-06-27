#include "FluxEngine.h"
#include "FluxShaders.h"
#include <QColor>
#include <QtGui/private/qrhi_p.h>
#include <cmath>
#include <cstring>

#pragma pack(push, 1)

struct FluidUniforms {
    float timestep;
    float dissipation;
    float alpha;
    float r_beta;
    float center_factor;
    float stencil_factor;
    float noise_multiplier;
    float _pad;
};
static_assert(sizeof(FluidUniforms) == 32, "FluidUniforms must be 32 bytes");


struct GpuNoiseParams {
    float elapsedTime;
    float noiseMultiplier;
    float _pad0[2];
    float ch0_off1, ch0_off2, ch0_blend, _pad1;
    float ch1_off1, ch1_off2, ch1_blend, _pad2;
    float ch2_off1, ch2_off2, ch2_blend, _pad3;
    float noiseSize[2];
    float _pad4[2];
};
static_assert(sizeof(GpuNoiseParams) == 80, "GpuNoiseParams must be 80 bytes");

struct LineUniforms {
    float aspect;
    float zoom;
    float line_width;
    float line_length;
    float line_begin_offset;
    float line_variance;
    float delta_time;
    float grid_cols;     // fcols = floor(logicalW / 15)
    float grid_rows;     // frows = floor(logicalH / logicalW * fcols)
    float grid_spacing_x; // 1.0 / fcols
    float grid_spacing_y; // 1.0 / frows
    float noise_scale_x;
    float noise_scale_y;
    float noise_offset_1;
    float noise_offset_2;
    float noise_blend_factor;
    float color_mode;
    float _pad0[3];
};
static_assert(sizeof(LineUniforms) == 80, "LineUniforms must be 80 bytes");
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

FluxEngine::FluxEngine(QObject *parent)
    : QObject(parent)
{
    initNoiseChannels();
}

FluxEngine::~FluxEngine()
{
    releaseResources();
}

void FluxEngine::initNoiseChannels()
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

void FluxEngine::releaseResources()
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
    m_gpuNoiseBuf.reset();
    m_lineUniformBuf.reset();

    m_quadVertexBuf.reset();

    m_lineFrameComputeSrb.reset();
    m_lineFrameDrawSrb.reset();
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
    m_displayTex.reset();
    m_lineStateTex[0].reset(); m_lineStateTex[1].reset();
    m_lineVertexBuf.reset();

    m_lineUpdatePipeline.reset();
    m_lineDrawPipeline.reset();
    m_lineBasepointBuf.reset();
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

void FluxEngine::init(QRhi *rhi, int fluidSize)
{
    m_rhi = rhi;
    m_fluidSize = fluidSize;

    fprintf(stderr, "FluxEngine::init: rhi=%p backend=%d\n",
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

    createLinePipelines();

    m_initialized = true;
    fprintf(stderr, "FluxEngine::init DONE\n");
}

void FluxEngine::createTextures()
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

void FluxEngine::createSamplers()
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

void FluxEngine::createBuffers()
{
    auto makeBuf = [&](const char *name, int size, QRhiBuffer::UsageFlags usage) {
        auto b = std::unique_ptr<QRhiBuffer>(m_rhi->newBuffer(
            QRhiBuffer::Dynamic, usage, size));
        b->setName(name);
        b->create();
        return b;
    };

    m_fluidUniformBuf = makeBuf("fluidUniforms", (int)sizeof(FluidUniforms), QRhiBuffer::UniformBuffer);
    m_gpuNoiseBuf = makeBuf("gpuNoise", (int)sizeof(GpuNoiseParams), QRhiBuffer::UniformBuffer);
    m_lineUniformBuf = makeBuf("lineUniforms", (int)sizeof(LineUniforms), QRhiBuffer::UniformBuffer);

    fprintf(stderr, "  params: dt=%.6f viscosity=%.1f size=%d\n", m_fluidTimestep, m_viscosity, m_fluidSize);
    fprintf(stderr, "  params: center=%.6f stencil=%.6f\n",
        1.0f / (m_viscosity * m_fluidTimestep),
        1.0f / (4.0f + 1.0f / (m_viscosity * m_fluidTimestep)));
}

QRhiShaderResourceBindings *FluxEngine::buildBinding(
    std::initializer_list<QRhiShaderResourceBinding> list)
{
    auto srb = std::unique_ptr<QRhiShaderResourceBindings>(m_rhi->newShaderResourceBindings());
    srb->setBindings(list);
    srb->create();
    return srb.release();
}

void FluxEngine::updateUniforms()
{
    float dt = m_fluidTimestep;
    float centerFactor = 1.0f / (m_viscosity * dt);
    float stencilFactor = 1.0f / (4.0f + centerFactor);
    FluidUniforms fu = { dt, m_dissipation, -1.0f, 0.25f, centerFactor, stencilFactor, m_noiseMultiplier, 0.0f };

    QRhiResourceUpdateBatch *ub = m_rhi->nextResourceUpdateBatch();
    ub->uploadStaticBuffer(m_fluidUniformBuf.get(), QByteArray((const char*)&fu, sizeof(fu)));
    // Keep batch for next beginPass
    if (m_pendingUploadBatch) m_pendingUploadBatch->release();
    m_pendingUploadBatch = ub;
    m_paramsDirty = false;
}

void FluxEngine::createRenderTargets()
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

void FluxEngine::createGraphicsPipelines()
{
    QRhiSampler *nearest = m_nearestSampler.get();
    QRhiSampler *linear = m_linearSampler.get();
    QRhiBuffer *fluidUBuf = m_fluidUniformBuf.get();

    QShader quadVs = FluxShaders::loadShader("fullscreen_quad");
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
        QShader fs = FluxShaders::loadShader(fragName);
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

    // Noise (procedural, reads UBO for animated params + noiseMultiplier, writes to noiseTex)
    m_passNoise.srb.reset(buildBinding({
        QRhiShaderResourceBinding::uniformBuffer(0, QRhiShaderResourceBinding::FragmentStage, m_gpuNoiseBuf.get()),
    }));
    makePipeline("pass_noise", m_passNoise.srb.get(), m_rpDescRGBA16F.get(), m_passNoise.pipeline);

    // Advection [0] reads vel[0] + fluid uniforms
    m_passAdvection[0].srb.reset(buildBinding({
        QRhiShaderResourceBinding::sampledTexture(0, QRhiShaderResourceBinding::FragmentStage, m_velocityTex[0].get(), linear),
        QRhiShaderResourceBinding::uniformBuffer(8, QRhiShaderResourceBinding::FragmentStage, m_fluidUniformBuf.get()),
    }));
    makePipeline("pass_advect", m_passAdvection[0].srb.get(), m_rpDescRGBA16F.get(), m_passAdvection[0].pipeline);

    // Advection [1] reads vel[1]
    m_passAdvection[1].srb.reset(buildBinding({
        QRhiShaderResourceBinding::sampledTexture(0, QRhiShaderResourceBinding::FragmentStage, m_velocityTex[1].get(), linear),
        QRhiShaderResourceBinding::uniformBuffer(8, QRhiShaderResourceBinding::FragmentStage, m_fluidUniformBuf.get()),
    }));
    makePipeline("pass_advect", m_passAdvection[1].srb.get(), m_rpDescRGBA16F.get(), m_passAdvection[1].pipeline);

    // Advection reverse (reads advectionFwd, fluid uniforms)
    m_passAdvectionRev.srb.reset(buildBinding({
        QRhiShaderResourceBinding::sampledTexture(0, QRhiShaderResourceBinding::FragmentStage, m_advectionFwdTex.get(), linear),
        QRhiShaderResourceBinding::uniformBuffer(8, QRhiShaderResourceBinding::FragmentStage, m_fluidUniformBuf.get()),
    }));
    makePipeline("pass_advect_rev", m_passAdvectionRev.srb.get(), m_rpDescRGBA16F.get(), m_passAdvectionRev.pipeline);

    // Adjust [vi] reads forwardTex, reverseTex, vel[vi] + fluid uniforms
    for (int i = 0; i < 2; i++) {
        m_passAdjust[i].srb.reset(buildBinding({
            QRhiShaderResourceBinding::sampledTexture(0, QRhiShaderResourceBinding::FragmentStage, m_advectionFwdTex.get(), nearest),
            QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage, m_advectionRevTex.get(), nearest),
            QRhiShaderResourceBinding::sampledTexture(2, QRhiShaderResourceBinding::FragmentStage, m_velocityTex[i].get(), nearest),
            QRhiShaderResourceBinding::uniformBuffer(8, QRhiShaderResourceBinding::FragmentStage, m_fluidUniformBuf.get()),
        }));
        makePipeline("pass_adjust", m_passAdjust[i].srb.get(), m_rpDescRGBA16F.get(), m_passAdjust[i].pipeline);
    }

    // Diffuse [vi] reads vel[vi] + fluid uniforms
    for (int i = 0; i < 2; i++) {
        m_passDiffuse[i].srb.reset(buildBinding({
            QRhiShaderResourceBinding::sampledTexture(0, QRhiShaderResourceBinding::FragmentStage, m_velocityTex[i].get(), nearest),
            QRhiShaderResourceBinding::uniformBuffer(8, QRhiShaderResourceBinding::FragmentStage, m_fluidUniformBuf.get()),
        }));
        makePipeline("pass_diffuse", m_passDiffuse[i].srb.get(), m_rpDescRGBA16F.get(), m_passDiffuse[i].pipeline);
    }

    // Inject noise [vi] reads noiseTex + vel[vi] + fluid uniforms
    for (int i = 0; i < 2; i++) {
        m_passInjectNoise[i].srb.reset(buildBinding({
            QRhiShaderResourceBinding::sampledTexture(0, QRhiShaderResourceBinding::FragmentStage, m_noiseTex.get(), linear),
            QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage, m_velocityTex[i].get(), nearest),
            QRhiShaderResourceBinding::uniformBuffer(8, QRhiShaderResourceBinding::FragmentStage, m_fluidUniformBuf.get()),
        }));
        makePipeline("pass_inject_noise", m_passInjectNoise[i].srb.get(), m_rpDescRGBA16F.get(), m_passInjectNoise[i].pipeline);
    }

    // Divergence [vi] reads vel[vi] + fluid uniforms
    for (int i = 0; i < 2; i++) {
        m_passDivergence[i].srb.reset(buildBinding({
            QRhiShaderResourceBinding::sampledTexture(0, QRhiShaderResourceBinding::FragmentStage, m_velocityTex[i].get(), nearest),
            QRhiShaderResourceBinding::uniformBuffer(8, QRhiShaderResourceBinding::FragmentStage, m_fluidUniformBuf.get()),
        }));
        makePipeline("pass_divergence", m_passDivergence[i].srb.get(), m_rpDescR32F.get(), m_passDivergence[i].pipeline);
    }

    // Pressure [pi] reads divergenceTex + pressureTex[pi] + fluid uniforms
    for (int i = 0; i < 2; i++) {
        m_passPressure[i].srb.reset(buildBinding({
            QRhiShaderResourceBinding::sampledTexture(0, QRhiShaderResourceBinding::FragmentStage, m_divergenceTex.get(), nearest),
            QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage, m_pressureTex[i].get(), nearest),
            QRhiShaderResourceBinding::uniformBuffer(8, QRhiShaderResourceBinding::FragmentStage, m_fluidUniformBuf.get()),
        }));
        makePipeline("pass_pressure", m_passPressure[i].srb.get(), m_rpDescR32F.get(), m_passPressure[i].pipeline);
    }

    // Subtract [vi][pi] reads pressureTex[pi] + vel[vi] + fluid uniforms
    for (int vi = 0; vi < 2; vi++) {
        for (int pi = 0; pi < 2; pi++) {
            m_passSubtract[vi][pi].srb.reset(buildBinding({
                QRhiShaderResourceBinding::sampledTexture(0, QRhiShaderResourceBinding::FragmentStage, m_pressureTex[pi].get(), linear),
                QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage, m_velocityTex[vi].get(), nearest),
                QRhiShaderResourceBinding::uniformBuffer(8, QRhiShaderResourceBinding::FragmentStage, m_fluidUniformBuf.get()),
            }));
            makePipeline("pass_subtract", m_passSubtract[vi][pi].srb.get(), m_rpDescRGBA16F.get(), m_passSubtract[vi][pi].pipeline);
        }
    }

    // Dummy (checkerboard)
    // -- No more dummy pipelines needed

    fprintf(stderr, "  All solver pipelines created\n");
}

void FluxEngine::createDisplayPass()
{
    int ds = m_displaySize;
    int ms = m_msaaSamples;

    if (ms > 1) {
        // MSAA: MSAA render target + resolve texture
        m_displayTexMS.reset(m_rhi->newTexture(
            QRhiTexture::RGBA8, {ds, ds}, ms, QRhiTexture::RenderTarget));
        m_displayTexMS->setName("displayTexMS");
        m_displayTexMS->create();

        m_displayTex.reset(m_rhi->newTexture(
            QRhiTexture::RGBA8, {ds, ds}, 1,
            QRhiTexture::RenderTarget | QRhiTexture::UsedAsTransferSource));
        m_displayTex->setName("displayTexResolve");
        m_displayTex->create();

        QRhiColorAttachment colorAtt(m_displayTexMS.get());
        colorAtt.setResolveTexture(m_displayTex.get());
        m_displayRT.reset(m_rhi->newTextureRenderTarget({colorAtt}));
    } else {
        // No MSAA: single render target (existing behavior)
        m_displayTexMS.reset();
        m_displayTex.reset(m_rhi->newTexture(
            QRhiTexture::RGBA8, {ds, ds}, 1, QRhiTexture::RenderTarget));
        m_displayTex->setName("displayTex");
        m_displayTex->create();
        m_displayRT.reset(m_rhi->newTextureRenderTarget({m_displayTex.get()}));
    }

    m_rpDescRGBA8.reset(m_displayRT->newCompatibleRenderPassDescriptor());
    m_rpDescRGBA8->setName("rpRGBA8");
    m_displayRT->setRenderPassDescriptor(m_rpDescRGBA8.get());
    m_displayRT->setName("displayRT");
    m_displayRT->create();

    auto makeDisplayPipeline = [&](const QString &fragName, PassPipeline &out) {
        QShader vs = FluxShaders::loadShader("fullscreen_quad");
        QShader fs = FluxShaders::loadShader(fragName);
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

    fprintf(stderr, "  Display passes created (%dx%d RGBA8, MSAA=%dx)\n", ds, ds, ms);
}

static constexpr int PASSES_PER_FRAME = 32;

void FluxEngine::step(QRhiCommandBuffer *cb, float dt)
{
    if (!m_initialized) return;

    // Upload dirty uniforms before any draw call this frame
    if (m_paramsDirty)
        updateUniforms();

    // Collect all pending uploads to process at first beginPass
    for (auto *batch : {m_pendingUploadBatch, m_pendingQuadUploadBatch}) {
        if (batch) {
            cb->resourceUpdate(batch);
        }
    }
    m_pendingUploadBatch = nullptr;
    m_pendingQuadUploadBatch = nullptr;

    int s = m_fluidSize;
    int pressureStart = 9;
    int pressureEnd = pressureStart + m_pressureIterations - 1;
    int subtractPhase = pressureEnd + 1;
    int totalPhases = subtractPhase + 1;

    // GPU noise generation at frame start
    if (m_stepPhase == 0) {
        updateNoiseChannels(dt);
        GpuNoiseParams gp;
        memset(&gp, 0, sizeof(gp));
        gp.elapsedTime = m_elapsedTime;
        gp.noiseMultiplier = m_noiseMultiplier;
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
    for (int done = 0; done < PASSES_PER_FRAME && ph < totalPhases; done++, ph++) {
        switch (ph) {
        case 0: // GPU noise — writes noiseTex
            drawPass(cb, m_noiseRT.get(), m_passNoise, noisePx.width(), noisePx.height(), nullptr);
            break;

        case 1: // Advect forward — reads vel[vi], writes to advectionFwd
            drawPass(cb, m_advectionFwdRT.get(), m_passAdvection[vi], s, s, nullptr);
            break;

        case 2: // Advect reverse — reads advectionFwd, writes to advectionRev
            drawPass(cb, m_advectionRevRT.get(), m_passAdvectionRev, s, s, nullptr);
            break;

        case 3: // Adjust advection (MacCormack)
            drawPass(cb, m_velRT[1 - vi].get(), m_passAdjust[vi], s, s, nullptr);
            vi = 1 - vi;
            break;

        case 4: case 5: case 6: // Diffuse ×3
            drawPass(cb, m_velRT[1 - vi].get(), m_passDiffuse[vi], s, s, nullptr);
            vi = 1 - vi;
            break;

        case 7: // Inject noise
            drawPass(cb, m_velRT[1 - vi].get(), m_passInjectNoise[vi], s, s, nullptr);
            vi = 1 - vi;
            break;

        case 8: // Divergence
            drawPass(cb, m_divergenceRT.get(), m_passDivergence[vi], s, s, nullptr);
            break;

        default:
            if (ph >= pressureStart && ph <= pressureEnd) {
                drawPass(cb, m_pressureRT[1 - pi].get(), m_passPressure[pi], s, s, nullptr);
                pi = 1 - pi;
                break;
            }
            if (ph == subtractPhase) {
                drawPass(cb, m_velRT[1 - vi].get(), m_passSubtract[vi][pi], s, s, nullptr);
                vi = 1 - vi;
                break;
            }
            break;
        }
    }

    m_velocityIndex = vi;
    m_pressureIndex = pi;

    if (ph >= totalPhases) {
        ph = 0;
        m_frameCount++;

        int ds = m_displaySize;

        // Run line update compute + render when mode is 5
        if (m_debugMode == 5) {
            tickLineNoise(dt);
            stepLines(cb);
        }

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
        case 5: // Lines rendering — draw via stepLines
            // stepLines handles its own render pass
            break;
        }
        if (m_debugMode != 5) {
            drawPass(cb, m_displayRT.get(), pipeline, srb.get(), ds, ds, nullptr);
        }
    }

}

void FluxEngine::updateNoiseChannels(float dt)
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

void FluxEngine::tickLineNoise(float dt)
{
    const float BLEND_THRESHOLD = 4.0f;
    const float BASE_OFFSET = 0.0015f;
    float perturb = 1.0f + 0.2f * sinf(0.010f * m_elapsedTime * 6.2831853f);
    float offset = BASE_OFFSET * perturb;
    m_lineNoiseOffset1 += offset;
    if (m_lineNoiseOffset1 > BLEND_THRESHOLD) {
        m_lineNoiseOffset2 += offset;
        m_lineNoiseBlendFactor += BASE_OFFSET;
    }
    if (m_lineNoiseBlendFactor > 1.0f) {
        m_lineNoiseOffset1 = m_lineNoiseOffset2;
        m_lineNoiseOffset2 = 0.0f;
        m_lineNoiseBlendFactor = 0.0f;
    }
}

void FluxEngine::drawPass(QRhiCommandBuffer *cb, QRhiTextureRenderTarget *rt,
                              PassPipeline &pass, int w, int h,
                              QRhiResourceUpdateBatch *ub)
{
    drawPass(cb, rt, pass.pipeline.get(), pass.srb.get(), w, h, ub);
}

void FluxEngine::drawPass(QRhiCommandBuffer *cb, QRhiTextureRenderTarget *rt,
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

void FluxEngine::createLinePipelines()
{
    if (!m_rhi->isFeatureSupported(QRhi::Compute)) {
        fprintf(stderr, "  Lines: Compute not supported, skipping\n");
        return;
    }

    // Grid from logical size (match grid.rs)
    int w = m_logicalW, h = m_logicalH;
    float fcols = floorf(float(w) / 15.0f);
    float frows = floorf(float(h) / float(w) * fcols);
    m_lineGridCols = int(fcols) + 1;
    m_lineGridRows = int(frows) + 1;
    m_lineCount = m_lineGridCols * m_lineGridRows;

    fprintf(stderr, "  Lines: grid %dx%d = %d\n",
        m_lineGridCols, m_lineGridRows, m_lineCount);

    // Create state textures + basepoints
    initLineState();
    initBasepoints();

    // Create line vertex buffer (6 vertices: 2 triangles forming a quad, never changes)
    const float verts[] = {
        -0.5f, 0.0f,   -0.5f, 1.0f,   0.5f, 1.0f,
        -0.5f, 0.0f,   0.5f, 1.0f,    0.5f, 0.0f,
    };
    m_lineVertexBuf.reset(m_rhi->newBuffer(
        QRhiBuffer::Immutable, QRhiBuffer::VertexBuffer, sizeof(verts)));
    m_lineVertexBuf->setName("lineQuads");
    m_lineVertexBuf->create();
    {
        QRhiCommandBuffer *cb = nullptr;
        if (m_rhi->beginOffscreenFrame(&cb) != QRhi::FrameOpSuccess) return;
        QRhiResourceUpdateBatch *ub = m_rhi->nextResourceUpdateBatch();
        ub->uploadStaticBuffer(m_lineVertexBuf.get(), QByteArray((const char*)verts, sizeof(verts)));
        cb->resourceUpdate(ub);
        m_rhi->endOffscreenFrame();
    }

    // Load shaders
    QShader updateCs = FluxShaders::loadShader("line_update");
    if (!updateCs.isValid()) { fprintf(stderr, "  Lines: line_update.comp FAILED\n"); return; }

    // --- Compute pipeline (line_update) ---
    {
        auto srb = std::unique_ptr<QRhiShaderResourceBindings>(
            m_rhi->newShaderResourceBindings());
        srb->setBindings({
            QRhiShaderResourceBinding::sampledTexture(0, QRhiShaderResourceBinding::ComputeStage,
                m_velocityTex[0].get(), m_linearSampler.get()),
            QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::ComputeStage,
                m_lineStateTex[0].get(), m_nearestSampler.get()),
            QRhiShaderResourceBinding::imageStore(2, QRhiShaderResourceBinding::ComputeStage,
                m_lineStateTex[1].get(), 0),
            QRhiShaderResourceBinding::uniformBuffer(3, QRhiShaderResourceBinding::ComputeStage,
                m_lineUniformBuf.get()),
        });
        srb->create();
        m_lineUpdatePipeline.reset(m_rhi->newComputePipeline());
        m_lineUpdatePipeline->setShaderStage(QRhiShaderStage(QRhiShaderStage::Compute, updateCs));
        m_lineUpdatePipeline->setShaderResourceBindings(srb.get());
        if (!m_lineUpdatePipeline->create()) {
            fprintf(stderr, "  Lines: compute pipeline FAILED\n");
            return;
        }
    }

    // --- Graphics pipelines (draw_lines + draw_endpoint) ---
    m_lineFrameDrawSrb.reset();
    m_lineFrameComputeSrb.reset();
    m_lineFrameEndpointSrb.reset();

    recreateLineGraphicsPipelines();

    // --- Color texture for ImageTexture mode (256x1 RGBA8 rainbow gradient) ---
    m_lineColorTex.reset(m_rhi->newTexture(
        QRhiTexture::RGBA8, {256, 1}, 1, QRhiTexture::UsedAsTransferSource));
    m_lineColorTex->setName("lineColorTex");
    m_lineColorTex->create();
    m_lineColorSampler.reset(m_rhi->newSampler(
        QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::None,
        QRhiSampler::Repeat, QRhiSampler::ClampToEdge));
    m_lineColorSampler->setName("lineColorSampler");
    m_lineColorSampler->create();
    {
        QByteArray colData(256 * 4, 0);
        uint8_t *cp = (uint8_t*)colData.data();
        for (int x = 0; x < 256; x++) {
            float t = float(x) / 255.0f;
            float r = 0.5f + 0.5f * sinf(t * 6.28318f + 0.0f);
            float g = 0.5f + 0.5f * sinf(t * 6.28318f + 2.094f);
            float b = 0.5f + 0.5f * sinf(t * 6.28318f + 4.188f);
            cp[x*4+0] = uint8_t(std::min(std::max(r * 255.0f, 0.0f), 255.0f));
            cp[x*4+1] = uint8_t(std::min(std::max(g * 255.0f, 0.0f), 255.0f));
            cp[x*4+2] = uint8_t(std::min(std::max(b * 255.0f, 0.0f), 255.0f));
            cp[x*4+3] = 255;
        }
        QRhiCommandBuffer *cb = nullptr;
        if (m_rhi->beginOffscreenFrame(&cb) != QRhi::FrameOpSuccess) { m_linePipelineReady = false; return; }
        QRhiResourceUpdateBatch *ub = m_rhi->nextResourceUpdateBatch();
        QRhiTextureSubresourceUploadDescription subdesc(colData, colData.size());
        ub->uploadTexture(m_lineColorTex.get(), QRhiTextureUploadDescription(QRhiTextureUploadEntry(0, 0, subdesc)));
        cb->resourceUpdate(ub);
        m_rhi->endOffscreenFrame();
    }

    m_linePipelineReady = true;

    fprintf(stderr, "  Lines: pipelines OK (%dx%d=%d lines)\n",
        m_lineGridCols, m_lineGridRows, m_lineCount);
}

void FluxEngine::recreateLineGraphicsPipelines()
{
    if (!m_linePipelineReady) return;

    QShader drawVs = FluxShaders::loadShader("draw_lines_vs");
    QShader drawFs = FluxShaders::loadShader("draw_lines_fs");
    QShader endpointVs = FluxShaders::loadShader("draw_endpoint_vs");
    QShader endpointFs = FluxShaders::loadShader("draw_endpoint_fs");

    // --- Draw pipeline ---
    m_lineDrawPipeline.reset();
    m_lineDrawPipeline.reset(m_rhi->newGraphicsPipeline());

    auto dummySrb = std::unique_ptr<QRhiShaderResourceBindings>(
        m_rhi->newShaderResourceBindings());
    dummySrb->setBindings({
        QRhiShaderResourceBinding::sampledTexture(0, QRhiShaderResourceBinding::VertexStage,
            m_velocityTex[0].get(), m_linearSampler.get()),
        QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::VertexStage,
            m_lineStateTex[0].get(), m_nearestSampler.get()),
        QRhiShaderResourceBinding::uniformBuffer(2, QRhiShaderResourceBinding::VertexStage,
            m_lineUniformBuf.get()),
    });
    dummySrb->create();

    m_lineDrawPipeline->setShaderStages({
        { QRhiShaderStage::Vertex, drawVs },
        { QRhiShaderStage::Fragment, drawFs } });
    m_lineDrawPipeline->setShaderResourceBindings(dummySrb.get());
    m_lineDrawPipeline->setRenderPassDescriptor(m_rpDescRGBA8.get());

    QRhiVertexInputLayout inputLayout;
    inputLayout.setBindings({
        QRhiVertexInputBinding(2 * sizeof(float)),
        QRhiVertexInputBinding(2 * sizeof(float), QRhiVertexInputBinding::PerInstance),
    });
    inputLayout.setAttributes({
        QRhiVertexInputAttribute(0, 0, QRhiVertexInputAttribute::Float2, 0),
        QRhiVertexInputAttribute(1, 1, QRhiVertexInputAttribute::Float2, 0),
    });
    m_lineDrawPipeline->setVertexInputLayout(inputLayout);

    m_lineDrawPipeline->setTopology(QRhiGraphicsPipeline::Triangles);

    QRhiGraphicsPipeline::TargetBlend blend;
    blend.enable = true;
    blend.srcColor = QRhiGraphicsPipeline::SrcAlpha;
    blend.dstColor = QRhiGraphicsPipeline::One;
    blend.srcAlpha = QRhiGraphicsPipeline::One;
    blend.dstAlpha = QRhiGraphicsPipeline::One;
    m_lineDrawPipeline->setTargetBlends({blend});

    m_lineDrawPipeline->create();

    // --- Endpoint pipeline ---
    m_lineEndpointPipeline.reset();
    m_lineEndpointPipeline.reset(m_rhi->newGraphicsPipeline());

    auto epDummySrb = std::unique_ptr<QRhiShaderResourceBindings>(
        m_rhi->newShaderResourceBindings());
    epDummySrb->setBindings({
        QRhiShaderResourceBinding::uniformBuffer(0, QRhiShaderResourceBinding::VertexStage,
            m_lineUniformBuf.get()),
        QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::VertexStage,
            m_lineStateTex[0].get(), m_nearestSampler.get()),
    });
    epDummySrb->create();

    m_lineEndpointPipeline->setShaderStages({
        { QRhiShaderStage::Vertex, endpointVs },
        { QRhiShaderStage::Fragment, endpointFs } });
    m_lineEndpointPipeline->setShaderResourceBindings(epDummySrb.get());
    m_lineEndpointPipeline->setRenderPassDescriptor(m_rpDescRGBA8.get());
    m_lineEndpointPipeline->setVertexInputLayout(inputLayout);
    m_lineEndpointPipeline->setTopology(QRhiGraphicsPipeline::Triangles);

    QRhiGraphicsPipeline::TargetBlend epBlend;
    epBlend.enable = true;
    epBlend.srcColor = QRhiGraphicsPipeline::SrcAlpha;
    epBlend.dstColor = QRhiGraphicsPipeline::One;
    epBlend.srcAlpha = QRhiGraphicsPipeline::One;
    epBlend.dstAlpha = QRhiGraphicsPipeline::One;
    m_lineEndpointPipeline->setTargetBlends({epBlend});

    m_lineEndpointPipeline->create();
}

void FluxEngine::initLineState()
{
    int stateTexels = m_lineCount * 3;
    int texW = 256;
    int texH = (stateTexels + texW - 1) / texW;

    // Create ping-pong state textures
    for (int i = 0; i < 2; i++) {
        char name[16]; snprintf(name, sizeof(name), "lineState%d", i);
        m_lineStateTex[i].reset(m_rhi->newTexture(
            QRhiTexture::RGBA32F, {texW, texH}, 1,
            QRhiTexture::UsedWithLoadStore | QRhiTexture::UsedAsTransferSource));
        m_lineStateTex[i]->setName(name);
        m_lineStateTex[i]->create();
    }

    // Upload initial state: endpoint=small random, velocity=0, color=(1,1,1,0), colorVel=0, width=0
    QRhiCommandBuffer *cb = nullptr;
    if (m_rhi->beginOffscreenFrame(&cb) != QRhi::FrameOpSuccess) return;
    QRhiResourceUpdateBatch *ub = m_rhi->nextResourceUpdateBatch();
    QByteArray initData(texW * texH * 16, 0);
    float *f = reinterpret_cast<float*>(initData.data());
    for (int i = 0; i < m_lineCount; i++) {
        int base = i * 12;
        int row = (i * 3) / texW;
        int col = (i * 3) % texW;
        int dataBase = (row * texW + col) * 4;
        float a = (float(rand()) / RAND_MAX) * 2.0f * 3.14159f;
        float r = (float(rand()) / RAND_MAX) * 0.03f;
        f[dataBase + 0] = cosf(a) * r;
        f[dataBase + 1] = sinf(a) * r;
        f[dataBase + 4] = 1.0f;
        f[dataBase + 5] = 1.0f;
        f[dataBase + 6] = 1.0f;
        f[dataBase + 7] = 0.0f;
    }
    int rowBytes = texW * 16;
    QRhiTextureSubresourceUploadDescription subdesc(initData, initData.size());
    subdesc.setDataStride(rowBytes);
    QRhiTextureUploadEntry entry(0, 0, subdesc);
    QRhiTextureUploadDescription desc = QRhiTextureUploadDescription(entry);
    ub->uploadTexture(m_lineStateTex[0].get(), desc);
    ub->uploadTexture(m_lineStateTex[1].get(), desc);
    cb->resourceUpdate(ub);
    m_rhi->endOffscreenFrame();
}

void FluxEngine::initBasepoints()
{
    float gsx = 1.0f / float(m_lineGridCols - 1);
    float gsy = 1.0f / float(m_lineGridRows - 1);
    QByteArray bpData(m_lineCount * 8, 0);
    float *bp = reinterpret_cast<float*>(bpData.data());
    for (int r = 0; r < m_lineGridRows; r++) {
        for (int c = 0; c < m_lineGridCols; c++) {
            int idx = r * m_lineGridCols + c;
            bp[idx * 2 + 0] = float(c) * gsx;
            bp[idx * 2 + 1] = float(r) * gsy;
        }
    }
    m_lineBasepointBuf.reset(m_rhi->newBuffer(
        QRhiBuffer::Immutable, QRhiBuffer::VertexBuffer, m_lineCount * 8));
    m_lineBasepointBuf->setName("lineBasepoints");
    m_lineBasepointBuf->create();
    QRhiCommandBuffer *cb = nullptr;
    if (m_rhi->beginOffscreenFrame(&cb) != QRhi::FrameOpSuccess) return;
    QRhiResourceUpdateBatch *ub = m_rhi->nextResourceUpdateBatch();
    ub->uploadStaticBuffer(m_lineBasepointBuf.get(), bpData);
    cb->resourceUpdate(ub);
    m_rhi->endOffscreenFrame();
}

void FluxEngine::resizeDisplay(int logicalW, int logicalH, int displayTexSize)
{
    m_desiredLogicalW = logicalW;
    m_desiredLogicalH = logicalH;
    m_desiredDisplaySize = displayTexSize;
    m_resizeNeeded = true;
}

void FluxEngine::checkResize()
{
    if (!m_resizeNeeded) return;

    // Release old display + line resources
    m_displayTexMS.reset();
    m_displayTex.reset();
    m_displayRT.reset();
    m_lineStateTex[0].reset();
    m_lineStateTex[1].reset();
    m_lineBasepointBuf.reset();

    // Apply new sizes
    m_displaySize = m_desiredDisplaySize;
    m_logicalW = m_desiredLogicalW;
    m_logicalH = m_desiredLogicalH;

    // Re-create display Tex + RT (same m_rpDescRGBA8, format unchanged)
    createDisplayPass();

    // Recalc grid from logical size (match grid.rs)
    float fcols = floorf(float(m_logicalW) / 15.0f);
    float frows = floorf(float(m_logicalH) / float(m_logicalW) * fcols);
    m_lineGridCols = int(fcols) + 1;
    m_lineGridRows = int(frows) + 1;
    m_lineCount = m_lineGridCols * m_lineGridRows;

    fprintf(stderr, "  RESIZE: display=%d logical=%dx%d grid=%dx%d lines=%d\n",
        m_displaySize, m_logicalW, m_logicalH, m_lineGridCols, m_lineGridRows, m_lineCount);

    // Re-create line state (zeroed, matching reference) + basepoints
    m_lineStateReadIdx = 0;
    initLineState();
    initBasepoints();

    // Recreate line graphics pipelines with current m_rpDescRGBA8 (MSAA may have changed)
    recreateLineGraphicsPipelines();

    // Reset velocity + pressure fields so lines fade in fresh (like first open)
    m_velocityIndex = 0;
    m_pressureIndex = 0;
    {
        QRhiCommandBuffer *cb = nullptr;
        if (m_rhi->beginOffscreenFrame(&cb) == QRhi::FrameOpSuccess) {
            QRhiResourceUpdateBatch *ub = m_rhi->nextResourceUpdateBatch();
            int s = m_fluidSize;
            QByteArray zeroVel(s * s * 8, 0);
            QRhiTextureSubresourceUploadDescription subVel(zeroVel, zeroVel.size());
            subVel.setDataStride(s * 8);
            ub->uploadTexture(m_velocityTex[0].get(), QRhiTextureUploadDescription(QRhiTextureUploadEntry(0, 0, subVel)));
            ub->uploadTexture(m_velocityTex[1].get(), QRhiTextureUploadDescription(QRhiTextureUploadEntry(0, 0, subVel)));
            QByteArray zeroPress(s * s * 4, 0);
            QRhiTextureSubresourceUploadDescription subPress(zeroPress, zeroPress.size());
            subPress.setDataStride(s * 4);
            ub->uploadTexture(m_pressureTex[0].get(), QRhiTextureUploadDescription(QRhiTextureUploadEntry(0, 0, subPress)));
            ub->uploadTexture(m_pressureTex[1].get(), QRhiTextureUploadDescription(QRhiTextureUploadEntry(0, 0, subPress)));
            cb->resourceUpdate(ub);
            m_rhi->endOffscreenFrame();
        }
    }

    m_resizeNeeded = false;
}

void FluxEngine::stepLines(QRhiCommandBuffer *cb)
{
    if (!m_lineUpdatePipeline || !m_linePipelineReady)
        return;

    int writeIdx = 1 - m_lineStateReadIdx;
    int lineCount = m_lineCount;
    int velIdx = m_velocityIndex;
    int ds = m_displaySize;

    // Upload line uniforms (match reference get_line_scale_factor + noise)
    float aspect = float(m_logicalW) / float(m_logicalH);
    float p = 1.0f / aspect;
    float screenSize = fmin((1.0f - p) * float(m_logicalW) + p * float(m_logicalH), 2000.0f);
    float scaleFactor = 1.0f / screenSize;
    LineUniforms lu;
    lu.aspect = aspect;
    lu.zoom = m_zoom;
    lu.line_width = m_zoom * m_lineWidthMultiplier * 9.0f * scaleFactor;
    lu.line_length = m_zoom * m_lineWidthMultiplier * 450.0f * scaleFactor;
    lu.line_begin_offset = 0.4f;
    lu.line_variance = m_lineVariance;
    lu.delta_time = m_fluidTimestep;
    float fcols = float(m_lineGridCols - 1);
    float frows = float(m_lineGridRows - 1);
    lu.grid_cols = fcols;
    lu.grid_rows = frows;
    lu.grid_spacing_x = 1.0f / fcols;
    lu.grid_spacing_y = 1.0f / frows;
    float sx = std::max(float(m_lineGridCols) / 171.0f, 1.0f);
    float sy = std::max(float(m_lineGridRows) / 171.0f, 1.0f);
    lu.noise_scale_x = 64.0f * sx;
    lu.noise_scale_y = 64.0f * sy;
    lu.noise_offset_1 = m_lineNoiseOffset1;
    lu.noise_offset_2 = m_lineNoiseOffset2;
    lu.noise_blend_factor = m_lineNoiseBlendFactor;
    lu.color_mode = m_colorMode;
    memset(lu._pad0, 0, sizeof(lu._pad0));
    QRhiResourceUpdateBatch *ub = m_rhi->nextResourceUpdateBatch();
    ub->uploadStaticBuffer(m_lineUniformBuf.get(), QByteArray((const char*)&lu, sizeof(lu)));
    cb->resourceUpdate(ub);

    // 1. Compute pass: update particle state
    // Build dynamic compute SRB with current velocity, state ping-pong, line UBO, and color tex
    m_lineFrameComputeSrb.reset(m_rhi->newShaderResourceBindings());
    m_lineFrameComputeSrb->setBindings({
        QRhiShaderResourceBinding::sampledTexture(0, QRhiShaderResourceBinding::ComputeStage,
            m_velocityTex[velIdx].get(), m_linearSampler.get()),
        QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::ComputeStage,
            m_lineStateTex[m_lineStateReadIdx].get(), m_nearestSampler.get()),
        QRhiShaderResourceBinding::imageStore(2, QRhiShaderResourceBinding::ComputeStage,
            m_lineStateTex[writeIdx].get(), 0),
        QRhiShaderResourceBinding::uniformBuffer(3, QRhiShaderResourceBinding::ComputeStage,
            m_lineUniformBuf.get()),
        QRhiShaderResourceBinding::sampledTexture(4, QRhiShaderResourceBinding::ComputeStage,
            m_lineColorTex.get(), m_lineColorSampler.get()),
    });
    m_lineFrameComputeSrb->create();

    cb->beginComputePass();
    cb->setComputePipeline(m_lineUpdatePipeline.get());
    cb->setShaderResources(m_lineFrameComputeSrb.get());
    int workGroups = (lineCount + 63) / 64;
    cb->dispatch(workGroups, 1, 1);
    cb->endComputePass();

    m_lineStateReadIdx = writeIdx;

    // 2. Render pass: draw instanced lines + endpoints on black
    cb->beginPass(m_displayRT.get(), QColor(0, 0, 0, 255), QRhiDepthStencilClearValue{1.0f, 0});
    cb->setViewport(QRhiViewport(0, 0, float(ds), float(ds)));

    // 2a. Draw lines
    cb->setGraphicsPipeline(m_lineDrawPipeline.get());
    m_lineFrameDrawSrb.reset(m_rhi->newShaderResourceBindings());
    m_lineFrameDrawSrb->setBindings({
        QRhiShaderResourceBinding::sampledTexture(0, QRhiShaderResourceBinding::VertexStage,
            m_velocityTex[velIdx].get(), m_linearSampler.get()),
        QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::VertexStage,
            m_lineStateTex[m_lineStateReadIdx].get(), m_nearestSampler.get()),
        QRhiShaderResourceBinding::uniformBuffer(2, QRhiShaderResourceBinding::VertexStage,
            m_lineUniformBuf.get()),
    });
    m_lineFrameDrawSrb->create();
    cb->setShaderResources(m_lineFrameDrawSrb.get());

    QRhiCommandBuffer::VertexInput vertBindings[2] = {
        { m_lineVertexBuf.get(), 0 },
        { m_lineBasepointBuf.get(), 0 },
    };
    cb->setVertexInput(0, 2, vertBindings);
    cb->draw(6, lineCount);

    // 2b. Draw endpoints
    cb->setGraphicsPipeline(m_lineEndpointPipeline.get());
    m_lineFrameEndpointSrb.reset(m_rhi->newShaderResourceBindings());
    m_lineFrameEndpointSrb->setBindings({
        QRhiShaderResourceBinding::uniformBuffer(0, QRhiShaderResourceBinding::VertexStage,
            m_lineUniformBuf.get()),
        QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::VertexStage,
            m_lineStateTex[m_lineStateReadIdx].get(), m_nearestSampler.get()),
    });
    m_lineFrameEndpointSrb->create();
    cb->setShaderResources(m_lineFrameEndpointSrb.get());
    cb->setVertexInput(0, 2, vertBindings);
    cb->draw(6, lineCount);

    cb->endPass();
}


