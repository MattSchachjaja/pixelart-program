#include "document.h"

#include <QPainter>
#include <QPen>
#include <QPoint>
#include <QRect>
#include <QStack>
#include <QQueue>
#include <QDebug>

Document::Document(int w, int h, QObject* parent)
    : QObject(parent), m_width(w), m_height(h)
{
    // leere Maske
    m_selectionMask = QImage(w, h, QImage::Format_Alpha8);
    m_selectionMask.fill(0);
    rebuildSelectionCache();

    Layer* bg = layer(addLayer("Hintergrund"));
    {
        QPainter p(&bg->image);
        p.fillRect(0, 0, w, h, Qt::white);
    }
    Layer* l1 = layer(addLayer("Ebene 1"));
    {
        QPainter p(&l1->image);
        p.setRenderHint(QPainter::Antialiasing);
        p.setBrush(QColor(80, 140, 220, 200));
        p.setPen(QPen(QColor(40, 80, 160), 3));
        p.drawEllipse(QPoint(400, 300), 200, 150);
    }
    Layer* l2 = layer(addLayer("Ebene 2"));
    {
        QPainter p(&l2->image);
        p.setRenderHint(QPainter::Antialiasing);
        p.setBrush(QColor(220, 80, 100, 180));
        p.setPen(QPen(QColor(140, 30, 50), 3));
        p.drawRoundedRect(QRect(600, 400, 300, 200), 20, 20);
    }
    m_activeLayer = m_layers.size() - 1;
}

Document::~Document() { qDeleteAll(m_layers); }

void Document::setActiveLayer(int i) {
    if (i >= 0 && i < m_layers.size() && m_activeLayer != i) {
        m_activeLayer = i;
        emit changed();
        emit structureChanged();
    }
}

int Document::addLayer(const QString& name) {
    auto* l = new Layer(m_width, m_height, name);
    m_layers.append(l);
    m_activeLayer = m_layers.size() - 1;
    emit changed();
    emit structureChanged();
    return m_layers.size() - 1;
}

void Document::insertLayer(int index, const QString& name,
                           const QImage& img, bool vis, float op)
{
    auto* l = new Layer(m_width, m_height, name);
    l->image = img;
    l->visible = vis;
    l->opacity = op;
    m_layers.insert(qBound(0, index, m_layers.size()), l);
    m_activeLayer = index;
    emit changed();
    emit structureChanged();
}

void Document::removeLayer(int i) {
    if (i < 0 || i >= m_layers.size()) return;
    delete m_layers.takeAt(i);
    if (m_activeLayer >= m_layers.size()) m_activeLayer = m_layers.size() - 1;
    if (m_activeLayer < 0) m_activeLayer = 0;
    emit changed();
    emit structureChanged();
}

void Document::moveLayer(int from, int to) {
    if (from < 0 || from >= m_layers.size()) return;
    if (to < 0 || to >= m_layers.size()) return;
    if (from == to) return;
    m_layers.move(from, to);
    if      (m_activeLayer == from) m_activeLayer = to;
    else if (from < m_activeLayer && to >= m_activeLayer) m_activeLayer--;
    else if (from > m_activeLayer && to <= m_activeLayer) m_activeLayer++;
    emit changed();
    emit structureChanged();
}

void Document::setLayerVisible(int i, bool v) {
    if (auto* l = layer(i)) { l->visible = v; emit changed(); }
}

void Document::setLayerOpacity(int i, float o) {
    if (auto* l = layer(i)) { l->opacity = qBound(0.0f, o, 1.0f); emit changed(); }
}

void Document::setLayerLocked(int i, bool locked) {
    if (auto* l = layer(i)) {
        l->locked = locked;
        emit structureChanged();
    }
}

void Document::renameLayer(int i, const QString& name) {
    if (auto* l = layer(i)) { l->name = name; emit structureChanged(); }
}

void Document::mergeLayerDown(int i) {
    if (i <= 0 || i >= m_layers.size()) return;
    Layer* upper = m_layers[i];
    Layer* lower = m_layers[i - 1];
    QImage merged(m_width, m_height, QImage::Format_ARGB32_Premultiplied);
    merged.fill(Qt::transparent);
    QPainter p(&merged);
    p.setOpacity(lower->opacity);
    p.drawImage(0, 0, lower->image);
    p.setOpacity(upper->opacity);
    p.drawImage(0, 0, upper->image);
    p.end();
    delete lower;
    m_layers.removeAt(i - 1);
    upper->image = merged;
    upper->opacity = 1.0f;
    m_activeLayer = i - 1;
    emit changed();
    emit structureChanged();
}

void Document::paintOnActiveLayer(std::function<void(QPainter&)> fn) {
    if (auto* l = activeLayer()) {
        QPainter p(&l->image);
        fn(p);
        p.end();
        emit changed();
    }
}

void Document::resize(int newW, int newH) {
    newW = qMax(1, newW);
    newH = qMax(1, newH);
    for (Layer* l : m_layers) {
        QImage scaled(newW, newH, QImage::Format_ARGB32_Premultiplied);
        scaled.fill(Qt::transparent);
        QPainter p(&scaled);
        p.setCompositionMode(QPainter::CompositionMode_Source);
        p.drawImage(0, 0, l->image);
        p.end();
        l->image = scaled;
    }
    // Auswahlmaske ebenfalls anpassen
    if (!m_selectionMask.isNull()) {
        QImage newMask(newW, newH, QImage::Format_Alpha8);
        newMask.fill(0);
        QPainter p(&newMask);
        p.setCompositionMode(QPainter::CompositionMode_Source);
        p.drawImage(0, 0, m_selectionMask);
        p.end();
        m_selectionMask = newMask;
        rebuildSelectionCache();
    }
    m_width = newW;
    m_height = newH;
    emit changed();
    emit structureChanged();
}

void Document::loadFromImage(const QImage& img) {
    qDeleteAll(m_layers);
    m_layers.clear();
    QImage base = img.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    m_width = base.width();
    m_height = base.height();
    auto* l = new Layer(m_width, m_height, "Hintergrund");
    l->image = base;
    m_layers.append(l);
    m_activeLayer = 0;
    // Auswahl zurücksetzen
    m_selectionMask = QImage(m_width, m_height, QImage::Format_Alpha8);
    m_selectionMask.fill(0);
    rebuildSelectionCache();
    emit changed();
    emit structureChanged();
    emit selectionChanged();
}

QImage Document::composite() const {
    QImage out(m_width, m_height, QImage::Format_ARGB32_Premultiplied);
    out.fill(Qt::transparent);
    QPainter p(&out);
    for (const Layer* l : m_layers) {
        if (!l->visible) continue;
        p.setOpacity(l->opacity);
        p.drawImage(0, 0, l->image);
    }
    // Schwebenden Paste-Buffer obendrauf zeichnen (falls vorhanden).
    if (m_hasPaste && !m_pasteBuffer.isNull()) {
        p.setOpacity(1.0);
        p.drawImage(m_pasteOffset, m_pasteBuffer);
    }
    p.end();
    return out;
}

// ---- Schwebende Zwischenablage (Paste-Buffer) ----

QRect Document::pasteBounds() const {
    if (!m_hasPaste || m_pasteBuffer.isNull()) return QRect();
    return QRect(m_pasteOffset, m_pasteBuffer.size());
}

void Document::setPasteBuffer(const QImage& img, QPoint pos) {
    m_pasteBuffer = img.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    m_pasteOffset = pos;
    m_hasPaste = !m_pasteBuffer.isNull();
    emit changed();
}

void Document::movePasteBuffer(int dx, int dy) {
    if (!m_hasPaste) return;
    m_pasteOffset.rx() += dx;
    m_pasteOffset.ry() += dy;
    emit changed();
}

void Document::stampPasteBuffer() {
    if (!m_hasPaste || m_pasteBuffer.isNull()) return;
    Layer* lay = activeLayer();
    if (lay) {
        QPainter p(&lay->image);
        p.drawImage(m_pasteOffset, m_pasteBuffer);
    }
}

void Document::clearPasteBuffer() {
    if (!m_hasPaste) return;
    m_hasPaste = false;
    m_pasteBuffer = QImage();
    m_pasteOffset = QPoint();
    emit changed();
}

// ---- Auswahl ----

void Document::setSelection(const QImage& mask) {
    if (mask.size() != QSize(m_width, m_height)) {
        qWarning() << "setSelection: Mask size mismatch";
        return;
    }
    m_selectionMask = mask.convertToFormat(QImage::Format_Alpha8);
    rebuildSelectionCache();
    emit selectionChanged();
    emit changed();
}

void Document::addToSelection(const QImage& mask) {
    if (mask.size() != QSize(m_width, m_height)) {
        qWarning() << "addToSelection: Mask size mismatch";
        return;
    }
    QImage add = mask.convertToFormat(QImage::Format_Alpha8);
    if (m_selectionMask.isNull() || m_selectionMask.size() != add.size()) {
        m_selectionMask = add;
    } else {
        // Vereinigung: an jedem Pixel das Maximum übernehmen.
        const int bpl = m_selectionMask.bytesPerLine();
        const int sbpl = add.bytesPerLine();
        uchar*       dst = m_selectionMask.bits();
        const uchar* src = add.constBits();
        const int w = m_selectionMask.width();
        const int h = m_selectionMask.height();
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                uchar s = src[y * sbpl + x];
                if (s > dst[y * bpl + x]) dst[y * bpl + x] = s;
            }
    }
    rebuildSelectionCache();
    emit selectionChanged();
    emit changed();
}

void Document::subtractFromSelection(const QImage& mask) {
    if (mask.size() != QSize(m_width, m_height)) {
        qWarning() << "subtractFromSelection: Mask size mismatch";
        return;
    }
    QImage sub = mask.convertToFormat(QImage::Format_Alpha8);
    if (m_selectionMask.isNull()) return;
    // Subtraktion: an jedem Pixel den Anteil der neuen Maske abziehen.
    //   neu = alt * (255 - s) / 255
    //   s=255 (voll subtrahieren) -> 0
    //   s=0   (nichts subtrahieren) -> alt bleibt
    const int bpl = m_selectionMask.bytesPerLine();
    const int sbpl = sub.bytesPerLine();
    uchar*       dst = m_selectionMask.bits();
    const uchar* src = sub.constBits();
    const int w = m_selectionMask.width();
    const int h = m_selectionMask.height();
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const uchar s = src[y * sbpl + x];
            if (s == 0) continue;
            const uchar cur = dst[y * bpl + x];
            dst[y * bpl + x] = uchar(cur * (255 - s) / 255);
        }
    rebuildSelectionCache();
    emit selectionChanged();
    emit changed();
}

void Document::clearSelection() {
    if (m_selectionMask.isNull()) return;
    m_selectionMask.fill(0);
    rebuildSelectionCache();
    emit selectionChanged();
    emit changed();
}

bool Document::hasSelection() const {
    // O(1) – der Cache wird bei jeder Änderung der Auswahl aktualisiert.
    return m_hasSelection;
}

QImage Document::selectionMask() const {
    return m_selectionMask;
}

QRegion Document::selectionRegion() const {
    return m_selectionRegion;
}

QRect Document::selectionBounds() const {
    return m_selectionRegion.boundingRect();
}

// Baut den Cache (m_hasSelection + m_selectionRegion) aus der Maske auf.
// Wird bei jeder Änderung der Auswahl einmalig aufgerufen – nicht pro Paint!
void Document::rebuildSelectionCache() {
    m_hasSelection = false;
    m_selectionRegion = QRegion();
    if (m_selectionMask.isNull()) return;

    const uchar* bits = m_selectionMask.constBits();
    const int bpl = m_selectionMask.bytesPerLine();
    const int w   = m_selectionMask.width();
    const int h   = m_selectionMask.height();
    // Region aus horizontalen Spans zusammensetzen (effizienter als pro Pixel).
    for (int y = 0; y < h; ++y) {
        int x = 0;
        while (x < w) {
            while (x < w && bits[y * bpl + x] == 0) ++x;
            if (x >= w) break;
            const int start = x;
            while (x < w && bits[y * bpl + x] != 0) ++x;
            m_selectionRegion += QRect(start, y, x - start, 1);
            m_hasSelection = true;
        }
    }
}

void Document::moveSelection(int dx, int dy) {
    if (!hasSelection()) return;
    QImage newMask(m_width, m_height, QImage::Format_Alpha8);
    newMask.fill(0);
    QPainter p(&newMask);
    p.setCompositionMode(QPainter::CompositionMode_Source);
    p.drawImage(dx, dy, m_selectionMask);
    p.end();
    m_selectionMask = newMask;
    rebuildSelectionCache();
    emit selectionChanged();
    emit changed();
}

void Document::movePixelsInSelection(int dx, int dy) {
    if (!hasSelection()) return;
    Layer* lay = activeLayer();
    if (!lay) return;

    QImage img = lay->image;
    QImage mask = m_selectionMask;
    int w = m_width, h = m_height;
    // ★ FIX: bytesPerLine() statt width() für Masken-Zugriff
    int maskBpl = mask.bytesPerLine();
    const uchar* maskBits = mask.constBits();

    // Verschobene Pixel vorbereiten
    QImage moved(w, h, QImage::Format_ARGB32_Premultiplied);
    moved.fill(Qt::transparent);

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            if (maskBits[y * maskBpl + x] != 0) {
                QRgb pixel = img.pixel(x, y);
                int nx = x + dx;
                int ny = y + dy;
                if (nx >= 0 && nx < w && ny >= 0 && ny < h) {
                    moved.setPixel(nx, ny, pixel);
                }
            }
        }
    }

    // Alte Pixel innerhalb der Maske auf transparent setzen
    QImage newImg = img;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            if (maskBits[y * maskBpl + x] != 0) {
                newImg.setPixel(x, y, qRgba(0,0,0,0));
            }
        }
    }

    // Verschobene Pixel einfügen (SourceOver = Default,
    // damit transparente Bereiche in 'moved' das Bild nicht löschen)
    {
        QPainter p(&newImg);
        p.drawImage(0, 0, moved);
    }
    lay->image = newImg;
    emit changed();
}
