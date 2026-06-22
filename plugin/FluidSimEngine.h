#pragma once
#include <QObject>
#include <memory>
#include <QtGui/rhi/qrhi.h>
#include <QtGui/private/qrhi_p.h>

class FluidSimEngine : public QObject {
    Q_OBJECT
public:
    explicit FluidSimEngine(QObject *parent = nullptr);
    ~FluidSimEngine();

    void init(QRhi *rhi, int fluidSize = 128);
    void step(QRhiCommandBuffer *cb, float dt);
    void releaseResources();

    QRhi *rhi() const { return m_rhi; }
    int frameCount() const { return m_frameCount; }
    int fluidSize() const { return m_fluidSize; }
    bool isInitialized() const { return m_initialized; }

    QRhiTexture *displayTex() const { return m_displayTex.get(); }
    QRhiSampler *nearestSampler() const { return m_nearestSampler.get(); }
    QRhiBuffer *quadVertexBuffer() const { return m_quadVertexBuf.get(); }

    void setDebugMode(int mode) { m_debugMode = mode; }

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
    void updateNoiseChannels(float dt);

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

    // Display texture (256x256 RGBA8) + pipeline
    std::unique_ptr<QRhiTexture> m_displayTex;
    std::unique_ptr<QRhiTextureRenderTarget> m_displayRT;
    std::unique_ptr<QRhiRenderPassDescriptor> m_rpDescRGBA8;
    PassPipeline m_passDisplay;      // heatmap (velocity → color)
    PassPipeline m_passDebug;        // bias+contrast (raw texture)

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

    // Texture refs
    QRhiTexture *curVelTex() const { return m_velocityTex[m_velocityIndex].get(); }
    QRhiTexture *nxtVelTex() const { return m_velocityTex[1 - m_velocityIndex].get(); }
    QRhiTexture *curPressTex() const { return m_pressureTex[m_pressureIndex].get(); }
    QRhiTexture *nxtPressTex() const { return m_pressureTex[1 - m_pressureIndex].get(); }

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
    std::unique_ptr<QRhiBuffer> m_directionBuf;       // Direction (float, 16 bytes std140)
    std::unique_ptr<QRhiBuffer> m_noiseUniformBuf;    // NoiseUniforms (multiplier, 16 bytes std140)
    std::unique_ptr<QRhiBuffer> m_noiseChannelBuf;    // NoiseChannel[3] (std430)
    std::unique_ptr<QRhiBuffer> m_pushConstantBuf;    // timestep (16 bytes std140)

    // Pending resource upload batches
    QRhiResourceUpdateBatch *m_pendingUploadBatch = nullptr;
    QRhiResourceUpdateBatch *m_pendingDisplayUploadBatch = nullptr;
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
    int m_diffusionIterations = 3;
    int m_pressureIterations = 19;
    int m_stepPhase = 0;
    float m_noiseMultiplier = 0.45f;
    int m_debugMode = 0;

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
