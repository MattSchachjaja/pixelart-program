#include "palettedock.h"
#include "colorstate.h"
#include "colorwidgets.h"   // ColorSwatch

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QScrollArea>
#include <QToolButton>
#include <QIcon>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QFile>
#include <QStandardPaths>
#include <QDir>
#include <QMessageBox>
#include <QFileDialog>
#include <QAction>
#include <QTimer>

namespace {

// Einheitlicher Plus-Button (quadratisch wie ein Farbslot).
QToolButton* makePlusButton(QWidget* parent) {
    auto* btn = new QToolButton(parent);
    btn->setIcon(QIcon::fromTheme("list-add"));
    btn->setFixedSize(26, 26);
    btn->setAutoRaise(true);
    btn->setFocusPolicy(Qt::NoFocus);
    btn->setCursor(Qt::ArrowCursor);
    btn->setToolTip(PaletteDock::tr("Farbslot in dieser Zeile anlegen"));
    btn->setStyleSheet(
        "QToolButton { background:#2a2a2a; color:#ddd; border:1px solid #444;"
        "              border-radius:3px; }"
        "QToolButton:hover { background:#3a3a3a; }"
        "QToolButton:pressed { background:#1f1f1f; }");
    return btn;
}

} // anonymous namespace

// ===================================================================
// PaletteDock
// ===================================================================
PaletteDock::PaletteDock(ColorState* state, QWidget* parent)
    : QDockWidget(QString::fromUtf8("Palette"), parent), m_state(state)
{
    setAllowedAreas(Qt::NoDockWidgetArea);
    setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);

    m_content = new QWidget(this);
    auto* outer = new QVBoxLayout(m_content);
    outer->setContentsMargins(8, 8, 8, 8);
    outer->setSpacing(6);

    // --- Scrollbarer Bereich für die Zeilen ------------------------
    m_scroll = new QScrollArea(m_content);
    m_scroll->setWidgetResizable(true);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scroll->setStyleSheet(
        "QScrollArea{ background:#1f1f1f; border:1px solid #333; }"
        "QScrollBar:vertical{ background:#2a2a2a; width:10px; }"
        "QScrollBar::handle:vertical{ background:#555; border-radius:3px; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical{ height:0; }");

    m_rowsHost = new QWidget(m_scroll);
    m_rowsHost->setStyleSheet("background:transparent;");
    m_rowsLayout = new QVBoxLayout(m_rowsHost);
    m_rowsLayout->setContentsMargins(0, 0, 0, 0);
    m_rowsLayout->setSpacing(0);
    m_rowsLayout->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    m_scroll->setWidget(m_rowsHost);
    outer->addWidget(m_scroll, 1);

    // --- Button-Leiste unten (Leeren / Speichern / Laden) -----------
    auto* btnRow = new QHBoxLayout();
    btnRow->setSpacing(4);

    auto* clearBtn = new QToolButton(m_content);
    clearBtn->setIcon(QIcon::fromTheme("edit-delete"));
    clearBtn->setToolTip(QString::fromUtf8("Alle Farbslots löschen"));
    connect(clearBtn, &QToolButton::clicked, this, [this]() {
        // Wirklich alle Slots löschen. ColorState lässt danach 0 Slots zu.
        // History-Aufzeichnung temporär aus – sonst würde jeder removeSlot
        // einen eigenen History-Eintrag erzeugen.
        m_state->setRecordHistory(false);
        m_internalChange = true;
        while (m_state->slotCount() > 0)
            m_state->removeSlot(m_state->slotCount() - 1);
        m_internalChange = false;
        m_state->setRecordHistory(true);
        recomputeRowSizesDefault();   // m_rowSizes = [0] (nur leere Zeile)
        rebuildRows();
    });
    btnRow->addWidget(clearBtn);

    btnRow->addStretch();

    auto* saveBtn = new QToolButton(m_content);
    saveBtn->setIcon(QIcon::fromTheme("document-save"));
    saveBtn->setToolTip(QString::fromUtf8("Palette als Datei sichern"));
    connect(saveBtn, &QToolButton::clicked, this, [this]() {
        const QString start = defaultPalettePath();
        const QString path = QFileDialog::getSaveFileName(
            this, QString::fromUtf8("Palette speichern"), start, "Palette (*.json)");
        if (path.isEmpty()) return;
        if (!saveToFile(path))
            QMessageBox::warning(this, QString::fromUtf8("Speichern"),
                QString::fromUtf8("Palette konnte nicht gespeichert werden."));
    });
    btnRow->addWidget(saveBtn);

    auto* loadBtn = new QToolButton(m_content);
    loadBtn->setIcon(QIcon::fromTheme("document-open"));
    loadBtn->setToolTip(QString::fromUtf8("Palette aus Datei laden"));
    connect(loadBtn, &QToolButton::clicked, this, [this]() {
        const QString start = defaultPalettePath();
        const QString path = QFileDialog::getOpenFileName(
            this, QString::fromUtf8("Palette laden"), start, "Palette (*.json)");
        if (path.isEmpty()) return;
        if (!loadFromFile(path))
            QMessageBox::warning(this, QString::fromUtf8("Laden"),
                QString::fromUtf8("Palette konnte nicht geladen werden."));
    });
    btnRow->addWidget(loadBtn);

    outer->addLayout(btnRow);

    setWidget(m_content);

    setMinimumSize(180, 180);
    setMaximumWidth(440);

    recomputeRowSizesDefault();
    rebuildRows();

    // --- ColorState-Signale -> UI aktuell halten -------------------
    // aktive Farbe geändert (z.B. durch ColorPicker): nur den einen
    // betroffenen ColorSwatch updaten, kein teures rebuildRows().
    connect(m_state, &ColorState::activeColorChanged, this, [this](const QColor&) {
        int a = m_state->activeSlot();
        if (a >= 0 && a < m_swatches.size() && m_swatches[a])
            m_swatches[a]->setColor(m_state->slotColor(a));
    });
    // aktiver Slot geändert (Shortcuts 1..8 oder Klick): Hervorhebungen
    // anpassen UND die aktive Zeile nachführen (für die Shortcut-Logik).
    connect(m_state, &ColorState::activeSlotChanged, this, [this](int) {
        int a = m_state->activeSlot();
        // m_activeRow aus dem flachen Index ableiten.
        if (a < 0) {
            m_activeRow = 0;
        } else {
            int row = 0;
            int remaining = a;
            while (row < m_rowSizes.size() && remaining >= m_rowSizes[row]) {
                remaining -= m_rowSizes[row];
                row++;
            }
            if (row < m_rowSizes.size())
                m_activeRow = row;
        }
        // Hervorhebungen updaten.
        for (int i = 0; i < m_swatches.size(); ++i)
            if (m_swatches[i]) m_swatches[i]->setActive(i == a);
    });
    // Strukturelle Änderung der Slots. Eigene Änderungen (aus
    // addSlotAtEndOfRow / removeSlotAt) sind über m_internalChange
    // markiert und übernehmen das Update selbst.
    // WICHTIG: setColor im ColorPicker feuert ebenfalls slotsChanged.
    // Dann hat sich aber nur eine Farbe geändert, nicht die Anzahl der
    // Slots. In diesem Fall dürfen wir m_rowSizes NICHT neu berechnen,
    // sonst wird die manuelle Zeilen-Struktur überschrieben und die
    // Slots verteilen sich neu. Wir vergleichen deshalb die Summe
    // der Zeilengrößen mit slotCount() und reagieren nur bei
    // Inkonsistenz (Load/Leeren) mit recomputeRowSizesDefault().
    connect(m_state, &ColorState::slotsChanged, this, [this]() {
        if (m_internalChange) return;
        int total = 0;
        for (int s : m_rowSizes) total += s;
        if (total != m_state->slotCount()) {
            recomputeRowSizesDefault();
            rebuildRows();
        }
        // sonst: nur Farbe geändert -> activeColorChanged-Callback
        // hat den ColorSwatch bereits upgedatet.
    });
}

QSize PaletteDock::sizeHint() const {
    return QSize(260, 260);
}

int PaletteDock::slotAt(int row, int col) const {
    if (row < 0 || row >= m_rowSizes.size()) return -1;
    if (col < 0 || col >= m_rowSizes[row]) return -1;
    int index = 0;
    for (int i = 0; i < row; ++i) index += m_rowSizes[i];
    return index + col;
}

// m_rowSizes aus der flachen Liste neu aufbauen (Default: m_columns
// Slots pro Zeile, plus eine leere Zeile am Schluss).
void PaletteDock::recomputeRowSizesDefault() {
    m_rowSizes.clear();
    int total = m_state->slotCount();
    while (total >= m_columns) {
        m_rowSizes.append(m_columns);
        total -= m_columns;
    }
    if (total > 0)
        m_rowSizes.append(total);
    // Immer eine leere Zeile am Schluss, damit der Nutzer jederzeit
    // eine neue Zeile anfangen kann.
    m_rowSizes.append(0);
}

// Einfüge-Index = Summe der Slots aller Zeilen 0..rowIndex.
// (Liefert für die leere Schluss-Zeile automatisch slotCount().)
int PaletteDock::computeInsertIndex(int rowIndex) const {
    int index = 0;
    for (int i = 0; i <= rowIndex && i < m_rowSizes.size(); ++i)
        index += m_rowSizes[i];
    return index;
}

// Entfern-Index = Summe der Slots vor Zeile rowIndex + Position in der Zeile.
int PaletteDock::computeRemoveIndex(int rowIndex, int posInRow) const {
    int index = 0;
    for (int i = 0; i < rowIndex && i < m_rowSizes.size(); ++i)
        index += m_rowSizes[i];
    return index + posInRow;
}

void PaletteDock::addSlotAtEndOfRow(int rowIndex) {
    if (rowIndex < 0 || rowIndex >= m_rowSizes.size()) return;

    const int insertIndex = computeInsertIndex(rowIndex);
    const bool wasLastRow = (rowIndex == m_rowSizes.size() - 1);
    const bool wasEmpty   = (m_rowSizes[rowIndex] == 0);

    // Farbe für den neuen Slot. Ist die aktive Farbe ungültig (z.B.
    // weil alle Slots gelöscht wurden), Default auf Weiß.
    QColor c = m_state->activeColor();
    if (!c.isValid()) c = Qt::white;

    // Eigenes Update: slotsChanged-Callback abschalten, damit dieser
    // nicht m_rowSizes überschreibt (Absturzursache früher).
    m_internalChange = true;
    m_state->insertSlot(insertIndex, c);

    // m_rowSizes nachführen – nur diese Zeile bekommt einen Slot dazu,
    // alle anderen Zeilen bleiben unangetastet.
    m_rowSizes[rowIndex]++;
    // War die Schluss-Zeile vorher leer, bekommt sie jetzt ihren ersten
    // Slot -> darunter eine neue leere Zeile anhängen.
    if (wasLastRow && wasEmpty)
        m_rowSizes.append(0);

    rebuildRows();
    m_internalChange = false;
}

void PaletteDock::removeSlotAt(int rowIndex, int posInRow) {
    if (rowIndex < 0 || rowIndex >= m_rowSizes.size()) return;
    if (posInRow < 0 || posInRow >= m_rowSizes[rowIndex]) return;

    const int removeIndex = computeRemoveIndex(rowIndex, posInRow);
    const bool isLastRow  = (rowIndex == m_rowSizes.size() - 1);

    m_internalChange = true;
    m_state->removeSlot(removeIndex);

    m_rowSizes[rowIndex]--;
    // Eine jetzt leere Zeile in der Mitte wird ganz entfernt
    // (nur die letzte Schluss-Zeile darf leer bleiben).
    if (m_rowSizes[rowIndex] == 0 && !isLastRow)
        m_rowSizes.removeAt(rowIndex);

    rebuildRows();
    m_internalChange = false;
}

void PaletteDock::rebuildRows() {
    // Alte Zeilen entfernen.
    QLayoutItem* it;
    while ((it = m_rowsLayout->takeAt(0)) != nullptr) {
        if (it->widget()) delete it->widget();
        delete it;
    }
    m_swatches.clear();

    const int activeSlot = m_state->activeSlot();
    int slotIndex = 0;

    for (int r = 0; r < m_rowSizes.size(); ++r) {
        const int rowSize = m_rowSizes[r];

        auto* rowWidget = new QWidget(m_rowsHost);
        // Jede Zeile hat exakt die Höhe eines Farbslots (26 px). Damit
        // liegen die Zeilen mit 0 Pixeln Abstand direkt untereinander,
        // unabhängig davon, wie viele Slots oder Zeilen es gibt.
        rowWidget->setFixedHeight(26);
        auto* rowLay = new QHBoxLayout(rowWidget);
        rowLay->setContentsMargins(0, 0, 0, 0);
        rowLay->setSpacing(0);

        for (int c = 0; c < rowSize; ++c) {
            auto* sw = new ColorSwatch(slotIndex, rowWidget);
            sw->setFixedSize(26, 26);
            sw->setColor(m_state->slotColor(slotIndex));
            sw->setActive(slotIndex == activeSlot);

            connect(sw, &ColorSwatch::clicked,
                    m_state, &ColorState::setActiveSlot);

            // Rechtsklick -> Slot DIREKT löschen (ohne Kontextmenü).
            // WICHTIG: verzögert ausführen über QTimer::singleShot(0, ...).
            // Beim Löschen wird rebuildRows() aufgerufen, das den
            // ColorSwatch zerstört, der gerade das Signal feuert – das
            // darf nicht synchron passieren (Use-after-Free -> Crash).
            sw->setContextMenuPolicy(Qt::CustomContextMenu);
            const int capturedRow = r;
            const int capturedCol = c;
            connect(sw, &QWidget::customContextMenuRequested, rowWidget,
                    [this, capturedRow, capturedCol](const QPoint&) {
                QTimer::singleShot(0, this, [this, capturedRow, capturedCol]() {
                    removeSlotAt(capturedRow, capturedCol);
                });
            });

            rowLay->addWidget(sw);
            m_swatches.append(sw);
            slotIndex++;
        }

        // Plus-Button rechtsbündig am Ende der Zeile.
        rowLay->addStretch();
        auto* plusBtn = makePlusButton(rowWidget);
        const int capturedRow = r;
        connect(plusBtn, &QToolButton::clicked, this, [this, capturedRow]() {
            addSlotAtEndOfRow(capturedRow);
        });
        rowLay->addWidget(plusBtn);

        m_rowsLayout->addWidget(rowWidget);
    }

    m_rowsLayout->addStretch();
}

QString PaletteDock::defaultPalettePath() const {
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (dir.isEmpty())
        dir = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
    QDir().mkpath(dir);
    return dir + "/palette.json";
}

bool PaletteDock::saveToFile(const QString& path) {
    QJsonArray arr;
    for (int i = 0; i < m_state->slotCount(); ++i)
        arr.append(QString(m_state->slotColor(i).name(QColor::HexArgb)));
    QJsonObject obj;
    obj["colors"] = arr;
    QJsonDocument doc(obj);

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.write(doc.toJson());
    f.close();
    return true;
}

bool PaletteDock::loadFromFile(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return false;
    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    f.close();
    if (err.error != QJsonParseError::NoError) return false;
    if (!doc.isObject()) return false;

    QJsonObject obj = doc.object();
    if (!obj.contains("colors")) return false;
    QJsonArray arr = obj.value("colors").toArray();

    QVector<QColor> loaded;
    loaded.reserve(arr.size());
    for (const QJsonValue& v : arr) {
        QColor c(v.toString());
        if (c.isValid()) loaded.append(c);
    }
    if (loaded.isEmpty()) return false;

    // Massenänderung: History-Aufzeichnung temporär aus.
    m_state->setRecordHistory(false);
    while (m_state->slotCount() > 0)
        m_state->removeSlot(m_state->slotCount() - 1);
    for (const QColor& c : loaded)
        m_state->addSlot(c);
    m_state->setRecordHistory(true);

    if (m_state->activeSlot() >= m_state->slotCount())
        m_state->setActiveSlot(m_state->slotCount() - 1);

    recomputeRowSizesDefault();
    rebuildRows();
    return true;
}
