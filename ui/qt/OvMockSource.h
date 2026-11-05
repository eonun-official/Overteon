#pragma once
// ============================================================
//  Overteon Qt/QML UI — mock data source.
//
//  Mirrors the parameter layout of src/FxProcessor.cpp exactly (same ids,
//  ranges, defaults) and animates meters + spectrum, so the QML front end
//  can be developed and screenshotted without a plugin host.
//  Used by the `overteon-qml-preview` target only.
// ============================================================
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <string>
#include <vector>

#include "OvSource.h"

class OvMockSource : public OvSource
{
public:
    // Set from the command line: freezes meters/spectrum so screenshot diffs
    // only show the effect of the interaction under test.
    static bool frozen;

    OvMockSource() { buildTable(); }

    int count() const override { return (int) rows.size(); }

    OvParamSpec spec (int i) const override
    {
        const auto& r = rows[(size_t) i];
        OvParamSpec s;
        s.id = r.id;
        s.label = r.label;
        s.minValue = r.lo;
        s.maxValue = r.hi;
        s.defaultNorm = r.defNorm;
        s.isBool = r.isBool;
        for (const auto& c : r.choices) s.choices.push_back (c);
        return s;
    }

    double getNorm (int i) const override { return norms[(size_t) i]; }

    void setNorm (int i, double v) override
    {
        norms[(size_t) i] = std::clamp (v, 0.0, 1.0);
    }

    double normForValue (int i, double v) const override
    {
        const auto& r = rows[(size_t) i];
        if (! r.choices.empty())
        {
            const double n = (double) r.choices.size() - 1.0;
            return n > 0.0 ? std::clamp (v / n, 0.0, 1.0) : 0.0;
        }
        const double lin = (v - r.lo) / (r.hi - r.lo);
        return std::clamp (std::pow (std::clamp (lin, 0.0, 1.0), 1.0 / r.skew), 0.0, 1.0);
    }

    std::string text (int i) const override
    {
        std::string s;
        textFor (i, s);
        return s;
    }

    void beginGesture (int) override {}
    void endGesture   (int) override {}

    void applyInit() override
    {
        for (size_t i = 0; i < rows.size(); ++i) norms[i] = rows[i].defNorm;
    }

    std::string presetListJson() override
    {
        return R"({"Factory":["default","Air Lift","Drum Glue","Vocal Sheen"],"User":[]})";
    }

    bool loadPresetNamed (const std::string&, const std::string&) override { return true; }
    void openLoadDialog() override {}
    void openSaveDialog() override {}

    OvMeters meters() const override
    {
        const double t = frozen ? 1.0 : now();
        OvMeters m;
        const double in = -22.0 + 7.0 * std::sin (t * 0.9) + 2.0 * std::sin (t * 4.7);
        m.inL  = (float) in;
        m.inR  = (float) (in - 1.7 + 0.8 * std::sin (t * 1.3));
        m.outL = (float) (in + 0.6);
        m.outR = (float) (m.inR + 0.6);
        m.harm = (float) (-14.0 + 3.0 * std::sin (t * 0.6));
        return m;
    }

    void spectrum (float* dst, int n) override
    {
        const double t = frozen ? 1.0 : now();
        for (int i = 0; i < n; ++i)
        {
            const double x = (double) i / (double) (n - 1);
            double v = 0.55 * std::exp (-std::pow ((x - 0.32) / 0.24, 2.0))
                     + 0.30 * std::exp (-std::pow ((x - 0.72) / 0.18, 2.0))
                     + 0.10 * (0.5 + 0.5 * std::sin (t * 3.0 + x * 9.0));
            v *= 0.92 + 0.08 * std::sin (t * 5.0 + x * 40.0);
            dst[i] = (float) std::clamp (v, 0.0, 1.0);
        }
    }

    double sampleRate() const override { return 48000.0; }
    int driveModeIndex() const override { return 1; }

private:
    struct Row
    {
        std::string id, label;
        double lo = 0.0, hi = 1.0, skew = 1.0, defNorm = 0.0;
        std::vector<std::string> choices;
        bool isBool = false;
    };

    static double now()
    {
        static const double t0 = (double) clock() / (double) CLOCKS_PER_SEC;
        return (double) clock() / (double) CLOCKS_PER_SEC - t0;
    }

    static double skewFromCentre (double lo, double hi, double centre)
    {
        const double lin = (centre - lo) / (hi - lo);
        return std::log (0.5) / std::log (lin);
    }

    static double normFromValue (double lo, double hi, double skew, double v)
    {
        const double lin = (v - lo) / (hi - lo);
        return std::clamp (std::pow (std::clamp (lin, 0.0, 1.0), 1.0 / skew), 0.0, 1.0);
    }

    void add (const std::string& id, const std::string& label, double lo, double hi,
              double skew, double defValue, std::vector<std::string> choices = {},
              bool isBool = false)
    {
        Row r;
        r.id = id;
        r.label = label;
        r.lo = lo; r.hi = hi; r.skew = skew;
        r.choices = std::move (choices);
        r.isBool = isBool;
        if (! r.choices.empty())
            r.defNorm = r.choices.size() > 1 ? defValue / (double) (r.choices.size() - 1) : 0.0;
        else
            r.defNorm = normFromValue (lo, hi, skew, defValue);
        rows.push_back (std::move (r));
        norms.push_back (rows.back().defNorm);
    }

    void buildTable()
    {
        const double driveSkew = skewFromCentre (0.0, 10.0, 2.0);
        const double freqSkew  = skewFromCentre (5.0, 20000.0, 800.0);
        const double qSkew     = skewFromCentre (0.1, 6.0, 0.71);
        const double airSkew   = skewFromCentre (2000.0, 20000.0, 9000.0);

        add ("drive",      "Drive",           0.0, 10.0, driveSkew, 0.0);
        add ("mix",        "Harmonics Mix",   0.0, 1.0, 1.0, 1.0);
        add ("driveType",  "Drive Type",      0, 2, 1.0, 0, { "Tanh", "Tube", "Tape" });
        add ("driveMode",  "Drive Mode",      0, 2, 1.0, 1, { "Classic", "Harmonic", "Schranz" });
        add ("osFactor",   "Oversampling",    0, 4, 1.0, 1, { "2x", "4x", "8x", "16x", "32x" });
        add ("inGain",     "Input Gain",    -24.0, 24.0, 1.0, 0.0);
        add ("outGain",    "Output Gain",   -24.0, 24.0, 1.0, 0.0);
        add ("deEmph",     "De-Emphasis",     0.0, 1.0, 1.0, 1.0);
        add ("harmGain",   "Harmonics Gain", -12.0, 24.0, 1.0, 0.0);
        add ("airProtect", "Air Protect",  2000.0, 20000.0, airSkew, 20000.0);
        add ("evenOdd",    "Even / Odd",     -1.0, 1.0, 1.0, 0.0);
        add ("msMode",     "Stereo Mode",     0, 3, 1.0, 0, { "Stereo", "Mid", "Side", "M/S" });
        add ("autoGain",   "Auto Gain",       0, 1, 1.0, 0, {}, true);
        add ("harmSolo",   "Solo Harmonics",  0, 1, 1.0, 0, {}, true);

        const double defF[5] = { 100.0, 250.0, 1200.0, 4000.0, 8500.0 };
        const int    defT[5] = { 2, 0, 0, 0, 3 };
        const std::vector<std::string> eqTypes {
            "Bell", "Notch", "Low Shelf", "High Shelf",
            "Low Cut 12", "Low Cut 24", "High Cut 12", "High Cut 24" };

        for (const char* pre : { "sc", "pq" })
        {
            const bool sc = (std::string (pre) == "sc");
            for (int b = 0; b < 5; ++b)
            {
                const std::string id (pre);
                const std::string nm = id + std::to_string (b + 1);
                const bool bump = (! sc && b == 3);
                add (nm + "Freq", "EQ " + nm + " Freq", 5.0, 20000.0, freqSkew,
                     bump ? 10000.0 : defF[b]);
                add (nm + "Gain", "EQ " + nm + " Gain", -12.0, 12.0, 1.0, 0.0);
                add (nm + "Q",    "EQ " + nm + " Q", 0.1, 6.0, qSkew, bump ? 0.9 : 0.707);
                add (nm + "Type", "EQ " + nm + " Type", 0, 7, 1.0, (double) defT[b], eqTypes);
            }
        }

        add ("scRange", "SC EQ Range", 0, 3, 1.0, 0,
             { "+/-12 dB", "+/-24 dB", "+/-48 dB", "+/-90 dB" });
        add ("pqRange", "Post EQ Range", 0, 3, 1.0, 2,
             { "+/-3 dB", "+/-6 dB", "+/-12 dB", "+/-30 dB" });
        add ("protectMode", "Limiter Mode", 0, 3, 1.0, 2,
             { "Off", "Clip", "Modern", "True Peak" });
        add ("limiterOs", "Limiter Oversampling", 0, 3, 1.0, 1,
             { "2x", "4x", "8x", "16x" });
    }

    void textFor (int i, std::string& out) const
    {
        const auto& r = rows[(size_t) i];
        const double n = norms[(size_t) i];
        char buf[64];
        if (! r.choices.empty() || r.isBool) { out.clear(); return; }

        const double v = r.lo + (r.hi - r.lo) * n;
        if (r.id == "mix" || r.id == "deEmph")
            std::snprintf (buf, sizeof buf, "%d%%", (int) std::lround (v * 100.0));
        else if (r.id == "drive")
            std::snprintf (buf, sizeof buf, "%.2f", v);
        else if (r.id == "evenOdd")
        {
            if (std::abs (v) < 0.05) std::snprintf (buf, sizeof buf, "BAL");
            else if (v < 0) std::snprintf (buf, sizeof buf, "E %d", (int) std::lround (-v * 100.0));
            else std::snprintf (buf, sizeof buf, "O %d", (int) std::lround (v * 100.0));
        }
        else if (r.id == "airProtect")
        {
            if (v >= 19500.0) std::snprintf (buf, sizeof buf, "OFF");
            else if (v >= 1000.0) std::snprintf (buf, sizeof buf, "%.1fk", v / 1000.0);
            else std::snprintf (buf, sizeof buf, "%d", (int) std::lround (v));
        }
        else
            std::snprintf (buf, sizeof buf, "%+.1f", v);
        out = buf;
    }

    std::vector<Row> rows;
    std::vector<double> norms;
};

inline bool OvMockSource::frozen = false;
