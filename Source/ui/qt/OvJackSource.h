#pragma once
// ============================================================
//  OvJackSource — the preview app playing a real signal.
//
//  The standalone preview used to run entirely on OvMockSource: a synthesised
//  60 Hz square wave, useful for comparing against the reference page but not
//  for answering "does the metering work with real audio".
//
//  This is a real JACK client.  Anything can be connected to its inputs — the
//  bench feeds it a FLAC through mpv — and the meters and the analyser are
//  computed from the samples that actually arrive, with the SAME maths the
//  plug-in uses (OvFft.h).  So `-Gs` measures the UI on a real signal, in a
//  process with its own event loop and no host pump, which is what separates
//  "the UI is slow" from "the host is slow".
//
//  Input and output spectra: the inputs are the signal, and the "output" is
//  that signal with the EQ response modelled on top, because THIS build links
//  no DSP.  It is only used by the PREVIEW_ONLY configuration; the normal
//  preview hosts the plug-in's OverteonProcessor and measures the processed
//  audio for real (OvJackJuce.h).  Even here the model is applied in dB before
//  the display floor, so silence stays silent instead of drawing the curve.
// ============================================================
#include "OvSource.h"
#include "OvBiquad.h"
#include "OvFft.h"

#include <jack/jack.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

class OvJackSource : public OvSource
{
public:
    explicit OvJackSource (const std::string& clientName = "overpreview")
    {
        buildTable();

        jack_status_t st {};
        m_client = jack_client_open (clientName.c_str(), JackNoStartServer, &st);
        if (m_client == nullptr)
        {
            std::fprintf (stderr, "[jack] cannot open a client (%s) — is jackd "
                                  "running and JACK_DEFAULT_SERVER right?\n",
                          clientName.c_str());
            return;
        }

        m_in[0] = jack_port_register (m_client, "in_1", JACK_DEFAULT_AUDIO_TYPE,
                                      JackPortIsInput, 0);
        m_in[1] = jack_port_register (m_client, "in_2", JACK_DEFAULT_AUDIO_TYPE,
                                      JackPortIsInput, 0);

        jack_set_process_callback (m_client, &OvJackSource::onProcess, this);
        m_rate = jack_get_sample_rate (m_client);

        // ---- size the analyser from the RATE ----------------------------
        // A fixed 2048-sample window is 42.7 ms at 48 kHz and 21.3 ms at 96:
        // the same code would measure half the signal on a 96 kHz interface, so
        // every level and every meter derived from it would be wrong.  The
        // window is therefore stated in TIME (ovfft::kWindowSeconds) and the
        // buffer is sized from it here, once the rate is known.
        m_fftSize = ovfft::fftSizeFor (m_rate);
        m_block   = ovfft::blockSizeFor (m_rate);
        m_ring.assign ((size_t) m_fftSize, 0.0f);
        m_fft.assign ((size_t) m_fftSize, {});
        std::fprintf (stderr, "[jack] analyser: %d samples = %.1f ms at %.0f Hz\n",
                      m_fftSize, 1000.0 * m_fftSize / m_rate, m_rate);

        if (jack_activate (m_client) != 0)
        {
            std::fprintf (stderr, "[jack] activate failed\n");
            jack_client_close (m_client);
            m_client = nullptr;
            return;
        }
        m_ok = true;
        std::fprintf (stderr, "[jack] client '%s' ready at %.0f Hz\n",
                      clientName.c_str(), m_rate);
    }

    ~OvJackSource() override
    {
        if (m_client != nullptr)
        {
            jack_deactivate (m_client);
            jack_client_close (m_client);
        }
    }

    bool ok() const { return m_ok; }

    // ---- OvSource -------------------------------------------------------
    int count() const override { return (int) rows.size(); }

    OvParamSpec spec (int i) const override
    {
        const auto& r = rows[(size_t) i];
        OvParamSpec s;
        s.id = r.id; s.label = r.label;
        s.minValue = r.lo; s.maxValue = r.hi;
        s.defaultNorm = r.defNorm;
        s.isBool = r.isBool;
        for (const auto& c : r.choices) s.choices.push_back (c);
        return s;
    }

    double getNorm (int i) const override { return norms[(size_t) i]; }
    void   setNorm (int i, double v) override { norms[(size_t) i] = std::clamp (v, 0.0, 1.0); }

    double normForValue (int i, double v) const override
    {
        const auto& r = rows[(size_t) i];
        if (! r.choices.empty())
        {
            const double last = (double) r.choices.size() - 1.0;
            return last > 0.0 ? std::clamp (v / last, 0.0, 1.0) : 0.0;
        }
        const double span = r.hi - r.lo;
        if (span <= 0.0) return 0.0;
        const double lin = std::clamp ((v - r.lo) / span, 0.0, 1.0);
        return (r.skew != 1.0 && lin > 0.0) ? std::pow (lin, r.skew) : lin;
    }

    double valueForNorm (int i, double n) const override
    {
        const auto& r = rows[(size_t) i];
        n = std::clamp (n, 0.0, 1.0);
        if (! r.choices.empty())
        {
            const double last = (double) r.choices.size() - 1.0;
            return std::clamp (std::round (n * last), 0.0, last);
        }
        if (r.isBool) return n > 0.5 ? 1.0 : 0.0;
        const double prop = (r.skew != 1.0 && n > 0.0) ? std::exp (std::log (n) / r.skew) : n;
        return r.lo + (r.hi - r.lo) * prop;
    }

    std::string text (int i) const override
    {
        const auto& r = rows[(size_t) i];
        const double v = valueForNorm (i, norms[(size_t) i]);
        if (! r.choices.empty())
            return r.choices[(size_t) std::clamp ((int) v, 0, (int) r.choices.size() - 1)];
        char buf[64];
        if (std::abs (r.hi) > 100.0) std::snprintf (buf, sizeof buf, "%.0f", v);
        else                         std::snprintf (buf, sizeof buf, "%.2f", v);
        return buf;
    }

    void beginGesture (int) override {}
    void endGesture   (int) override {}
    void applyInit() override
    {
        for (size_t i = 0; i < rows.size(); ++i) norms[i] = rows[i].defNorm;
    }

    Snapshot snapshotParams() override { return { norms, true }; }

    void restoreParams (const Snapshot& state) override
    {
        if (state.valid && state.norm.size() == norms.size())
            for (size_t i = 0; i < norms.size(); ++i)
                setNorm (int (i), state.norm[i]);
    }

    std::string presetListJson() override
    {
        return R"({"name":"presets","folders":[)"
               R"({"name":"Factory","folders":[],"presets":["default","Air Lift","Drum Glue","Vocal Sheen"]},)"
               R"({"name":"User","folders":[],"presets":[]})"
               R"(],"presets":[]})";
    }
    bool loadPresetNamed (const std::string&, const std::string&) override { return true; }
    void openLoadDialog() override {}
    void openSaveDialog() override {}

    OvMeters meters() const override
    {
        OvMeters m;
        const auto snap = snapshot();
        const float pk = ovfft::peakDbOfRing (snap.ring, snap.pos, m_block);
        m.inL = m.inR = pk;
        m.outL = m.outR = pk;
        // A stand-in for the harmonic/dry ratio, which needs the DSP.  Follows
        // the level so the ring animates with the material instead of sitting
        // at a constant.
        m.harm = std::clamp (-40.0f + 0.9f * pk, -48.0f, 0.0f);
        return m;
    }

    bool spectrum (float* dst, int n) override
    {
        // THE FALLBACK — no DSP in this build.
        //
        // This source exists for the PREVIEW_ONLY configuration (a Qt-only
        // cross-build where JUCE cannot be compiled) and has no
        // OverteonProcessor to measure.  The normal preview hosts the real DSP
        // and never reaches this code — see OvJackJuce.h.
        //
        // The EQ response is therefore a MODEL here, and a model is only honest
        // where it belongs: in dB, BEFORE the display floor.  It used to be
        // added AFTER the 0..1 conversion, so a silent input — a table sitting
        // on its -85 dB floor — plus the EQ curve came out as the EQ shape drawn
        // by itself: a static curve in silence, which is the one thing an
        // after-processing meter must never show.  Adding it to the dB value
        // first means silence plus gain is still below the floor.
        const auto snap = snapshot();
        ovfft::spectrumFromRing (snap.ring, snap.pos, m_rate, dst, n, m_fft);

        for (int i = 0; i < n; ++i)
        {
            const double f  = 20.0 * std::pow (1000.0, (double) i / std::max (1, n - 1));
            const double db = -90.0 + 96.0 * (double) dst[i] + eqGainDb (f);
            dst[i] = (float) std::clamp ((db + 90.0) / 96.0, 0.0, 1.0);
        }
        return true;
    }

    bool spectrumIn (float* dst, int n) override
    {
        const auto snap = snapshot();
        ovfft::spectrumFromRing (snap.ring, snap.pos, m_rate, dst, n, m_fft);
        return true;
    }

    double sampleRate() const override { return m_rate; }
    int    driveModeIndex() const override { return 1; }

private:
    struct Row
    {
        std::string id, label;
        double lo = 0.0, hi = 1.0, skew = 1.0, defNorm = 0.0;
        std::vector<std::string> choices;
        bool isBool = false;
    };

    struct Snap
    {
        std::vector<float> ring;
        int pos = 0;
    };

    /// Copy the ring under the lock.  The audio callback only ever appends, so
    /// a copy is 2048 floats — cheap next to the FFT that follows it.
    Snap snapshot() const
    {
        Snap s;
        const std::lock_guard<std::mutex> lk (m_lock);
        s.ring = m_ring;
        s.pos  = m_pos;
        return s;
    }

    static int onProcess (jack_nframes_t nframes, void* arg)
    {
        return static_cast<OvJackSource*> (arg)->process (nframes);
    }

    int process (jack_nframes_t nframes)
    {
        auto* l = (jack_default_audio_sample_t*) jack_port_get_buffer (m_in[0], nframes);
        auto* r = (jack_default_audio_sample_t*) jack_port_get_buffer (m_in[1], nframes);
        if (l == nullptr) return 0;

        const std::lock_guard<std::mutex> lk (m_lock);
        for (jack_nframes_t i = 0; i < nframes; ++i)
        {
            const float s = r != nullptr ? 0.5f * (l[i] + r[i]) : l[i];
            m_ring[(size_t) m_pos] = s;
            m_pos = (m_pos + 1) % m_fftSize;
        }
        return 0;
    }

    double eqGainDb (double hz) const
    {
        double sum = 0.0;
        for (const char* pre : { "sc", "pq" })
            for (int b = 1; b <= 5; ++b)
            {
                const std::string base = std::string (pre) + std::to_string (b);
                const int idx = indexOf (base + "Type");
                if (idx < 0) continue;
                sum += ovbq::bandMagDb ((int) std::lround (valueForNorm (idx, norms[(size_t) idx])),
                                        m_rate,
                                        valueForNorm (indexOf (base + "Freq"), norms[(size_t) indexOf (base + "Freq")]),
                                        valueForNorm (indexOf (base + "Q"),    norms[(size_t) indexOf (base + "Q")]),
                                        valueForNorm (indexOf (base + "Gain"), norms[(size_t) indexOf (base + "Gain")]),
                                        hz);
            }
        return sum;
    }

    int indexOf (const std::string& id) const
    {
        for (size_t i = 0; i < rows.size(); ++i) if (rows[i].id == id) return (int) i;
        return -1;
    }

    void buildTable()
    {
        auto add = [this] (std::string id, std::string label, double lo, double hi,
                           double skew, double def) {
            Row r; r.id = std::move (id); r.label = std::move (label);
            r.lo = lo; r.hi = hi; r.skew = skew;
            const double lin = (def - lo) / (hi - lo);
            r.defNorm = (skew != 1.0 && lin > 0.0) ? std::pow (lin, skew) : lin;
            rows.push_back (std::move (r));
        };
        auto addC = [this] (std::string id, std::string label,
                            std::vector<std::string> choices, int def) {
            Row r; r.id = std::move (id); r.label = std::move (label);
            r.choices = std::move (choices);
            r.defNorm = double (def) / double (std::max<size_t> (1, r.choices.size() - 1));
            rows.push_back (std::move (r));
        };
        auto addB = [this] (std::string id, std::string label) {
            Row r; r.id = std::move (id); r.label = std::move (label);
            r.isBool = true; r.defNorm = 0.0;
            rows.push_back (std::move (r));
        };

        add ("drive", "Drive", 0, 10, 1, 0);
        add ("mix", "Mix", 0, 1, 1, 1);
        addC ("driveType", "Drive Type", { "Tanh", "Tube", "Tape" }, 0);
        addC ("driveMode", "Drive Mode", { "Classic", "Harmonic", "Schranz" }, 1);
        addC ("osFactor", "Oversampling", { "2x", "4x", "8x", "16x", "32x" }, 1);
        add ("inGain", "In Gain", -24, 24, 1, 0);
        add ("outGain", "Out Gain", -24, 24, 1, 0);
        add ("deEmph", "De-Emphasis", 0, 1, 1, 1);
        add ("harmGain", "Harmonics", -12, 24, 1, 0);
        add ("airProtect", "Air Protect", 2000, 20000, 1, 20000);
        add ("evenOdd", "Even/Odd", -1, 1, 1, 0);
        addC ("msMode", "M/S", { "Stereo", "Mid", "Side", "M/S" }, 0);
        addB ("autoGain", "Auto Gain");
        addB ("harmSolo", "Harm Solo");

        static const char* eqTypes[] = { "Bell", "Notch", "Low Shelf", "High Shelf",
                                         "Low Cut 12", "Low Cut 24", "High Cut 12", "High Cut 24" };
        std::vector<std::string> types (eqTypes, eqTypes + 8);
        const double defF[5] = { 100, 250, 1200, 4000, 8500 };
        const int    defT[5] = { 2, 0, 0, 0, 3 };
        for (const char* pre : { "sc", "pq" })
            for (int b = 0; b < 5; ++b)
            {
                const std::string nm = std::string (pre) + std::to_string (b + 1);
                const bool bump = (std::string (pre) == "pq" && b == 3);
                add (nm + "Freq", nm + " Freq", 5, 20000, 1, bump ? 10000 : defF[b]);
                add (nm + "Gain", nm + " Gain", -12, 12, 1, 0);
                add (nm + "Q",    nm + " Q",    0.1, 6, 1, bump ? 0.9 : 0.707);
                addC (nm + "Type", nm + " Type", types, defT[b]);
            }
        addC ("scRange", "SC Range", { "+/-12 dB", "+/-24 dB", "+/-48 dB", "+/-90 dB" }, 0);
        addC ("pqRange", "PQ Range", { "+/-3 dB", "+/-6 dB", "+/-12 dB", "+/-30 dB" }, 2);
        addC ("protectMode", "Protect", { "Off", "Clip", "Modern", "True Peak" }, 2);
        addC ("limiterOs", "Limiter OS", { "2x", "4x", "8x", "16x" }, 1);

        norms.resize (rows.size());
        for (size_t i = 0; i < rows.size(); ++i) norms[i] = rows[i].defNorm;
    }

    jack_client_t* m_client = nullptr;
    jack_port_t*   m_in[2] {};
    double         m_rate = 48000.0;
    int            m_fftSize = 2048;      // from the rate, see the constructor
    int            m_block = 1024;        // metering block, same duration
    bool           m_ok = false;

    mutable std::mutex m_lock;
    mutable std::vector<float> m_ring;
    mutable int m_pos = 0;
    mutable std::vector<std::complex<float>> m_fft;

    std::vector<Row> rows;
    std::vector<double> norms;
};
