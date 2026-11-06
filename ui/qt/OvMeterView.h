#pragma once
// ============================================================
//  OnyxTEON Qt/QML UI — header meter pod.
//
//  Port of the #hpod SVG in web_fx/index.html: four hooked paths
//  (OUT L/R in pink, IN L/R in teal) with a trailing peak-hold bar,
//  plus the harmonic-ratio ring in the middle.  Painted with QPainter
//  because the paths need per-fraction trimming, which the SVG dash
//  trick did in the browser.
// ============================================================
#include <QQuickPaintedItem>
#include <QImage>
#include <QSize>
#include <QVector>
#include <QPainterPath>
#include <QTimer>
#include <chrono>

class OvEngine;

class OvMeterView : public QQuickPaintedItem
{
    Q_OBJECT

    Q_PROPERTY(OvEngine* engine READ engine WRITE setEngine NOTIFY engineChanged)

public:
    explicit OvMeterView (QQuickItem* parent = nullptr);

    OvEngine* engine() const { return m_engine; }
    void setEngine (OvEngine*);

    void paint (QPainter*) override;

signals:
    void engineChanged();

private:
    void advance (double dtSeconds);

    /// The pod graphics.  `glowOnly` marks the pre-blur pass that produces the
    /// feGaussianBlur halo drawn underneath the sharp layer.
    /// Which parts of the pod to draw.
    ///
    /// The design filters two groups with DIFFERENT amounts:
    ///     <filter id="neon">     feGaussianBlur stdDeviation="1.7"  <- bars+dots
    ///     <filter id="harmGlow"> feGaussianBlur stdDeviation="2.5"  <- the ring
    /// each merged as <feMergeNode in="b"/><feMergeNode in="SourceGraphic"/>,
    /// i.e. a blurred copy underneath the sharp one.  So the halo is built as
    /// two separate blurred images, each from its own group.
    enum PodPass { Sharp, GlowBars, GlowRing };
    void drawPod (QPainter* g, PodPass pass);

    /// Everything the pod draws, quantised, so a repaint can be skipped when
    /// nothing moved.
    quint64 stateKey() const;


    OvEngine* m_engine = nullptr;

    // One channel of the composite meter.  Mirrors the design exactly:
    //   level     the instantaneous level  -> the bright bar   (#bOL …)
    //   peak      the peak hold            -> the dim trail    (#pOL …)
    //   holdUntil when the trail stops holding and starts falling
    //   the cap dot (#cOL …) rides the trail tip, i.e. the peak position
    struct Channel
    {
        QPainterPath path;
        // The hook path flattened once (QPainterPath::pointAtPercent is O(n),
        // and trimming three paths per bar per frame called it 1600 times a
        // frame).  Trimming now copies a prefix of this polyline.
        QVector<QPointF> poly;
        double level = 0.0;
        double peak = 0.0;
        double holdUntil = 0.0;   // seconds
    };

    Channel m_outL, m_outR, m_inL, m_inR;
    double  m_harmFrac = 0.0;
    double  m_clock = 0.0;
    std::chrono::steady_clock::time_point m_last {};
    QTimer  m_timer;
    // Two halos: the design blurs two groups by different amounts.
    QImage  m_barsLayer, m_barsBlur;
    QImage  m_ringLayer, m_ringBlur;
    // The glow only has to be re-blurred when the pod's own pixels change;
    // re-running a separable Gaussian over the whole pod every frame cost
    // ~15 fps.
    quint64 m_glowKey = 0;
    std::chrono::steady_clock::time_point m_glowBuiltAt {};
};
