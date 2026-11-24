#include "OvQmlEditor.h"
#include "OvQtComponent.h"
#include "OvCryptoShim.h"
#include "OvEnv.h"
#include "OvGraphicsBackend.h"

#include "OverteonProcessor.h"
#include "OvRuntimeLog.h"
#include "OvQtLog.h"
#if OVERTEON_FILE_LOG
 #include "OvIssueReport.h"
#endif
#include "OvEngine.h"
#include "OvJuceSource.h"
#include "OvPointer.h"
#include "OvQmlTypes.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <QGuiApplication>
#include <QQuickView>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QScreen>
#include <QTimer>
#include <QWindow>
#include <QAbstractEventDispatcher>
#include <QAbstractNativeEventFilter>
#include <QFontDatabase>
#include <QFont>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QQuickItem>
#include <QPointingDevice>
#include <QNetworkProxyFactory>
#include <QFileInfo>

#include <functional>

#ifdef QT_STATIC
#include <QQmlExtensionPlugin>
Q_IMPORT_QML_PLUGIN(QtQuick2Plugin)
Q_IMPORT_QML_PLUGIN(QtQmlPlugin)
Q_IMPORT_QML_PLUGIN(QtQmlModelsPlugin)
Q_IMPORT_QML_PLUGIN(QtQmlWorkerScriptPlugin)
#if JUCE_WINDOWS
#include <QtCore/qplugin.h>
Q_IMPORT_PLUGIN(QWindowsIntegrationPlugin)
#endif
#endif

#if JUCE_WINDOWS
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace
{
#if JUCE_WINDOWS
    juce::File bundledQtDirectory()
    {
        return juce::File::getSpecialLocation (juce::File::currentExecutableFile).getParentDirectory();
    }

    void configureBundledQtPaths()
    {
        const auto dir = bundledQtDirectory();
        if (dir.getChildFile ("platforms").isDirectory())
            QCoreApplication::addLibraryPath (QString::fromWCharArray (dir.getFullPathName().toWideCharPointer()));
    }
#endif

    // One QGuiApplication per process while any editor exists.
    //
    // It is reference counted: the first editor creates it, the last editor
    // destroys it.  A host that removes the plug-in unloads this module, and a
    // leaked QGuiApplication (plus its event dispatcher, DBus and thread-pool
    // threads) then calls into unloaded code -- the crash FL Studio reports when
    // a rack slot is removed.  Reopening the editor recreates the application.
    int  qtAppUsers = 0;
    bool qtAppAttempted = false;
    bool qtAppOk = false;

    bool ensureQtApp()
    {
        if (qtAppAttempted) return qtAppOk;
        qtAppAttempted = true;

#if JUCE_WINDOWS
        configureBundledQtPaths();
#endif

        if (qobject_cast<QGuiApplication*> (QCoreApplication::instance()) != nullptr)
        {
            qtAppOk = true;
            return qtAppOk;
        }

        // Keep the embedded editor's frame pacing while respecting the environment.
        //
        // Threaded rendering gives Qt its own render thread paced by the
        // display's vsync -- on a 120 Hz panel inside a DAW that is 120 frames
        // of QML per second competing with the host's GUI thread, which is a
        // large part of the "FL Studio stutters while the editor is open"
        // report.  The basic loop renders inside the editor's own 60 Hz timer
        // instead, so the ceiling is 60 fps and there is no second thread.
        //   OVERTEON_RENDERLOOP=threaded|basic   override
       #if JUCE_WINDOWS
        const char* defaultLoop = "basic";
       #else
        const char* defaultLoop = "threaded";
       #endif
        if (qEnvironmentVariableIsEmpty ("QSG_RENDER_LOOP"))
            qputenv ("QSG_RENDER_LOOP", qEnvironmentVariable ("OVERTEON_RENDERLOOP", defaultLoop).toLatin1());
        // RHI selects Metal, D3D or Vulkan for the Slang shader package.
        // Preserve explicit Qt overrides and any existing host Qt application.
        ovgraphics::configureDefaults (ovgraphics::runningUnderWine());

       #if ! JUCE_WINDOWS
        // Never query the system proxy configuration.  On Linux that loads
        // libproxy's libpxbackend, whose GStrvBuilder/GUri calls can bind to
        // the glib shim this plug-in exports for hosts that ship an old glib
        // (Source/ui/qt/OvGlibShim.cpp) -- a mix of two allocators that
        // SIGSEGVs inside QNetworkProxyFactory::systemProxyForQuery.  The
        // plug-in's only network use is the log uploader's single HTTPS POST.
        // Windows has no libpxbackend; its WinHTTP proxy path is fine.
        QNetworkProxyFactory::setUseSystemConfiguration (false);
       #endif

        // Qt must know it is a GUEST in someone else's application.  Without
        // this, Qt installs session-management and application-lifecycle hooks
        // and expects to be the one deciding when the app ends — inside a host
        // that is wrong, and it is one of the ways a plug-in corrupts memory at
        // teardown.  The reference implementation in Qt's own "JUCE x Qt"
        // example sets exactly this before constructing QGuiApplication.
        QCoreApplication::setAttribute (Qt::AA_PluginApplication);

        static int argc = 1;
        static char arg0[] = "Overteon";
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

        qtAppOk = true;
        return qtAppOk;
    }

    void releaseQtApp()
    {
        if (qtAppUsers <= 0 || --qtAppUsers > 0)
            return;

        if (QCoreApplication::instance() != nullptr)
        {
            QCoreApplication::sendPostedEvents();
            QCoreApplication::sendPostedEvents (nullptr, QEvent::DeferredDelete);
            delete QCoreApplication::instance();
        }
        qtAppAttempted = false;
        qtAppOk = false;
    }

    /// QWindow::installEventFilter wants a QObject; OvQmlEditor is a JUCE
    /// object, so this thin adapter holds a callback into the editor.
    class KeyFilter : public QObject
    {
    public:
        explicit KeyFilter (std::function<bool (QEvent*)> fn) : handler (std::move (fn)) {}

        bool eventFilter (QObject*, QEvent* e) override
        {
            return handler != nullptr && handler (e);
        }

    private:
        std::function<bool (QEvent*)> handler;
    };

#if JUCE_WINDOWS
    /// Records which window owned keyboard focus immediately before the
    /// embedded QWindow took it.
    ///
    /// WM_SETFOCUS carries that window in lParam — the same value SetFocus()
    /// returns, and exactly what the VSTGUI host-forwarding fix records.  Keys
    /// the editor does not consume are posted to that window (via its root
    /// owner), which is how mature plug-ins keep Space/transport working in
    /// hosts whose shortcuts do not run before the plug-in window sees the key.
    ///
    /// This filter is READ-ONLY: it never changes focus and never consumes a
    /// message, so it cannot interfere with mouse handling.
    class FocusTracker : public QAbstractNativeEventFilter
    {
    public:
        FocusTracker (QQuickView* v, std::function<void (WId)> fn)
            : view (v), handler (std::move (fn)) {}

        bool nativeEventFilter (const QByteArray&, void* message, qintptr*) override
        {
            auto* msg = static_cast<MSG*> (message);
            if (msg == nullptr || msg->message != WM_SETFOCUS)
                return false;
            if (view == nullptr || msg->hwnd != (HWND) view->winId())
                return false;

            if (handler != nullptr)
                handler ((WId) msg->lParam);

            return false;                 // observe only
        }

    private:
        QQuickView* view = nullptr;
        std::function<void (WId)> handler;
    };
#endif
}   // anonymous namespace

struct OvQmlEditor::Impl
{
    std::unique_ptr<OvJuceSource> source;
    std::unique_ptr<OvEngine>     engine;

    /// Scene-wide pointer state, exposed to QML as `OvPointer`.  It filters
    /// the view's mouse events, so it must outlive the QML scene: declaring it
    /// before the view (which is owned by qtComponent) achieves that.
    std::unique_ptr<OvPointer>    pointer;

    QQuickView*   view = nullptr;      // owned by qtComponent
    bool          qtOk = false;
    bool          failed = false;
    bool          rendererLogged = false;   // Qt Quick backend reported once
    bool          embeddedLogged = false;   // host peer reported once
    int           autoReportTicks = 0;      // countdown to the automatic upload

    /// Installed on the QWindow: routes keys QML did not consume to the HOST
    /// window (not back into this plug-in's peer).
    std::unique_ptr<QObject> keyFilter;

#if JUCE_WINDOWS
    /// Watches WM_SETFOCUS so we know which host window to post to.
    std::unique_ptr<FocusTracker> focusTracker;

    /// The window that had focus immediately before the QWindow took it.
    HWND lastHostFocus = nullptr;
#endif
};

OvQmlEditor::OvQmlEditor (OverteonProcessor& p)
    : juce::AudioProcessorEditor (p),
      proc (p)
{
    impl = std::make_unique<Impl>();
    setOpaque (true);

    // Before any QML exists, repair Qt's OpenSSL bindings if the host has let
    // another plug-in's statically-linked OpenSSL win the process symbol scope
    // (Ardour/Mixbus ship exactly such plug-ins).  Without this the first QML
    // compilation jumps to a garbage address inside QCryptographicHash.  A
    // no-op in a clean process -- see OvCryptoShim.cpp.
    ovqt::bindQtToRealLibcrypto();

    // Qt FIRST, then the size.
    //
    // The size has to be the design size AFTER the desktop scale, and only Qt
    // knows that scale -- QScreen::devicePixelRatio() is what the embedded
    // window will be created with.  Asking for the size before Qt existed meant
    // primaryScreen() was null, the ratio fell back to 1.0, and the editor came
    // out design-sized in device pixels: physically smaller than intended on
    // every HiDPI desktop.
    impl->qtOk = ensureQtApp();
    if (impl->qtOk)
    {
        ++qtAppUsers;
        // Qt's own diagnostics (QML warnings, qt.qpa, qt.scenegraph) only go to
        // stderr by default.  Route them into the runtime log before the QML
        // engine is created, then record the environment they are running in.
        ovqtlog::install();
        ovqtlog::logEnvironment();
        ovlog::info (juce::String ("Qt initialised: ") + qVersion());
    }
    else
    {
        ovlog::error ("Qt could not be initialised; the editor starts without a UI");
    }
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
        // OVERTEON_FIXED_SIZE=0 still overrides at runtime, but only in builds
        // where the feature was compiled in — it cannot widen a fixed build.
       #if OVERTEON_RESIZABLE_WINDOW
        setResizable (true, true);
        // How far the host may stretch it.  Any size works (the design scales
        // uniformly and fills the frame given), so these limits only stop a host
        // from dragging it down to something unreadable or up to a size that
        // costs more than it shows.
        setResizeLimits (OvThemeWidth / 2, OvThemeHeight / 2, 4096, 4096);
        if (ovEnvFlag ("OVERTEON_FIXED_SIZE"))
            setResizable (false, false);
       #else
        setResizable (false, false);
       #endif

        setSize (want.x, want.y);

        if (ovEnvFlag ("OVERTEON_SIZEDBG"))
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
       #if OVERTEON_FILE_LOG
        // Settings panel's DIAGNOSTICS row: upload this session's log.
        impl->engine->reportIssueRequest = [this]
        {
            ovreport::request (QStringLiteral ("requested from the Settings panel"));
        };
       #endif

        // The QML corner grip asks for a WINDOW resize.  setSize is the host's
        // own path: JUCE's wrappers turn an editor size change into LV2
        // ui:resize / IPlugFrame::resizeView, so the host resizes the window
        // instead of the scene being stretched inside a fixed frame.  In a
        // fixed-size build the grip stays hidden (windowResizable=false) and
        // this lambda would refuse anyway.
        impl->engine->windowResizeRequest = [this] (int w, int h)
        {
            if (! isResizable()) return;
            w = juce::jlimit (OvThemeWidth / 2, 4096, w);
            h = juce::jlimit (OvThemeHeight / 2, 4096, h);
            if (w == getWidth() && h == getHeight()) return;
            setSize (w, h);
        };
        impl->engine->setWindowResizable (isResizable());

        // ---- WINDOW SIZE presets (the Settings row) ---------------------
        //
        // ORIGINAL always means the fixed design, never "whatever is on disk".
        // DEFAULT is the preset written by SAVE AS DEFAULT, and until one
        // exists it behaves exactly like ORIGINAL.  SAVE AS DEFAULT records the
        // current size only; the window still opens at the LAST-USED size,
        // which the resize debounce above keeps up to date.
        impl->engine->windowSizeOriginalRequest = [this]
        {
            if (! isResizable()) return;
            const auto s = designSize();
            ovqtlog::event (QStringLiteral ("window size: original %1x%2").arg (s.x).arg (s.y));
            if (s.x != getWidth() || s.y != getHeight()) setSize (s.x, s.y);
        };
        impl->engine->windowSizeDefaultRequest = [this]
        {
            if (! isResizable()) return;
            auto s = proc.loadDefaultEditorSize();
            s = (s.x > 0 && s.y > 0) ? clampSize (s) : designSize();
            ovqtlog::event (QStringLiteral ("window size: default %1x%2").arg (s.x).arg (s.y));
            if (s.x != getWidth() || s.y != getHeight()) setSize (s.x, s.y);
        };
        impl->engine->windowSizeSaveDefaultRequest = [this]
        {
            if (! isResizable()) return;
            proc.saveDefaultEditorSize (getWidth(), getHeight());
            ovqtlog::event (QStringLiteral ("window size: saved default %1x%2")
                                .arg (getWidth()).arg (getHeight()));
            if (impl && impl->engine)
                impl->engine->setWindowDefaultSize (getWidth(), getHeight());
        };
        if (const auto d = proc.loadDefaultEditorSize(); d.x > 0 && d.y > 0)
            impl->engine->setWindowDefaultSize (d.x, d.y);

        // ---- meter MODE persistence --------------------------------------
        //
        // One MODE per meter (0 = RMS, 1 = TRANSIENT), stored per user beside
        // the window size.  Loaded before the first frame so the plots come up
        // in the user's mode, then written back whenever the panel changes a
        // display setting.
        impl->engine->setHarmMode  (proc.loadMeterMode ("harmMode",  1));
        impl->engine->setUpperMode (proc.loadMeterMode ("upperMode", 1));
        impl->engine->setLowerMode (proc.loadMeterMode ("lowerMode", 1));

        {
            int r = proc.loadIntSetting ("grRange", 12);
            if (r != 6 && r != 12 && r != 24 && r != 48) r = 12;
            impl->engine->setGrRange (r);
        }

        QObject::connect (impl->engine.get(), &OvEngine::settingsChanged,
                          impl->engine.get(), [this] {
            if (! impl || ! impl->engine) return;
            proc.saveMeterMode ("harmMode",  impl->engine->harmMode());
            proc.saveMeterMode ("upperMode", impl->engine->upperMode());
            proc.saveMeterMode ("lowerMode", impl->engine->lowerMode());
            proc.saveIntSetting ("grRange",  impl->engine->grRange());
            ovqtlog::event (QStringLiteral ("display settings: harm %1, upper %2, lower %3, GR range %4")
                                .arg (impl->engine->harmMode()).arg (impl->engine->upperMode())
                                .arg (impl->engine->lowerMode()).arg (impl->engine->grRange()));
        });

        // keep the QML preset name in sync with dialogs opened from the UI
        if (auto* juceSource = dynamic_cast<OvJuceSource*> (&impl->engine->source()))
            juceSource->presetNameChanged = [this] (const juce::String& n)
            {
                if (impl->engine) impl->engine->setPresetName (QString::fromStdString (n.toStdString()));
                ovqtlog::event (QStringLiteral ("preset: %1").arg (QString::fromStdString (n.toStdString())));
            };

        impl->view = new QQuickView();

        // One pointer service for the whole scene.  QML popups ask it whether
        // the cursor is still on them instead of relying on hover event order.
        impl->pointer = std::make_unique<OvPointer> (impl->view);
        impl->view->rootContext()->setContextProperty ("OvPointer", impl->pointer.get());

#if JUCE_WINDOWS
        // Remember the host window that had focus just before this QWindow.
        // The filter only observes WM_SETFOCUS; it never calls SetFocus or
        // changes the focus chain in any way.
        impl->focusTracker = std::make_unique<FocusTracker> (
            impl->view, [this] (WId w)
            {
                if (impl == nullptr || impl->view == nullptr) return;
                auto* prev = reinterpret_cast<HWND> (w);
                if (prev == nullptr
                    || prev == reinterpret_cast<HWND> (impl->view->winId()))
                    return;
                if (IsWindow (prev))
                    impl->lastHostFocus = prev;
            });
        if (auto* app = QCoreApplication::instance())
            app->installNativeEventFilter (impl->focusTracker.get());
#endif

        // Keys the QML scene does not consume must continue to the host.
        impl->keyFilter = std::make_unique<KeyFilter> (
            [this] (QEvent* e) { return forwardKeyToHost (e); });
        impl->view->installEventFilter (impl->keyFilter.get());

#if JUCE_WINDOWS
        const auto qmlDir = bundledQtDirectory().getChildFile ("qml");
        if (qmlDir.isDirectory())
            impl->view->engine()->addImportPath (QString::fromWCharArray (qmlDir.getFullPathName().toWideCharPointer()));
#endif

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
        // The About panel shows the version, so it comes from the build where
        // the version is defined (CMakeLists' project(VERSION ...)) instead of
        // being typed into the QML -- it had drifted three releases behind.
        impl->view->rootContext()->setContextProperty (
            "ovVersion", QStringLiteral (OVERTEON_VERSION));
        impl->view->rootContext()->setContextProperty (
            "ovCpuFrost", qEnvironmentVariableIsSet ("OVERTEON_CPUFROST"));
        impl->view->rootContext()->setContextProperty (
            "ovNoFrost", qEnvironmentVariableIsSet ("OVERTEON_NOFROST"));
        impl->view->rootContext()->setContextProperty (
            "ovGlassSigma",
            qEnvironmentVariable ("OVERTEON_GLASSSIGMA", "1.0").toDouble());
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
            // Every QML error (category "qml") is recorded by the Qt message
            // handler installed above, both in the log and on stderr.
            impl->failed = true;
        }
        else if (auto* ro = impl->view->rootObject())
        {
            ro->setProperty ("engine", QVariant::fromValue<QObject*> (impl->engine.get()));
            ovlog::info ("QML scene loaded: qrc:/qml/main.qml");
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

            ovqtlog::event (QStringLiteral ("QML window ready, editor %1x%2")
                                .arg (getWidth()).arg (getHeight()));

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
                    if (ovEnvFlag ("OVERTEON_SIZEDBG"))
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

   #if OVERTEON_FILE_LOG
    // ---- automatic telemetry -------------------------------------------
    //
    // A log build with a report token (the automation build) uploads this
    // run's log once, without the user doing anything.  The countdown runs on
    // this editor's JUCE timer rather than a QTimer: the host pumps that timer
    // through its idle callback in every host and format (and under Wine),
    // while a Qt timer depends on a Qt event loop the host does not own.
    // OVERTEON_REPORT_AUTO=0 turns it off, OVERTEON_REPORT_DISABLE=1 turns the
    // reporter off, OVERTEON_REPORT_DELAY_MS tunes the delay.
    {
        const int configuredDelay = qEnvironmentVariableIntValue ("OVERTEON_REPORT_DELAY_MS");
        const int delay = configuredDelay > 0 ? configuredDelay : 3000;
        impl->autoReportTicks = juce::jmax (1, delay * 60 / 1000);
    }
   #endif

    // The host's idle callback is what actually pumps this timer, and jalv
    // idles at ~15 Hz.  Asking for 120 only fills a backlog we can never drain;
    // 60 is the rate the scene is designed for and the most any host delivers.
    startTimerHz (60);

    // Wait about a second of host idles before reading the size back.  The
    // resize is asynchronous: the host applies it after the editor maps its
    // window, so anything read in this constructor is the size the host picked,
    // not the size that was asked for.
    sizeCheckIn = 45;

    // What is already on disk.  resized() compares against it, so simply
    // opening and closing the editor at the stored size writes nothing.
    lastSavedSize = proc.loadEditorSize();

   #if OVERTEON_FILE_LOG
    // The editor could not start (Qt or QML failed): offer the log
    // automatically when the build is configured for it.
    if (impl->failed)
        ovreport::requestAutomatic (QStringLiteral ("editor could not start"));

    // Deliberate fault injection for the install/CI tests: build with the log
    // and run with OVERTEON_REPORT_TEST=1 to exercise the uploader.
    if (qEnvironmentVariableIsSet ("OVERTEON_REPORT_TEST"))
        ovreport::request (QStringLiteral ("test hook"));
   #endif

    ovlog::info ("editor opened: " + juce::String (getWidth()) + "x" + juce::String (getHeight())
                 + (impl->failed ? ", no UI" : ", QML embedded"));
}

OvQmlEditor::~OvQmlEditor()
{
    ovlog::info ("editor closed");

   #if OVERTEON_FILE_LOG
    // A run that closed the editor before the automatic delay fired still
    // reports; the request starts here and the log is complete up to this
    // point.  request() is idempotent per process, so a run that already
    // uploaded does nothing.
    ovreport::requestAutomatic (QStringLiteral ("editor closed"));
   #endif

    // One last chance to persist the size: the debounce may not have fired yet
    // if the editor was closed right after a drag.
    if (getWidth() > 0 && getHeight() > 0)
    {
        const juce::Point<int> s (getWidth(), getHeight());
        if (s != lastSavedSize) proc.saveEditorSize (s.x, s.y);
    }

#if JUCE_WINDOWS
    // Remove the focus observer BEFORE the QWindow it dereferences is
    // destroyed.  The tracker is read-only, but it must not outlive the view.
    if (impl != nullptr && impl->focusTracker != nullptr)
        if (auto* app = QCoreApplication::instance())
            app->removeNativeEventFilter (impl->focusTracker.get());
#endif

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

    // Last editor in this process: tear the Qt application down so no event,
    // DBus, thread-pool or network thread outlives the module that owns it.
    // releaseQtApp() is a no-op when Qt never came up or another editor is open.
    releaseQtApp();
}

void OvQmlEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff16181d));
    if (impl && impl->failed)
    {
        g.setColour (juce::Colour (0xffe7ebf2));
        g.setFont (juce::Font (juce::FontOptions (13.0f)));
        g.drawFittedText ("Overteon: the Qt6/QML UI could not be started.\n"
                          "Check that the Qt xcb platform plugin is installed.",
                          getLocalBounds().reduced (24), juce::Justification::centred, 3);
    }
}

void OvQmlEditor::resized()
{
    if (qtComponent != nullptr)
        qtComponent->setBounds (getLocalBounds());

    // Schedule the size save.  A drag calls this on every frame; the timer
    // writes once, half a second after the last change.
    if (impl != nullptr && impl->qtOk && getWidth() > 0 && getHeight() > 0
        && juce::Point<int> (getWidth(), getHeight()) != lastSavedSize)
        saveSizeIn = 30;
}

// ---------------------------------------------------------------------------
//  The editor size to request from the host.
// ---------------------------------------------------------------------------
juce::Point<int> OvQmlEditor::targetSize() const
{
    // The size the user left the editor at wins over the design size.  It is
    // stored per user, so closing the editor, removing the plug-in or adding a
    // second instance all reopen at that size instead of making the user drag
    // the corner again.
    const auto design = designSize();

    if (const auto saved = proc.loadEditorSize(); saved.x > 0 && saved.y > 0)
    {
        // A saved size far below the design is the unit bug from older builds:
        // a HiDPI session saved the host's LOGICAL size (e.g. 640x475 at
        // dpr 1.3), and later runs then opened the editor at 640 DEVICE pixels
        // -- 492 logical px, a UI at 47 %.  Only trust a size that could
        // plausibly be the design at this display scale; otherwise reopen at
        // the design size.
        if (saved.x >= design.x * 3 / 4 && saved.y >= design.y * 3 / 4)
            return clampSize (saved);
    }

    return design;
}

juce::Point<int> OvQmlEditor::designSize() const
{
    // JUCE editor sizes are logical pixels on Windows; its peer applies the
    // display scale. Multiplying by DPR here makes the host frame larger than
    // the Qt content. The existing Linux host path is measured in device pixels.
    const qreal dpr = [] {
       #if JUCE_WINDOWS
        return qreal (1.0);
       #else
        if (auto* scr = QGuiApplication::primaryScreen())
            return scr->devicePixelRatio();
        return qreal (1.0);
       #endif
    }();

    return clampSize ({ (int) std::lround (OvThemeWidth  * dpr),
                        (int) std::lround (OvThemeHeight * dpr) });
}

juce::Point<int> OvQmlEditor::clampSize (juce::Point<int> s) const
{
    const qreal dpr = [] {
       #if JUCE_WINDOWS
        return qreal (1.0);
       #else
        if (auto* scr = QGuiApplication::primaryScreen())
            return scr->devicePixelRatio();
        return qreal (1.0);
       #endif
    }();

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
            const double k = std::min (1.0, std::min ((double) avail.width()  / s.x,
                                                      (double) avail.height() / s.y));
            s.x = (int) std::lround (s.x * k);
            s.y = (int) std::lround (s.y * k);
        }
    }

    s.x = juce::jlimit (460, 8192, s.x);
    s.y = juce::jlimit (341, 8192, s.y);
    return s;
}



void OvQmlEditor::timerCallback()
{
    if (! impl) return;

   #if OVERTEON_FILE_LOG
    if (impl->autoReportTicks > 0 && --impl->autoReportTicks == 0
        && ! impl->failed && impl->view != nullptr)
        ovreport::requestAutomatic (QStringLiteral ("automatic session log"));
   #endif

    // The scene graph is set up on the render thread, so the selected Qt Quick
    // backend is only known once it has initialised; report it the first time
    // that has happened.
    if (! impl->rendererLogged && impl->view != nullptr && impl->view->isSceneGraphInitialized())
    {
        impl->rendererLogged = true;
        ovqtlog::logQuickWindow (impl->view);
    }

    // The peer only exists once the host has created the plug-in window, which
    // is after the constructor; report the window the editor was embedded into
    // as soon as it is known.
    if (! impl->embeddedLogged)
        if (auto* peer = getPeer())
        {
            impl->embeddedLogged = true;
            ovqtlog::event (QStringLiteral ("embedded into host window 0x%1, editor %2x%3")
                                .arg (qulonglong (quintptr (peer->getNativeHandle())), 0, 16)
                                .arg (getWidth()).arg (getHeight()));
        }

    // ---- did the size actually land? ------------------------------------
    //
    // Printed from the LOG, not inferred from the source: the resize goes
    // through the host (ui:resize), so the only trustworthy answer to "is the
    // editor the size it asked for" is to read it back once the window has been
    // up for a moment.  A mismatch is a real fault and is reported even
    // without OVERTEON_SIZEDBG -- it is the difference between a UI that is
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
        if (! ok || ovEnvFlag ("OVERTEON_SIZEDBG"))
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

    // ---- persist the editor size ----------------------------------------
    //
    // resized() only arms the countdown; this is where it fires.  Half a second
    // (30 ticks at 60 Hz) after the last resize, one tiny XML write.  A drag
    // therefore costs exactly one write, not one per frame.
    if (saveSizeIn > 0 && --saveSizeIn == 0)
    {
        const juce::Point<int> s (getWidth(), getHeight());
        if (s.x > 0 && s.y > 0 && s != lastSavedSize)
        {
            proc.saveEditorSize (s.x, s.y);
            lastSavedSize = s;
            ovqtlog::event (QStringLiteral ("editor resized to %1x%2 (saved)").arg (s.x).arg (s.y));
        }
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

// ---------------------------------------------------------------------------
//  Keys the QML scene does not consume go to the HOST.
// ---------------------------------------------------------------------------
//
//  The editor is a QQuickView embedded as a child of the JUCE peer.  Once that
//  child owns native focus, Windows delivers every key to Qt and neither the
//  JUCE peer nor the host sees a WM_KEYDOWN by the normal route — Space and
//  transport keys then appear dead.
//
//  Mature plug-ins (FabFilter; the VSTGUI host-forwarding fix) keep focus on
//  the plug-in window — the mouse wheel needs it on Windows — consume only the
//  keys the UI actually uses, and hand the rest to the HOST window.  Focus is
//  never changed here.  Posting to our own peer was the old bug: the message
//  re-entered this editor instead of reaching the host.
bool OvQmlEditor::forwardKeyToHost (QEvent* event)
{
   #if JUCE_WINDOWS
    if (impl == nullptr || impl->view == nullptr || event == nullptr)
        return false;

    const auto type = event->type();
    if (type != QEvent::KeyPress && type != QEvent::KeyRelease)
        return false;

    auto* ke = static_cast<QKeyEvent*> (event);

    // Escape closes the QML modals (About, Settings, the node editor).
    if (ke->key() == Qt::Key_Escape)
        return false;

    // An IME composition belongs to Qt even when no field has focus.
    if (ke->nativeVirtualKey() == VK_PROCESSKEY)
        return false;

    // Typing into a value field must stay in Qt.  The input-method query is
    // true exactly for TextInput/TextEdit and false for every other item.
    if (auto* focusItem = impl->view->activeFocusItem())
        if (focusItem->inputMethodQuery (Qt::ImEnabled).toBool())
            return false;

    auto* peer = getPeer();
    if (peer == nullptr)
        return false;

    auto* pluginHwnd = static_cast<HWND> (peer->getNativeHandle());
    if (pluginHwnd == nullptr || ! IsWindow (pluginHwnd))
        return false;

    // The plug-in's own peer is the WRONG target: posting there re-enters this
    // editor (and JUCE may consume the key), which is why the old version
    // looked like it did nothing.  Hand the key to the host window:
    //   1. the window that had focus just before the QWindow (WM_SETFOCUS
    //      lParam, recorded by FocusTracker), reduced to its root owner;
    //   2. otherwise the plug-in window's root owner.
    HWND target = nullptr;

    if (impl->lastHostFocus != nullptr && IsWindow (impl->lastHostFocus))
    {
        // Only a window in THIS process can be the DAW.  Focusing another
        // application and then clicking the plug-in would otherwise post the
        // key into that application.
        DWORD pid = 0;
        GetWindowThreadProcessId (impl->lastHostFocus, &pid);
        if (pid == GetCurrentProcessId())
            target = GetAncestor (impl->lastHostFocus, GA_ROOTOWNER);
    }

    if (target == nullptr || target == pluginHwnd)
        target = GetAncestor (pluginHwnd, GA_ROOTOWNER);

    if (target == nullptr || target == pluginHwnd)
        target = GetParent (pluginHwnd);

    if (target == nullptr || target == pluginHwnd)
        return false;

    const int vk = ke->nativeVirtualKey();
    if (vk == 0)
        return false;

    const bool isUp  = type == QEvent::KeyRelease;
    const bool isSys = (ke->modifiers() & Qt::AltModifier) != 0;

    // lParam layout (WM_KEYDOWN): repeat 1, scan code, extended flag;
    // WM_KEYUP additionally sets the previous-state and transition bits.
    LPARAM lp = 1 | ((LPARAM) ke->nativeScanCode() << 16);

    if (ke->isAutoRepeat())
        lp |= (1L << 30);              // previous key state: still down

    switch (vk)
    {
        case VK_LEFT: case VK_RIGHT: case VK_UP: case VK_DOWN:
        case VK_PRIOR: case VK_NEXT: case VK_HOME: case VK_END:
        case VK_INSERT: case VK_DELETE: case VK_DIVIDE:
        case VK_NUMLOCK: case VK_RCONTROL: case VK_RMENU:
            lp |= (1L << 24);   // extended key
            break;
        default:
            break;
    }

    if (isUp)
        lp |= (1L << 30) | (1L << 31);
    if (isSys)
        lp |= (1L << 29);              // context code: Alt is down

    const BOOL posted = PostMessageW (target,
                                      isSys ? (isUp ? WM_SYSKEYUP   : WM_SYSKEYDOWN)
                                            : (isUp ? WM_KEYUP      : WM_KEYDOWN),
                                      (WPARAM) vk, lp);

    if (ovEnvFlag ("OVERTEON_KEYDBG"))
        std::fprintf (stderr,
                      "[key] vk %d %s  lastHost %p  target %p  plugin %p  posted %d\n",
                      vk, isUp ? "up" : "down",
                      (void*) impl->lastHostFocus, (void*) target,
                      (void*) pluginHwnd, (int) posted);

    return true;
   #else
    juce::ignoreUnused (event);
    return false;
   #endif
}
