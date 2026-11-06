#pragma once
// Test-only item: blurs a source image on the CPU with the same kernel the
// editor's glass uses, so a GPU implementation can be diffed against it.
#include <QQuickPaintedItem>
#include <QImage>

class OvBlurTest : public QQuickPaintedItem
{
    Q_OBJECT
    Q_PROPERTY(QString file READ file WRITE setFile NOTIFY changed)
    Q_PROPERTY(qreal sigma READ sigma WRITE setSigma NOTIFY changed)
    /// Crop origin in source pixels, so two instances can render the same photo
    /// region and be subtracted.
    Q_PROPERTY(QPointF cropOrigin READ cropOrigin WRITE setCropOrigin NOTIFY changed)

public:
    explicit OvBlurTest (QQuickItem* parent = nullptr);

    QString file() const { return m_file; }
    void setFile (const QString&);
    qreal sigma() const { return m_sigma; }
    void setSigma (qreal);

    QPointF cropOrigin() const { return m_crop; }
    void setCropOrigin (QPointF);

    void paint (QPainter*) override;

signals:
    void changed();

private:
    QString m_file;
    qreal   m_sigma = 4.0;
    QPointF m_crop;
    QImage  m_src;
    QImage  m_out;
    bool    m_dirty = true;
};
