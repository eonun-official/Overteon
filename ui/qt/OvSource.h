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

    /// Rank-local "value -> normalised" using the parameter's own skew.
    virtual double normForValue (int index, double value) const = 0;

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
    virtual void     spectrum (float* dest, int count) = 0;    // 0..1, log 20 Hz..20 kHz
    virtual double   sampleRate() const = 0;

    /// 0 = Classic, 1 = Harmonic, 2 = Schranz (drives the section dimming).
    virtual int driveModeIndex() const = 0;
};
