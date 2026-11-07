#pragma once
// ============================================================
//  OvEqFog — the overflow fog for both EQ halves, in ONE surface.
//
//  The page does not clip the fog to a section: its two `.eqOverflow` canvases
//  live inside `#eqzone` with `inset:0` and are offset by the curve canvas's
//  position, so a curve leaving the top plot keeps going across the middle of
//  the UI and into the other section.  Splitting it in two (one fog per
//  OvEqView) cut it off after the ~24 px of padding the section has.
//
//  Painted on top of both plot views and inside the layer the glass samples,
//  which is the page's stacking order: the overflow canvases sit above the plot
//  canvases but below `#hdr`, whose backdrop-filter blurs what is behind it.
// ============================================================
#include <QQuickPaintedItem>
#include <QPointer>
#include <QTimer>

// The Q_PROPERTY(OvEqView*) below needs the type to be COMPLETE for moc's
// metatype registration, not just declared.
#include "OvEqView.h"

class OvEqFog : public QQuickPaintedItem
{
    Q_OBJECT
    Q_PROPERTY(OvEqView* topView    READ topView    WRITE setTopView    NOTIFY viewsChanged)
    Q_PROPERTY(OvEqView* bottomView READ bottomView WRITE setBottomView NOTIFY viewsChanged)

public:
    explicit OvEqFog (QQuickItem* parent = nullptr);

    OvEqView* topView() const    { return m_top; }
    OvEqView* bottomView() const { return m_bottom; }

    void setTopView (OvEqView* v);
    void setBottomView (OvEqView* v);

    void paint (QPainter* g) override;

signals:
    void viewsChanged();

private:
    void subscribe (OvEqView* v);
    void unsubscribe (OvEqView* v);

    QTimer m_tick;
    QPointer<OvEqView> m_top;
    QPointer<OvEqView> m_bottom;
};
