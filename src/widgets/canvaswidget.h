#pragma once

#include <QOpenGLWidget>
#include <QOpenGLExtraFunctions>
#include <QPointF>
#include <QPoint>
#include <QLineF>
#include <QVector>
#include <QKeySequence>
#include <memory>

class Document;
class ToolManager;
class QOpenGLShaderProgram;
class QOpenGLTexture;
class QTimer;

#include <QOpenGLBuffer>
#include <QOpenGLVertexArrayObject>

class QMouseEvent;
class QWheelEvent;
class QEnterEvent;

// Für QObject::connect Rückgabewerte (Signal-Verbindungen).
#include <QMetaObject>

// OpenGL-Leinwand: zeigt das zusammengesetzte Bild, leitet Maus an Werkzeuge
// weiter, unterstützt Zoom (Mausrad) und Verschieben (mittlere Maustaste).
class CanvasWidget : public QOpenGLWidget, protected QOpenGLExtraFunctions {
    Q_OBJECT
public:
    CanvasWidget(Document* doc, ToolManager* tools, QWidget* parent = nullptr);
    ~CanvasWidget() override;

    QPointF screenToDoc(const QPointF& s) const { return (s - m_pan) / m_zoom; }
    float   zoom() const { return m_zoom; }
    void resetView();

    // Multi-Document: Leinwand auf ein anderes Document umschwenken.
    void setDocument(Document* doc);

    // Konfigurierbare Tastenbelegung für "Auswahl aufheben"
    // (Default: Leertaste). QShortcut ist bei QOpenGLWidget nicht
    // zuverlässig – daher wird die Taste hier direkt ausgewertet.
    void setClearSelectionKey(const QKeySequence& key) { m_clearSelectionKey = key; }

signals:
    void deleteRequested();   // Delete-Taste: Auswahl-Inhalt löschen
    void clearSelectionRequested();              // Auswahl aufheben
    void zoomChanged(float zoom);                 // für die Statusleiste
    void mousePosChanged(const QPointF& docPos);  // Maus in Dokumentkoordinaten

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;

    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;
    void enterEvent(QEnterEvent* event) override;  // neu für Cursor
    void keyPressEvent(QKeyEvent* e) override;     // Text-Eingabe weiterreichen

private:
    void centerDocument();
    void fitDocumentToWidget();  // Ansicht (Zoom/Pan) an Widget anpassen – Pixelgröße bleibt von Einstellungen
    void recreateTexture();      // Textur sauber neu erzeugen (verhindert Qt6 Storage-Fehler)
    void rebuildVBO();
    void updateCursor();  // Hilfsfunktion zum Setzen des Cursors
    void rebuildSelectionOverlay();  // blaue Auswahl-Einfärbung + Kanten cachen
    bool isSelectionToolActive() const;  // true bei Auswahl-/Verschiebe-Werkzeugen

    // Pixel-perfektes Zoomen: diskrete Stufenleiter (6.25% .. 3200%).
    float snapZoomToStep(float z) const;
    int   zoomStepIndex(float z) const;     // Index in der Leiter (>=0, < kCount)
    // Pan auf ganzzahlige Bildschirmkoordinaten runden – verhindert
    // Flackern und ungleich dicke Rasterlinien beim Verschieben.
    void  snapPanToPixels();

    Document*    m_doc;
    ToolManager* m_tools;
    std::unique_ptr<QOpenGLShaderProgram> m_prog;
    std::unique_ptr<QOpenGLTexture>       m_tex;
    QOpenGLBuffer            m_vbo;
    QOpenGLVertexArrayObject m_vao;
    bool    m_dirty = true;
    bool    m_vboDirty = false;
    bool    m_needsFit = false;   // Document an Widget-Größe anpassen (deferred)
    bool    m_firstResize = true;
    QPointF m_pan{0, 0};
    float   m_zoom = 1.0f;
    bool    m_panning = false;
    QPoint  m_lastPos;
    QImage  m_selectionOverlay;        // blaue Einfärbung der Auswahl (Dokument-Auflösung)
    bool    m_selectionOverlayDirty = true;
    QVector<QLineF> m_selectionEdges; // Umriss-Kanten der Auswahl (Dokument-Koordinaten)

    // Marching-Ants-Animation: animierter Strich-Offset für die Auswahl-Umrandung.
    QTimer* m_marchTimer = nullptr;
    qreal   m_marchOffset = 0.0;

    // Signal-Verbindungen zum Document (für Multi-Document-Tausch).
    QMetaObject::Connection m_docConnChanged;
    QMetaObject::Connection m_docConnSel;

    // Taste zum Aufheben der Auswahl (über Shortcuts einstellbar).
    QKeySequence m_clearSelectionKey = QKeySequence(Qt::Key_Space);
};
