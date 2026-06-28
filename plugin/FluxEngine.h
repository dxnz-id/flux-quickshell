#pragma once
#include <QObject>
#include <memory>
#include <QtGui/rhi/qrhi.h>
#include <QtGui/private/qrhi_p.h>

class FluxEngine : public QObject {
    Q_OBJECT
public:
    explicit FluxEngine(QObject *parent = nullptr);
    ~FluxEngine();

    void init(QRhi *rhi, int fluidSize = 128);
    void step(QRhiCommandBuffer *cb, float dt);
    void releaseResources();

    QRhi *rhi() const { return m_rhi; }
    int frameCount() const { return m_frameCount; }
    int fluidSize() const { return m_fluidSize; }
    bool isInitialized() const { return m_initialized; }

    QRhiTexture *displayTex() const { return m_displayTex.get(); }
    int displaySize() const { return m_displaySize; }
    void setMsaaSamples(int s) { if (m_msaaSamples != s) { m_msaaSamples = s; m_resizeNeeded = true; } }
    int msaaSamples() const { return m_msaaSamples; }
    QRhiSampler *nearestSampler() const { return m_nearestSampler.get(); }
    QRhiBuffer *quadVertexBuffer() const { return m_quadVertexBuf.get(); }

    void setDebugMode(int mode) { m_debugMode = mode; }

    // Configurable simulation parameters
    void setViscosity(float v) { m_viscosity = v; m_paramsDirty = true; }
    void setTimestep(float t) { m_fluidTimestep = t; m_paramsDirty = true; }
    void setDissipation(float d) { m_dissipation = d; m_paramsDirty = true; }
    void setNoiseMultiplier(float n) { m_noiseMultiplier = n; m_paramsDirty = true; }
    void setPressureIterations(int p) { m_pressureIterations = p; }
    void setColorMode(int m) { m_colorMode = float(m); }
    void setLineVariance(float v) { m_lineVariance = v; }
    void setLineWidthMultiplier(float m) { m_lineWidthMultiplier = m; }
    void setZoom(float z) { m_zoom = z; }

    void resizeDisplay(int logicalW, int logicalH, int displayTexSize);
    void checkResize();

signals:
    void frameCountChanged(int count);

private:
    void createTextures();
    void createSamplers();
    void createBuffers();
    void createGraphicsPipelines();
    void createDisplayPass();
    void createRenderTargets();
    void initNoiseChannels();
    void updateUniforms(float realDt);  // upload FluidUniforms + Direction + PushConstants to GPU
    void createLinePipelines();
    void recreateLineGraphicsPipelines();
    void stepLines(QRhiCommandBuffer *cb, float realDt);
    void initLineState();
    void initBasepoints();
    void updateNoiseChannels(float dt);
    void tickLineNoise(float dt);

    QRhiShaderResourceBindings *buildBinding(std::initializer_list<QRhiShaderResourceBinding> list);

    struct PassPipeline {
        std::unique_ptr<QRhiGraphicsPipeline> pipeline;
        std::unique_ptr<QRhiShaderResourceBindings> srb;
    };

    // Full-screen quad rendering
    std::unique_ptr<QRhiBuffer> m_quadVertexBuf;
    QShader m_quadVertexShader;

    // Per-pass graphics pipelines (fragment shader based)
    PassPipeline m_passNoise;
    PassPipeline m_passAdvection[2]; // [0]=forward (reads vel[0]), [1]=(reads vel[1])
    PassPipeline m_passAdvectionRev; // reverse (reads advectionFwd)
    PassPipeline m_passAdjust[2];    // indexed by vi (which velocity to read for MacCormack clamp)
    PassPipeline m_passDiffuse[2];   // indexed by vi
    PassPipeline m_passInjectNoise[2]; // indexed by vi
    PassPipeline m_passDivergence[2]; // indexed by vi (which velocity to read)
    PassPipeline m_passPressure[2];  // indexed by pi (which pressure to read)
    PassPipeline m_passSubtract[2][2]; // indexed by [vi][pi]

    // Logical window size (from FluxItem, for line_scale_factor match reference)
    int m_logicalW = 750, m_logicalH = 750;
    int m_desiredLogicalW = 750, m_desiredLogicalH = 750;
    int m_desiredDisplaySize = 512;
    bool m_resizeNeeded = false;

    // Display textures (512x512 RGBA8 by default)
    int m_displaySize = 512;
    std::unique_ptr<QRhiTexture> m_displayTex;       // resolve target (MSAA) or direct RT (no MSAA)
    std::unique_ptr<QRhiTexture> m_displayTexMS;     // MSAA render target (null when msaaSamples==1)
    std::unique_ptr<QRhiTextureRenderTarget> m_displayRT;
    std::unique_ptr<QRhiRenderPassDescriptor> m_rpDescRGBA8;
    int m_msaaSamples = 4;
    PassPipeline m_passDisplay;      // heatmap (velocity → color)
    PassPipeline m_passDebug;        // bias+contrast (raw texture)

    // Line rendering (spring dynamics particles)
    int m_lineGridCols = 0;
    int m_lineGridRows = 0;
    int m_lineCount = 0;
    std::unique_ptr<QRhiTexture> m_lineStateTex[2];  // RGBA32F ping-pong (lineCount*3 texels, tiled 256×H)
    std::unique_ptr<QRhiBuffer> m_lineVertexBuf;      // 6 vec2 quad vertices (LINE_VERTICES)
    std::unique_ptr<QRhiBuffer> m_lineBasepointBuf;   // vec2[lineCount] PerInstance
    std::unique_ptr<QRhiComputePipeline> m_lineUpdatePipeline;
    std::unique_ptr<QRhiGraphicsPipeline> m_lineDrawPipeline;
    std::unique_ptr<QRhiGraphicsPipeline> m_lineEndpointPipeline;
    std::unique_ptr<QRhiShaderResourceBindings> m_lineFrameComputeSrb;  // rebuilt each frame in stepLines
    std::unique_ptr<QRhiShaderResourceBindings> m_lineFrameDrawSrb;     // rebuilt each frame in stepLines
    std::unique_ptr<QRhiShaderResourceBindings> m_lineFrameEndpointSrb; // rebuilt each frame in stepLines
    std::unique_ptr<QRhiTexture> m_lineColorTex;       // 256x1 RGBA8 gradient for ImageTexture mode
    std::unique_ptr<QRhiSampler> m_lineColorSampler;   // linear sampler for color tex
    int m_lineStateReadIdx = 0;
    bool m_linePipelineReady = false;
    float m_lineNoiseOffset1 = 0.0f;
    float m_lineNoiseOffset2 = 0.0f;
    float m_lineNoiseBlendFactor = 0.0f;

    // Render targets for solver passes (one per writable texture)
    std::unique_ptr<QRhiTextureRenderTarget> m_velRT[2];
    std::unique_ptr<QRhiTextureRenderTarget> m_pressureRT[2];
    std::unique_ptr<QRhiTextureRenderTarget> m_advectionFwdRT;
    std::unique_ptr<QRhiTextureRenderTarget> m_advectionRevRT;
    std::unique_ptr<QRhiTextureRenderTarget> m_divergenceRT;
    std::unique_ptr<QRhiTextureRenderTarget> m_noiseRT;

    // Shared render pass descriptors (one per format)
    std::unique_ptr<QRhiRenderPassDescriptor> m_rpDescRGBA16F;
    std::unique_ptr<QRhiRenderPassDescriptor> m_rpDescR32F;

    // Draw a full-screen quad into the given render target using the given pass
    void drawPass(QRhiCommandBuffer *cb, QRhiTextureRenderTarget *rt, PassPipeline &pass,
                  int width, int height, QRhiResourceUpdateBatch *ub = nullptr);
    void drawPass(QRhiCommandBuffer *cb, QRhiTextureRenderTarget *rt,
                  QRhiGraphicsPipeline *pipeline, QRhiShaderResourceBindings *srb,
                  int width, int height, QRhiResourceUpdateBatch *ub = nullptr);

    QRhi *m_rhi = nullptr;
    int m_fluidSize = 128;
    bool m_initialized = false;

    // Textures
    std::unique_ptr<QRhiTexture> m_velocityTex[2];     // RGBA16F
    std::unique_ptr<QRhiTexture> m_pressureTex[2];      // R32F
    std::unique_ptr<QRhiTexture> m_noiseTex;             // RGBA16F, 2x fluidSize
    std::unique_ptr<QRhiTexture> m_advectionFwdTex;      // RGBA16F
    std::unique_ptr<QRhiTexture> m_advectionRevTex;      // RGBA16F
    std::unique_ptr<QRhiTexture> m_divergenceTex;        // R32F

    // Samplers
    std::unique_ptr<QRhiSampler> m_linearSampler;
    std::unique_ptr<QRhiSampler> m_nearestSampler;

    // Uniform buffers
    std::unique_ptr<QRhiBuffer> m_fluidUniformBuf;   // FluidUniforms (std140, 32 bytes)
    std::unique_ptr<QRhiBuffer> m_gpuNoiseBuf;        // GpuNoiseParams (80 bytes std140)
    std::unique_ptr<QRhiBuffer> m_lineUniformBuf;     // LineUniforms (std140, 80 bytes)

    // Pending resource upload batches
    QRhiResourceUpdateBatch *m_pendingUploadBatch = nullptr;
    QRhiResourceUpdateBatch *m_pendingQuadUploadBatch = nullptr;

    // Simulation state
    int m_velocityIndex = 0;
    int m_pressureIndex = 0;
    int m_frameCount = 0;
    float m_elapsedTime = 0.0f;

    // Settings
    float m_viscosity = 5.0f;
    float m_fluidTimestep = 1.0f / 60.0f;
    float m_dissipation = 0.0f;
    int m_pressureIterations = 19;
    int m_stepPhase = 0;
    float m_noiseMultiplier = 0.45f;
    int m_debugMode = 5;  // Lines mode

    // Exposed QML settings
    float m_colorMode = 0.0f;
    float m_lineVariance = 0.55f;
    float m_lineWidthMultiplier = 1.0f;
    float m_zoom = 1.6f;
    bool m_paramsDirty = false;

    // Noise channel state
    static constexpr int NUM_CHANNELS = 3;
    struct alignas(16) NoiseChannel {
        float scale[2];
        float offset_1;
        float offset_2;
        float blend_factor;
        float multiplier;
        float _padding[2];
    };
    NoiseChannel m_channels[NUM_CHANNELS];
};
