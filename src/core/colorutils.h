#pragma once

#include <QColor>
#include <QImage>
#include <cmath>

// HSV -> QColor. h in Grad (0..360), s und v in 0..1, a optionaler Alpha 0..255.
inline QColor hsvToColor(float h, float s, float v, int a = 255) {
    h = std::fmod(h, 360.0f); if (h < 0) h += 360.0f;
    float c = v * s;
    float hp = h / 60.0f;
    float x = c * (1.0f - std::fabs(std::fmod(hp, 2.0f) - 1.0f));
    float r = 0, g = 0, b = 0;
    if      (hp < 1) { r = c; g = x; b = 0; }
    else if (hp < 2) { r = x; g = c; b = 0; }
    else if (hp < 3) { r = 0; g = c; b = x; }
    else if (hp < 4) { r = 0; g = x; b = c; }
    else if (hp < 5) { r = x; g = 0; b = c; }
    else             { r = c; g = 0; b = x; }
    float m = v - c;
    return QColor(int((r + m) * 255), int((g + m) * 255), int((b + m) * 255), a);
}

// Passt Sättigung, Helligkeit und Kontrast eines Bildes an.
//   saturationDelta / brightnessDelta / contrastDelta: -100..+100 (0 = neutral)
// Die Eingabe darf jedes Format haben; das Ergebnis ist ARGB32_Premultiplied
// (passend zu den Ebenen in Document). Vollständig transparente Pixel
// bleiben unverändert, halbtransparente werden korrekt behandelt
// (vor dem Anpassen un-prämultipliziert, danach wieder prämultipliziert).
inline QImage adjustImage(const QImage& src,
                          int saturationDelta,
                          int brightnessDelta,
                          int contrastDelta)
{
    if (src.isNull()) return src;
    if (saturationDelta == 0 && brightnessDelta == 0 && contrastDelta == 0)
        return src.convertToFormat(QImage::Format_ARGB32_Premultiplied);

    QImage img = src.convertToFormat(QImage::Format_ARGB32);

    // Helligkeit als additive Verschiebung (-255..+255).
    const int brightI = brightnessDelta * 255 / 100;
    // Kontrast-Formel wie in vielen Bildeditoren (Center = 128).
    const float cFactor =
        (contrastDelta <= -99) ? 0.0f
        : (259.0f * (contrastDelta + 255)) /
          (255.0f * (259.0f - contrastDelta));
    // Sättigung als additive Verschiebung des S-Kanals in HSL (-1..+1).
    const float satF = saturationDelta / 100.0f;

    const bool doSat = (saturationDelta != 0);
    const bool doBri = (brightnessDelta != 0);
    const bool doCon = (contrastDelta != 0);

    for (int y = 0; y < img.height(); ++y) {
        QRgb* line = reinterpret_cast<QRgb*>(img.scanLine(y));
        for (int x = 0; x < img.width(); ++x) {
            QRgb px = line[x];
            const int a = qAlpha(px);
            if (a == 0) continue;

            int r = qRed(px);
            int g = qGreen(px);
            int b = qBlue(px);

            if (doBri) {
                r += brightI;
                g += brightI;
                b += brightI;
            }
            if (doCon) {
                r = int(cFactor * (r - 128) + 128);
                g = int(cFactor * (g - 128) + 128);
                b = int(cFactor * (b - 128) + 128);
            }
            r = qBound(0, r, 255);
            g = qBound(0, g, 255);
            b = qBound(0, b, 255);

            if (doSat) {
                QColor c(r, g, b);
                float h, s, l;
                c.getHslF(&h, &s, &l);
                s = std::clamp(s + satF, 0.0f, 1.0f);
                c.setHslF(h, s, l);
                r = c.red();
                g = c.green();
                b = c.blue();
            }

            line[x] = qRgba(r, g, b, a);
        }
    }
    return img.convertToFormat(QImage::Format_ARGB32_Premultiplied);
}

// Mischt zwei Bilder (gleicher Größe) anhand einer Alpha-8-Maske:
//   Ergebnis = adjusted * (mask/255) + original * (1 - mask/255)
// Wird für die Farbanpassung innerhalb einer Auswahl verwendet – Pixel
// außerhalb der Auswahl bleiben unangetastet, Pixel am weichen Rand
// werden linear interpoliert.
inline QImage blendWithMask(const QImage& original,
                            const QImage& adjusted,
                            const QImage& mask)
{
    if (original.size() != adjusted.size() || original.size() != mask.size())
        return adjusted;

    QImage o = original.convertToFormat(QImage::Format_ARGB32);
    QImage a = adjusted.convertToFormat(QImage::Format_ARGB32);
    QImage out = o.copy();

    const int w = out.width();
    const int h = out.height();
    for (int y = 0; y < h; ++y) {
        QRgb*        oline = reinterpret_cast<QRgb*>(out.scanLine(y));
        const QRgb*  aline = reinterpret_cast<const QRgb*>(a.constScanLine(y));
        const uchar* mline = mask.constScanLine(y);
        for (int x = 0; x < w; ++x) {
            const int ma = mline[x];
            if (ma == 0)   continue;                    // original bleibt
            if (ma == 255) { oline[x] = aline[x]; continue; }
            const QRgb op = oline[x];
            const QRgb ap = aline[x];
            const int inv = 255 - ma;
            const int r  = (qRed(ap)   * ma + qRed(op)   * inv) / 255;
            const int g  = (qGreen(ap) * ma + qGreen(op) * inv) / 255;
            const int b  = (qBlue(ap)  * ma + qBlue(op)  * inv) / 255;
            const int al = (qAlpha(ap) * ma + qAlpha(op) * inv) / 255;
            oline[x] = qRgba(r, g, b, al);
        }
    }
    return out.convertToFormat(QImage::Format_ARGB32_Premultiplied);
}
