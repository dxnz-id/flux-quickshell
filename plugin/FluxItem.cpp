// Copyright 2025 — FluxEngine: C++ QRhi fluid simulation QML plugin

#include "FluxItem.h"
#include "FluxEngine.h"
#include <QSGImageNode>
#include <QSGTexture>
#include <QQuickWindow>


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

void FluxItem::setColorPreset(int v)
{
    if (m_colorPreset == v) return;
    m_colorPreset = v;
    if (m_engine) m_engine->setColorPreset(v);
    emit colorPresetChanged();
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

void FluxItem::setDiagStep(int v)
{
    if (m_diagStep == v) return;
    m_diagStep = v;
    emit diagStepChanged();
    update();
}

void FluxItem::initOurRhi()
{
    if (m_diagStep < 2) return;
    if (m_ourRhi)
        return;

    // Must create context on GUI thread so releaseResources() can safely
    // makeCurrent() there at unlock time.
    if (QThread::currentThread() != thread()) {
        if (!m_initQueued) {
            m_initQueued = true;
            QMetaObject::invokeMethod(this, [this]() {
                m_initQueued = false;
                initOurRhi();
            }, Qt::QueuedConnection);
        }
        return;
    }

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

    if (m_diagStep < 3) {
        // Step 2: context only, no QRhi
        m_sharedCtx->doneCurrent();
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
    update(); // trigger next frame so initEngine() can run
}

void FluxItem::onFrameTick()
{
    if (m_diagStep < 5) return;
    if (!m_running || !window())
        return;
    if (!m_engine || !m_engine->isInitialized())
        return;
    if (!m_sharedCtx || !m_fallbackSurface)
        return;

    if (!m_elapsed.isValid())
        m_elapsed.start();

    qint64 now = m_elapsed.nsecsElapsed();
    float dt = (now - m_lastTick) / 1e9f;
    m_lastTick = now;

    if (dt > 0.1f)
        dt = 0.1f;

    // Ensure GL context is on GUI thread before making current
    if (m_sharedCtx->thread() != QThread::currentThread()) {
        if (QOpenGLContext::currentContext() == m_sharedCtx)
            m_sharedCtx->doneCurrent();
        m_sharedCtx->moveToThread(QThread::currentThread());
        m_fallbackSurface->moveToThread(QThread::currentThread());
    }

    if (!m_sharedCtx->makeCurrent(m_fallbackSurface.get()))
        return;

    m_engine->checkResize();

    QRhiCommandBuffer *cb = nullptr;
    if (m_ourRhi->beginOffscreenFrame(&cb) != QRhi::FrameOpSuccess) {
        m_sharedCtx->doneCurrent();
        return;
    }

    m_engine->step(cb, dt);

    if (m_engine->displayTex() && !m_readbackPending.load(std::memory_order_acquire)) {
        m_readbackPending.store(true, std::memory_order_release);
        QSize ds = m_engine->displaySize();
        int px = ds.width() * ds.height() * 4;
        auto *result = new QRhiReadbackResult();
        QByteArray *outData = new QByteArray();
        outData->resize(px);
        result->completed = [this, result, outData, px]() {
            if (result->data.size() >= px) {
                memcpy(outData->data(), result->data.constData(), px);
                QMutexLocker lock(&m_readbackMutex);
                m_readbackData = *outData;
            }
            m_readbackPending.store(false, std::memory_order_release);
            delete outData;
            delete result;
        };
        QRhiResourceUpdateBatch *rub = m_ourRhi->nextResourceUpdateBatch();
        rub->readBackTexture(QRhiReadbackDescription(m_engine->displayTex()), result);
        cb->resourceUpdate(rub);
    }

    m_ourRhi->endOffscreenFrame();
    m_sharedCtx->doneCurrent();

    update();

    m_frameCount++;
    emit frameCountChanged(m_frameCount);
}

void FluxItem::initEngine()
{
    if (m_engine) return;
    if (!m_ourRhi) return;

    // Engine init must run on GUI thread because QRhi was created there.
    if (QThread::currentThread() != thread()) {
        if (!m_engineInitQueued) {
            m_engineInitQueued = true;
            QMetaObject::invokeMethod(this, [this]() {
                m_engineInitQueued = false;
                initEngine();
            }, Qt::QueuedConnection);
        }
        return;
    }

    m_engine = std::make_unique<FluxEngine>();
    m_engine->init(m_ourRhi.get(), m_simSize);
    m_engine->setDebugMode(m_debugMode);
    m_engine->setColorPreset(m_colorPreset);
    m_engine->setViscosity(m_viscosity);
    m_engine->setNoiseMultiplier(m_noiseMultiplier);
    m_engine->setTimestep(m_timestep);
    m_engine->setDissipation(m_dissipation);
    m_engine->setPressureIterations(m_pressureIterations);
    m_engine->setLineVariance(m_lineVariance);
    m_engine->setLineWidthMultiplier(m_lineWidthMultiplier);
    m_engine->setZoom(m_zoom);

    fprintf(stderr, "  engine initialized on GUI thread\n");
    update();
}

QSGNode *FluxItem::updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *)
{
    if (m_diagStep < 2) return nullptr;

    if (!window())
        return nullptr;

    initOurRhi();

    if (m_diagStep < 4) return nullptr;

    initEngine();
    if (!m_engine) return nullptr; // deferred — engine init queued to GUI thread

    auto *imageNode = static_cast<QSGImageNode *>(oldNode);
    if (!imageNode) {
        imageNode = window()->createImageNode();
        imageNode->setTextureCoordinatesTransform(QSGImageNode::NoTransform);
    }

    // Detect resize and propagate to engine
    int w = int(width());
    int h = int(height());
    if (w != m_lastItemW || h != m_lastItemH) {
        QSize ds = computeDisplaySize(w, h);
        if (m_engine)
            m_engine->resizeDisplay(w, h, ds);
        m_lastItemW = w;
        m_lastItemH = h;
    }

    QSize ds = m_engine->displaySize();
    imageNode->setRect(0, 0, width(), height());
    imageNode->setSourceRect(QRectF(0, 0, ds.width(), ds.height()));

    QImage img;
    {
        QMutexLocker lock(&m_readbackMutex);
        if (m_readbackData.size() >= ds.width() * ds.height() * 4) {
            img = QImage((const uchar*)m_readbackData.constData(),
                         ds.width(), ds.height(), QImage::Format_RGBA8888).copy();

        }
    }
    if (img.isNull()) {
        img = QImage(ds.width(), ds.height(), QImage::Format_RGBA8888);
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
    m_running = false;

    // Engine and QRhi were created on GUI thread — cleanup here.
    // makeCurrent may fail if window is being torn down, in which case
    // we leak GPU resources (safe — OS reclaims on process exit).

    if (!window() && m_sharedCtx && m_fallbackSurface) {
        m_engine.release();
        m_ourRhi.release();
    } else if (m_sharedCtx && m_fallbackSurface) {
        if (m_sharedCtx->thread() != QThread::currentThread()) {
            m_sharedCtx->moveToThread(QThread::currentThread());
            m_fallbackSurface->moveToThread(QThread::currentThread());
        }
        if (m_sharedCtx->makeCurrent(m_fallbackSurface.get())) {
            m_engine.reset();
            m_ourRhi.reset();
            m_sharedCtx->doneCurrent();
        } else {
            m_engine.release();
            m_ourRhi.release();
        }
    } else {
        m_engine.reset();
        m_ourRhi.reset();
    }
    m_fallbackSurface.reset();
    if (m_sharedCtx) {
        if (m_sharedCtx->thread() == QThread::currentThread())
            delete m_sharedCtx;
        else
            fprintf(stderr, "releaseResources: leaking sharedCtx (wrong thread)\n");
        m_sharedCtx = nullptr;
    }
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

QSize FluxItem::computeDisplaySize(int w, int h)
{
    return QSize(w, h);
}
