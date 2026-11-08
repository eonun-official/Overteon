#pragma once
// ============================================================
//  OvFogSource — the outgoing EQ curve, as a cached texture.
//
//  Both halves' curves past the plot edge, painted ONCE per curve change into a
//  device-resolution QImage and handed to the scene graph as a texture.  The blur
//  and the fade are blur.frag, which is a ShaderEffect and therefore already
//  backend-agnostic, so nothing here depends on OpenGL.
//
//  Why QPainter for the strokes: they need ANTIALIASING.  QSGGeometry has none —
//  setAntialiasing() only reaches QPainter — so a quad-built stroke comes out
//  with hard edges and, once blurred by a small sigma, keeps a bright sharp core
//  instead of the reference's soft one.  A skirt of partial-alpha vertices was
//  tried and helped, but a proper coverage rasteriser is what the reference used
//  and what this draws with.
//
//  Painting into an image is a CPU cost, so it is CACHED on a signature of
//  everything the strokes depend on: both plots' curve signatures, the size, and
//  the display scale.  A still curve costs one comparison; a drag costs one
//  rasterisation per frame, which is the same work the QPainter reference did and
//  measured 58 fps on the real GPU.
// ============================================================
#include <QQuickItem>
#include <QTimer>
#include <QPointer>
#include <QImage>
#include <QSGTexture>
#include <QSGImageNode>
#include <QVector>
#include <QUuid>

// The Q_PROPERTY(OvEqView*) below needs the type to be COMPLETE for moc's
// metatype registration.
#include "OvEqView.h"

class OvFogSource : public QQuickItem
{
    Q_OBJECT
    Q_PROPERTY(OvEqView* topView    READ topView    WRITE setTopView    NOTIFY viewChanged)
    Q_PROPERTY(OvEqView* bottomView READ bottomView WRITE setBottomView NOTIFY viewChanged)
    /// Frames this surface actually repainted in the last second.
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

    /// Everything the strokes depend on, as one number.
    quint64 signature() const;

    QTimer m_tick;
    int    m_revision = 0;
    QPointer<OvEqView> m_top, m_bottom;

    // ---- render-thread state --------------------------------------------
    QSGImageNode* m_node   = nullptr;
    QUuid         m_texKey;             // identifies the current texture
    quint64       m_builtKey = 0;
    QImage        m_img;                // the strokes, device resolution
    QImage        m_keep;               // outlives the texture upload
    QVector<QSGTexture*> m_retired;     // freed one frame later
    double        m_paintFps = 0.0;
    bool          m_dirty = true;
};
