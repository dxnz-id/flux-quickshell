#include "FluidSimItem.h"
#include "FluidSimEngine.h"
#include "FluidSimShaders.h"
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
    releaseResources();
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

    window()->update();

    // Track frame count on GUI thread (mirrors engine's render-thread count)
    m_frameCount++;
    emit frameCountChanged(m_frameCount);
}

QSGNode *FluidSimItem::updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *)
{
    if (!window())
        return nullptr;

    // Store the QRhi pointer (render thread, valid for window lifetime)
    auto *iface = window()->rendererInterface();
    if (iface)
        m_rhi = static_cast<QRhi *>(
            iface->getResource(window(), QSGRendererInterface::RhiResource));

    // Create/reuse display node
    auto *node = static_cast<FluidDisplayNode *>(oldNode);
    if (!node) {
        // Engine lazy init in prepare(), node stores QRhi for later use
        if (!m_engine)
            m_engine = std::make_unique<FluidSimEngine>();
        node = new FluidDisplayNode(m_engine.get(), m_rhi, m_pendingDt);
    } else {
        node->m_dt = m_pendingDt;
    }

    node->setDisplayRect(QRectF(0, 0, width(), height()));
    node->markDirty(QSGNode::DirtyMaterial);

    return node;
}

void FluidSimItem::releaseResources()
{
    if (m_engine) {
        m_engine->releaseResources();
        m_engine.reset();
    }
}

void FluidSimItem::itemChange(ItemChange change, const ItemChangeData &)
{
}

// ---- FluidDisplayNode ----

FluidDisplayNode::FluidDisplayNode(FluidSimEngine *engine, QRhi *rhi, float dt)
    : m_engine(engine), m_rhi(rhi), m_dt(dt)
{
}

FluidDisplayNode::~FluidDisplayNode()
{
    releaseResources();
}

QSGRenderNode::RenderingFlags FluidDisplayNode::flags() const
{
    return QSGRenderNode::BoundedRectRendering;
}

QRectF FluidDisplayNode::rect() const
{
    return m_displayRect;
}

void FluidDisplayNode::ensurePipeline(QRhiRenderTarget *rt)
{
    QRhiRenderPassDescriptor *rp = rt->renderPassDescriptor();
    if (!rp)
        return;

    // Recreate pipeline if render target size changed (RP desc might have changed)
    bool sizeChanged = m_lastRtWidth != rt->pixelSize().width() ||
                       m_lastRtHeight != rt->pixelSize().height();
    if (m_displayPipeline && m_rp == rp && !sizeChanged)
        return;

    m_rp = rp;

    QShader vs = FluidSimShaders::loadShader("display_vert");
    QShader fs = FluidSimShaders::loadShader("display_frag");
    if (!vs.isValid() || !fs.isValid())
        return;

    auto *pp = m_rhi->newGraphicsPipeline();
    pp->setShaderStages({
        { QRhiShaderStage::Vertex, vs },
        { QRhiShaderStage::Fragment, fs }
    });
    pp->setRenderPassDescriptor(rp);
    pp->setShaderResourceBindings(m_srb.get());

    QRhiVertexInputLayout inputLayout;
    inputLayout.setBindings({
        QRhiVertexInputBinding(4 * sizeof(float))
    });
    inputLayout.setAttributes({
        QRhiVertexInputAttribute(0, 0, QRhiVertexInputAttribute::Float2, 0),
        QRhiVertexInputAttribute(1, 0, QRhiVertexInputAttribute::Float2, 2 * sizeof(float)),
    });
    pp->setVertexInputLayout(inputLayout);

    QRhiGraphicsPipeline::TargetBlend blend;
    blend.enable = false;
    pp->setTargetBlends({ blend });
    pp->create();

    m_displayPipeline.reset(pp);
    m_lastRtWidth = rt->pixelSize().width();
    m_lastRtHeight = rt->pixelSize().height();
}

void FluidDisplayNode::prepare()
{
    if (!m_engine || !m_rhi)
        return;

    if (!m_engine->isInitialized()) {
        m_engine->init(m_rhi, 128);
        fprintf(stderr, "  FluidDisplayNode::prepare: init() called\n");
    }

    QRhiCommandBuffer *cb = commandBuffer();
    if (cb)
        m_engine->step(cb, m_dt);

    QRhiTexture *tex = m_engine->currentOutputTex();
    QRhiSampler *sampler = m_engine->nearestSampler();
    m_srb.reset(m_rhi->newShaderResourceBindings());
    QRhiShaderResourceBinding binding = QRhiShaderResourceBinding::sampledTexture(
        0, QRhiShaderResourceBinding::FragmentStage, tex, sampler);
    m_srb->setBindings({binding});
    m_srb->create();
}

void FluidDisplayNode::render(const RenderState *state)
{
    Q_UNUSED(state)
    if (!m_engine || !m_rhi || !m_srb)
        return;

    QRhiCommandBuffer *cb = commandBuffer();
    QRhiRenderTarget *rt = renderTarget();
    QRhiBuffer *vBuf = m_engine->displayVertexBuffer();

    if (!cb || !rt || !vBuf)
        return;

    ensurePipeline(rt);
    if (!m_displayPipeline)
        return;

    const QSize size = rt->pixelSize();

    QRhiCommandBuffer::VertexInput vi(vBuf, 0);
    cb->setGraphicsPipeline(m_displayPipeline.get());
    cb->setShaderResources(m_srb.get());
    cb->setViewport(QRhiViewport(0, 0, size.width(), size.height()));
    cb->setVertexInput(0, 1, &vi);
    cb->draw(4);
}

void FluidDisplayNode::releaseResources()
{
    m_srb.reset();
    m_displayPipeline.reset();
    m_rp = nullptr;
}
