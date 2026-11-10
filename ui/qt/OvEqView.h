#pragma once
#include "OvEnv.h"
// ============================================================
//  OnyxTEON Qt/QML UI — EQ plot.
//
//  A direct port of the canvas EQ engine in web_fx/index.html:
//  same log frequency axis (5 Hz..24 kHz), same RBJ coefficient maths,
//  same colours, same node geometry.
//
//  The frosted glass is painted here as well, exactly like the original's
//  FrostEngine: the plot is rendered into an image, that image is run through
//  a dual-Kawase pyramid, and the result is drawn clipped to the control-pit
//  trapezoid (with the CSS veil and the elliptical accent glow).  Doing it in
//  the painter rather than with a ShaderEffectSource keeps it deterministic —
//  sampling a QQuickPaintedItem through a texture gives garbage.
// ============================================================
#include <QQuickFramebufferObject>
#include <QTimer>
#include <QString>
#include <QImage>
#include <vector>
#include <QVector>
#include <QSGGeometryNode>
#include <QSGTexture>
#include <QSGImageNode>
#include <QColor>
#include <chrono>
#include <QPainterPath>

#include "OvBallistics.h"   // ovb::Fall / ovb::catmullRomLimited
#include "OvSpectrum.h"     // ovspec::SpectrumBallistics

class OvEngine;

//  GPU-backed.
//
//  This was a QQuickPaintedItem: every frame it rasterised the whole plot into
//  a QImage on the CPU — grid, spectrum path, five curves, two gradients — and
//  then uploaded 2.4 MB of it as a texture.  Measured at 1.4 ms of CPU per plot
//  per frame, 2 plots, 108 fps: about 30 % of a core purely to draw two curves.
//
//  A QQuickFramebufferObject fixes that without rewriting the drawing: Qt 6
//  still ships QOpenGL2PaintEngineEx, so a QPainter on a QOpenGLPaintDevice
//  tessellates paths, gradients and glyphs into GL commands.  The drawing code
//  is unchanged and the picture is identical; what changes is that the
//  rasterisation happens on the GPU and nothing is uploaded per frame.
//
//  (Qt's RHI path has no QPainter backend, so "the same QPainter code on the
//  GPU" is only available through GL.  The rest of the UI — the glass, the fog
//  — is ShaderEffect and therefore already RHI.)
class OvEqView : public QQuickItem
{
    Q_OBJECT

    Q_PROPERTY(OvEngine* engine READ engine WRITE setEngine NOTIFY engineChanged)
    Q_PROPERTY(QString prefix READ prefix WRITE setPrefix NOTIFY prefixChanged)
    Q_PROPERTY(QString accent READ accent WRITE setAccent NOTIFY accentChanged)
    Q_PROPERTY(bool headerTop READ headerTop WRITE setHeaderTop NOTIFY headerTopChanged)
    Q_PROPERTY(double glow READ glow WRITE setGlow NOTIFY glowChanged)

public:
    explicit OvEqView (QQuickItem* parent = nullptr);

    OvEngine* engine() const { return m_engine; }
    void setEngine (OvEngine*);

    QString prefix() const { return m_prefix; }

    /// Paint the outgoing curve — the part past the plot edge — as plain
    /// strokes.  No blur and no fading: OvFogSource paints this into a texture
    /// and fogfx.frag applies the distance-dependent blur and alpha on the GPU.
    /// `origin` is where this view's (0,0) lands in the target's coordinates.
    void paintOverflowFog (QPainter* g, const QPointF& origin);

    // ---- RHI accessors ---------------------------------------------------
    //
    // The plot is four layers, and only ONE of them changes per frame:
    //
    //   background  grid + ruler labels        cached on a key
    //   spectrum    the analyser fill          rebuilt every frame
    //   glow bar    the control pit's bloom    cached on the smoothed levels
    //   overlay     curves, nodes, digits      cached on a key
    //
    // So the render path is four textures' worth of quads plus one triangle
    // fan, and QPainter only runs when a cached layer's key changes.  Nothing
    // here needs OpenGL: the node is plain QSGGeometry + QSGSimpleTextureNode,
    // which Qt renders on whatever RHI backend it is using.
    struct Layers
    {
        QImage background;   ///< null when unchanged since `bgKey`
        QString bgKey;
        QImage overlay;      ///< null when unchanged since `overlayKey`
        QString overlayKey;
        QImage glow;         ///< null when unchanged since `glowKey`
        quint64 glowKey = 0;
        /// The analyser outline, already flattened to a polyline, and the
        /// baseline it fills down to.  Both in item coordinates.
        QVector<QPointF> spectrum;
        double spectrumBottom = 0.0;
        double spectrumTop    = 0.0;   // the gradient's other end
        QColor accent;
    };
    /// Everything the render node needs for this frame.  Builds whichever
    /// cached layers are stale and leaves the rest null.
    void buildLayers (Layers& out);
    /// Advance the analyser ballistics and write the smoothed levels.
    /// Called once per frame by whichever path is drawing.
    void advanceSpectrum (float* spec, float* specRaw);

    /// One straight run of the outgoing curve, in this view's coordinates.
    struct FogSegment { QPointF a, b; QColor color; float width; };

    /// The outgoing curve as line SEGMENTS rather than a painted stroke.
    ///
    /// This is the RHI path: QSGGeometry takes triangles, so the caller turns
    /// these into quads and picks whatever backend Qt is running — GL, D3D11,
    /// Vulkan, Metal.  Going through QPainter would have meant
    /// QQuickFramebufferObject + QOpenGLPaintDevice, and that is OpenGL only.
    ///
    /// `origin` shifts the segments into the target's coordinates, exactly as
    /// paintOverflowFog() does for a painter.
    void fogSegments (const QPointF& origin, QVector<FogSegment>& out);

    /// Where the fog starts and which way it goes, in this view's pixels.
    /// Q_PROPERTYs so QML can place the fog surface without duplicating the
    /// plot layout (fogEdge is plot().bottom() or plot().top(), which depends
    /// on the padding rule).
    Q_PROPERTY(double fogEdge  READ fogEdge  NOTIFY fogGeometryChanged)
    Q_PROPERTY(double fogDir   READ fogDir   NOTIFY fogGeometryChanged)
    Q_PROPERTY(double fogRange READ fogRange NOTIFY fogGeometryChanged)
    double fogEdge() const;
    double fogDir() const;
    double fogRange() const;
    void setPrefix (const QString&);

    /// How much this view is enlarged on its way to the window: 1.0 at the
    /// design size, 2.0 when the window is twice as large.  The overflow fog
    /// divides by it, because the fog's reach and blur profile are a fixed
    /// treatment rather than geometry that belongs to the design.
    double displayScale() const;

    /// Everything the overflow fog's strokes are drawn from, as one number: the
    /// five bands, the dB range, which half this is.  The fog rebuilds only when
    /// this changes, so a repaint request that would produce the same picture
    /// costs one comparison instead of a full path build and a repaint.
    quint64 curveSignature() const;

    QString accent() const { return m_accent; }
    void setAccent (const QString&);

    bool headerTop() const { return m_headerTop; }
    void setHeaderTop (bool);

    /// 0..1 accent glow inside the trapezoid, driven by the meter level of
    /// this half of the signal path (.controlpit::before).
    double glow() const { return m_glow; }
    void setGlow (double);

    void paint (QPainter*);

protected:
    /// The RHI path: four layers as scene-graph nodes.  Three of them are cached
    /// images uploaded as textures, and only the spectrum is rebuilt per frame —
    /// as a triangle strip with vertex colours, so the gradient costs nothing.
    QSGNode* updatePaintNode (QSGNode* old, UpdatePaintNodeData*) override;

public:
    /// Build contentImage()/unfrostedImage() as well.
    ///
    /// Only the CPU seam glass needs them, and on this path producing them
    /// means reading the framebuffer back to the CPU — the very cost this class
    /// no longer pays.  QML sets this when it falls back to OvSeamFrost.
    /// Frames this plot actually painted in the last second, measured inside
    /// paint() itself.  ONYXTEON_SOLO shows it, so the number describes THIS
    /// widget rather than the window it happens to be in.
    Q_PROPERTY(double paintFps READ paintFps NOTIFY paintFpsChanged)
    double paintFps() const { return m_paintFps; }

    Q_PROPERTY(bool wantImage READ wantImage WRITE setWantImage NOTIFY wantImageChanged)
    bool wantImage() const { return m_wantImage; }
    void setWantImage (bool on);

    /// Centre of band `index` (0..4) in item coordinates, after refreshing
    /// the geometry from the current parameter values.
    Q_INVOKABLE QPointF nodePosition (int index);

    /// Debug HUD support (--hud).  Given a point in this item's own
    /// coordinates, report where every node really is and which one a click
    /// there would grab.  One line per node plus a verdict, so a human can see
    /// for themselves that the hit-test and the drawing agree.
    Q_INVOKABLE QString debugHit (const QPointF& pos) const;

    /// Node centres as "x,y" pairs, for the HUD.
    Q_INVOKABLE QString debugNodes() const;

    /// The dB a parameter value is shown at: the parameter stores value/mult
    /// and the screen shows value, exactly as mult() does in web_fx.  The
    /// numeric editor needs it to display and to write back a gain in dB.
    Q_INVOKABLE double gainDisplayMult() const { return displayMult(); }

    /// True when this band's type has no gain control (cuts and notches), so
    /// the editor greys that field out instead of writing a value nothing
    /// reads.  typeHasGain() in web_fx.
    Q_INVOKABLE static bool typeHasGain (int type) { return type == 0 || type == 2 || type == 3; }

    /// Shelves ignore Q in this design; everything else uses it
    /// (typeHasQ() in web_fx).
    Q_INVOKABLE static bool typeHasQ (int type) { return type != 2 && type != 3; }

    /// Which band the numeric editor is bound to, or -1 for none.
    ///
    /// The editor widget itself belongs to the page, but two of the three ways
    /// it closes are plot gestures — web_fx dismisses it when empty canvas is
    /// pressed, re-binds it when another node is pressed, and closes it when
    /// the pointer walks more than 30 px from the node — and those arrive
    /// here.  The page sets this when it opens the editor so the plot can
    /// apply them.
    Q_INVOKABLE void setEditingBand (int band) { m_editBand = band; }

    /// True while a field of that editor is being scrubbed.
    ///
    /// Scrubbing freq or gain moves the NODE, and the editor follows it — so
    /// the pointer, which stays where the user's hand is, ends up outside a box
    /// that has walked away, and the 30 px rule would close the editor mid
    /// gesture.  The page has the same shape of problem and answers it by
    /// testing `if (drag)` first; this is the same idea for the numeric fields.
    Q_INVOKABLE void setEditingScrub (bool on) { m_editScrub = on; }

    /// True while the pointer is over the numeric editor's own box.
    ///
    /// The page never has to say this: its editor swallows pointer events and
    /// runs pointerleave to dismiss itself, so a pointer sitting on the box
    /// simply never reaches the canvas's move handler.  Here the box is a QML
    /// sibling of this item, so hover events keep arriving while the pointer is
    /// on it — and the 30 px "walked away" radius around the node is SMALLER
    /// than the box, so reaching for a field (40-60 px out) dismissed the
    /// editor that was being reached for.  With this set, that rule is skipped
    /// and the box's own pointerleave decides instead.
    Q_INVOKABLE void setEditingPointerInside (bool on) { m_editPointerInside = on; }

    /// Repaint now, because a parameter was written from outside a plot gesture.
    ///
    /// A drag on the plot calls update() itself at the end of every mouse move,
    /// which is why dragging a node feels immediate.  The numeric editor writes
    /// the same parameters but is not a mouse move on this item, so without this
    /// the curve and the node only caught up on the next repaint timer tick —
    /// 30 Hz at best, and nothing at all while the spectrum is idle.  Measured
    /// symptom: scrubbing Freq moves the node in the editor's own readout but
    /// the plot underneath does not move.
    ///
    /// m_dirty is set as well as update(): the drag path does the same, and it
    /// is what keeps a spectrum tick from swallowing the change.
    Q_INVOKABLE void repaintForParamEdit()
    {
        m_dirty = true;
        update();
    }

    /// The last rendered plot image; OvSeamFrost blurs it together with the
    /// other half's.  Null until the view has painted once.
    QImage contentImage() const { return m_content; }

    /// The plot without this half's glass on it.  This is what the seam glass
    /// samples: the page's FrostEngine reads the raw canvases, so feeding it a
    /// picture that already contains the control-pit frost would apply the
    /// glass twice.
    QImage unfrostedImage() const { return m_unfrosted; }

    /// Emitted right after m_content has been re-rendered.  OvSeamFrost listens
    /// to this: without it the seam glass never learns that the plots changed,
    /// so it paints once while m_content is still null and then sits blank
    /// forever.  (Measured: 0 px of the seam strip differed between a run with
    /// the glass and one with ONYXTEON_NOFROST, while the control pits — which
    /// repaint from their own paint() — differed by 15936 px.)

    /// Two-pass separable Gaussian (the "dual Gaussian": one horizontal, one
    /// vertical convolution), sigma in item pixels, plus CSS saturate().
    /// Shared with OvSeamFrost so both glass surfaces match.
    static QImage gaussianBlur (const QImage& src, double sigma, double sat);

    // interaction
    void mousePressEvent (QMouseEvent*) override;
    void mouseMoveEvent (QMouseEvent*) override;
    void mouseReleaseEvent (QMouseEvent*) override;
    void mouseUngrabEvent() override;
    void mouseDoubleClickEvent (QMouseEvent*) override;
    void wheelEvent (QWheelEvent*) override;
    void hoverMoveEvent (QHoverEvent*) override;
    void hoverLeaveEvent (QHoverEvent*) override;

signals:
    /// Emitted when the plot geometry moved, so the fog surface can follow.
    void fogGeometryChanged();
    /// m_content was just re-rendered; the seam glass has to rebuild.
    void contentChanged();
    void wantImageChanged();
    void paintFpsChanged();
    void engineChanged();
    void prefixChanged();
    void accentChanged();
    void headerTopChanged();
    void glowChanged();
    void menuRequested (int band, QPointF pos, QString currentType);
    void hoverInfo (QString text, QPointF pos);
    void hoverEnded();

    /// A node was double-clicked: show the numeric editor for `band` (0..4).
    /// The page keeps it glued to the node until it is dismissed — same as
    /// openEdit() in web_fx/index.html.
    void nodeEditRequested (int band);
    /// A double-click on empty canvas needed a new node and the pool was full.
    /// The page shows "ALL 5 NODES IN USE — REMOVE ONE FIRST" for 1.4 s.
    void poolExhausted();
    /// A plot gesture dismissed the numeric editor (empty-canvas press, or the
    /// pointer walking more than 30 px from the node it is bound to).
    void nodeEditClosed();

private:
public:
    struct Band
    {
        double freq = 1000.0, gain = 0.0, q = 0.707;
        int    type = 0;
        double x = 0.0, y = 0.0;
        bool   active = false;
    };
private:

    double  valueOf (const QString& id, double fallback) const;
    QString textOf (const QString& id) const;
    void    setValueOf (const QString& id, double v);
    void    setNormOf (const QString& id, double n);
    double  normOf (const QString& id) const;

    void gatherBands();
    void layoutGeometry (QRectF& plot, double& rangeDb) const;
    double toX (double hz, double w) const;
    double fromX (double x, double w) const;
    double toY (double db, double h, double rangeDb, double top, double bottom) const;
    double fromY (double y, double h, double rangeDb, double top, double bottom) const;

    static double bandMagDb (int type, double fs, double f, double q, double dB, double at);
    double bandMagAt (const Band&, double atHz) const;
    double totalMagAt (double atHz) const;
    double displayMult() const;

    void refreshNodePositions();
    /// Finish and forget the current node drag (idempotent).
    void endDrag();
    double plotRangeDb() const;
    double inactiveOpacity() const;
    int nodeAt (const QPointF& pos) const;

    // ---- painting helpers ------------------------------------------
    void paintContent (QPainter* g, double w, double h,
                       double top, double bottom, const QColor& accent);
    /// The glow bar in the control pit: per-frequency brightness across the
    /// trapezoid, low frequencies on the left.  Drawn UNDER the glass, so the
    /// frosted layer blooms it.
    void paintGlowBar (QPainter* g, double w, double h,
                       const QColor& accent, const float* spec);
    /// Build (or reuse) the glow bar's cached image; `dest` is where it goes.
    /// Split out of paintGlowBar so the render node can take the pixels without
    /// a QPainter.
    bool ensureGlowBar (double w, double h, const QColor& accent,
                        const float* spec, QRectF& dest);
    QRectF m_glowDest;

    // ---- scene-graph state ------------------------------------------------
    // The node tree is fixed after the first frame; only the contents of the
    // spectrum node and the textures change.
    QSGNode*         m_sgRoot    = nullptr;
    QSGGeometryNode* m_sgSpectrum = nullptr;
    QSGImageNode*    m_sgBg      = nullptr;
    QSGImageNode*    m_sgGlow    = nullptr;
    QSGImageNode*    m_sgOverlay = nullptr;
    QString m_texKeyBg, m_texKeyGlow, m_texKeyOv;
    // The images the textures are uploaded from.  They must outlive the upload,
    // which happens later in the render pass, so they are kept here.
    QImage  m_texImgBg, m_texImgGlow, m_texImgOv;
    // Textures replaced this frame, freed at the START of the next one.
    //
    // QSGImageNode::setTexture() does NOT take ownership, so every replaced
    // texture has to be deleted by hand or it leaks — and the old one cannot be
    // freed immediately, because the previous frame's batch may still be in
    // flight.  One frame of grace is the standard remedy.
    QVector<QSGTexture*> m_texRetired;
    bool    m_spAttached = true;
    /// Blit the cached glow image through the trapezoid clip.
    void drawGlowImage (QPainter* g, double pitX, double barY,
                        double pitW, double barH);
    /// Paint the frosted strip for this half of the EQ.
    void paintFrost (QPainter* g, const QImage& blurred,
                     double w, double h, const QColor& accent);

    OvEngine* m_engine = nullptr;
    QString   m_prefix = QStringLiteral ("sc");
    QString   m_accent = QStringLiteral ("#ff5c8a");
    bool      m_headerTop = true;
    double    m_glow = 0.0;

    Band   m_bands[5];

    // ---- total-curve sample cache ---------------------------------------
    //  Drawing the curve needs totalMagAt() at every x, and so does the
    //  overflow fog: 1040 samples x 5 biquads, twice, per repaint.  The curve
    //  only changes when a band parameter does, while the spectrum changes
    //  every frame — so at 120 Hz almost every repaint was recomputing a curve
    //  that had not moved.  This caches the samples against a key made of the
    //  parameters and the geometry, which is cheap to compare (several < on
    //  doubles) and immune to missing an invalidation.
    struct CurveKey
    {
        double p[5][4] {};        // freq, gain, q, type per band
        double range = 0.0, w = 0.0, h = 0.0, top = 0.0, bottom = 0.0;
        int    step = 1;
        bool operator== (const CurveKey& o) const
        {
            if (range != o.range || w != o.w || h != o.h
                || top != o.top || bottom != o.bottom || step != o.step) return false;
            for (int i = 0; i < 5; ++i)
                for (int k = 0; k < 4; ++k)
                    if (p[i][k] != o.p[i][k]) return false;
            return true;
        }
    };

    CurveKey                m_curveKey;
    bool                    m_curveValid = false;
    std::vector<double>     m_curveY;     // item-space y per sample
    int                     m_curveStep = 1;

    /// y of the total response at each x, sampled every `m_curveStep` px.
    /// Rebuilt only when the key changes; returns a reference so the curve
    /// and the fog can share one evaluation.
    const std::vector<double>& totalCurveY (double w, double h, double top,
                                            double bottom, int step);
    QRectF m_plot;
    double m_rangeDb = 12.0;

    int    m_dragBand = -1;
    bool   m_dragMovesGain = false;
    QPointF m_grabOffset;
    int    m_hoverBand = -1;

    /// Band the page's numeric editor is bound to (-1 = closed).  See
    /// setEditingBand(): the plot owns the dismissal gestures.
    int    m_editBand = -1;
    bool   m_editScrub = false;
    bool   m_editPointerInside = false;

    bool m_spectrumGate = false;
    std::chrono::steady_clock::time_point m_glowPainted {};
    bool   m_wantImage = false;
    double m_paintFps = 0.0;
    QImage m_content;        // the plot, rendered off-screen
    QImage m_unfrosted;      // the plot before paintFrost() touched it
    std::chrono::steady_clock::time_point m_lastMoveTime;

    // ---- wheel-on-Q grab lock -------------------------------------------
    //  While the wheel changes a band's Q, that band keeps the wheel for 1.2 s
    //  whatever the pointer does.  On a cut the node's DISPLAYED position comes
    //  from the curve's value at f0, so it moves as Q changes; without the lock
    //  the pointer has to chase a dot that is running away from it, and a
    //  second notch lands on whichever node happens to be under the cursor now.
    //  The page does exactly this (_wLock / _wUntil).
    int    m_wheelLockBand = -1;
    std::chrono::steady_clock::time_point m_wheelLockUntil {};

    // ---- static background cache ----------------------------------------
    //  The grid and the ~30 frequency/dB labels never move unless a parameter
    //  or the geometry does, but they were redrawn into the plot image on
    //  every repaint — 30 antialiased glyph runs at 120 Hz, twice, for a
    //  picture that had not changed.  They are rendered once into m_bg and
    //  blitted instead.
    QImage m_bg;

    QImage  m_frostLayer;   // last composited control-pit glass
    quint64 m_frostKey = 0;

    // The glow bar only changes when the spectrum does (15 Hz) or when the
    // geometry does, but the plot repaints at up to 120 Hz.  Rebuilding a
    // 160x8 image with two exp() and a pow() per pixel on every paint measured
    // 0.15 ms a frame; the key keeps it to once per spectrum update.
    QImage  m_glowBar;
    quint64 m_glowKey = 0;
    std::chrono::steady_clock::time_point m_frostBuiltAt {};
    // Everything above the spectrum — the five band curves, the total curve and
    // its fill, the overflow fog and the five nodes.  None of it moves unless a
    // parameter or the geometry does, while the spectrum moves every poll.  It
    // is cached as a transparent overlay and blitted over the spectrum, which
    // is what takes a plot repaint from 3 ms down to about 1 ms.
    QImage  m_overlay;
    QString m_overlayKey;
    QString m_bgKey;         // geometry + range + accent + dim state
    const QImage& backgroundImage (double w, double h, double top, double bottom,
                                   const QColor& accent);

    /// Grid + ruler labels: static.
    const QImage& overlayImage (double w, double h, double top, double bottom,
                                const QColor& accent);
    /// Curves, fill, fog and nodes: everything above the spectrum.
    void paintOverlay (QPainter* g, double w, double h, double top, double bottom,
                       const QColor& accent);
    QString curveCacheKeyTag() const;
    QImage m_blurred;        // its blurred copy (bar strip only)
    int    m_contentW = 0, m_contentH = 0;
    QTimer m_repaint;
    bool   m_dirty = true;      // repaint requested since the last frame
    double m_pendingGlow = -1000.0;
    quint64 m_spectrumStamp = 0, m_lastSpectrumStamp = 0;
    /// When the last spectrum-driven repaint was issued, for the kPlotHz limit.
    std::chrono::steady_clock::time_point m_lastPaintAt {};

    /// Ballistics for the spectrum fill and the glow bar.
    ///
    /// The analyser is polled by the HOST, and in a plug-in that is slow:
    /// measured 11.8 Hz in jalv, so a new FFT arrives every ~85 ms while the
    /// scene renders at 60 fps.  Drawing the raw bins means each value is held
    /// for five frames and then snaps to an unrelated one — consecutive frames
    /// have nothing to do with each other, which is exactly how it was
    /// reported.
    ///
    /// So the bins are smoothed HERE, where frames happen: instant attack, and
    /// a release that decays towards the newest value with a time constant, so
    /// a jump is a movement across several frames instead of a cut.  This is
    /// what every hardware analyser does; the data is unchanged, only its
    /// presentation.
    /// What the analyser said last, untouched (ui/qt/OvEqView.cpp copies it out
    /// for the debug trace and for anything that wants the measured value).
    float  m_specRaw[128] = {};
    /// What is on screen: the averaged outline.  The averaging — power domain,
    /// 1/2 octave along frequency, a one-pole along time — is ovspec::
    /// SpectrumBallistics (ui/qt/OvSpectrum.h), which is the whole state.
    float  m_specShown[128] = {};
    ovspec::SpectrumBallistics m_specBall;
    std::chrono::steady_clock::time_point m_specAdvanceAt {};   // per view
    std::chrono::steady_clock::time_point m_specShownAt {};

    /// The glow bar's own ballistics: 32 band levels, through the same ovb::Fall.
    double m_glowShown[32] = {};
    ovb::Fall m_glowFall[32];
    /// True while a band is still falling towards its target; keeps the bar
    /// drawn until the release finishes instead of freezing mid-fall.
    bool   m_glowFalling = false;
    std::chrono::steady_clock::time_point m_glowShownAt {};
};
