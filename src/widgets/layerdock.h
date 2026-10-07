#pragma once

#include <QFrame>
#include <QDockWidget>
#include <QVector>
#include <QPoint>
#include <QString>
#include <QMetaObject>

class QLabel;
class QCheckBox;
class QLineEdit;
class QPushButton;
class QWidget;
class QVBoxLayout;
class QHBoxLayout;
class QImage;
class QMouseEvent;
class QPaintEvent;
class QContextMenuEvent;
class QMenu;

class Document;
class History;
struct Layer;
class QTimer;

// ===================================================================
// LayerRowWidget
// ===================================================================
// Eine Zeile in der Ebenenliste: Vorschau, Sichtbarkeit, Name (Doppelklick
// zum Umbenennen) und visuelles Drag&Drop zum Umsortieren.
class LayerRowWidget : public QFrame {
    Q_OBJECT
public:
    explicit LayerRowWidget(int index, Layer* layer, QWidget* parent = nullptr);

    void updatePreview(const QImage& img);
    void setActive(bool a);
    void setIndex(int i) { m_index = i; }
    int  displayIndex() const { return m_index; }

    // Kleine Info-Zeile unten in der Zeile aktualisieren
    // (Sperre + Transparenz).
    void refreshInfo(float opacity, bool locked);

    enum class DropPos { None, Above, Below };
    void startInlineRename();
    void setDropIndicator(DropPos pos);

    // Wird aufgerufen, bevor das Widget zerstoert wird (z.B. bei rebuild),
    // um einen laufenden Drag sicher abzubrechen.
    void cancelDrag();

signals:
    void visibilityChanged(int index, bool visible);
    void selected(int index);
    void renameRequested(int index, const QString& newName);
    void propertiesRequested(int index);
    void dragMoving(int fromIndex, const QPoint& posInParent);
    void dragFinished(int fromIndex, const QPoint& posInParent);
    void contextMenuRequested(int index, const QPoint& globalPos);

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void contextMenuEvent(QContextMenuEvent* e) override;

private:
    int         m_index;
    QLabel*     m_preview       = nullptr;
    QCheckBox*  m_vis           = nullptr;
    QLabel*     m_name          = nullptr;
    QLineEdit*  m_nameEdit      = nullptr;
    QWidget*    m_nameContainer = nullptr;
    QLabel*     m_info          = nullptr;   // kleine Info unten (Sperre/Transparenz)
    QPoint      m_dragStart;
    bool        m_dragging      = false;
    bool        m_dragCancelled = false;
    DropPos     m_dropPos       = DropPos::None;
};

// ===================================================================
// LayerDock
// ===================================================================
// Dock mit der Ebenenliste und Schaltflaechen (hinzufuegen, loeschen, hoch,
// runter, zusammenfuehren).
class LayerDock : public QDockWidget {
    Q_OBJECT
public:
    explicit LayerDock(Document* doc, History* history, QWidget* parent = nullptr);

    // Multi-Document: Dock auf anderes Document/History umschwenken.
    void setDocument(Document* doc, History* history);

private slots:
    void onAddLayer();
    void onRemoveLayer();
    void onMoveLayerUp();
    void onMoveLayerDown();
    void onMergeLayerDown();
    void showContextMenu(int index, const QPoint& globalPos);

private:
    void rebuild();
    void clearDropIndicators();
    void cancelAllDrags();
    void refreshThumbnails();   // nur die Vorschaubilder updaten (kein rebuild)

    Document*                m_doc;
    History*                 m_history;
    QWidget*                 m_listHost;
    QVBoxLayout*             m_listLayout;
    QVector<LayerRowWidget*> m_rowWidgets;

    // Signal-Verbindungen zum Document (für Multi-Document-Tausch).
    QMetaObject::Connection m_docConnStruct;   // structureChanged -> rebuild
    QMetaObject::Connection m_docConnChanged;  // changed -> throttled thumbnails

    // Throttling: alle changed()-Signale werden gebuendelt und triggern
    // nach Ablauf dieser Pause EIN Thumbnail-Update (statt pro Mouse-Move).
    QTimer* m_thumbTimer = nullptr;
};
