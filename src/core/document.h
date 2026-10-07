#pragma once

#include <QObject>
#include <QList>
#include <QImage>
#include <QRegion>
#include <functional>
#include <QRect>

#include "layer.h"

class QPainter;

// Haelt den Bildzustand: eine Liste von Ebenen plus die aktive Ebene.
class Document : public QObject {
    Q_OBJECT
public:
    Document(int w, int h, QObject* parent = nullptr);
    ~Document() override;

    int width()  const { return m_width; }
    int height() const { return m_height; }
    int layerCount() const { return m_layers.size(); }

    Layer*       layer(int i)       { return (i >= 0 && i < m_layers.size()) ? m_layers[i] : nullptr; }
    const Layer* layer(int i) const { return (i >= 0 && i < m_layers.size()) ? m_layers[i] : nullptr; }
    Layer* activeLayer() { return layer(m_activeLayer); }
    int    activeLayerIndex() const { return m_activeLayer; }

    void setActiveLayer(int i);
    int  addLayer(const QString& name);
    void insertLayer(int index, const QString& name,
                     const QImage& img, bool vis, float op);
    void removeLayer(int i);
    void moveLayer(int from, int to);
    void setLayerVisible(int i, bool v);
    void setLayerOpacity(int i, float o);
    void setLayerLocked(int i, bool locked);
    void renameLayer(int i, const QString& name);
    void mergeLayerDown(int i);
    void paintOnActiveLayer(std::function<void(QPainter&)> fn);
    void resize(int newW, int newH);
    void loadFromImage(const QImage& img);
    QImage composite() const;

    // ---- Auswahl ----
    void setSelection(const QImage& mask);          // Maske muss gleiche Größe haben
    void addToSelection(const QImage& mask);        // Vereinigung mit bestehender Auswahl (STRG)
    void subtractFromSelection(const QImage& mask); // Subtrahiert Maske von bestehender Auswahl
    void clearSelection();
    bool hasSelection() const;                       // O(1) – gecacht
    QImage selectionMask() const;                   // Kopie der Maske
    QRegion selectionRegion() const;                // gecachte Region (Pixelgenau)
    QRect selectionBounds() const;                  // umgebendes Rechteck der Auswahl (oder leeres QRect)
    void moveSelection(int dx, int dy);             // verschiebt die Auswahlmaske
    void movePixelsInSelection(int dx, int dy);     // verschiebt Pixel auf aktiver Ebene innerhalb der Auswahl

    // ---- Schwebende Zwischenablage (Paste-Buffer) ----
    // Ein eingefügtes Bild schwebt über der Ebene und kann frei verschoben
    // werden, bevor es auf die Ebene gestempelt wird (wie in Paint.NET/PS).
    bool    hasPasteBuffer() const { return m_hasPaste; }
    QImage  pasteBuffer() const { return m_pasteBuffer; }
    QPoint  pasteOffset() const { return m_pasteOffset; }
    QRect   pasteBounds() const;                     // Offset-Rechteck (kann außerhalb liegen)
    void    setPasteBuffer(const QImage& img, QPoint pos);
    void    movePasteBuffer(int dx, int dy);
    void    stampPasteBuffer();                       // auf aktive Ebene stempeln (ohne Verlauf)
    void    clearPasteBuffer();

signals:
    void changed();
    void structureChanged();
    void selectionChanged();                        // neu: wird bei Änderung der Auswahl gesendet

private:
    int           m_width;
    int           m_height;
    QList<Layer*> m_layers;
    int           m_activeLayer = 0;
    QImage        m_selectionMask;                  // 8-Bit-Maske, 0 = nicht ausgewählt, 255 = voll ausgewählt
    // Cache für hasSelection() und selectionRegion() – wird bei jeder
    // Änderung der Auswahl einmalig (nicht pro Paint) neu aufgebaut.
    bool          m_hasSelection = false;
    QRegion       m_selectionRegion;
    void          rebuildSelectionCache();

    // Schwebender Paste-Buffer.
    bool          m_hasPaste = false;
    QImage        m_pasteBuffer;
    QPoint        m_pasteOffset;
};
