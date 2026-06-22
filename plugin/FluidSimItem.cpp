#include "FluidSimItem.h"
#include "FluidSimEngine.h"
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

    m_frameCount++;
    emit frameCountChanged(m_frameCount);
}

QSGNode *FluidSimItem::updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *)
{
    if (!window())
        return nullptr;

    auto *iface = window()->rendererInterface();
    if (iface)
        m_rhi = static_cast<QRhi *>(
            iface->getResource(window(), QSGRendererInterface::RhiResource));

    if (!m_engine)
        m_engine = std::make_unique<FluidSimEngine>();
    if (!m_engine->isInitialized() && m_rhi) {
        m_engine->init(m_rhi, m_simSize);
    }

    // --- Engine step node ---
    if (!oldNode) {
        auto *root = new QSGNode();
        auto *engineNode = new FluidDisplayNode(m_engine.get(), m_rhi, m_pendingDt);
        root->appendChildNode(engineNode);
        root->appendChildNode(buildDisplayNode());
        return root;
    }

    for (int i = 0; i < oldNode->childCount(); ++i) {
        auto *child = oldNode->childAtIndex(i);
        if (child->type() == QSGNode::RenderNodeType) {
            static_cast<FluidDisplayNode *>(child)->m_dt = m_pendingDt;
        } else if (child->type() == QSGNode::GeometryNodeType) {
            auto *gn = static_cast<QSGGeometryNode *>(child);
            QSGGeometry *g = gn->geometry();
            if (g) {
                auto *v = g->vertexDataAsTexturedPoint2D();
                float w = width(), h = height();
                v[0].set(0, 0, 0, 0);
                v[1].set(w, 0, 1, 0);
                v[2].set(0, h, 0, 1);
                v[3].set(w, h, 1, 1);
                gn->markDirty(QSGNode::DirtyGeometry);
            }
        }
    }

    return oldNode;
}

QSGGeometryNode *FluidSimItem::buildDisplayNode()
{
    auto *g = new QSGGeometry(QSGGeometry::defaultAttributes_TexturedPoint2D(), 4, 6);
    g->setDrawingMode(GL_TRIANGLES);
    auto *v = g->vertexDataAsTexturedPoint2D();
    float w = width(), h = height();
    v[0].set(0, 0, 0, 0);
    v[1].set(w, 0, 1, 0);
    v[2].set(0, h, 0, 1);
    v[3].set(w, h, 1, 1);
    quint16 *idx = g->indexDataAsUShort();
    idx[0] = 0; idx[1] = 1; idx[2] = 2;
    idx[3] = 1; idx[4] = 2; idx[5] = 3;

    auto *mat = new QSGOpaqueTextureMaterial();
    if (m_engine->isInitialized() && m_engine->displayTex()) {
        auto *tex = window()->createTextureFromRhiTexture(m_engine->displayTex(), {});
        mat->setTexture(tex);
    }

    auto *node = new QSGGeometryNode();
    node->setGeometry(g);
    node->setMaterial(mat);
    node->setFlag(QSGNode::OwnsGeometry, true);
    node->setFlag(QSGNode::OwnsMaterial, true);
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

void FluidDisplayNode::prepare()
{
    if (!m_engine || !m_rhi)
        return;

    QRhiCommandBuffer *cb = commandBuffer();
    if (cb)
        m_engine->step(cb, m_dt);
}

void FluidDisplayNode::render(const RenderState *)
{
}

void FluidDisplayNode::releaseResources()
{
    m_engine = nullptr;
}
