#pragma once
// ============================================================
//  Harmonic Injector — WebView editor (same battle-tested
//  DPI / jiggle / sentinel machinery as Supersaw Omega Web).
// ============================================================
#include "OverteonProcessor.h"
#include "FxData.h"

class InjectorWebEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit InjectorWebEditor (OverteonProcessor& p)
        : juce::AudioProcessorEditor (p), proc (p)
    {
        const auto ids = allParamIDs();
        for (const auto& id : ids)
            relays.push_back (std::make_unique<juce::WebSliderRelay> (id));

        auto opts = juce::WebBrowserComponent::Options {}
            .withNativeIntegrationEnabled()
            .withResourceProvider ([this] (const auto& url) { return getResource (url); })
            .withNativeFunction ("applyInit",
                [this] (const juce::Array<juce::var>&, auto complete)
                { proc.applyInit(); complete (true); })
            .withNativeFunction ("savePreset",
                [this] (const juce::Array<juce::var>& args, auto complete)
                {
                    savePresetFile (args.isEmpty() ? juce::String() : args[0].toString());
                    complete (true);
                })
            .withNativeFunction ("loadPreset",
                [this] (const juce::Array<juce::var>&, auto complete)
                { loadPresetFile(); complete (true); })
            .withNativeFunction ("listPresets",
                [this] (const juce::Array<juce::var>&, auto complete)
                { complete (juce::var (listPresetsJson())); })
            .withNativeFunction ("loadPresetAt",
                [this] (const juce::Array<juce::var>& args, auto complete)
                {
                    if (args.size() >= 2)
                        loadPresetNamed (args[0].toString(), args[1].toString());
                    complete (true);
                })
            .withNativeFunction ("openExternal",
                [] (const juce::Array<juce::var>& args, auto complete)
                {
                    if (! args.isEmpty())
                        juce::URL (args[0].toString()).launchInDefaultBrowser();
                    complete (true);
                });

       #if JUCE_WINDOWS
        opts = opts.withBackend (juce::WebBrowserComponent::Options::Backend::webview2)
                   .withWinWebView2Options (
                       juce::WebBrowserComponent::Options::WinWebView2 {}
                           .withUserDataFolder (juce::File::getSpecialLocation (
                               juce::File::tempDirectory)));
       #endif

        for (auto& r : relays)
            opts = opts.withOptionsFrom (*r);

        browser = std::make_unique<juce::WebBrowserComponent> (opts);
        addAndMakeVisible (*browser);

        for (int i = 0; i < ids.size(); ++i)
            if (auto* param = proc.apvts.getParameter (ids[i]))
                attachments.push_back (
                    std::make_unique<juce::WebSliderParameterAttachment> (
                        *param, *relays[(size_t) i], nullptr));

        browser->goToURL (juce::WebBrowserComponent::getResourceProviderRoot());
        setResizable (true, true);
        setResizeLimits (560, 416, 1960, 1456);
        if (auto* c = getConstrainer())
            c->setFixedAspectRatio (840.0 / 624.0);
        if (const auto bounds = readWindowGeometry())
        {
            setSize (bounds->getWidth(), bounds->getHeight());
            pendingPosition = bounds->getPosition();
            restorePosition = true; // restore after the host creates the window peer
        }
        else
            setSize (840, 624);
        startTimerHz (30);
    }

    ~InjectorWebEditor() override { saveWindowGeometry(); }

    void resized() override
    {
        if (browser == nullptr) return;
        const float s = hostScale > 0.01f ? hostScale : 1.0f;
        browser->setBounds (0, 0,
                            juce::roundToInt ((float) getWidth()  * s),
                            juce::roundToInt ((float) getHeight() * s));
    }

    void setScaleFactor (float newScale) override
    {
        juce::AudioProcessorEditor::setScaleFactor (newScale);
        hostScale = newScale;
        resized();
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0xff0b0709));
    }

private:
    static juce::File getEditorSettingsFile()
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
            .getChildFile ("Eonun")
            .getChildFile ("OverteonEditor.settings");
    }

    static std::optional<juce::Rectangle<int>> readWindowGeometry()
    {
        const auto f = getEditorSettingsFile();
        if (! f.existsAsFile()) return std::nullopt;
        if (auto xml = juce::XmlDocument::parse (f))
            if (xml->hasTagName ("OVERTEONEDITOR"))
            {
                const juce::Rectangle<int> b (xml->getIntAttribute ("x"),
                                              xml->getIntAttribute ("y"),
                                              xml->getIntAttribute ("w", 840),
                                              xml->getIntAttribute ("h", 624));
                if (b.getWidth() >= 560 && b.getWidth() <= 1960
                    && b.getHeight() >= 416 && b.getHeight() <= 1456)
                    return b;
            }
        return std::nullopt;
    }

    void saveWindowGeometry()
    {
        const auto b = getScreenBounds();
        if (b.getWidth() < 100 || b.getHeight() < 100) return;
        juce::XmlElement x ("OVERTEONEDITOR");
        x.setAttribute ("x", b.getX());
        x.setAttribute ("y", b.getY());
        x.setAttribute ("w", b.getWidth());
        x.setAttribute ("h", b.getHeight());
        const auto f = getEditorSettingsFile();
        f.getParentDirectory().createDirectory();
        x.writeTo (f);
        geometryDirty = false;
    }

    static juce::StringArray allParamIDs()
    {
        juce::StringArray ids { "drive", "mix", "driveType", "driveMode",
                                "osFactor", "inGain", "outGain",
                                "deEmph", "harmGain", "harmSolo" };
        for (auto* pre : { "sc", "pq" })
            for (int b = 1; b <= 5; ++b)
                for (auto* s : { "Freq", "Gain", "Q" })
                    ids.add (juce::String (pre) + juce::String (b) + s);
        for (auto* pre : { "sc", "pq" })
            for (int b = 1; b <= 5; ++b)
                ids.add (juce::String (pre) + juce::String (b) + "Type");
        ids.addArray ({ "airProtect", "evenOdd", "msMode", "autoGain",
                        "scRange", "pqRange", "protectMode", "limiterOs" });
        return ids;
    }

    std::optional<juce::WebBrowserComponent::Resource> getResource (const juce::String& url)
    {
        const auto makeRes = [] (const char* data, int size, const char* mime)
        {
            juce::WebBrowserComponent::Resource r;
            r.data.assign (reinterpret_cast<const std::byte*> (data),
                           reinterpret_cast<const std::byte*> (data) + size);
            r.mimeType = mime;
            return r;
        };
        const auto path = (url == "/" || url.isEmpty())
                              ? juce::String ("index.html")
                              : url.fromFirstOccurrenceOf ("/", false, false);
        if (path == "index.html")
            return makeRes (FxData::index_html, FxData::index_htmlSize, "text/html");
        if (path == "eonun_logo.png")
            return makeRes (FxData::eonun_logo_png, FxData::eonun_logo_pngSize, "image/png");
        if (path == "edix.otf")
            return makeRes (FxData::edix_otf, FxData::edix_otfSize, "font/otf");
        return std::nullopt;
    }

    void timerCallback() override
    {
        if (restorePosition && getPeer() != nullptr)
        {
            if (auto* top = getTopLevelComponent())
                top->setTopLeftPosition (pendingPosition);
            restorePosition = false;
        }

        // A host may move the top-level window without calling moved() here.
        const auto geometry = getScreenBounds();
        if (geometry != lastGeometry)
        {
            if (! lastGeometry.isEmpty())
            {
                geometryDirty = true;
                geometrySaveCountdown = 15; // write after about 0.5 s of stable geometry
            }
            lastGeometry = geometry;
        }
        if (geometryDirty && --geometrySaveCountdown <= 0)
            saveWindowGeometry();

        if (jiggleTick >= 0)
        {
            ++jiggleTick;
            if (jiggleTick == 12)
            {
                jigW = getWidth(); jigH = getHeight();
                setSize (jigW + 2, jigH + 2);
            }
            else if (jiggleTick == 18)
            {
                setSize (jigW, jigH);
                jiggleTick = -1;
            }
        }

        {
            const float s = hostScale > 0.01f ? hostScale : 1.0f;
            const juce::Rectangle<int> want (0, 0,
                juce::roundToInt ((float) getWidth()  * s),
                juce::roundToInt ((float) getHeight() * s));
            if (browser != nullptr && browser->getBounds() != want)
                browser->setBounds (want);
        }

        {
            auto* o = new juce::DynamicObject();
            o->setProperty ("il", juce::Decibels::gainToDecibels (proc.meterInL.load(),  -90.0f));
            o->setProperty ("ir", juce::Decibels::gainToDecibels (proc.meterInR.load(),  -90.0f));
            o->setProperty ("ol", juce::Decibels::gainToDecibels (proc.meterOutL.load(), -90.0f));
            o->setProperty ("or", juce::Decibels::gainToDecibels (proc.meterOutR.load(), -90.0f));
            o->setProperty ("hm", proc.meterHarm.load());
            browser->emitEventIfBrowserIsVisible ("meter", juce::var (o));
        }

        if (! proc.fftReady.load()) return;
        std::fill (fftBuf.begin(), fftBuf.end(), 0.0f);
        std::copy (proc.fftData.begin(),
                   proc.fftData.begin() + OverteonProcessor::fftSize, fftBuf.begin());
        proc.fftReady = false;

        window.multiplyWithWindowingTable (fftBuf.data(), OverteonProcessor::fftSize);
        fft.performFrequencyOnlyForwardTransform (fftBuf.data());
        const float fsr = juce::jmax (1.0f, (float) proc.getSampleRate());
        juce::Array<juce::var> out;
        for (int i = 0; i < 128; ++i)
        {
            const float frac = (float) i / 127.0f;
            const float fr   = 20.0f * std::pow (1000.0f, frac);
            const int bin    = juce::jlimit (0, OverteonProcessor::fftSize / 2,
                                  (int) (fr / fsr * (float) OverteonProcessor::fftSize));
            const float mag  = fftBuf[(size_t) bin] / (float) OverteonProcessor::fftSize * 8.0f;
            const float db   = juce::Decibels::gainToDecibels (mag, -90.0f);
            out.add (juce::jlimit (0.0f, 1.0f,
                         juce::jmap (db, -85.0f, 0.0f, 0.0f, 1.0f)));
        }
        browser->emitEventIfBrowserIsVisible ("spectrum", out);
    }

    // Presets live inside the plugin bundle: Overteon.vst3/Contents/presets,
    // split into Factory and User subfolders. If the bundle is read-only the
    // chooser falls back to a writable per-user folder so import/export still works.
    juce::File presetRoot() const
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

        const auto def = dir.getChildFile ("Factory").getChildFile ("default.hinj");
        if (! def.existsAsFile())
            def.replaceWithText (R"XML(<HINJ drive="0" mix="1" driveType="0" driveMode="1" osFactor="1"
      inGain="0" outGain="0" deEmph="1" harmGain="0" airProtect="20000"
      evenOdd="0" msMode="0" autoGain="0" harmSolo="0"
      sc1Freq="100" sc1Gain="0" sc1Q="0.707" sc1Type="2"
      sc2Freq="250" sc2Gain="0" sc2Q="0.707" sc2Type="0"
      sc3Freq="1200" sc3Gain="0" sc3Q="0.707" sc3Type="0"
      sc4Freq="4000" sc4Gain="0" sc4Q="0.707" sc4Type="0"
      sc5Freq="8500" sc5Gain="0" sc5Q="0.707" sc5Type="3"
      pq1Freq="100" pq1Gain="0" pq1Q="0.707" pq1Type="2"
      pq2Freq="250" pq2Gain="0" pq2Q="0.707" pq2Type="0"
      pq3Freq="1200" pq3Gain="0" pq3Q="0.707" pq3Type="0"
      pq4Freq="10000" pq4Gain="0" pq4Q="0.9" pq4Type="0"
      pq5Freq="8500" pq5Gain="0" pq5Q="0.707" pq5Type="3"
      scRange="0" pqRange="2" protectMode="2" limiterOs="1"/>)XML");
        return dir;
    }
    juce::File presetsDirectory() const
    {
        auto exe = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
        auto dir = exe.getParentDirectory();
        auto contents = dir.getFileName() == "Contents" ? dir : dir.getParentDirectory();
        return contents.getChildFile ("presets");
    }

    juce::String listPresetsJson()
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
        return juce::JSON::toString (juce::var (root));
    }

    void applyPresetFile (const juce::File& f)
    {
        if (! f.existsAsFile()) return;
        if (auto xml = juce::XmlDocument::parse (f))
            if (xml->hasTagName (proc.apvts.state.getType()))
            {
                // oversampling is a global preference, not preset data:
                // keep the current choice across .hinj loads as well.
                const float osKeep = proc.apvts.getRawParameterValue ("osFactor")->load();
                proc.apvts.replaceState (juce::ValueTree::fromXml (*xml));
                proc.apvts.getParameterAsValue ("osFactor") =
                    juce::jlimit (0.0f, 4.0f, osKeep);
            }
    }

    void loadPresetNamed (const juce::String& folder, const juce::String& name)
    {
        const auto base = presetRoot();
        const auto f = base.getChildFile (folder).getChildFile (name + ".hinj");
        if (! f.isAChildOf (base)) return;
        applyPresetFile (f);
    }

    void savePresetFile (const juce::String& suggestedName)
    {
        auto initial = presetRoot();
        if (suggestedName.isNotEmpty())
            initial = initial.getChildFile (juce::File::createLegalFileName (suggestedName)
                                                + ".hinj");
        chooser = std::make_unique<juce::FileChooser> ("Save preset as",
            initial, "*.hinj");
        chooser->launchAsync (juce::FileBrowserComponent::saveMode
                            | juce::FileBrowserComponent::warnAboutOverwriting,
            [this] (const juce::FileChooser& fc)
            {
                auto f = fc.getResult();
                if (f == juce::File()) return;
                if (auto xml = proc.apvts.copyState().createXml())
                {
                    xml->writeTo (f.withFileExtension ("hinj"));
                    browser->emitEventIfBrowserIsVisible (
                        "presetName", juce::var (f.getFileNameWithoutExtension()));
                }
            });
    }

    void loadPresetFile()
    {
        chooser = std::make_unique<juce::FileChooser> ("Load preset",
            presetRoot(), "*.hinj");
        chooser->launchAsync (juce::FileBrowserComponent::openMode
                            | juce::FileBrowserComponent::canSelectFiles,
            [this] (const juce::FileChooser& fc)
            {
                const auto f = fc.getResult();
                applyPresetFile (f);
                if (f.existsAsFile())
                    browser->emitEventIfBrowserIsVisible (
                        "presetName", juce::var (f.getFileNameWithoutExtension()));
            });
    }

    OverteonProcessor& proc;
    float hostScale = 1.0f;
    juce::Rectangle<int> lastGeometry;
    juce::Point<int> pendingPosition;
    bool restorePosition = false, geometryDirty = false;
    int geometrySaveCountdown = 15;
    int jiggleTick = 0;
    int jigW = 840, jigH = 624;
    std::vector<std::unique_ptr<juce::WebSliderRelay>> relays;
    std::unique_ptr<juce::WebBrowserComponent> browser;
    std::vector<std::unique_ptr<juce::WebSliderParameterAttachment>> attachments;
    std::unique_ptr<juce::FileChooser> chooser;

    juce::dsp::FFT fft { OverteonProcessor::fftOrder };
    juce::dsp::WindowingFunction<float> window
        { OverteonProcessor::fftSize, juce::dsp::WindowingFunction<float>::hann };
    std::array<float, OverteonProcessor::fftSize * 2> fftBuf {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (InjectorWebEditor)
};
