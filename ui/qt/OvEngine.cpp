#include "OvEngine.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <cmath>

OvEngine::OvEngine (std::unique_ptr<OvSource> source, QObject* parent)
    : QObject (parent), m_source (std::move (source))
{
    const int n = m_source->count();
    for (int i = 0; i < n; ++i)
    {
        auto* p = new OvParam (*m_source, i, this);   // parented: QML must not delete it
        m_params.insert (p->id(), QVariant::fromValue<QObject*> (p));
        m_paramList.push_back (p);
    }

    m_meters = m_source->meters();
    m_driveMode = m_source->driveModeIndex();
    m_spectrum = QVariantList();
    for (int i = 0; i < 128; ++i) m_spectrum << 0.0;

    refreshPresets();

    m_timer.setInterval (33);            // ~30 Hz: meters, params, spectrum
    connect (&m_timer, &QTimer::timeout, this, &OvEngine::tick);
    m_timer.start();
}

OvEngine::~OvEngine() = default;

QObject* OvEngine::param (const QString& id) const
{
    return typedParam (id);
}

OvParam* OvEngine::typedParam (const QString& id) const
{
    auto it = m_params.find (id);
    if (it == m_params.end()) return nullptr;
    return qobject_cast<OvParam*> (it->value<QObject*>());
}

void OvEngine::tick()
{
    bool any = false;
    for (auto* p : m_paramList)
    {
        const auto before = p->norm();
        p->poll();
        if (p->norm() != before) any = true;
    }
    if (any)
    {
        ++m_paramRevision;
        emit paramRevisionChanged();
    }

    const int mode = m_source->driveModeIndex();
    if (mode != m_driveMode)
    {
        m_driveMode = mode;
        if (auto* p = typedParam (QStringLiteral ("protectMode")))
        {
            m_limiterMode = (int) std::lround (p->norm() * 3.0);
        }
        emit driveModeChanged();
    }
    else if (auto* p = typedParam (QStringLiteral ("protectMode")))
    {
        const int lm = (int) std::lround (p->norm() * 3.0);
        if (lm != m_limiterMode) { m_limiterMode = lm; emit driveModeChanged(); }
    }

    const auto m = m_source->meters();
    if (std::abs (m.inL - m_meters.inL) > 0.01f || std::abs (m.inR - m_meters.inR) > 0.01f
        || std::abs (m.outL - m_meters.outL) > 0.01f || std::abs (m.outR - m_meters.outR) > 0.01f
        || std::abs (m.harm - m_meters.harm) > 0.01f)
    {
        m_meters = m;
        emit metersChanged();
    }

    if ((++m_tickCount % 2) == 0)
    {
        float spec[128];
        m_source->spectrum (spec, 128);
        for (int i = 0; i < 128; ++i)
        {
            const double v = (double) spec[i];
            if (std::abs (m_spectrum.at (i).toDouble() - v) > 0.0005)
                m_spectrum[i] = v;
        }
        emit spectrumChanged();
    }
}

void OvEngine::initAll()
{
    m_source->applyInit();
    for (auto* p : m_paramList) p->poll();
    ++m_paramRevision;
    emit paramRevisionChanged();
}

void OvEngine::refreshPresets()
{
    const auto json = QString::fromStdString (m_source->presetListJson());
    QVariantMap out;
    const auto doc = QJsonDocument::fromJson (json.toUtf8());
    if (doc.isObject())
    {
        const auto obj = doc.object();
        for (auto it = obj.begin(); it != obj.end(); ++it)
        {
            QStringList names;
            for (const auto& v : it.value().toArray())
                names << v.toString();
            out.insert (it.key(), names);
        }
    }
    if (out != m_presets)
    {
        m_presets = out;
        emit presetsChanged();
    }
}

void OvEngine::loadPreset (const QString& folder, const QString& name)
{
    if (m_source->loadPresetNamed (folder.toStdString(), name.toStdString()))
    {
        m_presetName = name;
        emit presetNameChanged();
        for (auto* p : m_paramList) p->poll();
        ++m_paramRevision;
        emit paramRevisionChanged();
    }
}

void OvEngine::loadPresetDialog() { m_source->openLoadDialog(); }
void OvEngine::savePresetDialog() { m_source->openSaveDialog(); }

void OvEngine::setPresetName (const QString& name)
{
    if (name != m_presetName) { m_presetName = name; emit presetNameChanged(); }
}

QString OvEngine::spectrumAt (int i) const
{
    if (i < 0 || i >= m_spectrum.size()) return QStringLiteral ("0");
    return QString::number (m_spectrum.at (i).toDouble(), 'f', 4);
}
