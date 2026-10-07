#pragma once

#include <QObject>
#include <QColor>
#include <QVector>

class History;
class ColorChangeCommand;
class ColorInsertCommand;
class ColorRemoveCommand;

// Variable Anzahl Farbslots. Default: 64 (AAP-64-Palette).
// Slot 0..7 sind über die Tasten 1..8 erreichbar – und zwar relativ
// zur Zeile des zuletzt aktivierten Slots (siehe PaletteDock).
//
// Farbänderungen / Inserts / Removes werden (sofern eine History
// gesetzt ist) als Undo/Redo-Command aufgezeichnet. Beim Drag im
// ColorPicker erzeugt der gesamte Drag-Vorgang nur EIN Command:
// dazu rufen ColorPicker mousePressEvent -> beginColorChange() und
// mouseReleaseEvent -> endColorChange() auf.
class ColorState : public QObject {
    Q_OBJECT
public:
    explicit ColorState(QObject* parent = nullptr);

    QColor activeColor() const { return m_slots.value(m_active); }
    QColor slotColor(int i) const { return m_slots.value(i); }
    int    activeSlot() const { return m_active; }
    int    slotCount() const { return m_slots.size(); }

    void setActiveSlot(int i);
    void setActiveColor(const QColor& c);
    void setSlot(int i, const QColor& c);
    void addSlot(const QColor& c);
    void insertSlot(int index, const QColor& c);
    void removeSlot(int i);

    // History-Anbindung. Wenn nicht gesetzt, werden keine Commands
    // gepusht (z.B. vor Programmstart / beim Laden).
    void setHistory(History* h) { m_history = h; }
    // Globaler Schalter, um Aufzeichnung temporär abzuschalten
    // (z.B. beim Laden einer Palette-Datei).
    void setRecordHistory(bool r) { m_recordHistory = r; }
    // Drag-Tracking: zwischen begin/end werden setActiveColor-Aufrufe
    // nicht einzeln als Command gepusht, sondern zu einem einzigen
    // zusammengefasst.
    void beginColorChange();
    void endColorChange();

signals:
    void activeColorChanged(const QColor&);
    void activeSlotChanged(int);
    void slotsChanged();

private:
    friend class ColorChangeCommand;
    friend class ColorInsertCommand;
    friend class ColorRemoveCommand;

    // Führen die eigentliche Änderung aus OHNE ein Command zu pushen.
    // Werden von den Commands (execute/undo) aufgerufen.
    void doSetSlot(int i, const QColor& c);
    void doInsertSlot(int index, const QColor& c);
    void doRemoveSlot(int i);

    QVector<QColor> m_slots;
    int             m_active = 0;
    History*        m_history = nullptr;
    bool            m_recordHistory = true;
    bool            m_inChange = false;
    QColor          m_changeStartColor;
};
