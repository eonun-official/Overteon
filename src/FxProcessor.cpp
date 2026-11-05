#include "FxProcessor.h"

#if OVERTEON_QT_UI
 #include "OvQmlEditor.h"
#else
 #include "FxWebEditor.h"
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
                        bool* invertible, omega::Biquad* pInv, float invAmt)
{
    bool s2 = false;
    bool inv = false;
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

juce::AudioProcessorValueTreeState::ParameterLayout InjectorProcessor::createLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> ps;

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
            // every node spans the full band
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

    return { ps.begin(), ps.end() };
}

InjectorProcessor::InjectorProcessor()
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

juce::File InjectorProcessor::getGlobalSettingsFile()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("Eonun")
        .getChildFile ("HarmonicInjector.settings");
}

int InjectorProcessor::loadGlobalOsFactor() const
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

void InjectorProcessor::saveGlobalOsFactor (int idx) const
{
    // Rare user gesture (oversampling switch): a tiny XML write is fine here.
    juce::XmlElement x ("HINJGLOBAL");
    x.setAttribute ("osFactor", idx);
    const auto f = getGlobalSettingsFile();
    f.getParentDirectory().createDirectory();
    x.writeTo (f);
}

void InjectorProcessor::cacheParams()
{
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
    }
}

void InjectorProcessor::applyInit()
{
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
        {
            rp->beginChangeGesture();
            rp->setValueNotifyingHost (rp->getDefaultValue());
            rp->endChangeGesture();
        }
}

void InjectorProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;
    for (int s2 = 0; s2 < 5; ++s2)
    {
        // FIR equiripple halfbands in DOUBLE, integer latency: flat passband,
        // blunt symmetric rolloff, exact PDC, -300 dB self-noise.
        osN[s2] = std::make_unique<juce::dsp::Oversampling<double>> (
                2, s2 + 1,
                juce::dsp::Oversampling<double>::filterHalfBandFIREquiripple,
                true, true);
        osN[s2]->initProcessing ((size_t) samplesPerBlock);
        osD[s2] = std::make_unique<juce::dsp::Oversampling<double>> (
                2, s2 + 1,
                juce::dsp::Oversampling<double>::filterHalfBandFIREquiripple,
                true, true);
        osD[s2]->initProcessing ((size_t) samplesPerBlock);
        osLat[s2] = (int) std::lround (osN[s2]->getLatencyInSamples());
    }
    lastOsIdx = juce::jlimit (0, 4, (int) par.osFactor->load());
    firGuardLat = 31;                       // (63-1)/2 linear-phase FIR group delay
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
    {
        // linear-phase FIR low-pass at ~0.47 fs: gentle, symmetric, no phase
        // rotation. Soft top-end roll instead of a brickwall at 20 kHz, while
        // still rejecting the halfband fold-back region above it.
        const double fc = 0.47 * sr;
        double s = 0.0;
        for (int i = 0; i < kGuardTaps; ++i)
        {
            const int kk = i - (kGuardTaps - 1) / 2;
            const double sinc = (kk == 0) ? 2.0 * fc / sr
                                          : std::sin (2.0 * juce::MathConstants<double>::pi * fc / sr * kk)
                                            / (juce::MathConstants<double>::pi * kk);
            const double bm = 0.42
                            - 0.5  * std::cos (2.0 * juce::MathConstants<double>::pi * i / (kGuardTaps - 1))
                            + 0.08 * std::cos (4.0 * juce::MathConstants<double>::pi * i / (kGuardTaps - 1));
            guardCoef[(size_t) i] = sinc * bm;
            s += guardCoef[(size_t) i];
        }
        for (auto& c : guardCoef) c /= s;
        for (auto& z : guardZH) z.fill (0.0);
        for (auto& z : guardZD) z.fill (0.0);
        guardZHpos[0] = guardZHpos[1] = guardZDpos[0] = guardZDpos[1] = 0;
    }
    meterInL = meterInR = meterOutL = meterOutR = 0.0f;
    gSmooth = 1.0;
    agComp  = 1.0;
    // snap the EQ range multipliers to their targets (no sweep on playback start)
    scMultS = kScRangeMult[juce::jlimit (0, 3, (int) par.scRange->load())];
    pqMultS = kPqRangeMult[juce::jlimit (0, 3, (int) par.pqRange->load())];
    for (auto& f : airLP) f.reset();
    fftIdx = 0;
    fftReady = false;
}

void InjectorProcessor::updateReportedLatency()
{
    const bool schranz = (int) par.mode->load() == 2;
    setLatencySamples ((schranz ? 0 : driveLat + firGuardLat)
                       + limiter.getLatencySamples());
}

void InjectorProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    if (n == 0) return;
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
    {
        const double ig = juce::Decibels::decibelsToGain ((double) par.inGain->load());
        double pl = 0.0, pr = 0.0;
        for (int i = 0; i < n; ++i)
        {
            L[i] *= ig;
            R[i] *= ig;
            pl = juce::jmax (pl, std::abs (L[i]));
            pr = juce::jmax (pr, std::abs (R[i]));
        }
        meterInL.store (juce::jmax ((float) pl, meterInL.load() * 0.86f));
        meterInR.store (juce::jmax ((float) pr, meterInR.load() * 0.86f));
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
                                      &invertible, &pInv, par.invAmt->load());
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

                auto* o0 = buffer.getWritePointer (0);
                auto* o1 = nCh > 1 ? buffer.getWritePointer (1) : o0;
                double pl = 0.0, pr = 0.0;
                for (int i = 0; i < n; ++i)
                {
                    const double l = ll[i], r = rr[i];
                    o0[i] = (float) l; o1[i] = (float) r;
                    pl = juce::jmax (pl, std::abs (l));
                    pr = juce::jmax (pr, std::abs (r));
                    fftData[(size_t) fftIdx] = (float) ((l + r) * 0.5);
                    if (++fftIdx >= fftSize) { fftIdx = 0; fftReady = true; }
                }
                meterOutL.store (juce::jmax ((float) pl, meterOutL.load() * 0.86f));
                meterOutR.store (juce::jmax ((float) pr, meterOutR.load() * 0.86f));
                meterInL.store (meterInL.load() * 0.92f);
                meterInR.store (meterInR.load() * 0.92f);
                harmRatioS *= 0.92;   // no saturator in Schranz -> ring falls to zero
                meterHarm.store ((float) juce::Decibels::gainToDecibels (harmRatioS, -100.0));
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

        // shared linear-phase FIR guard on BOTH paths, in BOTH modes. Linear
        // phase => zero phase rotation; identical filter on wet+dry => any
        // residual leakage stays in-phase with the dry and cannot build the
        // 20 kHz interference peak. Running it in both modes keeps the group
        // delay (and therefore host PDC) constant across mode switches.
        // The 0.47 fs gentle slope lets the top end extend past 20 kHz instead
        // of a brickwall, while still rejecting the halfband fold-back band.
        for (int ch = 0; ch < 2; ++ch)
        {
            auto* hch = dbuf.getWritePointer (ch);
            auto* dch = dryBufD.getWritePointer (ch);
            auto& zh = guardZH[ch]; auto& zd = guardZD[ch];
            int& ph = guardZHpos[ch]; int& pd = guardZDpos[ch];
            for (int i = 0; i < n; ++i)
            {
                zh[(size_t) ph] = hch[i];
                double oh = 0.0; int idx = ph;
                for (int t = 0; t < kGuardTaps; ++t)
                { oh += guardCoef[(size_t) t] * zh[(size_t) idx]; if (--idx < 0) idx = kGuardTaps - 1; }
                if (++ph >= kGuardTaps) ph = 0;
                hch[i] = oh;
                zd[(size_t) pd] = dch[i];
                double od = 0.0; idx = pd;
                for (int t = 0; t < kGuardTaps; ++t)
                { od += guardCoef[(size_t) t] * zd[(size_t) idx]; if (--idx < 0) idx = kGuardTaps - 1; }
                if (++pd >= kGuardTaps) pd = 0;
                dch[i] = od;
            }
        }

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
        if (dmode != 1) harmE *= dmix * dmix;
        const double ratio = std::sqrt (harmE / juce::jmax (dryE, 1.0e-18));
        harmRatioS += 0.15 * (ratio - harmRatioS);
        meterHarm.store ((float) juce::Decibels::gainToDecibels (harmRatioS, -100.0));
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
            s2on[b] = configBand (ty, fsr, f0, q0, gq, p, p2, nullptr, nullptr, 0.0f);
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

        double pl = 0.0, pr = 0.0;
        auto* o0 = buffer.getWritePointer (0);
        auto* o1 = nCh > 1 ? buffer.getWritePointer (1) : o0;
        for (int i = 0; i < n; ++i)
        {
            const double l = ll[i], r = rr[i];
            o0[i] = (float) l;
            o1[i] = (float) r;
            pl = juce::jmax (pl, std::abs (l));
            pr = juce::jmax (pr, std::abs (r));
            fftData[(size_t) fftIdx] = (float) ((l + r) * 0.5);
            if (++fftIdx >= fftSize) { fftIdx = 0; fftReady = true; }
        }
        meterOutL.store (juce::jmax ((float) pl, meterOutL.load() * 0.86f));
        meterOutR.store (juce::jmax ((float) pr, meterOutR.load() * 0.86f));
    }
}

juce::AudioProcessorEditor* InjectorProcessor::createEditor()
{
   #if OVERTEON_QT_UI
    return new OvQmlEditor (*this);
   #else
    return new InjectorWebEditor (*this);
   #endif
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new InjectorProcessor();
}
