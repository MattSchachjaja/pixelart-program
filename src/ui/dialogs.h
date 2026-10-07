#pragma once

#include <QDialog>
#include <QKeySequence>
#include <QString>
#include <QList>

class QSpinBox;
class QTableWidget;
class QKeyEvent;
class QShortcut;
class QLineEdit;
class QSlider;
class QCheckBox;
class QLabel;

// Dialog zum Einstellen einer neuen Leinwandgröße.
class ResizeCanvasDialog : public QDialog {
    Q_OBJECT
public:
    ResizeCanvasDialog(int curW, int curH, QWidget* parent = nullptr);
    int newW() const;
    int newH() const;
private:
    QSpinBox* m_wSpin;
    QSpinBox* m_hSpin;
};

// Dialog zum Bearbeiten von Ebeneneigenschaften (Name, Transparenz, Sperre).
// Aenderungen werden waehrend der Interaktion live angewendet, damit der
// Nutzer den Effekt sofort sieht. Bei Abbruch werden die urspruenglichen
// Werte wiederhergestellt.
class LayerPropertiesDialog : public QDialog {
    Q_OBJECT
public:
    LayerPropertiesDialog(const QString& name, float opacity, bool locked,
                          QWidget* parent = nullptr);

    QString newName() const;
    float    newOpacity() const;
    bool     newLocked() const;

signals:
    // Wird bei jeder Aenderung live ausgeloest, damit das Dokument
    // aktualisiert werden kann (Live-Vorschau).
    void changed(const QString& name, float opacity, bool locked);

private:
    QLineEdit* m_name;
    QSlider*   m_opacity;
    QLabel*    m_opacityVal;
    QCheckBox* m_locked;
};

// Ein konfigurierbarer Shortcut.
struct ShortcutEntry {
    QString      name;
    QKeySequence defaultKey;
    QKeySequence currentKey;
    QShortcut*   shortcut;
};

// Dialog zum Anzeigen und Anpassen aller Shortcuts.
class ShortcutDialog : public QDialog {
    Q_OBJECT
public:
    explicit ShortcutDialog(QWidget* parent = nullptr);

    void setEntries(const QList<ShortcutEntry>& entries);
    QList<ShortcutEntry> entries() const { return m_entries; }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private slots:
    void startRecording(int row, int col);
    void resetDefaults();

private:
    void handleRecordKey(QKeyEvent* e);

    QTableWidget*        m_table;
    QList<ShortcutEntry> m_entries;
    int                  m_recordingRow = -1;
};

// Modusloser Dialog zur Live-Anpassung von Sättigung, Helligkeit und
// Kontrast. Das Fenster kann frei verschoben werden, und jede
// Regleränderung wird sofort auf die Leinwand angewendet (Vorschau).
// Beim Bestätigen (OK) wird die Änderung in den Verlauf eingetragen,
// beim Abbrechen (Cancel / X) verworfen.
class AdjustmentsDialog : public QDialog {
    Q_OBJECT
public:
    explicit AdjustmentsDialog(QWidget* parent = nullptr);

    int saturation() const;
    int brightness() const;
    int contrast()   const;

public slots:
    void reset();

signals:
    // Live-Vorschau: bei jeder Regleränderung gesendet.
    void valuesChanged(int saturation, int brightness, int contrast);
    // OK geklickt → Änderungen in den Verlauf übernehmen.
    void applied();
    // Abbrechen / X → Änderungen verwerfen.
    void cancelled();

private:
    QSlider* m_sat = nullptr;
    QSlider* m_bri = nullptr;
    QSlider* m_con = nullptr;
};
