// Copyright 2025 — FluidSim: C++ QRhi fluid simulation QML plugin

#include "FluidSimItem.h"
#include "FluidSimEngine.h"
#include <QSGImageNode>
#include <QSGTexture>
#include <QQuickWindow>
#include <QTimer>

// ---- FluidSimItem ----

FluidSimItem::FluidSimItem(QQuickItem *parent)
    : QQuickItem(parent)
{
    setFlag(ItemHasContents, true);
}

FluidSimItem::~FluidSimItem()
{
    if (m_timer)
        m_timer->stop();
}

void FluidSimItem::setRunning(bool r)
{
    if (m_running == r)
        return;
    m_running = r;

    if (r) {
        if (!m_timer) {
            m_timer = std::make_unique<QTimer>(this);
            m_timer->setTimerType(Qt::PreciseTimer);
            connect(m_timer.get(), &QTimer::timeout, this, &FluidSimItem::onFrameTick);
        }
        if (!m_elapsed.isValid())
            m_elapsed.start();
        m_lastTick = m_elapsed.nsecsElapsed();
        m_timer->start(16);
    } else {
        if (m_timer)
            m_timer->stop();
    }

    emit runningChanged();
}

void FluidSimItem::setSimSize(int s)
{
    if (m_simSize == s)
        return;
    m_simSize = s;
    emit simSizeChanged();
}

void FluidSimItem::setDebugMode(int mode)
{
    if (m_debugMode == mode)
        return;
    m_debugMode = mode;
    if (m_engine)
        m_engine->setDebugMode(mode);
    emit debugModeChanged();
    update();
}

void FluidSimItem::initOurRhi()
{
    if (m_ourRhi)
        return;

    if (!window())
        return;

    auto *iface = window()->rendererInterface();
    if (!iface)
        return;

    auto *sgCtx = static_cast<QOpenGLContext *>(
        iface->getResource(window(), QSGRendererInterface::OpenGLContextResource));
    if (!sgCtx)
        return;

    auto *ourCtx = new QOpenGLContext();
    ourCtx->setShareContext(sgCtx);
    ourCtx->setFormat(sgCtx->format());
    if (!ourCtx->create()) {
        fprintf(stderr, "  ERROR: could not create shared GL context\n");
        delete ourCtx;
        return;
    }
    m_sharedCtx = ourCtx;

    m_fallbackSurface = std::unique_ptr<QOffscreenSurface>(
        QRhiGles2InitParams::newFallbackSurface(sgCtx->format()));

    bool ok = m_sharedCtx->makeCurrent(m_fallbackSurface.get());
    if (!ok) {
        fprintf(stderr, "  ERROR: could not make our context current for QRhi init\n");
        return;
    }

    QRhiGles2InitParams params;
    params.fallbackSurface = m_fallbackSurface.get();
    params.format = sgCtx->format();
    m_ourRhi = std::unique_ptr<QRhi>(QRhi::create(QRhi::OpenGLES2, &params));
    if (!m_ourRhi) {
        fprintf(stderr, "  ERROR: could not create shared QRhi\n");
        m_sharedCtx->doneCurrent();
        return;
    }

    m_sharedCtx->doneCurrent();
}

void FluidSimItem::onFrameTick()
{
    if (!window() || !m_running)
        return;

    if (!m_elapsed.isValid())
        m_elapsed.start();

    qint64 now = m_elapsed.nsecsElapsed();
    float dt = (now - m_lastTick) / 1e9f;
    m_lastTick = now;

    if (dt > 0.1f)
        dt = 0.1f;

    m_pendingDt = dt;

    update();
    scheduleEngineStep();

    m_frameCount++;
    emit frameCountChanged(m_frameCount);
}

void FluidSimItem::scheduleEngineStep()
{
    if (!m_ourRhi || !m_engine || !m_engine->isInitialized() || !m_sharedCtx)
        return;

    auto *job = new EngineStepJob(m_engine.get(), m_ourRhi.get(),
                                  m_sharedCtx, m_fallbackSurface.get(),
                                  this, m_pendingDt);
    window()->scheduleRenderJob(job, QQuickWindow::AfterSwapStage);
}

QSGNode *FluidSimItem::updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *)
{
    if (!window())
        return nullptr;

    initOurRhi();

    if (!m_engine && m_ourRhi) {
        m_engine = std::make_unique<FluidSimEngine>();
        m_engine->init(m_ourRhi.get(), m_simSize);
        m_engine->setDebugMode(m_debugMode);
    }

    auto *imageNode = static_cast<QSGImageNode *>(oldNode);
    if (!imageNode) {
        imageNode = window()->createImageNode();
        imageNode->setTextureCoordinatesTransform(QSGImageNode::NoTransform);
    }

    // Detect resize and propagate to engine
    int w = int(width());
    int h = int(height());
    if (w != m_lastItemW || h != m_lastItemH) {
        int ds = computeDisplaySize(w, h);
        if (m_engine)
            m_engine->resizeDisplay(w, h, ds);
        m_lastItemW = w;
        m_lastItemH = h;
    }

    int ds = m_engine->displaySize();
    imageNode->setRect(0, 0, width(), height());
    imageNode->setSourceRect(QRectF(0, 0, ds, ds));

    QImage img;
    {
        QMutexLocker lock(&m_readbackMutex);
        if (m_readbackData.size() >= ds * ds * 4) {
            img = QImage((const uchar*)m_readbackData.constData(),
                         ds, ds, QImage::Format_RGBA8888).copy();
        }
    }
    if (img.isNull()) {
        img = QImage(ds, ds, QImage::Format_RGBA8888);
        img.fill(QColor(15, 15, 30));
    }
    auto *tex = window()->createTextureFromImage(img);
    tex->setFiltering(QSGTexture::Linear);
    imageNode->setTexture(tex);
    imageNode->setOwnsTexture(true);

    return imageNode;
}

void FluidSimItem::releaseResources()
{
    m_engine.reset();
    m_ourRhi.reset();
    m_fallbackSurface.reset();
    delete m_sharedCtx;
    m_sharedCtx = nullptr;
}

void FluidSimItem::itemChange(ItemChange, const ItemChangeData &)
{
}

void FluidSimItem::storeReadback(const QByteArray &data)
{
    QMutexLocker lock(&m_readbackMutex);
    m_readbackData = data;
}

bool FluidSimItem::hasPendingReadback() const
{
    return m_readbackPending;
}

void FluidSimItem::setReadbackPending(bool p)
{
    m_readbackPending = p;
}

int FluidSimItem::computeDisplaySize(int w, int h)
{
    int base = std::min(w, h);
    int ds = int(float(base) / 750.0f * 512.0f);
    ds = ((ds + 7) / 15) * 15;  // round to nearest 15
    if (ds < 256) ds = 256;
    if (ds > 1024) ds = 1024;
    return ds;
}

// ---- EngineStepJob ----

void EngineStepJob::run()
{
    if (!m_engine || !m_ourRhi || !m_sharedCtx || !m_fallbackSurface)
        return;

    if (m_sharedCtx->thread() != QThread::currentThread()) {
        if (QOpenGLContext::currentContext() == m_sharedCtx)
            m_sharedCtx->doneCurrent();
        m_sharedCtx->moveToThread(QThread::currentThread());
        m_fallbackSurface->moveToThread(QThread::currentThread());
    }

    // Process pending resize before frame (QRhi resource ops outside frame)
    m_engine->checkResize();

    QRhiCommandBuffer *cb = nullptr;
    if (m_ourRhi->beginOffscreenFrame(&cb) != QRhi::FrameOpSuccess) {
        fprintf(stderr, "  STUCK: beginOffscreenFrame FAILED at engine frame %d\n", m_engine->frameCount());
        return;
    }

    int frame = m_engine->frameCount();
    if (frame % 60 == 0)
        fprintf(stderr, "  STEP frame=%d\n", frame);

    m_engine->step(cb, m_dt);

    if (m_engine->displayTex() && !m_item->hasPendingReadback()) {
        m_item->setReadbackPending(true);
        int ds = m_engine->displaySize();
        int px = ds * ds * 4;
        auto *result = new QRhiReadbackResult();
        QByteArray *outData = new QByteArray();
        outData->resize(px);
        result->completed = [item = m_item, result, outData, px, frame]() {
            if (result->data.size() >= px) {
                memcpy(outData->data(), result->data.constData(), px);
                item->storeReadback(*outData);
            } else {
                fprintf(stderr, "  STUCK: readback data too small at frame %d (%d < %d)\n", frame, result->data.size(), px);
            }
            item->setReadbackPending(false);
            delete outData;
            delete result;
        };
        QRhiResourceUpdateBatch *rub = m_ourRhi->nextResourceUpdateBatch();
        rub->readBackTexture(QRhiReadbackDescription(m_engine->displayTex()), result);
        cb->resourceUpdate(rub);
    } else if (m_item->hasPendingReadback() && frame % 60 == 0) {
        fprintf(stderr, "  STUCK: readback still pending at frame %d\n", frame);
    }

    m_ourRhi->endOffscreenFrame();
}
