#include "OvJuceSource.h"

#include "FxProcessor.h"

#include <algorithm>
#include <cmath>

namespace
{
    juce::String qs (const std::string& s) { return juce::String::fromUTF8 (s.c_str()); }
    std::string  st (const juce::String& s) { return s.toStdString(); }
}

OvJuceSource::OvJuceSource (InjectorProcessor& processor)
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

OvMeters OvJuceSource::meters() const
{
    OvMeters m;
    m.inL  = juce::Decibels::gainToDecibels (proc.meterInL.load(),  -90.0f);
    m.inR  = juce::Decibels::gainToDecibels (proc.meterInR.load(),  -90.0f);
    m.outL = juce::Decibels::gainToDecibels (proc.meterOutL.load(), -90.0f);
    m.outR = juce::Decibels::gainToDecibels (proc.meterOutR.load(), -90.0f);
    m.harm = proc.meterHarm.load();
    return m;
}

void OvJuceSource::spectrum (float* dest, int count)
{
    std::fill (dest, dest + count, 0.0f);
    if (! proc.fftReady.load()) return;

    std::fill (fftBuf.begin(), fftBuf.end(), 0.0f);
    std::copy (proc.fftData.begin(),
               proc.fftData.begin() + InjectorProcessor::fftSize, fftBuf.begin());
    proc.fftReady = false;

    window.multiplyWithWindowingTable (fftBuf.data(), InjectorProcessor::fftSize);
    fft.performFrequencyOnlyForwardTransform (fftBuf.data());

    const float fsr = juce::jmax (1.0f, (float) proc.getSampleRate());
    for (int i = 0; i < count; ++i)
    {
        const float frac = (float) i / (float) juce::jmax (1, count - 1);
        const float fr   = 20.0f * std::pow (1000.0f, frac);
        const int   bin  = juce::jlimit (0, InjectorProcessor::fftSize / 2,
                                         (int) (fr / fsr * (float) InjectorProcessor::fftSize));
        const float mag  = fftBuf[(size_t) bin] / (float) InjectorProcessor::fftSize * 8.0f;
        const float db   = juce::Decibels::gainToDecibels (mag, -90.0f);
        dest[i] = juce::jlimit (0.0f, 1.0f, juce::jmap (db, -85.0f, 0.0f, 0.0f, 1.0f));
    }
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
                  .getChildFile ("Eonun").getChildFile ("OnyxTEON Presets");
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

std::string OvJuceSource::presetListJson()
{
    auto root = new juce::DynamicObject();
    for (auto* folder : { "Factory", "User" })
    {
        std::vector<juce::String> names;
        const auto d = presetRoot().getChildFile (folder);
        if (d.isDirectory())
            for (auto& e : juce::RangedDirectoryIterator (d, false, "*.hinj",
                                                          juce::File::findFiles))
                names.push_back (e.getFile().getFileNameWithoutExtension());

        std::sort (names.begin(), names.end(),
                   [] (const juce::String& a, const juce::String& b)
                   { return a.compareIgnoreCase (b) < 0; });

        juce::Array<juce::var> items;
        for (auto& n : names) items.add (n);
        root->setProperty (folder, items);
    }
    return st (juce::JSON::toString (juce::var (root)));
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
    const auto base = presetRoot();
    auto f = base.getChildFile (qs (folder)).getChildFile (qs (name) + ".hinj");
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
