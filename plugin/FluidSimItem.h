#pragma once
#include <QQuickItem>
#include <QQuickWindow>
#include <QSGRenderNode>
#include <QSGGeometryNode>
#include <QSGOpaqueTextureMaterial>
#include <QSGTexture>
#include <QtGui/rhi/qrhi.h>
#include <QElapsedTimer>
#include <memory>

class FluidSimEngine;
class QTimer;

class FluidSimItem : public QQuickItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool running READ isRunning WRITE setRunning NOTIFY runningChanged)
    Q_PROPERTY(int frameCount READ frameCount NOTIFY frameCountChanged)
    Q_PROPERTY(int simSize READ simSize WRITE setSimSize NOTIFY simSizeChanged)
public:
    explicit FluidSimItem(QQuickItem *parent = nullptr);
    ~FluidSimItem();

    bool isRunning() const { return m_running; }
    void setRunning(bool r);

    int frameCount() const { return m_frameCount; }
    int simSize() const { return m_simSize; }
    void setSimSize(int s);

signals:
    void runningChanged();
    void frameCountChanged(int count);
    void simSizeChanged();

protected:
    QSGNode *updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *) override;
    void releaseResources() override;
    void itemChange(ItemChange change, const ItemChangeData &value) override;

private:
    QSGGeometryNode *buildDisplayNode();

private slots:
    void onFrameTick();

private:
    std::unique_ptr<FluidSimEngine> m_engine;
    std::unique_ptr<QTimer> m_timer;
    bool m_running = false;
    int m_frameCount = 0;
    int m_simSize = 128;
    float m_pendingDt = 0.016f;
    QRhi *m_rhi = nullptr;

    QElapsedTimer m_elapsed;
    qint64 m_lastTick = 0;
};

class FluidDisplayNode : public QSGRenderNode {
public:
    FluidDisplayNode(FluidSimEngine *engine, QRhi *rhi, float dt);
    ~FluidDisplayNode();

    void prepare() override;
    void render(const RenderState *state) override;
    void releaseResources() override;
    RenderingFlags flags() const override { return {}; }
    QRectF rect() const override { return {}; }

    float m_dt = 0.016f;

private:
    FluidSimEngine *m_engine;
    QRhi *m_rhi;
};
