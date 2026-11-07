#include "OvQmlEditor.h"
#include "OvQtComponent.h"

#include "FxProcessor.h"
#include "OvEngine.h"
#include "OvJuceSource.h"
#include "OvQmlTypes.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <QGuiApplication>
#include <QQuickView>
#include <QQmlContext>
#include <QQuickWindow>
#include <QScreen>
#include <QTimer>
#include <QWindow>
#include <QAbstractEventDispatcher>
#include <QFontDatabase>
#include <QFont>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QPointingDevice>
#include <QFileInfo>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace
{
    // One QGuiApplication per process, created on first editor use. It has to
    // outlive every editor, hence the intentional leak.
    bool ensureQtApp()
    {
        static bool attempted = false;
        static bool ok = false;
        if (attempted) return ok;
        attempted = true;

        if (qobject_cast<QGuiApplication*> (QCoreApplication::instance()) != nullptr)
        {
            ok = true;
            return ok;
        }

        // Render loop choice matters more than anything else here.
        //
        //   basic     renders ONLY inside processEvents() on this thread, so the
        //             frame rate is whatever rate the host happens to call the
        //             idle callback at — measured 9 to 32 Hz in jalv, i.e. the
        //             "fps is random" report.  No amount of pacing inside the
        //             callback can fix that: one callback can only paint once.
        //   threaded  gives Qt its own render thread with its own vsync clock.
        //             Rendering then proceeds independently of how often the
        //             host pumps us, and processEvents() only has to keep the
        //             GUI thread's properties and timers moving.
        //
        // The comment that used to be here said basic was "the safe pairing"
        // because the host owns the thread.  That is true for correctness of
        // the GUI thread, but it is exactly what makes the frame rate hostage
        // to the host's heartbeat.  Overridable for A/B measurement.
        qputenv ("QSG_RENDER_LOOP", qEnvironmentVariable ("ONYXTEON_RENDERLOOP", "threaded").toLatin1());
        qputenv ("QT_QUICK_BACKEND", "opengl");
        QQuickWindow::setGraphicsApi (QSGRendererInterface::OpenGL);

        // Qt must know it is a GUEST in someone else's application.  Without
        // this, Qt installs session-management and application-lifecycle hooks
        // and expects to be the one deciding when the app ends — inside a host
        // that is wrong, and it is one of the ways a plug-in corrupts memory at
        // teardown.  The reference implementation in Qt's own "JUCE x Qt"
        // example sets exactly this before constructing QGuiApplication.
        QCoreApplication::setAttribute (Qt::AA_PluginApplication);

        static int argc = 1;
        static char arg0[] = "OnyxTEON";
        static char* argv[] = { arg0, nullptr };
        new QGuiApplication (argc, argv);

        // grayscale antialiasing, as a browser renders text
        if (auto* app = qobject_cast<QGuiApplication*> (QCoreApplication::instance()))
        {
            QFont f = app->font();
            f.setStyleStrategy (QFont::NoSubpixelAntialias);
            app->setFont (f);
        }

        for (const char* f : { ":/res/Inter-Regular.ttf", ":/res/Inter-Medium.ttf",
                               ":/res/Inter-SemiBold.ttf", ":/res/Inter-Bold.ttf",
                               ":/res/edix.otf" })
            QFontDatabase::addApplicationFont (QString::fromLatin1 (f));

        ok = true;
        return ok;
    }
}   // anonymous namespace


struct OvQmlEditor::Impl
{
    std::unique_ptr<OvJuceSource> source;
    std::unique_ptr<OvEngine>     engine;
    QQuickView*   view = nullptr;      // owned by qtComponent
    bool          qtOk = false;
    bool          failed = false;
};

OvQmlEditor::OvQmlEditor (InjectorProcessor& p)
    : juce::AudioProcessorEditor (p),
      proc (p)
{
    impl = std::make_unique<Impl>();
    setOpaque (true);

    // Ask the host for the design size.  JUCE turns this into the format's own
    // resize request (ui:resize for LV2, IPlugView/vendor-specific for VST3),
    // which is the portable way to say how big the editor wants to be — the
    // host applies the desktop scale itself.  Without it the editor stayed at
    // JUCE's 840x624 default and the whole UI was drawn at 62 % inside a
    // larger canvas.
    {
        const auto want = targetSize();
        setResizable (true, true);
        // How far the host may stretch the editor.  The UI scales uniformly and
        // fills the frame it is given (main.qml), so any size works — these
        // limits only stop a host from dragging it down to something unreadable
        // or up to a size that costs more than it shows.
        setResizeLimits (OvThemeWidth / 2, OvThemeHeight / 2, 4096, 4096);
        setSize (want.x, want.y);
    }

    impl->qtOk = ensureQtApp();
    if (impl->qtOk)
    {
        ovRegisterQmlTypes();
        impl->source = std::make_unique<OvJuceSource> (proc);
        impl->engine = std::make_unique<OvEngine> (std::move (impl->source));

        // keep the QML preset name in sync with dialogs opened from the UI
        if (auto* juceSource = dynamic_cast<OvJuceSource*> (&impl->engine->source()))
            juceSource->presetNameChanged = [this] (const juce::String& n)
            {
                if (impl->engine) impl->engine->setPresetName (QString::fromStdString (n.toStdString()));
            };

        impl->view = new QQuickView();

        // A QQuickView is a top-level window by default.  It must NOT be
        // translucent: once it is reparented into the host's window, any region
        // Qt has not painted has alpha 0 and the host's background shows
        // through — which reads as the editor flashing and half drawn.
        impl->view->setFlag (Qt::FramelessWindowHint, true);
        impl->view->setFlag (Qt::NoDropShadowWindowHint, true);
        impl->view->setFlags (impl->view->flags() | Qt::MSWindowsOwnDC);

        // logicalDpi / 96, so OvTheme.pt() keeps font sizes in logical pixels
        double scale = 1.0;
        if (auto* scr = QGuiApplication::primaryScreen())
            scale = scr->logicalDotsPerInch() / 96.0;
        impl->view->rootContext()->setContextProperty ("ovUiScale", scale);
        impl->view->rootContext()->setContextProperty (
            "ovCpuFrost", qEnvironmentVariableIsSet ("ONYXTEON_CPUFROST"));
        impl->view->rootContext()->setContextProperty (
            "ovNoFrost", qEnvironmentVariableIsSet ("ONYXTEON_NOFROST"));
        impl->view->rootContext()->setContextProperty (
            "ovGlassSigma",
            qEnvironmentVariable ("ONYXTEON_GLASSSIGMA", "1.0").toDouble());
        // The glass chain renders at device resolution; it needs the ratio.
        {
            QScreen* scr = impl->view->screen();
            if (! scr) scr = QGuiApplication::primaryScreen();
            impl->view->rootContext()->setContextProperty (
                "ovDpr", scr ? scr->devicePixelRatio() : 1.0);
        }
        impl->view->setResizeMode (QQuickView::SizeRootObjectToView);
        impl->view->setColor (QColor (0x16, 0x18, 0x1d));
        impl->view->setSource (QUrl (QStringLiteral ("qrc:/qml/main.qml")));

        if (impl->view->status() == QQuickView::Error)
        {
            impl->failed = true;
            for (const auto& e : impl->view->errors())
                std::fprintf (stderr, "[ov] qml error: %s\n", e.toString().toStdString().c_str());
        }
        else if (auto* ro = impl->view->rootObject())
        {
            ro->setProperty ("engine", QVariant::fromValue<QObject*> (impl->engine.get()));
        }

        if (! impl->failed)
        {
            // Hand the window to the embedding component.  From here on all
            // positioning, parenting and show/hide is ovqt::Component's job —
            // cross-platform, no raw window handles.
            qtComponent = std::make_unique<ovqt::Component>();
            qtComponent->setWindow (impl->view);
            addAndMakeVisible (*qtComponent);
            qtComponent->setBounds (getLocalBounds());
        }
    }
    else
    {
        impl->failed = true;
    }

    // The host's idle callback is what actually pumps this timer, and jalv
    // idles at ~15 Hz.  Asking for 120 only fills a backlog we can never drain;
    // 60 is the rate the scene is designed for and the most any host delivers.
    startTimerHz (60);
}

OvQmlEditor::~OvQmlEditor()
{
    // Order matters.  The component owns the QWindow and its foreign-window
    // wrapper; destroying it detaches the window from the host first (see the
    // note in OvQtComponent.h about the double destroy).  Only then is it safe
    // to drop the rest.
    qtComponent.reset();
    impl.reset();
}

void OvQmlEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff16181d));
    if (impl && impl->failed)
    {
        g.setColour (juce::Colour (0xffe7ebf2));
        g.setFont (juce::Font (juce::FontOptions (13.0f)));
        g.drawFittedText ("OnyxTEON: the Qt6/QML UI could not be started.\n"
                          "Check that the Qt xcb platform plugin is installed.",
                          getLocalBounds().reduced (24), juce::Justification::centred, 3);
    }
}

void OvQmlEditor::resized()
{
    if (qtComponent != nullptr)
        qtComponent->setBounds (getLocalBounds());
}

// ---------------------------------------------------------------------------
//  The editor's size, in LOGICAL pixels — what the host is asked for.
// ---------------------------------------------------------------------------
juce::Point<int> OvQmlEditor::targetSize() const
{
    // ---- ask the HOST in logical pixels -----------------------------------
    // Two different scales are in play and confusing them is what kept the
    // editor the wrong size:
    //
    //   * the request to the host (ui:resize / setSize) is in LOGICAL pixels,
    //     and the host multiplies it by the desktop scale to build its canvas.
    //     Asking for 1354 made a 1763-wide canvas; asking for 1040 makes 1354,
    //     which is the size we want.
    //   * the editor's own window must then be that canvas size in DEVICE
    //     pixels, and that conversion is ovqt::Component's job now.
    //
    // So: plain design size here, in logical pixels.
    int w = OvThemeWidth;
    int h = OvThemeHeight;

    if (auto* scr = QGuiApplication::primaryScreen())
    {
        const QSize avail = scr->availableGeometry().size();
        if (avail.width() > 200 && avail.height() > 200)
        {
            const double k = std::min (1.0, std::min ((double) avail.width()  / w,
                                                      (double) avail.height() / h));
            w = (int) std::lround (w * k);
            h = (int) std::lround (h * k);
        }
    }

    w = juce::jlimit (460, 4096, w);
    h = juce::jlimit (341, 4096, h);
    return { w, h };
}



void OvQmlEditor::timerCallback()
{
    if (! impl) return;

    // ---- the one thing JUCE still has to do for Qt ----------------------
    // Qt has no event loop of its own inside a plug-in: the host pumps this
    // timer through the LV2/VST3 idle path, and this is where Qt's queued
    // events (timers, bindings, input) get dispatched.
    //
    // That is ALL it does now.  Painting belongs to Qt's render thread
    // (QSG_RENDER_LOOP=threaded), the embedded window's position and visibility
    // belong to ovqt::Component / ComponentMovementWatcher, and the audio data
    // is polled by OvEngine's own timer.  Every previous version of this
    // function tried to drive one of those from here, and each attempt made the
    // frame rate depend on the host's idle cadence — measured 9 to 32 calls a
    // second in jalv, which is the "the fps is random" report.
    if (QCoreApplication::instance() != nullptr)
        QCoreApplication::processEvents (QEventLoop::AllEvents, 5);
}
