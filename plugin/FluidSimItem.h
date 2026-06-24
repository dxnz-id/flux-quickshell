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
#include <memory>

class FluidSimEngine;
class FluidSimItem;
class QTimer;

class EngineStepJob : public QRunnable {
public:
    EngineStepJob(FluidSimEngine *engine, QRhi *ourRhi,
                  QOpenGLContext *sharedCtx, QOffscreenSurface *fallbackSurface,
                  FluidSimItem *item, float dt)
        : m_engine(engine), m_ourRhi(ourRhi), m_sharedCtx(sharedCtx),
          m_fallbackSurface(fallbackSurface), m_item(item), m_dt(dt) {}
    void run() override;
private:
    FluidSimEngine *m_engine;
    QRhi *m_ourRhi;
    QOpenGLContext *m_sharedCtx;
    QOffscreenSurface *m_fallbackSurface;
    FluidSimItem *m_item;
    float m_dt;
};

class FluidSimItem : public QQuickItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool running READ isRunning WRITE setRunning NOTIFY runningChanged)
    Q_PROPERTY(int frameCount READ frameCount NOTIFY frameCountChanged)
    Q_PROPERTY(int simSize READ simSize WRITE setSimSize NOTIFY simSizeChanged)
    Q_PROPERTY(int debugMode READ debugMode WRITE setDebugMode NOTIFY debugModeChanged)
public:
    explicit FluidSimItem(QQuickItem *parent = nullptr);
    ~FluidSimItem();

    bool isRunning() const { return m_running; }
    void setRunning(bool r);
    int frameCount() const { return m_frameCount; }
    int simSize() const { return m_simSize; }
    void setSimSize(int s);
    int debugMode() const { return m_debugMode; }
    void setDebugMode(int mode);

    void storeReadback(const QByteArray &data);
    bool hasPendingReadback() const;
    void setReadbackPending(bool p);

signals:
    void runningChanged();
    void frameCountChanged(int count);
    void simSizeChanged();
    void debugModeChanged();

protected:
    QSGNode *updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *) override;
    void releaseResources() override;
    void itemChange(ItemChange change, const ItemChangeData &value) override;

private:
    void initOurRhi();
    void scheduleEngineStep();

private slots:
    void onFrameTick();

private:
    std::unique_ptr<FluidSimEngine> m_engine;
    std::unique_ptr<QTimer> m_timer;
    bool m_running = false;
    int m_frameCount = 0;
    int m_simSize = 128;
    float m_pendingDt = 0.016f;
    int m_debugMode = 0;

    std::unique_ptr<QOffscreenSurface> m_fallbackSurface;
    std::unique_ptr<QRhi> m_ourRhi;
    QOpenGLContext *m_sharedCtx = nullptr;

    // Readback display pipeline (only option that works with separate QRhi)
    QMutex m_readbackMutex;
    QByteArray m_readbackData;
    std::atomic<bool> m_readbackPending{false};

    QElapsedTimer m_elapsed;
    qint64 m_lastTick = 0;
};
