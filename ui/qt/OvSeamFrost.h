#pragma once
// ============================================================
//  OnyxTEON Qt/QML UI — seam frost.
//
//  #hdr::before in the design is a 550x68 elliptical backdrop blur sitting on
//  the boundary between the two EQ halves, so its source spans both plots.
//  It therefore cannot be painted by either OvEqView on its own: this item
//  takes the rendered image of both and blurs them together.
//
//  Same construction as FrostEngine in web_fx/index.html: half-resolution
//  capture, 7-tap dual Gaussian, saturate(.85), the .018 veil, and the CSS
//  mask-image: radial-gradient(ellipse at center, #000 0%, #000 45%,
//  transparent 78%) applied as a destination-in fade.  All of it lives in
//  OvFrost, shared with the control pits so the three glass surfaces cannot
//  drift apart.
// ============================================================
#include <QQuickItem>
#include <QSGImageNode>
#include <QImage>
#include <chrono>

class OvEqView;

class OvSeamFrost : public QQuickItem
{
    Q_OBJECT

    Q_PROPERTY(OvEqView* topView    READ topView    WRITE setTopView    NOTIFY viewsChanged)
    Q_PROPERTY(OvEqView* bottomView READ bottomView WRITE setBottomView NOTIFY viewsChanged)

public:
    explicit OvSeamFrost (QQuickItem* parent = nullptr);

    OvEqView* topView() const { return m_top; }
    void setTopView (OvEqView*);

    OvEqView* bottomView() const { return m_bottom; }
    void setBottomView (OvEqView*);

protected:
    /// The RHI path.  renderGlass() does the work and caches its result; this
    /// just puts the finished picture in the scene graph as a texture.
    QSGNode* updatePaintNode (QSGNode* old, UpdatePaintNodeData*) override;

public:
    /// Build (or reuse) the composited glass.  Null when there is nothing.
    QImage renderGlass();

signals:
    void viewsChanged();

private:
    void attach (OvEqView*);
    void scheduleRebuild();

    OvEqView* m_top = nullptr;
    OvEqView* m_bottom = nullptr;
    bool m_scheduled = false;
    std::chrono::steady_clock::time_point m_lastRebuild {};
    QImage  m_cache;        // last composited glass
    QSGImageNode* m_node = nullptr;
    QString m_texKey;
    QImage  m_texKeep;      // outlives the texture upload
    quint64 m_cacheKey = 0;
};
