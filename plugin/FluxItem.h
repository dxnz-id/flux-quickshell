#pragma once
#include <QQuickItem>
#include <QQuickWindow>
#include <QSGImageNode>
#include <QSGTexture>
#include <QtGui/rhi/qrhi.h>
#include <QElapsedTimer>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QRunnable>
#include <QThread>
#include <QMutex>
#include <QOpenGLFunctions>
#include <QCoreApplication>
#include <memory>
#include <atomic>

class FluxEngine;
class FluxItem;
class QTimer;

class EngineStepJob : public QRunnable {
public:
    EngineStepJob(FluxEngine *engine, QRhi *ourRhi,
                  QOpenGLContext *sharedCtx, QOffscreenSurface *fallbackSurface,
                  FluxItem *item, float dt)
        : m_engine(engine), m_ourRhi(ourRhi), m_sharedCtx(sharedCtx),
          m_fallbackSurface(fallbackSurface), m_item(item), m_dt(dt) {}
    void run() override;
private:
    FluxEngine *m_engine;
    QRhi *m_ourRhi;
    QOpenGLContext *m_sharedCtx;
    QOffscreenSurface *m_fallbackSurface;
    FluxItem *m_item;
    float m_dt;
};

class FluxItem : public QQuickItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool running READ isRunning WRITE setRunning NOTIFY runningChanged)
    Q_PROPERTY(int frameCount READ frameCount NOTIFY frameCountChanged)
    Q_PROPERTY(int simSize READ simSize WRITE setSimSize NOTIFY simSizeChanged)
    Q_PROPERTY(int debugMode READ debugMode WRITE setDebugMode NOTIFY debugModeChanged)

    Q_PROPERTY(int colorMode READ colorMode WRITE setColorMode NOTIFY colorModeChanged)
    Q_PROPERTY(float viscosity READ viscosity WRITE setViscosity NOTIFY viscosityChanged)
    Q_PROPERTY(float noiseMultiplier READ noiseMultiplier WRITE setNoiseMultiplier NOTIFY noiseMultiplierChanged)
    Q_PROPERTY(float timestep READ timestep WRITE setTimestep NOTIFY timestepChanged)
    Q_PROPERTY(float dissipation READ dissipation WRITE setDissipation NOTIFY dissipationChanged)
    Q_PROPERTY(int pressureIterations READ pressureIterations WRITE setPressureIterations NOTIFY pressureIterationsChanged)
    Q_PROPERTY(float lineVariance READ lineVariance WRITE setLineVariance NOTIFY lineVarianceChanged)
    Q_PROPERTY(float lineWidthMultiplier READ lineWidthMultiplier WRITE setLineWidthMultiplier NOTIFY lineWidthMultiplierChanged)
    Q_PROPERTY(float zoom READ zoom WRITE setZoom NOTIFY zoomChanged)
    Q_PROPERTY(int msaaSampleCount READ msaaSampleCount WRITE setMsaaSampleCount NOTIFY msaaSampleCountChanged)
    Q_PROPERTY(int diagStep READ diagStep WRITE setDiagStep NOTIFY diagStepChanged)
public:
    explicit FluxItem(QQuickItem *parent = nullptr);
    ~FluxItem();

    bool isRunning() const { return m_running; }
    void setRunning(bool r);
    int frameCount() const { return m_frameCount; }
    int simSize() const { return m_simSize; }
    void setSimSize(int s);
    int debugMode() const { return m_debugMode; }
    void setDebugMode(int mode);

    int colorMode() const { return m_colorMode; }
    void setColorMode(int v);
    float viscosity() const { return m_viscosity; }
    void setViscosity(float v);
    float noiseMultiplier() const { return m_noiseMultiplier; }
    void setNoiseMultiplier(float v);
    float timestep() const { return m_timestep; }
    void setTimestep(float v);
    float dissipation() const { return m_dissipation; }
    void setDissipation(float v);
    int pressureIterations() const { return m_pressureIterations; }
    void setPressureIterations(int v);
    float lineVariance() const { return m_lineVariance; }
    void setLineVariance(float v);
    float lineWidthMultiplier() const { return m_lineWidthMultiplier; }
    void setLineWidthMultiplier(float v);
    float zoom() const { return m_zoom; }
    void setZoom(float v);

    int msaaSampleCount() const { return m_msaaSamples; }
    void setMsaaSampleCount(int v);
    int diagStep() const { return m_diagStep; }
    void setDiagStep(int v);

    void storeReadback(const QByteArray &data);
    bool hasPendingReadback() const;
    void setReadbackPending(bool p);
    void decrementInflight() { m_inflightJobs.fetch_sub(1, std::memory_order_release); }
    bool isStopping() const { return m_stopping.load(std::memory_order_acquire); }
    bool appExiting() const { return m_appExiting.load(std::memory_order_acquire); }

signals:
    void runningChanged();
    void frameCountChanged(int count);
    void simSizeChanged();
    void debugModeChanged();
    void colorModeChanged();
    void viscosityChanged();
    void noiseMultiplierChanged();
    void timestepChanged();
    void dissipationChanged();
    void pressureIterationsChanged();
    void lineVarianceChanged();
    void lineWidthMultiplierChanged();
    void zoomChanged();
    void msaaSampleCountChanged();
    void diagStepChanged();

protected:
    QSGNode *updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *) override;
    void releaseResources() override;
    void itemChange(ItemChange change, const ItemChangeData &value) override;

private:
    void initOurRhi();
    void initEngine();
    void scheduleEngineStep();
    int computeDisplaySize(int w, int h);

public slots:
    void onFrameTick();

private:
    std::unique_ptr<FluxEngine> m_engine;
    bool m_running = false;
    int m_frameCount = 0;
    int m_simSize = 128;
    float m_pendingDt = 0.016f;
    int m_debugMode = 5;

    int m_colorMode = 0;
    float m_viscosity = 5.0f;
    float m_noiseMultiplier = 0.45f;
    float m_timestep = 1.0f / 60.0f;
    float m_dissipation = 0.0f;
    int m_pressureIterations = 19;
    float m_lineVariance = 0.55f;
    float m_lineWidthMultiplier = 1.0f;
    float m_zoom = 1.6f;
    int m_msaaSamples = 4;

    std::unique_ptr<QOffscreenSurface> m_fallbackSurface;
    std::unique_ptr<QRhi> m_ourRhi;
    QOpenGLContext *m_sharedCtx = nullptr;

    // Readback display pipeline (only option that works with separate QRhi)
    QMutex m_readbackMutex;
    QByteArray m_readbackData;
    std::atomic<bool> m_readbackPending{false};

    QElapsedTimer m_elapsed;
    qint64 m_lastTick = 0;

    // Resize tracking
    int m_lastItemW = 0, m_lastItemH = 0;

    std::atomic<int> m_inflightJobs{0};
    std::atomic<bool> m_stopping{false};
    std::atomic<bool> m_appExiting{false};
    int m_diagStep = 1;
    bool m_initQueued = false;
    bool m_engineInitQueued = false;
};
