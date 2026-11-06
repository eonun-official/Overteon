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
#include <vector>
#include <chrono>
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

    /// The plot without this half's glass on it.  This is what the seam glass
    /// samples: the page's FrostEngine reads the raw canvases, so feeding it a
    /// picture that already contains the control-pit frost would apply the
    /// glass twice.
    QImage unfrostedImage() const { return m_unfrosted; }

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
public:
    struct Band
    {
        double freq = 1000.0, gain = 0.0, q = 0.707;
        int    type = 0;
        double x = 0.0, y = 0.0;
        bool   active = false;
    };
private:

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
    /// The glow bar in the control pit: per-frequency brightness across the
    /// trapezoid, low frequencies on the left.  Drawn UNDER the glass, so the
    /// frosted layer blooms it.
    void paintGlowBar (QPainter* g, double w, double h,
                       const QColor& accent, const float* spec);
    /// Blit the cached glow image through the trapezoid clip.
    void drawGlowImage (QPainter* g, double pitX, double barY,
                        double pitW, double barH);
    /// Paint the frosted strip for this half of the EQ.
    void paintFrost (QPainter* g, const QImage& blurred,
                     double w, double h, const QColor& accent);

    OvEngine* m_engine = nullptr;
    QString   m_prefix = QStringLiteral ("sc");
    QString   m_accent = QStringLiteral ("#ff5c8a");
    bool      m_headerTop = true;
    double    m_glow = 0.0;

    Band   m_bands[5];

    // ---- total-curve sample cache ---------------------------------------
    //  Drawing the curve needs totalMagAt() at every x, and so does the
    //  overflow fog: 1040 samples x 5 biquads, twice, per repaint.  The curve
    //  only changes when a band parameter does, while the spectrum changes
    //  every frame — so at 120 Hz almost every repaint was recomputing a curve
    //  that had not moved.  This caches the samples against a key made of the
    //  parameters and the geometry, which is cheap to compare (several < on
    //  doubles) and immune to missing an invalidation.
    struct CurveKey
    {
        double p[5][4] {};        // freq, gain, q, type per band
        double range = 0.0, w = 0.0, h = 0.0, top = 0.0, bottom = 0.0;
        int    step = 1;
        bool operator== (const CurveKey& o) const
        {
            if (range != o.range || w != o.w || h != o.h
                || top != o.top || bottom != o.bottom || step != o.step) return false;
            for (int i = 0; i < 5; ++i)
                for (int k = 0; k < 4; ++k)
                    if (p[i][k] != o.p[i][k]) return false;
            return true;
        }
    };

    CurveKey                m_curveKey;
    bool                    m_curveValid = false;
    std::vector<double>     m_curveY;     // item-space y per sample
    int                     m_curveStep = 1;

    /// y of the total response at each x, sampled every `m_curveStep` px.
    /// Rebuilt only when the key changes; returns a reference so the curve
    /// and the fog can share one evaluation.
    const std::vector<double>& totalCurveY (double w, double h, double top,
                                            double bottom, int step);
    QRectF m_plot;
    double m_rangeDb = 12.0;

    int    m_dragBand = -1;
    bool   m_dragMovesGain = false;
    QPointF m_grabOffset;
    int    m_hoverBand = -1;

    bool m_spectrumGate = false;
    std::chrono::steady_clock::time_point m_glowPainted {};
    QImage m_content;        // the plot, rendered off-screen
    QImage m_unfrosted;      // the plot before paintFrost() touched it
    std::chrono::steady_clock::time_point m_lastMoveTime;

    // ---- static background cache ----------------------------------------
    //  The grid and the ~30 frequency/dB labels never move unless a parameter
    //  or the geometry does, but they were redrawn into the plot image on
    //  every repaint — 30 antialiased glyph runs at 120 Hz, twice, for a
    //  picture that had not changed.  They are rendered once into m_bg and
    //  blitted instead.
    QImage m_bg;

    QImage  m_frostLayer;   // last composited control-pit glass
    quint64 m_frostKey = 0;

    // The glow bar only changes when the spectrum does (15 Hz) or when the
    // geometry does, but the plot repaints at up to 120 Hz.  Rebuilding a
    // 160x8 image with two exp() and a pow() per pixel on every paint measured
    // 0.15 ms a frame; the key keeps it to once per spectrum update.
    QImage  m_glowBar;
    quint64 m_glowKey = 0;
    std::chrono::steady_clock::time_point m_frostBuiltAt {};
    // Everything above the spectrum — the five band curves, the total curve and
    // its fill, the overflow fog and the five nodes.  None of it moves unless a
    // parameter or the geometry does, while the spectrum moves every poll.  It
    // is cached as a transparent overlay and blitted over the spectrum, which
    // is what takes a plot repaint from 3 ms down to about 1 ms.
    QImage  m_overlay;
    QString m_overlayKey;
    QString m_bgKey;         // geometry + range + accent + dim state
    const QImage& backgroundImage (double w, double h, double top, double bottom,
                                   const QColor& accent);

    /// Grid + ruler labels: static.
    const QImage& overlayImage (double w, double h, double top, double bottom,
                                const QColor& accent);
    /// Curves, fill, fog and nodes: everything above the spectrum.
    void paintOverlay (QPainter* g, double w, double h, double top, double bottom,
                       const QColor& accent);
    QString curveCacheKeyTag() const;
    QImage m_blurred;        // its blurred copy (bar strip only)
    int    m_contentW = 0, m_contentH = 0;
    QTimer m_repaint;
    bool   m_dirty = true;      // repaint requested since the last frame
    double m_pendingGlow = -1000.0;
    quint64 m_spectrumStamp = 0, m_lastSpectrumStamp = 0;

    /// Ballistics for the spectrum fill and the glow bar.
    ///
    /// The analyser is polled by the HOST, and in a plug-in that is slow:
    /// measured 11.8 Hz in jalv, so a new FFT arrives every ~85 ms while the
    /// scene renders at 60 fps.  Drawing the raw bins means each value is held
    /// for five frames and then snaps to an unrelated one — consecutive frames
    /// have nothing to do with each other, which is exactly how it was
    /// reported.
    ///
    /// So the bins are smoothed HERE, where frames happen: instant attack, and
    /// a release that decays towards the newest value with a time constant, so
    /// a jump is a movement across several frames instead of a cut.  This is
    /// what every hardware analyser does; the data is unchanged, only its
    /// presentation.
    float  m_specShown[128] = {};
    std::chrono::steady_clock::time_point m_specShownAt {};
};
