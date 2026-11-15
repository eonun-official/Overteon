#pragma once
// ============================================================
//  Overteon Qt/QML UI — engine facade handed to QML as `engine`.
//
//  Everything QML needs lives here: the parameter map (objects with
//  bindable norm/value/display), metering, spectrum, presets and the
//  global actions (INIT, load/save).
// ============================================================
#include <QObject>
#include <chrono>
#include <QVariantMap>
#include <QVariantList>
#include <QStringList>
#include <QTimer>
#include <memory>
#include <vector>

#include "OvSource.h"
#include "OvParam.h"
#include "OvBallistics.h"   // ovb::Fall — the light's ballistics

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
    /// The harmonic ratio before the DSP's smoothing.  Only the ring's Peak
    /// display reads it; everything else wants `harm`.
    Q_PROPERTY(double harmPeak READ harmPeak NOTIFY metersChanged)

    /// SMOOTHED level, 0..1, for anything that lights up rather than measures:
    /// the accent glow in the control pits and the meter pod's accent.
    ///
    /// These cannot come from `outL`/`inL` directly, and that is what was wrong:
    /// a QML binding on the raw dB value re-evaluates only when a new
    /// measurement arrives, so the glow jumped straight from one brightness to
    /// the next — it never appeared to move.  A light needs its own ballistics,
    /// the same ones the bars have, on a clock of its own.
    Q_PROPERTY(double outLight READ outLight NOTIFY lightChanged)
    Q_PROPERTY(double inLight  READ inLight  NOTIFY lightChanged)

    Q_PROPERTY(QVariantList spectrum READ spectrum NOTIFY spectrumChanged)

    Q_PROPERTY(QVariantMap presets READ presets NOTIFY presetsChanged)
    Q_PROPERTY(QString presetName READ presetName NOTIFY presetNameChanged)

    Q_PROPERTY(int driveMode   READ driveMode   NOTIFY driveModeChanged)
    Q_PROPERTY(int limiterMode READ limiterMode NOTIFY driveModeChanged)

    /// ---- display settings (the Settings panel) -------------------------
    //
    // Detection and interpolation are DISPLAY choices, not DSP ones: nothing
    // here changes a sample.  They are persisted so the panel does not reset
    // every time it is opened, but they are deliberately not parameters — a
    // parameter would claim they are part of the sound.
    //
    // detector: 0 = RMS (power mean), 1 = Peak (loudest bin, max-hold).
    // One per meter: the harmonic ring and the two spectrum sections measure
    // different things, and there is no reason to force them to agree.
    Q_PROPERTY(int harmDetector  READ harmDetector  WRITE setHarmDetector  NOTIFY settingsChanged)
    Q_PROPERTY(int upperDetector READ upperDetector WRITE setUpperDetector NOTIFY settingsChanged)
    Q_PROPERTY(int lowerDetector READ lowerDetector WRITE setLowerDetector NOTIFY settingsChanged)

    /// Interpolation, per meter: 0 = interpolated (instant attack, eased fall),
    /// 1 = raw (the last measurement verbatim, no ramp).
    //
    // Per meter and not global, because "raw" is a choice about a METER, in the
    // same category as its detector — a user comparing the DSP wants one meter
    // unvarnished while the others stay readable.  One switch for all four was
    // the wrong shape: it forced the same answer on meters measuring different
    // things.
    Q_PROPERTY(int harmInterp  READ harmInterp  WRITE setHarmInterp  NOTIFY settingsChanged)
    Q_PROPERTY(int upperInterp READ upperInterp WRITE setUpperInterp NOTIFY settingsChanged)
    Q_PROPERTY(int lowerInterp READ lowerInterp WRITE setLowerInterp NOTIFY settingsChanged)

    // ---- global controls (the deck) ------------------------------------
    //
    // Bypass is a real parameter and is forwarded to the host through
    // getBypassParameter(), so the host's own bypass button and ours are the
    // same switch and the meters keep running either way.  Kept as a property
    // here so QML can bind a toggle without knowing about the parameter map.
    Q_PROPERTY(bool bypassed READ bypassed WRITE setBypassed NOTIFY bypassChanged)

    /// Which snapshot is live: 0 = A, 1 = B.
    Q_PROPERTY(int abSlot READ abSlot WRITE setAbSlot NOTIFY abChanged)

    /// True when the OTHER slot holds something worth recalling.  A/B starts
    /// with only A recorded, and a switch to an empty slot would silently
    /// replace the sound with the default — so the switch is disabled until the
    /// other slot has been written.
    Q_PROPERTY(bool abOtherReady READ abOtherReady NOTIFY abChanged)


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
    double harmPeak() const { return m_meters.harmPeak; }

    /// 0..1, already ballistic-filtered.  Same mapping the design uses:
    /// (dB + 60) / 60 clamped, with a rate-derived release.
    double outLight() const { return m_outLight; }
    double inLight()  const { return m_inLight; }
    QVariantList spectrum() const { return m_spectrum; }

    /// The spectrum table itself, for C++ painters, so a plot does not pull the
    /// source a second time — the taps are single-shot and would return zeros.
    ///
    /// The engine is the ONLY consumer of the source: both taps are single-shot
    /// (reading clears the ready flag), so a painter that pulled the source
    /// itself in the same frame would get zeros.  OvEqView used to do exactly
    /// that, which is why the plot's spectrum fill could go blank.
    const QVariantList& spectrumTable()    const { return m_spectrum; }
    const QVariantList& inputSpectrumTable() const { return m_inSpectrum; }

    QVariantMap  presets() const { return m_presets; }
    QString presetName() const { return m_presetName; }
    int driveMode() const { return m_driveMode; }
    int limiterMode() const { return m_limiterMode; }

    int  harmDetector()  const { return m_harmDetector; }
    int  upperDetector() const { return m_upperDetector; }
    int  lowerDetector() const { return m_lowerDetector; }
    int  harmInterp()  const { return m_harmInterp; }
    int  upperInterp() const { return m_upperInterp; }
    int  lowerInterp() const { return m_lowerInterp; }

    void setHarmDetector (int);
    void setUpperDetector (int);
    void setLowerDetector (int);
    void setHarmInterp (int);
    void setUpperInterp (int);
    void setLowerInterp (int);

    bool bypassed() const { return m_bypassed; }
    void setBypassed (bool);

    int  abSlot() const { return m_abSlot; }
    void setAbSlot (int);
    bool abOtherReady() const { return m_ab[m_abSlot == 0 ? 1 : 0].valid; }
    /// Copy FROM `from` onto the other slot.  The source is named rather than
    /// implied: the A and B buttons are both on screen and either can be
    /// right-clicked, so "copy the live one" would be wrong half the time.
    ///
    /// Copies what is on screen when `from` is the live slot; otherwise copies
    /// the recording.  That distinction matters — a stale recording must not be
    /// what gets written when the user right-clicks the slot they are looking at.
    Q_INVOKABLE void abCopyFrom (int from);

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
    /// `scMoved` / `pqMoved` say which EQ half needs repainting; a global
    /// parameter sets both.
    void paramRevisionChanged (bool scMoved, bool pqMoved);
    void metersChanged();
    void lightChanged();
    void spectrumChanged();
    void presetsChanged();
    void presetNameChanged();
    void driveModeChanged();
    void settingsChanged();
    void bypassChanged();
    void abChanged();

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
    QVariantList m_spectrum;       // output analyser, 128 bins
    QVariantList m_inSpectrum;     // input analyser, 128 bins
    QVariantMap  m_presets;
    QString      m_presetName = QStringLiteral ("default");
    int          m_driveMode = 1, m_limiterMode = 2;

    // Display settings.  Saved under the display-settings key, not as
    // parameters — see the Q_PROPERTY block at the top of the class.
    int  m_harmDetector  = 0;      // 0 RMS, 1 Peak
    int  m_upperDetector = 0;
    int  m_lowerDetector = 0;
    int  m_harmInterp  = 0;        // 0 interpolated, 1 raw
    int  m_upperInterp = 0;
    int  m_lowerInterp = 0;

    // Global controls.  The two snapshots live beside the live parameter state:
    // "A" is not the currently-live slot's copy, it is a recording, so the user
    // can switch back and forth without the slot they left decaying underneath.
    bool     m_bypassed = false;
    int      m_abSlot = 0;
    OvSource::Snapshot m_ab[2];
    int          m_tickCount = 0;
    QTimer       m_timer;

    // ---- the light's own clock ------------------------------------------
    // A separate timer at a fixed display rate, not the data poll: a light has
    // to keep moving between measurements, and its ballistics must be derived
    // from the rate they actually run at (see OvEngine::lightTick).
    QTimer       m_lightTimer;
    double       m_outLight = 0.0;
    double       m_inLight  = 0.0;
    // The ballistic state behind those two, in dB (ui/qt/OvBallistics.h).
    ovb::Fall    m_outFall;
    ovb::Fall    m_inFall;
    std::chrono::steady_clock::time_point m_lightAt {};
    void lightTick();
};
