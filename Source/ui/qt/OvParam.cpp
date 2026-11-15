#include "OvParam.h"

#include <cmath>
#include <algorithm>

namespace
{
    QString qs (const std::string& s) { return QString::fromStdString (s); }
}

OvParam::OvParam (OvSource& source, int index, QObject* parent)
    : QObject (parent), m_source (source), m_index (index)
{
    m_spec = m_source.spec (index);
    m_id    = qs (m_spec.id);
    m_label = qs (m_spec.label);
    for (const auto& c : m_spec.choices)
        m_choices << qs (c);

    cache();
    m_display = formatted (m_norm);
    m_haveCached = true;
}

QString OvParam::formatted (double n) const
{
    if (! m_spec.choices.empty())
    {
        const int idx = (int) std::lround (std::clamp (n, 0.0, 1.0)
                                           * (double) (m_spec.choices.size() - 1));
        return m_choices.value (std::clamp (idx, 0, (int) m_choices.size() - 1));
    }

    const auto host = m_source.text (m_index);
    if (! host.empty())
        return qs (host);

    return QString::number (m_source.valueForNorm (m_index, n), 'f', 2);
}

void OvParam::cache()
{
    m_norm  = std::clamp (m_source.getNorm (m_index), 0.0, 1.0);
    m_value = m_source.valueForNorm (m_index, m_norm);
}

void OvParam::poll()
{
    const double n = std::clamp (m_source.getNorm (m_index), 0.0, 1.0);
    const auto   t = formatted (n);

    bool changed = false;
    if (! m_haveCached || std::abs (n - m_norm) > 1.0e-6)
    {
        m_norm  = n;
        m_value = m_source.valueForNorm (m_index, n);
        m_haveCached = true;
        changed = true;
        emit valueChanged();
    }
    if (t != m_display)
    {
        m_display = t;
        emit displayChanged();
    }
    (void) changed;
}

void OvParam::setNorm (double n)
{
    n = std::clamp (n, 0.0, 1.0);
    m_source.setNorm (m_index, n);
    const auto t = formatted (n);
    if (std::abs (n - m_norm) > 1.0e-9)
    {
        m_norm  = n;
        m_value = m_source.valueForNorm (m_index, n);
        emit valueChanged();
    }
    if (t != m_display)
    {
        m_display = t;
        emit displayChanged();
    }
    emit edited();
}

void OvParam::setValue (double v)
{
    setNorm (m_source.normForValue (m_index, v));
}

void OvParam::resetToDefault()
{
    beginGesture();
    setNorm (m_spec.defaultNorm);
    endGesture();
}

void OvParam::beginGesture()
{
    m_source.beginGesture (m_index);
    if (! m_dragging) { m_dragging = true; emit draggingChanged(); }
}

void OvParam::endGesture()
{
    m_source.endGesture (m_index);
    if (m_dragging) { m_dragging = false; emit draggingChanged(); }
}
