#include "commands.h"
#include "document.h"

#include <QPainter>

// ---- AddLayerCommand ----
AddLayerCommand::AddLayerCommand(Document* doc, const QString& layerName)
    : m_doc(doc), m_name(layerName) {}
void AddLayerCommand::execute() {
    // Neue Ebene direkt UEBER der aktuell aktiven Ebene einfuegen.
    // Beim ersten Ausfuehren wird die Position bestimmt; bei Redo
    // (m_index bereits gesetzt) bleibt sie erhalten.
    if (m_index < 0) {
        m_prevActive = m_doc->activeLayerIndex();
        m_index = qMin(m_doc->activeLayerIndex() + 1, m_doc->layerCount());
    }
    m_doc->insertLayer(m_index, m_name, QImage(), true, 1.0f);
}
void AddLayerCommand::undo() {
    m_doc->removeLayer(m_index);
    if (m_prevActive >= 0) m_doc->setActiveLayer(m_prevActive);
}
void AddLayerCommand::write(QDataStream& out) const {
    out << m_name << m_index << m_prevActive;
}

// ---- RemoveLayerCommand ----
RemoveLayerCommand::RemoveLayerCommand(Document* doc, int index)
    : m_doc(doc), m_index(index)
{
    if (auto* l = m_doc->layer(index)) {
        m_name = l->name; m_image = l->image.copy();
        m_visible = l->visible; m_opacity = l->opacity;
    }
}
void RemoveLayerCommand::execute() { m_doc->removeLayer(m_index); }
void RemoveLayerCommand::undo() {
    m_doc->insertLayer(m_index, m_name, m_image, m_visible, m_opacity);
}
void RemoveLayerCommand::write(QDataStream& out) const {
    out << m_index << m_name << m_image << m_visible << m_opacity;
}

// ---- MoveLayerCommand ----
MoveLayerCommand::MoveLayerCommand(Document* doc, int from, int to)
    : m_doc(doc), m_from(from), m_to(to) {}
void MoveLayerCommand::execute() { m_doc->moveLayer(m_from, m_to); }
void MoveLayerCommand::undo()    { m_doc->moveLayer(m_to, m_from); }
QString MoveLayerCommand::name() const {
    return (m_to > m_from) ? "Ebene nach oben" : "Ebene nach unten";
}
void MoveLayerCommand::write(QDataStream& out) const {
    out << m_from << m_to;
}

// ---- DuplicateLayerCommand ----
DuplicateLayerCommand::DuplicateLayerCommand(Document* doc, int index)
    : m_doc(doc), m_index(index)
{
    if (auto* l = m_doc->layer(m_index)) {
        m_copy = l->image.copy();
        m_name = l->name + " (Kopie)";
    }
    m_newIndex = qMin(m_index + 1, m_doc->layerCount());
}
void DuplicateLayerCommand::execute() {
    if (m_copy.isNull()) return;
    m_doc->insertLayer(m_newIndex, m_name, m_copy, true, 1.0f);
}
void DuplicateLayerCommand::undo() {
    if (m_newIndex >= 0 && m_newIndex < m_doc->layerCount())
        m_doc->removeLayer(m_newIndex);
}
void DuplicateLayerCommand::write(QDataStream& out) const {
    out << m_index << m_newIndex << m_copy << m_name;
}

// ---- MergeLayerCommand ----
MergeLayerCommand::MergeLayerCommand(Document* doc, int upperIndex)
    : m_doc(doc), m_upper(upperIndex)
{
    m_below = m_upper - 1;
    // Zustand vor dem Merge sichern.
    if (auto* up = m_doc->layer(m_upper)) {
        m_name     = up->name;
        m_upperImg = up->image.copy();
    }
    if (auto* lo = m_doc->layer(m_below)) {
        m_lowerImg  = lo->image.copy();
        m_lowerName = lo->name;
        m_lowerVis  = lo->visible;
        m_lowerOp   = lo->opacity;
    }
}
void MergeLayerCommand::execute() { m_doc->mergeLayerDown(m_upper); }
void MergeLayerCommand::undo() {
    // Merge rueckgaengig: untere wiederherstellen, obere zuruecksetzen.
    m_doc->insertLayer(m_below, m_lowerName, m_lowerImg, m_lowerVis, m_lowerOp);
    if (auto* up = m_doc->layer(m_upper)) { up->image = m_upperImg; up->name = m_name; }
    m_doc->setActiveLayer(m_upper);
}
void MergeLayerCommand::write(QDataStream& out) const {
    out << m_upper << m_below << m_name << m_lowerName
        << m_upperImg << m_lowerImg << m_lowerVis << m_lowerOp;
}

// ---- StrokeCommand ----
StrokeCommand::StrokeCommand(Document* doc, int layerIndex,
                             const QRect& dirtyRect,
                             const QImage& before,
                             const QImage& after,
                             const QString& actionName)
    : m_doc(doc), m_layer(layerIndex), m_rect(dirtyRect),
      m_before(before), m_after(after), m_actionName(actionName) {}
void StrokeCommand::execute() {
    if (auto* l = m_doc->layer(m_layer)) {
        QPainter p(&l->image);
        p.setCompositionMode(QPainter::CompositionMode_Source);
        p.drawImage(m_rect.topLeft(), m_after);
        p.end();
        emit m_doc->changed();
    }
}
void StrokeCommand::undo() {
    if (auto* l = m_doc->layer(m_layer)) {
        QPainter p(&l->image);
        p.setCompositionMode(QPainter::CompositionMode_Source);
        p.drawImage(m_rect.topLeft(), m_before);
        p.end();
        emit m_doc->changed();
    }
}
void StrokeCommand::write(QDataStream& out) const {
    out << m_layer << m_rect << m_before << m_after << m_actionName;
}

// ---- ResizeCanvasCommand ----
ResizeCanvasCommand::ResizeCanvasCommand(Document* doc, int newW, int newH)
    : m_doc(doc), m_newW(newW), m_newH(newH)
{
    m_oldW = doc->width();
    m_oldH = doc->height();
    for (int i = 0; i < doc->layerCount(); ++i)
        m_oldImages.append(doc->layer(i)->image.copy());
}
void ResizeCanvasCommand::execute() { m_doc->resize(m_newW, m_newH); }
void ResizeCanvasCommand::undo() {
    m_doc->resize(m_oldW, m_oldH);
    for (int i = 0; i < m_doc->layerCount() && i < m_oldImages.size(); ++i)
        m_doc->layer(i)->image = m_oldImages[i];
    emit m_doc->changed();
}
void ResizeCanvasCommand::write(QDataStream& out) const {
    out << m_oldW << m_oldH << m_newW << m_newH << m_oldImages;
}

// ---- FlipCanvasCommand ----
FlipCanvasCommand::FlipCanvasCommand(Document* doc, bool horizontal, bool vertical)
    : m_doc(doc), m_h(horizontal), m_v(vertical) {}
void FlipCanvasCommand::execute() { m_doc->flip(m_h, m_v); }
void FlipCanvasCommand::undo()    { m_doc->flip(m_h, m_v); }
QString FlipCanvasCommand::name() const {
    return m_h ? "Horizontal spiegeln" : "Vertikal spiegeln";
}
void FlipCanvasCommand::write(QDataStream& out) const {
    out << m_h << m_v;
}

// ===================================================================
// Farbslot-Commands
// ===================================================================
#include "colorstate.h"

// ---- ColorChangeCommand ----
ColorChangeCommand::ColorChangeCommand(ColorState* state, int slotIndex,
                                       const QColor& oldColor, const QColor& newColor)
    : m_state(state), m_slotIndex(slotIndex), m_oldColor(oldColor), m_newColor(newColor) {}

void ColorChangeCommand::execute() { m_state->doSetSlot(m_slotIndex, m_newColor); }
void ColorChangeCommand::undo()    { m_state->doSetSlot(m_slotIndex, m_oldColor); }
void ColorChangeCommand::write(QDataStream& out) const {
    out << m_slotIndex << m_oldColor << m_newColor;
}

// ---- ColorInsertCommand ----
ColorInsertCommand::ColorInsertCommand(ColorState* state, int index, const QColor& color,
                                       const QVector<int>& rowsBefore,
                                       const QVector<int>& rowsAfter)
    : m_state(state), m_index(index), m_color(color),
      m_rowsBefore(rowsBefore), m_rowsAfter(rowsAfter) {}

// Die Zeilenstruktur wird mit gesetzt -> der Slot landet bei Redo exakt
// wieder in derselben Zeile, bei Undo wird die alte Struktur hergestellt.
void ColorInsertCommand::execute() { m_state->doInsertSlot(m_index, m_color, m_rowsAfter); }
void ColorInsertCommand::undo()    { m_state->doRemoveSlot(m_index, m_rowsBefore); }
void ColorInsertCommand::write(QDataStream& out) const {
    out << m_index << m_color << m_rowsBefore << m_rowsAfter;
}

// ---- ColorRemoveCommand ----
ColorRemoveCommand::ColorRemoveCommand(ColorState* state, int index, const QColor& color,
                                       const QVector<int>& rowsBefore,
                                       const QVector<int>& rowsAfter)
    : m_state(state), m_index(index), m_color(color),
      m_rowsBefore(rowsBefore), m_rowsAfter(rowsAfter) {}

void ColorRemoveCommand::execute() { m_state->doRemoveSlot(m_index, m_rowsAfter); }
void ColorRemoveCommand::undo()    { m_state->doInsertSlot(m_index, m_color, m_rowsBefore); }
void ColorRemoveCommand::write(QDataStream& out) const {
    out << m_index << m_color << m_rowsBefore << m_rowsAfter;
}

// ===================================================================
// Projektformat: Command aus dem Stream laden
// ===================================================================
ICommand* loadCommandFromStream(QDataStream& in, int type,
                                Document* doc, ColorState* colors,
                                int version) {
    switch (type) {
    case CmdAddLayer: {
        QString name; int index, prevActive;
        in >> name >> index >> prevActive;
        auto* c = new AddLayerCommand(doc, name);
        c->m_index = index; c->m_prevActive = prevActive;
        return c;
    }
    case CmdRemoveLayer: {
        int index; QString name; QImage image; bool vis; float op;
        in >> index >> name >> image >> vis >> op;
        auto* c = new RemoveLayerCommand(doc, index);
        c->m_name = name; c->m_image = image;
        c->m_visible = vis; c->m_opacity = op;
        return c;
    }
    case CmdMoveLayer: {
        int from, to;
        in >> from >> to;
        return new MoveLayerCommand(doc, from, to);
    }
    case CmdDuplicateLayer: {
        int index, newIndex; QImage copy; QString name;
        in >> index >> newIndex >> copy >> name;
        auto* c = new DuplicateLayerCommand(doc, index);
        c->m_newIndex = newIndex; c->m_copy = copy; c->m_name = name;
        return c;
    }
    case CmdMergeLayer: {
        int upper, below; QString name, lowerName;
        QImage upperImg, lowerImg; bool lowerVis; float lowerOp;
        in >> upper >> below >> name >> lowerName
           >> upperImg >> lowerImg >> lowerVis >> lowerOp;
        auto* c = new MergeLayerCommand(doc, upper);
        c->m_below = below; c->m_name = name; c->m_lowerName = lowerName;
        c->m_upperImg = upperImg; c->m_lowerImg = lowerImg;
        c->m_lowerVis = lowerVis; c->m_lowerOp = lowerOp;
        return c;
    }
    case CmdStroke: {
        int layer; QRect rect; QImage before, after; QString actionName;
        in >> layer >> rect >> before >> after >> actionName;
        return new StrokeCommand(doc, layer, rect, before, after, actionName);
    }
    case CmdResizeCanvas: {
        int oldW, oldH, newW, newH; QList<QImage> oldImages;
        in >> oldW >> oldH >> newW >> newH >> oldImages;
        auto* c = new ResizeCanvasCommand(doc, newW, newH);
        c->m_oldW = oldW; c->m_oldH = oldH; c->m_oldImages = oldImages;
        return c;
    }
    case CmdColorChange: {
        int slot; QColor oldColor, newColor;
        in >> slot >> oldColor >> newColor;
        return new ColorChangeCommand(colors, slot, oldColor, newColor);
    }
    case CmdColorInsert: {
        int index; QColor color; QVector<int> before, after;
        in >> index >> color;
        if (version >= 2) in >> before >> after;
        return new ColorInsertCommand(colors, index, color, before, after);
    }
    case CmdColorRemove: {
        int index; QColor color; QVector<int> before, after;
        in >> index >> color;
        if (version >= 2) in >> before >> after;
        return new ColorRemoveCommand(colors, index, color, before, after);
    }
    case CmdFlipCanvas: {
        bool h, v;
        in >> h >> v;
        return new FlipCanvasCommand(doc, h, v);
    }
    default:
        return nullptr;
    }
}
