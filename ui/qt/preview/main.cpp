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
#include <QTimer>
#include <QFontDatabase>
#include <QFont>
#include <QImage>
#include <QPainter>
#include <QWidget>
#include <QStringList>
#include <QUrl>
#include <QVariant>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>

namespace
{
    // ---- compatibility mode ------------------------------------------------
    //
    //  Qt Quick's own window never presents under wine (verified: the scene
    //  animates — grabWindow() at different times differs — but the X window
    //  keeps showing its first frame, while a plain QWidget repaints fine).
    //  So: keep the QQuickView off-screen, grab it every frame and blit the
    //  result into a QWidget.  Same QML, same rendering, a window that works.
    class CompatWindow : public QWidget
    {
    public:
        CompatWindow (QQuickView* viewIn, int w, int h)
            : view (viewIn)
        {
            setWindowTitle (QStringLiteral ("OnyxTEON (compat)"));
            setFixedSize (w, h);
            setAttribute (Qt::WA_OpaquePaintEvent, true);

            frame.setInterval (1000 / 60);
            QObject::connect (&frame, &QTimer::timeout, this, [this] {
                const QImage img = view->grabWindow();
                if (! img.isNull())
                {
                    backBuffer = img;
                    update();
                }
            });
            frame.start();
        }

    protected:
        void paintEvent (QPaintEvent*) override
        {
            QPainter p (this);
            if (backBuffer.isNull())
                p.fillRect (rect(), QColor (0x16, 0x18, 0x1d));
            else
                p.drawImage (rect(), backBuffer);
        }

    private:
        QQuickView* view = nullptr;
        QImage      backBuffer;
        QTimer      frame;
    };
}

int main (int argc, char** argv)
{
    // Defaults only: a caller must be able to override these, e.g.
    //   QT_QUICK_BACKEND=software  (wine without working GL)
    //   QT_OPENGL=angle            (Windows)
    // Forcing OpenGL unconditionally is what made the window hang under wine
    // with no fallback.
    if (qEnvironmentVariableIsEmpty ("QSG_RENDER_LOOP"))
        qputenv ("QSG_RENDER_LOOP", "basic");

    int w = 840, h = 624;
    QString shot, customQml;
    int shotDelay = 1200;
    bool compat = qEnvironmentVariableIsSet ("ONYXTEON_COMPAT");
    QStringList presets;          // id=value pairs, applied before the first frame

    for (int i = 1; i < argc; ++i)
    {
        const std::string a (argv[i]);
        if (a == "--size" && i + 1 < argc)
        {
            const QString s = QString::fromLatin1 (argv[++i]);
            const auto parts = s.split ('x');
            if (parts.size() == 2) { w = parts[0].toInt(); h = parts[1].toInt(); }
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

    QQuickView view;
    view.setColor (QColor (0x16, 0x18, 0x1d));
    view.setResizeMode (QQuickView::SizeRootObjectToView);
    view.rootContext()->setContextProperty ("engine", engine.get());

    if (customQml.isEmpty())
        view.setSource (QUrl (QStringLiteral ("qrc:/qml/main.qml")));
    else
        view.setSource (QUrl::fromLocalFile (customQml));

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

    view.resize (w, h);

    std::unique_ptr<CompatWindow> compatWindow;

    if (compat)
    {
        // the Quick view stays off-screen; the widget shows its frames
        compatWindow = std::make_unique<CompatWindow> (&view, w, h);
        compatWindow->show();
    }
    else
    {
        view.show();

        // The only honest frame-rate number: frames the window presented.
        if (std::getenv ("ONYXTEON_FPS") != nullptr)
        {
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
