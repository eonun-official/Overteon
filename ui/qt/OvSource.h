#pragma once
// ============================================================
//  OnyxTEON Qt/QML UI — data source interface.
//
//  The QML front end never talks to JUCE directly: it sees an OvEngine,
//  which is fed by an OvSource.  Two implementations exist:
//
//    OvJuceSource  — wraps the live InjectorProcessor (both hosts + plugin)
//    OvMockSource  — static defaults + animated meters (preview app)
//
//  This header deliberately uses only the C++ standard library so the
//  preview application does not have to link JUCE.
// ============================================================
#include <string>
#include <vector>

struct OvParamSpec
{
    std::string id;                       // e.g. "sc3Freq"
    std::string label;                    // human readable, e.g. "SC EQ 3 Freq"
    double minValue    = 0.0;             // scaled range
    double maxValue    = 1.0;
    double defaultNorm = 0.0;             // default, already normalised
    std::vector<std::string> choices;     // non-empty -> choice parameter
    bool   isBool      = false;
};

struct OvMeters
{
    float inL  = -90.0f;
    float inR  = -90.0f;
    float outL = -90.0f;
    float outR = -90.0f;
    float harm = -100.0f;                 // harmonic/dry ratio, dB
};

class OvSource
{
public:
    virtual ~OvSource() = default;

    virtual int         count() const = 0;
    virtual OvParamSpec spec (int index) const = 0;

    virtual double getNorm (int index) const = 0;
    virtual void   setNorm (int index, double norm) = 0;

    /// "value -> normalised" and back, using the parameter's own skew
    /// (JUCE: convertTo0to1 uses pow(lin, skew); convertFrom0to1 uses
    /// pow(norm, 1/skew)).  Both directions must come from the same source or
    /// the UI would draw one frequency and write another.
    virtual double normForValue (int index, double value) const = 0;
    virtual double valueForNorm (int index, double norm) const = 0;

    /// Host-formatted display string (falls back to our own formatting).
    virtual std::string text (int index) const = 0;

    virtual void beginGesture (int index) = 0;
    virtual void endGesture   (int index) = 0;

    virtual void applyInit() = 0;

    // ---- presets -------------------------------------------------------
    virtual std::string presetListJson() = 0;                       // {"Factory":[...],"User":[...]}
    virtual bool loadPresetNamed (const std::string& folder,
                                  const std::string& name) = 0;
    virtual void openLoadDialog() = 0;
    virtual void openSaveDialog() = 0;

    // ---- metering / analysis -------------------------------------------
    virtual OvMeters meters() const = 0;
    /// 0..1 over a log 20 Hz..20 kHz axis.  TWO analysers:
    ///   spectrum()    the signal leaving the processor  (the PQ plot)
    ///   spectrumIn()  the signal entering it            (the SC plot)
    /// Returns FALSE when the analyser had no new block, in which case `dest`
    /// is left untouched and the caller must keep showing what it had.
    ///
    /// The taps are single-shot: a 2048-point FFT at 48 kHz completes every
    /// 43 ms, while a host may poll at 15 Hz (66 ms).  Filling `dest` with
    /// zeros on every poll that found nothing made the spectrum vanish and
    /// come back 60 % of the time (measured 171 hits / 249 misses in 7 s) —
    /// the "the meters flicker in and out" report.
    virtual bool     spectrum (float* dest, int count) = 0;
    virtual bool     spectrumIn (float* dest, int count) = 0;
    virtual double   sampleRate() const = 0;

    /// 0 = Classic, 1 = Harmonic, 2 = Schranz (drives the section dimming).
    virtual int driveModeIndex() const = 0;
};
