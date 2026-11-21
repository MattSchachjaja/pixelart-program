#pragma once

#include <QObject>
#include <QVector>
#include <QPointF>
#include <cmath>

// Globale Spiegel-Einstellungen (Live-Spiegelung beim Zeichnen).
//  - "horizontal": Spiegelung links <-> rechts an einer senkrechten Achse (axisX)
//  - "vertikal":   Spiegelung oben <-> unten an einer waagerechten Achse (axisY)
// Achsenpositionen in Dokumentkoordinaten (Vielfache von 0.5). Solange der Nutzer
// eine Achse nicht verschoben hat, liegt sie in der Dokumentmitte.
class MirrorSettings : public QObject {
    Q_OBJECT
public:
    static MirrorSettings& instance() {
        static MirrorSettings s;
        return s;
    }

    bool horizontal() const { return m_h; }
    bool vertical()   const { return m_v; }
    bool any()        const { return m_h || m_v; }

    void setHorizontal(bool on) { if (m_h != on) { m_h = on; emit changed(); } }
    void setVertical(bool on)   { if (m_v != on) { m_v = on; emit changed(); } }

    double axisX(int docW) const {
        const double a = std::isnan(m_ax) ? docW / 2.0 : m_ax;
        return qBound(0.0, a, double(docW));
    }
    double axisY(int docH) const {
        const double a = std::isnan(m_ay) ? docH / 2.0 : m_ay;
        return qBound(0.0, a, double(docH));
    }
    void setAxisX(double x) { x = std::round(x * 2.0) / 2.0; if (x != m_ax) { m_ax = x; emit changed(); } }
    void setAxisY(double y) { y = std::round(y * 2.0) / 2.0; if (y != m_ay) { m_ay = y; emit changed(); } }

    // Alle Spiegelvarianten (ohne das Original) als Flags (flipX, flipY).
    QVector<QPair<bool, bool>> variants() const {
        QVector<QPair<bool, bool>> v;
        if (m_h) v.append({true, false});
        if (m_v) v.append({false, true});
        if (m_h && m_v) v.append({true, true});
        return v;
    }

signals:
    void changed();

private:
    MirrorSettings() = default;
    bool   m_h = false;
    bool   m_v = false;
    double m_ax = std::nan("");
    double m_ay = std::nan("");
};
