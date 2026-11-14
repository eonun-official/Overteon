#include "OvEnv.h"
#include "OvFrameProbe.h"

#include <QSGNode>
#include <QQuickWindow>
#include <cstdio>
#include <cstdlib>

OvFrameProbe::OvFrameProbe (QQuickItem* parent) : QQuickItem (parent), m_publish (this)
{
    setFlag (QQuickItem::ItemHasContents, true);
    // Zero size: it draws nothing, it only gets visited.
    setWidth (0);
    setHeight (0);
    setVisible (true);

    // Reading a counter written by the render thread from the GUI thread needs
    // one hop; doing it once a second keeps that hop off the frame path.
    m_publish.setInterval (1000);
    QObject::connect (&m_publish, &QTimer::timeout, this, [this] { publish(); });
    m_publish.start();
}

QSGNode* OvFrameProbe::updatePaintNode (QSGNode* old, UpdatePaintNodeData*)
{
    // This is the tick: the renderer reached this widget's subtree.
    m_frames++;
    if (ovEnvFlag ("OVERTEON_PROBEDEBUG") && m_frames % 120 == 0)
        std::fprintf (stderr, "[probe] %s %llu\n",
                      qPrintable (objectName()), (unsigned long long) m_frames);
    // No node: nothing to draw.  Returning null does not stop Qt from calling
    // this again next frame — the item is still marked as having contents.
    return old;
}

void OvFrameProbe::publish()
{
    const quint64 n = m_frames;
    static quint64 last = 0;
    const quint64 delta = n - last;
    last = n;
    const double now = n;                 // avoid a clock in the signal path
    Q_UNUSED (now);
    m_fps = double (delta);               // one publish per second -> frames/s
    emit fpsChanged();
}
