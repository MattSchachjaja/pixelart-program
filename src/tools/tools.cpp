#include "tools.h"
#include "document.h"
#include "colorstate.h"
#include "history.h"
#include "commands.h"

#include <QPainter>
#include <QPen>
#include <QBrush>
#include <QColor>
#include <QImage>
#include <QRect>
#include <QPoint>
#include <QPolygonF>
#include <QPainterPath>
#include <QRadialGradient>
#include <QQueue>
#include <QVector>
#include <QFont>
#include <QFontMetrics>
#include <QKeyEvent>
#include <cmath>
#include <algorithm>

// =========================================================================
// Hilfsfunktionen
// =========================================================================

// Skaliert eine Alpha8-Maske mit SmoothTransformation. Qt kann Alpha8
// nicht direkt smooth-skalieren, daher der Umweg ueber ARGB32.
static QImage scaleAlphaMaskSmooth(const QImage& src, const QSize& dstSize) {
    if (src.size() == dstSize) return src.copy();
    QImage argb(src.size(), QImage::Format_ARGB32_Premultiplied);
    argb.fill(Qt::transparent);
    const uchar* sBits = src.constBits();
    const int sBpl = src.bytesPerLine();
    QRgb* dBits = reinterpret_cast<QRgb*>(argb.bits());
    const int dBpl = argb.bytesPerLine() / 4;
    for (int y = 0; y < src.height(); ++y)
        for (int x = 0; x < src.width(); ++x) {
            const uchar a = sBits[y * sBpl + x];
            dBits[y * dBpl + x] = qRgba(a, a, a, a);  // alpha in alle Kanaele
        }
    QImage scaled = argb.scaled(dstSize, Qt::IgnoreAspectRatio,
                                Qt::SmoothTransformation);
    QImage result(dstSize, QImage::Format_Alpha8);
    const QRgb* scBits = reinterpret_cast<const QRgb*>(scaled.constBits());
    const int scBpl = scaled.bytesPerLine() / 4;
    uchar* rBits = result.bits();
    const int rBpl = result.bytesPerLine();
    for (int y = 0; y < dstSize.height(); ++y)
        for (int x = 0; x < dstSize.width(); ++x)
            rBits[y * rBpl + x] = qAlpha(scBits[y * scBpl + x]);
    return result;
}

static QImage polygonToMask(const QPolygonF& poly, int w, int h) {
    QImage mask(w, h, QImage::Format_Alpha8);
    mask.fill(0);
    if (poly.size() < 3) return mask;
    QPainterPath path;
    path.addPolygon(poly);
    QPainter painter(&mask);
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QBrush(Qt::white, Qt::SolidPattern));
    painter.drawPath(path);
    painter.end();
    return mask.convertToFormat(QImage::Format_Alpha8);
}

static QImage floodFillMask(const QImage& src, QPoint start, int tolerance) {
    QImage mask(src.size(), QImage::Format_Alpha8);
    mask.fill(0);
    if (src.isNull() || src.depth() < 32) return mask;
    if (start.x() < 0 || start.x() >= src.width() || start.y() < 0 || start.y() >= src.height())
        return mask;
    const int w = src.width(), h = src.height();
    // Direkter Byte-Zugriff statt pixel()/pixelIndex()/setPixel() – sicher und schnell.
    const QRgb target = reinterpret_cast<const QRgb*>(src.scanLine(start.y()))[start.x()];
    const int mbpl = mask.bytesPerLine();
    uchar* mbits = mask.bits();
    auto isSimilar = [&](QRgb a, QRgb b) {
        int dr = qAbs(qRed(a) - qRed(b));
        int dg = qAbs(qGreen(a) - qGreen(b));
        int db = qAbs(qBlue(a) - qBlue(b));
        int da = qAbs(qAlpha(a) - qAlpha(b));
        return (dr + dg + db + da) <= tolerance;
    };
    QQueue<QPoint> queue;
    queue.enqueue(start);
    mbits[start.y() * mbpl + start.x()] = 255;
    while (!queue.isEmpty()) {
        QPoint p = queue.dequeue();
        int x = p.x(), y = p.y();
        const int dx[] = {0, 0, 1, -1};
        const int dy[] = {1, -1, 0, 0};
        for (int i = 0; i < 4; ++i) {
            int nx = x + dx[i];
            int ny = y + dy[i];
            if (nx < 0 || nx >= w || ny < 0 || ny >= h) continue;
            if (mbits[ny * mbpl + nx] != 0) continue;
            const QRgb c = reinterpret_cast<const QRgb*>(src.scanLine(ny))[nx];
            if (isSimilar(c, target)) {
                mbits[ny * mbpl + nx] = 255;
                queue.enqueue(QPoint(nx, ny));
            }
        }
    }
    return mask;
}

// Alle Pixel des Bildes, die zur Zielfarbe ähnlich sind (Modus „alle Pixel“).
static QImage globalMatchMask(const QImage& src, QRgb target, int tolerance) {
    QImage mask(src.size(), QImage::Format_Alpha8);
    mask.fill(0);
    if (src.isNull() || src.depth() < 32) return mask;
    const int w = src.width(), h = src.height();
    const int mbpl = mask.bytesPerLine();
    uchar* mbits = mask.bits();
    auto isSimilar = [&](QRgb a, QRgb b) {
        int dr = qAbs(qRed(a) - qRed(b));
        int dg = qAbs(qGreen(a) - qGreen(b));
        int db = qAbs(qBlue(a) - qBlue(b));
        int da = qAbs(qAlpha(a) - qAlpha(b));
        return (dr + dg + db + da) <= tolerance;
    };
    for (int y = 0; y < h; ++y) {
        const QRgb* srow = reinterpret_cast<const QRgb*>(src.scanLine(y));
        uchar* mrow = mbits + y * mbpl;
        for (int x = 0; x < w; ++x)
            if (isSimilar(srow[x], target))
                mrow[x] = 255;
    }
    return mask;
}

// =========================================================================
// PencilTool
// =========================================================================

PencilTool::PencilTool(Document* doc, ColorState* cs, History* hist)
    : m_doc(doc), m_colors(cs), m_history(hist) {}

// Setzt einen einzelnen Pixel (premultiplied), respektiert Auswahl und –
// im Pixel-Perfekt-Modus – die Besuchs-Maske (keine Doppelpixel).
void PencilTool::plot(int x, int y) {
    const int w = m_doc->width(), h = m_doc->height();
    if (x < 0 || x >= w || y < 0 || y >= h) return;
    if (m_hasSel) {
        const uchar* sb = m_selMask.constBits();
        if (sb[y * m_selMask.bytesPerLine() + x] == 0) return;
    }
    if (m_pixelPerfect) {
        if (m_visited.testBit(y * w + x)) return;
        m_visited.setBit(y * w + x);
    }
    QImage& img = m_lay->image;
    QRgb* bits = reinterpret_cast<QRgb*>(img.bits());
    const int idx = y * (img.bytesPerLine() / 4) + x;
    if (m_erasing && m_eraseStrength < 1.0f) {
        // Partielles Löschen: vorhandenen Pixel alpha-mäßig reduzieren.
        // Format ist ARGB32_Premultiplied → alle Kanäle mit (1-e) skalieren.
        const QRgb cur = bits[idx];
        const float keep = 1.0f - m_eraseStrength;
        const int newA = qRound(qAlpha(cur) * keep);
        const int newR = qRound(qRed(cur)   * keep);
        const int newG = qRound(qGreen(cur) * keep);
        const int newB = qRound(qBlue(cur)  * keep);
        bits[idx] = qRgba(newR, newG, newB, newA);
    } else {
        bits[idx] = m_penColor;
    }
    growDirty(QRect(x, y, 1, 1));
}

// Holt einen Pixel aus dem Vor-Strich-Zustand zurück (zum Entfernen einer Ecke).
void PencilTool::restore(int x, int y) {
    const int w = m_doc->width(), h = m_doc->height();
    if (x < 0 || x >= w || y < 0 || y >= h) return;
    QImage& img = m_lay->image;
    QRgb* dbits = reinterpret_cast<QRgb*>(img.bits());
    const QRgb* sbits = reinterpret_cast<const QRgb*>(m_beforeFull.constBits());
    dbits[y * (img.bytesPerLine() / 4) + x] =
        sbits[y * (m_beforeFull.bytesPerLine() / 4) + x];
    if (m_pixelPerfect) m_visited.clearBit(y * w + x);
}

// Bresenham-Linie, pixelweise über plot().
void PencilTool::line(int x0, int y0, int x1, int y1) {
    int dx =  std::abs(x1 - x0);
    int dy = -std::abs(y1 - y0);
    const int sx = x0 < x1 ? 1 : -1;
    const int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        plot(x0, y0);
        if (x0 == x1 && y0 == y1) break;
        const int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

bool PencilTool::mousePress(const QPointF& docPos, Qt::MouseButton button, Qt::KeyboardModifiers) {
    if (button != Qt::LeftButton && button != Qt::RightButton) return false;
    m_lay = m_doc->activeLayer();
    if (!m_lay) return false;
    if (m_lay->locked) return false;  // Ebene gesperrt: nicht zeichnen

    const int px = int(std::floor(docPos.x()));
    const int py = int(std::floor(docPos.y()));
    if (px < 0 || px >= m_doc->width() || py < 0 || py >= m_doc->height())
        return false;

    m_drawing = true;
    m_erasing = (button == Qt::RightButton);
    m_dirtyRect = QRect();
    m_beforeFull = m_lay->image.copy();
    m_hasSel = m_doc->hasSelection();
    m_selMask = m_hasSel ? m_doc->selectionMask() : QImage();
    if (m_pixelPerfect) {
        m_visited.resize(m_doc->width() * m_doc->height());
        m_visited.fill(false);
    }

    // premultiplied Stiftfarbe (Radieren = transparent).
    QColor c = m_colors->activeColor();
    const int a = c.alpha();
    // Löschstärke aus dem Alpha der aktiven Farbe ableiten: alpha=255
    // bedeutet vollständiges Löschen, alpha=0 bedeutet kein Löschen.
    m_eraseStrength = (m_erasing ? (a / 255.0f) : 1.0f);
    m_penColor = m_erasing ? qRgba(0, 0, 0, 0)
                           : qRgba(c.red() * a / 255, c.green() * a / 255,
                                   c.blue() * a / 255, a);

    m_lastPx = m_prevPx = QPoint(px, py);
    plot(px, py);

    emit m_doc->changed();
    return true;
}

void PencilTool::mouseMove(const QPointF& docPos, Qt::KeyboardModifiers) {
    if (!m_drawing) return;

    const int nx = qBound(0, int(std::floor(docPos.x())), m_doc->width() - 1);
    const int ny = qBound(0, int(std::floor(docPos.y())), m_doc->height() - 1);
    if (nx == m_lastPx.x() && ny == m_lastPx.y()) return;

    // Pixel-Perfekt: L-Form erkennen.
    //  prev . last . new  bilden eine Ecke, wenn prev und new diagonal
    //  benachbart sind und last der gemeinsame orthogonale Knick ist.
    //  Dann last entfernen und direkt prev -> new brücken (saubere Diagonale).
    const bool corner = m_pixelPerfect
        && m_prevPx != m_lastPx
        && qAbs(m_prevPx.x() - nx) == 1 && qAbs(m_prevPx.y() - ny) == 1
        && qAbs(m_lastPx.x()  - nx) <= 1 && qAbs(m_lastPx.y()  - ny) <= 1;

    if (corner) {
        restore(m_lastPx.x(), m_lastPx.y());
        plot(nx, ny);
        m_lastPx = QPoint(nx, ny);   // prevPx bleibt (Brücke prev -> new)
    } else {
        line(m_lastPx.x(), m_lastPx.y(), nx, ny);
        m_prevPx = m_lastPx;
        m_lastPx = QPoint(nx, ny);
    }

    emit m_doc->changed();
}

void PencilTool::mouseRelease(const QPointF&, Qt::MouseButton button) {
    if (button != Qt::LeftButton && button != Qt::RightButton) return;
    if (!m_drawing) return;
    m_drawing = false;

    QRect docBounds(0, 0, m_doc->width(), m_doc->height());
    QRect r = m_dirtyRect.intersected(docBounds);
    if (!r.isEmpty()) {
        r = r.adjusted(-2, -2, 2, 2).intersected(docBounds);
        QImage after  = m_lay->image.copy(r);
        QImage before = m_beforeFull.copy(r);
        m_history->push(new StrokeCommand(m_doc, m_doc->activeLayerIndex(), r, before, after,
                                          m_erasing ? "Radieren" : "Bleistiftstrich"));
    }
    m_dirtyRect = QRect();
}

void PencilTool::growDirty(const QRect& r) {
    if (m_dirtyRect.isNull()) m_dirtyRect = r;
    else m_dirtyRect = m_dirtyRect.united(r);
}

// =========================================================================
// BrushTool
// =========================================================================

BrushTool::BrushTool(Document* doc, ColorState* cs, History* hist)
    : m_doc(doc), m_colors(cs), m_history(hist) {}

bool BrushTool::mousePress(const QPointF& docPos, Qt::MouseButton button, Qt::KeyboardModifiers) {
    if (button != Qt::LeftButton && button != Qt::RightButton) return false;
    if (!m_doc->activeLayer()) return false;
    if (m_doc->activeLayer()->locked) return false;  // Ebene gesperrt
    m_drawing = true;
    m_erasing = (button == Qt::RightButton);
    m_last = docPos;
    m_dirtyRect = QRect();
    m_beforeFull = m_doc->activeLayer()->image.copy();
    // Max-Maske für diesen Strich: komplett leer, wächst nur durch Maximum.
    m_strokeMask = QImage(m_doc->width(), m_doc->height(), QImage::Format_Alpha8);
    m_strokeMask.fill(0);
    paintDot(docPos, m_erasing);
    emit m_doc->changed();
    return true;
}

void BrushTool::mouseMove(const QPointF& docPos, Qt::KeyboardModifiers) {
    if (!m_drawing) return;
    paintLine(m_last, docPos, m_erasing);
    m_last = docPos;
    emit m_doc->changed();
}

void BrushTool::mouseRelease(const QPointF&, Qt::MouseButton button) {
    if (button != Qt::LeftButton && button != Qt::RightButton) return;
    if (!m_drawing) return;
    m_drawing = false;
    QRect docBounds(0, 0, m_doc->width(), m_doc->height());
    QRect r = m_dirtyRect.intersected(docBounds);
    if (r.isEmpty()) return;
    r = r.adjusted(-m_size, -m_size, m_size, m_size).intersected(docBounds);
    QImage after = m_doc->activeLayer()->image.copy(r);
    QImage before = m_beforeFull.copy(r);
    m_history->push(new StrokeCommand(m_doc, m_doc->activeLayerIndex(), r, before, after,
                                      m_erasing ? "Radieren" : "Pinselstrich"));
}

// Immer Max-Maske: Strich stapelt sich nie auf.
void BrushTool::paintDot(const QPointF& p, bool erase) {
    stampDab(p, erase);
}
//     (m_strokeMask), NICHT aufaddiert. So bleibt ein Strich überall so
//     weich wie ein einzelner Stempel – überlappende Stempel beim Halten
//     stapeln sich nicht auf.
//
// --- Max-Maske: Alpha-Werte werden über ein Maximum geführt
//     (m_strokeMask), NICHT aufaddiert. Strich stapelt sich nie.
//
// Dither-Modus: nur harte Pixel (Alpha 0 oder 255). Weichheit steuert die
//   Dither-Intensität:
//     Weichheit 0   → kein Dither, volle Pinselfüllung
//     Weichheit 100 → reiner Dither, Pixel haben Abstand voneinander
//
// Pro Pixel:
//   dabA        = Stempel-Deckkraft an dieser Stelle (Gamma-Kurve)
//   maskPixel   = max(bisheriger Wert, dabA)    <- keine Stapelung!
//   sichtbar    = Originalpixel + Pinselfarbe @ maskPixel
void BrushTool::stampDab(const QPointF& p, bool erase) {
    Layer* lay = m_doc->activeLayer();
    if (!lay || m_strokeMask.isNull()) return;

    const int docW = m_doc->width();
    const int docH = m_doc->height();
    const qreal cx = p.x();
    const qreal cy = p.y();
    const qreal radius = m_size / 2.0;

    const int x0 = qMax(0, int(std::floor(cx - radius)) - 1);
    const int y0 = qMax(0, int(std::floor(cy - radius)) - 1);
    const int x1 = qMin(docW - 1, int(std::ceil(cx + radius)) + 1);
    const int y1 = qMin(docH - 1, int(std::ceil(cy + radius)) + 1);
    if (x0 > x1 || y0 > y1) return;

    // Beim Löschen steuert das Alpha der aktiven Farbe die Löschstärke
    // (voller Alpha = vollständiges Löschen). Daher für Löschen nicht
    // auf 255 erzwingen, sondern das aktive Alpha verwenden.
    const QColor active = m_colors->activeColor();
    const QColor col    = erase ? QColor(0, 0, 0, active.alpha()) : active;
    const int bR = col.red(), bG = col.green(), bB = col.blue(), bA = col.alpha();

    // Weichheit 0   = komplett hart (Sprungfunktion, alle Pixel voll)
    // Weichheit > 0 = Gammakurve, Gamma steigt mit der Weichheit (tieferer Übergang)
    const bool  hard  = (m_softness <= 0);
    const qreal gamma = hard ? 0.0 : (0.5 + (m_softness / 100.0) * 3.0);
    const qreal invR  = (radius > 0.0) ? 1.0 / radius : 0.0;
    const qreal r2    = radius * radius;

    // Dither-Vorbereitung: Schwellenwert skaliert mit Weichheit
    // ditherFactor: 0.0 bei Weichheit=0 (kein Dither), 1.0 bei Weichheit=100 (reiner Dither)
    const qreal ditherFactor = m_softness / 100.0;

    QImage& layerImg = lay->image;
    const int layerBpl  = layerImg.bytesPerLine();
    const int beforeBpl = m_beforeFull.bytesPerLine();
    const int maskBpl   = m_strokeMask.bytesPerLine();

    // Auswahlmaske holen (falls vorhanden), um nur in ausgewählten Pixeln zu malen.
    const bool  hasSel  = m_doc->hasSelection();
    const QImage selMask = hasSel ? m_doc->selectionMask() : QImage();
    const uchar* selBits = hasSel ? selMask.constBits() : nullptr;
    const int   selBpl   = hasSel ? selMask.bytesPerLine() : 0;

    for (int y = y0; y <= y1; ++y) {
        const qreal dy = qreal(y) - cy;
        QRgb*       layerRow  = reinterpret_cast<QRgb*>(layerImg.bits() + y * layerBpl);
        const QRgb* beforeRow = reinterpret_cast<const QRgb*>(m_beforeFull.constBits() + y * beforeBpl);
        uchar*      maskRow   = m_strokeMask.bits() + y * maskBpl;
        const uchar* selRow   = selBits ? (selBits + y * selBpl) : nullptr;

        for (int x = x0; x <= x1; ++x) {
            // Nur in ausgewählten Pixeln zeichnen.
            if (selRow && selRow[x] == 0) continue;

            const qreal dx = qreal(x) - cx;

            // Stempel-Alpha an diesem Pixel bestimmen.
            int dabA;
            if (hard) {
                dabA = (dx * dx + dy * dy <= r2) ? 255 : 0;
            } else {
                const qreal dist = std::sqrt(dx * dx + dy * dy);
                if (dist >= radius) continue;
                const qreal t = dist * invR;                 // 0..1
                dabA = int(std::pow(1.0 - t, gamma) * 255.0);
            }
            if (dabA <= 0) continue;

            // Dither: mathematisch perfektes, versetztes Schachbrettmuster.
            // Nur harte Pixel. Weichheit steuert die Dither-Intensität:
            //   "helle" Felder ((x+y) gerade) haben niedrigere Schwelle → eher gezeichnet
            //   "dunkle" Felder ((x+y) ungerade) haben höhere Schwelle → eher übersprungen
            //   Weichheit 0   → helle immer, dunkle ab 50% → kaum sichtbares Dither
            //   Weichheit 100 → helle ab 50%, dunkle nie → reines Schachbrett mit Lücken
            if (m_dither) {
                const bool light = ((x + y) & 1) == 0;
                const qreal intensity = dabA / 255.0;
                const qreal threshold = light
                    ? (0.5 * ditherFactor)       // 0.0 .. 0.5
                    : (0.5 + 0.5 * ditherFactor); // 0.5 .. 1.0
                dabA = (intensity > threshold) ? 255 : 0;
            }
            if (dabA <= 0) continue;

            // --- Max-Logik: nur aktualisieren, wenn dieser Stempel
            //     opaker ist als alles Bisherige. Keine Stapelung! ---
            uchar& m = maskRow[x];
            if (dabA <= m) continue;
            m = static_cast<uchar>(dabA);

            // --- Pixel neu berechnen: Original + Farbe @ Masken-Alpha ---
            const QRgb before = beforeRow[x];
            const int  dA     = qAlpha(before);          // Original-Alpha
            const int  effA   = (dabA * bA) / 255;        // effektive Quelle
            const int  invA   = 255 - effA;

            int rA, rR, rG, rB;
            if (erase) {
                // DestinationOut: herausnehmen.
                rA = (dA * invA) / 255;
                rR = (qRed(before)   * invA) / 255;
                rG = (qGreen(before) * invA) / 255;
                rB = (qBlue(before)  * invA) / 255;
            } else {
                // SourceOver (premultiplied): Farbe darüberlegen.
                rA = effA + (dA * invA) / 255;
                rR = (bR * effA) / 255 + (qRed(before)   * invA) / 255;
                rG = (bG * effA) / 255 + (qGreen(before) * invA) / 255;
                rB = (bB * effA) / 255 + (qBlue(before)  * invA) / 255;
            }
            layerRow[x] = qRgba(rR, rG, rB, rA);
        }
    }

    growDirty(QRect(x0, y0, x1 - x0 + 1, y1 - y0 + 1));
}

// Interpoliert zwischen zwei Punkten. Der Stempelabstand hängt von der
// Weichheit ab, damit der Strich auch bei sehr weichen Pinseln und
// schnellem Ziehen lückenlos durchzieht:
//   - hart:           volle Scheiben überlappen stark → größerer Abstand ok
//   - weich (hohes Gamma): sichtbarer Kern ist klein → sehr enger Abstand
void BrushTool::paintLine(const QPointF& a, const QPointF& b, bool erase) {
    const qreal dist = std::hypot(b.x() - a.x(), b.y() - a.y());
    if (dist <= 0.0) return;

    const qreal radius = m_size / 2.0;
    qreal spacing;
    if (m_softness <= 0) {
        spacing = qMax(1.0, radius * 0.4);                 // hart: 0,2 × Größe
    } else {
        const qreal gamma = 0.5 + (m_softness / 100.0) * 3.0;
        spacing = qMax(0.5, radius / (3.0 + 4.0 * gamma)); // weich: sehr eng
    }

    const int steps = int(dist / spacing) + 1;
    // Bei i=1 beginnen, damit der Startpunkt (bereits gestempelt) nicht
    // doppelt gezeichnet wird.
    for (int i = 1; i <= steps; ++i) {
        const qreal t = qreal(i) / steps;
        paintDot(a + (b - a) * t, erase);
    }
}

void BrushTool::growDirty(const QRect& r) {
    if (m_dirtyRect.isNull()) m_dirtyRect = r;
    else m_dirtyRect = m_dirtyRect.united(r);
}

// =========================================================================
// RectSelectTool
// =========================================================================

RectSelectTool::RectSelectTool(Document* doc) : m_doc(doc) {}

bool RectSelectTool::mousePress(const QPointF& docPos, Qt::MouseButton button, Qt::KeyboardModifiers mods) {
    // Rechte Maustaste = Subtraktionsmodus (Auswahl aufheben).
    if (button == Qt::RightButton) {
        button = Qt::LeftButton;
        mods |= Qt::AltModifier;   // als Merkmal für Subtraktion nutzen
    }
    if (button != Qt::LeftButton) return false;

    m_dragging = true;
    m_addMode = bool(mods & Qt::ControlModifier);   // STRG = vereinigen
    m_subMode = bool(mods & Qt::AltModifier);        // ALT / Rechtsklick = subtrahieren
    QPointF snapped(std::floor(docPos.x()), std::floor(docPos.y()));

    if (m_mode == Lasso) {
        m_points.clear();
        m_points.push_back(snapped);
        m_lastPoint = snapped;
    } else {
        m_start = m_end = snapped;
    }
    return true;
}

void RectSelectTool::mouseMove(const QPointF& docPos, Qt::KeyboardModifiers) {
    if (!m_dragging) return;
    QPointF snapped(std::floor(docPos.x()), std::floor(docPos.y()));
    if (m_mode == Lasso) {
        if (snapped != m_lastPoint) {
            m_points.push_back(snapped);
            m_lastPoint = snapped;
            emit m_doc->changed();
        }
    } else {
        m_end = snapped;
        emit m_doc->changed();
    }
}

void RectSelectTool::mouseRelease(const QPointF&, Qt::MouseButton button) {
    // Sowohl Links- als auch Rechtsklick beenden den Ziehvorgang
    // (Rechtsklick = Subtraktion, in mousePress als m_subMode markiert).
    if (button != Qt::LeftButton && button != Qt::RightButton) return;
    if (!m_dragging) return;
    m_dragging = false;

    if (m_mode == Lasso) {
        finishLasso();
        return;
    }

    // Rechteck / Ellipse: pixelgenaue Maske im normalize+geschnittenen Rechteck.
    const int x1 = int(m_start.x()), y1 = int(m_start.y());
    const int x2 = int(m_end.x()),   y2 = int(m_end.y());
    QRect r = QRect(QPoint(x1, y1), QPoint(x2, y2)).normalized()
                  .intersected(QRect(0, 0, m_doc->width(), m_doc->height()));

    if (r.isEmpty()) {
        if (!m_addMode && !m_subMode) m_doc->clearSelection();
        return;
    }

    QImage mask(m_doc->width(), m_doc->height(), QImage::Format_Alpha8);
    mask.fill(0);
    const int bpl = mask.bytesPerLine();
    uchar* bits = mask.bits();

    if (m_mode == Ellipse) {
        // Pixelgenaue Ellipse: Pixel-Mittelpunkt innerhalb der Ellipse?
        const double cx  = r.x() + r.width()  / 2.0;
        const double cy  = r.y() + r.height() / 2.0;
        const double rx  = qMax(0.5, r.width()  / 2.0);
        const double ry  = qMax(0.5, r.height() / 2.0);
        const double rx2 = rx * rx;
        const double ry2 = ry * ry;
        for (int y = r.top(); y <= r.bottom(); ++y)
            for (int x = r.left(); x <= r.right(); ++x) {
                const double dx = (x + 0.5) - cx;
                const double dy = (y + 0.5) - cy;
                if ((dx * dx) / rx2 + (dy * dy) / ry2 <= 1.0)
                    bits[y * bpl + x] = 255;
            }
    } else {
        // Rechteck
        for (int y = r.top(); y <= r.bottom(); ++y)
            for (int x = r.left(); x <= r.right(); ++x)
                bits[y * bpl + x] = 255;
    }

    if (m_subMode)      m_doc->subtractFromSelection(mask);
    else if (m_addMode) m_doc->addToSelection(mask);
    else                m_doc->setSelection(mask);
}

void RectSelectTool::finishLasso() {
    if (m_points.size() < 3) {
        if (!m_addMode && !m_subMode) m_doc->clearSelection();
        emit m_doc->selectionChanged();
        emit m_doc->changed();
        return;
    }
    QPolygonF poly = m_points;
    if (poly.first() != poly.last()) poly.push_back(poly.first());
    QImage mask = polygonToMask(poly, m_doc->width(), m_doc->height());
    if (m_subMode)      m_doc->subtractFromSelection(mask);
    else if (m_addMode) m_doc->addToSelection(mask);
    else                m_doc->setSelection(mask);
}

void RectSelectTool::paintOverlay(QPainter& painter) const {
    if (!m_dragging) return;
    // Zieh-Vorschau: dünne Linie mit weißem Außen- und schwarzem Innenrahmen.
    // Cosmetic-Pinsel: Breite in Bildschirm-Pixel, wird NICHT mit dem Zoom
    // skaliert -> Rahmen bleibt bei jedem Zoom dünn (1-2px).
    QPen whitePen(QColor(255, 255, 255, 230), 2);
    QPen blackPen(QColor(0, 0, 0, 230), 1);
    whitePen.setCosmetic(true);
    blackPen.setCosmetic(true);

    auto drawOutlinedRect = [&](const QRectF& r) {
        painter.setPen(whitePen);
        painter.drawRect(r);
        painter.setPen(blackPen);
        painter.drawRect(r);
    };
    auto drawOutlinedEllipse = [&](const QRectF& r) {
        painter.setPen(whitePen);
        painter.drawEllipse(r);
        painter.setPen(blackPen);
        painter.drawEllipse(r);
    };

    painter.setBrush(Qt::NoBrush);
    if (m_mode == Lasso) {
        if (m_points.size() < 2) return;
        painter.setPen(whitePen);
        painter.drawPolyline(m_points);
        painter.setPen(blackPen);
        painter.drawPolyline(m_points);
    } else {
        // Die Maske umfasst Pixel (x1,y1)..(x2,y2) INKLUSIVE – das sind
        // (x2-x1+1)x(y2-y1+1) Pixel. Der visuelle Rahmen muss daher bis
        // an die Unterkante des Endpixels reichen (+1), sonst ist die
        // Vorschau um genau ein Pixel zu klein und deckt sich nicht mit
        // der tatsächlich ausgewählten Fläche.
        const QRectF vis(m_start, m_end + QPointF(1, 1));
        if (m_mode == Ellipse) drawOutlinedEllipse(vis.normalized());
        else                   drawOutlinedRect(vis.normalized());
    }
}

// =========================================================================
// MagicWandTool
// =========================================================================

MagicWandTool::MagicWandTool(Document* doc) : m_doc(doc) {}

bool MagicWandTool::mousePress(const QPointF& docPos, Qt::MouseButton button, Qt::KeyboardModifiers) {
    // Rechtsklick: aktuelle Auswahl aufheben.
    if (button == Qt::RightButton) {
        if (m_doc->hasSelection()) m_doc->clearSelection();
        return true;
    }
    if (button != Qt::LeftButton) return false;
    QPoint p(int(std::floor(docPos.x())), int(std::floor(docPos.y())));
    if (p.x() < 0 || p.x() >= m_doc->width() || p.y() < 0 || p.y() >= m_doc->height())
        return false;
    // Quelle: aktive Ebene oder Komposit (ebenenübergreifend).
    QImage src = m_crossLayer ? m_doc->composite()
                              : (m_doc->activeLayer() ? m_doc->activeLayer()->image : QImage());
    if (src.isNull()) return false;
    QImage mask = (m_matchMode == Contiguous)
        ? floodFillMask(src, p, m_tolerance)
        : globalMatchMask(src, src.pixel(p), m_tolerance);
    m_doc->setSelection(mask);
    return true;
}

// =========================================================================
// FillTool
// =========================================================================

FillTool::FillTool(Document* doc, ColorState* cs, History* hist)
    : m_doc(doc), m_colors(cs), m_history(hist) {}

bool FillTool::mousePress(const QPointF& docPos, Qt::MouseButton button, Qt::KeyboardModifiers) {
    if (button != Qt::LeftButton && button != Qt::RightButton) return false;
    Layer* lay = m_doc->activeLayer();
    if (!lay) return false;
    if (lay->locked) return false;  // Ebene gesperrt
    QPoint p(int(std::floor(docPos.x())), int(std::floor(docPos.y())));
    if (p.x() < 0 || p.x() >= m_doc->width() || p.y() < 0 || p.y() >= m_doc->height())
        return false;

    const bool erase = (button == Qt::RightButton);   // Rechtsklick = löschen
    QImage src = m_crossLayer ? m_doc->composite() : lay->image;
    QImage mask = (m_matchMode == Contiguous)
        ? floodFillMask(src, p, m_tolerance)
        : globalMatchMask(src, src.pixel(p), m_tolerance);

    QImage before = lay->image.copy();
    const int w = m_doc->width(), h = m_doc->height();
    const int maskBpl = mask.bytesPerLine();
    const uchar* mbits = mask.constBits();
    const int imgBpl   = lay->image.bytesPerLine();
    QRgb* ibits = reinterpret_cast<QRgb*>(lay->image.bits());

    QColor c = m_colors->activeColor();
    // Löschstärke aus Alpha (255 = voll, 0 = gar nicht).
    const float eraseStrength = (erase ? (c.alpha() / 255.0f) : 1.0f);
    const int fa = c.alpha();
    const QRgb prem = qRgba(c.red() * fa / 255, c.green() * fa / 255,
                            c.blue() * fa / 255, fa);

    // Auswahl als zusätzliches Limit.
    const bool hasSel = m_doc->hasSelection();
    const QImage selMask = hasSel ? m_doc->selectionMask() : QImage();
    const uchar* sbits = hasSel ? selMask.constBits() : nullptr;
    const int selBpl   = hasSel ? selMask.bytesPerLine() : 0;

    const float keep = 1.0f - eraseStrength;
    for (int y = 0; y < h; ++y) {
        const uchar* mrow = mbits + y * maskBpl;
        const uchar* srow = sbits ? (sbits + y * selBpl) : nullptr;
        QRgb* irow = ibits + y * (imgBpl / 4);
        for (int x = 0; x < w; ++x) {
            if (mrow[x] == 0) continue;
            if (srow && srow[x] == 0) continue;
            if (erase && eraseStrength < 1.0f) {
                // Partielles Löschen: Pixel alpha-mäßig reduzieren.
                const QRgb cur = irow[x];
                const int newA = qRound(qAlpha(cur) * keep);
                const int newR = qRound(qRed(cur)   * keep);
                const int newG = qRound(qGreen(cur) * keep);
                const int newB = qRound(qBlue(cur)  * keep);
                irow[x] = qRgba(newR, newG, newB, newA);
            } else {
                irow[x] = prem;
            }
        }
    }
    emit m_doc->changed();
    QRect full(0, 0, w, h);
    m_history->push(new StrokeCommand(m_doc, m_doc->activeLayerIndex(),
                                       full, before, lay->image.copy(full),
                                       erase ? "Bereich löschen" : "Füllen"));
    return true;
}

// =========================================================================
// MoverTool (Auswahl verschieben / ausgewählte Pixel verschieben)
// =========================================================================

MoverTool::MoverTool(Document* doc, History* hist)
    : m_doc(doc), m_history(hist) {}

bool MoverTool::mousePress(const QPointF& docPos, Qt::MouseButton button, Qt::KeyboardModifiers) {
    // Rechtsklick: NUR die Auswahl aufheben. Die Pixel bleiben, wo sie sind.
    // Ein schwebender Paste-Buffer wird dafür auf die Ebene gestempelt
    // (übernommen), damit der eingefügte Inhalt erhalten bleibt. Ohne Buffer
    // wird ausschließlich die Auswahlmaske gelöscht – die Ebene unangetastet.
    if (button == Qt::RightButton) {
        if (m_doc->hasPasteBuffer() && m_doc->activeLayer()) {
            Layer* lay = m_doc->activeLayer();
            QRect full(0, 0, m_doc->width(), m_doc->height());
            QImage before = lay->image.copy(full);
            m_doc->stampPasteBuffer();
            QImage after = lay->image.copy(full);
            m_history->push(new StrokeCommand(m_doc, m_doc->activeLayerIndex(),
                                              full, before, after, "Einfügen"));
            m_doc->clearPasteBuffer();
            m_mode = Pixels;
        }
        m_doc->clearSelection();
        if (onRightClickSwitchToSelect) onRightClickSwitchToSelect();
        return true;
    }
    if (button != Qt::LeftButton) return false;
    // Zuerst pruefen, ob ein Eck-Knoten getroffen wurde -> Resize statt Move.
    if (m_doc->hasSelection() || m_doc->hasPasteBuffer()) {
        DragKind hk = hitTestHandle(docPos);
        if (hk != DragKind::None) {
            beginResize(docPos, hk);
            return true;
        }
    }
    // Paste-Modus: schwebenden Buffer verschieben.
    if (m_mode == Paste) {
        if (!m_doc->hasPasteBuffer()) return false;
        m_dragging = true;
        m_lastPos = docPos;
        return true;
    }
    if (!m_doc->hasSelection()) return false;
    m_dragging = true;
    m_lastPos = docPos;
    m_pressPos = docPos;
    if (m_mode == Pixels) {
        Layer* lay = m_doc->activeLayer();
        if (!lay) return false;
        if (lay->locked) return false;  // Ebene gesperrt: Pixel nicht verschieben
        // Originalzustand der Ebene + Auswahlmaske sichern.
        m_beforeImage = lay->image.copy();
        m_origMask = m_doc->selectionMask().copy();
        // Ausgewählte Pixel einmalig erfassen (nur die Auswahl, Rest transparent).
        const int w = m_doc->width();
        const int h = m_doc->height();
        m_capturedPixels = QImage(w, h, QImage::Format_ARGB32_Premultiplied);
        m_capturedPixels.fill(Qt::transparent);
        const uchar* mb = m_origMask.constBits();
        const int mbpl = m_origMask.bytesPerLine();
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
                if (mb[y * mbpl + x] != 0)
                    m_capturedPixels.setPixel(x, y, m_beforeImage.pixel(x, y));
    }
    return true;
}

void MoverTool::mouseMove(const QPointF& docPos, Qt::KeyboardModifiers) {
    if (!m_dragging) return;
    // Resize-Modus: skaliert Pixel + Auswahl (Pixels), nur Auswahl (Selection)
    // oder Paste-Buffer (Paste).
    if (m_dragKind == DragKind::ResizeTL
        || m_dragKind == DragKind::ResizeTR
        || m_dragKind == DragKind::ResizeBL
        || m_dragKind == DragKind::ResizeBR) {
        updateResize(docPos);
        return;
    }
    if (m_mode == Paste) {
        QPoint delta = (docPos - m_lastPos).toPoint();
        if (delta.isNull()) return;
        m_doc->movePasteBuffer(delta.x(), delta.y());
        m_doc->moveSelection(delta.x(), delta.y());   // Auswahl folgt dem Buffer
        m_lastPos = docPos;
        emit m_doc->changed();
        return;
    }
    if (m_mode == Selection) {
        QPoint delta = (docPos - m_lastPos).toPoint();
        if (delta.isNull()) return;
        m_doc->moveSelection(delta.x(), delta.y());
        m_lastPos = docPos;
        return;
    }
    // Pixel-Modus: schwebende Verschiebung ohne Verzerrung.
    // Gesamt-Delta vom Press berechnen, Ebene auf Original zurücksetzen,
    // Quellbereich löschen, erfasste Pixel frisch an die Zielposition stempeln.
    Layer* lay = m_doc->activeLayer();
    if (!lay) return;
    const QPoint total = (docPos - m_pressPos).toPoint();
    const int w = m_doc->width();
    const int h = m_doc->height();

    // 1) Ebene auf den Originalzustand zurücksetzen.
    {
        QPainter p(&lay->image);
        p.setCompositionMode(QPainter::CompositionMode_Source);
        p.drawImage(0, 0, m_beforeImage);
    }
    // 2) Quellbereich (Original-Auswahl) löschen.
    {
        QPainter p(&lay->image);
        p.setCompositionMode(QPainter::CompositionMode_DestinationOut);
        p.drawImage(0, 0, m_origMask);
    }
    // 3) Erfasste Pixel an die Zielposition stempeln (SourceOver).
    {
        QPainter p(&lay->image);
        p.drawImage(total.x(), total.y(), m_capturedPixels);
    }
    // 4) Auswahlmaske = Originalmaske um Gesamt-Delta verschoben.
    {
        QImage newMask(w, h, QImage::Format_Alpha8);
        newMask.fill(0);
        QPainter mp(&newMask);
        mp.setCompositionMode(QPainter::CompositionMode_Source);
        mp.drawImage(total.x(), total.y(), m_origMask);
        mp.end();
        m_doc->setSelection(newMask);
    }
    m_lastPos = docPos;
    emit m_doc->changed();
}

void MoverTool::mouseRelease(const QPointF&, Qt::MouseButton button) {
    if (button != Qt::LeftButton) return;
    if (!m_dragging) return;
    // Wenn wir am Resizen waren, Resize abschließen.
    const bool wasResize = (m_dragKind != DragKind::None
                            && m_dragKind != DragKind::Move);
    if (wasResize) {
        endResize();
        return;
    }
    m_dragging = false;
    if (m_mode == Pixels && m_doc->activeLayer()) {
        QRect full(0, 0, m_doc->width(), m_doc->height());
        QImage after = m_doc->activeLayer()->image.copy(full);
        m_history->push(new StrokeCommand(m_doc, m_doc->activeLayerIndex(),
                                          full, m_beforeImage, after,
                                          "Auswahl verschieben"));
    }
    // Paste-Modus: beim Loslassen wird noch nicht gestempelt — der Buffer
    // schwebt weiter, bis Enter (übernehmen) oder Werkzeugwechsel.
}

void MoverTool::keyPress(QKeyEvent* e) {
    if (m_mode != Paste) return;
    if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
        // Übernehmen: auf Ebene stempeln + Verlauf.
        Layer* lay = m_doc->activeLayer();
        if (lay && m_doc->hasPasteBuffer()) {
            QRect full(0, 0, m_doc->width(), m_doc->height());
            QImage before = lay->image.copy(full);
            m_doc->stampPasteBuffer();
            QImage after = lay->image.copy(full);
            m_history->push(new StrokeCommand(m_doc, m_doc->activeLayerIndex(),
                                              full, before, after, "Einfügen"));
        }
        m_doc->clearPasteBuffer();
        m_mode = Pixels;      // zurück in den Pixel-Modus (Default nach Paste)
        emit m_doc->changed();
        e->accept();
    } else if (e->key() == Qt::Key_Escape) {
        m_doc->clearPasteBuffer();
        m_mode = Pixels;      // zurück in den Pixel-Modus (Default nach Paste)
        emit m_doc->changed();
        e->accept();
    }
}

void MoverTool::paintOverlay(QPainter& painter) const {
    // Aktuellen Zoom cachen (für Handle-Größe und Hit-Testing in doc-Koordinaten).
    m_viewZoom = painter.transform().m11();
    if (m_viewZoom <= 0.0) m_viewZoom = 1.0;

    // Eck-Knoten zeichnen, wenn eine Auswahl oder Paste-Buffer existiert.
    const QRectF b = const_cast<MoverTool*>(this)->selectionBoundsF();
    if (b.isNull() || b.width() <= 0 || b.height() <= 0) return;

    drawHandle(painter, b.topLeft());
    drawHandle(painter, b.topRight());
    drawHandle(painter, b.bottomLeft());
    drawHandle(painter, b.bottomRight());
}

// Handle-Größe in Bildschirm-Pixeln (unabhängig vom Zoom).
// Der Painter ist bereits in Dokument-Koordinaten (transformiert),
// daher zeichnen wir ein Rechteck in doc-Koordinaten, dessen Größe
// = handleScreenSize / zoom ist. So bleiben die Handle-Quadrate auf
// dem Bildschirm immer gleich groß.
void MoverTool::drawHandle(QPainter& painter, const QPointF& docCorner) const {
    const qreal hs = 5.0 / m_viewZoom;   // halbe Handle-Größe in doc-Pixeln
    QRectF rect(docCorner.x() - hs, docCorner.y() - hs, hs * 2, hs * 2);
    painter.setBrush(QColor(80, 160, 255, 230));
    painter.setPen(QPen(QColor(255, 255, 255, 230), 1.0 / m_viewZoom));
    painter.drawRect(rect);
}

QRectF MoverTool::selectionBoundsF() const {
    if (m_doc->hasPasteBuffer()) {
        QRect r = m_doc->pasteBounds();
        return QRectF(r);
    }
    if (m_doc->hasSelection()) {
        QRect r = m_doc->selectionBounds();
        return QRectF(r);
    }
    return QRectF();
}

MoverTool::DragKind MoverTool::hitTestHandle(const QPointF& docPos) const {
    const QRectF b = selectionBoundsF();
    if (b.isNull() || b.width() <= 0 || b.height() <= 0) return DragKind::None;
    const qreal hs = 6.0 / qMax(0.001, m_viewZoom);   // etwas größere Hit-Zone
    const qreal tol = hs;
    auto near = [&](const QPointF& c) {
        return qAbs(docPos.x() - c.x()) <= tol && qAbs(docPos.y() - c.y()) <= tol;
    };
    if (near(b.topLeft()))     return DragKind::ResizeTL;
    if (near(b.topRight()))    return DragKind::ResizeTR;
    if (near(b.bottomLeft()))  return DragKind::ResizeBL;
    if (near(b.bottomRight())) return DragKind::ResizeBR;
    return DragKind::None;
}

void MoverTool::beginResize(const QPointF& docPos, DragKind kind) {
    // Nur im Pixels-Modus werden die Ebenen-Pixel mitskaliert.
    // Im Selection-Modus wird nur die Maske skaliert.
    // Im Paste-Modus wird nur der Paste-Buffer skaliert.
    m_dragKind = kind;
    m_dragging = true;
    m_lastPos  = docPos;
    m_pressPos = docPos;
    m_resizeOrigBounds = selectionBoundsF();

    // Gegenüberliegende Ecke als Anker (bleibt während Resize fix).
    const QRectF& b = m_resizeOrigBounds;
    switch (kind) {
        case DragKind::ResizeTL: m_resizeAnchor = b.bottomRight(); break;
        case DragKind::ResizeTR: m_resizeAnchor = b.bottomLeft();  break;
        case DragKind::ResizeBL: m_resizeAnchor = b.topRight();    break;
        case DragKind::ResizeBR: m_resizeAnchor = b.topLeft();     break;
        default: break;
    }

    if (m_mode == Pixels) {
        Layer* lay = m_doc->activeLayer();
        if (!lay) { m_dragKind = DragKind::None; m_dragging = false; return; }
        if (lay->locked) { m_dragKind = DragKind::None; m_dragging = false; return; }
        m_resizeBeforeImage = lay->image.copy();
        m_resizeOrigMask    = m_doc->selectionMask().copy();
        // Auswahl-Pixel auf full-doc Bild kopieren (maskiert).
        const int w = m_doc->width(), h = m_doc->height();
        m_resizeOrigPixels = QImage(w, h, QImage::Format_ARGB32_Premultiplied);
        m_resizeOrigPixels.fill(Qt::transparent);
        const uchar* mb = m_resizeOrigMask.constBits();
        const int mbpl = m_resizeOrigMask.bytesPerLine();
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
                if (mb[y * mbpl + x] != 0)
                    m_resizeOrigPixels.setPixel(x, y, m_resizeBeforeImage.pixel(x, y));
    }
}

void MoverTool::endResize() {
    if (m_mode == Pixels && m_doc->activeLayer()) {
        QRect full(0, 0, m_doc->width(), m_doc->height());
        QImage after = m_doc->activeLayer()->image.copy(full);
        m_history->push(new StrokeCommand(m_doc, m_doc->activeLayerIndex(),
                                          full, m_resizeBeforeImage, after,
                                          "Auswahl skalieren"));
    }
    m_dragKind = DragKind::None;
    m_dragging = false;
}

void MoverTool::updateResize(const QPointF& docPos) {
    // Neue Bounds aus Anker + aktueller Mausposition berechnen.
    // Anker = gegenueberliegende Ecke, bleibt fix.
    QPointF cur = docPos;
    cur.setX(qBound<qreal>(0.0, cur.x(), qreal(m_doc->width())));
    cur.setY(qBound<qreal>(0.0, cur.y(), qreal(m_doc->height())));
    QRectF newB(m_resizeAnchor, cur);
    newB = newB.normalized();
    if (newB.width()  < 1) newB.setWidth(1);
    if (newB.height() < 1) newB.setHeight(1);

    const QRect origRect = m_resizeOrigBounds.toRect();

    if (m_mode == Pixels) {
        Layer* lay = m_doc->activeLayer();
        if (!lay) return;
        const int w = m_doc->width(), h = m_doc->height();

        // 1) Ebene auf Originalzustand zuruecksetzen.
        {
            QPainter p(&lay->image);
            p.setCompositionMode(QPainter::CompositionMode_Source);
            p.drawImage(0, 0, m_resizeBeforeImage);
        }
        // 2) Original-Auswahlbereich loeschen.
        {
            QPainter p(&lay->image);
            p.setCompositionMode(QPainter::CompositionMode_DestinationOut);
            p.drawImage(0, 0, m_resizeOrigMask);
        }
        // 3) Erfasste Pixel auf neue Groesse skalieren + stempeln.
        {
            QImage cropped = m_resizeOrigPixels.copy(origRect);
            if (!cropped.isNull() && !newB.size().toSize().isEmpty()) {
                QImage scaled = cropped.scaled(newB.size().toSize(),
                                               Qt::IgnoreAspectRatio,
                                               Qt::SmoothTransformation);
                QPainter p(&lay->image);
                p.drawImage(QPointF(newB.left(), newB.top()), scaled);
            }
        }
        // 4) Auswahlmaske ebenfalls skalieren und uebernehmen.
        {
            QImage croppedMask = m_resizeOrigMask.copy(origRect);
            QImage scaledMask = scaleAlphaMaskSmooth(croppedMask,
                                                     newB.size().toSize());
            QImage fullMask(w, h, QImage::Format_Alpha8);
            fullMask.fill(0);
            QPainter pm(&fullMask);
            pm.setCompositionMode(QPainter::CompositionMode_Source);
            pm.drawImage(QPointF(newB.left(), newB.top()), scaledMask);
            pm.end();
            m_doc->setSelection(fullMask);
        }
    } else if (m_mode == Selection) {
        // Nur die Maske wird skaliert.
        QImage croppedMask = m_resizeOrigMask.copy(origRect);
        QImage scaledMask = scaleAlphaMaskSmooth(croppedMask,
                                                 newB.size().toSize());
        QImage fullMask(m_doc->width(), m_doc->height(), QImage::Format_Alpha8);
        fullMask.fill(0);
        QPainter pm(&fullMask);
        pm.setCompositionMode(QPainter::CompositionMode_Source);
        pm.drawImage(QPointF(newB.left(), newB.top()), scaledMask);
        pm.end();
        m_doc->setSelection(fullMask);
    } else if (m_mode == Paste) {
        // Paste-Buffer mitskaliert (falls vorhanden).
        QImage pb = m_doc->pasteBuffer();
        if (!pb.isNull()) {
            QRect pbo = m_doc->pasteBounds();
            QPointF a2(m_resizeOrigBounds.right()  - cur.x() < 0 ? pbo.right()  : pbo.left(),
                       m_resizeOrigBounds.bottom() - cur.y() < 0 ? pbo.bottom() : pbo.top());
            // Skalierung relativ zu Original-Bounds:
            const qreal sx = newB.width()  / m_resizeOrigBounds.width();
            const qreal sy = newB.height() / m_resizeOrigBounds.height();
            QSize ns(qMax(1, int(pbo.width()  * sx)),
                     qMax(1, int(pbo.height() * sy)));
            QImage scaled = pb.scaled(ns, Qt::IgnoreAspectRatio,
                                      Qt::SmoothTransformation);
            // Neue Position: Anker + Skalierung des Original-Offsets.
            const QPointF relOrig(pbo.topLeft() - m_resizeOrigBounds.topLeft());
            const QPointF relNew(relOrig.x() * sx, relOrig.y() * sy);
            const QPoint newPos = (newB.topLeft() + relNew).toPoint();
            m_doc->setPasteBuffer(scaled, newPos);
        }
    }
    m_lastPos = docPos;
    emit m_doc->changed();
}

// =========================================================================
// PipetteTool
// =========================================================================

PipetteTool::PipetteTool(Document* doc, ColorState* cs)
    : m_doc(doc), m_colors(cs) {}

bool PipetteTool::mousePress(const QPointF& docPos, Qt::MouseButton button, Qt::KeyboardModifiers) {
    if (button != Qt::LeftButton) return false;
    Layer* lay = m_doc->activeLayer();
    if (!lay) return false;
    QPoint p(int(std::floor(docPos.x())), int(std::floor(docPos.y())));
    if (p.x() < 0 || p.x() >= m_doc->width() || p.y() < 0 || p.y() >= m_doc->height())
        return false;
    // Quelle: aktive Ebene oder Komposit (ebenenübergreifend).
    QImage src = m_crossLayer ? m_doc->composite() : lay->image;
    QRgb pixel = src.pixel(p);
    QColor col(pixel);
    if (col.isValid())
        m_colors->setActiveColor(col);
    return true;
}

// =========================================================================
// FormTool
// =========================================================================

FormTool::FormTool(Document* doc, ColorState* cs, History* hist)
    : m_doc(doc), m_colors(cs), m_history(hist) {}

bool FormTool::mousePress(const QPointF& docPos, Qt::MouseButton button, Qt::KeyboardModifiers) {
    if (button != Qt::LeftButton && button != Qt::RightButton) return false;
    if (m_doc->activeLayer() && m_doc->activeLayer()->locked) return false;  // Ebene gesperrt
    m_dragging = true;
    m_erasing = (button == Qt::RightButton);   // Rechtsklick = löschen
    // Startpunkt auf ganze Pixel snapen → die Form springt von Anfang an
    // im Pixelraster (wie das Endergebnis), keine fließende Bewegung.
    m_start = m_end = QPointF(std::floor(docPos.x()), std::floor(docPos.y()));
    return true;
}

void FormTool::mouseMove(const QPointF& docPos, Qt::KeyboardModifiers) {
    if (!m_dragging) return;
    m_end = QPointF(std::floor(docPos.x()), std::floor(docPos.y()));
    emit m_doc->changed();
}

void FormTool::mouseRelease(const QPointF&, Qt::MouseButton button) {
    if (button != Qt::LeftButton && button != Qt::RightButton) return;
    if (!m_dragging) return;
    m_dragging = false;
    Layer* lay = m_doc->activeLayer();
    if (!lay) return;
    QImage before = lay->image.copy();
    QRectF rect = QRectF(m_start, m_end).normalized();

    QColor col = m_colors->activeColor();
    QPainter painter(&lay->image);
    // Bei Strichdicke 1 kein Antialiasing → nur komplette Pixel, harte Kanten.
    painter.setRenderHint(QPainter::Antialiasing, m_strokeWidth != 1);
    // Zeichnen auf die Auswahl begrenzen (falls eine besteht).
    if (m_doc->hasSelection())
        painter.setClipRegion(m_doc->selectionRegion());
    if (m_erasing) {
        painter.setCompositionMode(QPainter::CompositionMode_DestinationOut);
        // Löschstärke aus Alpha (opacity skaliert das DestinationOut).
        painter.setOpacity(col.alpha() / 255.0);
        col = Qt::black;   // bei DestinationOut zählt nur der Alpha
    }
    // Gefüllt → nur Füllung, kein Rand (Dicke hat dann keinen Einfluss).
    // Nicht gefüllt → nur Rand mit der eingestellten Dicke.
    if (m_filled) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(QBrush(col));
    } else {
        QPen pen(col, m_strokeWidth);
        pen.setJoinStyle(Qt::MiterJoin);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
    }

    // Anzeigename der Form-Aktion (vom Modus abhängig).
    QString actionName;
    const char* shapeWord = m_erasing ? "löschen" : "form";
    switch (m_mode) {
        case Rectangle: actionName = QString("Rechteck-%1").arg(shapeWord); break;
        case Ellipse:   actionName = QString("Kreis-%1").arg(shapeWord); break;
        case Triangle:  actionName = QString("Dreieck-%1").arg(shapeWord); break;
        case Line:      actionName = QString("Linie-%1").arg(shapeWord); break;
    }

    switch (m_mode) {
        case Rectangle: painter.drawRect(rect); break;
        case Ellipse:   painter.drawEllipse(rect); break;
        case Triangle: {
            QPolygonF tri;
            tri << QPointF(rect.left() + rect.width() / 2, rect.top())
                << QPointF(rect.left(), rect.bottom())
                << QPointF(rect.right(), rect.bottom());
            painter.drawPolygon(tri);
            break;
        }
        case Line:      painter.drawLine(m_start, m_end); break;
    }
    painter.end();
    emit m_doc->changed();
    QRect full(0, 0, m_doc->width(), m_doc->height());
    m_history->push(new StrokeCommand(m_doc, m_doc->activeLayerIndex(),
                                       full, before, lay->image.copy(full),
                                       actionName));
}

void FormTool::paintOverlay(QPainter& painter) const {
    if (!m_dragging) return;
    QRectF rect = QRectF(m_start, m_end).normalized();

    // Bei Strichdicke 1 kein Antialiasing → Vorschau wie das fertige Pixelbild.
    painter.setRenderHint(QPainter::Antialiasing, m_strokeWidth != 1);

    if (m_erasing) {
        // Beim Löschen kann das Overlay kein echtes Pixel-Löschen zeigen –
        // daher klassische gestrichelte Vorschau der Lösch-Form.
        painter.setPen(QPen(QColor(255, 255, 255, 200), 2, Qt::DashLine));
        painter.setBrush(Qt::NoBrush);
    } else {
        // Echte Formvorschau: sichtbar in Farbe, Strichdicke und Füllung,
        // exakt wie sie beim Loslassen gezeichnet wird.
        QColor col = m_colors->activeColor();
        if (m_filled) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(QBrush(col));
        } else {
            QPen pen(col, m_strokeWidth);
            pen.setJoinStyle(Qt::MiterJoin);
            painter.setPen(pen);
            painter.setBrush(Qt::NoBrush);
        }
    }

    switch (m_mode) {
        case Rectangle: painter.drawRect(rect); break;
        case Ellipse:   painter.drawEllipse(rect); break;
        case Triangle: {
            QPolygonF tri;
            tri << QPointF(rect.left() + rect.width() / 2, rect.top())
                << QPointF(rect.left(), rect.bottom())
                << QPointF(rect.right(), rect.bottom());
            painter.drawPolygon(tri);
            break;
        }
        case Line: painter.drawLine(m_start, m_end); break;
    }
}

// =========================================================================
// TextTool
// =========================================================================

TextTool::TextTool(Document* doc, ColorState* cs, History* hist)
    : m_doc(doc), m_colors(cs), m_history(hist) {}

bool TextTool::mousePress(const QPointF& docPos, Qt::MouseButton button, Qt::KeyboardModifiers) {
    if (button != Qt::LeftButton) return false;
    if (m_doc->activeLayer() && m_doc->activeLayer()->locked) return false;  // Ebene gesperrt
    // Bereits laufende Eingabe abschließen, dann neue beginnen.
    if (m_editing) commit();
    m_editing = true;
    m_pos = docPos;
    m_text.clear();
    emit m_doc->changed();
    return true;
}

void TextTool::keyPress(QKeyEvent* e) {
    if (!m_editing) return;
    if (e->key() == Qt::Key_Escape) {
        m_editing = false; m_text.clear();
        emit m_doc->changed();
        return;
    }
    if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
        commit();
        return;
    }
    if (e->key() == Qt::Key_Backspace) {
        if (!m_text.isEmpty()) m_text.chop(1);
        emit m_doc->changed();
        return;
    }
    // Druckbare Zeichen übernehmen.
    if (!e->text().isEmpty()) {
        QChar ch = e->text().at(0);
        if (ch.isPrint()) {
            m_text += e->text();
            emit m_doc->changed();
        }
    }
}

void TextTool::commit() {
    if (!m_editing) return;
    m_editing = false;
    if (m_text.isEmpty()) { emit m_doc->changed(); return; }
    Layer* lay = m_doc->activeLayer();
    if (!lay) return;
    QImage before = lay->image.copy();
    QFont font;
    font.setPointSize(m_fontSize);
    QFontMetrics fm(font);
    QPainter painter(&lay->image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setFont(font);
    painter.setPen(m_colors->activeColor());
    if (m_doc->hasSelection())
        painter.setClipRegion(m_doc->selectionRegion());
    painter.drawText(m_pos, m_text);
    painter.end();
    QRect b = fm.boundingRect(m_text);
    QRect dirty = b.translated(m_pos.toPoint()).adjusted(-2, -2, 2, 2);
    dirty = dirty.intersected(QRect(0, 0, m_doc->width(), m_doc->height()));
    QRect full(0, 0, m_doc->width(), m_doc->height());
    m_history->push(new StrokeCommand(m_doc, m_doc->activeLayerIndex(),
                                       full, before, lay->image.copy(full),
                                       "Text"));
    m_text.clear();
    emit m_doc->changed();
}

void TextTool::paintOverlay(QPainter& painter) const {
    if (!m_editing || m_text.isEmpty()) {
        // Cursor-Balken an der Startposition zeigen.
        if (m_editing) {
            painter.setPen(QPen(QColor(255, 255, 255, 200), 1));
            painter.drawLine(m_pos, m_pos + QPointF(0, m_fontSize));
        }
        return;
    }
    QFont font;
    font.setPointSize(m_fontSize);
    painter.setFont(font);
    painter.setPen(QPen(QColor(255, 255, 255, 220), 2));
    painter.drawText(m_pos, m_text);
    painter.setPen(QPen(QColor(0, 0, 0, 220), 1));
    painter.drawText(m_pos, m_text);
}

// =========================================================================
// ToolManager
// =========================================================================

ToolManager::ToolManager(Document* doc, ColorState* cs, History* hist, QObject* parent)
    : QObject(parent), m_doc(doc), m_colors(cs), m_history(hist)
{
    m_pencil        = std::make_unique<PencilTool>(doc, cs, hist);
    m_brush         = std::make_unique<BrushTool>(doc, cs, hist);
    m_rectSelect    = std::make_unique<RectSelectTool>(doc);
    m_magicWand     = std::make_unique<MagicWandTool>(doc);
    m_fill          = std::make_unique<FillTool>(doc, cs, hist);
    m_mover         = std::make_unique<MoverTool>(doc, hist);
    m_pipette       = std::make_unique<PipetteTool>(doc, cs);
    m_text          = std::make_unique<TextTool>(doc, cs, hist);
    m_form          = std::make_unique<FormTool>(doc, cs, hist);

    m_active = m_pencil.get();
}

void ToolManager::setActiveTool(ITool* tool) {
    if (tool && m_active != tool) {
        m_active = tool;
    }
}

// Multi-Document: alle Tools auf neues Document/History umschwenken.
void ToolManager::setDocument(Document* doc) {
    if (!doc || m_doc == doc) return;
    m_doc = doc;
    for (ITool* t : allTools()) t->setDocument(doc);
}
void ToolManager::setHistory(History* hist) {
    if (!hist || m_history == hist) return;
    m_history = hist;
    for (ITool* t : allTools()) t->setHistory(hist);
}

QList<ITool*> ToolManager::allTools() const {
    return {
        m_pencil.get(),
        m_brush.get(),
        m_rectSelect.get(),
        m_magicWand.get(),
        m_fill.get(),
        m_mover.get(),
        m_pipette.get(),
        m_text.get(),
        m_form.get()
    };
}