#pragma once
// ============================================================
//  OvFogSource — the texture the GPU fog pass reads.
//
//  It paints BOTH halves' outgoing curves — the parts of the EQ curves that have
//  left their plots — as plain strokes: no blur, no fading, no band split.  The
//  blur and the fade are applied by the shared OvBlur chain, with the radius a
//  continuous function of the distance from the seam instead of twelve
//  hard-edged slices (see blur.frag).
//
//  ONE item for both halves, deliberately.  Two items — one per half — meant two
//  textures and two chains, and those did not present in the same frame while a
//  window was being dragged: the upper half showed a new frame and the lower one
//  the previous frame, which is exactly what reads as tearing.  One surface
//  cannot be inconsistent with itself.
//
//  Item geometry: the surface spans the seam, from half a reach above it to half
//  a reach below, so it contains both halves' strokes.  OvEqView::paintOverflowFog
//  clamps each stroke to its own reach, so both fit inside.
// ============================================================
#include <QImage>
#include <QQuickPaintedItem>
#include <QPointer>
#include <QTimer>

// The Q_PROPERTY(OvEqView* view) below needs the type to be COMPLETE for moc's
// metatype registration, not just declared.
#include "OvEqView.h"

class OvFogSource : public QQuickPaintedItem
{
    Q_OBJECT
    Q_PROPERTY(OvEqView* topView    READ topView    WRITE setTopView    NOTIFY viewChanged)
    Q_PROPERTY(OvEqView* bottomView READ bottomView WRITE setBottomView NOTIFY viewChanged)
    /// Frames this surface actually painted in the last second — its own rate,
    /// not the window's.
    Q_PROPERTY(double paintFps READ paintFps NOTIFY paintFpsChanged)

public:
    double paintFps() const { return m_paintFps; }
    explicit OvFogSource (QQuickItem* parent = nullptr);

    OvEqView* topView() const    { return m_top.data(); }
    OvEqView* bottomView() const { return m_bottom.data(); }
    void setTopView (OvEqView* v);
    void setBottomView (OvEqView* v);

    void paint (QPainter* g) override;

signals:
    void viewChanged();
    void revisionChanged();
    void paintFpsChanged();

private:
    void subscribe (OvEqView* v);
    void unsubscribe (OvEqView* v);
    OvEqView* prefixView() const { return m_top != nullptr ? m_top.data() : m_bottom.data(); }

    // The fog depends only on the parameters — not on the spectrum — so it is
    // rebuilt when one moved.  The tick is a safety net for a signapl this fog
    // does not listen to; it only marks the surface dirty.
    QTimer m_tick;
    int    m_revision = 0;
    QPointer<OvEqView> m_top, m_bottom;
    QImage  m_cache;            // the strokes, at device resolution
    quint64 m_cacheKey = 0;     // what the cache was built from
    double  m_paintFps = 0.0;

};
