#include "OvJuceSource.h"

#include "OverteonProcessor.h"

#include <algorithm>
#include <cmath>

namespace
{
    juce::String qs (const std::string& s) { return juce::String::fromUTF8 (s.c_str()); }
    std::string  st (const juce::String& s) { return s.toStdString(); }
}

OvJuceSource::OvJuceSource (OverteonProcessor& processor)
    : proc (processor)
{
    // JUCE's APVTS does not expose its ID list; the state tree carries one
    // PARAM child per parameter, in creation order.
    const auto& state = proc.apvts.state;
    for (int i = 0; i < state.getNumChildren(); ++i)
    {
        const auto child = state.getChild (i);
        if (! child.hasType ("PARAM")) continue;

        const auto id = child.getProperty ("id").toString();
        if (id.isEmpty() || ids.contains (id)) continue;

        if (auto* p = proc.apvts.getParameter (id))
        {
            ids.add (id);
            params.push_back (p);
        }
    }
}

OvJuceSource::~OvJuceSource() = default;

juce::AudioProcessorParameter* OvJuceSource::param (int index) const
{
    if (index < 0 || index >= (int) params.size()) return nullptr;
    return params[(size_t) index];
}

OvParamSpec OvJuceSource::spec (int index) const
{
    OvParamSpec s;
    if (index < 0 || index >= ids.size()) return s;

    s.id    = st (ids[index]);
    auto* p = param (index);
    if (p == nullptr) return s;

    s.label = st (p->getName (64));
    s.defaultNorm = p->getDefaultValue();

    if (auto* f = dynamic_cast<juce::AudioParameterFloat*> (p))
    {
        s.minValue = f->range.start;
        s.maxValue = f->range.end;
    }
    else if (auto* c = dynamic_cast<juce::AudioParameterChoice*> (p))
    {
        s.minValue = 0.0;
        s.maxValue = juce::jmax (0, c->choices.size() - 1);
        for (const auto& ch : c->choices) s.choices.push_back (st (ch));
    }
    else if (dynamic_cast<juce::AudioParameterBool*> (p) != nullptr)
    {
        s.minValue = 0.0;
        s.maxValue = 1.0;
        s.isBool = true;
    }
    else
    {
        s.minValue = 0.0;
        s.maxValue = 1.0;
    }
    return s;
}

double OvJuceSource::getNorm (int index) const
{
    if (auto* p = param (index)) return (double) p->getValue();
    return 0.0;
}

void OvJuceSource::setNorm (int index, double norm)
{
    if (auto* p = param (index))
        p->setValueNotifyingHost ((float) juce::jlimit (0.0, 1.0, norm));
}

double OvJuceSource::normForValue (int index, double value) const
{
    auto* p = param (index);
    if (p == nullptr) return 0.0;

    if (auto* f = dynamic_cast<juce::AudioParameterFloat*> (p))
        return (double) f->convertTo0to1 ((float) value);

    if (auto* c = dynamic_cast<juce::AudioParameterChoice*> (p))
    {
        const int n = juce::jmax (1, c->choices.size() - 1);
        return juce::jlimit (0.0, 1.0, value / (double) n);
    }

    return juce::jlimit (0.0, 1.0, value);
}

double OvJuceSource::valueForNorm (int index, double norm) const
{
    auto* p = param (index);
    if (p == nullptr) return 0.0;

    norm = juce::jlimit (0.0, 1.0, norm);

    if (auto* f = dynamic_cast<juce::AudioParameterFloat*> (p))
        return (double) f->convertFrom0to1 ((float) norm);

    if (auto* c = dynamic_cast<juce::AudioParameterChoice*> (p))
    {
        const int n = juce::jmax (0, c->choices.size() - 1);
        return (double) juce::jlimit (0, n, (int) std::lround (norm * (double) n));
    }

    return norm;
}

std::string OvJuceSource::text (int index) const
{
    if (auto* p = param (index)) return st (p->getCurrentValueAsText());
    return {};
}

void OvJuceSource::beginGesture (int index)
{
    if (auto* p = param (index)) p->beginChangeGesture();
}

void OvJuceSource::endGesture (int index)
{
    if (auto* p = param (index)) p->endChangeGesture();
}

void OvJuceSource::applyInit()
{
    proc.applyInit();
}

// ---- A/B snapshots ----------------------------------------------------------
//
// Read and written through the HOST's parameter interface, not the APVTS map, so
// a gesture is reported for each value: a snapshot recall that skipped
// begin/endChangeGesture would leave the host's automation view showing values
// that no longer match the sound.
OvSource::Snapshot OvJuceSource::snapshotParams()
{
    Snapshot s;
    const int n = count();
    s.norm.resize ((size_t) n);
    for (int i = 0; i < n; ++i) s.norm[(size_t) i] = getNorm (i);
    s.valid = true;
    return s;
}

void OvJuceSource::restoreParams (const Snapshot& s)
{
    // A stale snapshot (different parameter count) must not half-apply: a
    // partially restored set is a state the user never asked for and cannot
    // reproduce.
    if (! s.valid || (int) s.norm.size() != count()) return;

    const int n = count();
    for (int i = 0; i < n; ++i)
    {
        if (auto* p = param (i))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost ((float) juce::jlimit (0.0, 1.0, s.norm[(size_t) i]));
            p->endChangeGesture();
        }
    }
}

OvMeters OvJuceSource::meters() const
{
    OvMeters m;
    m.inL  = juce::Decibels::gainToDecibels (proc.meterInL.load(),  -90.0f);
    m.inR  = juce::Decibels::gainToDecibels (proc.meterInR.load(),  -90.0f);
    m.outL = juce::Decibels::gainToDecibels (proc.meterOutL.load(), -90.0f);
    m.outR = juce::Decibels::gainToDecibels (proc.meterOutR.load(), -90.0f);
    m.inPkL  = juce::Decibels::gainToDecibels (proc.meterInPkL.load(),  -90.0f);
    m.inPkR  = juce::Decibels::gainToDecibels (proc.meterInPkR.load(),  -90.0f);
    m.outPkL = juce::Decibels::gainToDecibels (proc.meterOutPkL.load(), -90.0f);
    m.outPkR = juce::Decibels::gainToDecibels (proc.meterOutPkR.load(), -90.0f);
    m.gr     = proc.meterGr.load();
    m.grPeak = proc.meterGrPk.load();
    m.harm = proc.meterHarm.load();
    m.harmPeak = proc.meterHarmPk.load();
    return m;
}

bool OvJuceSource::spectrum (float* dest, int count)
{
    return analyse (dest, count, false);   // the output tap: leaves the processor
}

bool OvJuceSource::spectrumIn (float* dest, int count)
{
    return analyse (dest, count, true);    // the input tap: enters the processor
}

bool OvJuceSource::analyse (float* dest, int count, bool inTap)
{
    // ---- continuous analysis -------------------------------------------
    //
    // The audio thread appends every sample to a ring, so there is always a
    // fresh window available.  Taking the last fftSize samples here (on the UI
    // thread, at the display's rate) is what makes the curve move every frame:
    // the old tap published one block per fftSize samples and the display had
    // nothing new in between, which read as stepping.
    auto& written = inTap ? proc.specInWritten : proc.specOutWritten;
    const uint32_t w = written.load (std::memory_order_acquire);
    uint32_t& last = inTap ? lastInWritten : lastOutWritten;
    if (w == last) return false;                    // no new samples: keep last picture
    last = w;
    if (w < (uint32_t) OverteonProcessor::fftSize) return false;   // not enough history yet

    auto&  buf  = inTap ? fftInBuf : fftBuf;
    auto&  ring = inTap ? proc.specRingIn : proc.specRingOut;
    auto&  f    = inTap ? fftIn : fft;

    std::fill (buf.begin(), buf.end(), 0.0f);
    const uint32_t start = w - (uint32_t) OverteonProcessor::fftSize;
    const uint32_t mask  = (uint32_t) (OverteonProcessor::specRingSize - 1);
    for (int i = 0; i < OverteonProcessor::fftSize; ++i)
        buf[(size_t) i] = ring[(size_t) ((start + (uint32_t) i) & mask)];

    window.multiplyWithWindowingTable (buf.data(), OverteonProcessor::fftSize);
    f.performFrequencyOnlyForwardTransform (buf.data());

    const float fsr = juce::jmax (1.0f, (float) proc.getSampleRate());
    const int   maxBin = OverteonProcessor::fftSize / 2 - 1;

    // One pow instead of one per bin: 1024 bins at 60 Hz per tap adds up, and
    // a running product produces the same log spacing.
    const float ratio = std::pow (1000.0f, 1.0f / (float) juce::jmax (1, count - 1));
    float fr = 20.0f;

    // ---- display tilt, dB per octave -------------------------------------
    //
    // White-ish material falls about 4.5 dB per octave, so an untilted
    // analyser draws a long downhill and the top octaves look empty even when
    // there is plenty there.  Every mainstream analyser compensates on the
    // DISPLAY (SPAN's default slope, Pro-Q's Slope control); this is that
    // compensation and it touches only the plotted value, never the audio.
    // 4.5 dB/oct is SPAN's default; 1 kHz is the pivot so the overall level
    // stays where it was.  Override with OVERTEON_SPECTILT (0 restores the
    // old untilted display).
    static const double tiltDbPerOct = [] {
        const auto s = juce::SystemStats::getEnvironmentVariable ("OVERTEON_SPECTILT", {});
        return s.isEmpty() ? 4.5 : s.getDoubleValue();
    }();

    for (int i = 0; i < count; ++i)
    {

        // ---- one display bin = one log-frequency column -----------------
        //
        // Below ~1 kHz a display bin is narrower than an FFT bin, so the value
        // is interpolated between the two neighbours: that is what turns the
        // left end from a staircase of plateaus into a ramp.
        //
        // Above that the column spans several FFT bins, and the column takes
        // the LOUDEST one rather than a point sample.  Point sampling there was
        // wrong in a way that showed: a narrow 10 kHz tone is 5.9 Hz wide while
        // a display bin is ~135 Hz, so whether the tone appeared at all
        // depended on where the sample happened to fall — a steady sine could
        // vanish.  The max is also what gives the reference's dense, spiky
        // high end: neighbouring columns pick different FFT bins, so the
        // outline follows the analyser bin by bin instead of an averaged
        // envelope.
        //
        // Below the FIRST usable bin there is no data — bin 0 is DC and, with a
        // Hann window, holds the window sum, which twitches as the waveform
        // slides through the buffer.  Clamp to bin 1 instead, or the fill drops
        // at the left edge.
        const float raw0 = fr / fsr * (float) OverteonProcessor::fftSize;
        const double tiltDb = tiltDbPerOct * std::log2 ((double) fr / 1000.0);
        fr *= ratio;
        const float raw1 = fr / fsr * (float) OverteonProcessor::fftSize;

        float mag;
        if (raw1 - raw0 <= 1.0f)
        {
            const float pos  = juce::jlimit (1.0f, (float) maxBin, raw0);
            const int   b0   = (int) pos;
            const int   b1   = juce::jmin (maxBin, b0 + 1);
            const float t    = pos - (float) b0;
            mag = (buf[(size_t) b0] + (buf[(size_t) b1] - buf[(size_t) b0]) * t)
                / (float) OverteonProcessor::fftSize * 4.0f;
        }
        else
        {
            const int b0 = juce::jlimit (1, maxBin, (int) std::floor (raw0));
            const int b1 = juce::jlimit (b0, maxBin, (int) std::ceil (raw1));
            float mx = 0.0f;
            for (int b = b0; b <= b1; ++b) mx = juce::jmax (mx, buf[(size_t) b]);
            // Hann coherent gain is 0.5, so a full-scale sine at bin centre
            // gives N/4, not N/2: 4/N is the calibration that makes 0 dBFS
            // really 0 dBFS.
            mag = mx / (float) OverteonProcessor::fftSize * 4.0f;
        }

        // The floor is the analyser's "nothing here", not a measurement: keep
        // it at the bottom instead of tilting it up into a rising line, or
        // digital silence would draw a slope on the right half.
        const float measDb = juce::Decibels::gainToDecibels (mag, -90.0f);
        if (measDb <= -89.5f)
        {
            dest[i] = 0.0f;
        }
        else
        {
            const float db = measDb + (float) tiltDb;
            dest[i] = juce::jlimit (0.0f, 1.0f, juce::jmap (db, -90.0f, 6.0f, 0.0f, 1.0f));
        }
    }
    return true;
}

double OvJuceSource::sampleRate() const
{
    return juce::jmax (1.0, proc.getSampleRate());
}

int OvJuceSource::driveModeIndex() const
{
    if (auto* v = proc.apvts.getRawParameterValue ("driveMode"))
        return (int) std::lround (v->load());
    return 1;
}

// ------------------------------------------------------------
//  Presets — same on-disk format and layout as the WebView editor:
//  <bundle>/presets/{Factory,User}/*.hinj
// ------------------------------------------------------------
juce::File OvJuceSource::presetsDirectory() const
{
    auto exe = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
    auto dir = exe.getParentDirectory();
    auto contents = dir.getFileName() == "Contents" ? dir : dir.getParentDirectory();
    return contents.getChildFile ("presets");
}

juce::File OvJuceSource::presetRoot() const
{
    auto dir = presetsDirectory();
    dir.createDirectory();
    dir.getChildFile ("Factory").createDirectory();
    dir.getChildFile ("User").createDirectory();

    if (! dir.isDirectory())
    {
        dir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                  .getChildFile ("Eonun").getChildFile ("Overteon Presets");
        dir.createDirectory();
        dir.getChildFile ("Factory").createDirectory();
        dir.getChildFile ("User").createDirectory();
    }
    ensureDefaultPreset (dir);
    return dir;
}

void OvJuceSource::ensureDefaultPreset (const juce::File& root) const
{
    const auto def = root.getChildFile ("Factory").getChildFile ("default.hinj");
    if (def.existsAsFile()) return;
    if (auto xml = proc.apvts.copyState().createXml())
        xml->writeTo (def);
}

namespace
{
    // One folder of the preset tree:
    //
    //   { "name": "Factory",
    //     "folders": [ <folder>, ... ],
    //     "presets": [ "Air Lift", ... ] }
    //
    // The scan is RECURSIVE and does not know any folder names: Factory and
    // User are created by presetRoot() but every other directory is reported
    // exactly the same way, and a folder inside a folder becomes another node.
    // The QML menu walks this tree, so depth is not limited anywhere.
    juce::var presetFolderToVar (const juce::File& dir)
    {
        juce::Array<juce::File> dirs, files;

        for (auto& e : juce::RangedDirectoryIterator (dir, false, "*",
                     juce::File::findDirectories | juce::File::ignoreHiddenFiles))
            dirs.add (e.getFile());

        for (auto& e : juce::RangedDirectoryIterator (dir, false, "*.hinj",
                     juce::File::findFiles | juce::File::ignoreHiddenFiles))
            files.add (e.getFile());

        const auto byName = [] (const juce::File& a, const juce::File& b)
        {
            return a.getFileName().compareIgnoreCase (b.getFileName()) < 0;
        };
        std::sort (dirs.begin(), dirs.end(), byName);
        std::sort (files.begin(), files.end(), byName);

        auto* obj = new juce::DynamicObject();
        obj->setProperty ("name", dir.getFileName());

        juce::Array<juce::var> subfolders;
        for (auto& d : dirs) subfolders.add (presetFolderToVar (d));

        juce::Array<juce::var> presets;
        for (auto& f : files) presets.add (f.getFileNameWithoutExtension());

        obj->setProperty ("folders", subfolders);
        obj->setProperty ("presets", presets);
        return juce::var (obj);
    }
}

std::string OvJuceSource::presetListJson()
{
    return st (juce::JSON::toString (presetFolderToVar (presetRoot())));
}

void OvJuceSource::applyPresetFile (const juce::File& f)
{
    if (! f.existsAsFile()) return;
    if (auto xml = juce::XmlDocument::parse (f))
        if (xml->hasTagName (proc.apvts.state.getType()))
        {
            // Oversampling is a global preference, never preset data.
            const float osKeep = proc.apvts.getRawParameterValue ("osFactor")->load();
            proc.apvts.replaceState (juce::ValueTree::fromXml (*xml));
            proc.apvts.getParameterAsValue ("osFactor") = juce::jlimit (0.0f, 4.0f, osKeep);
        }
}

bool OvJuceSource::loadPresetNamed (const std::string& folder, const std::string& name)
{
    // `folder` is now the RELATIVE PATH of the folder inside presets/, e.g.
    // "Factory" or "Factory/Drums/Kicks" — the menu can nest without limit.
    const auto base = presetRoot();
    auto dir = base;
    for (auto& part : juce::StringArray::fromTokens (qs (folder), "/\\", ""))
    {
        if (part.isEmpty() || part == ".") continue;
        if (part == "..") return false;
        dir = dir.getChildFile (part);
    }

    auto f = dir.getChildFile (qs (name) + ".hinj");
    if (! f.isAChildOf (base) || ! f.existsAsFile()) return false;
    applyPresetFile (f);
    if (presetNameChanged) presetNameChanged (qs (name));
    return true;
}

void OvJuceSource::openLoadDialog()
{
    chooser = std::make_unique<juce::FileChooser> ("Load preset", presetRoot(), "*.hinj");
    chooser->launchAsync (juce::FileBrowserComponent::openMode
                        | juce::FileBrowserComponent::canSelectFiles,
        [this] (const juce::FileChooser& fc)
        {
            const auto f = fc.getResult();
            applyPresetFile (f);
            if (f.existsAsFile() && presetNameChanged)
                presetNameChanged (f.getFileNameWithoutExtension());
        });
}

void OvJuceSource::openSaveDialog()
{
    auto initial = presetRoot()
        .getChildFile ("User")
        .getChildFile ("user-preset.hinj");

    chooser = std::make_unique<juce::FileChooser> ("Save preset as", initial, "*.hinj");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode
                        | juce::FileBrowserComponent::warnAboutOverwriting,
        [this] (const juce::FileChooser& fc)
        {
            const auto f = fc.getResult();
            if (f == juce::File()) return;
            const auto target = f.withFileExtension ("hinj");
            if (auto xml = proc.apvts.copyState().createXml())
                xml->writeTo (target);
            if (presetNameChanged)
                presetNameChanged (target.getFileNameWithoutExtension());
        });
}
