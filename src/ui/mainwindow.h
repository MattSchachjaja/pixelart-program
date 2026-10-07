#pragma once

#include <QMainWindow>
#include <QList>
#include <QHash>
#include <QPointF>
#include <QImage>
#include <QString>
#include <QMetaObject>

#include "dialogs.h" // ShortcutEntry

class QEvent;
class QAction;
class QMoveEvent;
class QResizeEvent;
class QDockWidget;
class QWidget;
class QToolBar;
class QHBoxLayout;
class QScrollArea;
class QLabel;
class QPushButton;

class Document;
class History;
class ColorState;
class ToolManager;
class CanvasWidget;
class LayerDock;
class HistoryDock;
class ColorDock;
class PaletteDock;
class ToolsDock;
class ITool;
class AdjustmentsDialog;

// Eine geöffnete Datei: ihr Document, ihr Verlauf und ihr Anzeigename.
// Die Session gehört MainWindow; beim Wechseln wird nur der aktive Zeiger
// umgeschwenkt (Canvas/Docks/Tools), nicht die Objekte neu erzeugt.
struct DocumentSession {
    Document* doc = nullptr;
    History*  history = nullptr;
    QString   name;
    // Signal-Verbindung für Thumbnail-Aktualisierung.
    QMetaObject::Connection changedConn;
};

// Ein Thumbnail in der Preview-Leiste (Container + Bild-Label + Name-Label).
// Halte Referenzen auf die inneren Widgets, damit Bild und Hervorhebung
// aktualisiert werden können, OHNE die Widgets neu aufbauen zu müssen.
struct PreviewThumb {
    QWidget* container = nullptr;
    QLabel*  image     = nullptr;
    QLabel*  name      = nullptr;
};

// Hauptfenster mit Menueleiste, Docks und Vollbild-Umschaltung.
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

protected:
    void changeEvent(QEvent* e) override;
    void moveEvent(QMoveEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void buildMenu();
    void buildToolBar();
    void buildStatusBar();          // untere Leiste: Zoom, Koordinaten, Reset
    void buildPreviewBar();         // Leiste unter dem ToolBar für Datei-Thumbnails
    void buildToolOptions(ITool* tool);
    void toggleFullscreen();
    void beginNewDocument(int w, int h);  // neue leere Datei als eigene Session
    void openFile();                      // Datei öffnen → neue Session
    void deleteSelectionPixels();   // Delete / Strg+X: Pixel in Auswahl löschen
    void copySelection();           // Strg+C: Auswahl ins Clipboard
    void pasteClipboard();          // Strg+V: aus Clipboard eininfügen
    void duplicateSelection();      // Strg+D: Copy + Paste in einem Schritt
    void commitPaste();             // schwebenden Buffer übernehmen
    void openAdjustments();         // Farbanpassung öffnen (Sättigung/Helligkeit/Kontrast)
    void applyAdjustmentsLive(int saturation, int brightness, int contrast);
    void commitAdjustments();       // OK: Änderung in den Verlauf eintragen
    void revertAdjustments();       // Abbrechen: Original wiederherstellen
    void constrainDock(QDockWidget* dock);
    void addEdgeGrips(QDockWidget* dock);
    void repositionGrips(QWidget* dock);
    void addMinimizeButton(QDockWidget* dock);   // eigene Titelleiste mit Min-Button
    void storeDockRelativePositions();   // relative Dock-Positionen erfassen
    void repositionDocksRelative();      // Docks proportional zur Fenstergröße setzen

    // --- Multi-Document (Session-Verwaltung) ---
    void switchToSession(int index);     // aktive Session wechseln
    void closeSession(int index);        // Datei/Session schließen (X-Button)
    void refreshActivePointers();        // Canvas/Docks/Tools auf aktive Session schwenken
    void updatePreviewThumbnails();         // alle Thumbnails neu aufbauen (nur bei Datei hinzugefügt/entfernt)
    void updatePreviewThumbnail(int index); // einzelnes Thumbnail-Bild aktualisieren (Live-Update)
    void updatePreviewHighlights();         // nur aktive Hervorhebung ändern – löscht KEINE Widgets
    void regenerateThumbnailImage(int index); // Composite-Bild eines Thumbnails neu erzeugen

    Document*     m_doc;
    History*      m_history;
    ColorState*   m_colors;
    ToolManager*  m_tools;
    CanvasWidget* m_canvas;
    LayerDock*    m_layerDock;
    HistoryDock*  m_historyDock;
    ColorDock*    m_colorDock;
    PaletteDock*  m_paletteDock;
    ToolsDock*    m_toolsDock;
    QAction*      m_fullscreenAct = nullptr;
    QList<ShortcutEntry> m_scInfos;

    // --- Farbanpassung (modusloser Dialog mit Live-Vorschau) ---
    // Während der Dialog offen ist, enthält m_adjustOriginal die
    // unbearbeiteten Pixel des Arbeitsbereichs (ganze Ebene oder Auswahl).
    // Jede Regleränderung stellt erst das Original wieder her und wendet
    // dann die neuen Werte an. Bei OK wird ein StrokeCommand gepusht,
    // bei Abbrechen das Original zurückgeschrieben.
    AdjustmentsDialog* m_adjustmentsDlg = nullptr;
    QImage  m_adjustOriginal;
    QImage  m_adjustMask;        // Alpha-8-Maske (Ausschnitt) – nur bei Auswahl
    QRect   m_adjustRect;        // Arbeitsbereich in Canvas-Koordinaten
    int     m_adjustLayerIndex = -1;
    bool    m_adjustHasMask = false;

    // Interne Zwischenablage (für "Einfügen an ursprünglicher Stelle").
    QImage        m_internalCopy;
    QPoint        m_internalOrigin;
    bool          m_hasInternalCopy = false;
    QHash<QObject*, QList<QWidget*>> m_grips;

    // Relative Dock-Positionen (Bruchteil der Fenstergröße), damit die
    // Fenster beim Vergößern/Verkleinern mitwandern.
    QHash<QWidget*, QPointF> m_dockRelPos;
    bool m_repositioning = false;   // verhindert Speichern bei programmatischem Move

    // Werkzeug-Einstellleiste (unter der Menüleiste).
    QToolBar*     m_toolBar = nullptr;
    QWidget*      m_toolOptionsHost = nullptr;
    QHBoxLayout*  m_toolOptionsLayout = nullptr;

    // --- Statusleiste (unterer Rand) ---
    // Zeigt den aktuellen Zoom, die Mauskoordinaten in Dokument-Pixeln
    // und einen Button, der die Ansicht zurücksetzt (100% + zentriert).
    QLabel*       m_statusZoom = nullptr;
    QLabel*       m_statusPos  = nullptr;
    QPushButton*  m_statusReset = nullptr;

    // --- Multi-Document (Preview-Leiste) ---
    // Die Leiste sitzt direkt unter der Werkzeug-Einstellleiste und enthält
    // pro geöffneter Datei einen klickbaren Thumbnail (wie in Paint.NET).
    QWidget*      m_previewHost = nullptr;
    QHBoxLayout*  m_previewLayout = nullptr;
    QList<DocumentSession> m_sessions;
    int           m_activeSession = 0;
    // Map von Thumbnail-Widget → Session-Index (für Klicks).
    QHash<QWidget*, int> m_previewWidgets;
    // Map von Close-Button → Session-Index (für X-Klicks).
    QHash<QWidget*, int> m_closeButtons;
    // Direkter Zugriff auf die Thumbnails (Index = Session-Index).
    QList<PreviewThumb>  m_thumbs;
};
