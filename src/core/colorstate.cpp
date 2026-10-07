#include "colorstate.h"
#include "history.h"
#include "commands.h"

ColorState::ColorState(QObject* parent) : QObject(parent) {
    // AAP-64 – professionelle 64-Farben-Palette für Pixel-Art
    // von Arne Niklas Jansson. 8 Zeilen à 8 Farben.
    static const char* aap64[] = {
        // Zeile 1
        "#0d0c1c", "#1d1621", "#382a3e", "#56435e", "#735c75", "#95798f", "#b698a8", "#d2bdbb",
        // Zeile 2
        "#eadfd8", "#f6ecdc", "#ffffff", "#4c1e25", "#7c1224", "#b1364a", "#ee635f", "#ff8d7a",
        // Zeile 3
        "#ffb385", "#ffd393", "#fff1b8", "#fffa96", "#cdd95e", "#9bcb61", "#6cb35e", "#3f8a4d",
        // Zeile 4
        "#1f5b3e", "#16402e", "#dad3b9", "#b9aa8b", "#8d7853", "#6b552f", "#48371b", "#28200e",
        // Zeile 5
        "#120c08", "#2d130a", "#491506", "#6e1c0b", "#92271b", "#b54429", "#d36639", "#e58f4c",
        // Zeile 6
        "#ecbb5f", "#f0de83", "#2b1a38", "#46224f", "#6f305f", "#883f63", "#9e4a69", "#b96b8a",
        // Zeile 7
        "#cd92a5", "#e8c4ce", "#1a203f", "#283c66", "#38578e", "#4b73bf", "#6394ee", "#82b6ff",
        // Zeile 8
        "#aed4ff", "#d6edff", "#1d1c24", "#252428", "#3a3838", "#4a484c", "#615f5e", "#828280"
    };
    for (const char* hex : aap64)
        m_slots.append(QColor(hex));
}

void ColorState::setActiveSlot(int i) {
    if (i < 0 || i >= m_slots.size()) return;
    if (m_active != i) {
        m_active = i;
        emit activeSlotChanged(m_active);
        emit activeColorChanged(m_slots[m_active]);
    } else {
        emit activeSlotChanged(m_active);
    }
}

void ColorState::setActiveColor(const QColor& c) {
    if (!c.isValid() || m_slots.isEmpty()) return;
    if (c == m_slots[m_active]) return;

    const QColor oldColor = m_slots[m_active];
    doSetSlot(m_active, c);

    // Außerhalb eines Drags: Command pushen (sonst übernimmt endColorChange das).
    if (!m_inChange && m_history && m_recordHistory)
        m_history->push(new ColorChangeCommand(this, m_active, oldColor, c));
}

void ColorState::setSlot(int i, const QColor& c) {
    if (i < 0 || i >= m_slots.size() || !c.isValid()) return;
    const QColor oldColor = m_slots[i];
    doSetSlot(i, c);
    if (!m_inChange && m_history && m_recordHistory)
        m_history->push(new ColorChangeCommand(this, i, oldColor, c));
}

void ColorState::addSlot(const QColor& c) {
    insertSlot(m_slots.size(), c);
}

void ColorState::insertSlot(int index, const QColor& c) {
    if (!c.isValid()) return;
    if (m_history && m_recordHistory && !m_inChange)
        m_history->push(new ColorInsertCommand(this, index, c));
    else
        doInsertSlot(index, c);
}

void ColorState::removeSlot(int i) {
    if (m_slots.isEmpty()) return;
    if (i < 0 || i >= m_slots.size()) return;
    if (m_history && m_recordHistory && !m_inChange)
        m_history->push(new ColorRemoveCommand(this, i, m_slots[i]));
    else
        doRemoveSlot(i);
}

void ColorState::beginColorChange() {
    if (!m_inChange) {
        m_inChange = true;
        m_changeStartColor = activeColor();
    }
}

void ColorState::endColorChange() {
    if (!m_inChange) return;
    m_inChange = false;
    if (m_history && m_recordHistory && activeColor() != m_changeStartColor)
        m_history->push(new ColorChangeCommand(this, m_active, m_changeStartColor, activeColor()));
}

// ---------- interne doXXX-Methoden (ohne Command-Push) ----------

void ColorState::doSetSlot(int i, const QColor& c) {
    if (i < 0 || i >= m_slots.size()) return;
    if (m_slots[i] == c) return;
    m_slots[i] = c;
    if (i == m_active) emit activeColorChanged(c);
    emit slotsChanged();
}

void ColorState::doInsertSlot(int index, const QColor& c) {
    if (!c.isValid()) return;
    if (index < 0) index = 0;
    if (index > m_slots.size()) index = m_slots.size();
    m_slots.insert(index, c);
    if (m_slots.size() == 1)
        m_active = 0;
    else if (index <= m_active)
        m_active++;
    emit slotsChanged();
}

void ColorState::doRemoveSlot(int i) {
    if (m_slots.isEmpty()) return;
    if (i < 0 || i >= m_slots.size()) return;
    m_slots.removeAt(i);
    if (m_slots.isEmpty())
        m_active = -1;
    else if (i < m_active)
        m_active--;
    else if (m_active >= m_slots.size())
        m_active = m_slots.size() - 1;
    emit slotsChanged();
    emit activeSlotChanged(m_active);
    if (m_active >= 0)
        emit activeColorChanged(m_slots[m_active]);
}
