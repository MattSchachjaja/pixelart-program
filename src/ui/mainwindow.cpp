#include "mainwindow.h"

#include "config.h"
#include "document.h"
#include "history.h"
#include "colorstate.h"
#include "colorutils.h"
#include "tools.h"
#include "commands.h"
#include "canvaswidget.h"
#include "layerdock.h"
#include "historydock.h"
#include "colorwidgets.h"
#include "palettedock.h"
#include "toolsdock.h"

#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QShortcut>
#include <QFileDialog>
#include <QMessageBox>
#include <QStandardPaths>
#include <QImage>
#include <QEvent>
#include <QDockWidget>
#include <QMoveEvent>
#include <QResizeEvent>
#include <QMouseEvent>
#include <QWidget>
#include <QToolBar>
#include <QSlider>
#include <QLabel>
#include <QHBoxLayout>
#include <QToolButton>
#include <QButtonGroup>
#include <QCheckBox>
#include <QPainter>
#include <QRegion>
#include <QApplication>
#include <QClipboard>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QFileInfo>
#include <QStatusBar>
#include <cmath>


// ===================================================================
// EdgeGrip — unsichtbarer Griffrand zum leichten Vergroessern
// ===================================================================
namespace {

// Breite der Greifzone in Pixeln.
// 8 Pixel gewaehlt, weil der Dock-Rahmen ca. 2 Pixel
// darueberlegt — effektiv bleiben ~6 Pixel klickbar.
constexpr int GRIP = 8;

// Bitflags: welche Kanten dieser Grip steuert.
// KEINE oberen Kanten — die Titelleiste behandelt das Verschieben.
enum Edge {
    Left        = 1,
    Right       = 2,
    Bottom      = 4,
    BottomLeft  = Bottom | Left,
    BottomRight = Bottom | Right
};

class EdgeGrip : public QWidget {
public:
    EdgeGrip(int edges, QDockWidget* dock)
        : QWidget(), m_edges(edges), m_dock(dock)
    {
        // Unsichtbar, aber faengt Maus-Events ab.
        setAttribute(Qt::WA_NoSystemBackground);
        setAttribute(Qt::WA_TranslucentBackground);
        setMouseTracking(true);

        // Cursor passend zur Kante.
        switch (edges) {
        case Left:  case Right:  setCursor(Qt::SizeHorCursor);  break;
        case Bottom:            setCursor(Qt::SizeVerCursor);  break;
        case BottomLeft:        setCursor(Qt::SizeFDiagCursor); break;
        case BottomRight:       setCursor(Qt::SizeBDiagCursor); break;
        }
    }

protected:
    void mousePressEvent(QMouseEvent* e) override {
        if (e->button() == Qt::LeftButton) {
            m_dragStart = e->globalPos();
            m_startGeo  = m_dock->geometry();
        }
    }

    void mouseMoveEvent(QMouseEvent* e) override {
        if (!(e->buttons() & Qt::LeftButton)) return;

        QPoint delta = e->globalPos() - m_dragStart;
        QRect  geo   = m_startGeo;

        if (m_edges & Left)   geo.setLeft  (geo.left()   + delta.x());
        if (m_edges & Right)  geo.setRight (geo.right()  + delta.x());
        if (m_edges & Bottom) geo.setBottom(geo.bottom() + delta.y());

        // Min/Max-Groesse des Docks respektieren.
        geo.setSize(geo.size().expandedTo(m_dock->minimumSize()));
        if (m_dock->maximumSize() != QSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX))
            geo.setSize(geo.size().boundedTo(m_dock->maximumSize()));

        m_dock->setGeometry(geo);
    }

private:
    int          m_edges;
    QDockWidget* m_dock;
    QPoint       m_dragStart;
    QRect        m_startGeo;
};

// ===================================================================
// DockTitleBar — eigene Titelleiste mit Einklappen-Button
// ===================================================================
// Ersetzt die native Titelleiste jedes Docks durch eine eigene, die
// zusätzlich einen Einklappen-Button enthält. Beim Klick wird der
// Inhalt des Docks versteckt und die Fensterhöhe auf die Titelleiste
// reduziert – nur die Leiste bleibt sichtbar (Collapsing).
// Beim erneuten Klick wird das Fenster wieder vollständig ausgeklappt.
// Das Verschieben des Docks muss selbst implementiert werden, da Qt
// bei gesetztem TitleBarWidget das Moving-Verhalten nicht mehr liefert.
class DockTitleBar : public QWidget {
public:
    static constexpr int kBarHeight = 26;

    explicit DockTitleBar(QDockWidget* dock)
        : QWidget(dock), m_dock(dock)
    {
        setFixedHeight(kBarHeight);
        setAutoFillBackground(true);
        setCursor(Qt::SizeAllCursor);
        setStyleSheet(
            "DockTitleBar { background:#2b2b2b; border:none; }"
            "QLabel { color:#ddd; font-weight:bold; background:transparent; }");

        auto* lay = new QHBoxLayout(this);
        lay->setContentsMargins(8, 2, 4, 2);
        lay->setSpacing(4);

        m_title = new QLabel(dock->windowTitle(), this);
        lay->addWidget(m_title);
        lay->addStretch();

        m_collapseBtn = new QToolButton(this);
        m_collapseBtn->setText(QString::fromUtf8("\xE2\x96\xBC"));  // ▼ einklappen
        m_collapseBtn->setToolTip(QString::fromUtf8("Fenster einklappen"));
        m_collapseBtn->setFixedSize(22, 20);
        m_collapseBtn->setAutoRaise(true);
        m_collapseBtn->setFocusPolicy(Qt::NoFocus);
        m_collapseBtn->setCursor(Qt::ArrowCursor);
        m_collapseBtn->setStyleSheet(
            "QToolButton { background:#3a3a3a; color:#ddd; border:none;"
            "              border-radius:3px; font-weight:bold; padding:0; }"
            "QToolButton:hover { background:#4a4a4a; }"
            "QToolButton:pressed { background:#2f2f2f; }");
        connect(m_collapseBtn, &QToolButton::clicked, this, [this]() { toggleCollapsed(); });
        lay->addWidget(m_collapseBtn);

        connect(dock, &QDockWidget::windowTitleChanged, m_title, &QLabel::setText);
        dock->setTitleBarWidget(this);
    }

    QSize sizeHint() const override { return QSize(0, kBarHeight); }
    QSize minimumSizeHint() const override { return QSize(0, kBarHeight); }

    bool collapsed() const { return m_collapsed; }

protected:
    void mousePressEvent(QMouseEvent* e) override {
        if (e->button() == Qt::LeftButton && !m_collapseBtn->geometry().contains(e->pos())) {
            m_dragging = true;
            m_dragOffset = e->globalPos() - m_dock->frameGeometry().topLeft();
            e->accept();
            return;
        }
        QWidget::mousePressEvent(e);
    }

    void mouseMoveEvent(QMouseEvent* e) override {
        if (m_dragging && (e->buttons() & Qt::LeftButton) && m_dock->isFloating()) {
            m_dock->move(e->globalPos() - m_dragOffset);
            e->accept();
            return;
        }
        QWidget::mouseMoveEvent(e);
    }

    void mouseReleaseEvent(QMouseEvent* e) override {
        m_dragging = false;
        QWidget::mouseReleaseEvent(e);
    }

private:
    void toggleCollapsed() {
        if (m_collapsed) {
            // ---------- Ausklappen ----------
            // 1) Constraints vollständig freigeben
            // 2) Größe wiederherstellen (Breite + Höhe)
            // 3) Inhalt wieder anzeigen – REIHENFOLGE ist wichtig, sonst
            //    käme es beim sichtbar Werden des Inhalts zu einem
            //    Resize mit falscher Basis-Größe.
            m_dock->setMinimumSize(0, 0);
            m_dock->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
            if (m_savedSize.isValid())
                m_dock->resize(m_savedSize);
            if (m_dock->widget()) m_dock->widget()->setVisible(true);
            m_collapsed = false;
            m_collapseBtn->setText(QString::fromUtf8("\xE2\x96\xBC")); // ▼
            m_collapseBtn->setToolTip(QString::fromUtf8("Fenster einklappen"));
        } else {
            // ---------- Einklappen ----------
            // Originalgröße merken, Inhalt verstecken, Fenster auf
            // Titelleisten-Größe reduzieren – mit der ORIGINAL-Breite,
            // damit die Titelleiste exakt so breit bleibt wie das
            // Fenster vorher war.
            m_savedSize = m_dock->size();
            if (m_dock->widget()) m_dock->widget()->setVisible(false);
            m_dock->setFixedSize(m_savedSize.width(), kBarHeight);
            m_collapsed = true;
            m_collapseBtn->setText(QString::fromUtf8("\xE2\x96\xB2")); // ▲ ausklappen
            m_collapseBtn->setToolTip(QString::fromUtf8("Fenster ausklappen"));
        }
    }

    QDockWidget* m_dock;
    QLabel*      m_title;
    QToolButton* m_collapseBtn;
    bool         m_collapsed = false;
    bool         m_dragging   = false;
    QPoint       m_dragOffset;
    QSize        m_savedSize;
};

} // anonymous namespace


// ===================================================================
// MainWindow
// ===================================================================

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle("MiniPaint");
    resize(1400, 900);

    m_colors = new ColorState(this);

    // --- Multi-Document: erste Session anlegen ---
    {
        DocumentSession s;
        s.doc = new Document(DOC_W, DOC_H, this);
        s.history = new History(this);
        s.name = "Unbenannt";
        m_sessions.append(s);
    }
    m_activeSession = 0;
    m_doc     = m_sessions.first().doc;
    m_history = m_sessions.first().history;

    m_tools   = new ToolManager(m_doc, m_colors, m_history, this);

    m_canvas = new CanvasWidget(m_doc, m_tools, this);
    setCentralWidget(m_canvas);

    // Delete-Taste direkt vom Canvas: zuverlässiger als QShortcut bei
    // QOpenGLWidget. Löscht die Pixel in der aktuellen Auswahl.
    connect(m_canvas, &CanvasWidget::deleteRequested,
            this, &MainWindow::deleteSelectionPixels);

    m_layerDock   = new LayerDock(m_doc, m_history, this);
    m_historyDock = new HistoryDock(m_history, this);
    m_colorDock   = new ColorDock(m_colors, this);
    m_paletteDock = new PaletteDock(m_colors, this);
    m_toolsDock   = new ToolsDock(m_tools, this);

    // Verschieber: Rechtsklick hebt Auswahl auf + wechselt zum Auswahlwerkzeug.
    m_tools->mover()->onRightClickSwitchToSelect = [this]() {
        m_toolsDock->selectTool(2);   // Rechteckauswahl ist Index 2
    };

    // Alle Docks als freie, ungebundene Fenster.
    // Kein addDockWidget - damit verwalten Qt sie nicht.
    m_toolsDock->setAllowedAreas(Qt::NoDockWidgetArea);
    m_colorDock->setAllowedAreas(Qt::NoDockWidgetArea);
    m_paletteDock->setAllowedAreas(Qt::NoDockWidgetArea);
    m_layerDock->setAllowedAreas(Qt::NoDockWidgetArea);
    m_historyDock->setAllowedAreas(Qt::NoDockWidgetArea);

    m_toolsDock->setFloating(true);
    m_colorDock->setFloating(true);
    m_paletteDock->setFloating(true);
    m_layerDock->setFloating(true);
    m_historyDock->setFloating(true);

    // Unsichtbare Greifzonen an den Raendern jedes Docks,
    // damit man die Fenster leichter gross/klein ziehen kann.
    // Nur Links, Rechts, Unten und untere Ecken — KEINE oberen
    // Kanten, damit die Titelleiste ausschliesslich zum Verschieben dient.
    addEdgeGrips(m_toolsDock);
    addEdgeGrips(m_colorDock);
    addEdgeGrips(m_paletteDock);
    addEdgeGrips(m_layerDock);
    addEdgeGrips(m_historyDock);

    // Eigene Titelleiste mit Minimieren-Button für jedes Dock.
    addMinimizeButton(m_toolsDock);
    addMinimizeButton(m_colorDock);
    addMinimizeButton(m_paletteDock);
    addMinimizeButton(m_layerDock);
    addMinimizeButton(m_historyDock);

    // Startpositionen setzen (nachdem Fenster sichtbar ist).
    QMetaObject::invokeMethod(this, [this]() {
        QRect mg = geometry();
        int gap = 10;
        int dockW = 260;

        // Y-Start: unter Menüleiste + Werkzeugleiste + Preview-Leiste.
        int topInset = 0;
        if (auto* mb = menuBar()) topInset += mb->height();
        if (m_toolBar)            topInset += m_toolBar->height();
        if (m_previewHost)        topInset += m_previewHost->height();
        topInset += gap;

        // --- Linke Seite: Farben oben, Palette darunter ---------------------
        int leftX = mg.left() + gap;
        int colorTop = mg.top() + topInset;

        // ColorDock: sizeHint als Start, Aspect-Ratio korrigiert die Höhe.
        m_colorDock->resize(dockW, m_colorDock->sizeHint().height());
        int colorH = m_colorDock->height();
        m_colorDock->move(leftX, colorTop);

        // Palette: direkt darunter, nimmt den Rest der linken Seite ein.
        int palTop = colorTop + colorH + gap;
        int palH   = mg.bottom() - palTop - gap;
        m_paletteDock->resize(dockW, qMax(palH, m_paletteDock->minimumSizeHint().height()));
        m_paletteDock->move(leftX, palTop);

        // --- Rechte Seite: Ebenen, Verlauf, Werkzeuge -----------------------
        int rightX = mg.right() - dockW - gap;

        // Ebenen oben.
        int layerH = qMax(m_layerDock->sizeHint().height(), 300);
        m_layerDock->resize(dockW, layerH);
        m_layerDock->move(rightX, mg.top() + topInset);

        // Verlauf direkt darunter.
        int histTop = mg.top() + topInset + layerH + gap;
        int histH   = m_historyDock->sizeHint().height();
        m_historyDock->resize(dockW, histH);
        m_historyDock->move(rightX, histTop);

        // Werkzeuge direkt darunter, als 3x3-Gitter.
        // 3 Buttons * 40px + 2*4 spacing + 2*6 margin = 140 Inhalt
        // + Titelleiste (26px) + etwas Luft = ~175px
        int toolsW = 140;
        int toolsContentH = 3 * 40 + 2 * 4 + 2 * 6;  // = 140
        int toolsH = toolsContentH + DockTitleBar::kBarHeight + 6;
        int toolsTop = histTop + histH + gap;
        // Werkzeugfenster rechtsbündig ausrichten (gleich rechter Rand wie die anderen).
        int toolsX = mg.right() - toolsW - gap;
        m_toolsDock->resize(toolsW, toolsH);
        m_toolsDock->move(toolsX, toolsTop);

        // Grips nach dem ersten Show/Resize neu positionieren.
        for (auto it = m_grips.constBegin(); it != m_grips.constEnd(); ++it) {
            auto* w = qobject_cast<QWidget*>(it.key());
            if (w) repositionGrips(w);
        }
        // Relative Positionen erfassen, damit die Fenster beim Resize mitwandern.
        storeDockRelativePositions();
    }, Qt::QueuedConnection);

    // Event-Filter installieren: verhindert, dass Docks
    // aus dem Hauptfenster rausbewegt werden.
    m_toolsDock->installEventFilter(this);
    m_colorDock->installEventFilter(this);
    m_paletteDock->installEventFilter(this);
    m_layerDock->installEventFilter(this);
    m_historyDock->installEventFilter(this);

    buildMenu();
    buildToolBar();
    buildPreviewBar();
    buildStatusBar();

    // Erste Session mit allen Komponenten verbinden (insbesondere das
    // changed-Signal für die Live-Thumbnail-Aktualisierung).
    refreshActivePointers();

    // Werkzeug-Einstellungen aktualisieren, sobald ein Werkzeug
    // ausgewaehlt wird.
    connect(m_toolsDock, &ToolsDock::toolActivated,
            this, &MainWindow::buildToolOptions);
    // Startzustand: Bleistift ist aktiv (hat keine Einstellungen).
    buildToolOptions(m_tools->active());

    // Shortcuts zentral registrieren (im Dialog aenderbar).
    auto regSC = [this](const QString& name, const QKeySequence& key) -> QShortcut* {
        auto* sc = new QShortcut(key, this);
        m_scInfos.push_back({name, key, key, sc});
        return sc;
    };

    // Undo/Redo immer auf die AKTIVE Session wirken lassen (Lambda),
    // da m_history beim Session-Wechsel umschwenkt.
    auto* undoSC = regSC("Rückgängig", QKeySequence("Ctrl+Z"));
    connect(undoSC, &QShortcut::activated, this, [this]() { m_history->undo(); });

    auto* redoSC = regSC("Wiederholen", QKeySequence("Ctrl+Shift+Z"));
    connect(redoSC, &QShortcut::activated, this, [this]() { m_history->redo(); });

    auto* redoSC2 = regSC("Wiederholen (alt)", QKeySequence("Ctrl+Y"));
    connect(redoSC2, &QShortcut::activated, this, [this]() { m_history->redo(); });

    for (int i = 0; i < 8; ++i) {
        auto* sc = regSC("Farb-Slot " + QString::number(i + 1),
                         QKeySequence(QString::number(i + 1)));
        connect(sc, &QShortcut::activated, this, [this, i]() {
            // Shortcuts 1..8 wirken auf die aktuell aktive Zeile im
            // Palette-Dock (die Zeile des zuletzt angeklickten Slots).
            const int row = m_paletteDock->activeRow();
            const int slot = m_paletteDock->slotAt(row, i);
            if (slot >= 0) m_colors->setActiveSlot(slot);
        });
    }

    auto* fsSC = regSC("Vollbild", QKeySequence("F11"));
    connect(fsSC, &QShortcut::activated, this, &MainWindow::toggleFullscreen);

    // Delete / Strg+X: Pixel in der Auswahl löschen.
    auto* delSC = regSC("Auswahl löschen", QKeySequence(QKeySequence::Delete));
    connect(delSC, &QShortcut::activated, this, &MainWindow::deleteSelectionPixels);
    auto* cutSC = regSC("Auswahl ausschneiden", QKeySequence("Ctrl+X"));
    connect(cutSC, &QShortcut::activated, this, &MainWindow::deleteSelectionPixels);

    // Auswahl aufheben (Default: Leertaste). Zusätzlich QShortcut + Canvas-
    // Handler, weil QShortcut bei QOpenGLWidget nicht immer zuverlässig ist.
    // Während der Texteingabe (TextTool) nicht auslösen.
    auto* clearSelSC = regSC("Auswahl aufheben", QKeySequence(Qt::Key_Space));
    m_canvas->setClearSelectionKey(clearSelSC->key());
    connect(clearSelSC, &QShortcut::activated, this, [this]() {
        auto* t = dynamic_cast<TextTool*>(m_tools ? m_tools->active() : nullptr);
        if (t && t->isEditing()) return;
        if (m_doc && m_doc->hasSelection()) m_doc->clearSelection();
    });
    connect(m_canvas, &CanvasWidget::clearSelectionRequested, this, [this]() {
        if (m_doc && m_doc->hasSelection()) m_doc->clearSelection();
    });

    // Copy / Paste
    auto* copySC = regSC("Kopieren", QKeySequence("Ctrl+C"));
    connect(copySC, &QShortcut::activated, this, &MainWindow::copySelection);
    auto* pasteSC = regSC("Einfügen", QKeySequence("Ctrl+V"));
    connect(pasteSC, &QShortcut::activated, this, &MainWindow::pasteClipboard);

    // Duplizieren: Copy + Paste in einem Schritt (schwebender Buffer
    // liegt nach demselben Tastendruck sofort zum Verschieben bereit).
    auto* dupSC = regSC("Duplizieren", QKeySequence("Ctrl+D"));
    connect(dupSC, &QShortcut::activated, this, &MainWindow::duplicateSelection);

    // Wenn ein ANDERES Programm den Clipboard ändert, ist unsere interne
    // Kopie nicht mehr gültig → Flag zurücksetzen.
    connect(QApplication::clipboard(), &QClipboard::changed,
            this, [this]() { m_hasInternalCopy = false; });

    // Werkzeug-Shortcuts (vom Nutzer setzbar).
    // ACHTUNG: Diese Namen müssen mit den Tool-Namen in ToolsDock übereinstimmen!
    static const char* toolNames[] = {
        "Pixelstift",
        "Pinsel",
        "Rechteckauswahl",
        "Zauberstab",
        "Füllen",
        "Verschieber",
        "Pipette",
        "Text",
        "Formen"
    };
    static const char* toolDefaultKeys[] = {
        "P",   // Pixelstift
        "B",   // Pinsel
        "R",   // Rechteckauswahl
        "W",   // Zauberstab
        "F",   // Füllen
        "V",   // Verschieber
        "I",   // Pipette
        "T",   // Text
        "S"    // Formen
    };
    // Anzahl der Tools aus dem Dock (oder aus ToolManager) abfragen.
    int toolCount = m_toolsDock->toolCount();
    for (int i = 0; i < toolCount && i < (int)(sizeof(toolNames)/sizeof(toolNames[0])); ++i) {
        const char* defKey = (i < (int)(sizeof(toolDefaultKeys)/sizeof(toolDefaultKeys[0])))
                             ? toolDefaultKeys[i] : "";
        auto* sc = regSC(toolNames[i], QKeySequence(defKey));
        int idx = i;
        connect(sc, &QShortcut::activated, this, [this, idx]() {
            m_toolsDock->selectTool(idx);
        });
    }
}

// ===================================================================
// Edge-Grips: unsichtbare Vergroesserungs-Zonen an den Raendern
// ===================================================================

void MainWindow::addEdgeGrips(QDockWidget* dock) {
    // 5 Grips: Links, Rechts, Unten, Links-Unten, Rechts-Unten.
    // KEINE oberen Grips — die Titelleiste ist nur fuer Verschieben.
    auto* gL  = new EdgeGrip(Left,        dock);
    auto* gR  = new EdgeGrip(Right,       dock);
    auto* gB  = new EdgeGrip(Bottom,      dock);
    auto* gBL = new EdgeGrip(BottomLeft,  dock);
    auto* gBR = new EdgeGrip(BottomRight, dock);

    // Grips als Kinder des DOCK-Widgets.
    // Sie ueberlagern die aeussersten Pixel des Fensterrands
    // und faengen Maus-Events ab, ohne das Layout zu aendern.
    gL->setParent(dock);
    gR->setParent(dock);
    gB->setParent(dock);
    gBL->setParent(dock);
    gBR->setParent(dock);

    // Grips merken fuer repositionGrips().
    m_grips[dock] = { gL, gR, gB, gBL, gBR };
}

void MainWindow::addMinimizeButton(QDockWidget* dock) {
    // Erzeugt die eigene Titelleiste mit Min-Button.
    // Der Konstruktor von DockTitleBar ruft dock->setTitleBarWidget auf.
    new DockTitleBar(dock);
    // Window-Manager-Rahmen entfernen, damit NUR unsere eigene Titelleiste
    // sichtbar ist (sonst hätte man doppelte Titel-Leisten).
    dock->setWindowFlags(dock->windowFlags() | Qt::FramelessWindowHint);
}

void MainWindow::repositionGrips(QWidget* dock) {
    auto it = m_grips.find(dock);
    if (it == m_grips.end()) return;

    int w = dock->width();
    int h = dock->height();
    auto& g = *it;

    // Höhe der eigenen Titelleiste ermitteln, damit die seitlichen Grips
    // erst UNTERHALB der Titelleiste beginnen und den Min-Button nicht
    // überdecken. Hat das Dock keine eigene TitleBar (Rückgabe = 0),
    // gehen die Grips wie bisher über die volle Höhe.
    int tbH = 0;
    auto* dw = qobject_cast<QDockWidget*>(dock);
    if (dw && dw->titleBarWidget())
        tbH = dw->titleBarWidget()->height();

    // 0=links, 1=rechts, 2=unten, 3=links-unten, 4=rechts-unten
    g[0]->setGeometry(0,        tbH,     GRIP, h - tbH);  // links (unter Titel)
    g[1]->setGeometry(w - GRIP, tbH,     GRIP, h - tbH);  // rechts (unter Titel)
    g[2]->setGeometry(0,        h - GRIP, w,    GRIP);    // unten (volle Breite)
    g[3]->setGeometry(0,        h - GRIP, GRIP, GRIP);    // links-unten
    g[4]->setGeometry(w - GRIP, h - GRIP, GRIP, GRIP);    // rechts-unten

    // WICHTIG: Nach jeder Neupositionierung wieder nach oben bringen.
    // Das interne Layout des Docks wuerde die Grips sonst
    // unter sich begraben.
    for (auto* grip : g) {
        grip->show();
        grip->raise();
    }
}

// ===================================================================
// Dock-Einschraenkung: Docks bleiben immer im Hauptfenster
// ===================================================================

void MainWindow::constrainDock(QDockWidget* dock) {
    QRect mainGeo = geometry();
    QRect dockGeo = dock->frameGeometry();

    int x = dockGeo.x();
    int y = dockGeo.y();

    // Dock innerhalb des Hauptfensters halten.
    x = qBound(mainGeo.left(), x, mainGeo.right() - dockGeo.width());
    y = qBound(mainGeo.top(), y, mainGeo.bottom() - dockGeo.height());

    // Falls der Dock breiter/hoeher als das Hauptfenster ist,
    // zumindest die linke obere Ecke anpassen.
    if (dockGeo.width() > mainGeo.width())
        x = mainGeo.left();
    if (dockGeo.height() > mainGeo.height())
        y = mainGeo.top();

    if (x != dockGeo.x() || y != dockGeo.y())
        dock->move(x, y);
}

// Erfasst die aktuelle Position jedes Docks als Bruchteil der
// Hauptfenstergröße (0..1). So lassen sich die Fenster beim Resize
// proportional mitbewegen.
void MainWindow::storeDockRelativePositions() {
    QRect mg = geometry();
    if (mg.width() <= 0 || mg.height() <= 0) return;
    auto store = [&](QDockWidget* d) {
        QPointF rel;
        rel.setX(qreal(d->pos().x() - mg.left()) / mg.width());
        rel.setY(qreal(d->pos().y() - mg.top()) / mg.height());
        m_dockRelPos[d] = rel;
    };
    store(m_toolsDock);
    store(m_colorDock);
    store(m_paletteDock);
    store(m_layerDock);
    store(m_historyDock);
}

// Setzt die Docks an ihre relativen Positionen bezogen auf die aktuelle
// Hauptfenstergröße – sie wandern also beim Vergrößern/Verkleinern mit.
void MainWindow::repositionDocksRelative() {
    QRect mg = geometry();
    if (mg.width() <= 0 || mg.height() <= 0) return;
    m_repositioning = true;   // Speichern bei programmatischem Move unterdrücken
    auto apply = [&](QDockWidget* d) {
        auto it = m_dockRelPos.find(d);
        if (it == m_dockRelPos.end()) return;
        int nx = mg.left() + int(it->x() * mg.width());
        int ny = mg.top()  + int(it->y() * mg.height());
        d->move(nx, ny);
        constrainDock(d);
    };
    apply(m_toolsDock);
    apply(m_colorDock);
    apply(m_paletteDock);
    apply(m_layerDock);
    apply(m_historyDock);
    m_repositioning = false;
    for (auto it = m_grips.constBegin(); it != m_grips.constEnd(); ++it) {
        auto* w = qobject_cast<QWidget*>(it.key());
        if (w) repositionGrips(w);
    }
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
    // Dock wird bewegt -> im Hauptfenster einschraenken.
    if (event->type() == QEvent::Move) {
        auto* dock = qobject_cast<QDockWidget*>(watched);
        if (dock && dock->isFloating()) {
            constrainDock(dock);
            // Relative Position aktualisieren, damit der Nutzer-Verschub
            // beim nächsten Resize erhalten bleibt – aber NICHT während
            // programmatischer Verschiebung (sonst Verzerrung beim Clamplen).
            if (!m_repositioning)
                storeDockRelativePositions();
        }
    }

    // Dock wird vergroessert/verkleinert -> Grips neu positionieren.
    if (event->type() == QEvent::Resize) {
        auto* w = qobject_cast<QWidget*>(watched);
        if (w && m_grips.contains(w))
            repositionGrips(w);
    }

    // Preview-Thumbnail angeklickt -> zugehörige Session aktivieren.
    // Der Wechsel wird verzögert ausgeführt (QueuedConnection), damit wir
    // nicht innerhalb des Event-Filters die gesamte UI umschwenken.
    // switchToSession() ändert nur die Hervorhebung (updatePreviewHighlights),
    // zerstört KEINE Thumbnails mehr – das ursprüngliche Widget bleibt also
    // auch dann gültig, wenn der Klick (Press+Release) noch nicht
    // abgeschlossen ist. Das ist der Fix für den früheren Absturz beim
    // Wechseln zwischen Dateien in der Preview-Leiste.
    if (event->type() == QEvent::MouseButtonPress) {
        auto* w = qobject_cast<QWidget*>(watched);
        auto* me = static_cast<QMouseEvent*>(event);
        if (w && me->button() == Qt::LeftButton) {
            // Close-Button (X) hat Vorrang vor dem Session-Wechsel.
            if (m_closeButtons.contains(w)) {
                int idx = m_closeButtons.value(w, -1);
                if (idx >= 0) {
                    QMetaObject::invokeMethod(this, [this, idx]() {
                        closeSession(idx);
                    }, Qt::QueuedConnection);
                }
                return true;  // Event verarbeitet -> kein Session-Wechsel.
            }
            if (m_previewWidgets.contains(w)) {
                int idx = m_previewWidgets.value(w, -1);
                if (idx >= 0) {
                    QMetaObject::invokeMethod(this, [this, idx]() {
                        switchToSession(idx);
                    }, Qt::QueuedConnection);
                }
            }
        }
    }

    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::moveEvent(QMoveEvent* e) {
    // Wenn das Hauptfenster bewegt wird, alle Docks mitnehmen.
    QPoint delta = e->pos() - e->oldPos();
    m_toolsDock->move(m_toolsDock->pos() + delta);
    m_colorDock->move(m_colorDock->pos() + delta);
    m_paletteDock->move(m_paletteDock->pos() + delta);
    m_layerDock->move(m_layerDock->pos() + delta);
    m_historyDock->move(m_historyDock->pos() + delta);
    QMainWindow::moveEvent(e);
}

void MainWindow::resizeEvent(QResizeEvent* e) {
    // Wenn das Hauptfenster vergroessert/verkleinert wird,
    // Docks proportional mitbewegen (sie bleiben an ihrer relativen Stelle).
    Q_UNUSED(e);
    repositionDocksRelative();

    // Preview-Leiste (schwebendes Child-Widget) direkt unter dem ToolBar
    // positionieren. Die Breite wird an den tatsächlichen Inhalt
    // (Thumbnails) angepasst, damit der transparente Bereich keine
    // Maus-Events auf dem darunterliegenden Canvas blockiert.
    if (m_previewHost) {
        int top = 0;
        if (auto* mb = menuBar()) top += mb->height();
        if (m_toolBar)             top += m_toolBar->height();
        // Explizite Breitenberechnung: jedes Thumbnail ist 90px breit,
        // plus spacing (6) plus äußere margins (8+8). So wird verhindert,
        // dass Thumbnails überlappen, wenn sizeHint() noch nicht aktuell ist.
        const int thumbW = 90;
        const int spacing = 6;
        const int margin = 16;
        int neededW = margin + m_thumbs.size() * (thumbW + spacing);
        int w = qBound(0, neededW, width());
        m_previewHost->setGeometry(0, top, w, m_previewHost->height());
        m_previewHost->raise();
    }

    QMainWindow::resizeEvent(e);
}

// ===================================================================
// Fenster-Ereignisse
// ===================================================================

void MainWindow::changeEvent(QEvent* e) {
    if (e->type() == QEvent::WindowStateChange) {
        if (m_fullscreenAct) m_fullscreenAct->setChecked(isFullScreen());

        if (isMinimized()) {
            // Alle Docks verstecken wenn minimiert.
            m_toolsDock->hide();
            m_colorDock->hide();
            m_paletteDock->hide();
            m_layerDock->hide();
            m_historyDock->hide();
        } else {
            // Alle Docks wieder zeigen wenn wiederhergestellt.
            m_toolsDock->show();
            m_colorDock->show();
            m_paletteDock->show();
            m_layerDock->show();
            m_historyDock->show();
        }
    }
    QMainWindow::changeEvent(e);
}

// ===================================================================
// Menü
// ===================================================================

void MainWindow::buildMenu() {
    // --- Datei ---
    auto* fileMenu = menuBar()->addMenu("&Datei");

    auto* newAct = fileMenu->addAction("&Neu", QKeySequence::New);
    connect(newAct, &QAction::triggered, this, [this]() {
        ResizeCanvasDialog dlg(DOC_W, DOC_H, this);
        dlg.setWindowTitle("Neues Bild");
        if (dlg.exec() != QDialog::Accepted) return;
        beginNewDocument(dlg.newW(), dlg.newH());
    });

    auto* openAct = fileMenu->addAction("Ö&ffnen...", QKeySequence::Open);
    connect(openAct, &QAction::triggered, this, [this]() { openFile(); });

    auto* saveAct = fileMenu->addAction("&Speichern unter...", QKeySequence::Save);
    connect(saveAct, &QAction::triggered, this, [this]() {
        const QString start = QStandardPaths::writableLocation(
            QStandardPaths::PicturesLocation);
        const QString path = QFileDialog::getSaveFileName(
            this, "Bild speichern", start + "/unbenannt.png",
            "PNG (*.png);;JPEG (*.jpg);;BMP (*.bmp)");
        if (path.isEmpty()) return;
        QImage out = m_doc->composite();
        if (!out.save(path)) {
            QMessageBox::warning(this, "Speichern",
                "Bild konnte nicht gespeichert werden:\n" + path);
        }
    });

    fileMenu->addSeparator();

    auto* exitAct = fileMenu->addAction("&Beenden", QKeySequence::Quit);
    connect(exitAct, &QAction::triggered, this, &QMainWindow::close);

    // --- Bild ---
    auto* imgMenu = menuBar()->addMenu("&Bild");
    auto* resizeAct = imgMenu->addAction("Leinwandgröße...", tr("Ctrl+R"));
    connect(resizeAct, &QAction::triggered, this, [this]() {
        ResizeCanvasDialog dlg(m_doc->width(), m_doc->height(), this);
        if (dlg.exec() != QDialog::Accepted) return;
        m_history->push(new ResizeCanvasCommand(m_doc, dlg.newW(), dlg.newH()));
        m_canvas->resetView();
    });

    auto* adjustAct = imgMenu->addAction(QString::fromUtf8("Farbanpassung..."));
    connect(adjustAct, &QAction::triggered, this, &MainWindow::openAdjustments);

    // --- Vollbild (direkt klickbar) ---
    m_fullscreenAct = menuBar()->addAction("&Vollbild");
    m_fullscreenAct->setCheckable(true);
    connect(m_fullscreenAct, &QAction::triggered, this, &MainWindow::toggleFullscreen);

    // --- Shortcuts anpassen (direkt klickbar) ---
    auto* scAct = menuBar()->addAction("Shortcuts anpassen...");
    connect(scAct, &QAction::triggered, this, [this]() {
        QList<ShortcutEntry> entries;
        for (auto& info : m_scInfos)
            entries.push_back({info.name, info.defaultKey, info.currentKey, info.shortcut});
        ShortcutDialog dlg(this);
        dlg.setEntries(entries);
        if (dlg.exec() == QDialog::Accepted) {
            auto result = dlg.entries();
            for (int i = 0; i < m_scInfos.size(); ++i) {
                m_scInfos[i].currentKey = result[i].currentKey;
                m_scInfos[i].shortcut->setKey(result[i].currentKey);
                // Canvas-seitig ausgewertete Tasten synchron halten.
                if (m_scInfos[i].name == "Auswahl aufheben")
                    m_canvas->setClearSelectionKey(result[i].currentKey);
            }
        }
    });
}

void MainWindow::toggleFullscreen() {
    if (isFullScreen())
        showNormal();
    else
        showFullScreen();
}

// Delete / Strg+X: löscht die Pixel in der aktuellen Auswahl auf der
// aktiven Ebene (macht sie transparent). Ohne Auswahl passiert nichts.
void MainWindow::deleteSelectionPixels() {
    if (!m_doc || !m_doc->activeLayer() || !m_doc->hasSelection()) return;
    Layer* lay = m_doc->activeLayer();
    QRect r = m_doc->selectionBounds().intersected(
                  QRect(0, 0, m_doc->width(), m_doc->height()));
    if (r.isEmpty()) return;

    QImage before = lay->image.copy(r);

    // Pixel in der Auswahl transparent machen (DestinationOut mit voller Deckkraft).
    QPainter p(&lay->image);
    p.setClipRegion(m_doc->selectionRegion());   // nur innerhalb der Auswahl
    p.setCompositionMode(QPainter::CompositionMode_DestinationOut);
    p.fillRect(r, QColor(0, 0, 0, 255));
    p.end();

    QImage after = lay->image.copy(r);
    m_history->push(new StrokeCommand(m_doc, m_doc->activeLayerIndex(), r, before, after,
                                      "Auswahl löschen"));

    // Nach dem Löschen der Pixel auch die Auswahl selbst entfernen
    // (wie in Paint.NET/Photoshop: Delete löscht Inhalt UND Auswahlmarke).
    m_doc->clearSelection();

    emit m_doc->changed();
}

// Strg+C: kopiert die Pixel in der Auswahl (vom Komposit) ins Clipboard.
// Ohne Auswahl wird das gesamte Bild kopiert.
void MainWindow::copySelection() {
    if (!m_doc) return;
    QRect r = m_doc->hasSelection()
        ? m_doc->selectionBounds().intersected(QRect(0, 0, m_doc->width(), m_doc->height()))
        : QRect(0, 0, m_doc->width(), m_doc->height());
    if (r.isEmpty()) return;

    QImage comp = m_doc->composite();
    QImage sub = comp.copy(r).convertToFormat(QImage::Format_ARGB32);

    // Auswahlmaske anwenden: nur ausgewählte Pixel behalten, alle anderen
    // transparent machen. So werden bei mehreren getrennten Auswahlen nur die
    // echten Auswahlbereiche kopiert, nicht das Rechteck dazwischen.
    if (m_doc->hasSelection()) {
        QImage maskSub = m_doc->selectionMask().copy(r);
        QPainter mp(&sub);
        mp.setCompositionMode(QPainter::CompositionMode_DestinationIn);
        mp.drawImage(0, 0, maskSub);
        mp.end();
    }

    // Interne Kopie + Ursprungsposition merken (für "Einfügen am selben Ort").
    m_internalCopy = sub;
    m_internalOrigin = r.topLeft();
    m_hasInternalCopy = true;

    // Ins System-Clipboard legen (für Programm-übergreifendes Einfügen).
    // Signale blockieren, damit der changed()-Handler unser Flag nicht zurücksetzt.
    auto* clip = QApplication::clipboard();
    clip->blockSignals(true);
    clip->setImage(sub);
    clip->blockSignals(false);
}

// Strg+V: fügt das Bild als schwebenden Buffer über der Ebene ein (Duplikat).
// Der Buffer liegt über dem Original und kann verschoben werden, OHNE die
// Stelle darunter zu zerstören. Erst beim Übernehmen (Enter/Werkzeugwechsel)
// wird er auf die Ebene gestempelt.
//  - interne Kopie (per Strg+C hier erzeugt) → genau an der Ursprungsstelle
//  - externes Bild (aus anderem Programm)    → zentriert auf der Leinwand
void MainWindow::pasteClipboard() {
    if (!m_doc) return;

    // Bereits schwebender Buffer wird VOR dem neuen Einfügen auf die Ebene
    // gestempelt (mit Verlauf). Ohne diesen Schritt würde der neue Buffer
    // den alten einfach überschreiben – die vorherige Einfügung wäre weg.
    // Vermieden: beim allerersten Paste (kein Buffer vorhanden) entsteht
    // kein leerer Verlaufsschritt, da commitPaste() früh zurückkehrt.
    if (m_doc->hasPasteBuffer()) commitPaste();

    QImage img;
    QPoint pos;

    // Interne Kopie: Bild und Position direkt aus dem Speicher verwenden,
    // ohne den Clipboard-Rundtrippt (vermeidet Format-Konvertierungsprobleme).
    if (m_hasInternalCopy && !m_internalCopy.isNull()) {
        img = m_internalCopy.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        pos = m_internalOrigin;
    } else {
        // Externes Bild: aus dem System-Clipboard lesen und zentrieren.
        img = QApplication::clipboard()->image();
        if (img.isNull()) return;
        img = img.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        pos = QPoint((m_doc->width()  - img.width())  / 2,
                     (m_doc->height() - img.height()) / 2);
    }

    // Schwebenden Buffer setzen (liegt über der Ebene, Ebene bleibt unangetastet).
    m_doc->setPasteBuffer(img, pos);

    // Auswahl auf die tatsächliche Form des eingefügten Bildes setzen
    // (nur wo das Bild sichtbare Pixel hat). So stimmt die Auswahl auch bei
    // mehreren getrennten Bereichen exakt mit dem Buffer überein.
    {
        QImage selMask(m_doc->width(), m_doc->height(), QImage::Format_Alpha8);
        selMask.fill(0);
        QRect bufRect = QRect(pos, img.size())
            .intersected(QRect(0, 0, m_doc->width(), m_doc->height()));
        if (!bufRect.isEmpty()) {
            uchar* bits = selMask.bits();
            const int mbpl = selMask.bytesPerLine();
            for (int y = bufRect.top(); y <= bufRect.bottom(); ++y) {
                for (int x = bufRect.left(); x <= bufRect.right(); ++x) {
                    const QRgb px = img.pixel(x - pos.x(), y - pos.y());
                    if (qAlpha(px) > 0)
                        bits[y * mbpl + x] = 255;
                }
            }
        }
        m_doc->setSelection(selMask);
    }

    // Verschieber im Paste-Modus aktivieren (zeigt optisch "Pixel" als aktiv).
    if (auto* mv = m_tools->mover()) mv->setMoveMode(MoverTool::Paste);
    m_toolsDock->selectTool(5);   // Verschieber ist Index 5
}

// Strg+D: Dupliziert die aktuelle Auswahl, indem erst kopiert und
// unmittelbar danach eingefügt wird. Das Ergebnis liegt als schwebender
// Buffer über der Ebene und kann direkt verschoben werden.
void MainWindow::duplicateSelection() {
    copySelection();
    pasteClipboard();
}

// Übernimmt den schwebenden Paste-Buffer auf die aktive Ebene (mit Verlauf).
void MainWindow::commitPaste() {
    if (!m_doc || !m_doc->hasPasteBuffer() || !m_doc->activeLayer()) return;
    Layer* lay = m_doc->activeLayer();
    QRect full(0, 0, m_doc->width(), m_doc->height());
    QImage before = lay->image.copy(full);
    m_doc->stampPasteBuffer();
    QImage after = lay->image.copy(full);
    m_history->push(new StrokeCommand(m_doc, m_doc->activeLayerIndex(), full, before, after,
                                      "Einfügen"));
    m_doc->clearPasteBuffer();
    if (auto* mv = m_tools->mover()) mv->setMoveMode(MoverTool::Pixels);  // Default nach Paste
    emit m_doc->changed();
}

// ===================================================================
// Farbanpassung (Sättigung / Helligkeit / Kontrast) mit Live-Vorschau
// ===================================================================

// Öffnet den moduslosen Farbanpassungs-Dialog. Ist eine Auswahl aktiv,
// wird nur innerhalb der Auswahl angepasst, sonst die ganze aktive Ebene.
// Der Originalzustand wird gesichert, damit die Vorschau jederzeit
// zurückgesetzt werden kann.
void MainWindow::openAdjustments() {
    if (!m_doc || !m_doc->activeLayer()) return;

    // Bereits offener Dialog in den Vordergrund holen (nicht zweimal öffnen).
    if (m_adjustmentsDlg) {
        m_adjustmentsDlg->show();
        m_adjustmentsDlg->raise();
        m_adjustmentsDlg->activateWindow();
        return;
    }

    // Schwebenden Paste-Buffer vorher übernehmen (sonst Konflikt).
    if (m_doc->hasPasteBuffer()) commitPaste();

    Layer* lay = m_doc->activeLayer();
    if (!lay) return;

    // Arbeitsbereich: Auswahl (beschnitten auf die Leinwand) oder ganze Ebene.
    QRect rect = m_doc->hasSelection()
        ? m_doc->selectionBounds().intersected(QRect(0, 0, m_doc->width(), m_doc->height()))
        : QRect(0, 0, m_doc->width(), m_doc->height());
    if (rect.isEmpty()) return;

    m_adjustRect       = rect;
    m_adjustLayerIndex = m_doc->activeLayerIndex();
    m_adjustOriginal   = lay->image.copy(rect);
    m_adjustHasMask    = m_doc->hasSelection();
    if (m_adjustHasMask) m_adjustMask = m_doc->selectionMask().copy(rect);

    auto* dlg = new AdjustmentsDialog(this);
    m_adjustmentsDlg = dlg;
    // Beim Schließen (X / ESC) wie Abbrechen behandeln – wichtig, falls
    // der Nutzer das Fenster ohne Button schließt.
    dlg->setAttribute(Qt::WA_DeleteOnClose);

    connect(dlg, &AdjustmentsDialog::valuesChanged,
            this, &MainWindow::applyAdjustmentsLive);
    connect(dlg, &AdjustmentsDialog::applied,
            this, &MainWindow::commitAdjustments);
    connect(dlg, &AdjustmentsDialog::cancelled,
            this, &MainWindow::revertAdjustments);
    connect(dlg, &QObject::destroyed, this, [this]() {
        m_adjustmentsDlg = nullptr;
    });

    dlg->show();
    dlg->raise();
    dlg->activateWindow();
}

// Wendet die aktuellen Reglerwerte live an. Vor jeder Berechnung wird
// das Original zurückgeschrieben, damit sich die Werte nicht aufschaukeln.
void MainWindow::applyAdjustmentsLive(int sat, int bri, int con) {
    if (!m_doc || m_adjustOriginal.isNull()) return;
    Layer* lay = m_doc->layer(m_adjustLayerIndex);
    if (!lay) return;

    // 1) Original zurückkopieren – saubere Basis für die Berechnung.
    {
        QPainter p(&lay->image);
        p.setCompositionMode(QPainter::CompositionMode_Source);
        p.drawImage(m_adjustRect.topLeft(), m_adjustOriginal);
        p.end();
    }

    // 2) Anpassung berechnen und ggf. mit der Auswahlmaske mischen.
    QImage adjusted = adjustImage(m_adjustOriginal, sat, bri, con);
    QImage finalImg = (m_adjustHasMask && !m_adjustMask.isNull())
        ? blendWithMask(m_adjustOriginal, adjusted, m_adjustMask)
        : adjusted;

    // 3) Ergebnis in die Ebene schreiben.
    {
        QPainter p(&lay->image);
        p.setCompositionMode(QPainter::CompositionMode_Source);
        p.drawImage(m_adjustRect.topLeft(), finalImg);
        p.end();
    }

    emit m_doc->changed();
}

// OK: Die aktuelle (angepasste) Fassung als Verlaufsschritt ablegen.
// StrokeCommand sichert before/after und macht die Änderung damit rückgängig.
void MainWindow::commitAdjustments() {
    if (!m_doc || m_adjustOriginal.isNull()) return;
    Layer* lay = m_doc->layer(m_adjustLayerIndex);
    if (!lay) return;

    QImage after = lay->image.copy(m_adjustRect);
    m_history->push(new StrokeCommand(m_doc, m_adjustLayerIndex, m_adjustRect,
                                      m_adjustOriginal, after,
                                      QString::fromUtf8("Farbanpassung")));

    // State zurücksetzen (Dialog wird gleich vom Loader zerstört).
    m_adjustOriginal = QImage();
    m_adjustMask     = QImage();
    m_adjustRect     = QRect();
    m_adjustLayerIndex = -1;
    m_adjustHasMask  = false;
}

// Abbrechen: Original wiederherstellen, keine Verlaufs-Eintragung.
void MainWindow::revertAdjustments() {
    if (!m_doc || m_adjustOriginal.isNull()) return;
    Layer* lay = m_doc->layer(m_adjustLayerIndex);
    if (!lay) return;

    {
        QPainter p(&lay->image);
        p.setCompositionMode(QPainter::CompositionMode_Source);
        p.drawImage(m_adjustRect.topLeft(), m_adjustOriginal);
        p.end();
    }
    emit m_doc->changed();

    m_adjustOriginal = QImage();
    m_adjustMask     = QImage();
    m_adjustRect     = QRect();
    m_adjustLayerIndex = -1;
    m_adjustHasMask  = false;
}

// ===================================================================
// Werkzeug-Einstellleiste (Direkt unter der Menüleiste)
// ===================================================================

void MainWindow::buildToolBar() {
    m_toolBar = new QToolBar("Werkzeug-Einstellungen", this);
    m_toolBar->setObjectName("ToolBar");
    m_toolBar->setMovable(false);
    m_toolBar->setFloatable(false);
    m_toolBar->setAllowedAreas(Qt::TopToolBarArea);
    addToolBar(Qt::TopToolBarArea, m_toolBar);

    // Ein Container-Widget nimmt die Einstellungen auf.
    m_toolOptionsHost = new QWidget();
    m_toolOptionsLayout = new QHBoxLayout(m_toolOptionsHost);
    m_toolOptionsLayout->setContentsMargins(8, 4, 8, 4);
    m_toolOptionsLayout->setSpacing(10);
    m_toolBar->addWidget(m_toolOptionsHost);

    // Feste Hoehe: die Leiste bleibt bei jedem Werkzeug gleich gross,
    // sodass alle Einstellungen immer sichtbar bleiben und nix springt.
    m_toolBar->setFixedHeight(48);
}

// ===================================================================
// Statusleiste (unterer Rand): Zoom, Mauskoordinaten, Reset-Button
// ===================================================================

void MainWindow::buildStatusBar() {
    auto* sb = statusBar();
    sb->setSizeGripEnabled(true);
    sb->setStyleSheet(
        "QStatusBar{ background:#2b2b2b; color:#ccc; }"
        "QStatusBar::item{ border:none; }"
        "QLabel{ color:#ccc; padding:0 8px; }"
        "QPushButton{ background:#3a3a3a; color:#ddd; border:1px solid #555;"
        "  border-radius:3px; padding:2px 12px; }"
        "QPushButton:hover{ background:#4a4a4a; }"
        "QPushButton:pressed{ background:#2f2f2f; }");

    // Zoom-Anzeige (z. B. "100%").
    m_statusZoom = new QLabel("100%", this);
    m_statusZoom->setMinimumWidth(60);
    m_statusZoom->setAlignment(Qt::AlignCenter);
    m_statusZoom->setToolTip("Aktueller Zoom");
    sb->addWidget(m_statusZoom);

    // Trenner / Koordinaten-Anzeige (z. B. "X: 0  Y: 0").
    m_statusPos = new QLabel("X: 0  Y: 0", this);
    m_statusPos->setMinimumWidth(130);
    m_statusPos->setToolTip("Mausposition auf der Leinwand");
    sb->addWidget(m_statusPos);

    // Reset-Button ganz rechts: setzt die Ansicht auf 100% und zentriert.
    m_statusReset = new QPushButton("Ansicht zurücksetzen", this);
    m_statusReset->setToolTip("Zoom auf 100% setzen und die Leinwand zentrieren");
    sb->addPermanentWidget(m_statusReset);

    // Zoom-Änderungen (Mausrad, Reset, Fit) anzeigen.
    connect(m_canvas, &CanvasWidget::zoomChanged, this, [this](float z) {
        const int pct = qRound(z * 100.0f);
        if (qAbs(z * 100.0f - pct) < 0.01f)
            m_statusZoom->setText(QString::number(pct) + "%");
        else
            m_statusZoom->setText(QString::number(z * 100.0f, 'f', 2) + "%");
    });

    // Mauskoordinaten live anzeigen (in Dokument-Pixeln, ganzzahlig).
    connect(m_canvas, &CanvasWidget::mousePosChanged, this, [this](const QPointF& p) {
        m_statusPos->setText(QString("X: %1  Y: %2")
            .arg(int(std::floor(p.x())))
            .arg(int(std::floor(p.y()))));
    });

    // Button setzt die Ansicht zurück (zentriert + 100%).
    connect(m_statusReset, &QPushButton::clicked, this, [this]() {
        m_canvas->resetView();
    });
}

void MainWindow::buildToolOptions(ITool* tool) {
    if (!m_toolOptionsLayout) return;

    // Alte Einstellungen entfernen.
    QLayoutItem* it;
    while ((it = m_toolOptionsLayout->takeAt(0)) != nullptr) {
        if (it->widget()) delete it->widget();
        delete it;
    }

    if (!tool) return;

    // Schwebenden Paste-Buffer übernehmen, sobald ein Werkzeug aktiv wird,
    // das NICHT der Verschieber im Paste-Modus ist.
    if (m_doc->hasPasteBuffer()) {
        auto* mv = dynamic_cast<MoverTool*>(tool);
        if (!mv || mv->moveMode() != MoverTool::Paste)
            commitPaste();
    }

    // Werkzeugname links als Hinweis.
    auto* nameLabel = new QLabel(tool->name() + ":", m_toolOptionsHost);
    nameLabel->setStyleSheet("font-weight:bold; color:#3a7bd5;");
    m_toolOptionsLayout->addWidget(nameLabel);

    if (auto* pencil = dynamic_cast<PencilTool*>(tool)) {
        auto* pp = new QCheckBox("Pixel-Perfekt", m_toolOptionsHost);
        pp->setToolTip("Verhindert Doppelpixel und L-Formen beim Zeichnen.");
        pp->setChecked(pencil->pixelPerfect());
        connect(pp, &QCheckBox::toggled, this, [pencil](bool c) {
            pencil->setPixelPerfect(c);
        });
        m_toolOptionsLayout->addWidget(pp);
    } else if (auto* brush = dynamic_cast<BrushTool*>(tool)) {
        // --- Größe ---
        m_toolOptionsLayout->addWidget(new QLabel("Größe:", m_toolOptionsHost));
        auto* sizeSlider = new QSlider(Qt::Horizontal, m_toolOptionsHost);
        sizeSlider->setRange(1, 200);
        sizeSlider->setValue(brush->size());
        sizeSlider->setMinimumWidth(120);
        auto* sizeValue = new QLabel(QString::number(brush->size()), m_toolOptionsHost);
        sizeValue->setMinimumWidth(30);
        m_toolOptionsLayout->addWidget(sizeSlider);
        m_toolOptionsLayout->addWidget(sizeValue);
        connect(sizeSlider, &QSlider::valueChanged, this, [brush, sizeValue](int v) {
            brush->setSize(v);
            sizeValue->setText(QString::number(v));
        });

        // --- Weichheit ---
        m_toolOptionsLayout->addWidget(new QLabel("Weichheit:", m_toolOptionsHost));
        auto* softSlider = new QSlider(Qt::Horizontal, m_toolOptionsHost);
        softSlider->setRange(0, 100);
        softSlider->setValue(brush->softness());
        softSlider->setMinimumWidth(120);
        auto* softValue = new QLabel(QString::number(brush->softness()), m_toolOptionsHost);
        softValue->setMinimumWidth(30);
        m_toolOptionsLayout->addWidget(softSlider);
        m_toolOptionsLayout->addWidget(softValue);
        connect(softSlider, &QSlider::valueChanged, this, [brush, softValue](int v) {
            brush->setSoftness(v);
            softValue->setText(QString::number(v));
        });

        // --- Dither ---
        auto* ditherCheck = new QCheckBox("Dither", m_toolOptionsHost);
        ditherCheck->setToolTip(
            "Zeichnet nur harte Pixel.\n"
            "Weichheit steuert die Dither-Intensität:\n"
            "  Wenig Weichheit → wenig Dither-Effekt\n"
            "  Volle Weichheit → reiner Dither mit Lücken");
        ditherCheck->setChecked(brush->dither());
        connect(ditherCheck, &QCheckBox::toggled, this, [brush](bool checked) {
            brush->setDither(checked);
        });
        m_toolOptionsLayout->addWidget(ditherCheck);
    } else if (auto* magic = dynamic_cast<MagicWandTool*>(tool)) {
        // Modus: Zusammenhängend / Alle
        auto* mg = new QButtonGroup(m_toolOptionsHost);
        mg->setExclusive(true);
        const struct { const char* label; MagicWandTool::MatchMode mode; } mmodes[] = {
            {"Zusammenh\xC3\xA4ngend", MagicWandTool::Contiguous},
            {"Alle", MagicWandTool::All}
        };
        for (auto& mm : mmodes) {
            auto* b = new QToolButton(m_toolOptionsHost);
            b->setText(QString::fromUtf8(mm.label));
            b->setCheckable(true);
            b->setStyleSheet("QToolButton{ padding:2px 8px; }"
                             "QToolButton:checked{ background:#3a7bd5; color:white; }");
            mg->addButton(b);
            m_toolOptionsLayout->addWidget(b);
            if (mm.mode == magic->matchMode()) b->setChecked(true);
            connect(b, &QToolButton::clicked, this, [magic, mm]() { magic->setMatchMode(mm.mode); });
        }
        // Empfindlichkeit
        m_toolOptionsLayout->addWidget(new QLabel("Empfindlichkeit:", m_toolOptionsHost));
        auto* tolSlider = new QSlider(Qt::Horizontal, m_toolOptionsHost);
        tolSlider->setRange(0, 255);
        tolSlider->setValue(magic->tolerance());
        tolSlider->setMinimumWidth(120);
        auto* tolValue = new QLabel(QString::number(magic->tolerance()), m_toolOptionsHost);
        tolValue->setMinimumWidth(30);
        m_toolOptionsLayout->addWidget(tolSlider);
        m_toolOptionsLayout->addWidget(tolValue);
        connect(tolSlider, &QSlider::valueChanged, this, [magic, tolValue](int v) {
            magic->setTolerance(v);
            tolValue->setText(QString::number(v));
        });
        auto* cross = new QCheckBox("Ebenen\xC3\xBCbergreifend", m_toolOptionsHost);
        cross->setChecked(magic->crossLayer());
        connect(cross, &QCheckBox::toggled, this, [magic](bool c) { magic->setCrossLayer(c); });
        m_toolOptionsLayout->addWidget(cross);
    } else if (auto* fill = dynamic_cast<FillTool*>(tool)) {
        auto* mg = new QButtonGroup(m_toolOptionsHost);
        mg->setExclusive(true);
        const struct { const char* label; FillTool::MatchMode mode; } fmodes[] = {
            {"Zusammenh\xC3\xA4ngend", FillTool::Contiguous},
            {"Alle", FillTool::All}
        };
        for (auto& fm : fmodes) {
            auto* b = new QToolButton(m_toolOptionsHost);
            b->setText(QString::fromUtf8(fm.label));
            b->setCheckable(true);
            b->setStyleSheet("QToolButton{ padding:2px 8px; }"
                             "QToolButton:checked{ background:#3a7bd5; color:white; }");
            mg->addButton(b);
            m_toolOptionsLayout->addWidget(b);
            if (fm.mode == fill->matchMode()) b->setChecked(true);
            connect(b, &QToolButton::clicked, this, [fill, fm]() { fill->setMatchMode(fm.mode); });
        }
        m_toolOptionsLayout->addWidget(new QLabel("Empfindlichkeit:", m_toolOptionsHost));
        auto* tolSlider = new QSlider(Qt::Horizontal, m_toolOptionsHost);
        tolSlider->setRange(0, 255);
        tolSlider->setValue(fill->tolerance());
        tolSlider->setMinimumWidth(120);
        auto* tolValue = new QLabel(QString::number(fill->tolerance()), m_toolOptionsHost);
        tolValue->setMinimumWidth(30);
        m_toolOptionsLayout->addWidget(tolSlider);
        m_toolOptionsLayout->addWidget(tolValue);
        connect(tolSlider, &QSlider::valueChanged, this, [fill, tolValue](int v) {
            fill->setTolerance(v);
            tolValue->setText(QString::number(v));
        });
        auto* cross = new QCheckBox("Ebenen\xC3\xBCbergreifend", m_toolOptionsHost);
        cross->setChecked(fill->crossLayer());
        connect(cross, &QCheckBox::toggled, this, [fill](bool c) { fill->setCrossLayer(c); });
        m_toolOptionsLayout->addWidget(cross);
    } else if (auto* sel = dynamic_cast<RectSelectTool*>(tool)) {
        // Auswahl-Form: Rechteck / Ellipse / Lasso.
        m_toolOptionsLayout->addWidget(new QLabel("Form:", m_toolOptionsHost));
        auto* modeGroup = new QButtonGroup(m_toolOptionsHost);
        modeGroup->setExclusive(true);
        const struct { const char* icon; RectSelectTool::SelectMode mode; } modes[] = {
            {"\xE2\x96\xAD", RectSelectTool::Rectangle},
            {"\xE2\x97\x8B", RectSelectTool::Ellipse},
            {"\xE2\x8C\x92", RectSelectTool::Lasso}
        };
        const char* const tips[3] = { "Rechteckauswahl", "Kreisauswahl", "Lassoauswahl" };
        for (int i = 0; i < 3; ++i) {
            const auto& m = modes[i];
            auto* btn = new QToolButton(m_toolOptionsHost);
            btn->setText(QString::fromUtf8(m.icon));
            btn->setToolTip(QString::fromUtf8(tips[i]));
            btn->setCheckable(true);
            btn->setMinimumSize(32, 32);
            btn->setStyleSheet(
                "QToolButton{ background:#2a2a2a; color:#ddd; border:1px solid #444;"
                "  border-radius:3px; font-size:16px; }"
                "QToolButton:checked{ background:#3a7bd5; color:white; }");
            modeGroup->addButton(btn);
            m_toolOptionsLayout->addWidget(btn, 0, Qt::AlignVCenter);
            if (m.mode == sel->selectMode()) btn->setChecked(true);
            connect(btn, &QToolButton::clicked, this, [sel, m]() {
                sel->setSelectMode(m.mode);
            });
        }
    } else if (auto* form = dynamic_cast<FormTool*>(tool)) {
        m_toolOptionsLayout->addWidget(new QLabel("Form:", m_toolOptionsHost));
        auto* modeGroup = new QButtonGroup(m_toolOptionsHost);
        modeGroup->setExclusive(true);
        const struct { const char* icon; const char* tip; FormTool::ShapeMode mode; } modes[] = {
            {"\xE2\x96\xAD", "Viereck", FormTool::Rectangle},
            {"\xE2\x97\x8B", "Kreis",   FormTool::Ellipse},
            {"\xE2\x96\xB3", "Dreieck", FormTool::Triangle},
            {"\xE2\x95\xB1", "Linie",   FormTool::Line}
        };
        for (auto& m : modes) {
            auto* btn = new QToolButton(m_toolOptionsHost);
            btn->setText(QString::fromUtf8(m.icon));
            btn->setToolTip(QString::fromUtf8(m.tip));
            btn->setCheckable(true);
            btn->setMinimumSize(32, 32);
            btn->setStyleSheet(
                "QToolButton{ background:#2a2a2a; color:#ddd; border:1px solid #444;"
                "  border-radius:3px; font-size:16px; }"
                "QToolButton:checked{ background:#3a7bd5; color:white; }");
            modeGroup->addButton(btn);
            m_toolOptionsLayout->addWidget(btn);
            if (m.mode == form->shapeMode()) btn->setChecked(true);
            connect(btn, &QToolButton::clicked, this, [form, m]() {
                form->setShapeMode(m.mode);
            });
        }
        // Gefüllt / nur Rahmen
        auto* fillCheck = new QCheckBox("Gef\xC3\xBCllt", m_toolOptionsHost);
        fillCheck->setChecked(form->filled());
        connect(fillCheck, &QCheckBox::toggled, this, [form](bool c) {
            form->setFilled(c);
        });
        m_toolOptionsLayout->addWidget(fillCheck);
        // Rahmendicke
        m_toolOptionsLayout->addWidget(new QLabel("Dicke:", m_toolOptionsHost));
        auto* wSlider = new QSlider(Qt::Horizontal, m_toolOptionsHost);
        wSlider->setRange(1, 100);
        wSlider->setValue(form->strokeWidth());
        wSlider->setMinimumWidth(100);
        auto* wValue = new QLabel(QString::number(form->strokeWidth()), m_toolOptionsHost);
        wValue->setMinimumWidth(30);
        m_toolOptionsLayout->addWidget(wSlider);
        m_toolOptionsLayout->addWidget(wValue);
        connect(wSlider, &QSlider::valueChanged, this, [form, wValue](int v) {
            form->setStrokeWidth(v);
            wValue->setText(QString::number(v));
        });
    } else if (auto* mover = dynamic_cast<MoverTool*>(tool)) {
        auto* mg = new QButtonGroup(m_toolOptionsHost);
        mg->setExclusive(true);
        const struct { const char* label; MoverTool::MoveMode mode; } mmodes[] = {
            {"Auswahl",   MoverTool::Selection},
            {"Pixel",     MoverTool::Pixels}
        };
        for (auto& mm : mmodes) {
            auto* b = new QToolButton(m_toolOptionsHost);
            b->setText(QString::fromUtf8(mm.label));
            b->setCheckable(true);
            b->setStyleSheet("QToolButton{ padding:2px 8px; }"
                             "QToolButton:checked{ background:#3a7bd5; color:white; }");
            mg->addButton(b);
            m_toolOptionsLayout->addWidget(b);
            if (mm.mode == mover->moveMode()
                || (mover->moveMode() == MoverTool::Paste && mm.mode == MoverTool::Pixels))
                b->setChecked(true);
            connect(b, &QToolButton::clicked, this, [this, mover, mm]() {
                // Vom Paste-Modus weg → Buffer zuerst übernehmen.
                if (m_doc->hasPasteBuffer()) commitPaste();
                mover->setMoveMode(mm.mode);
            });
        }
    } else if (auto* pip = dynamic_cast<PipetteTool*>(tool)) {
        auto* cross = new QCheckBox("Ebenenübergreifend", m_toolOptionsHost);
        cross->setToolTip("Farbe vom gesamten Bild (alle Ebenen) aufnehmen.");
        cross->setChecked(pip->crossLayer());
        connect(cross, &QCheckBox::toggled, this, [pip](bool c) { pip->setCrossLayer(c); });
        m_toolOptionsLayout->addWidget(cross);
    } else if (auto* txt = dynamic_cast<TextTool*>(tool)) {
        m_toolOptionsLayout->addWidget(new QLabel("Schriftgröße:", m_toolOptionsHost));
        auto* fs = new QSlider(Qt::Horizontal, m_toolOptionsHost);
        fs->setRange(6, 400);
        fs->setValue(txt->fontSize());
        fs->setMinimumWidth(120);
        auto* fv = new QLabel(QString::number(txt->fontSize()), m_toolOptionsHost);
        fv->setMinimumWidth(30);
        m_toolOptionsLayout->addWidget(fs);
        m_toolOptionsLayout->addWidget(fv);
        connect(fs, &QSlider::valueChanged, this, [txt, fv](int v) {
            txt->setFontSize(v);
            fv->setText(QString::number(v));
        });
        auto* hint = new QLabel("Klick → tippen → Enter", m_toolOptionsHost);
        hint->setStyleSheet("color:#888; font-style:italic;");
        m_toolOptionsLayout->addWidget(hint);
    }
    // Bleistift und andere Tools ohne Einstellungen: nur der Name wird angezeigt.

    m_toolOptionsLayout->addStretch();
}

// ===================================================================
// Multi-Document: Session-Verwaltung
// ===================================================================

// "Neu" erzeugt eine zusätzliche, eigenständige Datei (wie in Paint.NET).
// Jede Datei bekommt ihr eigenes Document + History und taucht als Preview
// in der Leiste unter dem ToolBar auf.
void MainWindow::beginNewDocument(int w, int h) {
    DocumentSession s;
    s.doc = new Document(w, h, this);
    // Document-Konstruktor legt schon Ebenen an – für eine saubere, leere
    // Datei alle Ebenen außer einer entfernen.
    while (s.doc->layerCount() > 0) s.doc->removeLayer(0);
    s.doc->resize(w, h);
    s.doc->addLayer("Hintergrund");
    s.history = new History(this);
    s.name = "Unbenannt " + QString::number(m_sessions.size() + 1);

    int idx = m_sessions.size();
    m_sessions.append(s);

    // Neue Datei → Thumbnail-Leiste komplett neu aufbauen (ein Widget
    // kommt hinzu). Danach auf die neue Session schalten.
    updatePreviewThumbnails();
    switchToSession(idx);
}

// "Öffnen" lädt ein Bild als neue Session.
void MainWindow::openFile() {
    const QString start = QStandardPaths::writableLocation(
        QStandardPaths::PicturesLocation);
    const QString path = QFileDialog::getOpenFileName(
        this, "Bild öffnen", start,
        "Bilder (*.png *.jpg *.jpeg *.bmp *.webp)");
    if (path.isEmpty()) return;

    QImage img(path);
    if (img.isNull()) {
        QMessageBox::warning(this, "Öffnen",
            "Bild konnte nicht geladen werden:\n" + path);
        return;
    }

    DocumentSession s;
    s.doc = new Document(img.width(), img.height(), this);
    s.doc->loadFromImage(img);
    s.history = new History(this);
    // Dateiname ohne Pfad als Anzeigename.
    s.name = QFileInfo(path).fileName();

    int idx = m_sessions.size();
    m_sessions.append(s);

    // Neue Datei → Thumbnail-Leiste komplett neu aufbauen (ein Widget
    // kommt hinzu). Danach auf die neue Session schalten.
    updatePreviewThumbnails();
    switchToSession(idx);
}

// Wechselt die aktive Datei. Alle Komponenten (Canvas, Docks, Tools)
// schwenken auf das neue Document/History um.
void MainWindow::switchToSession(int index) {
    if (index < 0 || index >= m_sessions.size()) return;

    // Schwebenden Paste-Buffer der aktuellen Session übernehmen, bevor
    // wir sie verlassen (sonst geht er verloren).
    if (m_doc && m_doc->hasPasteBuffer() && index != m_activeSession)
        commitPaste();

    m_activeSession = index;
    m_doc     = m_sessions[index].doc;
    m_history = m_sessions[index].history;

    refreshActivePointers();
    buildToolOptions(m_tools->active());

    // NUR die Hervorhebung ändern – KEINE Widgets neu aufbauen/löschen!
    // (Früher wurde hier updatePreviewThumbnails() aufgerufen, das alle
    //  Thumbnails zerstört, einschließlich des gerade angeklickten. Da ein
    //  Mausklick aus Press+Release besteht, landete das Release-Event dann
    //  auf einem toten Widget → Absturz.)
    updatePreviewHighlights();

    // Titel aktualisieren.
    setWindowTitle("MiniPaint - " + m_sessions[index].name);
}

// Schließt die Datei/Session mit dem gegebenen Index (X-Button).
// Die Document- und History-Objekte werden gelöscht. Ist es die letzte
// Session, wird sie durch eine neue leere Datei ersetzt (wie in Paint.NET).
void MainWindow::closeSession(int index) {
    if (index < 0 || index >= m_sessions.size()) return;

    auto& s = m_sessions[index];
    QObject::disconnect(s.changedConn);

    // Wenn die aktive Session geschlossen wird, müssen wir zuerst auf eine
    // Nachbar-Session schwenken, damit Canvas/Docks nie auf einen toten
    // Document-Zeiger zeigen.
    const bool wasActive = (index == m_activeSession);

    if (m_sessions.size() <= 1) {
        // Letzte Session: durch eine neue leere Datei ersetzen, damit das
        // Programm nicht ohne geöffnete Datei dasteht.
        // Alte Session dann NACH dem Aufbau der neuen löschen.
        DocumentSession ns;
        ns.doc = new Document(800, 600, this);
        while (ns.doc->layerCount() > 0) ns.doc->removeLayer(0);
        ns.doc->resize(800, 600);
        ns.doc->addLayer("Hintergrund");
        ns.history = new History(this);
        ns.name = "Unbenannt 1";

        delete s.doc;
        delete s.history;
        m_sessions[0] = ns;
        m_activeSession = 0;
        m_doc     = ns.doc;
        m_history = ns.history;
        refreshActivePointers();
        buildToolOptions(m_tools->active());
        updatePreviewThumbnails();
        setWindowTitle("MiniPaint - " + ns.name);
        return;
    }

    // Mehrere Sessions offen: Session löschen und entfernen.
    delete s.doc;
    delete s.history;
    m_sessions.removeAt(index);

    // Neuen aktiven Index bestimmen.
    if (wasActive) {
        // Aktive war die Geschlossene → auf Nachbarn schalten.
        int newActive = qMax(0, index - 1);
        if (newActive >= m_sessions.size()) newActive = m_sessions.size() - 1;
        m_activeSession = newActive;
        m_doc     = m_sessions[newActive].doc;
        m_history = m_sessions[newActive].history;
        refreshActivePointers();
        buildToolOptions(m_tools->active());
    } else if (index < m_activeSession) {
        // Eine Session vor der aktiven wurde geschlossen → Index sackt um 1.
        m_activeSession--;
    }

    updatePreviewThumbnails();

    if (m_activeSession >= 0 && m_activeSession < m_sessions.size())
        setWindowTitle("MiniPaint - " + m_sessions[m_activeSession].name);
}

// Schwenkt Canvas, Docks und Tools auf die aktuell aktive Session.
// Wird nach jedem Session-Wechsel aufgerufen.
void MainWindow::refreshActivePointers() {
    if (!m_doc || !m_history) return;

    // Tools auf neues Document/History umschwenken.
    m_tools->setDocument(m_doc);
    m_tools->setHistory(m_history);

    // ColorState an die History der aktiven Session anbinden, damit
    // Farb-Änderungen (Farbe ändern, Slot hinzufügen/entfernen) im
    // Verlaufsfenster auftauchen und rückgängig gemacht werden können.
    m_colors->setHistory(m_history);

    // Canvas auf neues Document umschwenken.
    m_canvas->setDocument(m_doc);

    // Docks auf neues Document/History umschwenken.
    m_layerDock->setDocument(m_doc, m_history);
    m_historyDock->setHistory(m_history);

    // Live-Aktualisierung des aktiven Thumbnails: immer wenn sich das
    // Bild ändert, das zugehörige Preview aktualisieren (throttled).
    // Alte Verbindung der aktiven Session trennen, dann neu verbinden.
    auto& active = m_sessions[m_activeSession];
    QObject::disconnect(active.changedConn);
    int activeIdx = m_activeSession;
    active.changedConn = connect(active.doc, &Document::changed, this,
        [this, activeIdx]() { updatePreviewThumbnail(activeIdx); },
        Qt::QueuedConnection);
}

// ===================================================================
// Preview-Leiste (Datei-Thumbnails unter dem ToolBar)
// ===================================================================

// Erstellt die Leiste unter dem ToolBar. Jede Datei bekommt einen
// klickbaren Thumbnail (Preview-Bild + Dateiname), ähnlich wie in Paint.NET.
//
// WICHTIG: Die Leiste ist ein schwebendes Child-Widget des Hauptfensters
// (KEIN QDockWidget), das direkt über dem Canvas liegt. Dadurch gibt es
// keinen Dock-Hintergrund – nur die Thumbnails selbst sind sichtbar.
void MainWindow::buildPreviewBar() {
    m_previewHost = new QWidget(this);
    m_previewHost->setObjectName("PreviewBar");
    // Vollständig transparenter Container: nur die Thumbnails sind
    // sichtbar, die Leiste selbst hat keinen Hintergrund.
    m_previewHost->setAttribute(Qt::WA_NoSystemBackground);
    m_previewHost->setAttribute(Qt::WA_TranslucentBackground);
    m_previewHost->setAutoFillBackground(false);
    m_previewHost->setStyleSheet("QWidget#PreviewBar{ background:transparent; }");

    m_previewLayout = new QHBoxLayout(m_previewHost);
    m_previewLayout->setContentsMargins(8, 2, 8, 2);
    m_previewLayout->setSpacing(6);
    m_previewLayout->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    // Leiste schwebt über dem Canvas — wird in resizeEvent positioniert.
    m_previewHost->setFixedHeight(64);
    m_previewHost->show();
    m_previewHost->raise();

    // Das erste Thumbnail für die Start-Session erzeugen.
    updatePreviewThumbnails();
}

// Baut alle Thumbnails neu auf.
// Wird NUR aufgerufen, wenn Dateien hinzukommen oder entfernt werden
// (z.B. beginNewDocument / openFile) – NIEMALS während eines Klicks.
// Beim Session-Wechsel verwenden wir stattdessen updatePreviewHighlights().
void MainWindow::updatePreviewThumbnails() {
    if (!m_previewLayout) return;

    // Alte Widgets entfernen und Maps leeren (sonst Dangling-Pointer).
    QLayoutItem* it;
    while ((it = m_previewLayout->takeAt(0)) != nullptr) {
        if (it->widget()) delete it->widget();
        delete it;
    }
    m_previewWidgets.clear();
    m_closeButtons.clear();
    m_thumbs.clear();

    for (int i = 0; i < m_sessions.size(); ++i) {
        const auto& s = m_sessions[i];

        // Ein Thumbnail-Widget: Bild + Name untereinander.
        auto* thumb = new QWidget(m_previewHost);
        thumb->setFixedSize(90, 60);
        auto* tl = new QVBoxLayout(thumb);
        tl->setContentsMargins(2, 2, 2, 2);
        tl->setSpacing(1);

        // Schachbrett-Thumbnail (wie in LayerDock).
        auto* preview = new QLabel(thumb);
        preview->setFixedSize(84, 42);
        preview->setScaledContents(true);
        preview->setStyleSheet("border:1px solid #555;");
        tl->addWidget(preview, 0, Qt::AlignCenter);

        auto* name = new QLabel(s.name, thumb);
        name->setStyleSheet(
            "font-size:10px; color:#ccc;"
            "background:transparent; border:none;");
        name->setAlignment(Qt::AlignCenter);
        name->setWordWrap(false);
        tl->addWidget(name);

        // --- Close-Button (X) oben rechts auf dem Thumbnail -----------
        auto* closeBtn = new QToolButton(thumb);
        closeBtn->setText(QString::fromUtf8("\xc3\x97"));  // ×
        closeBtn->setFixedSize(14, 14);
        closeBtn->setAutoRaise(true);
        closeBtn->setFocusPolicy(Qt::NoFocus);
        closeBtn->setCursor(Qt::ArrowCursor);
        closeBtn->setStyleSheet(
            "QToolButton { background: rgba(40,40,40,220); color:#fff;"
            "              border:none; border-radius:2px;"
            "              font-weight:bold; padding:0px; }"
            "QToolButton:hover { background: rgba(210,60,60,240); }");
        // Oben rechts positionieren (feste Geometrie, da Thumb fixed-size).
        closeBtn->move(thumb->width() - closeBtn->width() - 2, 2);
        closeBtn->show();
        closeBtn->raise();
        closeBtn->installEventFilter(this);
        m_closeButtons[closeBtn] = i;

        // Referenzen auf die inneren Widgets speichern, damit später
        // Bild und Hervorhebung aktualisiert werden können, ohne das
        // Container-Widget (und damit den Maus-Grabber) zu zerstören.
        PreviewThumb pt;
        pt.container = thumb;
        pt.image     = preview;
        pt.name      = name;
        m_thumbs.push_back(pt);

        // Initiales Bild + Hervorhebung setzen.
        regenerateThumbnailImage(i);

        // Klick → Session wechseln (über Event-Filter, verzögert).
        thumb->installEventFilter(this);
        m_previewWidgets[thumb] = i;

        m_previewLayout->addWidget(thumb);
    }

    m_previewLayout->addStretch();
    updatePreviewHighlights();

    // Host-Breite sofort an die neue Thumbnail-Anzahl anpassen, damit
    // keine Überlappungen entstehen (das resizeEvent kommt erst später).
    if (m_previewHost) {
        const int thumbW = 90;
        const int spacing = 6;
        const int margin = 16;
        int neededW = margin + m_thumbs.size() * (thumbW + spacing);
        int w = qBound(0, neededW, width());
        QRect g = m_previewHost->geometry();
        if (g.width() != w) {
            m_previewHost->resize(w, m_previewHost->height());
            m_previewHost->raise();
        }
    }
}

// Einzelnes Thumbnail-Bild aktualisieren (Live-Update nach Bildänderung).
// Erzeugt NUR das Composite-Bild neu und setzt es ins QLabel – das Widget
// selbst bleibt bestehen. Das ist sicher, auch wenn gerade ein Klick
// läuft, weil kein Widget zerstört wird.
void MainWindow::updatePreviewThumbnail(int index) {
    if (index < 0 || index >= m_thumbs.size()) return;
    regenerateThumbnailImage(index);
}

// Erzeugt das Composite-Bild für das Thumbnail mit dem gegebenen Index
// neu (inkl. Schachbrett-Hintergrund) und setzt es ins Bild-Label.
void MainWindow::regenerateThumbnailImage(int index) {
    if (index < 0 || index >= m_thumbs.size()) return;
    if (index < 0 || index >= m_sessions.size()) return;

    const auto& s = m_sessions[index];
    auto* preview = m_thumbs[index].image;
    if (!preview) return;

    QImage comp = s.doc->composite();
    QPixmap pm(84, 42);
    QPainter p(&pm);
    for (int y = 0; y < 42; y += 8)
        for (int x = 0; x < 84; x += 8)
            p.fillRect(x, y, 8, 8,
                      ((x / 8 + y / 8) % 2) ? QColor(60, 60, 60)
                                             : QColor(90, 90, 90));
    p.drawImage(0, 0, comp.scaled(84, 42, Qt::KeepAspectRatio,
                                   Qt::SmoothTransformation));
    p.end();
    preview->setPixmap(pm);
}

// Aktualisiert NUR die Hervorhebung (aktive vs. inaktive Session).
// Es werden KEINE Widgets zerstört oder neu erzeugt – damit ist diese
// Funktion sicher aufrufbar, während ein Thumbnail-Klick verarbeitet wird.
void MainWindow::updatePreviewHighlights() {
    for (int i = 0; i < m_thumbs.size() && i < m_sessions.size(); ++i) {
        auto& pt = m_thumbs[i];
        if (!pt.container || !pt.name) continue;
        if (i == m_activeSession) {
            pt.container->setStyleSheet(
                "QWidget{ background:#3a7bd5; border-radius:3px; }");
            pt.name->setStyleSheet(
                "font-size:10px; color:white; font-weight:bold;"
                "background:transparent; border:none;");
        } else {
            pt.container->setStyleSheet(
                "QWidget{ background:#2a2a2a; border-radius:3px; }");
            pt.name->setStyleSheet(
                "font-size:10px; color:#ccc;"
                "background:transparent; border:none;");
        }
    }
}
