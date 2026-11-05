#include "OvQmlEditor.h"

#include "FxProcessor.h"
#include "OvEngine.h"
#include "OvJuceSource.h"
#include "OvQmlTypes.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <QGuiApplication>
#include <QQuickView>
#include <QQuickWindow>
#include <QScreen>
#include <QTimer>
#include <QWindow>
#include <QAbstractEventDispatcher>
#include <QFontDatabase>
#include <QFileInfo>
#include <algorithm>
#include <cmath>
#include <cstdio>

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

        // Our own event loop never runs (the host owns the thread), so pump
        // with processEvents(); the basic render loop is the safe pairing.
        qputenv ("QSG_RENDER_LOOP", "basic");
        qputenv ("QT_QUICK_BACKEND", "opengl");
        QQuickWindow::setGraphicsApi (QSGRendererInterface::OpenGL);

        static int argc = 1;
        static char arg0[] = "Overteon";
        static char* argv[] = { arg0, nullptr };
        new QGuiApplication (argc, argv);

        for (const char* f : { ":/res/Inter-Regular.ttf", ":/res/Inter-Medium.ttf",
                               ":/res/Inter-SemiBold.ttf", ":/res/Inter-Bold.ttf",
                               ":/res/edix.otf" })
            QFontDatabase::addApplicationFont (QString::fromLatin1 (f));

        ok = true;
        return ok;
    }

    QString x11Display()
    {
        if (const char* d = std::getenv ("DISPLAY")) return QString::fromLocal8Bit (d);
        return {};
    }
}

struct OvQmlEditor::Impl
{
    std::unique_ptr<OvJuceSource> source;
    std::unique_ptr<OvEngine>     engine;
    QQuickView*   view = nullptr;
    QWindow*      container = nullptr;
    bool          qtOk = false;
    bool          failed = false;
};

OvQmlEditor::OvQmlEditor (InjectorProcessor& p)
    : juce::AudioProcessorEditor (p), proc (p)
{
    impl = std::make_unique<Impl>();
    setOpaque (true);
    setSize (840, 624);

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
    }
    else
    {
        impl->failed = true;
    }

    startTimerHz (30);
}

OvQmlEditor::~OvQmlEditor()
{
    detach();
    if (impl && impl->view != nullptr)
    {
        impl->view->hide();
        delete impl->view;
        impl->view = nullptr;
    }
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
    if (impl && impl->view != nullptr && attached)
        impl->view->setGeometry (0, 0, getWidth(), getHeight());
}

void OvQmlEditor::parentHierarchyChanged()
{
    attachToHostWindow();
}

void OvQmlEditor::attachToHostWindow()
{
    if (! impl || impl->view == nullptr || attached) return;

    auto* top = getTopLevelComponent();
    if (top == nullptr || top->getPeer() == nullptr) return;

    void* native = top->getPeer()->getNativeHandle();
    if (native == nullptr) return;

    const auto xid = (WId) (quintptr) native;

    impl->container = QWindow::fromWinId (xid);
    if (impl->container == nullptr) return;

    impl->view->setParent (impl->container);
    impl->view->setGeometry (0, 0, top->getWidth(), top->getHeight());
    impl->view->show();
    impl->view->raise();
    attached = true;
    attachedXid = native;
}

void OvQmlEditor::detach()
{
    if (impl && impl->view != nullptr && attached)
    {
        impl->view->hide();
        impl->view->setParent (nullptr);
    }
    attached = false;
}

void OvQmlEditor::timerCallback()
{
    if (! impl) return;

    if (! attached && attachAttempts < 600)
    {
        ++attachAttempts;
        attachToHostWindow();
    }
    else if (attached)
    {
        // The host may hand us its window before it is mapped (jalv.gtk3 wraps
        // the widget in a GtkSocket), or replace the peer when the editor is
        // shown/hidden.  Keep the child mapped and follow peer changes.
        auto* top = getTopLevelComponent();
        if (top != nullptr && top->getPeer() != nullptr)
        {
            void* native = top->getPeer()->getNativeHandle();
            if (native != nullptr && native != attachedXid)
            {
                detach();
                attachToHostWindow();
            }
            else if (! impl->view->isVisible())
            {
                impl->view->show();
                impl->view->raise();
            }
        }
    }

    if (attached && impl->container != nullptr)
    {
        auto* top = getTopLevelComponent();
        if (top != nullptr && top->getPeer() != nullptr)
        {
            const int w = top->getPeer()->getBounds().getWidth();
            const int h = top->getPeer()->getBounds().getHeight();
            if (impl->view->width() != w || impl->view->height() != h)
                impl->view->setGeometry (0, 0, w, h);
        }
    }

    // Qt has no event loop of its own inside the plugin: drive it from the
    // JUCE timer, which the LV2 idle callback already pumps.
    if (QCoreApplication::instance() != nullptr)
        QCoreApplication::processEvents (QEventLoop::AllEvents, 4);
}
