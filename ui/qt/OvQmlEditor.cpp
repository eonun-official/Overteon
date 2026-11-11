#include "OvQmlEditor.h"
#include "OvQtComponent.h"
#include "OvEnv.h"

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

    // Qt FIRST, then the size.
    //
    // The size has to be the design size AFTER the desktop scale, and only Qt
    // knows that scale -- QScreen::devicePixelRatio() is what the embedded
    // window will be created with.  Asking for the size before Qt existed meant
    // primaryScreen() was null, the ratio fell back to 1.0, and the editor came
    // out design-sized in device pixels: physically smaller than intended on
    // every HiDPI desktop.
    impl->qtOk = ensureQtApp();
    {
        // Ask the host for the size.  JUCE turns this into the format's own
        // resize request (ui:resize for LV2, IPlugView/vendor-specific for
        // VST3), which is the portable way to say how big the editor wants to
        // be.  Without it the editor stayed at JUCE's 840x624 default and the
        // whole UI was drawn at 62 % inside a larger canvas.
        const auto want = targetSize();

        // ---- is the editor resizable? ----------------------------------
        //
        // A COMPILE-TIME choice (OVERTEON_RESIZABLE_WINDOW, default OFF), not a
        // runtime environment switch.
        //
        // `setResizable (true, true)` tells the host it may stretch the editor,
        // and hosts act on it: jalv.gtk3's window becomes draggable to any size.
        // The UI does adapt — main.qml scales the whole design to whatever frame
        // it is given — but the design is what the plugin is FOR, and a window
        // that can be dragged down to 400x300 makes it unreadable rather than
        // flexible.  So it ships fixed, and being resizable is a build decision
        // for someone who wants the editor at another size, not something a
        // stray environment variable can turn on inside a user's session.
        //
        //     cmake -DOVERTEON_RESIZABLE_WINDOW=ON ..
        //
        // ONYXTEON_FIXED_SIZE=0 still overrides at runtime, but only in builds
        // where the feature was compiled in — it cannot widen a fixed build.
       #if OVERTEON_RESIZABLE_WINDOW
        setResizable (true, true);
        // How far the host may stretch it.  Any size works (the design scales
        // uniformly and fills the frame given), so these limits only stop a host
        // from dragging it down to something unreadable or up to a size that
        // costs more than it shows.
        setResizeLimits (OvThemeWidth / 2, OvThemeHeight / 2, 4096, 4096);
        if (ovEnvFlag ("ONYXTEON_FIXED_SIZE"))
            setResizable (false, false);
       #else
        setResizable (false, false);
       #endif

        setSize (want.x, want.y);

        if (ovEnvFlag ("ONYXTEON_SIZEDBG"))
        {
            const qreal dpr = QGuiApplication::primaryScreen()
                                ? QGuiApplication::primaryScreen()->devicePixelRatio()
                                : qreal (0.0);
            const QRect avail = QGuiApplication::primaryScreen()
                                ? QGuiApplication::primaryScreen()->availableGeometry()
                                : QRect();
            std::fprintf (stderr,
                          "[size] design %dx%d  dpr %.4f  want %dx%d  "
                          "screen avail %dx%d  resizable %d\n",
                          OvThemeWidth, OvThemeHeight, (double) dpr,
                          want.x, want.y, avail.width(), avail.height(),
                          (int) isResizable());
        }
    }

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

            // ---- trigger the resize AGAIN, now that Qt exists --------------
            //
            // The size asked for at the top of this constructor is computed
            // from the desktop scale, and it is set before this editor has a
            // PEER.  A resize request with no peer has nothing to forward to
            // the host, so the number can be silently dropped and the editor
            // keeps whatever the host decided on its own.
            //
            // By here the peer and the embedded QWindow both exist, so setting
            // the size a second time is the request that actually lands.  The
            // guard keeps it silent when the first one did work.
            {
                const auto want = targetSize();
                if (want.x != getWidth() || want.y != getHeight())
                {
                    if (ovEnvFlag ("ONYXTEON_SIZEDBG"))
                        std::fprintf (stderr, "[size] re-applying %dx%d (was %dx%d)\n",
                                      want.x, want.y, getWidth(), getHeight());
                    setSize (want.x, want.y);
                }
                // The child has to match the component whatever happened above;
                // this is the same call resized() makes, and doing it here means
                // the very first presented frame is already the right size.
                qtComponent->setBounds (getLocalBounds());
            }
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

    // Wait about a second of host idles before reading the size back.  The
    // resize is asynchronous: the host applies it after the editor maps its
    // window, so anything read in this constructor is the size the host picked,
    // not the size that was asked for.
    sizeCheckIn = 45;
}

OvQmlEditor::~OvQmlEditor()
{
    // Order matters.  The component owns the QWindow and its foreign-window
    // wrapper; destroying it detaches the window from the host first (see the
    // note in OvQtComponent.h about the double destroy).  Only then is it safe
    // to drop the rest.
    qtComponent.reset();
    impl.reset();

    // Drain Qt's queue before this editor goes away.
    //
    // The QGuiApplication outlives the editor (it is deliberately leaked, one
    // per process, because the host keeps running between editors), so anything
    // still posted -- metacalls from OvEqView's render-thread invokeMethod, and
    // deferred deletes -- would otherwise be delivered after every QML item
    // that could receive them is gone.  Delivering them here, while the items
    // are still valid, keeps teardown synchronous instead of "later, maybe".
    if (QCoreApplication::instance() != nullptr)
        QCoreApplication::sendPostedEvents();
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
//  The editor's size in DEVICE pixels — what the host is asked for.
// ---------------------------------------------------------------------------
juce::Point<int> OvQmlEditor::targetSize() const
{
    // ---- the design size, AFTER the desktop scale --------------------------
    //
    // The editor must occupy the same PHYSICAL area the design was drawn for,
    // so the number the host is asked for is design x devicePixelRatio: 1040x772
    // becomes 1354x1005 on a 1.302 desktop.
    //
    // This used to return the plain design size, on the theory that "the host
    // multiplies by the desktop scale itself".  Measured on jalv.gtk3 it does
    // not: asking for 1040 produced a 1040x772 host window, and the QQuickView
    // inside it -- which Qt had already scaled up by the same ratio -- came out
    // 1354x1005, larger than its own parent.  So the scaling happened exactly
    // once, on the wrong side.
    //
    // The ratio is the one Qt itself will use for the embedded window, taken
    // from the same screen Qt picks the window up on.  Rounding to whole pixels
    // keeps the host canvas and the child window on the same integer.
    const qreal dpr = [] {
        if (auto* scr = QGuiApplication::primaryScreen())
            return scr->devicePixelRatio();
        return qreal (1.0);
    }();

    int w = (int) std::lround (OvThemeWidth  * dpr);
    int h = (int) std::lround (OvThemeHeight * dpr);

    // The design also has to FIT: a scaled editor must not ask for more than the
    // screen it will open on, or the host clamps it and the panels are cut off.
    // Comparing in device pixels on both sides is what makes this a real limit
    // rather than a second, accidental scale.
    if (auto* scr = QGuiApplication::primaryScreen())
    {
        const QSize avail (scr->availableGeometry().width()  * dpr,
                           scr->availableGeometry().height() * dpr);
        if (avail.width() > 200 && avail.height() > 200)
        {
            const double k = std::min (1.0, std::min ((double) avail.width()  / w,
                                                      (double) avail.height() / h));
            w = (int) std::lround (w * k);
            h = (int) std::lround (h * k);
        }
    }

    w = juce::jlimit (460, 8192, w);
    h = juce::jlimit (341, 8192, h);
    return { w, h };
}



void OvQmlEditor::timerCallback()
{
    if (! impl) return;

    // ---- did the size actually land? ------------------------------------
    //
    // Printed from the LOG, not inferred from the source: the resize goes
    // through the host (ui:resize), so the only trustworthy answer to "is the
    // editor the size it asked for" is to read it back once the window has been
    // up for a moment.  A mismatch is a real fault and is reported even
    // without ONYXTEON_SIZEDBG -- it is the difference between a UI that is
    // laid out correctly and one that is a stretched copy of itself.
    if (sizeCheckIn > 0 && --sizeCheckIn == 0 && impl->view != nullptr)
    {
        const auto want = targetSize();
        const auto got  = impl->view->size();          // device-INDEPENDENT
        const qreal dpr = impl->view->devicePixelRatio();
        const bool ok = want.x == getWidth() && want.y == getHeight();

        // Both numbers, because they are in different units and mixing them up
        // is what hid this bug for two rounds: the editor is in device pixels
        // (what the host canvas is measured in), the QWindow in DIPs.
        if (! ok || ovEnvFlag ("ONYXTEON_SIZEDBG"))
            std::fprintf (stderr,
                          "[size] asked %dx%d  editor %dx%d  qwindow %dx%d dip "
                          "(%dx%d device)  dpr %.4f  %s\n",
                          want.x, want.y, getWidth(), getHeight(),
                          got.width(), got.height(),
                          qRound (got.width() * dpr), qRound (got.height() * dpr),
                          (double) dpr,
                          ok ? "ok" : "MISMATCH (host ignored the resize)");
        sizeReported = true;
    }

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
