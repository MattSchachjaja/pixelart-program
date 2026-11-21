#include "layerdock.h"
#include "document.h"
#include "history.h"
#include "commands.h"
#include "../ui/dialogs.h"

#include <QLabel>
#include <QCheckBox>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QPainter>
#include <QPen>
#include <QPixmap>
#include <QImage>
#include <QFontMetrics>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QApplication>
#include <QGraphicsOpacityEffect>
#include <QContextMenuEvent>
#include <QMenu>
#include <QTimer>

// Begrenzungsrechteck aller nicht-transparenten Pixel einer Ebene.
// Leeres QRect, wenn nichts gemalt wurde.
static QRect layerContentBounds(const QImage& img) {
    if (img.isNull()) return QRect();
    const QImage argb = (img.format() == QImage::Format_ARGB32_Premultiplied)
        ? img : img.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    const int w = argb.width(), h = argb.height();
    int minX = w, minY = h, maxX = -1, maxY = -1;
    for (int y = 0; y < h; ++y) {
        const QRgb* row = reinterpret_cast<const QRgb*>(argb.constScanLine(y));
        for (int x = 0; x < w; ++x) {
            if (qAlpha(row[x]) != 0) {
                if (x < minX) minX = x;
                if (x > maxX) maxX = x;
                if (y < minY) minY = y;
                if (y > maxY) maxY = y;
            }
        }
    }
    if (maxX < 0) return QRect();
    // Kleiner Rand, damit der Inhalt nicht am Preview-Rand klebt.
    QRect b(minX, minY, maxX - minX + 1, maxY - minY + 1);
    return b.adjusted(-1, -1, 1, 1).intersected(QRect(0, 0, w, h));
}

// ===================================================================
// LayerNameLabel — Ebenenname über max. 2 Zeilen, immer lesbar
// ===================================================================
// Ein normales QLabel mit wordWrap bricht nur an Leerzeichen um und
// berechnet seine Höhe im Layout oft falsch -> lange Namen wurden rechts
// bzw. unten abgeschnitten. Dieses Label zeichnet den Text selbst:
//  - Umbruch an Wortgrenzen, notfalls mitten im Wort,
//  - höchstens 2 Zeilen, die zweite wird bei Bedarf mit "…" gekürzt,
//  - feste Höhe = 2 volle Zeilenhöhen -> keine abgeschnittenen Buchstaben,
//  - voller Name als Tooltip.
namespace {
class LayerNameLabel : public QLabel {
public:
    explicit LayerNameLabel(const QString& text, QWidget* parent = nullptr)
        : QLabel(text, parent)
    {
        setWordWrap(false);
        setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        setFixedHeight(QFontMetrics(font()).lineSpacing() * 2 + 2);
        setToolTip(text);
    }
    void setNameText(const QString& t) { setText(t); setToolTip(t); update(); }

    QSize sizeHint() const override {
        return QSize(80, QFontMetrics(font()).lineSpacing() * 2 + 2);
    }
    QSize minimumSizeHint() const override {
        return QSize(20, QFontMetrics(font()).lineSpacing() * 2 + 2);
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setPen(palette().color(QPalette::WindowText));
        p.setFont(font());
        const QFontMetrics fm(font());
        const int w = qMax(1, width());

        // Zeilen bilden (max. 2).
        QStringList lines;
        QString rest = text();
        while (!rest.isEmpty() && lines.size() < 2) {
            if (lines.size() == 1) {
                // Letzte Zeile: Rest ggf. mit "…" kürzen.
                lines << fm.elidedText(rest, Qt::ElideRight, w);
                break;
            }
            if (fm.horizontalAdvance(rest) <= w) { lines << rest; break; }
            // Längster Anfang, der in die Zeile passt.
            int fit = 1;
            for (int n = 1; n <= rest.size(); ++n) {
                if (fm.horizontalAdvance(rest.left(n)) > w) break;
                fit = n;
            }
            // Möglichst an einem Leerzeichen umbrechen.
            int brk = rest.left(fit + 1).lastIndexOf(' ');
            int take = (brk > 0) ? brk : fit;
            lines << rest.left(take);
            rest = rest.mid(take).trimmed();
        }

        const int lh = fm.lineSpacing();
        const int totalH = lines.size() * lh;
        int y = (height() - totalH) / 2 + fm.ascent();
        for (const QString& l : lines) {
            p.drawText(0, y, l);
            y += lh;
        }
    }
};
} // namespace

// ===================================================================
// LayerRowWidget
// ===================================================================
LayerRowWidget::LayerRowWidget(int index, Layer* layer, QWidget* parent)
    : QFrame(parent), m_index(index)
{
    setFrameShape(QFrame::Box);
    setLineWidth(1);
    setMinimumHeight(64);
    setMaximumHeight(64);

    auto* lay = new QHBoxLayout(this);
    lay->setContentsMargins(4, 4, 4, 4);
    lay->setSpacing(4);

    // --- Vorschau-Thumbnail + Info darunter ---------------------------
    auto* leftCol = new QVBoxLayout();
    leftCol->setContentsMargins(0, 0, 0, 0);
    leftCol->setSpacing(2);

    m_preview = new QLabel(this);
    m_preview->setFixedSize(40, 40);
    m_preview->setScaledContents(true);
    m_preview->setStyleSheet("border:1px solid #555;");
    updatePreview(layer->image, layer->visible);
    leftCol->addWidget(m_preview);

    m_info = new QLabel(this);
    m_info->setFixedSize(40, 14);
    {
        QFont f = m_info->font();
        f.setPointSize(qMax(6, f.pointSize() - 2));
        m_info->setFont(f);
    }
    m_info->setAlignment(Qt::AlignCenter);
    refreshInfo(layer->opacity, layer->locked);
    leftCol->addWidget(m_info);

    lay->addLayout(leftCol);

    // --- Sichtbarkeit -------------------------------------------------
    m_vis = new QCheckBox(this);
    m_vis->setChecked(layer->visible);
    m_vis->setToolTip("Sichtbarkeit umschalten");
    lay->addWidget(m_vis);

    // --- Name (Label + Inline-Edit) -----------------------------------
    m_nameContainer = new QWidget(this);
    auto* nameLay = new QHBoxLayout(m_nameContainer);
    nameLay->setContentsMargins(0, 0, 0, 0);
    nameLay->setSpacing(0);
    nameLay->setAlignment(Qt::AlignVCenter);
    // Name über max. 2 Zeilen, Buchstaben werden nie abgeschnitten
    // (siehe LayerNameLabel oben).
    m_name = new LayerNameLabel(layer->name, this);
    m_nameEdit = new QLineEdit(layer->name, this);
    m_nameEdit->setFrame(false);
    m_nameEdit->selectAll();
    m_nameEdit->hide();
    nameLay->addWidget(m_name, 1);
    nameLay->addWidget(m_nameEdit, 1);
    lay->addWidget(m_nameContainer, 1);

    // --- Signal-Verbindungen ------------------------------------------
    connect(m_nameEdit, &QLineEdit::editingFinished, this, [this]() {
        if (!m_nameEdit->isVisible()) return;   // schon abgeschlossen
        QString txt = m_nameEdit->text().trimmed();
        m_nameEdit->hide();
        m_name->show();
        // Tastatur wieder ans Hauptfenster geben (Shortcuts, Leinwand).
        if (QWidget* mw = window()->parentWidget())
            mw->window()->activateWindow();
        if (!txt.isEmpty() && txt != m_name->text()) {
            static_cast<LayerNameLabel*>(m_name)->setNameText(txt);
            emit renameRequested(m_index, txt);   // loest rebuild aus
        }
    });

    connect(m_vis, &QCheckBox::toggled, this, [this](bool v) {
        emit visibilityChanged(m_index, v);
    });

}

void LayerRowWidget::updatePreview(const QImage& img, bool visible) {
    QPixmap pm(40, 40);
    QPainter p(&pm);
    // Schachbrettmuster fuer Transparenz
    for (int y = 0; y < 40; y += 8)
        for (int x = 0; x < 40; x += 8)
            p.fillRect(x, y, 8, 8,
                       ((x / 8 + y / 8) % 2) ? QColor(60, 60, 60)
                                              : QColor(90, 90, 90));
    // Inhalt IMMER anzeigen – auch bei ausgeblendeten oder von oberen
    // Ebenen verdeckten Zeichnungen sieht man so, was auf welcher Ebene ist.
    Q_UNUSED(visible);
    if (!img.isNull()) {
        // Auf den bemalten Bereich zoomen: kleine Zeichnungen werden
        // vergroessert dargestellt, damit man alles gut erkennt.
        const QRect bounds = layerContentBounds(img);
        const QImage crop = bounds.isNull() ? img : img.copy(bounds);
        const QImage scaled = crop.scaled(40, 40, Qt::KeepAspectRatio,
                                          Qt::SmoothTransformation);
        p.drawImage((40 - scaled.width()) / 2, (40 - scaled.height()) / 2, scaled);
    }
    p.end();
    m_preview->setPixmap(pm);
}

bool LayerRowWidget::isRenaming() const {
    return m_nameEdit && m_nameEdit->isVisible();
}

QString LayerRowWidget::renameText() const {
    return m_nameEdit ? m_nameEdit->text() : QString();
}

int LayerRowWidget::renameCursor() const {
    return m_nameEdit ? m_nameEdit->cursorPosition() : 0;
}

// Das Ebenen-Dock ist ein schwebendes Fenster, das vom Fenstermanager
// beim Klicken NICHT aktiviert wird (Qt setzt dafuer
// X11BypassWindowManagerHint). Ohne explizites activateWindow() kaeme
// die Tastatur-Eingabe nie im Eingabefeld an -> man koennte nicht tippen.
static void focusLineEditNow(QLineEdit* edit) {
    QWidget* win = edit->window();
    win->raise();
    win->activateWindow();
    edit->setFocus(Qt::MouseFocusReason);
    // Aktivierung kann asynchron sein -> Fokus nach dem Event-Loop-Durchlauf
    // noch einmal sicherstellen.
    QTimer::singleShot(0, edit, [edit]() {
        if (!edit->isVisible()) return;
        edit->window()->activateWindow();
        edit->setFocus(Qt::MouseFocusReason);
    });
}

void LayerRowWidget::resumeRename(const QString& text, int cursorPos) {
    m_name->hide();
    m_nameEdit->setText(text);
    m_nameEdit->show();
    focusLineEditNow(m_nameEdit);
    m_nameEdit->setCursorPosition(cursorPos);
}

void LayerRowWidget::refreshInfo(float opacity, bool locked) {
    const int pct = qRound(qBound(0.0f, opacity, 1.0f) * 100.0f);
    QString t;
    if (locked) {
        // Schloss-Symbol + Prozent
        t = QString::fromUtf8("\xe2\x9c\x96 ") + QString::number(pct) + "%";
    } else {
        t = QString::number(pct) + "%";
    }
    m_info->setText(t);
    m_info->setStyleSheet(locked ? "color:#ff7777;" : "color:#aaa;");
}

void LayerRowWidget::setActive(bool a) {
    if (a) {
        setStyleSheet(
            "QFrame{ background:#3a7bd5; color:white; }"
            "QLabel{ color:white; }"
            "QCheckBox{ color:white; }");
    } else {
        setStyleSheet(
            "QFrame{ background:#2a2a2a; color:#ddd; }"
            "QLabel{ color:#ddd; }"
            "QCheckBox{ color:#ddd; }");
    }
}

void LayerRowWidget::startInlineRename() {
    m_nameEdit->setText(m_name->text());
    m_name->hide();
    m_nameEdit->show();
    focusLineEditNow(m_nameEdit);
    m_nameEdit->selectAll();   // sofort lostippen ersetzt den alten Namen
}

void LayerRowWidget::setDropIndicator(DropPos pos) {
    m_dropPos = pos;
    update();
}

void LayerRowWidget::cancelDrag() {
    m_dragCancelled = true;
    if (m_dragging) {
        m_dragging = false;
        setGraphicsEffect(nullptr);
        // KEIN setActive(false) hier! Das wuerde die aktive Ebene
        // faelschlicherweise als inaktiv darstellen. Das Widget wird
        // ohnehin gleich in rebuild() zerstoert.
    }
}

void LayerRowWidget::paintEvent(QPaintEvent* e) {
    QFrame::paintEvent(e);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    if (m_dropPos == DropPos::Above) {
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(76, 175, 80, 220));
        p.drawRect(0, 0, width(), 4);
        int cx = width() / 2;
        QPolygon triangle;
        triangle << QPoint(cx - 6, 8) << QPoint(cx + 6, 8) << QPoint(cx, 1);
        p.drawPolygon(triangle);
    } else if (m_dropPos == DropPos::Below) {
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(76, 175, 80, 220));
        p.drawRect(0, height() - 4, width(), 4);
        int cx = width() / 2;
        QPolygon triangle;
        triangle << QPoint(cx - 6, height() - 8)
                 << QPoint(cx + 6, height() - 8)
                 << QPoint(cx, height() - 1);
        p.drawPolygon(triangle);
    }
}

void LayerRowWidget::mousePressEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton) {
        m_dragStart = e->pos();
        m_dragCancelled = false;
        // KEIN emit selected() hier!
        // Auswahl erst bei mouseRelease, wenn kein Drag stattfand.
        // Sonst wuerde setActiveLayer -> structureChanged -> rebuild()
        // das Widget zerstoeren waehrend es noch gedraggt wird.
    }
    QFrame::mousePressEvent(e);
}

void LayerRowWidget::mouseDoubleClickEvent(QMouseEvent* e) {
    // Doppelklick = sofort inline umbenennen (Eigenschaften gibt es
    // weiterhin ueber das Kontextmenue).
    if (e->button() == Qt::LeftButton)
        startInlineRename();
}

void LayerRowWidget::mouseMoveEvent(QMouseEvent* e) {
    if (m_dragCancelled) return;
    if (!(e->buttons() & Qt::LeftButton)) return;

    if (!m_dragging &&
        (e->pos() - m_dragStart).manhattanLength()
            >= QApplication::startDragDistance()) {
        m_dragging = true;

        // Visuelles Feedback: Opazitaet reduzieren waehrend des Drags.
        // Nur EIN GraphicsEffect gleichzeitig moeglich.
        auto* op = new QGraphicsOpacityEffect(this);
        op->setOpacity(0.6);
        setGraphicsEffect(op);
    }

    if (m_dragging)
        emit dragMoving(m_index, mapToParent(e->pos()));
}

void LayerRowWidget::mouseReleaseEvent(QMouseEvent* e) {
    if (m_dragCancelled) {
        m_dragCancelled = false;
        QFrame::mouseReleaseEvent(e);
        return;
    }

    if (m_dragging && e->button() == Qt::LeftButton) {
        m_dragging = false;
        setGraphicsEffect(nullptr);
        // KEIN setActive(false) hier! Das wuerde die aktive
        // Hervorhebung entfernen. setGraphicsEffect(nullptr)
        // stellt das normale Aussehen schon wieder her.
        emit dragFinished(m_index, mapToParent(e->pos()));
    } else if (e->button() == Qt::LeftButton) {
        // Kein Drag = normaler Klick -> Ebene auswaehlen
        emit selected(m_index);
    }
    QFrame::mouseReleaseEvent(e);
}

void LayerRowWidget::contextMenuEvent(QContextMenuEvent* e) {
    emit contextMenuRequested(m_index, e->globalPos());
}

// ===================================================================
// LayerDock
// ===================================================================
LayerDock::LayerDock(Document* doc, History* history, QWidget* parent)
    : QDockWidget("Ebenen", parent), m_doc(doc), m_history(history)
{
    setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    setFeatures(QDockWidget::DockWidgetMovable);

    auto* host = new QWidget(this);
    auto* outer = new QVBoxLayout(host);
    outer->setContentsMargins(4, 4, 4, 4);

    m_listHost   = new QWidget();
    m_listLayout = new QVBoxLayout(m_listHost);
    m_listLayout->setContentsMargins(0, 0, 0, 0);
    m_listLayout->setSpacing(2);
    m_listLayout->addStretch();

    auto* scroll = new QScrollArea();
    scroll->setWidget(m_listHost);
    scroll->setWidgetResizable(true);
    outer->addWidget(scroll, 1);

    // --- Button-Leiste ------------------------------------------------
    auto* btns = new QHBoxLayout();
    btns->setSpacing(3);

    auto* bAdd   = new QPushButton("+", this);
    auto* bDel   = new QPushButton(QString::fromUtf8("\xe2\x88\x92"), this);
    auto* bUp    = new QPushButton(QString::fromUtf8("\xe2\x96\xb2"), this);
    auto* bDn    = new QPushButton(QString::fromUtf8("\xe2\x96\xbc"), this);
    auto* bMerge = new QPushButton(QString::fromUtf8("\xe2\x8a\x95"), this);
    bMerge->setToolTip("Mit Ebene darunter verbinden");

    for (auto* b : {bAdd, bDel, bUp, bDn, bMerge}) {
        b->setMaximumWidth(36);
        btns->addWidget(b);
    }
    btns->addStretch();
    outer->addLayout(btns);

    setWidget(host);

    // --- Button-Verbindungen ------------------------------------------
    connect(bAdd,   &QPushButton::clicked, this, &LayerDock::onAddLayer);
    connect(bDel,   &QPushButton::clicked, this, &LayerDock::onRemoveLayer);
    connect(bUp,    &QPushButton::clicked, this, &LayerDock::onMoveLayerUp);
    connect(bDn,    &QPushButton::clicked, this, &LayerDock::onMoveLayerDown);
    connect(bMerge, &QPushButton::clicked, this, &LayerDock::onMergeLayerDown);

    // --- Dokument-Aenderungen abonnieren ------------------------------
    // structureChanged: Layer-Liste hat sich geaendert -> kompletter rebuild.
    m_docConnStruct = connect(m_doc, &Document::structureChanged, this, [this]() {
        rebuild();
    }, Qt::QueuedConnection);

    // changed: Pixel wurden gemalt. Ein rebuild waere zu teuer (zerstoert
    // alle Zeilen und baut sie neu). Stattdessen wird nur das jeweilige
    // Vorschaubild geupdatet - und das per Single-Shot-Timer gebuendelt,
    // damit bei schnellem Malen (viele changed-Signale pro Sekunde) nur
    // ein einziges Thumbnail-Update pro Pause stattfindet.
    m_thumbTimer = new QTimer(this);
    m_thumbTimer->setSingleShot(true);
    m_thumbTimer->setInterval(120);   // ms
    connect(m_thumbTimer, &QTimer::timeout, this, &LayerDock::refreshThumbnails);

    m_docConnChanged = connect(m_doc, &Document::changed, this, [this]() {
        if (!m_thumbTimer->isActive()) m_thumbTimer->start();
    }, Qt::QueuedConnection);

    rebuild();
}

// Multi-Document: Dock auf anderes Document/History umschwenken.
// Trennt die alte Signal-Verbindung, verbindet das neue Document und
// baut die Ebenenliste komplett neu auf.
void LayerDock::setDocument(Document* doc, History* history) {
    if (!doc || !history || (doc == m_doc && history == m_history)) return;
    QObject::disconnect(m_docConnStruct);
    QObject::disconnect(m_docConnChanged);
    if (m_thumbTimer) m_thumbTimer->stop();
    m_doc = doc;
    m_history = history;
    m_docConnStruct = connect(m_doc, &Document::structureChanged, this, [this]() {
        rebuild();
    }, Qt::QueuedConnection);
    m_docConnChanged = connect(m_doc, &Document::changed, this, [this]() {
        if (!m_thumbTimer->isActive()) m_thumbTimer->start();
    }, Qt::QueuedConnection);
    rebuild();
}

// ===================================================================
// Private Slots
// ===================================================================
void LayerDock::onAddLayer() {
    m_history->push(new AddLayerCommand(
        m_doc, "Ebene " + QString::number(m_doc->layerCount())));
}

void LayerDock::onRemoveLayer() {
    if (m_doc->layerCount() > 1)
        m_history->push(new RemoveLayerCommand(m_doc, m_doc->activeLayerIndex()));
}

void LayerDock::onMoveLayerUp() {
    int i = m_doc->activeLayerIndex();
    if (i < m_doc->layerCount() - 1)
        m_history->push(new MoveLayerCommand(m_doc, i, i + 1));
}

void LayerDock::onMoveLayerDown() {
    int i = m_doc->activeLayerIndex();
    if (i > 0)
        m_history->push(new MoveLayerCommand(m_doc, i, i - 1));
}

void LayerDock::onMergeLayerDown() {
    int i = m_doc->activeLayerIndex();
    if (i > 0)
        m_history->push(new MergeLayerCommand(m_doc, i));
}

void LayerDock::showContextMenu(int index, const QPoint& globalPos) {
    QMenu menu(this);

    QAction* actRename  = menu.addAction("Umbenennen");
    QAction* actProps   = menu.addAction("Eigenschaften...");
    QAction* actDel     = menu.addAction("Loeschen");
    menu.addSeparator();
    QAction* actUp      = menu.addAction("Nach oben");
    QAction* actDn      = menu.addAction("Nach unten");
    menu.addSeparator();
    QAction* actMerge   = menu.addAction("Mit darunter verbinden");

    // Deaktivieren, wenn die Aktion nicht moeglich ist
    actDel->setEnabled(m_doc->layerCount() > 1);
    actUp->setEnabled(index < m_doc->layerCount() - 1);
    actDn->setEnabled(index > 0);
    actMerge->setEnabled(index > 0);

    QAction* chosen = menu.exec(globalPos);
    if (!chosen) return;

    if (chosen == actRename) {
        for (auto* rw : m_rowWidgets) {
            if (rw->displayIndex() == index) {
                rw->startInlineRename();
                break;
            }
        }
    } else if (chosen == actProps) {
        // Eigenschaften-Dialog: frueher per Doppelklick, jetzt hier.
        for (auto* rw : m_rowWidgets) {
            if (rw->displayIndex() == index) {
                emit rw->propertiesRequested(index);
                break;
            }
        }
    } else if (chosen == actDel) {
        m_history->push(new RemoveLayerCommand(m_doc, index));
    } else if (chosen == actUp) {
        if (index < m_doc->layerCount() - 1)
            m_history->push(new MoveLayerCommand(m_doc, index, index + 1));
    } else if (chosen == actDn) {
        if (index > 0)
            m_history->push(new MoveLayerCommand(m_doc, index, index - 1));
    } else if (chosen == actMerge) {
        if (index > 0)
            m_history->push(new MergeLayerCommand(m_doc, index));
    }
}

// ===================================================================
// Liste aufbauen
// ===================================================================
void LayerDock::rebuild() {
    // Alle laufenden Drags abbrechen, bevor Widgets zerstoert werden.
    cancelAllDrags();

    // Laufende Inline-Umbenennung merken: der Doppelklick waehlt die
    // Ebene auch aus (-> rebuild). Die Eingabe darf dabei nicht verloren
    // gehen, sondern wird auf die neue Zeile uebertragen.
    int     renameIdx    = -1;
    QString renameText;
    int     renameCursor = 0;
    for (auto* rw : m_rowWidgets) {
        if (rw->isRenaming()) {
            renameIdx    = rw->displayIndex();
            renameText   = rw->renameText();
            renameCursor = rw->renameCursor();
            break;
        }
    }

    // Alle Zeilen-Widgets entfernen
    QLayoutItem* it;
    while ((it = m_listLayout->takeAt(0)) != nullptr) {
        if (it->widget()) delete it->widget();
        delete it;
    }
    m_rowWidgets.clear();
    m_listLayout->addStretch();

    int N = m_doc->layerCount();

    // Ebenen in umgekehrter Reihenfolge: Widget 0 = oberste Ebene (real N-1),
    // letztes Widget = unterste Ebene (real 0).
    for (int i = N - 1; i >= 0; --i) {
        auto* row = new LayerRowWidget(i, m_doc->layer(i), m_listHost);
        row->setActive(i == m_doc->activeLayerIndex());

        // --- Standard-Signal-Verbindungen -----------------------------
        connect(row, &LayerRowWidget::visibilityChanged,
                m_doc, &Document::setLayerVisible);
        connect(row, &LayerRowWidget::selected,
                m_doc, &Document::setActiveLayer);

        connect(row, &LayerRowWidget::renameRequested, this,
                [this](int idx, const QString& newName) {
                    m_doc->renameLayer(idx, newName);
                });

        // --- Ebeneneigenschaften-Dialog (Doppelklick) ------------------
        connect(row, &LayerRowWidget::propertiesRequested, this,
                [this](int idx) {
                    Layer* l = m_doc->layer(idx);
                    if (!l) return;

                    // Urspruengliche Werte fuer "Abbrechen" merken.
                    const QString origName = l->name;
                    const float   origOp   = l->opacity;
                    const bool    origLock = l->locked;

                    LayerPropertiesDialog dlg(l->name, l->opacity,
                                              l->locked, this);
                    // Live-Aktualisierung: Aenderungen sofort anwenden.
                    // Achtung: renameLayer/setLayerLocked loesen ein
                    // (queued) rebuild() aus, wodurch die Zeilen-Widgets
                    // zerstoert werden. Daher das aktuelle Widget immer
                    // per Index suchen - niemals einen Pointer captuern!
                    connect(&dlg, &LayerPropertiesDialog::changed,
                            this, [this, idx](const QString& n,
                                              float op, bool lk) {
                        m_doc->renameLayer(idx, n);
                        m_doc->setLayerOpacity(idx, op);
                        m_doc->setLayerLocked(idx, lk);
                        for (auto* rw : m_rowWidgets) {
                            if (rw->displayIndex() == idx) {
                                rw->refreshInfo(op, lk);
                                break;
                            }
                        }
                    });

                    if (dlg.exec() == QDialog::Rejected) {
                        // Bei Abbrechen die urspruenglichen Werte
                        // wiederherstellen.
                        m_doc->renameLayer(idx, origName);
                        m_doc->setLayerOpacity(idx, origOp);
                        m_doc->setLayerLocked(idx, origLock);
                        for (auto* rw : m_rowWidgets) {
                            if (rw->displayIndex() == idx) {
                                rw->refreshInfo(origOp, origLock);
                                break;
                            }
                        }
                    }
                });

        // --- Kontextmenue ---------------------------------------------
        connect(row, &LayerRowWidget::contextMenuRequested, this,
                &LayerDock::showContextMenu);

        // --- Drag & Drop ----------------------------------------------
        //
        // Anzeigereihenfolge (m_rowWidgets):
        //   m_rowWidgets[0] = oberste Ebene (realer Index N-1)
        //   m_rowWidgets[w] = realer Index N-1-w
        //   m_rowWidgets[N-1] = unterste Ebene (realer Index 0)
        //
        // Um den korrekten Ziel-Index fuer moveLayer() zu berechnen,
        // arbeiten wir mit "reversed indices" (Anzeigepositionen):
        //   rFrom   = N-1 - dragReal   (Anzeigeposition der gezogenen Ebene)
        //   rTarget = w                (Anzeigeposition des Ziel-Widgets)
        //
        // insertPos = gewuenschte Anzeigeposition nach dem Drop.
        // Da beim Entfernen der gezogenen Ebene die Anzeigepositionen
        // darunter verschoben werden, muss insertPos korrigiert werden:
        //   - Wenn die gezogene Ebene ueber dem Ziel war (rFrom < rTarget),
        //     verschieben sich die Positionen unterhalb von rFrom um -1
        //   - Wenn die gezogene Ebene unter dem Ziel war (rFrom > rTarget),
        //     verschieben sich die Positionen oberhalb von rFrom um +1

        connect(row, &LayerRowWidget::dragMoving, this,
                [this](int /*dragIdx*/, const QPoint& posInList) {
                    clearDropIndicators();
                    if (m_rowWidgets.isEmpty()) return;

                    int y = posInList.y();
                    int hitW = -1;
                    bool above = false;

                    for (int w = 0; w < m_rowWidgets.size(); ++w) {
                        QRect r = m_rowWidgets[w]->geometry();
                        if (y >= r.top() && y <= r.bottom()) {
                            hitW  = w;
                            above = (y < r.top() + r.height() / 2);
                            break;
                        }
                    }

                    if (hitW < 0) {
                        // Maus ausserhalb aller Widgets:
                        // Oberhalb des ersten = ganz oben,
                        // unterhalb des letzten = ganz unten.
                        QRect first = m_rowWidgets[0]->geometry();
                        QRect last  = m_rowWidgets.last()->geometry();
                        if (y < first.top()) {
                            hitW  = 0;
                            above = true;
                        } else if (y > last.bottom()) {
                            hitW  = m_rowWidgets.size() - 1;
                            above = false;
                        } else
                            return;
                    }

                    m_rowWidgets[hitW]->setDropIndicator(
                        above ? LayerRowWidget::DropPos::Above
                              : LayerRowWidget::DropPos::Below);
                });

        connect(row, &LayerRowWidget::dragFinished, this,
                [this](int dragIdx, const QPoint& posInList) {
                    clearDropIndicators();
                    if (m_rowWidgets.isEmpty()) return;

                    int N = m_doc->layerCount();
                    int dragReal = dragIdx;
                    int rFrom = N - 1 - dragReal;

                    // Finde das Widget unter dem Mauszeiger
                    int hitW = -1;
                    bool above = false;
                    int y = posInList.y();
                    for (int w = 0; w < m_rowWidgets.size(); ++w) {
                        QRect r = m_rowWidgets[w]->geometry();
                        if (y >= r.top() && y <= r.bottom()) {
                            hitW  = w;
                            above = (y < r.top() + r.height() / 2);
                            break;
                        }
                    }

                    if (hitW < 0) {
                        // Maus ausserhalb aller Widgets:
                        // Oberhalb des ersten = ganz nach oben,
                        // unterhalb des letzten = ganz nach unten.
                        QRect first = m_rowWidgets[0]->geometry();
                        QRect last  = m_rowWidgets.last()->geometry();
                        if (y < first.top()) {
                            hitW  = 0;
                            above = true;
                        } else if (y > last.bottom()) {
                            hitW  = m_rowWidgets.size() - 1;
                            above = false;
                        } else
                            return;
                    }

                    int rTarget = hitW; // Widget-Index = Anzeigeposition

                    // Berechne die Einfuegeposition in der Anzeige
                    int insertPos;
                    if (above) {
                        if (rFrom < rTarget)
                            insertPos = rTarget - 1;
                        else
                            insertPos = rTarget;
                    } else {
                        if (rFrom < rTarget)
                            insertPos = rTarget;
                        else
                            insertPos = rTarget + 1;
                    }

                    // Auf gueltigen Bereich beschraenken
                    insertPos = qBound(0, insertPos, N - 1);

                    // Anzeigeposition in echten Layer-Index umrechnen
                    int to = N - 1 - insertPos;

                    if (dragReal != to) {
                        m_history->push(
                            new MoveLayerCommand(m_doc, dragReal, to));
                    }
                });

        m_rowWidgets.push_back(row);
        m_listLayout->insertWidget(m_listLayout->count() - 1, row);
    }

    // Umbenennung fortsetzen, falls eine lief.
    if (renameIdx >= 0) {
        for (auto* rw : m_rowWidgets) {
            if (rw->displayIndex() == renameIdx) {
                rw->resumeRename(renameText, renameCursor);
                break;
            }
        }
    }
}

void LayerDock::clearDropIndicators() {
    for (auto* rw : m_rowWidgets)
        rw->setDropIndicator(LayerRowWidget::DropPos::None);
}

void LayerDock::cancelAllDrags() {
    for (auto* rw : m_rowWidgets)
        rw->cancelDrag();
}

// Aktualisiert NUR die Vorschaubilder aller Zeilen (live, ohne rebuild).
// Wird per Throttle-Timer nach Document::changed aufgerufen.
//
// m_rowWidgets ist in umgekehrter Reihenfolge: Widget 0 = realer Index N-1,
// Widget w = realer Index N-1-w. Daher: realer Index = N-1-w.
void LayerDock::refreshThumbnails() {
    if (!m_doc) return;
    const int N = m_doc->layerCount();
    for (int w = 0; w < m_rowWidgets.size() && w < N; ++w) {
        const int realIdx = N - 1 - w;
        if (realIdx < 0 || realIdx >= N) continue;
        if (Layer* lay = m_doc->layer(realIdx))
            m_rowWidgets[w]->updatePreview(lay->image, lay->visible);
    }
}
