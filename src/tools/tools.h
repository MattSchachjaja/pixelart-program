#pragma once

#include <QPointF>
#include <QString>
#include <Qt>
#include <QPoint>
#include <QRect>
#include <QImage>
#include <QBitArray>
#include <QPolygonF>
#include <QPainterPath>
#include <memory>
#include <functional>
#include <QObject>

class Document;
class ColorState;
class History;
struct Layer;

#include "tool.h"

// ---------- Pixelstift ----------
class PencilTool : public ITool {
public:
    PencilTool(Document* doc, ColorState* cs, History* hist);

    QString name() const override { return "Pixelstift"; }
    bool mousePress(const QPointF& docPos, Qt::MouseButton button,
                    Qt::KeyboardModifiers mods) override;
    void mouseMove(const QPointF& docPos, Qt::KeyboardModifiers mods) override;
    void mouseRelease(const QPointF& docPos, Qt::MouseButton button) override;

    void setDocument(Document* d) override { m_doc = d; }
    void setHistory(History* h) override { m_history = h; }

    // Pixel-Perfekt: verhindert Doppelpixel und L-Formen beim Zeichnen.
    void setPixelPerfect(bool p) { m_pixelPerfect = p; }
    bool pixelPerfect() const { return m_pixelPerfect; }
private:
    void plot(int x, int y);          // einzelnen Pixel setzen (mit Auswahl- & Besuchs-Test)
    void restore(int x, int y);       // Pixel aus Vor-Zustand zurückholen (Ecke entfernen)
    void line(int x0, int y0, int x1, int y1);  // Bresenham
    void growDirty(const QRect& r);

    Document*   m_doc;
    ColorState* m_colors;
    History*    m_history;
    Layer*      m_lay = nullptr;
    bool        m_drawing = false;
    bool        m_erasing = false;
    QPoint      m_lastPx, m_prevPx;   // letzte beiden Pixel (für Eckenerkennung)
    QRect       m_dirtyRect;
    QImage      m_beforeFull;
    QImage      m_selMask;
    bool        m_hasSel = false;
    QBitArray   m_visited;            // schon gezeichneter Pixel? (verhindert Doppelpixel)
    QRgb        m_penColor = 0;       // premultiplied Stiftfarbe
    bool        m_pixelPerfect = true;
    float       m_eraseStrength = 1.0f;  // Rechtsklick-Löschstärke (0..1), aus Alpha
};

// ---------- Pinsel ----------
class BrushTool : public ITool {
public:
    BrushTool(Document* doc, ColorState* cs, History* hist);
    QString name() const override { return "Pinsel"; }
    bool mousePress(const QPointF& docPos, Qt::MouseButton button,
                    Qt::KeyboardModifiers mods) override;
    void mouseMove(const QPointF& docPos, Qt::KeyboardModifiers mods) override;
    void mouseRelease(const QPointF& docPos, Qt::MouseButton button) override;

    void setDocument(Document* d) override { m_doc = d; }
    void setHistory(History* h) override { m_history = h; }

    void setSize(int size) { m_size = qBound(1, size, 200); }
    int  size() const { return m_size; }
    void setSoftness(int soft) { m_softness = qBound(0, soft, 100); }
    int  softness() const { return m_softness; }
    // Dither: zeichnet nur harte Pixel; Weichheit steuert Dither-Intensität.
    void setDither(bool d) { m_dither = d; }
    bool dither() const { return m_dither; }

private:
    void paintDot(const QPointF& p, bool erase);
    void stampDab(const QPointF& p, bool erase);       // Max-Maske (nie stapelnd)
    void paintLine(const QPointF& a, const QPointF& b, bool erase);
    void growDirty(const QRect& r);

    Document*   m_doc;
    ColorState* m_colors;
    History*    m_history;
    bool        m_drawing = false;
    bool        m_erasing = false;
    QPointF     m_last;
    QRect       m_dirtyRect;
    QImage      m_beforeFull;
    QImage      m_strokeMask;   // Max-Alpha-Footprint pro Strich (verhindert Stapelung)
    int         m_size = 10;
    int         m_softness = 0;
    bool        m_dither = false;
};

// ---------- Auswahl (Rechteck / Ellipse / Lasso) ----------
class RectSelectTool : public ITool {
public:
    enum SelectMode { Rectangle, Ellipse, Lasso };

    RectSelectTool(Document* doc);
    QString name() const override { return "Rechteckauswahl"; }
    bool mousePress(const QPointF& docPos, Qt::MouseButton button,
                    Qt::KeyboardModifiers mods) override;
    void mouseMove(const QPointF& docPos, Qt::KeyboardModifiers mods) override;
    void mouseRelease(const QPointF& docPos, Qt::MouseButton button) override;
    void paintOverlay(QPainter& painter) const override;

    void setDocument(Document* d) override { m_doc = d; }

    void setSelectMode(SelectMode m) { m_mode = m; }
    SelectMode selectMode() const { return m_mode; }
private:
    void finishLasso();
    Document*   m_doc;
    bool        m_dragging = false;
    bool        m_addMode = false;   // STRG gedrückt -> zur Auswahl hinzufügen
    bool        m_subMode = false;   // ALT / Rechtsklick -> von Auswahl abziehen
    QPointF     m_start, m_end;
    SelectMode  m_mode = Rectangle;
    QPolygonF   m_points;            // Lasso-Punkte
    QPointF     m_lastPoint;
};

// ---------- Zauberstab ----------
class MagicWandTool : public ITool {
public:
    enum MatchMode { Contiguous, All };
    MagicWandTool(Document* doc);
    QString name() const override { return "Zauberstab"; }
    bool mousePress(const QPointF& docPos, Qt::MouseButton button,
                    Qt::KeyboardModifiers mods) override;
    void mouseMove(const QPointF& docPos, Qt::KeyboardModifiers mods) override {}
    void mouseRelease(const QPointF& docPos, Qt::MouseButton button) override {}
    void paintOverlay(QPainter&) const override {}

    void setDocument(Document* d) override { m_doc = d; }

    void setTolerance(int tol) { m_tolerance = qBound(0, tol, 255); }
    int  tolerance() const { return m_tolerance; }
    void setMatchMode(MatchMode m) { m_matchMode = m; }
    MatchMode matchMode() const { return m_matchMode; }
    void setCrossLayer(bool c) { m_crossLayer = c; }     // Ebenenübergreifend
    bool crossLayer() const { return m_crossLayer; }
private:
    Document* m_doc;
    int       m_tolerance = 32;
    MatchMode m_matchMode = Contiguous;
    bool      m_crossLayer = false;
};

// ---------- Verschieber (Auswahl / Pixel / Paste) ----------
class MoverTool : public ITool {
public:
    enum MoveMode { Selection, Pixels, Paste };
    MoverTool(Document* doc, History* hist);
    QString name() const override { return "Verschieber"; }
    bool mousePress(const QPointF& docPos, Qt::MouseButton button,
                    Qt::KeyboardModifiers mods) override;
    void mouseMove(const QPointF& docPos, Qt::KeyboardModifiers mods) override;
    void mouseRelease(const QPointF& docPos, Qt::MouseButton button) override;
    void paintOverlay(QPainter& painter) const override;
    void keyPress(QKeyEvent* e) override;            // Enter = übernehmen, Esc = verwerfen

    void setDocument(Document* d) override { m_doc = d; }
    void setHistory(History* h) override { m_history = h; }

    void setMoveMode(MoveMode m) { m_mode = m; }
    MoveMode moveMode() const { return m_mode; }

    // Rechtsklick: Auswahl aufheben + zum Auswahlwerkzeug wechseln.
    std::function<void()> onRightClickSwitchToSelect;

    enum class DragKind { None, Move, ResizeTL, ResizeTR, ResizeBL, ResizeBR };

private:
    Document* m_doc;
    History*  m_history;
    MoveMode  m_mode = Selection;
    bool      m_dragging = false;
    QPointF   m_lastPos;
    QPointF   m_pressPos;          // Press-Position für das Gesamt-Delta (Pixel-Modus)
    QImage    m_beforeImage;       // Ebenen-Zustand vor dem Verschieben (Pixel-Modus)
    QImage    m_capturedPixels;    // erfasste Auswahl-Pixel (document-groß, maskiert)
    QImage    m_origMask;          // Auswahlmaske beim Press (Pixel-Modus)

    // --- Resize über Eck-Knoten ---
    DragKind  m_dragKind = DragKind::None;
    mutable qreal     m_viewZoom = 1.0;          // zoom aus paintOverlay gecacht
    QRectF    m_resizeOrigBounds;          // selectionBounds beim Press
    QPointF   m_resizeAnchor;              // gegenüberliegende Ecke (bleibt fix)
    QImage    m_resizeOrigPixels;          // full-doc ARGB, nur Auswahl-Pixel
    QImage    m_resizeOrigMask;            // full-doc Alpha8
    QImage    m_resizeBeforeImage;         // Ebenen-Zustand beim Press

    QRectF    selectionBoundsF() const;    // selectionBounds als QRectF (ggf. Paste)
    DragKind  hitTestHandle(const QPointF& docPos) const;
    void      drawHandle(QPainter& painter, const QPointF& docCorner) const;
    void      beginResize(const QPointF& docPos, DragKind kind);
    void      updateResize(const QPointF& docPos);
    void      endResize();
};

// ---------- Pipette ----------
class PipetteTool : public ITool {
public:
    PipetteTool(Document* doc, ColorState* cs);
    QString name() const override { return "Pipette"; }
    bool mousePress(const QPointF& docPos, Qt::MouseButton button,
                    Qt::KeyboardModifiers mods) override;
    void mouseMove(const QPointF& docPos, Qt::KeyboardModifiers mods) override {}
    void mouseRelease(const QPointF& docPos, Qt::MouseButton button) override {}

    void setDocument(Document* d) override { m_doc = d; }
    void setCrossLayer(bool c) { m_crossLayer = c; }     // Ebenenübergreifend
    bool crossLayer() const { return m_crossLayer; }
private:
    Document*   m_doc;
    ColorState* m_colors;
    bool        m_crossLayer = false;
};

// ---------- Formen ----------
class FormTool : public ITool {
public:
    enum ShapeMode { Rectangle, Ellipse, Triangle, Line };
    FormTool(Document* doc, ColorState* cs, History* hist);
    QString name() const override { return "Formen"; }
    bool mousePress(const QPointF& docPos, Qt::MouseButton button,
                    Qt::KeyboardModifiers mods) override;
    void mouseMove(const QPointF& docPos, Qt::KeyboardModifiers mods) override;
    void mouseRelease(const QPointF& docPos, Qt::MouseButton button) override;
    void paintOverlay(QPainter& painter) const override;

    void setDocument(Document* d) override { m_doc = d; }
    void setHistory(History* h) override { m_history = h; }

    void setShapeMode(ShapeMode mode) { m_mode = mode; }
    ShapeMode shapeMode() const { return m_mode; }
    void setFilled(bool f) { m_filled = f; }
    bool filled() const { return m_filled; }
    void setStrokeWidth(int w) { m_strokeWidth = qBound(1, w, 100); }
    int  strokeWidth() const { return m_strokeWidth; }
private:
    Document*   m_doc;
    ColorState* m_colors;
    History*    m_history;
    bool        m_dragging = false;
    bool        m_erasing = false;   // Rechtsklick = löschen
    QPointF     m_start, m_end;
    ShapeMode   m_mode = Rectangle;
    bool        m_filled = true;
    int         m_strokeWidth = 2;
};

// ---------- Füllen ----------
class FillTool : public ITool {
public:
    enum MatchMode { Contiguous, All };
    FillTool(Document* doc, ColorState* cs, History* hist);
    QString name() const override { return "Füllen"; }
    bool mousePress(const QPointF& docPos, Qt::MouseButton button,
                    Qt::KeyboardModifiers mods) override;
    void mouseMove(const QPointF& docPos, Qt::KeyboardModifiers mods) override {}
    void mouseRelease(const QPointF& docPos, Qt::MouseButton button) override {}
    void paintOverlay(QPainter&) const override {}

    void setDocument(Document* d) override { m_doc = d; }
    void setHistory(History* h) override { m_history = h; }

    void setTolerance(int tol) { m_tolerance = qBound(0, tol, 255); }
    int  tolerance() const { return m_tolerance; }
    void setMatchMode(MatchMode m) { m_matchMode = m; }
    MatchMode matchMode() const { return m_matchMode; }
    void setCrossLayer(bool c) { m_crossLayer = c; }     // Ebenenübergreifend
    bool crossLayer() const { return m_crossLayer; }
private:
    Document*   m_doc;
    ColorState* m_colors;
    History*    m_history;
    int         m_tolerance = 32;
    MatchMode   m_matchMode = Contiguous;
    bool        m_crossLayer = false;
};

// ---------- Text ----------
class TextTool : public ITool {
public:
    TextTool(Document* doc, ColorState* cs, History* hist);
    QString name() const override { return "Text"; }
    bool mousePress(const QPointF& docPos, Qt::MouseButton button,
                    Qt::KeyboardModifiers mods) override;
    void mouseMove(const QPointF&, Qt::KeyboardModifiers) override {}
    void mouseRelease(const QPointF&, Qt::MouseButton) override {}
    void paintOverlay(QPainter& painter) const override;
    void keyPress(QKeyEvent* e) override;

    void setDocument(Document* d) override { m_doc = d; }
    void setHistory(History* h) override { m_history = h; }

    void setFontSize(int s) { m_fontSize = qBound(6, s, 400); }
    int  fontSize() const { return m_fontSize; }
    bool isEditing() const { return m_editing; }   // läuft gerade eine Texteingabe?
private:
    void commit();
    Document*   m_doc;
    ColorState* m_colors;
    History*    m_history;
    bool        m_editing = false;
    QPointF     m_pos;
    QString     m_text;
    int         m_fontSize = 32;
};

// ---------- ToolManager ----------
class ToolManager : public QObject {
    Q_OBJECT
public:
    ToolManager(Document* doc, ColorState* cs, History* hist, QObject* parent = nullptr);

    ITool* active() const { return m_active; }
    void setActiveTool(ITool* tool);

    // Multi-Document: alle Tools auf neues Document/History umschwenken.
    void setDocument(Document* doc);
    void setHistory(History* hist);

    PencilTool* pencil() { return m_pencil.get(); }
    BrushTool*  brush()  { return m_brush.get(); }
    RectSelectTool* rectSelect() { return m_rectSelect.get(); }
    MagicWandTool* magicWand() { return m_magicWand.get(); }
    FillTool*  fill()    { return m_fill.get(); }
    MoverTool* mover()   { return m_mover.get(); }
    PipetteTool* pipette() { return m_pipette.get(); }
    TextTool*  text()    { return m_text.get(); }
    FormTool*   form()   { return m_form.get(); }

    QList<ITool*> allTools() const;

private:
    Document*   m_doc;
    ColorState* m_colors;
    History*    m_history;
    ITool*      m_active = nullptr;

    std::unique_ptr<PencilTool>       m_pencil;
    std::unique_ptr<BrushTool>        m_brush;
    std::unique_ptr<RectSelectTool>   m_rectSelect;
    std::unique_ptr<MagicWandTool>    m_magicWand;
    std::unique_ptr<FillTool>         m_fill;
    std::unique_ptr<MoverTool>        m_mover;
    std::unique_ptr<PipetteTool>      m_pipette;
    std::unique_ptr<TextTool>         m_text;
    std::unique_ptr<FormTool>         m_form;
};