#pragma once

#include <QWidget>
#include <QFrame>
#include <QDockWidget>
#include <QColor>

class QMouseEvent;
class QPaintEvent;
class QResizeEvent;
class QSlider;
class QLabel;
class QVBoxLayout;
class ColorState;

// Farbbalken (Hue) + SV-Quadrat zur freien Farbwahl.
// Skalierbar: zeichnet relativ zur aktuellen Widget-Groesse.
class ColorPicker : public QWidget {
    Q_OBJECT
public:
    explicit ColorPicker(ColorState* state, QWidget* parent = nullptr);
    QSize sizeHint() const override { return QSize(200, 220); }

protected:
    void paintEvent(QPaintEvent*) override;
    void resizeEvent(QResizeEvent*) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent*) override;

private:
    void rebuildSvImage();
    void handleHueDrag(const QPointF& pos);
    void handleSvDrag(const QPointF& pos);
    void emitColor();
    void updateFromColor(const QColor& c);

    // Merkt sich, in welchem Bereich der Drag gestartet wurde,
    // damit der andere Bereich waehrend des Zugs unberuehrt bleibt.
    enum DragTarget { None, Hue, SV };
    DragTarget m_dragTarget = None;

    // Verhindert, dass ein Signal-Rueckkopplungskreislauf
    // (emitColor -> activeColorChanged -> updateFromColor)
    // die eigenen Werte m_h, m_s, m_v ueberschreibt.
    bool m_selfUpdate = false;

    ColorState* m_state;
    float m_h = 0.0f, m_s = 1.0f, m_v = 1.0f;
    QImage m_svCache;
    float m_cachedHue = -1.0f;
};

// Ein anklickbares Farbfeld fuer einen der 7 Slots.
// Skalierbar: Groesse und Rahmen passen sich dynamisch an.
class ColorSwatch : public QFrame {
    Q_OBJECT
public:
    explicit ColorSwatch(int index, QWidget* parent = nullptr);
    void setColor(const QColor& c);
    void setActive(bool a);
    QColor color() const { return m_color; }
    int    index() const { return m_index; }

    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int w) const override { return w; }

signals:
    void clicked(int index);

protected:
    void resizeEvent(QResizeEvent*) override;
    void mousePressEvent(QMouseEvent* e) override;

private:
    void updateStyle();
    int    m_index;
    QColor m_color;
    bool   m_active = false;
};

// Ein Feld der festen Palette; Klick setzt die aktive Farbe.
// Skalierbar: Groesse und Rahmen passen sich dynamisch an.
class PaletteSwatch : public QFrame {
    Q_OBJECT
public:
    explicit PaletteSwatch(const QColor& c, QWidget* parent = nullptr);
    QColor color() const { return m_color; }

    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int w) const override { return w; }

signals:
    void picked(const QColor& c);

protected:
    void resizeEvent(QResizeEvent*) override;
    void mousePressEvent(QMouseEvent* e) override;

private:
    void updateStyle();
    QColor m_color;
};

// Dock mit HSV-ColorPicker + Transparenz-Slider zum Umstellen der
// aktiven Farbe. Die 7 Farbslots und die Palette sind ins "Palette"-Dock
// umgezogen.
class ColorDock : public QDockWidget {
    Q_OBJECT
public:
    explicit ColorDock(ColorState* state, QWidget* parent = nullptr);
    QSize sizeHint() const override;

private:
    ColorState*      m_state;
    ColorPicker*      m_picker;
    QSlider*         m_alphaSlider;
    QLabel*          m_alphaLabel;
};
