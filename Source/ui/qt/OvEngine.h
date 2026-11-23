#pragma once
// ============================================================
//  Overteon Qt/QML UI — engine facade handed to QML as `engine`.
//
//  Everything QML needs lives here: the parameter map (objects with
//  bindable norm/value/display), metering, spectrum, presets and the
//  global actions (INIT, load/save).
// ============================================================
#include <QObject>
#include <QPointF>
#include <QVariantMap>
#include <QVariantList>
#include <QStringList>
#include <QTimer>
#include <chrono>
#include <functional>
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
    /// The harmonic ratio before the DSP's smoothing.  Only the ring's Peak
    /// display reads it; everything else wants `harm`.
    Q_PROPERTY(double harmPeak READ harmPeak NOTIFY metersChanged)

    /// Direct 0..1 light level from the louder L/R meter.  The glass squares
    /// this value and applies the reference's 100 ms linear opacity transition.
    Q_PROPERTY(double outLight READ outLight NOTIFY lightChanged)
    Q_PROPERTY(double inLight  READ inLight  NOTIFY lightChanged)

    Q_PROPERTY(QVariantList spectrum READ spectrum NOTIFY spectrumChanged)

    Q_PROPERTY(QVariantMap presets READ presets NOTIFY presetsChanged)
    Q_PROPERTY(QString presetName READ presetName NOTIFY presetNameChanged)

    Q_PROPERTY(int driveMode   READ driveMode   NOTIFY driveModeChanged)
    Q_PROPERTY(int limiterMode READ limiterMode NOTIFY driveModeChanged)

    /// ---- display settings (the Settings panel) -------------------------
    //
    // MODE is a DISPLAY choice, not a DSP one: nothing here changes a sample.
    // It is persisted so the panel does not reset every time it is opened, but
    // it is deliberately not a parameter — a parameter would claim it is part
    // of the sound.
    //
    // mode: 0 = RMS, 1 = TRANSIENT.  These are the only two choices now.
    //
    //   spectra  RMS uses the power-domain mean AND the eased display
    //            (interpolated); TRANSIENT uses the loudest bin AND the raw
    //            display (no time smoothing, straight polyline).  The two were
    //            independent switches; they are now one, because "mean + raw"
    //            and "peak + eased" were combinations nobody asked for.
    //
    //   ring     the display no longer smooths at all, so the mode only picks
    //            the MEASUREMENT: RMS = the DSP's 131 ms ratio, TRANSIENT =
    //            this block's own ratio.
    //
    // One per meter: the ring and the two spectrum sections measure different
    // things, and there is no reason to force them to agree.
    Q_PROPERTY(int harmMode  READ harmMode  WRITE setHarmMode  NOTIFY settingsChanged)
    Q_PROPERTY(int upperMode READ upperMode WRITE setUpperMode NOTIFY settingsChanged)
    Q_PROPERTY(int lowerMode READ lowerMode WRITE setLowerMode NOTIFY settingsChanged)

    /// Full-circle range of the limiter GR arc, in dB: 6 / 12 / 24 / 48.
    /// Display only; the ring's scale labels follow it.
    Q_PROPERTY(int grRange READ grRange WRITE setGrRange NOTIFY settingsChanged)

    /// 复合仪表亮条的回落方式：0 = INSTANT（直接跟随块采样峰值，停止播放立即归零），
    /// 1 = VU（按 VU 表标准 300 ms 响应，99 % 在 300 ms 内到达）。
    Q_PROPERTY(int meterRelease READ meterRelease WRITE setMeterRelease NOTIFY settingsChanged)

    /// True when the editor is allowed to resize its window.  The QML corner
    /// grip is only shown when this is set, so a fixed-size build cannot offer
    /// a handle that does nothing.
    Q_PROPERTY(bool windowResizable READ windowResizable NOTIFY windowResizableChanged)

    /// The DEFAULT window-size preset, for the Settings panel's lit state.
    /// `windowDefaultSaved` is false until SAVE AS DEFAULT has been used; the
    /// DEFAULT button then behaves exactly like ORIGINAL.
    Q_PROPERTY(int  defaultWindowW READ defaultWindowW NOTIFY windowSettingsChanged)
    Q_PROPERTY(int  defaultWindowH READ defaultWindowH NOTIFY windowSettingsChanged)
    Q_PROPERTY(bool windowDefaultSaved READ windowDefaultSaved NOTIFY windowSettingsChanged)

    // ---- global controls (the deck) ------------------------------------
    //
    // Bypass is a real parameter and is forwarded to the host through
    // getBypassParameter(), so the host's own bypass button and ours are the
    // same switch and the meters keep running either way.  Kept as a property
    // here so QML can bind a toggle without knowing about the parameter map.
    Q_PROPERTY(bool bypassed READ bypassed WRITE setBypassed NOTIFY bypassChanged)

    /// Which snapshot is live: 0 = A, 1 = B.
    Q_PROPERTY(int abSlot READ abSlot WRITE setAbSlot NOTIFY abChanged)

    /// True when the OTHER slot holds a recording.  Both slots are seeded from
    /// the initial parameter state when the engine is constructed, so this is
    /// true from the first frame; kept for QML bindings.
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

    /// 0..1, using the design's (dB + 60) / 60 mapping without extra smoothing.
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

    int  harmMode()  const { return m_harmMode; }
    int  upperMode() const { return m_upperMode; }
    int  lowerMode() const { return m_lowerMode; }
    int  grRange() const { return m_grRange; }
    int  meterRelease() const { return m_meterRelease; }

    void setHarmMode (int);
    void setUpperMode (int);
    void setLowerMode (int);
    void setGrRange (int);
    void setMeterRelease (int);

    bool windowResizable() const { return m_windowResizable; }
    void setWindowResizable (bool on)
    {
        if (on == m_windowResizable) return;
        m_windowResizable = on;
        emit windowResizableChanged();
    }

    /// The cursor position in screen (logical) pixels.
    ///
    /// The QML resize grip uses this instead of MouseArea coordinates because
    /// the window is moving under the pointer while it drags: local
    /// coordinates would feed the resize back into the delta and jitter.
    Q_INVOKABLE QPointF cursorGlobalPos() const;

    /// Ask the editor to resize the WINDOW (not the scene inside it).  The
    /// editor forwards it to the host's own resize path.
    Q_INVOKABLE void requestWindowResize (int w, int h);

    /// The three WINDOW SIZE actions in the Settings panel.
    ///
    /// ORIGINAL is the fixed 1040x772 design.  DEFAULT is the preset written
    /// by SAVE AS DEFAULT; until one exists it behaves exactly like ORIGINAL.
    /// SAVE AS DEFAULT only records the CURRENT size — it does not become a
    /// "always open at this size" rule, because the last-used size already is
    /// what the window opens at.
    Q_INVOKABLE void windowSizeOriginal();
    Q_INVOKABLE void windowSizeDefault();
    Q_INVOKABLE void windowSizeSaveDefault();

    /// DIAGNOSTICS: upload this session's runtime log to a GitHub issue.
    /// Installed by the editor only in -DOVERTEON_FILE_LOG=ON builds; the
    /// Settings panel hides the row when diagnosticsAvailable() is false.
    Q_INVOKABLE void reportIssue();
    Q_INVOKABLE bool diagnosticsAvailable() const;

    int  defaultWindowW() const { return m_defaultWindowW; }
    int  defaultWindowH() const { return m_defaultWindowH; }
    bool windowDefaultSaved() const { return m_defaultWindowW > 0 && m_defaultWindowH > 0; }
    void setWindowDefaultSize (int w, int h)
    {
        if (w == m_defaultWindowW && h == m_defaultWindowH) return;
        m_defaultWindowW = w;
        m_defaultWindowH = h;
        emit windowSettingsChanged();
    }

    /// Installed by the editor.  Empty in a fixed-size build.  The size
    /// functions are separate from the grip's resize because ORIGINAL and
    /// DEFAULT need the editor's DPI/screen clamping, not QML pixels.
    std::function<void (int, int)> windowResizeRequest;
    std::function<void()> windowSizeOriginalRequest;
    std::function<void()> windowSizeDefaultRequest;
    std::function<void()> windowSizeSaveDefaultRequest;
    std::function<void()> reportIssueRequest;

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
    void windowResizableChanged();
    void windowSettingsChanged();

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
    int  m_harmMode  = 1;          // 0 RMS, 1 TRANSIENT
    int  m_upperMode = 1;
    int  m_lowerMode = 1;
    int  m_grRange   = 12;         // dB for a full limiter ring
    int  m_meterRelease = 0;      // 0 instant, 1 VU 300 ms
    bool m_windowResizable = false;
    int  m_defaultWindowW = 0, m_defaultWindowH = 0;

    /// When each analyser last delivered a block.  A stale table is silence,
    /// not the previous picture — see the note in tick().
    std::chrono::steady_clock::time_point m_inSpecAt {}, m_outSpecAt {};
    /// When the analyser tables were last re-read.  A member, not a static:
    /// several editor instances exist in one host process and a shared clock
    /// would let one instance starve the others.
    std::chrono::steady_clock::time_point m_lastSpecScan {};

    // Global controls.  The two snapshots live beside the live parameter state:
    // "A" is not the currently-live slot's copy, it is a recording, so the user
    // can switch back and forth without the slot they left decaying underneath.
    bool     m_bypassed = false;
    int      m_abSlot = 0;
    OvSource::Snapshot m_ab[2];
    QTimer       m_timer;

    // Display-rate sampling for the glass light; QML supplies its transition.
    QTimer       m_lightTimer;
    double       m_outLight = 0.0;
    double       m_inLight  = 0.0;
    void lightTick();
};
