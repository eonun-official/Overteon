#pragma once
// ============================================================
//  OvFogSource — the outgoing EQ curve, as RHI geometry.
//
//  Both halves' curves past the plot edge, emitted as triangles through
//  QQuickItem::updatePaintNode.  Qt renders that with whatever RHI backend is
//  active — GL, D3D11, Vulkan or Metal — so there is no GL dependency and no
//  per-frame upload.
//
//  Why not QPainter: Qt 6 has only two QPainter engines, raster (CPU) and
//  QOpenGL2PaintEngineEx (GL).  A QPainter that rasterises on the GPU therefore
//  forces OpenGL, which is why the first attempt had to be a
//  QQuickFramebufferObject.  The fog is only two polylines, so it does not need
//  a paint engine at all: each segment becomes a quad, and Qt Quick draws it on
//  every backend.
//
//  The blur and the fade are still blur.frag, which is a ShaderEffect and thus
//  already RHI.  This item supplies the shape; that shader supplies the look.
// ============================================================
#include <QQuickItem>
#include <QTimer>
#include <QPointer>
#include <QSGGeometry>

// The Q_PROPERTY(OvEqView*) below needs the type to be COMPLETE for moc's
// metatype registration.
#include "OvEqView.h"

class OvFogSource : public QQuickItem
{
    Q_OBJECT
    Q_PROPERTY(OvEqView* topView    READ topView    WRITE setTopView    NOTIFY viewChanged)
    Q_PROPERTY(OvEqView* bottomView READ bottomView WRITE setBottomView NOTIFY viewChanged)
    /// Frames this surface actually produced in the last second — its own rate,
    /// not the window's.
    Q_PROPERTY(double paintFps READ paintFps NOTIFY paintFpsChanged)

public:
    explicit OvFogSource (QQuickItem* parent = nullptr);

    OvEqView* topView() const    { return m_top.data(); }
    OvEqView* bottomView() const { return m_bottom.data(); }
    void setTopView (OvEqView* v);
    void setBottomView (OvEqView* v);

    double paintFps() const { return m_paintFps; }

signals:
    void viewChanged();
    void revisionChanged();
    void paintFpsChanged();

protected:
    QSGNode* updatePaintNode (QSGNode* old, UpdatePaintNodeData*) override;

private:
    void subscribe (OvEqView* v);
    void unsubscribe (OvEqView* v);
    void markDirty();

    /// Everything the geometry depends on, as one number.  Rebuilding ~2000
    /// vertices per frame for a picture that did not change was most of what
    /// made this surface slow; one comparison is not.
    quint64 signature() const;

    QTimer m_tick;
    int    m_revision = 0;
    QPointer<OvEqView> m_top, m_bottom;

    // Render-thread state.
    quint64 m_builtKey = 0;
    quint64 m_built = 0;
    double  m_paintFps = 0.0;
    bool    m_dirty = true;
};
