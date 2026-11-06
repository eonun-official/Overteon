#pragma once
// ============================================================
//  OnyxTEON Qt/QML UI — EQ plot.
//
//  A direct port of the canvas EQ engine in web_fx/index.html:
//  same log frequency axis (5 Hz..24 kHz), same RBJ coefficient maths,
//  same colours, same node geometry.
//
//  The frosted glass is painted here as well, exactly like the original's
//  FrostEngine: the plot is rendered into an image, that image is run through
//  a dual-Kawase pyramid, and the result is drawn clipped to the control-pit
//  trapezoid (with the CSS veil and the elliptical accent glow).  Doing it in
//  the painter rather than with a ShaderEffectSource keeps it deterministic —
//  sampling a QQuickPaintedItem through a texture gives garbage.
// ============================================================
#include <QQuickPaintedItem>
#include <QTimer>
#include <QString>
#include <QImage>
#include <QPainterPath>

class OvEngine;

class OvEqView : public QQuickPaintedItem
{
    Q_OBJECT

    Q_PROPERTY(OvEngine* engine READ engine WRITE setEngine NOTIFY engineChanged)
    Q_PROPERTY(QString prefix READ prefix WRITE setPrefix NOTIFY prefixChanged)
    Q_PROPERTY(QString accent READ accent WRITE setAccent NOTIFY accentChanged)
    Q_PROPERTY(bool headerTop READ headerTop WRITE setHeaderTop NOTIFY headerTopChanged)
    Q_PROPERTY(double glow READ glow WRITE setGlow NOTIFY glowChanged)

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

    /// 0..1 accent glow inside the trapezoid, driven by the meter level of
    /// this half of the signal path (.controlpit::before).
    double glow() const { return m_glow; }
    void setGlow (double);

    void paint (QPainter*) override;

    /// Centre of band `index` (0..4) in item coordinates, after refreshing
    /// the geometry from the current parameter values.
    Q_INVOKABLE QPointF nodePosition (int index);

    /// Debug HUD support (--hud).  Given a point in this item's own
    /// coordinates, report where every node really is and which one a click
    /// there would grab.  One line per node plus a verdict, so a human can see
    /// for themselves that the hit-test and the drawing agree.
    Q_INVOKABLE QString debugHit (const QPointF& pos) const;

    /// Node centres as "x,y" pairs, for the HUD.
    Q_INVOKABLE QString debugNodes() const;

    /// The last rendered plot image; OvSeamFrost blurs it together with the
    /// other half's.  Null until the view has painted once.
    QImage contentImage() const { return m_content; }

    /// Emitted right after m_content has been re-rendered.  OvSeamFrost listens
    /// to this: without it the seam glass never learns that the plots changed,
    /// so it paints once while m_content is still null and then sits blank
    /// forever.  (Measured: 0 px of the seam strip differed between a run with
    /// the glass and one with ONYXTEON_NOFROST, while the control pits — which
    /// repaint from their own paint() — differed by 15936 px.)

    /// Two-pass separable Gaussian (the "dual Gaussian": one horizontal, one
    /// vertical convolution), sigma in item pixels, plus CSS saturate().
    /// Shared with OvSeamFrost so both glass surfaces match.
    static QImage gaussianBlur (const QImage& src, double sigma, double sat);

    // interaction
    void mousePressEvent (QMouseEvent*) override;
    void mouseMoveEvent (QMouseEvent*) override;
    void mouseReleaseEvent (QMouseEvent*) override;
    void mouseUngrabEvent() override;
    void mouseDoubleClickEvent (QMouseEvent*) override;
    void wheelEvent (QWheelEvent*) override;
    void hoverMoveEvent (QHoverEvent*) override;
    void hoverLeaveEvent (QHoverEvent*) override;

signals:
    /// m_content was just re-rendered; the seam glass has to rebuild.
    void contentChanged();
    void engineChanged();
    void prefixChanged();
    void accentChanged();
    void headerTopChanged();
    void glowChanged();
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

    void refreshNodePositions();
    /// Finish and forget the current node drag (idempotent).
    void endDrag();
    double plotRangeDb() const;
    double inactiveOpacity() const;
    int nodeAt (const QPointF& pos) const;

    // ---- painting helpers ------------------------------------------
    void paintContent (QPainter* g, double w, double h,
                       double top, double bottom, const QColor& accent);
    /// Paint the frosted strip for this half of the EQ.
    void paintFrost (QPainter* g, const QImage& blurred,
                     double w, double h, const QColor& accent);

    OvEngine* m_engine = nullptr;
    QString   m_prefix = QStringLiteral ("sc");
    QString   m_accent = QStringLiteral ("#ff5c8a");
    bool      m_headerTop = true;
    double    m_glow = 0.0;

    Band   m_bands[5];
    QRectF m_plot;
    double m_rangeDb = 12.0;

    int    m_dragBand = -1;
    bool   m_dragMovesGain = false;
    QPointF m_grabOffset;
    int    m_hoverBand = -1;

    QImage m_content;        // the plot, rendered off-screen
    QImage m_blurred;        // its blurred copy (bar strip only)
    int    m_contentW = 0, m_contentH = 0;
    QTimer m_repaint;
    bool   m_dirty = true;      // repaint requested since the last frame
    double m_pendingGlow = -1000.0;
    quint64 m_spectrumStamp = 0, m_lastSpectrumStamp = 0;
};
