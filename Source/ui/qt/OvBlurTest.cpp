#include "OvBlurTest.h"
#include "OvFrost.h"

#include <QImageReader>
#include <QPainter>
#include <cstdio>
#include <QQuickWindow>

OvBlurTest::OvBlurTest (QQuickItem* parent) : QQuickPaintedItem (parent)
{
    setAntialiasing (false);
}

void OvBlurTest::setFile (const QString& f)
{
    if (f == m_file) return;
    m_file = f;
    QImageReader r (f);
    r.setAutoTransform (true);
    m_src = r.read();
    m_dirty = true;
    emit changed();
    update();
}

void OvBlurTest::setCropOrigin (QPointF p)
{
    if (p == m_crop) return;
    m_crop = p;
    m_dirty = true;
    emit changed();
    update();
}

void OvBlurTest::setSigma (qreal s)
{
    if (qFuzzyCompare (s, m_sigma)) return;
    m_sigma = s;
    m_dirty = true;
    emit changed();
    update();
}

void OvBlurTest::paint (QPainter* g)
{
    if (m_src.isNull() || width() < 2 || height() < 2) return;

    const int w = int (std::lround (width()));
    const int h = int (std::lround (height()));

    // Scale the photo to the item once, then blur it with OvFrost::dualGaussian
    // — the exact function the editor's glass calls.
    if (m_dirty || m_out.size() != QSize (w, h))
    {
        // Same crop as the GPU side: take a w x h window at m_crop.
        QImage window = m_src;
        if (! m_crop.isNull())
        {
            const QRect r (int (m_crop.x()), int (m_crop.y()),
                           std::min (w, m_src.width()), std::min (h, m_src.height()));
            window = m_src.copy (r.intersected (m_src.rect()));
        }
        const QImage scaled = window.scaled (w, h, Qt::IgnoreAspectRatio,
                                             Qt::SmoothTransformation)
                                  .convertToFormat (QImage::Format_ARGB32_Premultiplied);
        m_out = OvFrost::dualGaussian (scaled, m_sigma);
        m_dirty = false;
    }

    g->drawImage (QRectF (0, 0, w, h), m_out,
                  QRectF (0, 0, m_out.width(), m_out.height()));
}
