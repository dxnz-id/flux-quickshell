// Copyright 2025 — FluxEngine: C++ QRhi fluid simulation QML plugin

#include "FluxItem.h"
#include "FluxEngine.h"
#include <QSGImageNode>
#include <QSGTexture>
#include <QQuickWindow>
#include <QScopeGuard>


// ---- FluxItem ----

FluxItem::FluxItem(QQuickItem *parent)
    : QQuickItem(parent)
{
    setFlag(ItemHasContents, true);
}

FluxItem::~FluxItem()
{
}

void FluxItem::setRunning(bool r)
{
    if (m_running == r)
        return;
    m_running = r;

    if (r) {
        if (!m_elapsed.isValid())
            m_elapsed.start();
        m_lastTick = m_elapsed.nsecsElapsed();
    }

    emit runningChanged();
}

void FluxItem::setSimSize(int s)
{
    if (m_simSize == s)
        return;
    m_simSize = s;
    emit simSizeChanged();
}

void FluxItem::setDebugMode(int mode)
{
    if (m_debugMode == mode)
        return;
    m_debugMode = mode;
    if (m_engine)
        m_engine->setDebugMode(mode);
    emit debugModeChanged();
    update();
}

void FluxItem::setColorMode(int v)
{
    if (m_colorMode == v) return;
    m_colorMode = v;
    if (m_engine) m_engine->setColorMode(v);
    emit colorModeChanged();
}

void FluxItem::setViscosity(float v)
{
    if (m_viscosity == v) return;
    m_viscosity = v;
    if (m_engine) m_engine->setViscosity(v);
    emit viscosityChanged();
}

void FluxItem::setNoiseMultiplier(float v)
{
    if (m_noiseMultiplier == v) return;
    m_noiseMultiplier = v;
    if (m_engine) m_engine->setNoiseMultiplier(v);
    emit noiseMultiplierChanged();
}

void FluxItem::setTimestep(float v)
{
    if (m_timestep == v) return;
    m_timestep = v;
    if (m_engine) m_engine->setTimestep(v);
    emit timestepChanged();
}

void FluxItem::setDissipation(float v)
{
    if (m_dissipation == v) return;
    m_dissipation = v;
    if (m_engine) m_engine->setDissipation(v);
    emit dissipationChanged();
}

void FluxItem::setPressureIterations(int v)
{
    if (m_pressureIterations == v) return;
    m_pressureIterations = v;
    if (m_engine) m_engine->setPressureIterations(v);
    emit pressureIterationsChanged();
}

void FluxItem::setLineVariance(float v)
{
    if (m_lineVariance == v) return;
    m_lineVariance = v;
    if (m_engine) m_engine->setLineVariance(v);
    emit lineVarianceChanged();
}

void FluxItem::setLineWidthMultiplier(float v)
{
    if (m_lineWidthMultiplier == v) return;
    m_lineWidthMultiplier = v;
    if (m_engine) m_engine->setLineWidthMultiplier(v);
    emit lineWidthMultiplierChanged();
}

void FluxItem::setZoom(float v)
{
    if (m_zoom == v) return;
    m_zoom = v;
    if (m_engine) m_engine->setZoom(v);
    emit zoomChanged();
}

void FluxItem::setMsaaSampleCount(int v)
{
    if (m_msaaSamples == v) return;
    if (v != 1 && v != 2 && v != 4) return;
    m_msaaSamples = v;
    if (m_engine) m_engine->setMsaaSamples(v);
    emit msaaSampleCountChanged();
}

void FluxItem::initOurRhi()
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

void FluxItem::onFrameTick()
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

void FluxItem::scheduleEngineStep()
{
    if (!m_ourRhi || !m_engine || !m_engine->isInitialized() || !m_sharedCtx)
        return;
    if (m_stopping.load(std::memory_order_acquire))
        return;

    m_inflightJobs.fetch_add(1, std::memory_order_relaxed);
    auto *job = new EngineStepJob(m_engine.get(), m_ourRhi.get(),
                                  m_sharedCtx, m_fallbackSurface.get(),
                                  this, m_pendingDt);
    window()->scheduleRenderJob(job, QQuickWindow::AfterSwapStage);
}

QSGNode *FluxItem::updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *)
{
    if (!window())
        return nullptr;

    initOurRhi();

    if (!m_engine && m_ourRhi) {
        m_engine = std::make_unique<FluxEngine>();
        m_engine->init(m_ourRhi.get(), m_simSize);
        m_engine->setDebugMode(m_debugMode);
        m_engine->setColorMode(m_colorMode);
        m_engine->setViscosity(m_viscosity);
        m_engine->setNoiseMultiplier(m_noiseMultiplier);
        m_engine->setTimestep(m_timestep);
        m_engine->setDissipation(m_dissipation);
        m_engine->setPressureIterations(m_pressureIterations);
        m_engine->setLineVariance(m_lineVariance);
        m_engine->setLineWidthMultiplier(m_lineWidthMultiplier);
        m_engine->setZoom(m_zoom);
        m_engine->setMsaaSamples(m_msaaSamples);
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

void FluxItem::releaseResources()
{
    m_stopping.store(true, std::memory_order_seq_cst);
    m_running = false;

    // Wait for in-flight EngineStepJobs to complete
    QElapsedTimer waitTimer;
    waitTimer.start();
    while (m_inflightJobs.load(std::memory_order_acquire) > 0) {
        QThread::msleep(1);
        if (waitTimer.elapsed() > 100) {
            fprintf(stderr, "releaseResources: timeout waiting for %d inflight jobs\n",
                    m_inflightJobs.load(std::memory_order_acquire));
            break;
        }
    }

    if (m_sharedCtx && m_fallbackSurface) {
        if (m_sharedCtx->thread() != QThread::currentThread())
            m_sharedCtx->moveToThread(QThread::currentThread());
        if (!m_sharedCtx->makeCurrent(m_fallbackSurface.get())) {
            fprintf(stderr, "releaseResources: cannot makeCurrent, leaking GPU resources\n");
            m_engine.release();
            m_ourRhi.release();
        } else {
            m_engine.reset();
            m_ourRhi.reset();
            m_sharedCtx->doneCurrent();
        }
    } else {
        m_engine.reset();
        m_ourRhi.reset();
    }
    m_fallbackSurface.reset();
    delete m_sharedCtx;
    m_sharedCtx = nullptr;
}

void FluxItem::itemChange(ItemChange, const ItemChangeData &)
{
}

void FluxItem::storeReadback(const QByteArray &data)
{
    QMutexLocker lock(&m_readbackMutex);
    m_readbackData = data;
}

bool FluxItem::hasPendingReadback() const
{
    return m_readbackPending;
}

void FluxItem::setReadbackPending(bool p)
{
    m_readbackPending = p;
}

int FluxItem::computeDisplaySize(int w, int h)
{
    return std::min(w, h);
}

// ---- EngineStepJob ----

void EngineStepJob::run()
{
    auto guard = qScopeGuard([this] {
        if (m_sharedCtx && QOpenGLContext::currentContext() == m_sharedCtx)
            m_sharedCtx->doneCurrent();
        m_item->decrementInflight();
    });
    if (!m_engine || !m_ourRhi || !m_sharedCtx || !m_fallbackSurface)
        return;
    if (m_item->isStopping())
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
                fprintf(stderr, "  STUCK: readback data too small at frame %d (%lld < %d)\n", frame, (long long)result->data.size(), px);
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
