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
#include <QPainterPath>
#include <QTimer>

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

    OvEngine* m_engine = nullptr;

    struct Channel
    {
        QPainterPath path;
        double p = 0.0;         // displayed level 0..1
        double peak = 0.0;
        double holdUntil = 0.0; // seconds
    };

    Channel m_outL, m_outR, m_inL, m_inR;
    double  m_harmFrac = 0.0;
    double  m_clock = 0.0;
    QTimer  m_timer;
};
