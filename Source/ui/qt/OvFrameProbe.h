#pragma once
// ============================================================
//  OvFrameProbe — how many frames a WIDGET was actually rendered in.
//
//  OVERTEON_SOLO needs one number per widget, and a window frame rate cannot
//  give it: the window presents whether or not a given widget redrew.  The
//  painted widgets can count inside their own paint(); a QML widget made of
//  ShaderEffects has no such callback at all.
//
//  This is that callback.  It is a zero-size item with ItemHasContents, so the
//  scene graph calls updatePaintNode() for it on every frame in which its
//  subtree is rendered — the widget's own frames, taken from the renderer
//  rather than inferred.
//
//  updatePaintNode() runs on the RENDER thread.  The count is published to the
//  GUI thread once a second through a queued call, which is also where the QML
//  property is read.
// ============================================================
#include <QQuickItem>
#include <QTimer>

class OvFrameProbe : public QQuickItem
{
    Q_OBJECT
    Q_PROPERTY(double fps READ fps NOTIFY fpsChanged)

public:
    explicit OvFrameProbe (QQuickItem* parent = nullptr);

    double fps() const { return m_fps; }

protected:
    QSGNode* updatePaintNode (QSGNode* old, UpdatePaintNodeData*) override;

signals:
    void fpsChanged();

private:
    void publish();

    quint64 m_frames = 0;          // render thread
    double  m_fps = 0.0;
    QTimer  m_publish;
};
