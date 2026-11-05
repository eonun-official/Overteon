// ============================================================
//  overteon-qml-preview — standalone QML viewer for the editor UI.
//
//  Runs the exact same QML front end as the plugin, but against
//  OvMockSource (same parameter layout, animated meters), so the UI can
//  be developed, screenshotted and compared with web_fx/index.html
//  without starting a DAW or a plugin host.
//
//    ./overteon-qml-preview [--size WxH] [--shot file.png]
// ============================================================
#include "OvEngine.h"
#include "OvMockSource.h"
#include "OvQmlTypes.h"

#include <QGuiApplication>
#include <QQuickView>
#include <QQuickWindow>
#include <QQuickItem>
#include <QQmlContext>
#include <QScreen>
#include <QTimer>
#include <QFontDatabase>
#include <QImage>
#include <QPainter>
#include <cstdio>
#include <cstring>
#include <string>
#include <memory>

int main (int argc, char** argv)
{
    qputenv ("QSG_RENDER_LOOP", "basic");
    qputenv ("QT_QUICK_BACKEND", "opengl");

    QGuiApplication app (argc, argv);
    ovRegisterQmlTypes();

    for (const char* f : { ":/res/Inter-Regular.ttf", ":/res/Inter-Medium.ttf",
                           ":/res/Inter-SemiBold.ttf", ":/res/Inter-Bold.ttf",
                           ":/res/edix.otf" })
        QFontDatabase::addApplicationFont (QString::fromLatin1 (f));

    int w = 840, h = 624;
    QString shot, customQml;

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
        else if (a == "--freeze")
        {
            OvMockSource::frozen = true;
        }
        else if (a == "--qml" && i + 1 < argc)
        {
            customQml = QString::fromLatin1 (argv[++i]);
        }
    }

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

    view.resize (w, h);
    view.show();

    if (! shot.isEmpty())
    {
        // let a couple of frames render, then grab and quit
        QTimer::singleShot (1200, [&] {
            const QImage img = view.grabWindow();
            if (! img.save (shot))
                std::fprintf (stderr, "could not write %s\n", shot.toStdString().c_str());
            else
                std::printf ("wrote %s (%dx%d)\n", shot.toStdString().c_str(),
                             img.width(), img.height());
            app.quit();
        });
    }

    return app.exec();
}
