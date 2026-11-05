#pragma once
// ============================================================
//  Overteon Qt/QML UI — EQ plot.
//
//  A direct port of the canvas EQ engine in web_fx/index.html:
//  same log frequency axis (5 Hz..24 kHz), same RBJ coefficient maths,
//  same colours, same node geometry and the same "overflow" pass that
//  lets the curve bleed into the header area (the frosted trapezoid
//  above/blow the plot shows a blurred copy of it).
// ============================================================
#include <QQuickPaintedItem>
#include <QTimer>
#include <QString>

class OvEngine;

class OvEqView : public QQuickPaintedItem
{
    Q_OBJECT

    Q_PROPERTY(OvEngine* engine READ engine WRITE setEngine NOTIFY engineChanged)
    Q_PROPERTY(QString prefix READ prefix WRITE setPrefix NOTIFY prefixChanged)
    Q_PROPERTY(QString accent READ accent WRITE setAccent NOTIFY accentChanged)
    Q_PROPERTY(bool headerTop READ headerTop WRITE setHeaderTop NOTIFY headerTopChanged)

public:
    explicit OvEqView (QQuickItem* parent = nullptr);

    OvEngine* engine() const { return m_engine; }
    void setEngine (OvEngine*);

    QString prefix() const { return m_prefix; }
    void setPrefix (const QString&);

    QString accent() const { return m_accent; }
    void setAccent (const QString&);

    bool headerTop() const { return m_headerTop; }
    void setHeaderTop (bool);

    void paint (QPainter*) override;

    // interaction
    void mousePressEvent (QMouseEvent*) override;
    void mouseMoveEvent (QMouseEvent*) override;
    void mouseReleaseEvent (QMouseEvent*) override;
    void mouseDoubleClickEvent (QMouseEvent*) override;
    void wheelEvent (QWheelEvent*) override;
    void hoverMoveEvent (QHoverEvent*) override;
    void hoverLeaveEvent (QHoverEvent*) override;

signals:
    void engineChanged();
    void prefixChanged();
    void accentChanged();
    void headerTopChanged();
    void menuRequested (int band, QPointF pos, QString currentType);
    void hoverInfo (QString text, QPointF pos);
    void hoverEnded();

private:
    struct Band
    {
        double freq = 1000.0, gain = 0.0, q = 0.707;
        int    type = 0;
        double x = 0.0, y = 0.0;
        bool   active = false;
    };

    // parameter helpers (band index 0..4)
    double  valueOf (const QString& id, double fallback) const;
    QString textOf (const QString& id) const;
    void    setValueOf (const QString& id, double v);
    void    setNormOf (const QString& id, double n);
    double  normOf (const QString& id) const;

    void gatherBands();
    void layoutGeometry (QRectF& plot, double& rangeDb) const;
    double toX (double hz, double w) const;
    double fromX (double x, double w) const;
    double toY (double db, double h, double rangeDb, double top, double bottom) const;
    double fromY (double y, double h, double rangeDb, double top, double bottom) const;

    static double bandMagDb (int type, double fs, double f, double q, double dB, double at);
    double bandMagAt (const Band&, double atHz) const;
    double totalMagAt (double atHz) const;
    double displayMult() const;

    int nodeAt (const QPointF& pos) const;

    OvEngine* m_engine = nullptr;
    QString   m_prefix = QStringLiteral ("sc");
    QString   m_accent = QStringLiteral ("#ff5c8a");
    bool      m_headerTop = true;

    Band   m_bands[5];
    QRectF m_plot;
    double m_rangeDb = 12.0;

    int    m_dragBand = -1;
    bool   m_dragGain = false;
    QPointF m_dragStart;
    double m_dragFreqNorm = 0.0, m_dragGainNorm = 0.0;
    int    m_hoverBand = -1;

    QTimer m_repaint;
};
