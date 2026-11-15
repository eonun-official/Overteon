#pragma once
// ============================================================
//  Overteon Qt/QML UI — header meter pod.
//
//  Port of the #hpod SVG in web_fx/index.html: four hooked paths
//  (OUT L/R in pink, IN L/R in teal) with a trailing peak-hold bar,
//  plus the harmonic-ratio ring in the middle.  Painted with QPainter
//  because the paths need per-fraction trimming, which the SVG dash
//  trick did in the browser.
// ============================================================
#include <QQuickPaintedItem>
#include <QImage>

#include "OvSource.h"   // OvMeters, the latched target
#include "OvBallistics.h"   // ovb::Fall — the shared ballistics
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

    /// True while the pointer is over the pod.  Turns the monitor ring green and
    /// replaces the percentage read-out with the word SETTINGS — the pod is the
    /// entry to the display settings panel, and this is the whole of the
    /// affordance: the ring keeps drawing, so it still reads as a meter.
    Q_PROPERTY(bool settingsHover READ settingsHover WRITE setSettingsHover NOTIFY settingsHoverChanged)

public:
    explicit OvMeterView (QQuickItem* parent = nullptr);

    OvEngine* engine() const { return m_engine; }
    void setEngine (OvEngine*);

    bool settingsHover() const { return m_settingsHover; }
    void setSettingsHover (bool);

    void paint (QPainter*) override;

signals:
    void engineChanged();
    void settingsHoverChanged();

    /// Emitted when a fresh measurement has arrived; the view advances and
    /// repaints once for it.
    void advanceRequested();

private:
    void advance (double dtSeconds);

    bool m_settingsHover = false;

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


    /// The last measurement, latched when it arrived.  The display moves
    /// towards this at its own rate; see the note in the constructor.
    OvMeters m_target;

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

        /// The ballistic state behind `level` and behind `peak`, in dB.
        /// Both go through the same ovb::Fall as the ring, the light and the
        /// analyser, so the four meters on this UI cannot drift apart again.
        ovb::Fall levelFall;
        ovb::Fall peakFall;
    };

    Channel m_outL, m_outR, m_inL, m_inR;
    ovb::Fall m_harmFall;
    double  m_harmFrac = 0.0;

    // Display settings, mirrored from the engine each advance so this view does
    // not have to hold a second connection to the settings signal.
    bool    m_harmPeakMode = false;   // engine.harmDetector == 1
    bool    m_noInterp      = false;  // engine.meterInterpolation == false
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
