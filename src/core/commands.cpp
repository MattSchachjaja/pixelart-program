#include "commands.h"
#include "document.h"

#include <QPainter>

// ---- AddLayerCommand ----
AddLayerCommand::AddLayerCommand(Document* doc, const QString& layerName)
    : m_doc(doc), m_name(layerName) {}
void AddLayerCommand::execute() { m_index = m_doc->addLayer(m_name); }
void AddLayerCommand::undo()    { m_doc->removeLayer(m_index); }

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

// ---- MoveLayerCommand ----
MoveLayerCommand::MoveLayerCommand(Document* doc, int from, int to)
    : m_doc(doc), m_from(from), m_to(to) {}
void MoveLayerCommand::execute() { m_doc->moveLayer(m_from, m_to); }
void MoveLayerCommand::undo()    { m_doc->moveLayer(m_to, m_from); }
QString MoveLayerCommand::name() const {
    return (m_to > m_from) ? "Ebene nach oben" : "Ebene nach unten";
}

// ---- DuplicateLayerCommand ----
DuplicateLayerCommand::DuplicateLayerCommand(Document* doc, int index)
    : m_doc(doc), m_index(index)
{
    if (auto* l = m_doc->layer(m_index)) {
        m_copy = l->image.copy();
        m_name = l->name + " (Kopie)";
    }
}
void DuplicateLayerCommand::execute() {
    if (m_copy.isNull()) return;
    int insertAt = m_index + 1;
    if (insertAt > m_doc->layerCount())
        insertAt = m_doc->layerCount();
    m_doc->insertLayer(insertAt, m_name, m_copy, true, 1.0f);
    m_newIndex = insertAt;
}
void DuplicateLayerCommand::undo() {
    if (m_newIndex >= 0 && m_newIndex < m_doc->layerCount())
        m_doc->removeLayer(m_newIndex);
    m_newIndex = -1;
}

// ---- MergeLayerCommand ----
MergeLayerCommand::MergeLayerCommand(Document* doc, int upperIndex)
    : m_doc(doc), m_upper(upperIndex)
{
    m_below = m_upper - 1;
    // Zustand vor dem Merge sichern.
    auto* up = m_doc->layer(m_upper);
    auto* lo = m_doc->layer(m_below);
    m_name      = up->name;
    m_upperImg  = up->image.copy();
    m_lowerImg  = lo->image.copy();
    m_lowerName = lo->name;
    m_lowerVis  = lo->visible;
    m_lowerOp   = lo->opacity;
}
void MergeLayerCommand::execute() { m_doc->mergeLayerDown(m_upper); }
void MergeLayerCommand::undo() {
    // Merge rueckgaengig: untere wiederherstellen, obere zuruecksetzen.
    m_doc->insertLayer(m_below, m_lowerName, m_lowerImg, m_lowerVis, m_lowerOp);
    if (auto* up = m_doc->layer(m_upper)) { up->image = m_upperImg; up->name = m_name; }
    m_doc->setActiveLayer(m_upper);
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

// ---- ColorInsertCommand ----
ColorInsertCommand::ColorInsertCommand(ColorState* state, int index, const QColor& color)
    : m_state(state), m_index(index), m_color(color) {}

void ColorInsertCommand::execute() { m_state->doInsertSlot(m_index, m_color); }
void ColorInsertCommand::undo()    { m_state->doRemoveSlot(m_index); }

// ---- ColorRemoveCommand ----
ColorRemoveCommand::ColorRemoveCommand(ColorState* state, int index, const QColor& color)
    : m_state(state), m_index(index), m_color(color) {}

void ColorRemoveCommand::execute() { m_state->doRemoveSlot(m_index); }
void ColorRemoveCommand::undo()    { m_state->doInsertSlot(m_index, m_color); }
