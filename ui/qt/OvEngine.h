#pragma once
// ============================================================
//  OnyxTEON Qt/QML UI — engine facade handed to QML as `engine`.
//
//  Everything QML needs lives here: the parameter map (objects with
//  bindable norm/value/display), metering, spectrum, presets and the
//  global actions (INIT, load/save).
// ============================================================
#include <QObject>
#include <QVariantMap>
#include <QVariantList>
#include <QStringList>
#include <QTimer>
#include <memory>
#include <vector>

#include "OvSource.h"
#include "OvParam.h"

class OvEngine : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QVariantMap params READ params CONSTANT)
    Q_PROPERTY(int paramRevision READ paramRevision NOTIFY paramRevisionChanged)

    Q_PROPERTY(double inL  READ inL  NOTIFY metersChanged)
    Q_PROPERTY(double inR  READ inR  NOTIFY metersChanged)
    Q_PROPERTY(double outL READ outL NOTIFY metersChanged)
    Q_PROPERTY(double outR READ outR NOTIFY metersChanged)
    Q_PROPERTY(double harm READ harm NOTIFY metersChanged)

    Q_PROPERTY(QVariantList spectrum READ spectrum NOTIFY spectrumChanged)

    Q_PROPERTY(QVariantMap presets READ presets NOTIFY presetsChanged)
    Q_PROPERTY(QString presetName READ presetName NOTIFY presetNameChanged)

    Q_PROPERTY(int driveMode   READ driveMode   NOTIFY driveModeChanged)
    Q_PROPERTY(int limiterMode READ limiterMode NOTIFY driveModeChanged)

public:
    explicit OvEngine (std::unique_ptr<OvSource> source, QObject* parent = nullptr);
    ~OvEngine() override;

    QVariantMap params() const { return m_params; }
    int  paramRevision() const { return m_paramRevision; }
    double inL()  const { return m_meters.inL; }
    double inR()  const { return m_meters.inR; }
    double outL() const { return m_meters.outL; }
    double outR() const { return m_meters.outR; }
    double harm() const { return m_meters.harm; }
    QVariantList spectrum() const { return m_spectrum; }
    QVariantMap  presets() const { return m_presets; }
    QString presetName() const { return m_presetName; }
    int driveMode() const { return m_driveMode; }
    int limiterMode() const { return m_limiterMode; }

    // QML can only marshal pointer types it knows; OvParam* is not registered,
    // so the invokable accessor hands back a plain QObject* (the properties
    // still resolve through the meta-object).  C++ code uses typedParam().
    Q_INVOKABLE QObject* param (const QString& id) const;
    OvParam* typedParam (const QString& id) const;

    Q_INVOKABLE void initAll();
    Q_INVOKABLE void refreshPresets();
    Q_INVOKABLE void loadPreset (const QString& folder, const QString& name);
    Q_INVOKABLE void loadPresetDialog();
    Q_INVOKABLE void savePresetDialog();
    Q_INVOKABLE void setPresetName (const QString& name);
    Q_INVOKABLE QString spectrumAt (int i) const;

    OvSource& source() { return *m_source; }

signals:
    void paramRevisionChanged();
    void metersChanged();
    void spectrumChanged();
    void presetsChanged();
    void presetNameChanged();
    void driveModeChanged();

private slots:
    void tick();

private:
    std::unique_ptr<OvSource> m_source;
    // QML takes JavaScript ownership of any QObject* that comes back from an
    // invokable without a parent, so every OvParam is parented to the engine
    // and only referenced (never owned) here.
    QList<OvParam*>           m_paramList;
    QVariantMap  m_params;
    int          m_paramRevision = 0;
    OvMeters     m_meters;
    QVariantList m_spectrum;
    QVariantMap  m_presets;
    QString      m_presetName = QStringLiteral ("default");
    int          m_driveMode = 1, m_limiterMode = 2;
    int          m_tickCount = 0;
    QTimer       m_timer;
};
