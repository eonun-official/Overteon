#include "OvQmlEditor.h"

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

#if JUCE_LINUX
 #include <X11/Xlib.h>
 #include <X11/Xutil.h>
 #include <X11/extensions/shape.h>
#endif

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

   #if JUCE_LINUX
    // Some hosts wrap the plug-in widget in a socket and hand us a window that
    // is still unmapped (jalv.gtk3 does exactly that).  Qt will map *its* child
    // but the foreign ancestor stays hidden, so nothing is ever exposed.
    // Walk up from our own X11 window and map any ancestor that is unmapped.
    // QWindow::isExposed() reports true even when the window is buried under an
    // unmapped foreign ancestor, so the X map state has to be consulted
    // directly.  Returns true when our own window is actually viewable.
    bool ensureViewable (WId ownWindow, bool* repaired)
    {
        if (repaired != nullptr) *repaired = false;
        static Display* dpy = XOpenDisplay (nullptr);
        if (dpy == nullptr || ownWindow == 0) return false;

        bool mappedSomething = false;

        // reparenting to a new container can leave our own window unmapped
        {
            XWindowAttributes ownAttrs {};
            if (XGetWindowAttributes (dpy, (Window) ownWindow, &ownAttrs) != 0
                && ownAttrs.map_state == IsUnmapped)
            {
                XMapWindow (dpy, (Window) ownWindow);
                mappedSomething = true;
            }
        }

        // GTK's socket grab swallows button presses aimed at a foreign plug-in
        // window; we live inside the host process, so release it.
        {
            Window w2 = (Window) ownWindow;
            for (int i = 0; i < 6; ++i)
            {
                Window root = 0, parent = 0, *ch = nullptr;
                unsigned int n = 0;
                if (XQueryTree (dpy, w2, &root, &parent, &ch, &n) == 0) break;
                if (ch) XFree (ch);
                if (parent == 0 || parent == root) break;
                XUngrabButton (dpy, AnyButton, AnyModifier, parent);
                XUngrabPointer (dpy, CurrentTime);
                w2 = parent;
            }
            XUngrabButton (dpy, AnyButton, AnyModifier, (Window) ownWindow);
            XFlush (dpy);
        }

        Window w = (Window) ownWindow;
        for (int depth = 0; depth < 8; ++depth)
        {
            Window root = 0, parent = 0, *children = nullptr;
            unsigned int nChildren = 0;
            if (XQueryTree (dpy, w, &root, &parent, &children, &nChildren) == 0)
                break;
            if (children != nullptr) XFree (children);

            if (parent == 0 || parent == root) break;

            XWindowAttributes attrs {};
            if (XGetWindowAttributes (dpy, parent, &attrs) != 0)
            {
                if (attrs.map_state == IsUnmapped)
                {
                    XMapWindow (dpy, parent);
                    mappedSomething = true;
                }
                else if (attrs.map_state == IsUnviewable)
                {
                    // ancestor itself is buried: raise it so the chain shows
                    XRaiseWindow (dpy, parent);
                    mappedSomething = true;
                }
            }
            w = parent;
        }

        if (mappedSomething)
        {
            XFlush (dpy);
            XSync (dpy, False);
            if (repaired != nullptr) *repaired = true;
        }

        XWindowAttributes own {};
        const bool viewable = XGetWindowAttributes (dpy, (Window) ownWindow, &own) != 0
                              && own.map_state == IsViewable;

        return viewable;
    }
   #endif

   #if JUCE_LINUX
    // Prefer the peer JUCE created for the editor: input routing, clipping and
    // resizing all behave there.  Some hosts hand it over unmapped (jalv.gtk3
    // wraps the plug-in in a GtkSocket and maps it late), so map it here and
    // keep it mapped from the timer.  Only if the peer never becomes usable do
    // we fall back to the nearest viewable ancestor, covering the same rect.
    WId prepareContainer (WId peer, int* offsetX, int* offsetY)
    {
        *offsetX = 0;
        *offsetY = 0;

        static Display* dpy = XOpenDisplay (nullptr);
        if (dpy == nullptr) return peer;

        auto stateOf = [&] (::Window w) -> int
        {
            XWindowAttributes a {};
            if (XGetWindowAttributes (dpy, w, &a) == 0) return -1;
            return (int) a.map_state;
        };

        if (stateOf ((::Window) peer) == IsUnmapped)
        {
            XMapWindow (dpy, (::Window) peer);
            XFlush (dpy);
            XSync (dpy, False);
        }

        if (stateOf ((::Window) peer) == IsViewable)
            return peer;

        ::Window w = (::Window) peer;
        for (int depth = 0; depth < 8; ++depth)
        {
            ::Window root = 0, parent = 0, *children = nullptr;
            unsigned int nChildren = 0;
            if (XQueryTree (dpy, w, &root, &parent, &children, &nChildren) == 0)
                break;
            if (children != nullptr) XFree (children);
            if (parent == 0 || parent == root) break;

            if (stateOf (parent) == IsViewable)
            {
                ::Window child = 0;
                int x = 0, y = 0;
                if (XTranslateCoordinates (dpy, (::Window) peer, parent, 0, 0, &x, &y, &child) != 0)
                {
                    *offsetX = x;
                    *offsetY = y;
                }
                return (WId) parent;
            }
            w = parent;
        }
        return peer;
    }
   #endif

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
    void*         containerId = nullptr;
    bool          qtOk = false;
    bool          failed = false;
};

// A QWindow that is parented to a FOREIGN window is sized in Qt LOGICAL
// pixels, while JUCE's peer and every XGetWindowAttributes() report DEVICE
// pixels.  Passing device pixels to setGeometry() made Qt apply the desktop
// scale a second time: a 1354x1005 editor became 1763x1309 on a 1.302 display,
// which is both wrong and large enough to push the window out of the host's
// container (X then reported IsUnviewable and nothing was ever drawn).
static inline double ovQtScale()
{
    if (auto* scr = QGuiApplication::primaryScreen())
    {
        const double s = scr->devicePixelRatio();
        if (s > 0.1) return s;
    }
    return 1.0;
}

static inline int ovDeviceToLogical (int v)
{
    return (int) std::lround (v / ovQtScale());
}

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
    }
    else
    {
        impl->failed = true;
    }

    startTimerHz (OvTiming::kFrameHz);
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
        g.drawFittedText ("OnyxTEON: the Qt6/QML UI could not be started.\n"
                          "Check that the Qt xcb platform plugin is installed.",
                          getLocalBounds().reduced (24), juce::Justification::centred, 3);
    }
}

void OvQmlEditor::resized()
{
    if (impl && impl->view != nullptr && attached)
        impl->view->setGeometry (0, 0, ovDeviceToLogical (getWidth()),
                                 ovDeviceToLogical (getHeight()));
}

void OvQmlEditor::parentHierarchyChanged()
{
    attachToHostWindow();
}

void OvQmlEditor::attachToHostWindow()
{
    const bool dbg = std::getenv ("ONYXTEON_ATTACHDEBUG") != nullptr;
    if (! impl || impl->view == nullptr || attached)
    {
        if (dbg) std::fprintf (stderr, "[attach] skip impl=%p view=%p attached=%d\n",
                               (void*) impl.get(), impl ? (void*) impl->view : nullptr,
                               (int) attached);
        return;
    }

    // diagnostic switch: keep the QML view detached so the host's own window
    // handling can be observed on its own
    if (std::getenv ("ONYXTEON_NO_ATTACH") != nullptr) return;

    auto* top = getTopLevelComponent();
    if (top == nullptr || top->getPeer() == nullptr)
    {
        if (dbg) std::fprintf (stderr, "[attach] no top=%p peer=%p\n", (void*) top,
                               top ? (void*) top->getPeer() : nullptr);
        return;
    }

    void* native = top->getPeer()->getNativeHandle();
    if (dbg) std::fprintf (stderr, "[attach] top %dx%d native=%p\n",
                           top->getWidth(), top->getHeight(), native);
    if (native == nullptr) return;

    WId containerId = (WId) (quintptr) native;
    int offX = 0, offY = 0;

   #if JUCE_LINUX
    containerId = prepareContainer (containerId, &offX, &offY);
   #endif

    impl->containerId = (void*) containerId;
    impl->container = QWindow::fromWinId (containerId);
    if (impl->container == nullptr) return;

    impl->view->setParent (impl->container);
    // JUCE's top->getWidth() is device pixels; Qt wants logical.  See ovQtScale.
    impl->view->setGeometry (offX, offY, ovDeviceToLogical (top->getWidth()),
                             ovDeviceToLogical (top->getHeight()));

    // The editor must never be larger than the area the host gives us: jalv
    // hands out a widget window that is smaller than the 840x624 default, so
    // ask the host for a size that actually fits (setSize -> LV2 ui:resize).
    fitToHost (containerId);
    impl->view->show();
    impl->view->raise();
    if (dbg) std::fprintf (stderr, "[attach] container %lx geom %dx%d shown\n",
                           (unsigned long) containerId,
                           impl->view->width(), impl->view->height());

   #if JUCE_LINUX
    // Empty input shape: pointer events pass through to the JUCE window
    // underneath, which forwards them into the QML scene (see forwardMouse).
    if (std::getenv ("ONYXTEON_CLEAR_INPUT") != nullptr)
        if (auto* dpy = XOpenDisplay (nullptr))
        {
            XShapeCombineRectangles (dpy, (::Window) (quintptr) impl->view->winId(),
                                     ShapeInput, 0, 0, nullptr, 0, ShapeSet, Unsorted);
            XFlush (dpy);
        }
   #endif

    attached = true;
    attachedXid = native;
    ticksSinceAttach = 0;
}

void OvQmlEditor::forwardMouse (const juce::MouseEvent& e, int qtType)
{
    if (! impl || impl->view == nullptr) return;

    const QPointF local ((qreal) e.position.x, (qreal) e.position.y);
    const QPointF global ((qreal) e.getScreenX(), (qreal) e.getScreenY());

    Qt::MouseButtons buttons = Qt::NoButton;
    if (e.mods.isLeftButtonDown())   buttons |= Qt::LeftButton;
    if (e.mods.isRightButtonDown())  buttons |= Qt::RightButton;
    if (e.mods.isMiddleButtonDown()) buttons |= Qt::MiddleButton;

    Qt::MouseButton button = Qt::NoButton;
    if (qtType == QEvent::MouseButtonPress || qtType == QEvent::MouseButtonRelease)
        button = e.mods.isRightButtonDown() ? Qt::RightButton
               : e.mods.isMiddleButtonDown() ? Qt::MiddleButton : Qt::LeftButton;

    Qt::KeyboardModifiers mods = Qt::NoModifier;
    if (e.mods.isShiftDown()) mods |= Qt::ShiftModifier;
    if (e.mods.isCtrlDown())  mods |= Qt::ControlModifier;
    if (e.mods.isAltDown())   mods |= Qt::AltModifier;

    QMouseEvent ev ((QEvent::Type) qtType, local, local, global,
                    button, buttons, mods);
    QCoreApplication::sendEvent (impl->view, &ev);
}

void OvQmlEditor::mouseMove (const juce::MouseEvent& e)  { forwardMouse (e, QEvent::MouseMove); }
void OvQmlEditor::mouseDown (const juce::MouseEvent& e)  { forwardMouse (e, QEvent::MouseButtonPress); }
void OvQmlEditor::mouseDrag (const juce::MouseEvent& e)  { forwardMouse (e, QEvent::MouseMove); }
void OvQmlEditor::mouseUp   (const juce::MouseEvent& e)  { forwardMouse (e, QEvent::MouseButtonRelease); }

void OvQmlEditor::forwardWheel (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (! impl || impl->view == nullptr) return;

    const QPointF local ((qreal) e.position.x, (qreal) e.position.y);
    const QPointF global ((qreal) e.getScreenX(), (qreal) e.getScreenY());
    const QPoint angle ((int) (w.deltaX * 120.0f), (int) (w.deltaY * 120.0f));
    QWheelEvent ev (local, global, QPoint(), angle, Qt::NoButton, Qt::NoModifier,
                    Qt::NoScrollPhase, false);
    QCoreApplication::sendEvent (impl->view, &ev);
}

void OvQmlEditor::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    forwardWheel (e, w);
}

// ---------------------------------------------------------------------------
//  Host window sizing.
//
//  The design is 1040x772 CSS pixels, and a CSS pixel is a LOGICAL pixel, so
//  on a 1.302 display the editor has to be 1354x1005 DEVICE pixels.  The old
//  version did the opposite: it measured the host's container in device pixels
//  and asked for min(container/design), i.e. it could only ever shrink, and it
//  never grew with the desktop scale — "the window does not get bigger when
//  Overteon follows the system DPI".
//
//  Worse, asking the host for a size makes the host replace our peer window,
//  which the timer reads as "new container" and answers with detach+attach.
//  Shrinking every tick therefore also flashed every tick, and the two bugs
//  fed each other.  So the target is now computed from the DESKTOP SCALE alone
//  — never from the container — and requested once.  The container then simply
//  follows the host window, and nothing re-enters.
// ---------------------------------------------------------------------------
juce::Point<int> OvQmlEditor::targetSize() const
{
    // Qt is the only part of this program that knows the desktop scale: JUCE
    // reports 1.0 on the same screen (measured), because it reads its own
    // display list and no Xft.dpi.  The QML scene already uses this number for
    // its own device-pixel work, so taking it from the same place keeps the
    // editor and the scene consistent.
    double dpr = 1.0;
    if (auto* scr = QGuiApplication::primaryScreen())
        dpr = scr->devicePixelRatio();

    int w = (int) std::lround (OvThemeWidth  * dpr);
    int h = (int) std::lround (OvThemeHeight * dpr);

    // Never ask for more than the screen can hold, and never less than a size
    // that the layout can still be read at.
    if (auto* scr = QGuiApplication::primaryScreen())
    {
        const QSize avail = scr->availableGeometry().size() * dpr;
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

void OvQmlEditor::fitToHost (WId containerId)
{
    juce::ignoreUnused (containerId);

    const auto want = targetSize();

    if (std::abs (want.x - getWidth()) <= 2 && std::abs (want.y - getHeight()) <= 2)
        return;

    // ONE request.  If the host refuses, its container stays where it is and we
    // simply fill that; asking again every tick is what caused the flashing.
    if (want == lastRequested) return;
    lastRequested = want;
    setSize (want.x, want.y);
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
    static int tickCount = 0;
    ++tickCount;

    if (! attached)
    {
        ++attachAttempts;
        const auto now = std::chrono::steady_clock::now();
        if (attachDeadline == std::chrono::steady_clock::time_point())
            attachDeadline = now + std::chrono::seconds (60);
        if (now < attachDeadline)
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
            else
            {
                ++ticksSinceAttach;

                if (! impl->view->isVisible())
                {
                    impl->view->show();
                    impl->view->raise();
                }

                // The host may hand us the window before it is mapped (GtkSocket
                // wraps the widget), leaving our view attached but never
                // exposed.  Re-attach until Qt reports the surface as exposed.
               #if JUCE_LINUX
                // Keep the plug-in window actually on screen.  Hosts like
                // jalv.gtk3 hand us a socket child that may still be unmapped,
                // and they map it at arbitrary times.
                // follow host window resizes: only shrink, never fight the user
                if (tickCount % 10 == 0 && attachedXid != nullptr)
                {
                   #if JUCE_LINUX
                    fitToHost ((WId) (quintptr) impl->containerId);
                   #endif
                }

                if (ticksSinceAttach > 1)
                {
                    bool repaired = false;
                    const bool viewable = ensureViewable ((WId) (quintptr) impl->view->winId(), &repaired);

                    // If the window had been buried, Qt's render loop never got
                    // an expose event; bounce it so the scenegraph starts.
                    if (repaired)
                    {
                        impl->view->hide();
                        impl->view->show();
                        impl->view->raise();
                        impl->view->requestUpdate();
                    }

                    if (! viewable && ticksSinceAttach > 150)
                    {
                        detach();
                        attachToHostWindow();
                    }
                }
               #endif
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
                impl->view->setGeometry (0, 0, ovDeviceToLogical (w),
                                         ovDeviceToLogical (h));
        }
    }

    // Qt has no event loop of its own inside the plugin: it is driven from the
    // JUCE timer, which the host pumps through the LV2 idle interface.
    //
    // The host decides how often that happens, and it is NOT the rate we ask
    // for.  jalv idles at its own update rate — 15 Hz measured — so a fixed
    // 4 ms of processEvents per call gave Qt 15 x 4 = 60 ms out of every
    // second: 6 % of the time.  Qt's timers then fire in bursts, the meters
    // and the spectrum appear and disappear, and the scene renders a few
    // irregular frames.  That is the "flickering and very laggy" report.
    //
    // So the budget is a FRACTION OF THE TIME THAT ACTUALLY ELAPSED since the
    // last call.  Half of it goes to Qt, whatever cadence the host uses: 15 Hz
    // gives ~33 ms per second each way, a 120 Hz pump gives the same share of a
    // much smaller slice.  The cap stops a long stall (a resize, a first paint)
    // from locking the audio thread out of the message queue.
    if (QCoreApplication::instance() != nullptr)
    {
        using clk = std::chrono::steady_clock;
        static auto last = clk::now();
        const auto now = clk::now();
        const double elapsedMs = std::chrono::duration<double, std::milli> (now - last).count();
        last = now;

        // A/B switch while bisecting the flashing: ONYXTEON_PUMP=<ms> forces a
        // fixed budget, the default is the adaptive fraction.
        const int budget = std::getenv ("ONYXTEON_PUMP")
            ? std::atoi (std::getenv ("ONYXTEON_PUMP"))
            : (int) juce::jlimit (4.0, 20.0, elapsedMs * 0.5);
        QCoreApplication::processEvents (QEventLoop::AllEvents, budget);

        // Qt applies the host's UnmapNotify to our QWindow, so a longer pump
        // ends with the editor hidden: measured, the window sat at Map State
        // IsUnMapped while `view->isVisible()` still said true, and nothing was
        // ever drawn.  Mapping it again here, after the events are drained, is
        // the only place that wins the race — the earlier attempt in the timer
        // body ran before Qt had seen the unmap.
       #if JUCE_LINUX
        if (attached && impl->view != nullptr && impl->view->isVisible())
        {
            WId own = (WId) (quintptr) impl->view->winId();
            if (own != 0)
            {
                static Display* dpy = XOpenDisplay (nullptr);
                XWindowAttributes a {};
                if (dpy != nullptr && XGetWindowAttributes (dpy, (::Window) own, &a) != 0
                    && a.map_state == IsUnmapped)
                {
                    XMapWindow (dpy, (::Window) own);
                    XFlush (dpy);
                }
            }
        }
       #endif
    }
}
