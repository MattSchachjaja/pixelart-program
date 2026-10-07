#pragma once

#include <QDockWidget>
#include <QColor>
#include <QVector>
#include <QSize>

class ColorState;
class QWidget;
class QVBoxLayout;
class QScrollArea;
class QToolButton;
class ColorSwatch;

// Palette-Fenster mit autonomen Zeilen.
//
// Aufbau:
//   Zeile 0: [Slot][Slot]...[+]      (belegte Zeile)
//   Zeile 1: [Slot]...[+]            (belegte Zeile)
//   Zeile 2: [+]                     (immer eine leere Zeile am Schluss)
//
// Jede Zeile ist eine eigenständige Entität mit eigenem Plus-Button:
//   - Plus in einer belegten Zeile  -> Zeile bekommt einen weiteren Slot.
//   - Plus in der leeren Zeile      -> Zeile bekommt ihren ersten Slot,
//                                      darunter entsteht eine neue leere Zeile.
//
// Der ColorState hält weiterhin eine flache Liste aller Slots; das
// PaletteDock merkt sich zusätzlich in m_rowSizes, wie viele Slots
// in welcher Zeile sitzen.
//
// Die ersten 7 Slots sind über die Tasten 1..7 erreichbar.
// Rechtsklick auf einen Slot -> "Entfernen".
class PaletteDock : public QDockWidget {
    Q_OBJECT
public:
    explicit PaletteDock(ColorState* state, QWidget* parent = nullptr);
    QSize sizeHint() const override;

    // Für Shortcuts 1..8 im MainWindow: liefert die Zeile, in der der
    // aktuell aktive Slot liegt.
    int activeRow() const { return m_activeRow; }
    // Liefert den flachen Slot-Index für (Zeile, Spalte) oder -1, wenn
    // die Koordinaten außerhalb der aktuellen Zeilenstruktur liegen.
    int slotAt(int row, int col) const;

private:
    void recomputeRowSizesDefault();   // m_rowSizes aus flacher Liste neu aufbauen
    void rebuildRows();                // gesamtes UI neu aufbauen
    int  computeInsertIndex(int rowIndex) const;
    int  computeRemoveIndex(int rowIndex, int posInRow) const;
    void addSlotAtEndOfRow(int rowIndex);
    void removeSlotAt(int rowIndex, int posInRow);

    bool saveToFile(const QString& path);
    bool loadFromFile(const QString& path);
    QString defaultPalettePath() const;

    ColorState*            m_state;
    QWidget*               m_content    = nullptr;
    QScrollArea*           m_scroll     = nullptr;
    QWidget*               m_rowsHost    = nullptr;
    QVBoxLayout*           m_rowsLayout  = nullptr;
    QVector<int>           m_rowSizes;  // Slots pro Zeile (letzte ist immer 0)
    QVector<ColorSwatch*>  m_swatches;  // flach, parallel zu ColorState-Slots
    int                    m_columns    = 8;
    int                    m_activeRow  = 0;   // Zeile des aktiven Slots
    bool                   m_internalChange = false;  // unterdrückt slotsChanged-Callback
};
