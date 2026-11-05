#pragma once
// ============================================================
//  OnyxTEON Qt/QML UI — seam frost.
//
//  #hdr::before in the design is a 550x68 elliptical backdrop blur sitting on
//  the boundary between the two EQ halves, so its source spans both plots.
//  It therefore cannot be painted by either OvEqView on its own: this item
//  takes the rendered image of both and blurs them together.
//
//  Same construction as FrostEngine in web_fx/index.html: dual-Kawase
//  pyramid, saturate(.85), the .018 veil, and the CSS
//  mask-image: radial-gradient(ellipse at center, #000 0%, #000 45%,
//  transparent 78%) applied as a destination-in fade.
// ============================================================
#include <QQuickPaintedItem>
#include <QImage>

class OvEqView;

class OvSeamFrost : public QQuickPaintedItem
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

    void paint (QPainter*) override;

signals:
    void viewsChanged();

private:
    OvEqView* m_top = nullptr;
    OvEqView* m_bottom = nullptr;
};
