// ============================================================
//  overteon-qml-preview — standalone QML viewer for the editor UI.
//
//  Runs the exact same QML front end as the plug-in, but against
//  OvJackSource (real JACK audio), so the UI can
//  be developed, screenshotted and compared with web_fx/index.html
//  without starting a DAW or a plug-in host.
//
//    ./overteon-qml-preview [--size WxH] [--shot file.png]
//                           [--compat] [--set id=value]
//
//  --compat renders the QML scene into a plain QWidget instead of letting
//  Qt Quick present its own window.  Qt Quick windows do not repaint under
//  wine (a plain QWidget does), so this is what makes the preview usable
//  there; it is also useful on any platform with a broken swap path.
// ============================================================
#include "OvEngine.h"
#if OVERTEON_PREVIEW_HAS_DSP
 #include "OvJackJuce.h"
 #include "FxProcessor.h"
#else
 #include "OvJackSource.h"
#endif
#include "OvPreset.h"
#include "OvTick.h"
#include "OvQmlTypes.h"
#include "OvCtl.h"
#include "OvEqView.h"

#include <QApplication>
#include <QGuiApplication>
#include <QCursor>
#include <QLabel>
#include <QVBoxLayout>
#include <QQuickView>
#include <QQuickWindow>
#include <QQuickItem>
#include <QQmlContext>
#include <QScreen>
#include <QGuiApplication>
#include <QTimer>

#include <chrono>
#include <future>
#include <thread>
#include <algorithm>
#include <cmath>
#include <QFontDatabase>
#include <QFont>
#include <QImage>
#include <QPainter>
#include <QWidget>
#include <QQuickWidget>
#include <QStringList>
#include <QUrl>
#include <QVariant>

#include <chrono>
#include <vector>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <QLibrary>

#ifdef Q_OS_LINUX
 #include <X11/Xlib.h>
 #include <GL/glx.h>
#endif
#include <memory>
#include <string>

namespace
{
    /// #wrap in web_fx/index.html: every layout number is derived from it.
    constexpr double OvThemeDesignW = 1040.0;

    // ---- compatibility mode ------------------------------------------------
    //
    //  Qt Quick's own window does not present under wine: the scene renders
    //  (grabWindow() at different times differs by tens of thousands of
    //  pixels) but the X window keeps showing its first frame.  A plain
    //  QWidget repaints fine.
    //
    //  Grabbing the hidden QQuickView into a QWidget is NOT a fix: a window
    //  that was never presented has no surface to grab, so grabWindow()
    //  returns an empty image and the widget shows nothing.
    //
    //  QQuickWidget is the supported answer: it renders the scene through
    //  QQuickRenderControl into an FBO and blits that through QWidget's own
    //  paint path, so no window present is involved anywhere.
}

namespace
{
    /// logicalDpi / 96 for the primary screen (1.0 on a plain 96 dpi display).
    double uiScale()
    {
        if (auto* s = QGuiApplication::primaryScreen())
            return s->logicalDotsPerInch() / 96.0;
        return 1.0;
    }

    //  Whether vsync actually paces on this display has to be MEASURED, not
    //  inferred from the X SYNC counters.
    //
    //  The old rule was "no vblank/UST/MSC system counter => disable vsync".
    //  That is wrong on this very machine: :0 has none of those counters, yet
    //  glXSwapBuffers with swap interval 1 paces it exactly to its 120 Hz
    //  panel, while a Xephyr screen with the same missing counters does not
    //  pace at all.  Disabling vsync on the strength of the counter test left
    //  the UI running at ~90 fps on a 122 Hz display — visible as judder.
    //
    //  So: open a throwaway GLX context, time a few swaps with the interval
    //  set to 1 and again at 0, and let the numbers decide.
    //      :0  interval 1 = 8.20 ms (122 fps)   interval 0 = 0.29 ms
    //      :3  interval 1 = 0.23 ms             interval 0 = 0.23 ms
    struct VSyncProbe
    {
        bool paces = true;        // does the driver block on the display?
        double interval1Ms = 0.0;
        double interval0Ms = 0.0;
        double refreshHz = 0.0;   // implied by the paced interval
    };

   #ifdef Q_OS_LINUX
    using SwapIntervalEXT = void (*) (Display*, GLXDrawable, int);

    VSyncProbe probeVSync()
    {
        VSyncProbe r;
        Display* dpy = XOpenDisplay (nullptr);
        if (dpy == nullptr) return r;

        int attribs[] = { GLX_RGBA, GLX_DOUBLEBUFFER, GLX_RED_SIZE, 8,
                          GLX_GREEN_SIZE, 8, GLX_BLUE_SIZE, 8, None };
        XVisualInfo* vi = glXChooseVisual (dpy, DefaultScreen (dpy), attribs);
        if (vi == nullptr) { XCloseDisplay (dpy); return r; }

        XSetWindowAttributes swa {};
        swa.colormap = XCreateColormap (dpy, RootWindow (dpy, vi->screen),
                                       vi->visual, AllocNone);
        swa.override_redirect = True;
        Window w = XCreateWindow (dpy, RootWindow (dpy, vi->screen),
                                  0, 0, 64, 64, 0, vi->depth, InputOutput,
                                  vi->visual, CWColormap | CWOverrideRedirect, &swa);
        XMapWindow (dpy, w);
        // The window must be MAPPED before it is swapped, or the driver's
        // swap has nothing to wait for and the first glXSwapBuffers can block
        // until the server decides otherwise.  Measured: without this sync the
        // probe occasionally never returned, and the application then never
        // started at all — the whole UI "hanging on launch" with no message.
        XSync (dpy, False);
        GLXContext ctx = glXCreateContext (dpy, vi, nullptr, True);
        if (ctx == nullptr) { XCloseDisplay (dpy); return r; }
        glXMakeCurrent (dpy, w, ctx);

        auto* setInterval = reinterpret_cast<SwapIntervalEXT> (
            glXGetProcAddressARB (reinterpret_cast<const GLubyte*> ("glXSwapIntervalEXT")));

        // Per-swap times, then the MEDIAN.  A mean is useless here: a single
        // 900 ms stall (which this X server does produce) moves the average
        // from 8 ms to 98 ms and flips the whole decision.  Measured three
        // runs of the naive mean on the same machine: 8.00, 7.79, 98.47 ms.
        auto timeSwaps = [&] (int interval, int n)
        {
            if (setInterval != nullptr) setInterval (dpy, w, interval);
            XSync (dpy, False);
            std::vector<double> per;
            per.reserve ((size_t) n);
            for (int i = 0; i < n; ++i)
            {
                const auto t0 = std::chrono::steady_clock::now();
                glClearColor (0.05f, 0.05f, 0.10f, 1.0f);
                glClear (GL_COLOR_BUFFER_BIT);
                glXSwapBuffers (dpy, w);
                per.push_back (std::chrono::duration<double, std::milli> (
                                   std::chrono::steady_clock::now() - t0).count());
            }
            std::sort (per.begin(), per.end());
            return per[per.size() / 2];
        };

        // Warm up: the first swaps map the window and allocate back buffers.
        timeSwaps (1, 4);
        r.interval0Ms = timeSwaps (0, 15);
        r.interval1Ms = timeSwaps (1, 30);

        glXMakeCurrent (dpy, None, nullptr);
        glXDestroyContext (dpy, ctx);
        XDestroyWindow (dpy, w);
        XCloseDisplay (dpy);

        // "Paces" means interval 1 waits noticeably longer than interval 0.
        // A 120 Hz panel gives ~8 ms; a 60 Hz one ~16 ms.  If the two are
        // within 1.5x of each other the driver is ignoring the interval.
        r.paces = r.interval1Ms > std::max (2.0, r.interval0Ms * 1.5);
        if (r.paces && r.interval1Ms > 0.1)
            r.refreshHz = 1000.0 / r.interval1Ms;
        return r;
    }
   #endif

    /// True when running under wine.
    ///
    /// Wine exposes `wine_get_version` from ntdll; nothing else does.  This is
    /// needed because Qt Quick does not work there out of the box, and the two
    /// fixes have to be applied before QApplication is constructed.
    bool runningUnderWine()
    {
       #ifdef Q_OS_WIN
        QLibrary ntdll (QStringLiteral ("ntdll"));
        return ntdll.resolve ("wine_get_version") != nullptr;
       #else
        return false;
       #endif
    }
}

int main (int argc, char** argv)
{
    // Under wine, Qt 6 picks the D3D11 RHI by default and wine's DXGI cannot
    // create an output for the window ("dxgi_device_init Failed to get
    // output"), so everything renders black.  The OpenGL RHI works.  This must
    // be set before QApplication exists, hence the pre-scan.
    const bool wine = runningUnderWine()
                   || qEnvironmentVariableIsSet ("WINEPREFIX");
    if (wine && qEnvironmentVariableIsEmpty ("QSG_RHI_BACKEND")
             && qEnvironmentVariableIsEmpty ("QT_QUICK_BACKEND"))
        qputenv ("QSG_RHI_BACKEND", "opengl");

    // No compositor on this X display => no vsync frame callback => Qt Quick's
    // render loop waits forever and the window advances one frame per second.
    // Measured on a bare X session (no _NET_WM_CM_S0): 1.0 fps; the same build
    // with QSG_NO_VSYNC=1 runs at ~104 fps.  Set it here so the preview works
    // out of the box, and let a caller override.
   #ifdef Q_OS_LINUX
    if (qEnvironmentVariableIsEmpty ("QSG_NO_VSYNC"))
    {
        // The probe must never be able to stop the application from starting.
        // glXSwapBuffers on a display with no compositor can block
        // indefinitely — observed: the process sat inside probeVSync() and
        // printed nothing at all, so "the UI hangs on launch" had no diagnostic
        // path whatsoever.  It runs on its own thread with a deadline now; if it
        // is not done in time we assume the display does not pace, which is the
        // safe branch (QSG_NO_VSYNC=1 plus the driver's own swap interval).
        VSyncProbe vs;
        {
            std::packaged_task<VSyncProbe()> task (probeVSync);
            auto fut = task.get_future();
            std::thread th (std::move (task));
            th.detach();                     // it may never return; let it leak
            if (fut.wait_for (std::chrono::milliseconds (1500)) ==
                std::future_status::ready)
            {
                vs = fut.get();
            }
            else
            {
                std::fprintf (stderr, "[preview] the vsync probe did not answer in "
                                      "1.5 s; assuming the display does not pace\n");
                vs.paces = false;
            }
        }
        if (vs.paces)
        {
            // The driver blocks in glXSwapBuffers until the display's next
            // refresh, so set the swap interval inside the driver (measured:
            // 8.00 ms/frame = 125 Hz on this 120 Hz panel) and take Qt's own
            // vsync wait out of the picture.
            //
            // Qt's wait is the part that breaks here: it waits for a vblank
            // notification this X server cannot deliver and lands on 1 fps.
            // With the wait gone, the driver's swap still paces the frames —
            // measured 93-113 fps on a 120 Hz panel at 1 % CPU, against 75 fps
            // at 58 % CPU when Qt rasterises on the CPU instead.
            std::fprintf (stderr, "[preview] vsync paces this display: "
                                  "%.2f ms/frame = %.1f Hz; using it "
                                  "(driver-paced, not Qt-paced)\n",
                          vs.interval1Ms, vs.refreshHz);
            qputenv ("QSG_NO_VSYNC", "1");

            // Software rasterisation throws the frame away: it redraws the
            // whole window on the CPU.  On a display that really paces there
            // is a GPU to use, so ask for it.
            if (qEnvironmentVariableIsEmpty ("QSG_RHI_BACKEND")
                && qEnvironmentVariableIsEmpty ("QT_QUICK_BACKEND"))
                qputenv ("QSG_RHI_BACKEND", "opengl");
        }
        else
        {
            // Nothing paces here.  Waiting for a vblank that never arrives
            // pins Qt Quick to 1 fps, so let the render loop free-run.
            std::fprintf (stderr, "[preview] vsync does not pace this display "
                                  "(%.2f ms at interval 1, %.2f ms at 0); "
                                  "letting the render loop free-run\n",
                          vs.interval1Ms, vs.interval0Ms);
            qputenv ("QSG_NO_VSYNC", "1");
        }
    }
   #endif

    // Defaults only: a caller must be able to override these, e.g.
    //   QT_QUICK_BACKEND=software  (wine without working GL)
    //   QT_OPENGL=angle            (Windows)
    // Forcing OpenGL unconditionally is what made the window hang under wine
    // with no fallback.
    //
    // RENDER LOOP: basic, and this is NOT a preference.
    //
    // I switched this to `threaded` after measuring that it spread the work over
    // two cores — and it broke the preview outright on this display: with
    // `threaded` the window is never mapped at all, leaving only the 1x1 helper
    // and the 10x10 stub (verified: same binary, QSG_RENDER_LOOP=basic maps a
    // 1354x1005 window, =threaded maps nothing).  A frame rate is worth nothing
    // if there is no window to show it in.
    //
    // `basic` renders inside processEvents() on the GUI thread, which is what
    // this preview has always done.  Threaded rendering stays available for the
    // plug-in, where the host owns the loop and it does work.
    if (qEnvironmentVariableIsEmpty ("QSG_RENDER_LOOP"))
        qputenv ("QSG_RENDER_LOOP", "basic");

    // Confirm it took.  Qt reads this when the FIRST QQuickWindow is created,
    // and silently falls back to the basic loop when the platform cannot host a
    // render thread, so a value that is set here is not proof that it is used.
    std::fprintf (stderr, "[preview] render loop requested: %s\n",
                  qgetenv ("QSG_RENDER_LOOP").constData());


    // Default window: the design's aspect ratio, sized to the screen so it is
    // not tiny on a 2560x1600 panel.  --size overrides.  The actual figure is
    // computed further down, once QApplication exists -- primaryScreen()
    // returns null before that and the default would be silently ignored.
    int w = 840, h = 624;
    bool sizeGiven = false;
    QString shot, customQml, ctlName;
    int shotDelay = 1200;
    bool hud = false;             // --hud: separate read-out window
    QPointF probe (-1, -1);       // --probe x,y: ask the hit-test about a point
                                  // without moving the real pointer
    bool compat = wine || qEnvironmentVariableIsSet ("ONYXTEON_COMPAT");
    int tickReadPid = 0;   // --tick-read <pid>
    QStringList presets;          // id=value pairs, applied before the first frame
    QStringList presetFiles;      // .hinj files, also applied before the first frame

    for (int i = 1; i < argc; ++i)
    {
        const std::string a (argv[i]);
        if (a == "--size" && i + 1 < argc)
        {
            const QString s = QString::fromLatin1 (argv[++i]);
            const auto parts = s.split ('x');
            if (parts.size() == 2)
            {
                w = parts[0].toInt();
                h = parts[1].toInt();
                sizeGiven = true;
            }
        }
        else if (a == "--shot" && i + 1 < argc)
        {
            shot = QString::fromLatin1 (argv[++i]);
        }
        else if (a == "--shot-delay" && i + 1 < argc)
        {
            shotDelay = QString::fromLatin1 (argv[++i]).toInt();
        }
        else if (a == "--compat")
        {
            compat = true;
        }
        else if (a == "--no-compat")
        {
            compat = false;
        }
        else if (a == "--hud")
        {
            hud = true;
        }
        else if (a == "--probe" && i + 1 < argc)
        {
            const auto xy = QString::fromLatin1 (argv[++i]).split (QLatin1Char (','));
            if (xy.size() == 2) probe = QPointF (xy[0].toDouble(), xy[1].toDouble());
        }
        else if (a == "--freeze")
        {
            std::fprintf (stderr, "[preview] --freeze is gone: the metering now "
                                  "comes from real audio, so there is nothing to pin\n");
        }
        else if (a == "--tick")
        {
            ovtick::Registry::instance().enabled = true;
        }
        else if (a == "--tick-read")
        {
            // Reader-only mode: attach to the ring of the pid given as the next
            // argument and report.  Nothing else is started, so this works even
            // when the UI is wedged.
            ovtick::Registry::instance().enabled = true;
            tickReadPid = (i + 1 < argc) ? std::atoi (argv[++i]) : 0;
        }
        else if (a == "--nosignal")
        {
            std::fprintf (stderr, "[preview] --nosignal is gone: connect nothing "
                                  "to the JACK inputs for silence\n");
        }
        else if (a == "--set" && i + 1 < argc)
        {
            presets << QString::fromLatin1 (argv[++i]);
        }
        else if (a == "--preset" && i + 1 < argc)
        {
            // A .hinj preset, the same file the plug-in loads.  Repeatable, and
            // applied before --set so a command line can start from a preset and
            // then override single values.
            presetFiles << QString::fromLatin1 (argv[++i]);
        }
        else if (a == "--qml" && i + 1 < argc)
        {
            customQml = QString::fromLatin1 (argv[++i]);
        }
        else if (a == "--ctl" && i + 1 < argc)
        {
            // Control block name for `ovctl`: parameters can then be driven from
            // outside through MIT shared memory, with an exponential approach so
            // the code under test sees a drag rather than a jump.
            ctlName = QString::fromLatin1 (argv[++i]);
        }
    }

    std::unique_ptr<QApplication> app;

    if (compat)
    {
        // QWidget needs QApplication, not QGuiApplication
        app = std::make_unique<QApplication> (argc, argv);
    }
    else
    {
        app = std::make_unique<QApplication> (argc, argv);
    }

    // Now the screen is known: fit the design's 1040x772 aspect to it.
    if (! sizeGiven)
    {
        if (auto* scr = QGuiApplication::primaryScreen())
        {
            const QRect avail = scr->availableGeometry();
            const double s = std::min (0.80 * avail.width() / 1040.0,
                                       0.90 * avail.height() / 772.0);
            const double use = std::clamp (s, 0.6, 2.5);
            w = int (std::lround (1040.0 * use));
            h = int (std::lround (772.0 * use));
            std::fprintf (stderr, "[preview] screen %dx%d dpr %.3f logicalDpi %.1f, window %dx%d\n",
                          avail.width(), avail.height(), scr->devicePixelRatio(),
                          scr->logicalDotsPerInch(), w, h);
        }
    }

    // Match the browser's text rendering: grayscale antialiasing.  Qt's
    // default subpixel (LCD) antialiasing fringes glyph edges with colour,
    // which the web page never shows.
    {
        QFont f = app->font();
        f.setStyleStrategy (QFont::NoSubpixelAntialias);
        app->setFont (f);
    }

    ovRegisterQmlTypes();

    for (const char* f : { ":/res/Inter-Regular.ttf", ":/res/Inter-Medium.ttf",
                           ":/res/Inter-SemiBold.ttf", ":/res/Inter-Bold.ttf",
                           ":/res/edix.otf" })
        QFontDatabase::addApplicationFont (QString::fromLatin1 (f));

    // The preview meters REAL audio: a JACK client whose inputs anything can be
    // connected to.  The synthesised mock that used to live here is gone — it
    // could not tell a working analyser from a broken one, and every comparison
    // made against it was a comparison against a signal that did not exist.
    // `bench.py -Gs` connects mpv to these ports.
#if OVERTEON_PREVIEW_HAS_DSP
    // THE REAL DSP.  The preview now hosts the plug-in's own InjectorProcessor
    // and plays the part a host plays: the JACK thread copies the incoming
    // frames into a JUCE buffer and calls processBlock().  The two spectrum taps
    // therefore come out of the DSP itself — the input tap is what entered it,
    // the output tap is what actually left.  Nothing is modelled.
    auto proc = std::make_unique<InjectorProcessor>();
    auto jackSrc = std::make_unique<OvJackJuce> (*proc, "onyxpreview");
#else
    auto jackSrc = std::make_unique<OvJackSource> ("onyxpreview");
#endif
    if (! jackSrc->ok())
    {
        // There is no fallback source any more — the mock was removed because
        // every comparison made against it was a comparison against a signal
        // that did not exist.  So a missing JACK server is a hard error with a
        // clear message, not a silent dead UI.  (It used to be a SIGSEGV: the
        // empty source was handed to the engine anyway.)
        std::fprintf (stderr,
                      "\n[preview] no JACK client. Start a server first, e.g.\n"
                      "    jackd -n ovtest -d dummy -r 48000 -p 1024 &\n"
                      "    JACK_DEFAULT_SERVER=ovtest %s --jack\n"
                      "Nothing can be metered without one, so this exits.\n",
                      argv[0]);
        return 2;
    }
    std::unique_ptr<OvSource> src = std::move (jackSrc);
    auto engine = std::make_unique<OvEngine> (std::move (src));

    const QUrl sourceUrl = customQml.isEmpty()
        ? QUrl (QStringLiteral ("qrc:/qml/main.qml"))
        : QUrl::fromLocalFile (customQml);

    // ---- compat: QQuickWidget renders through QQuickRenderControl ---------
    if (compat)
    {
        auto widget = std::make_unique<QQuickWidget>();
        widget->setResizeMode (QQuickWidget::SizeRootObjectToView);
        widget->setClearColor (QColor (0x16, 0x18, 0x1d));
        widget->rootContext()->setContextProperty ("engine", engine.get());
        widget->rootContext()->setContextProperty ("ovUiScale", uiScale());
        // Same solo switches as the QQuickView path below: the compat widget is
        // the only one that runs under wine, so the debug views have to work
        // there too.
        widget->rootContext()->setContextProperty (
            "ovSolo", qEnvironmentVariable ("ONYXTEON_SOLO"));
        widget->rootContext()->setContextProperty (
            "ovSoloOpacity", qEnvironmentVariable ("ONYXTEON_SOLO_OPACITY"));

        // QQuickWidget renders into an FBO, which needs a GL/RHI context; the
        // software backend has none.  Under wine the OpenGL RHI is already
        // forced above.
        widget->setSource (sourceUrl);

        if (widget->status() == QQuickWidget::Error)
        {
            for (const auto& e : widget->errors())
                std::fprintf (stderr, "%s\n", e.toString().toStdString().c_str());
            return 1;
        }

        if (auto* ro = widget->rootObject())
            ro->setProperty ("engine", QVariant::fromValue<QObject*> (engine.get()));

    // Presets first, then --set (same order as the QQuickView path).
        for (const auto& pf : presetFiles)
        {
            std::string err;
            const auto vals = ovpreset::read (pf.toStdString(), &err);
            if (vals.empty())
            {
                std::fprintf (stderr, "[preset] %s: %s\n", qPrintable (pf), err.c_str());
                continue;
            }
            int applied = 0;
            for (const auto& kv : vals)
                if (auto* prm = engine->typedParam (QString::fromStdString (kv.first)))
                {
                    prm->setValue (kv.second);
                    ++applied;
                }
            std::fprintf (stderr, "[preset] %s: %d of %zu values applied\n",
                          qPrintable (pf), applied, vals.size());
        }

        for (const auto& pv : presets)
        {
            const int eq = pv.indexOf ('=');
            if (eq <= 0) continue;
            if (auto* prm = engine->typedParam (pv.left (eq)))
                prm->setValue (pv.mid (eq + 1).toDouble());
        }

        widget->setMinimumSize (520, 386);   // half the design size
        widget->resize (w, h);
        widget->show();

        if (! shot.isEmpty())
        {
            QTimer::singleShot (shotDelay, [&] {
                const QImage img = widget->grab().toImage();
                if (! img.save (shot))
                    std::fprintf (stderr, "could not write %s\n", shot.toStdString().c_str());
                else
                    std::printf ("wrote %s (%dx%d)\n", shot.toStdString().c_str(),
                                 img.width(), img.height());
                app->quit();
            });
        }

        return app->exec();
    }

    // ---- default: a real Qt Quick window ----------------------------------
    QQuickView view;
    view.setColor (QColor (0x16, 0x18, 0x1d));
    view.setResizeMode (QQuickView::SizeRootObjectToView);
    view.rootContext()->setContextProperty ("engine", engine.get());
    // logicalDpi / 96: OvTheme.pt() divides by this so font sizes stay in
    // logical pixels on a scaled display (see OvTheme.qml).
    view.rootContext()->setContextProperty ("ovUiScale", uiScale());
    // ONYXTEON_NOSIGNAL=1 makes the mock generate silence, so nothing is drawn
    // over the plots and the glass can be measured on a clean background.
    if (qEnvironmentVariableIsSet ("ONYXTEON_NOSIGNAL"))
        std::fprintf (stderr, "[preview] ONYXTEON_NOSIGNAL is gone: connect "
                              "nothing to the JACK inputs for silence\n");

    // ONYXTEON_CPUFROST=1 forces the QImage glass instead of the GPU shaders.
    view.rootContext()->setContextProperty (
        "ovCpuFrost", qEnvironmentVariableIsSet ("ONYXTEON_CPUFROST"));
    view.rootContext()->setContextProperty (
        "ovNoFrost", qEnvironmentVariableIsSet ("ONYXTEON_NOFROST"));
    // ONYXTEON_NOFOG=1 turns the EQ overflow fog off as well, for a capture
    // that has to contain none of it.
    view.rootContext()->setContextProperty (
        "ovNoFog", qEnvironmentVariableIsSet ("ONYXTEON_NOFOG"));
    // ONYXTEON_FOGRAW=1 shows the fog's strokes before the blur, for checking
    // what the source contains on its own.
    view.rootContext()->setContextProperty (
        "ovFogRaw", qEnvironmentVariableIsSet ("ONYXTEON_FOGRAW"));
    view.rootContext()->setContextProperty (
        "ovGlassSigma", qEnvironmentVariable("ONYXTEON_GLASSSIGMA", "1.0").toDouble());
    // Logical-to-device ratio the QML glass chain must render at.  Taken from
    // the screen the window opens on; 1.0 when unknown.
    {
        QScreen* scr = QGuiApplication::primaryScreen();
        view.rootContext()->setContextProperty (
            "ovDpr", scr ? scr->devicePixelRatio() : 1.0);
    }
    view.rootContext()->setContextProperty (
        "ovGlassDebug", qEnvironmentVariableIsSet ("ONYXTEON_GLASSDEBUG"));
    // labelled window-coordinate grid, for reading positions off a screenshot
    view.rootContext()->setContextProperty (
        "ovRuler", qEnvironmentVariableIsSet ("ONYXTEON_RULER"));

    // ---- ONYXTEON_SOLO=<name>[,<name>...]: show only those widgets --------
    //
    // A frame-rate number for the whole window says nothing about which part
    // spends it.  Hiding everything except one widget attributes the cost: run
    // with `each` to see them one at a time, or with a list.
    //
    // Names are the widget ids used in main.qml (plots, fog, seam, header,
    // deck, bars).  `each` runs every one in turn and reports a rate for each,
    // which is the only honest way to attribute a frame budget.
    {
        const QString solo = qEnvironmentVariable ("ONYXTEON_SOLO");
        view.rootContext()->setContextProperty ("ovSolo", solo);
        if (! solo.isEmpty())
            std::fprintf (stderr, "[solo] showing only: %s\n", solo.toUtf8().constData());

        // ONYXTEON_SOLO_OPACITY: how faintly those widgets are drawn.
        //
        // Solo hides everything else, so two widgets can never be seen at once —
        // which is fine for attributing a frame cost and useless for finding
        // where two renders disagree.  Fading the survivors lets them be laid
        // over each other: at 0.5 a one-pixel difference reads as a doubled
        // edge instead of one picture hiding the other.
        //
        // A bare number fades every soloed widget; `name=value` pairs override
        // it per widget.  Ignored without ONYXTEON_SOLO, so a normal run can
        // never be faded by a stray environment variable.
        const QString op = qEnvironmentVariable ("ONYXTEON_SOLO_OPACITY");
        view.rootContext()->setContextProperty ("ovSoloOpacity", op);
        if (! solo.isEmpty() && ! op.isEmpty())
            std::fprintf (stderr, "[solo] opacity: %s\n", op.toUtf8().constData());
    }

    // ---- --tick: publish and consume through a shared-memory ring ----------
    // The producer side (below) writes one record per event; this timer is the
    // consumer, and it is the only place that counts or prints.  Keeping the two
    // apart is the point: the audio callback and the paint path must not print,
    // allocate, or wait, and the reported rate must not depend on them.
    if (ovtick::Registry::instance().enabled)
    {
        ovtick::Registry& reg = ovtick::Registry::instance();
        reg.reportEvery = 5.0;

        const int selfPid = (int) ::getpid();
        bool okGui = reg.openRing (ovtick::Producer::Gui,
                                   ovtick::Registry::ringNameFor (selfPid, ovtick::Producer::Gui), true);
        bool okRender = reg.openRing (ovtick::Producer::Render,
                                      ovtick::Registry::ringNameFor (selfPid, ovtick::Producer::Render), true);
        if (! okGui || ! okRender)
            std::fprintf (stderr, "[tick] could not create the shared ring; "
                                  "counters still work, the reader path does not\n");

        auto* consumer = new QTimer (&view);
        QObject::connect (consumer, &QTimer::timeout, [&reg] { reg.drainRing(); });
        consumer->start (100);          // drain often; the report is time-based
    }

    // ---- --tick: one line per PRESENTED frame ------------------------------
    // frameSwapped is the only honest answer to "how many frames did the user
    // see": beforeRendering counts work that may be dropped, and a repaint
    // counter counts requests.  It is also the number that has to be compared
    // with `sample`, because the display cannot show more than it is given.
    if (ovtick::Registry::instance().enabled)
    {
        QObject::connect (&view, &QQuickWindow::frameSwapped, [&view] {
            ovtick::markKind ("frame", ovtick::TickKind::FrameSwapped, 0.0,
                              ovtick::Producer::Render);
        });
        // And the render loop's own sync/swap phases, which separate "Qt is
        // behind" from "the driver is slow".
        // These two also run on the render thread, so they must use its ring:
        // counting them on the GUI thread's ring gave intervals measured from
        // whenever the GUI thread happened to fold them, which is meaningless.
        QObject::connect (&view, &QQuickWindow::beforeRendering, [] {
            ovtick::Registry::instance().push (ovtick::TickKind::FrameSwapped,
                                               ovtick::Producer::Render);
        });
        QObject::connect (&view, &QQuickWindow::afterRendering, [] {
            ovtick::Registry::instance().push (ovtick::TickKind::FrameSwapped,
                                               ovtick::Producer::Render);
        });
    }

    view.setSource (sourceUrl);

    if (view.status() == QQuickView::Error)
    {
        for (const auto& e : view.errors())
            std::fprintf (stderr, "%s\n", e.toString().toStdString().c_str());
        return 1;
    }

    if (auto* ro = view.rootObject())
        ro->setProperty ("engine", QVariant::fromValue<QObject*> (engine.get()));

    // Presets first, then --set, so --set can override single values of a preset.
    for (const auto& pf : presetFiles)
    {
        std::string err;
        const auto vals = ovpreset::read (pf.toStdString(), &err);
        if (vals.empty())
        {
            std::fprintf (stderr, "[preset] %s: %s\n", qPrintable (pf), err.c_str());
            continue;
        }

        int applied = 0;
        for (const auto& kv : vals)
            if (auto* prm = engine->typedParam (QString::fromStdString (kv.first)))
            {
                prm->setValue (kv.second);
                ++applied;
            }
        std::fprintf (stderr, "[preset] %s: %d of %zu values applied\n",
                      qPrintable (pf), applied, vals.size());
    }

    for (const auto& pv : presets)
    {
        const int eq = pv.indexOf ('=');
        if (eq <= 0) continue;
        const auto id = pv.left (eq);
        const auto val = pv.mid (eq + 1).toDouble();
        if (auto* prm = engine->typedParam (id))
            prm->setValue (val);
    }

    // ---- --ctl: parameters driven from outside through shared memory ------
    //
    // A writer (ovctl) sets a target and a time constant; this timer moves the
    // parameter towards it every frame, so whatever the UI does in response is
    // the response to a drag, not to a step.  Keeping it out of the paint path
    // matters: the point is to measure the UI, so the driver must not be part of
    // what is being measured.
    if (! ctlName.isEmpty())
    {
        auto* blk = ovctl::map (ctlName.toStdString(), true);
        if (blk == nullptr)
        {
            std::fprintf (stderr, "[ctl] cannot map /%s\n",
                          ctlName.toStdString().c_str());
        }
        else
        {
            if (qEnvironmentVariableIsSet ("ONYXTEON_CTLDEBUG"))
            {
                auto& src = engine->source();
                for (int i = 0; i < src.count(); ++i)
                    std::fprintf (stderr, "[ctl] param[%d] id='%s'\n", i,
                                  src.spec (i).id.c_str());
            }
            std::fprintf (stderr, "[ctl] control block /%s ready (ovctl --name %s set …)\n",
                          ctlName.toStdString().c_str(), ctlName.toStdString().c_str());
            auto* seen = new quint32[ovctl::kSlots] {};
            auto* cur  = new double[ovctl::kSlots] {};
            auto* tgt  = new double[ovctl::kSlots] {};
            auto* tau  = new double[ovctl::kSlots] {};
            auto* live = new bool[ovctl::kSlots] {};
            auto* last = new std::chrono::steady_clock::time_point;
            auto* eqViews = new QList<OvEqView*>;

            // A raw pointer into the engine: the lambda cannot capture the
            // unique_ptr, and the engine outlives this timer either way.
            OvEngine* eng = engine.get();
            auto* tick = new QTimer (&view);
            tick->setInterval (8);          // same cadence as the engine's poll
            QObject::connect (tick, &QTimer::timeout, [=, &view] {
                const auto now = std::chrono::steady_clock::now();
                const double dt = (*last == std::chrono::steady_clock::time_point())
                                ? 0.0
                                : std::chrono::duration<double> (now - *last).count();
                *last = now;
                bool touched = false;

                for (int i = 0; i < ovctl::kSlots; ++i)
                {
                    auto& sl = blk->entries[i];
                    const quint32 s = sl.seq.load (std::memory_order_acquire);
                    if (s == 0) continue;

                    if (s != seen[i])
                    {
                        // A new request: start from where the parameter is now,
                        // so a second command mid-drag continues smoothly.
                        seen[i] = s;
                        tgt[i]  = double (sl.target);
                        tau[i]  = double (sl.tau);
                        auto* p = eng->typedParam (QString::fromLatin1 (sl.id)
                                                          .trimmed());
                        cur[i]  = p ? p->value() : tgt[i];
                        live[i] = (p != nullptr);
                        if (qEnvironmentVariableIsSet ("ONYXTEON_CTLDEBUG"))
                            std::fprintf (stderr, "[ctl] slot %d id '%s' target %g tau %g -> %s\n",
                                          i, sl.id, tgt[i], tau[i],
                                          p ? "param found" : "NO SUCH PARAM");
                    }
                    if (! live[i]) continue;

                    auto* p = eng->typedParam (QString::fromLatin1 (sl.id)
                                                      .trimmed());
                    if (p == nullptr) { live[i] = false; continue; }

                    const double next = ovctl::approach (cur[i], tgt[i], dt, tau[i]);
                    if (std::abs (next - cur[i]) > 1e-9)
                    {
                        cur[i] = next;
                        p->setValue (next);
                        touched = true;
                    }
                }

                // Repaint the plots by hand.
                //
                // OvParam::setNorm updates its own cached norm, so OvEngine's
                // poll sees no difference and never emits paramRevisionChanged
                // for a change that came from outside — host automation, a
                // preset load, or this control block.  A drag repaints because
                // mouseMoveEvent calls update() itself, which is why only
                // programmatic changes were affected.
                //
                // The real fix belongs in the engine (compare against the
                // source, not against its own cache).  Until then the harness
                // asks the views directly.
                if (touched)
                {
                    if (eqViews->isEmpty() && view.rootObject() != nullptr)
                        *eqViews = view.rootObject()->findChildren<OvEqView*>();
                    for (auto* v : *eqViews) v->update();
                }
            });
            tick->start();
        }
    }

    // Does the node end up where the cursor went?  This is "the place I can
    // drag it and the node itself are not together" complaint, asked as a
    // number: press on a node, move to a known point, and compare the node's
    // resulting position with that point.
    if (std::getenv ("ONYXTEON_DRAGFOLLOW") != nullptr)
    {
        QTimer::singleShot (900, [&] {
            OvEqView* v = nullptr;
            for (auto* c : view.rootObject()->findChildren<OvEqView*>())
                if (c->prefix() == QStringLiteral ("sc")) v = c;
            if (v == nullptr) { std::fprintf (stderr, "[follow] no view\n"); app->quit(); return; }

            std::fprintf (stderr, "[follow] view %gx%g\n", v->width(), v->height());
            for (int i = 0; i < 5; ++i)
            {
                const QPointF start = v->nodePosition (i);
                const QPointF goal  = start + QPointF (37.0, -26.0);

                auto send = [&] (QEvent::Type t, QPointF p, Qt::MouseButton b,
                                 Qt::MouseButtons held) {
                    QMouseEvent m (t, p, p, p, b, held, Qt::NoModifier);
                    QCoreApplication::sendEvent (v, &m);
                };
                send (QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
                send (QEvent::MouseMove, goal, Qt::NoButton, Qt::LeftButton);
                send (QEvent::MouseButtonRelease, goal, Qt::LeftButton, Qt::NoButton);

                const QPointF now = v->nodePosition (i);
                const double dx = now.x() - goal.x(), dy = now.y() - goal.y();
                const double off = std::hypot (dx, dy);
                std::fprintf (stderr,
                    "[follow] node %d  start (%6.1f,%6.1f)  goal (%6.1f,%6.1f)"
                    "  got (%6.1f,%6.1f)  off %5.2f px  %s\n",
                    i, start.x(), start.y(), goal.x(), goal.y(),
                    now.x(), now.y(), off, off <= 1.0 ? "PASS" : "FAIL");
            }
            app->quit();
        });
    }

    // Regression harness for the stuck-drag bug: press in empty space, then
    // move with no button held.  No node may move.
    if (std::getenv ("ONYXTEON_DRAGTEST2") != nullptr)
    {
        QTimer::singleShot (900, [&] {
            OvEqView* eqView = nullptr;
            for (auto* g : view.rootObject()->findChildren<QQuickItem*>())
                if (g->objectName().startsWith (QLatin1String ("glass-")))
                {
                    const QVariant o = g->property ("srcOrigin");
                    std::fprintf (stderr, "[glass] %-8s item xy=(%g,%g) %gx%g  srcOrigin=(%g,%g)\n",
                                  qPrintable (g->objectName()),
                                  g->x(), g->y(), g->width(), g->height(),
                                  o.toPointF().x(), o.toPointF().y());
                }
            for (auto* v : view.rootObject()->findChildren<OvEqView*>())
                if (v->prefix() == QStringLiteral ("sc")) eqView = v;
            if (eqView == nullptr) { std::fprintf (stderr, "[drag2] no view\n"); app->quit(); return; }

            const QStringList ids { "sc1Freq","sc2Freq","sc3Freq","sc4Freq","sc5Freq" };
            QVector<double> before;
            for (const auto& id : ids) before << engine->typedParam (id)->value();

            // a normal click on a node: press + release
            const QPointF n2 = eqView->nodePosition (1);
            QMouseEvent p1 (QEvent::MouseButtonPress, n2, n2, n2,
                            Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QCoreApplication::sendEvent (eqView, &p1);
            QMouseEvent r1 (QEvent::MouseButtonRelease, n2, n2, n2,
                            Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
            QCoreApplication::sendEvent (eqView, &r1);

            // press in empty space, then move with nothing held
            const QPointF empty (60.0, 300.0);
            QMouseEvent p2 (QEvent::MouseButtonPress, empty, empty, empty,
                            Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QCoreApplication::sendEvent (eqView, &p2);
            const QPointF far (400.0, 260.0);
            QMouseEvent m2 (QEvent::MouseMove, far, far, far,
                            Qt::NoButton, Qt::NoButton, Qt::NoModifier);
            QCoreApplication::sendEvent (eqView, &m2);

            int moved = 0;
            for (int i = 0; i < ids.size(); ++i)
                if (std::abs (engine->typedParam (ids[i])->value() - before[i]) > 1e-6) ++moved;

            // and the positive case: a real drag with the button held must
            // move exactly that node
            const double f2before = engine->typedParam ("sc2Freq")->value();
            QMouseEvent p3 (QEvent::MouseButtonPress, n2, n2, n2,
                            Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QCoreApplication::sendEvent (eqView, &p3);
            const QPointF n2moved (n2.x() - 40.0, n2.y());
            QMouseEvent m3 (QEvent::MouseMove, n2moved, n2moved, n2moved,
                            Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
            QCoreApplication::sendEvent (eqView, &m3);
            QMouseEvent r3 (QEvent::MouseButtonRelease, n2moved, n2moved, n2moved,
                            Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
            QCoreApplication::sendEvent (eqView, &r3);

            const double f2after = engine->typedParam ("sc2Freq")->value();
            const bool dragWorks = std::abs (f2after - f2before) > 1.0;

            std::fprintf (stderr, "[drag2] no-button move: %d nodes moved -> %s\n",
                          moved, moved == 0 ? "PASS" : "FAIL");
            std::fprintf (stderr, "[drag2] real drag: sc2Freq %.1f -> %.1f Hz -> %s\n",
                          f2before, f2after, dragWorks ? "PASS" : "FAIL");
            app->quit();
        });
    }

    if (std::getenv ("ONYXTEON_METERS") != nullptr)
    {
        auto* t = new QTimer (app.get());
        QObject::connect (t, &QTimer::timeout, [&] {
            std::fprintf (stderr, "[meters] inL %.1f outL %.1f  glowIn %.3f glowOut %.3f\n",
                          engine->inL(), engine->outL(),
                          std::max (0.0, std::min (1.0, (engine->inL() + 60) / 60)),
                          std::max (0.0, std::min (1.0, (engine->outL() + 60) / 60)));
        });
        t->start (500);
    }

    if (std::getenv ("ONYXTEON_DUMP") != nullptr)
    {
        QTimer::singleShot (1500, [&view] {
            auto* r = view.rootObject();
            std::fprintf (stderr, "[dump] root %s %gx%g\n",
                          r ? r->metaObject()->className() : "null",
                          r ? r->property ("width").toDouble() : 0.0,
                          r ? r->property ("height").toDouble() : 0.0);
            for (auto* g : view.rootObject()->findChildren<QQuickItem*>())
                if (g->objectName().startsWith (QLatin1String ("glass-")))
                {
                    const QVariant o = g->property ("srcOrigin");
                    std::fprintf (stderr, "[glass] %-8s item xy=(%g,%g) %gx%g  srcOrigin=(%g,%g)\n",
                                  qPrintable (g->objectName()),
                                  g->x(), g->y(), g->width(), g->height(),
                                  o.toPointF().x(), o.toPointF().y());
                }
            for (auto* v : view.rootObject()->findChildren<OvEqView*>())
                std::fprintf (stderr, "[dump] view %p prefix=%s %gx%g at (%g,%g) "
                              "visible=%d parent=%s\n", (void*) v,
                              v->prefix().toStdString().c_str(), v->width(), v->height(),
                              v->x(), v->y(), (int) v->isVisible(),
                              v->parentItem() ? v->parentItem()->metaObject()->className() : "null");
            for (auto* q : view.rootObject()->findChildren<QQuickItem*>())
                if (q->objectName().startsWith ("OvEqView"))
                    std::fprintf (stderr, "[dump] named %s %gx%g\n",
                                  q->objectName().toStdString().c_str(), q->width(), q->height());
        });
    }

    // A floor, so the UI cannot be dragged down to an unreadable sliver.  Half
    // the design size (1040x772, see OvTheme.designW/H) is still a legible
    // layout; below that the 550 px seam glass and the header stop fitting.
    view.setMinimumSize (QSize (520, 386));

    view.resize (w, h);

    // Declared out here on purpose: the block below only shows the view for
    // the duration of the setup, and a holder declared inside it would destroy
    // the debug window the moment the block ended (it did, and the window
    // never appeared).
    std::unique_ptr<QWidget> hudWin;

    // Window-level event trace (ONYXTEON_HITTEST): every press the QQuickWindow
    // receives, plus the item Qt finds under it.  This separates "the window
    // never got the click" from "an item above the plot swallowed it".
    if (std::getenv ("ONYXTEON_HITTEST") != nullptr)
    {
        struct Trace : QObject
        {
            QQuickWindow* w;
            explicit Trace (QQuickWindow* win) : w (win) {}
            bool eventFilter (QObject*, QEvent* e) override
            {
                if (e->type() != QEvent::MouseButtonPress) return false;
                auto* me = static_cast<QMouseEvent*> (e);
                const QPointF p = me->position();
                QStringList hits;
                for (QQuickItem* it = w->contentItem()->childAt (p.x(), p.y());
                     it != nullptr; it = it->parentItem())
                    hits << QStringLiteral ("%1[%2]")
                            .arg (it->objectName().isEmpty()
                                      ? QString::fromLatin1 (it->metaObject()->className())
                                      : it->objectName())
                            .arg (it->isEnabled() ? "on" : "off");
                std::fprintf (stderr, "[win] press (%.1f,%.1f) -> %s\n",
                              p.x(), p.y(),
                              hits.isEmpty() ? "(nothing)" : hits.join ('/').toStdString().c_str());
                return false;
            }
        };
        view.installEventFilter (new Trace (&view));
    }

    {
        view.show();

        // ---- --hud: a separate, unscaled read-out window ----------------
        //  A label next to the UI that says, live, where each EQ node is
        //  drawn and which node a click at the cursor would grab.  This
        //  replaces guessing from screenshots: the numbers are the same ones
        //  paint() and mousePressEvent() use.
        if (hud)
        {
            auto* win = new QWidget (nullptr, Qt::Tool | Qt::WindowStaysOnTopHint);
            win->setWindowTitle (QStringLiteral ("OnyxTEON hit-test"));
            win->setStyleSheet (QStringLiteral (
                "QWidget{background:#16181d;color:#e7ebf2}"
                "QLabel{font-family:monospace;font-size:11px;color:#e7ebf2}"
                "QLabel#head{color:#4a525f}"));
            auto* lay = new QVBoxLayout (win);
            lay->setContentsMargins (10, 8, 10, 8);
            auto* st = new QLabel (win);
            st->setObjectName ("head");
            auto* body = new QLabel (win);
            body->setTextFormat (Qt::PlainText);
            lay->addWidget (st);
            lay->addWidget (body);
            win->resize (430, 190);
            win->show();
            hudWin.reset (win);

            // Track the cursor by polling, not by an event filter.  A filter
            // on the QQuickWindow never fired for motion that had already been
            // routed to a QML item, and a HUD that says "move the cursor" while
            // the cursor is moving is worse than useless.
            // Everything below is in WINDOW coordinates.  Mixing item-local
            // sizes with window-space points is what made the first version of
            // this HUD report nonsense, and it is the same mistake that makes a
            // hit-test look "offset" from the drawing.
            auto views = view.rootObject()
                             ? view.rootObject()->findChildren<OvEqView*> ()
                             : QList<OvEqView*> ();

            auto* tick = new QTimer (win);
            tick->setInterval (40);       // 25 Hz is plenty for reading text
            QObject::connect (tick, &QTimer::timeout,
                              [&view, views, body, st, &probe] {
                const QPointF winPos = (probe.x() >= 0)
                                       ? probe
                                       : QPointF (view.mapFromGlobal (QCursor::pos()));

                OvEqView* hot = nullptr;
                QRectF hotRect;
                QStringList rows;
                for (auto* v : views)
                {
                    const QPointF a = v->mapToScene ({0, 0});
                    const QPointF b = v->mapToScene ({v->width(), v->height()});
                    const QRectF r (a, b);
                    rows << QStringLiteral ("  %1: local %2x%3   screen %4,%5-%6,%7")
                            .arg (v->prefix().isEmpty() ? QStringLiteral ("?")
                                                        : v->prefix())
                            .arg (v->width(), 0, 'f', 0).arg (v->height(), 0, 'f', 0)
                            .arg (r.left(), 0, 'f', 0).arg (r.top(), 0, 'f', 0)
                            .arg (r.right(), 0, 'f', 0).arg (r.bottom(), 0, 'f', 0);
                    if (hot == nullptr && r.contains (winPos)) { hot = v; hotRect = r; }
                }

                const QString qmlGeom = view.rootObject()
                        ? view.rootObject()->property ("geomText").toString()
                        : QStringLiteral ("(no qml)");
                st->setText (QStringLiteral ("window %1x%2   dpr %3   cursor (%4, %5)\n%6\n%7")
                             .arg (view.width()).arg (view.height())
                             .arg (view.devicePixelRatio(), 0, 'f', 2)
                             .arg (winPos.x(), 0, 'f', 0).arg (winPos.y(), 0, 'f', 0)
                             .arg (qmlGeom)
                             .arg (rows.join (QStringLiteral ("\n"))));

                if (hot == nullptr)
                { body->setText (QStringLiteral ("\ncursor is outside both plots")); return; }

                // nodes in WINDOW coordinates, so these numbers are literally
                // where the disc is on screen
                const double scale = hotRect.width() / std::max (1.0, hot->width());
                QStringList nl;
                int best = -1;
                double bestD = 1e9;
                for (int i = 0; i < 5; ++i)
                {
                    const QPointF n = hot->nodePosition (i);
                    const QPointF w = hot->mapToScene (n);
                    const double d = std::hypot (w.x() - winPos.x(), w.y() - winPos.y());
                    if (d < bestD) { bestD = d; best = i; }
                    nl << QStringLiteral ("  %1 (%2, %3)  d=%4")
                            .arg (i).arg (w.x(), 0, 'f', 0).arg (w.y(), 0, 'f', 0)
                            .arg (d, 0, 'f', 0);
                }
                const double grabR = 11.0 * scale;
                body->setText (QStringLiteral ("\nnodes on screen (window px):\n%1\n"
                                               "  -> %2   grab radius %3 px")
                               .arg (nl.join (QStringLiteral ("\n")))
                               .arg (bestD <= grabR
                                     ? QStringLiteral ("GRAB node %1").arg (best)
                                     : QStringLiteral ("no grab (nearest %1, %2 px)")
                                           .arg (best).arg (bestD, 0, 'f', 0))
                               .arg (grabR, 0, 'f', 0));
            });
            tick->start();

            std::fprintf (stderr, "[hud] hit-test window open\n");
        }

        // Prove the event loop is alive independently of the render loop: a
        // plain QTimer that fires even if nothing is ever painted.
        if (std::getenv ("ONYXTEON_FPS") != nullptr)
        {
            auto* beat = new QTimer (&view);
            beat->setInterval (1000);
            QObject::connect (beat, &QTimer::timeout, [&view] {
                static int n = 0;
                std::fprintf (stderr, "[fps] tick %d  exposed=%d  visible=%d  size=%dx%d\n",
                              ++n, (int) view.isExposed(), (int) view.isVisible(),
                              view.width(), view.height());
            });
            beat->start();
        }

        // The only honest frame-rate number: frames the window presented.
        if (std::getenv ("ONYXTEON_FPS") != nullptr)
        {
            // Is the window exposed at all?  A QQuickWindow that never gets an
            // expose does not run its render loop.
            QObject::connect (&view, &QQuickWindow::sceneGraphInitialized, [&view] {
                std::fprintf (stderr, "[fps] scene graph initialised, api=%d backend=%s\n",
                              (int) view.rendererInterface()->graphicsApi(),
                              view.sceneGraphBackend().toStdString().c_str());
            });
            QObject::connect (&view, &QQuickWindow::sceneGraphError,
                              [] (QQuickWindow::SceneGraphError e, const QString& m) {
                std::fprintf (stderr, "[fps] SCENE GRAPH ERROR %d: %s\n",
                              (int) e, m.toStdString().c_str());
            });
            QObject::connect (&view, &QQuickWindow::frameSwapped, [&view] {
                static int n = 0;
                static auto t0 = std::chrono::steady_clock::now();
                ++n;
                const auto now = std::chrono::steady_clock::now();
                const double dt = std::chrono::duration<double> (now - t0).count();
                if (dt >= 2.0)
                {
                    std::fprintf (stderr, "[fps] window %.1f fps\n", n / dt);
                    n = 0; t0 = now;
                }
            });
        }
    }

    if (! shot.isEmpty())
    {
        // let a couple of frames render, then grab and quit
        QTimer::singleShot (shotDelay, [&] {
            const QImage img = view.grabWindow();
            if (! img.save (shot))
                std::fprintf (stderr, "could not write %s\n", shot.toStdString().c_str());
            else
                std::printf ("wrote %s (%dx%d)\n", shot.toStdString().c_str(),
                             img.width(), img.height());
            app->quit();
        });
    }

    return app->exec();
}
