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
#include "OvEqView.h"
#include <QMouseEvent>


#include <QGuiApplication>
#include <QQuickView>
#include <QQuickWindow>
#include <QQuickItem>
#include <QQmlContext>
#include <QScreen>
#include <QTimer>
#include <QStringList>
#include <QFontDatabase>
#include <QImage>
#include <QPainter>
#include <cstdio>
#include <cmath>
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
    int shotDelay = 1200;
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
        else if (a == "--freeze")
        {
            OvMockSource::frozen = true;
        }
        else if (a == "--shot-delay" && i + 1 < argc)
        {
            shotDelay = QString::fromLatin1 (argv[++i]).toInt();
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
    view.show();

    if (std::getenv ("ONYXTEON_DRAGTEST") != nullptr)
    {
        QTimer::singleShot (600, [&] {
            OvEqView* eq = nullptr;
            for (auto* v : view.rootObject()->findChildren<OvEqView*>())
                if (v->prefix() == QStringLiteral ("pq")) eq = v;
            if (eq == nullptr) { std::fprintf (stderr, "[drag] pq view not found\n"); return; }

            const QPointF before = eq->nodePosition (1);
            std::fprintf (stderr, "[drag] node 2 before: %.1f,%.1f  gain=%.2f\n",
                          before.x(), before.y(), engine->typedParam ("pq2Gain")->value());

            const double dx = std::getenv ("ONYXTEON_DRAGDX") ? std::atof (std::getenv ("ONYXTEON_DRAGDX")) : 0.0;
            const double dy = std::getenv ("ONYXTEON_DRAGDY") ? std::atof (std::getenv ("ONYXTEON_DRAGDY")) : -20.0;
            const QPointF to (before.x() + dx, before.y() + dy);

            QMouseEvent press (QEvent::MouseButtonPress, before, before, to,
                               Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QCoreApplication::sendEvent (eq, &press);

            QMouseEvent move (QEvent::MouseMove, to, to, to,
                              Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
            QCoreApplication::sendEvent (eq, &move);

            QMouseEvent rel (QEvent::MouseButtonRelease, to, to, to,
                             Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
            QCoreApplication::sendEvent (eq, &rel);

            const QPointF after = eq->nodePosition (1);
            std::fprintf (stderr, "[drag] node 2 after : %.1f,%.1f  gain=%.2f\n",
                          after.x(), after.y(), engine->typedParam ("pq2Gain")->value());
            std::fprintf (stderr, "[drag] pointer at   : %.1f,%.1f\n", to.x(), to.y());
            std::fprintf (stderr, "[drag] follow error : %.1f px\n",
                          std::hypot (after.x() - to.x(), after.y() - to.y()));

            // let one frame render, then dump it so the curve can be checked
            // against the node position
            QTimer::singleShot (200, [&] {
                if (const char* f = std::getenv ("ONYXTEON_DRAGSHOT"))
                    view.grabWindow().save (QString::fromUtf8 (f));
                app.quit();
            });
        });
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
            app.quit();
        });
    }

    return app.exec();
}
