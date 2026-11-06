// ============================================================
//  overteon-qml-preview — standalone QML viewer for the editor UI.
//
//  Runs the exact same QML front end as the plug-in, but against
//  OvMockSource (same parameter layout, animated meters), so the UI can
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
#include "OvMockSource.h"
#include "OvQmlTypes.h"
#include "OvEqView.h"

#include <QApplication>
#include <QGuiApplication>
#include <QQuickView>
#include <QQuickWindow>
#include <QQuickItem>
#include <QQmlContext>
#include <QScreen>
#include <QGuiApplication>
#include <QTimer>
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
#include <cstdio>
#include <cstdlib>
#include <QLibrary>

#ifdef Q_OS_LINUX
 #include <X11/Xlib.h>
 #include <X11/extensions/sync.h>
#endif
#include <memory>
#include <string>

namespace
{
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

   #ifdef Q_OS_LINUX
    /// Does this display provide a vblank / UST counter?
    ///
    /// This is what Qt's vsync'd render loop waits on.  A server with no such
    /// counter (measured on a bare X session here: only IDLETIME and
    /// SERVERTIME are advertised) makes every glXSwapBuffers with swap
    /// interval 1 block until the driver's ~900 ms timeout:
    ///
    ///     interval 1 : 904.89 ms per swap   (1.1 fps)
    ///     interval 0 :   0.02 ms per swap   (53523 fps)
    ///
    /// That reads as a completely frozen UI.  When there is no counter, Qt
    /// must not wait for vsync; its render loop then paces itself instead.
    bool hasVblankCounter()
    {
       #ifdef Q_OS_LINUX
        if (auto* dpy = XOpenDisplay (nullptr))
        {
            int ev = 0, er = 0;
            bool found = false;
            if (XSyncQueryExtension (dpy, &ev, &er))
            {
                int n = 0;
                if (auto* counters = XSyncListSystemCounters (dpy, &n))
                {
                    for (int i = 0; i < n; ++i)
                    {
                        const char* name = counters[i].name;
                        if (name == nullptr) continue;
                        const std::string s (name);
                        if (s.find ("vblank") != std::string::npos
                            || s.find ("VBLANK") != std::string::npos
                            || s.find ("UST")    != std::string::npos
                            || s.find ("MSC")    != std::string::npos)
                        {
                            found = true;
                            break;
                        }
                    }
                    XSyncFreeSystemCounterList (counters);
                }
            }
            XCloseDisplay (dpy);
            return found;
        }
       #endif
        return true;      // cannot tell: leave Qt alone
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
    if (qEnvironmentVariableIsEmpty ("QSG_NO_VSYNC") && ! hasVblankCounter())
    {
        std::fprintf (stderr, "[preview] this display has no vblank counter; "
                              "disabling vsync (otherwise the UI runs at 1 fps)\n");
        qputenv ("QSG_NO_VSYNC", "1");
    }
   #endif

    // Defaults only: a caller must be able to override these, e.g.
    //   QT_QUICK_BACKEND=software  (wine without working GL)
    //   QT_OPENGL=angle            (Windows)
    // Forcing OpenGL unconditionally is what made the window hang under wine
    // with no fallback.
    if (qEnvironmentVariableIsEmpty ("QSG_RENDER_LOOP"))
        qputenv ("QSG_RENDER_LOOP", "basic");


    // Default window: the design's aspect ratio, sized to the screen so it is
    // not tiny on a 2560x1600 panel.  --size overrides.  The actual figure is
    // computed further down, once QApplication exists -- primaryScreen()
    // returns null before that and the default would be silently ignored.
    int w = 840, h = 624;
    bool sizeGiven = false;
    QString shot, customQml;
    int shotDelay = 1200;
    bool compat = wine || qEnvironmentVariableIsSet ("ONYXTEON_COMPAT");
    QStringList presets;          // id=value pairs, applied before the first frame

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
        else if (a == "--freeze")
        {
            OvMockSource::frozen = true;
        }
        else if (a == "--set" && i + 1 < argc)
        {
            presets << QString::fromLatin1 (argv[++i]);
        }
        else if (a == "--qml" && i + 1 < argc)
        {
            customQml = QString::fromLatin1 (argv[++i]);
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

    auto engine = std::make_unique<OvEngine> (std::make_unique<OvMockSource>());

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

    for (const auto& pv : presets)
        {
            const int eq = pv.indexOf ('=');
            if (eq <= 0) continue;
            if (auto* prm = engine->typedParam (pv.left (eq)))
                prm->setValue (pv.mid (eq + 1).toDouble());
        }

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

    view.setSource (sourceUrl);

    if (view.status() == QQuickView::Error)
    {
        for (const auto& e : view.errors())
            std::fprintf (stderr, "%s\n", e.toString().toStdString().c_str());
        return 1;
    }

    if (auto* ro = view.rootObject())
        ro->setProperty ("engine", QVariant::fromValue<QObject*> (engine.get()));

    for (const auto& pv : presets)
    {
        const int eq = pv.indexOf ('=');
        if (eq <= 0) continue;
        const auto id = pv.left (eq);
        const auto val = pv.mid (eq + 1).toDouble();
        if (auto* prm = engine->typedParam (id))
            prm->setValue (val);
    }

    // Regression harness for the stuck-drag bug: press in empty space, then
    // move with no button held.  No node may move.
    if (std::getenv ("ONYXTEON_DRAGTEST2") != nullptr)
    {
        QTimer::singleShot (900, [&] {
            OvEqView* eqView = nullptr;
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

    view.resize (w, h);

    {
        view.show();

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
                std::fprintf (stderr, "[fps] scene graph initialised, api=%d\n",
                              (int) view.rendererInterface()->graphicsApi());
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
