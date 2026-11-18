#include "OverteonProcessor.h"
#include "Oversampling.h"
#include "OvBallistics.h"   // ovb::Rms / ovb::onePoleCoef — the meters' detector

namespace
{
    /// Per-call decay coefficient for a meter that must fall at a fixed RATE.
    ///
    /// Kept for the values still stored as linear gain with an exponential
    /// release (the harmonic ratio).  The LEVEL meters no longer use it: they
    /// are true RMS (ovb::Rms in ui/qt/OvBallistics.h) and their fall comes out
    /// of the integration constant, so there is no coefficient to tune.
    inline float meterDecay (int numSamples, double sampleRate, double tauSeconds)
    {
        if (sampleRate <= 0.0 || numSamples <= 0) return 1.0f;
        const double dt = double (numSamples) / sampleRate;
        return (float) std::exp (-dt / std::max (1e-4, tauSeconds));
    }

    /// RMS integration time for the four level meters.
    ///
    /// 300 ms is the VU convention (IEC 60268-17 integrates over 300 ms) and it
    /// is what makes the fall read as RMS rather than as a peak release: with
    /// tau = 0.3 s the detector falls at 4.343/0.3 = 14.5 dB/s once the signal
    /// stops, which is the natural slope of a windowed RMS — no extra release
    /// coefficient is applied on top of it.
    constexpr double kMeterRmsTau = 0.300;

    /// The harmonic ratio's smoothing, as a TIME constant.
    ///
    /// It used to be `+= 0.15 * (x - v)` per BLOCK, which is 131 ms at
    /// 48 kHz / 1024 and 33 ms at 48 kHz / 256 — the same block-rate dependence
    /// the level meters were already fixed for.  0.15 at 21.3 ms is tau 131 ms,
    /// so that is what it is now, at every buffer size.
    constexpr double kHarmTau = 0.105;    // 131 ms x 1.25: same 25 % faster ring
}



#if OVERTEON_QT_UI
 #include "OvQmlEditor.h"
#else
 #include "OverteonWebEditor.h"
#endif

using FP = juce::AudioParameterFloat;
using CP = juce::AudioParameterChoice;

// EQ display-range multipliers: the Gain parameters stay +/-12 dB so old
// presets map 1:1; the selector scales the effective gain in the DSP.
// SC: +/-12/24/48/90 dB -> x1/x2/x4/x7.5   Post: +/-3/6/12/30 -> x0.25/0.5/1/2.5
static constexpr double kScRangeMult[4] = { 1.0, 2.0, 4.0, 7.5 };
static constexpr double kPqRangeMult[4] = { 0.25, 0.5, 1.0, 2.5 };

static juce::NormalisableRange<float> timeRange (float lo, float hi, float centre)
{
    juce::NormalisableRange<float> r (lo, hi);
    r.setSkewForCentre (centre);
    return r;
}

// Configure a biquad (and optional 2nd-order stage for 24 dB cuts) for one EQ
// node from the unified 8-way type code. Returns true if a 2nd stage is used.
// invGain (if non-null) receives the inverse-EQ gain for de-emphasis, and
// *invertible tells the caller whether this node has a bounded inverse.
//  type: 0 Bell 1 Notch 2 LowShelf 3 HighShelf
//        4 LowCut12 5 LowCut24 6 HighCut12 7 HighCut24
static bool configBand (int type, float fs, float f0, float q0, float gq,
                        omega::Biquad& p, omega::Biquad& p2,
                        bool* invertible, omega::Biquad* pInv, float invAmt,
                        bool on = true)
{
    bool s2 = false;
    bool inv = false;

    if (! on)
    {
        // Muted: exact pass-through in the first section, no second section and
        // nothing to invert.  The biquad STAYS in the chain — see
        // Biquad::setIdentity for why removing it is the wrong idea.
        p.setIdentity();
        if (invertible) *invertible = false;
        return false;
    }

    switch (type)
    {
        case 0:  p.setPeak      (fs, f0, q0, gq); inv = true; break;
        case 1:  p.setNotch     (fs, f0, q0);                 break;
        case 2:  p.setLowShelf  (fs, f0, q0, gq); inv = true; break;
        case 3:  p.setHighShelf (fs, f0, q0, gq); inv = true; break;
        case 4:  p.setHighpass  (fs, f0, q0);                 break;  // low cut 12
        case 5:  p.setHighpass  (fs, f0, q0);
                 p2.setHighpass (fs, f0, 0.707f); s2 = true;  break;  // low cut 24
        case 6:  p.setLowpass   (fs, f0, q0);                 break;  // high cut 12
        case 7:  p.setLowpass   (fs, f0, q0);
                 p2.setLowpass  (fs, f0, 0.707f); s2 = true;  break;  // high cut 24
        default: p.setPeak      (fs, f0, q0, gq); inv = true; break;
    }
    if (invertible) *invertible = inv;
    if (inv && pInv != nullptr)
    {
        const float gi = -gq * invAmt;
        if      (type == 2) pInv->setLowShelf  (fs, f0, q0, gi);
        else if (type == 3) pInv->setHighShelf (fs, f0, q0, gi);
        else                pInv->setPeak      (fs, f0, q0, gi);
    }
    return s2;
}

juce::AudioProcessorValueTreeState::ParameterLayout OverteonProcessor::createLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> ps;

    // Whole plug-in bypass as a real parameter, so a host sees it and can
    // automate it.  JUCE also consults getBypassParameter(), which is wired to
    // this below — that is what lets the host's own bypass button work.
    ps.push_back (std::make_unique<juce::AudioParameterBool> (
                      juce::ParameterID { "bypass", 1 }, "Bypass", false));

    ps.push_back (std::make_unique<FP> (juce::ParameterID { "drive", 1 }, "Drive",
                      timeRange (0.0f, 10.0f, 2.0f), 0.0f));
    ps.push_back (std::make_unique<FP> (juce::ParameterID { "mix", 1 }, "Harmonics Mix",
                      juce::NormalisableRange<float> (0.0f, 1.0f), 1.0f));
    ps.push_back (std::make_unique<CP> (juce::ParameterID { "driveType", 1 }, "Drive Type",
                      juce::StringArray { "Tanh", "Tube", "Tape" }, 0));
    ps.push_back (std::make_unique<CP> (juce::ParameterID { "driveMode", 1 }, "Drive Mode",
                      juce::StringArray { "Classic", "Harmonic", "Schranz" }, 1));
    ps.push_back (std::make_unique<CP> (juce::ParameterID { "osFactor", 1 }, "Oversampling",
                      juce::StringArray { "2x", "4x", "8x", "16x", "32x" }, 1));
    ps.push_back (std::make_unique<FP> (juce::ParameterID { "inGain", 1 }, "Input Gain",
                      juce::NormalisableRange<float> (-24.0f, 24.0f), 0.0f));
    ps.push_back (std::make_unique<FP> (juce::ParameterID { "outGain", 1 }, "Output Gain",
                      juce::NormalisableRange<float> (-24.0f, 24.0f), 0.0f));
    ps.push_back (std::make_unique<FP> (juce::ParameterID { "deEmph", 1 }, "De-Emphasis",
                      juce::NormalisableRange<float> (0.0f, 1.0f), 1.0f));
    ps.push_back (std::make_unique<FP> (juce::ParameterID { "harmGain", 1 }, "Harmonics Gain",
                      juce::NormalisableRange<float> (-12.0f, 24.0f), 0.0f));
    ps.push_back (std::make_unique<FP> (juce::ParameterID { "airProtect", 1 }, "Air Protect",
                      timeRange (2000.0f, 20000.0f, 9000.0f), 20000.0f));
    ps.push_back (std::make_unique<FP> (juce::ParameterID { "evenOdd", 1 }, "Even / Odd",
                      juce::NormalisableRange<float> (-1.0f, 1.0f), 0.0f));
    ps.push_back (std::make_unique<CP> (juce::ParameterID { "msMode", 1 }, "Stereo Mode",
                      juce::StringArray { "Stereo", "Mid", "Side", "M/S" }, 0));
    ps.push_back (std::make_unique<juce::AudioParameterBool> (
                      juce::ParameterID { "autoGain", 1 }, "Auto Gain", false));
    ps.push_back (std::make_unique<juce::AudioParameterBool> (
                      juce::ParameterID { "harmSolo", 1 }, "Solo Harmonics", false));

    // Every band on BOTH EQs is now a fully general node: any of the 8 filter
    // types, any frequency across the whole band. Default layout still spreads
    // the five nodes low->high with a tasteful post-EQ 10 k presence bump, but
    // the user can retype/move any node freely (Pro-Q style), within the fixed
    // pool of 5 slots per EQ that the DSP provides.
    const float defF[5] = { 100.0f, 250.0f, 1200.0f, 4000.0f, 8500.0f };
    // default type per slot: slot0 LowShelf, slot4 HighShelf, middle Bell
    const int   defT[5] = { 2, 0, 0, 0, 3 };   // 0 Bell,2 LowShelf,3 HighShelf
    const juce::StringArray eqTypes {
        "Bell", "Notch", "Low Shelf", "High Shelf",
        "Low Cut 12", "Low Cut 24", "High Cut 12", "High Cut 24" };
    for (auto* pre : { "sc", "pq" })
        for (int b = 0; b < 5; ++b)
        {
            const auto id   = juce::String (pre) + juce::String (b + 1);
            const auto name = (juce::String (pre) == "sc" ? "SC EQ " : "Post EQ ")
                            + juce::String (b + 1) + " ";
            const bool bump = (juce::String (pre) == "pq" && b == 3);
            // Every node spans the audible band.  The UI axis ends at 20 kHz,
            // so the parameter stops there too: nothing can be dragged or
            // automated outside the part of the curve the user can see.  The
            // oversampling / signal path are untouched.
            ps.push_back (std::make_unique<FP> (juce::ParameterID { id + "Freq", 1 },
                              name + "Freq", timeRange (5.0f, 20000.0f, 800.0f),
                              bump ? 10000.0f : defF[b]));
            ps.push_back (std::make_unique<FP> (juce::ParameterID { id + "Gain", 1 },
                              name + "Gain",
                              juce::NormalisableRange<float> (-12.0f, 12.0f),
                              0.0f));
            ps.push_back (std::make_unique<FP> (juce::ParameterID { id + "Q", 1 },
                              name + "Q", timeRange (0.1f, 6.0f, 0.71f),
                              bump ? 0.9f : 0.707f));
            // unified 8-way type on EVERY band of EVERY EQ
            ps.push_back (std::make_unique<CP> (juce::ParameterID { id + "Type", 1 },
                              name + "Type", eqTypes, defT[b]));
        }

    // Display-range selectors (appended LAST so every existing parameter
    // index stays unchanged -> old presets/automation keep their meaning).
    ps.push_back (std::make_unique<CP> (juce::ParameterID { "scRange", 1 }, "SC EQ Range",
                      juce::StringArray { "+/-12 dB", "+/-24 dB", "+/-48 dB", "+/-90 dB" }, 0));
    ps.push_back (std::make_unique<CP> (juce::ParameterID { "pqRange", 1 }, "Post EQ Range",
                      juce::StringArray { "+/-3 dB", "+/-6 dB", "+/-12 dB", "+/-30 dB" }, 2));

    // Output limiter: bypass, clipper, and two lookahead limiter modes.
    ps.push_back (std::make_unique<CP> (juce::ParameterID { "protectMode", 1 }, "Limiter Mode",
                      juce::StringArray { "Off", "Clip", "Modern", "True Peak" }, 2));
    ps.push_back (std::make_unique<CP> (juce::ParameterID { "limiterOs", 1 }, "Limiter Oversampling",
                      juce::StringArray { "2x", "4x", "8x", "16x" }, 1));

    // Per-band on/off, appended AFTER everything that already existed -- the
    // same rule the range selectors follow.  Parameter indices are part of the
    // plug-in's interface: putting these beside each band's Type would have
    // shifted scRange, pqRange, protectMode and limiterOs, silently
    // re-pointing any index-based host automation.
    //
    // A separate switch, not a "mute" filter type: a muted band keeps its
    // frequency, gain, Q and type, so bringing it back is one click instead of
    // a rebuild.
    for (auto* pre : { "sc", "pq" })
        for (int b = 0; b < 5; ++b)
        {
            const auto id   = juce::String (pre) + juce::String (b + 1);
            const auto name = (juce::String (pre) == "sc" ? "SC EQ " : "Post EQ ")
                            + juce::String (b + 1) + " ";
            ps.push_back (std::make_unique<juce::AudioParameterBool> (
                              juce::ParameterID { id + "On", 1 },
                              name + "On", true));
        }

    return { ps.begin(), ps.end() };
}

OverteonProcessor::OverteonProcessor()
    : juce::AudioProcessor (BusesProperties()
          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "HINJ", createLayout())
{
    cacheParams();

    // Oversampling is a GLOBAL preference, not preset data: apply the stored
    // choice (shared across instances and sessions) on top of the default.
    // setStateInformation() preserves it across preset/session loads too.
    if (const int stored = loadGlobalOsFactor(); stored >= 0)
        apvts.getParameterAsValue ("osFactor") = stored;
}

juce::File OverteonProcessor::getGlobalSettingsFile()
{
    // The product has been called Overteon throughout its life; the file name
    // still said HarmonicInjector, the working title.  Read the new name and
    // fall back to the old one once, so an existing pre-rename install keeps
    // its oversampling choice instead of silently resetting to the default.
    const auto dir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                         .getChildFile ("Eonun");

    const auto current = dir.getChildFile ("Overteon.settings");
    if (current.existsAsFile())
        return current;

    const auto legacy = dir.getChildFile ("HarmonicInjector.settings");
    if (legacy.existsAsFile())
        return legacy;

    return current;
}

int OverteonProcessor::loadGlobalOsFactor() const
{
    const auto f = getGlobalSettingsFile();
    if (! f.existsAsFile()) return -1;
    if (auto xml = juce::XmlDocument::parse (f))
        if (xml->hasTagName ("HINJGLOBAL"))
        {
            const int v = xml->getIntAttribute ("osFactor", -1);
            if (v >= 0 && v <= 4) return v;
        }
    return -1;
}

namespace
{
    /// Read the global settings file for a read-modify-write.
    ///
    /// The old save path built a fresh element with only `osFactor` in it, so
    /// adding a second preference (the editor size) would have had the two
    /// writers erasing each other.  Every writer now starts from the file.
    std::unique_ptr<juce::XmlElement> readGlobalSettings (const juce::File& f)
    {
        if (auto xml = juce::XmlDocument::parse (f))
            if (xml->hasTagName ("HINJGLOBAL"))
                return xml;
        return std::make_unique<juce::XmlElement> ("HINJGLOBAL");
    }
}

void OverteonProcessor::saveGlobalOsFactor (int idx) const
{
    // Rare user gesture (oversampling switch): a tiny XML write is fine here.
    const auto f = getGlobalSettingsFile();
    auto x = readGlobalSettings (f);
    x->setAttribute ("osFactor", idx);
    f.getParentDirectory().createDirectory();
    x->writeTo (f);
}

juce::Point<int> OverteonProcessor::loadEditorSize() const
{
    const auto f = getGlobalSettingsFile();
    if (! f.existsAsFile()) return {};
    if (auto xml = juce::XmlDocument::parse (f))
        if (xml->hasTagName ("HINJGLOBAL"))
        {
            const int w = xml->getIntAttribute ("editorW", 0);
            const int h = xml->getIntAttribute ("editorH", 0);
            // The editor clamps to its own minimum; anything smaller is a
            // corrupt or half-written value, not a size to restore.
            if (w >= 460 && h >= 341) return { w, h };
        }
    return {};
}

void OverteonProcessor::saveEditorSize (int w, int h) const
{
    if (w <= 0 || h <= 0) return;
    const auto f = getGlobalSettingsFile();
    auto xml = readGlobalSettings (f);
    xml->setAttribute ("editorW", w);
    xml->setAttribute ("editorH", h);
    f.getParentDirectory().createDirectory();
    xml->writeTo (f);
}

juce::Point<int> OverteonProcessor::loadDefaultEditorSize() const
{
    const auto f = getGlobalSettingsFile();
    if (! f.existsAsFile()) return {};
    if (auto xml = juce::XmlDocument::parse (f))
        if (xml->hasTagName ("HINJGLOBAL"))
        {
            const int w = xml->getIntAttribute ("defaultW", 0);
            const int h = xml->getIntAttribute ("defaultH", 0);
            if (w >= 460 && h >= 341) return { w, h };
        }
    return {};
}

void OverteonProcessor::saveDefaultEditorSize (int w, int h) const
{
    if (w <= 0 || h <= 0) return;
    const auto f = getGlobalSettingsFile();
    auto xml = readGlobalSettings (f);
    xml->setAttribute ("defaultW", w);
    xml->setAttribute ("defaultH", h);
    f.getParentDirectory().createDirectory();
    xml->writeTo (f);
}

int OverteonProcessor::loadMeterMode (const juce::String& key, int fallback) const
{
    const auto f = getGlobalSettingsFile();
    if (! f.existsAsFile()) return fallback;
    if (auto xml = juce::XmlDocument::parse (f))
        if (xml->hasTagName ("HINJGLOBAL"))
        {
            const int v = xml->getIntAttribute (key, fallback);
            if (v == 0 || v == 1) return v;
        }
    return fallback;
}

void OverteonProcessor::saveMeterMode (const juce::String& key, int value) const
{
    if (value != 0 && value != 1) return;
    const auto f = getGlobalSettingsFile();
    auto xml = readGlobalSettings (f);
    xml->setAttribute (key, value);
    f.getParentDirectory().createDirectory();
    xml->writeTo (f);
}

int OverteonProcessor::loadIntSetting (const juce::String& key, int fallback) const
{
    const auto f = getGlobalSettingsFile();
    if (! f.existsAsFile()) return fallback;
    if (auto xml = juce::XmlDocument::parse (f))
        if (xml->hasTagName ("HINJGLOBAL"))
            return xml->getIntAttribute (key, fallback);
    return fallback;
}

void OverteonProcessor::saveIntSetting (const juce::String& key, int value) const
{
    const auto f = getGlobalSettingsFile();
    auto xml = readGlobalSettings (f);
    xml->setAttribute (key, value);
    f.getParentDirectory().createDirectory();
    xml->writeTo (f);
}

void OverteonProcessor::cacheParams()
{
    par.bypass   = apvts.getRawParameterValue ("bypass");
    par.drive    = apvts.getRawParameterValue ("drive");
    par.mix      = apvts.getRawParameterValue ("mix");
    par.type     = apvts.getRawParameterValue ("driveType");
    par.mode     = apvts.getRawParameterValue ("driveMode");
    par.osFactor = apvts.getRawParameterValue ("osFactor");
    par.inGain   = apvts.getRawParameterValue ("inGain");
    par.outGain  = apvts.getRawParameterValue ("outGain");
    par.invAmt   = apvts.getRawParameterValue ("deEmph");
    par.harmGain = apvts.getRawParameterValue ("harmGain");
    par.airFreq  = apvts.getRawParameterValue ("airProtect");
    par.evenOdd  = apvts.getRawParameterValue ("evenOdd");
    par.msMode   = apvts.getRawParameterValue ("msMode");
    par.autoGain = apvts.getRawParameterValue ("autoGain");
    par.harmSolo = apvts.getRawParameterValue ("harmSolo");
    par.scRange  = apvts.getRawParameterValue ("scRange");
    par.pqRange  = apvts.getRawParameterValue ("pqRange");
    par.protectMode = apvts.getRawParameterValue ("protectMode");
    par.limiterOs   = apvts.getRawParameterValue ("limiterOs");
    for (int b = 0; b < 5; ++b)
    {
        const auto sc = "sc" + juce::String (b + 1);
        const auto pq = "pq" + juce::String (b + 1);
        par.scF[b] = apvts.getRawParameterValue (sc + "Freq");
        par.scG[b] = apvts.getRawParameterValue (sc + "Gain");
        par.scQ[b] = apvts.getRawParameterValue (sc + "Q");
        par.pqF[b] = apvts.getRawParameterValue (pq + "Freq");
        par.pqG[b] = apvts.getRawParameterValue (pq + "Gain");
        par.pqQ[b] = apvts.getRawParameterValue (pq + "Q");
        par.scType[b] = apvts.getRawParameterValue (sc + "Type");
        par.pqType[b] = apvts.getRawParameterValue (pq + "Type");
        par.scOn[b]   = apvts.getRawParameterValue (sc + "On");
        par.pqOn[b]   = apvts.getRawParameterValue (pq + "On");
    }
}

void OverteonProcessor::applyInit()
{
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
        {
            rp->beginChangeGesture();
            rp->setValueNotifyingHost (rp->getDefaultValue());
            rp->endChangeGesture();
        }
}

void OverteonProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;
    // High-quality half-band FIRs, built explicitly so the transition width can
    // be chosen.  The recipe (transition, stopband, integer latency, init) is
    // shared with the limiter in Oversampling.h; the two oversamplers remain
    // independent processors with independent state and factors.
    for (int s2 = 0; s2 < 5; ++s2)
    {
        omega::initHighQualityOversampling (osN[s2], s2 + 1, samplesPerBlock);
        omega::initHighQualityOversampling (osD[s2], s2 + 1, samplesPerBlock);
        osLat[s2] = (int) std::lround (osN[s2]->getLatencyInSamples());
    }
    lastOsIdx = juce::jlimit (0, 4, (int) par.osFactor->load());
    driveLat  = osLat[lastOsIdx];

    lastLimiterMode = juce::jlimit (0, 3, (int) par.protectMode->load());
    lastLimiterOs   = juce::jlimit (0, 3, (int) par.limiterOs->load());
    limiter.prepare (sr, juce::jmax (1, samplesPerBlock));
    limiter.setMode ((OverteonLimiter::Mode) lastLimiterMode);
    limiter.setOversampling (lastLimiterOs);
    updateReportedLatency();
    int maxLat = 1;
    for (int s2 = 0; s2 < 5; ++s2) maxLat = juce::jmax (maxLat, osLat[s2]);
    dryFifoL.assign ((size_t) maxLat, 0.0);
    dryFifoR.assign ((size_t) maxLat, 0.0);
    dryPos = 0;
    dbuf.setSize (2, samplesPerBlock);
    dryBufD.setSize (2, samplesPerBlock);
    limBuf.setSize (2, juce::jmax (1, samplesPerBlock));
    for (auto& bb : scEq)    for (auto& f : bb) f.reset();
    for (auto& bb : scEq2)   for (auto& f : bb) f.reset();
    for (auto& bb : scInv)   for (auto& f : bb) f.reset();
    for (auto& bb : postEq)  for (auto& f : bb) f.reset();
    for (auto& bb : postEq2) for (auto& f : bb) f.reset();
    meterInL = meterInR = meterOutL = meterOutR = 0.0f;
    meterInPkL = meterInPkR = meterOutPkL = meterOutPkR = 0.0f;
    rmsInL.reset(); rmsInR.reset(); rmsOutL.reset(); rmsOutR.reset();
    rmsInL.tau = rmsInR.tau = rmsOutL.tau = rmsOutR.tau = kMeterRmsTau;
    gSmooth = 1.0;
    agComp  = 1.0;
    // snap the EQ range multipliers to their targets (no sweep on playback start)
    scMultS = kScRangeMult[juce::jlimit (0, 3, (int) par.scRange->load())];
    pqMultS = kPqRangeMult[juce::jlimit (0, 3, (int) par.pqRange->load())];
    for (auto& f : airLP) f.reset();
    fftIdx = 0;
    fftReady = false;
    fftInIdx = 0;
    fftInReady = false;
    specInW = specOutW = 0;
    specInWritten.store (0, std::memory_order_relaxed);
    specOutWritten.store (0, std::memory_order_relaxed);
    grSmooth = 0.0;
    meterGr.store (0.0f);
    meterGrPk.store (0.0f);
}

void OverteonProcessor::updateReportedLatency()
{
    const bool schranz = (int) par.mode->load() == 2;
    setLatencySamples ((schranz ? 0 : driveLat)
                       + limiter.getLatencySamples());
}

void OverteonProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    if (n == 0) return;

    // ---- bypass ---------------------------------------------------------
    //
    // Done here, at the top, rather than by the host: JUCE reports this
    // parameter through getBypassParameter(), so a host that supports it takes
    // its own shortcut and never calls us; a host that does not still gets a
    // silent pass-through.  Either way the meters keep reading, and they read
    // the INPUT — during bypass in and out are the same signal, and a meter
    // that showed the last processed level would be showing something that is
    // no longer being sent.
    if (par.bypass != nullptr && par.bypass->load() > 0.5f)
    {
        const int ch = juce::jmin (2, buffer.getNumChannels());
        for (int c = 0; c < ch; ++c)
        {
            const auto* s = buffer.getReadPointer (c);
            float sum = 0.0f;
            float pk = 0.0f;
            for (int i = 0; i < n; ++i)
            {
                sum += s[i] * s[i];
                pk = juce::jmax (pk, std::abs (s[i]));
            }
            const float ms = n > 0 ? sum / float (n) : 0.0f;
            (c == 0 ? meterInL : meterInR).store (std::sqrt (ms));
            (c == 0 ? meterOutL : meterOutR).store (std::sqrt (ms));
            (c == 0 ? meterInPkL : meterInPkR).store (pk);
            (c == 0 ? meterOutPkL : meterOutPkR).store (pk);
        }
        // The harmonic ratio is undefined while nothing is being generated;
        // report the floor instead of freezing at the level it had.  Nothing
        // passes the limiter either, so its ring reads zero.
        meterHarm.store (-100.0f);
        meterHarmPk.store (-100.0f);
        grSmooth = 0.0;
        meterGr.store (0.0f);
        meterGrPk.store (0.0f);
        return;
    }
    const int nCh = juce::jmin (2, buffer.getNumChannels());
    if (dbuf.getNumSamples()    < n) dbuf.setSize    (2, n, false, false, true);
    if (dryBufD.getNumSamples() < n) dryBufD.setSize (2, n, false, false, true);
    if (limBuf.getNumSamples()  < n) limBuf.setSize  (2, n, false, false, true);

    const int limMode = juce::jlimit (0, 3, (int) par.protectMode->load());
    const int limOs   = juce::jlimit (0, 3, (int) par.limiterOs->load());
    if (limMode != lastLimiterMode || limOs != lastLimiterOs)
    {
        lastLimiterMode = limMode;
        lastLimiterOs   = limOs;
        limiter.setMode ((OverteonLimiter::Mode) limMode);
        limiter.setOversampling (limOs);
        updateReportedLatency();
    }

    // ---- float in -> 64-bit internal chain ----
    for (int ch = 0; ch < 2; ++ch)
    {
        auto* d = dbuf.getWritePointer (ch);
        auto* s = buffer.getReadPointer (juce::jmin (ch, nCh - 1));
        for (int i = 0; i < n; ++i) d[i] = (double) s[i];
    }
    auto* L = dbuf.getWritePointer (0);
    auto* R = dbuf.getWritePointer (1);

    // ---- M/S encode: when a stereo mode is active, work in Mid/Side so the
    // saturation/harmonics can be steered to the centre or the sides. Stereo
    // (mode 0) leaves L/R untouched. We encode here and decode at the very end.
    const int msm = (int) par.msMode->load();
    if (msm != 0)
        for (int i = 0; i < n; ++i)
        {
            const double m = (L[i] + R[i]) * 0.5;
            const double s = (L[i] - R[i]) * 0.5;
            L[i] = m; R[i] = s;   // L slot = Mid, R slot = Side
        }

    // ---- input gain + input meters ----
    //
    // The upper plot's monitor point is AFTER the sidechain EQ.  That tap is
    // taken inside the drive block; this one only feeds it in Classic mode,
    // where the sidechain EQ does not run at all (the saturator hears the
    // input unchanged).  The legacy fftInData tap stays on the pre-EQ signal
    // for the WebView editor.
    const int dmodeIn = (int) par.mode->load();
    {
        const double ig = juce::Decibels::decibelsToGain ((double) par.inGain->load());
        for (int i = 0; i < n; ++i)
        {
            L[i] *= ig;
            R[i] *= ig;
            const float mono = (float) ((L[i] + R[i]) * 0.5);
            fftInData[(size_t) fftInIdx] = mono;
            if (++fftInIdx >= fftSize) { fftInIdx = 0; fftInReady = true; }
            if (dmodeIn == 0)
            {
                // In the M/S domain the mono sum is the Mid component.
                const float mid = (msm != 0) ? (float) L[i] : mono;
                specRingIn[(size_t) (specInW & (uint32_t) (specRingSize - 1))] = mid;
                ++specInW;
            }
        }
        if (dmodeIn == 0)
            specInWritten.store (specInW, std::memory_order_release);

        // TRUE RMS, not max(|x|) with a decay coefficient.  What goes on screen
        // has to be the RMS of what went through: the bars are labelled level,
        // the manual calls the ring an RMS ratio, and a peak detector reads the
        // crest factor instead — 3 dB high on a sine and 10+ dB high on a
        // transient, then falling at a speed set by an arbitrary coefficient.
        rmsInL.process (L, n, sr);
        rmsInR.process (R, n, sr);
        meterInL.store ((float) rmsInL.amplitude());
        meterInR.store ((float) rmsInR.amplitude());

        float pkL = 0.0f, pkR = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            pkL = juce::jmax (pkL, (float) std::abs (L[i]));
            pkR = juce::jmax (pkR, (float) std::abs (R[i]));
        }
        meterInPkL.store (pkL);
        meterInPkR.store (pkR);
    }

    // ---- drive: classic / harmonic injector ----
    {
        const double dmix  = (double) par.mix->load();
        const int    dmode = (int) par.mode->load();
        const int    oi    = juce::jlimit (0, 4, (int) par.osFactor->load());
        auto& osp          = *osN[oi];
        if (dmode != lastDmode)
        {
            lastDmode = dmode;
            updateReportedLatency();
        }
        // Schranz never runs the saturator or its oversampling path.
        if (dmode != 2 && oi != lastOsIdx)
        {
            osp.reset();
            osD[oi]->reset();
            lastOsIdx = oi;
            driveLat  = osLat[oi];
            updateReportedLatency();
            std::fill (dryFifoL.begin(), dryFifoL.end(), 0.0);
            std::fill (dryFifoR.begin(), dryFifoR.end(), 0.0);
            dryPos = 0;
            saveGlobalOsFactor (oi);   // persist the global preference
        }

        for (int ch = 0; ch < 2; ++ch)
            dryBufD.copyFrom (ch, 0, dbuf, ch, 0, n);

        bool inv[5]  = { false, false, false, false, false };
        bool s2on[5] = { false, false, false, false, false };
        if (dmode >= 1)   // SC EQ shapes the saturator feed (Harmonic) or IS the output (Schranz)
        {
            const float fsr = (float) sr;
            // range selector: smooth the multiplier so switching ranges is
            // click-free even while audio is running
            scMultS += 0.05 * (kScRangeMult[juce::jlimit (0, 3, (int) par.scRange->load())] - scMultS);
            omega::Biquad p, p2, pInv;
            // NOTCH / CUT compatibility with the inverse topology:
            //   out = dry + MIX * E^-1( NL(E(x)) - g*E(x) )
            // Bell/Shelf with +g/-g (same f,Q) are exact RBJ reciprocals, so
            // E^-1 exists. Notch/Cut have E(f0)=0 -> non-invertible, but that
            // is the intended behaviour: a protected band feeds ~0 into NL, so
            // generates no harmonics, so needs no de-emphasis. We skip E^-1 for
            // them; the linear-null subtraction stays sample-exact for whatever
            // passed, preserving phase alignment with zero error.
            for (int b = 0; b < 5; ++b)
            {
                const float f0 = par.scF[b]->load();
                const float q0 = par.scQ[b]->load();
                const float gq = par.scG[b]->load() * (float) scMultS;   // effective gain incl. range
                const int   ty = (int) par.scType[b]->load();
                bool invertible = false;
                s2on[b] = configBand (ty, fsr, f0, q0, gq, p, p2,
                                      &invertible, &pInv, par.invAmt->load(),
                                      par.scOn[b]->load() > 0.5f);
                scEq[b][0].copyCoefsFrom (p);
                scEq[b][1].copyCoefsFrom (p);
                if (s2on[b]) { scEq2[b][0].copyCoefsFrom (p2); scEq2[b][1].copyCoefsFrom (p2); }
                inv[b] = invertible;
                if (inv[b]) { scInv[b][0].copyCoefsFrom (pInv); scInv[b][1].copyCoefsFrom (pInv); }
            }
            for (int ch = 0; ch < 2; ++ch)
            {
                auto* dch = dbuf.getWritePointer (ch);
                for (int i = 0; i < n; ++i)
                {
                    double v = dch[i];
                    for (int b = 0; b < 5; ++b)
                    {
                        v = scEq[b][ch].processD (v);
                        if (s2on[b]) v = scEq2[b][ch].processD (v);
                    }
                    dch[i] = v;
                }
            }

            // ---- the upper plot's monitor point -------------------------
            //
            // AFTER the sidechain EQ, BEFORE the distortion: this is what the
            // saturator actually hears, so the plot now moves when the SC EQ
            // moves.  The old tap was the raw input, which is why the upper
            // spectrum ignored the EQ entirely.  In the M/S domain the mono
            // sum is the Mid component, not (M+S)/2.
            //
            // SOLO HARM is the exception: the harmonic solo mutes the dry, so
            // the meaningful picture is the distortion output.  That tap is
            // taken after the merge instead; skipping this one keeps the two
            // signals from interleaving in the same ring.
            if (! (dmode == 1 && par.harmSolo->load() > 0.5f))
            {
                const auto* l0 = dbuf.getReadPointer (0);
                const auto* r0 = dbuf.getReadPointer (1);
                for (int i = 0; i < n; ++i)
                {
                    const float mid = (msm != 0)
                                    ? (float) l0[i]
                                    : (float) ((l0[i] + r0[i]) * 0.5);
                    specRingIn[(size_t) (specInW & (uint32_t) (specRingSize - 1))] = mid;
                    ++specInW;
                }
                specInWritten.store (specInW, std::memory_order_release);
            }

            // Schranz mode: output exactly what the saturator would hear — the
            // post-sidechain-EQ, pre-distortion signal. The rest of the chain is
            // deliberately bypassed, so POST EQ has no effect in this mode.
            if (dmode == 2)
            {
                // auto-gain uses the same RMS-match rule as the main path, so
                // this mode's loudness stays comparable when it is toggled.
                if (par.autoGain->load() > 0.5f)
                {
                    const auto* dl = dryBufD.getReadPointer (0);
                    const auto* dr = dryBufD.getReadPointer (1);
                    double inP = 0.0, outP = 0.0;
                    for (int i = 0; i < n; ++i)
                    {
                        inP  += dl[i] * dl[i] + dr[i] * dr[i];
                        outP += dbuf.getSample (0, i) * dbuf.getSample (0, i)
                              + dbuf.getSample (1, i) * dbuf.getSample (1, i);
                    }
                    if (outP > 1.0e-9)
                        agComp += 0.05 * (juce::jlimit (0.25, 4.0,
                                          std::sqrt (inP / outP)) - agComp);
                }
                else agComp += 0.05 * (1.0 - agComp);

                const double og = juce::Decibels::decibelsToGain ((double) par.outGain->load()) * agComp;
                auto* ll = limBuf.getWritePointer (0);
                auto* rr = limBuf.getWritePointer (1);
                for (int i = 0; i < n; ++i)
                {
                    ll[i] = juce::jlimit (-8.0, 8.0, dbuf.getSample (0, i) * og);
                    rr[i] = juce::jlimit (-8.0, 8.0, dbuf.getSample (1, i) * og);
                }
                limiter.process (ll, rr, n);

                // Limiter ring: the block's deepest gain reduction, plus the
                // same 131 ms one-pole the harmonic ratio uses for RMS mode.
                {
                    const double grDb = limiter.getLastGainReductionDb();
                    meterGrPk.store ((float) grDb);
                    grSmooth += ovb::onePoleCoef (double (n) / std::max (1.0, sr), kHarmTau)
                              * (grDb - grSmooth);
                    meterGr.store ((float) grSmooth);
                }

                auto* o0 = buffer.getWritePointer (0);
                auto* o1 = nCh > 1 ? buffer.getWritePointer (1) : o0;
                for (int i = 0; i < n; ++i)
                {
                    const double l = ll[i], r = rr[i];
                    o0[i] = (float) l; o1[i] = (float) r;
                    const float mono = (float) ((l + r) * 0.5);
                    fftData[(size_t) fftIdx] = mono;
                    if (++fftIdx >= fftSize) { fftIdx = 0; fftReady = true; }
                    specRingOut[(size_t) (specOutW & (uint32_t) (specRingSize - 1))] = mono;
                    ++specOutW;
                }
                specOutWritten.store (specOutW, std::memory_order_release);
                // RMS on the output tap too, and no second, artificial decay on
                // the input meter: this branch used to multiply meterInL by an
                // extra exp(-dt/0.55) on top of the input stage's own release,
                // so in Schranz the IN bar fell at a different speed from the
                // same bar in every other mode.
                rmsOutL.process (ll, n, sr);
                rmsOutR.process (rr, n, sr);
                meterOutL.store ((float) rmsOutL.amplitude());
                meterOutR.store ((float) rmsOutR.amplitude());

                float pkL = 0.0f, pkR = 0.0f;
                for (int i = 0; i < n; ++i)
                {
                    pkL = juce::jmax (pkL, (float) std::abs (ll[i]));
                    pkR = juce::jmax (pkR, (float) std::abs (rr[i]));
                }
                meterOutPkL.store (pkL);
                meterOutPkR.store (pkR);
                harmRatioS *= meterDecay (n, sr, kHarmTau);
                meterHarm.store ((float) juce::Decibels::gainToDecibels (harmRatioS, -100.0));
                // No dry/harm energies are measured in this branch, so there is
                // nothing unsmoothed to report; the display's peak mode then
                // simply holds the same value.
                meterHarmPk.store (meterHarm.load());
                return;
            }
        }

        double dryE = 0.0, harmE = 0.0;   // harmonic-monitor energies
        auto sub = juce::dsp::AudioBlock<double> (dbuf).getSubBlock (0, (size_t) n);
        auto up  = osp.processSamplesUp (sub);

        const double k  = (double) par.drive->load();
        const int    dt = (int) par.type->load();
        const double eo = (double) par.evenOdd->load();   // -1 even .. +1 odd
        // Base shapes are odd-symmetric (=> odd harmonics). To add EVEN
        // harmonics we blend in an asymmetric term: a*x*|x| style bias breaks
        // symmetry and generates 2nd/4th. evenOdd<0 leans even (warm, tube-like),
        // evenOdd>0 stays/over-drives odd (sharp, electronic). At 0 = the
        // original balanced shape.
        const double evenAmt = juce::jlimit (0.0, 1.0, -eo);   // 0..1 toward even
        const double oddBoost = juce::jlimit (0.0, 1.0,  eo);  // 0..1 extra odd
        auto shape = [dt, evenAmt, oddBoost] (double x)
        {
            double base;
            if (dt == 0) base = std::tanh (x);
            else if (dt == 1) base = std::tanh (x + 0.18 * x * x);
            else base = x / (1.0 + 0.6 * std::abs (x));
            const double th = std::tanh (x);
            // both enrichment terms have ZERO derivative at x=0, so the shaper's
            // small-signal gain stays = base's. This keeps the linear-null math
            // stable and the harmonic LEVEL consistent as the knob is swept;
            // only the harmonic CONTENT (even vs odd flavour) changes.
            const double evenPart = th * std::abs (th);          // 2nd/4th, slope 0
            const double oddPart  = std::tanh (2.0 * x) - 2.0 * th;  // 3rd/5th, slope 0
            return base
                 + evenPart * (0.6 * evenAmt)
                 + oddPart  * (0.3 * oddBoost);
        };
        const double norm = 1.0 / juce::jmax (0.05, std::abs (shape (k)));
        if (dmode == 1)
        {
            // ---- harmonic extraction (per-block least-squares linear null) ----
            // Residue = shape(y*k) - g*y, where g is the per-block LEAST-SQUARES
            // projection of shape(y*k) onto y (the signal's actual average gain
            // through the shaper, i.e. its Bussgang/describing-function gain).
            // Subtracting the true LS gain nulls the linear part for ANY input
            // level, leaving a residue that is verifiably PURE HARMONICS (zero
            // energy at the fundamental) while the dry stays untouched -> this
            // is what makes it a genuine active-distortion EQ. Offline numeric
            // verification (verify_dsp.cpp) confirms: dry gain ~1.0, fundamental
            // leakage <0.08 (down to 0.009 at high level), peak <0.8 (stable) —
            // it does NOT diverge, unlike the old norm-based v1.6.1 formula
            // which reached peak 4.6 / fundamental gain 6.2 ("the explosion").
            // The 1/sqrt(k) scaling keeps the residue level musical across DRIVE.
            double num = 0.0, den = 0.0;
            for (size_t ch = 0; ch < up.getNumChannels(); ++ch)
            {
                auto* d = up.getChannelPointer (ch);
                for (size_t i = 0; i < up.getNumSamples(); ++i)
                {
                    const double y = d[i];
                    num += shape (y * k) * y;
                    den += y * y;
                }
            }
            if (den > 1.0e-12)
                gSmooth += 0.25 * (num / den - gSmooth);   // tracked LS gain
            const double rscale = 1.0 / std::sqrt (juce::jmax (1.0, k));
            for (size_t ch = 0; ch < up.getNumChannels(); ++ch)
            {
                auto* d = up.getChannelPointer (ch);
                for (size_t i = 0; i < up.getNumSamples(); ++i)
                {
                    const double y = d[i];
                    double res = (shape (y * k) - gSmooth * y) * rscale;
                    d[i] = juce::jlimit (-4.0, 4.0, res);   // belt-and-braces clamp
                }
            }
        }
        else
        {
            for (size_t ch = 0; ch < up.getNumChannels(); ++ch)
            {
                auto* d = up.getChannelPointer (ch);
                for (size_t i = 0; i < up.getNumSamples(); ++i)
                    d[i] = k > 0.0 ? shape (d[i] * k) * norm : d[i];
            }
        }
        osp.processSamplesDown (sub);

        if (dmode == 1)   // de-emphasis: exact inverse, ONLY for invertible
            for (int ch = 0; ch < 2; ++ch)   // (bell/shelf) bands. inv[b] is
            {                                 // false for notch/cut by design.
                auto* dch = dbuf.getWritePointer (ch);
                for (int i = 0; i < n; ++i)
                {
                    double v = dch[i];
                    for (int b = 0; b < 5; ++b)
                        if (inv[b]) v = scInv[b][ch].processD (v);
                    dch[i] = v;
                }
            }

        if (dmode == 1)
        {
            // phase-twin alignment: the dry passes through an IDENTICAL idle
            // oversampler -> bit-equal filters, latency and phase on both paths
            auto dsub = juce::dsp::AudioBlock<double> (dryBufD).getSubBlock (0, (size_t) n);
            osD[oi]->processSamplesUp (dsub);
            osD[oi]->processSamplesDown (dsub);

        }

        // The old 63-tap guard low-pass at 0.47 fs is gone: with the half-band
        // filters now flat to ~0.49 fs it would have been the first thing to
        // roll off the top end, and it filtered the DRY path too.  Wet and dry
        // still share the same linear-phase oversampling filters, so their
        // relative phase is unchanged.

        // AIR PROTECT: a wet-ONLY high cut. Injected harmonics above this
        // frequency are removed so they can't smear the dry's high-frequency
        // air (critical for supersaws, whose openness lives in a dense field of
        // phase-randomised HF partials that uncorrelated injected harmonics
        // would muddy). The DRY path is NOT filtered -> its air is untouched.
        if (dmode == 1)
        {
            const float af = par.airFreq->load();
            if (af < 19500.0f)   // 20 kHz = off
            {
                omega::Biquad proto;
                proto.setLowpass ((float) sr, af, 0.707f);
                airLP[0].copyCoefsFrom (proto);
                airLP[1].copyCoefsFrom (proto);
                for (int ch = 0; ch < 2; ++ch)
                {
                    auto* hch = dbuf.getWritePointer (ch);
                    for (int i = 0; i < n; ++i)
                        hch[i] = airLP[ch].processD (hch[i]);
                }
            }
        }

        // merge (classic mode aligns dry via integer FIFO instead)
        auto* ddl = dryBufD.getWritePointer (0);
        auto* ddr = dryBufD.getWritePointer (1);
        const double hg = juce::Decibels::decibelsToGain ((double) par.harmGain->load());
        const bool   so = par.harmSolo->load() > 0.5f;
        for (int i = 0; i < n; ++i)
        {
            double dl_ = ddl[i], dr_ = ddr[i];
            if (dmode != 1 && driveLat > 0)
            {
                const double a2 = dryFifoL[(size_t) dryPos];
                const double b2 = dryFifoR[(size_t) dryPos];
                dryFifoL[(size_t) dryPos] = dl_;
                dryFifoR[(size_t) dryPos] = dr_;
                dryPos = (dryPos + 1) % driveLat;
                dl_ = a2; dr_ = b2;
            }
            if (dmode == 1)
            {
                const double dg = so ? 0.0 : 1.0;   // solo mutes the dry path
                // clamp the harmonics branch itself so a hot SC-EQ + de-emphasis
                // combination can never run the sum away
                const double hL = juce::jlimit (-4.0, 4.0, L[i] * dmix * hg);
                const double hR = juce::jlimit (-4.0, 4.0, R[i] * dmix * hg);
                L[i] = dl_ * dg + hL;
                R[i] = dr_ * dg + hR;
                harmE += hL * hL + hR * hR;
                dryE  += dl_ * dg * (dl_ * dg) + dr_ * dg * (dr_ * dg);
            }
            else
            {
                const double wL = L[i], wR = R[i];
                L[i] = wL * dmix + dl_ * (1.0 - dmix);
                R[i] = wR * dmix + dr_ * (1.0 - dmix);
                harmE += (wL - dl_) * (wL - dl_) + (wR - dr_) * (wR - dr_);   // what the saturator added
                dryE  += dl_ * dl_ + dr_ * dr_;
            }
        }

        // ---- SOLO HARM moves the UPPER plot to the distortion output -----
        //
        // With the harmonic solo on, the dry is muted and what leaves this
        // stage is pure harmonics.  The SC EQ plot follows THAT signal instead
        // of its usual pre-distortion point, so the solo shows the harmonic
        // content itself.  The post EQ plot is not involved: it keeps reading
        // the final output.  The pre-distortion tap for this block was skipped
        // above, so the ring never interleaves two different signals.
        if (dmode == 1 && so)
        {
            for (int i = 0; i < n; ++i)
            {
                const float mid = (msm != 0)
                                ? (float) L[i]
                                : (float) ((L[i] + R[i]) * 0.5);
                specRingIn[(size_t) (specInW & (uint32_t) (specRingSize - 1))] = mid;
                ++specInW;
            }
            specInWritten.store (specInW, std::memory_order_release);
        }

        if (dmode != 1) harmE *= dmix * dmix;
        const double ratio = std::sqrt (harmE / juce::jmax (dryE, 1.0e-18));
        // Time-based, not per block.  0.15 per block was 131 ms at 48 kHz /
        // 1024 and 33 ms at 48 kHz / 256, so the ring emptied four times faster
        // in a host with a smaller buffer — the same bug the level meters had
        // already been fixed for.
        harmRatioS += ovb::onePoleCoef (double (n) / std::max (1.0, sr), kHarmTau)
                    * (ratio - harmRatioS);
        meterHarm.store ((float) juce::Decibels::gainToDecibels (harmRatioS, -100.0));
        // Unsmoothed, for the ring's Peak display: this block's own ratio.
        meterHarmPk.store ((float) juce::Decibels::gainToDecibels (ratio, -100.0));
    }

    // ---- post EQ: 5 bands, minimum phase, shelf bands switchable to cuts ----
    {
        const float fsr = (float) sr;
        pqMultS += 0.05 * (kPqRangeMult[juce::jlimit (0, 3, (int) par.pqRange->load())] - pqMultS);
        bool s2on[5] = { false, false, false, false, false };
        omega::Biquad p, p2, pInv;
        for (int b = 0; b < 5; ++b)
        {
            const float f0 = par.pqF[b]->load();
            const float q0 = par.pqQ[b]->load();
            const float gq = par.pqG[b]->load() * (float) pqMultS;   // effective gain incl. range
            const int   ty = (int) par.pqType[b]->load();
            // post EQ has no inverse leg; just configure the forward filter
            s2on[b] = configBand (ty, fsr, f0, q0, gq, p, p2, nullptr, nullptr, 0.0f,
                                  par.pqOn[b]->load() > 0.5f);
            postEq[b][0].copyCoefsFrom (p);
            postEq[b][1].copyCoefsFrom (p);
            if (s2on[b]) { postEq2[b][0].copyCoefsFrom (p2); postEq2[b][1].copyCoefsFrom (p2); }
        }
        for (int ch = 0; ch < 2; ++ch)
        {
            auto* dch = dbuf.getWritePointer (ch);
            for (int i = 0; i < n; ++i)
            {
                double v = dch[i];
                for (int b = 0; b < 5; ++b)
                {
                    v = postEq[b][ch].processD (v);
                    if (s2on[b]) v = postEq2[b][ch].processD (v);
                }
                dch[i] = v;
            }
        }
    }

    // ---- M/S mode masking + decode, auto-gain, meters, fft, double->float ----
    {
        const double og = juce::Decibels::decibelsToGain ((double) par.outGain->load());

        // For Mid / Side modes, only the chosen component keeps its processed
        // version; the other component reverts to the (encoded) DRY so we don't
        // touch it. dryBufD currently holds the time-aligned dry in the SAME
        // domain as L/R (it was encoded alongside, see top), so M=dry[0],S=dry[1].
        auto* dl = dryBufD.getWritePointer (0);
        auto* dr = dryBufD.getWritePointer (1);
        if (msm == 1)            // Mid only: keep processed Mid, dry Side
            for (int i = 0; i < n; ++i) R[i] = dr[i];
        else if (msm == 2)       // Side only: keep processed Side, dry Mid
            for (int i = 0; i < n; ++i) L[i] = dl[i];

        // auto-gain: match output loudness to input so bypass A/B is fair.
        if (par.autoGain->load() > 0.5f)
        {
            double inP = 0.0, outP = 0.0;
            for (int i = 0; i < n; ++i)
            {
                // input energy = the dry (pre-processing); output = current L/R
                inP  += dl[i]*dl[i] + dr[i]*dr[i];
                outP += L[i]*L[i]  + R[i]*R[i];
            }
            if (outP > 1.0e-9)
            {
                const double target = std::sqrt (inP / outP);     // RMS match
                const double clamped = juce::jlimit (0.25, 4.0, target);
                agComp += 0.05 * (clamped - agComp);              // smooth
            }
        }
        else agComp += 0.05 * (1.0 - agComp);
        const double ag = agComp;

        // decode M/S back to L/R if we were in an M/S domain
        const bool decode = (msm != 0);

        auto* ll = limBuf.getWritePointer (0);
        auto* rr = limBuf.getWritePointer (1);
        for (int i = 0; i < n; ++i)
        {
            double a = L[i], b = R[i];
            if (decode) { const double m = a, s = b; a = m + s; b = m - s; }  // M/S -> L/R
            ll[i] = juce::jlimit (-8.0, 8.0, a * og * ag);
            rr[i] = juce::jlimit (-8.0, 8.0, b * og * ag);
        }
        limiter.process (ll, rr, n);

        // Limiter ring: the block's deepest gain reduction, plus the same
        // 131 ms one-pole the harmonic ratio uses for RMS mode.
        {
            const double grDb = limiter.getLastGainReductionDb();
            meterGrPk.store ((float) grDb);
            grSmooth += ovb::onePoleCoef (double (n) / std::max (1.0, sr), kHarmTau)
                      * (grDb - grSmooth);
            meterGr.store ((float) grSmooth);
        }

        auto* o0 = buffer.getWritePointer (0);
        auto* o1 = nCh > 1 ? buffer.getWritePointer (1) : o0;
        for (int i = 0; i < n; ++i)
        {
            const double l = ll[i], r = rr[i];
            o0[i] = (float) l;
            o1[i] = (float) r;
            const float mono = (float) ((l + r) * 0.5);
            fftData[(size_t) fftIdx] = mono;
            if (++fftIdx >= fftSize) { fftIdx = 0; fftReady = true; }
            specRingOut[(size_t) (specOutW & (uint32_t) (specRingSize - 1))] = mono;
            ++specOutW;
        }
        specOutWritten.store (specOutW, std::memory_order_release);
        rmsOutL.process (ll, n, sr);
        rmsOutR.process (rr, n, sr);
        meterOutL.store ((float) rmsOutL.amplitude());
        meterOutR.store ((float) rmsOutR.amplitude());

        float pkL = 0.0f, pkR = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            pkL = juce::jmax (pkL, (float) std::abs (ll[i]));
            pkR = juce::jmax (pkR, (float) std::abs (rr[i]));
        }
        meterOutPkL.store (pkL);
        meterOutPkR.store (pkR);
    }
}

juce::AudioProcessorEditor* OverteonProcessor::createEditor()
{
   #if OVERTEON_QT_UI
    return new OvQmlEditor (*this);
   #else
    return new InjectorWebEditor (*this);
   #endif
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new OverteonProcessor();
}
