#include "dialogs.h"

#include <QSpinBox>
#include <QTableWidget>
#include <QHeaderView>
#include <QPushButton>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QDialogButtonBox>
#include <QKeyEvent>
#include <QLineEdit>
#include <QSlider>
#include <QCheckBox>

// ===================================================================
// ResizeCanvasDialog
// ===================================================================
ResizeCanvasDialog::ResizeCanvasDialog(int curW, int curH, QWidget* parent)
    : QDialog(parent)
{
    auto* form = new QFormLayout(this);

    m_wSpin = new QSpinBox(this);
    m_wSpin->setRange(1, 8192);
    m_wSpin->setValue(curW);
    m_hSpin = new QSpinBox(this);
    m_hSpin->setRange(1, 8192);
    m_hSpin->setValue(curH);

    form->addRow("Breite:", m_wSpin);
    form->addRow("Höhe:",   m_hSpin);

    auto* btns = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    btns->button(QDialogButtonBox::Ok)->setText("OK");
    btns->button(QDialogButtonBox::Cancel)->setText("Abbrechen");
    form->addRow(btns);

    connect(btns, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(btns, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

int ResizeCanvasDialog::newW() const { return m_wSpin->value(); }
int ResizeCanvasDialog::newH() const { return m_hSpin->value(); }

// ===================================================================
// LayerPropertiesDialog
// ===================================================================
LayerPropertiesDialog::LayerPropertiesDialog(const QString& name,
                                             float opacity,
                                             bool locked,
                                             QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(QString::fromUtf8("Ebeneneigenschaften"));

    auto* form = new QFormLayout(this);

    m_name = new QLineEdit(name, this);
    form->addRow(QString::fromUtf8("Name:"), m_name);

    const int pct = qRound(qBound(0.0f, opacity, 1.0f) * 100.0f);
    m_opacity = new QSlider(Qt::Horizontal, this);
    m_opacity->setRange(0, 100);
    m_opacity->setValue(pct);
    m_opacityVal = new QLabel(QString::number(pct) + "%", this);
    m_opacityVal->setMinimumWidth(40);

    auto* opRow = new QHBoxLayout();
    opRow->setContentsMargins(0, 0, 0, 0);
    opRow->addWidget(m_opacity, 1);
    opRow->addWidget(m_opacityVal);
    form->addRow(QString::fromUtf8("Transparenz:"), opRow);

    m_locked = new QCheckBox(
        QString::fromUtf8("Ebene zum Zeichnen sperren"), this);
    m_locked->setChecked(locked);
    form->addRow(QString::fromUtf8("Sperre:"), m_locked);

    auto* btns = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    btns->button(QDialogButtonBox::Ok)->setText("OK");
    btns->button(QDialogButtonBox::Cancel)->setText("Abbrechen");
    form->addRow(btns);

    connect(btns, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(btns, &QDialogButtonBox::rejected, this, &QDialog::reject);

    // Live-Aktualisierung: Aenderungen sofort anwenden.
    connect(m_opacity, &QSlider::valueChanged, this, [this](int v) {
        m_opacityVal->setText(QString::number(v) + "%");
        emit changed(m_name->text(), v / 100.0f, m_locked->isChecked());
    });
    connect(m_name, &QLineEdit::textChanged, this, [this](const QString& t) {
        emit changed(t, m_opacity->value() / 100.0f, m_locked->isChecked());
    });
    connect(m_locked, &QCheckBox::toggled, this, [this](bool b) {
        emit changed(m_name->text(), m_opacity->value() / 100.0f, b);
    });
}

QString LayerPropertiesDialog::newName()     const { return m_name->text().trimmed(); }
float    LayerPropertiesDialog::newOpacity() const { return m_opacity->value() / 100.0f; }
bool     LayerPropertiesDialog::newLocked()  const { return m_locked->isChecked(); }

// ===================================================================
// ShortcutDialog
// ===================================================================
ShortcutDialog::ShortcutDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle("Shortcuts anpassen");
    setMinimumSize(520, 420);
    setStyleSheet(
        "QDialog{ background:#2b2b2b; color:#ddd; }"
        "QLabel{ color:#ccc; }"
        "QPushButton{ background:#3a3a3a; color:#ddd; border:1px solid #555; "
        "  border-radius:3px; padding:6px 16px; }"
        "QPushButton:hover{ background:#4a4a4a; }"
        "QTableWidget{ background:#2b2b2b; color:#ddd; gridline-color:#444; "
        "  border:1px solid #444; }"
        "QTableWidget::item{ padding:4px; }"
        "QTableWidget::item:selected{ background:#3a7bd5; }"
        "QHeaderView::section{ background:#333; color:#aaa; border:1px solid #444; "
        "  padding:4px; }");

    auto* layout = new QVBoxLayout(this);
    layout->setSpacing(10);

    auto* info = new QLabel(
        "Doppelklicke auf einen Shortcut und drücke dann die gewünschte Tastenkombination.\n"
        "Mit Escape wird die Aufnahme abgebrochen.", this);
    info->setWordWrap(true);
    info->setStyleSheet("color:#999; font-style:italic;");
    layout->addWidget(info);

    m_table = new QTableWidget(this);
    m_table->setColumnCount(2);
    m_table->setHorizontalHeaderLabels({"Aktion", "Shortcut"});
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->verticalHeader()->setVisible(false);
    m_table->installEventFilter(this);
    layout->addWidget(m_table);

    auto* btnRow = new QHBoxLayout();
    auto* resetBtn = new QPushButton("Standard", this);
    connect(resetBtn, &QPushButton::clicked, this, &ShortcutDialog::resetDefaults);
    btnRow->addWidget(resetBtn);
    btnRow->addStretch();

    auto* okBtn = new QPushButton("OK", this);
    connect(okBtn, &QPushButton::clicked, this, &QDialog::accept);
    btnRow->addWidget(okBtn);

    auto* cancelBtn = new QPushButton("Abbrechen", this);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    btnRow->addWidget(cancelBtn);

    layout->addLayout(btnRow);

    m_recordingRow = -1;
}

void ShortcutDialog::setEntries(const QList<ShortcutEntry>& entries) {
    m_entries = entries;
    m_table->setRowCount(entries.size());
    for (int i = 0; i < entries.size(); ++i) {
        auto* nameItem = new QTableWidgetItem(entries[i].name);
        nameItem->setFlags(nameItem->flags() & ~Qt::ItemIsEditable);
        m_table->setItem(i, 0, nameItem);

        auto* keyItem = new QTableWidgetItem(entries[i].currentKey.toString());
        keyItem->setFlags(keyItem->flags() & ~Qt::ItemIsEditable);
        keyItem->setTextAlignment(Qt::AlignCenter);
        m_table->setItem(i, 1, keyItem);
    }
    connect(m_table, &QTableWidget::cellDoubleClicked,
            this, &ShortcutDialog::startRecording);
}

void ShortcutDialog::startRecording(int row, int col) {
    if (col != 1) return;
    // Bisherige Aufnahme abbrechen.
    if (m_recordingRow >= 0 && m_recordingRow < m_table->rowCount()) {
        m_table->item(m_recordingRow, 1)->setText(
            m_entries[m_recordingRow].currentKey.toString());
        m_table->item(m_recordingRow, 1)->setBackground(QColor(43, 43, 43));
    }
    m_recordingRow = row;
    m_table->item(row, 1)->setText("Drücke Taste...");
    m_table->item(row, 1)->setBackground(QColor(50, 50, 90));
    m_table->selectRow(row);
}

bool ShortcutDialog::eventFilter(QObject* watched, QEvent* event) {
    if (watched == m_table && event->type() == QEvent::KeyPress && m_recordingRow >= 0) {
        auto* ke = static_cast<QKeyEvent*>(event);
        handleRecordKey(ke);
        return true;
    }
    return QDialog::eventFilter(watched, event);
}

void ShortcutDialog::handleRecordKey(QKeyEvent* e) {
    int key = e->key();
    if (key == Qt::Key_Escape) {
        m_table->item(m_recordingRow, 1)->setText(
            m_entries[m_recordingRow].currentKey.toString());
        m_table->item(m_recordingRow, 1)->setBackground(QColor(43, 43, 43));
        m_recordingRow = -1;
        return;
    }
    // Reine Modifier-Tasten ignorieren.
    if (key == Qt::Key_Control || key == Qt::Key_Shift ||
        key == Qt::Key_Alt || key == Qt::Key_Meta) {
        return;
    }
    QKeySequence ks(key | int(e->modifiers()));
    m_entries[m_recordingRow].currentKey = ks;
    m_table->item(m_recordingRow, 1)->setText(ks.toString());
    m_table->item(m_recordingRow, 1)->setBackground(QColor(43, 43, 43));
    m_recordingRow = -1;
}

void ShortcutDialog::resetDefaults() {
    for (int i = 0; i < m_entries.size(); ++i) {
        m_entries[i].currentKey = m_entries[i].defaultKey;
        m_table->item(i, 1)->setText(m_entries[i].defaultKey.toString());
    }
}

// ===================================================================
// AdjustmentsDialog
// ===================================================================
AdjustmentsDialog::AdjustmentsDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(QString::fromUtf8("Farbanpassung"));
    // Moduslos: das Hauptfenster bleibt bedienbar, das Dialogfenster
    // kann frei verschoben werden und bleibt dabei sichtbar.
    setModal(false);
    setWindowFlags(Qt::Tool | Qt::WindowTitleHint |
                   Qt::WindowSystemMenuHint | Qt::WindowCloseButtonHint);
    setMinimumWidth(340);
    setStyleSheet(
        "QDialog{ background:#2b2b2b; color:#ddd; }"
        "QLabel{ color:#ccc; }"
        "QPushButton{ background:#3a3a3a; color:#ddd; border:1px solid #555;"
        "  border-radius:3px; padding:6px 16px; }"
        "QPushButton:hover{ background:#4a4a4a; }"
        "QPushButton:pressed{ background:#2f2f2f; }"
        "QSlider::groove:horizontal{ background:#222; height:6px;"
        "  border-radius:3px; }"
        "QSlider::handle:horizontal{ background:#3a7bd5; width:14px;"
        "  margin:-5px 0; border-radius:7px; }"
        "QSlider::handle:horizontal:hover{ background:#5a9bf5; }"
        "QSlider::sub-page:horizontal{ background:#3a7bd5;"
        "  border-radius:3px; }");

    auto* layout = new QVBoxLayout(this);
    layout->setSpacing(10);
    layout->setContentsMargins(12, 12, 12, 12);

    // Hilfsfunktion: eine Reglerzeile (Label + Slider + Wert) erzeugen.
    auto addRow = [this, layout](const QString& title, QSlider*& slider) {
        auto* row = new QHBoxLayout();
        row->setSpacing(8);
        auto* lbl = new QLabel(title, this);
        lbl->setMinimumWidth(90);
        slider = new QSlider(Qt::Horizontal, this);
        slider->setRange(-100, 100);
        slider->setValue(0);
        auto* val = new QLabel("0", this);
        val->setMinimumWidth(40);
        val->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        row->addWidget(lbl);
        row->addWidget(slider, 1);
        row->addWidget(val);
        layout->addLayout(row);

        // Wert-Anzeige neben dem Regler aktualisieren.
        QObject::connect(slider, &QSlider::valueChanged, val,
                         [val](int v) { val->setText(QString::number(v)); });
    };

    addRow(QString::fromUtf8("Sättigung:"),  m_sat);
    addRow(QString::fromUtf8("Helligkeit:"), m_bri);
    addRow(QString::fromUtf8("Kontrast:"),   m_con);

    // Live-Aktualisierung der Leinwand bei jeder Regleränderung.
    auto emitAll = [this]() {
        emit valuesChanged(m_sat->value(), m_bri->value(), m_con->value());
    };
    connect(m_sat, &QSlider::valueChanged, this, emitAll);
    connect(m_bri, &QSlider::valueChanged, this, emitAll);
    connect(m_con, &QSlider::valueChanged, this, emitAll);

    // Buttons: OK (übernehmen), Abbrechen (verwerfen), Zurücksetzen (auf 0).
    auto* btns = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Reset,
        this);
    btns->button(QDialogButtonBox::Ok)->setText("Übernehmen");
    btns->button(QDialogButtonBox::Cancel)->setText("Abbrechen");
    btns->button(QDialogButtonBox::Reset)->setText("Zurücksetzen");
    layout->addWidget(btns);

    connect(btns, &QDialogButtonBox::accepted, this, [this]() {
        emit applied();
        accept();
    });
    connect(btns, &QDialogButtonBox::rejected, this, [this]() {
        emit cancelled();
        reject();
    });
    connect(btns->button(QDialogButtonBox::Reset), &QPushButton::clicked,
            this, [this]() { m_sat->setValue(0); m_bri->setValue(0); m_con->setValue(0); });
}

int AdjustmentsDialog::saturation() const { return m_sat ? m_sat->value() : 0; }
int AdjustmentsDialog::brightness() const { return m_bri ? m_bri->value() : 0; }
int AdjustmentsDialog::contrast()   const { return m_con ? m_con->value() : 0; }

void AdjustmentsDialog::reset() {
    if (m_sat) m_sat->setValue(0);
    if (m_bri) m_bri->setValue(0);
    if (m_con) m_con->setValue(0);
}
