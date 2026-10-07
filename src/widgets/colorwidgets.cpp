#include "colorwidgets.h"
#include "colorstate.h"
#include "colorutils.h"

#include <QPainter>
#include <QPen>
#include <QPainterPath>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QSlider>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QApplication>

// ===================================================================
// ColorPicker — skalierbarer HSV-Farbwähler
// ===================================================================
ColorPicker::ColorPicker(ColorState* state, QWidget* parent)
    : QWidget(parent), m_state(state)
{
    setMinimumSize(100, 120);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setMouseTracking(true);
    setCursor(Qt::CrossCursor);
    connect(m_state, &ColorState::activeColorChanged, this, [this](const QColor& c) {
        // Nur aktualisieren, wenn die Aenderung NICHT von uns selbst kam.
        // Verhindert, dass der Rueckkopplungskreislauf
        // (emitColor -> activeColorChanged -> updateFromColor)
        // unsere eigenen m_h/m_s/m_v-Werte durch Fließkomma-Ungenauigkeit
        // beim Hin-und-Her-Rundtrip (HSV -> QColor -> HSV) veraendert.
        if (!m_selfUpdate)
            updateFromColor(c);
    });
    updateFromColor(m_state->activeColor());
}

void ColorPicker::resizeEvent(QResizeEvent*) {
    rebuildSvImage();
}

void ColorPicker::rebuildSvImage() {
    const int margin = 4;
    const int barH = 22;
    const int w = width() - margin * 2;
    const int svSize = w;
    if (svSize <= 0) return;

    m_svCache = QImage(svSize, svSize, QImage::Format_RGB32);
    for (int y = 0; y < svSize; ++y) {
        for (int x = 0; x < svSize; ++x) {
            float s = float(x) / qMax(1, svSize - 1);
            float v = float(svSize - 1 - y) / qMax(1, svSize - 1);
            // Qts native HSV-Konvertierung verwenden, damit die angezeigte
            // Farbe exakt mit der gezeichneten Farbe übereinstimmt.
            m_svCache.setPixel(x, y, QColor::fromHsvF(m_h / 360.0f, s, v).rgb());
        }
    }
    m_cachedHue = m_h;
}

void ColorPicker::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const int margin = 4;
    const int barH = 22;
    const int w = width() - margin * 2;
    if (w <= 0) return;

    QRectF hueRect(margin, margin, w, barH);
    const int svSize = w;
    QRectF svRect(margin, margin + barH + 6, svSize, svSize);

    // SV-Cache bei Hue-Aenderung neu aufbauen (nur wenn noetig)
    if (qAbs(m_h - m_cachedHue) > 0.01f) {
        rebuildSvImage();
    }

    // Farbbalken (Hue).
    // WICHTIG: Hue muss 0..359 sein (QColor::fromHsv lehnt 360 ab und
    // erzeugt eine UNGÜLTIGE Farbe → "out of range"-Warnung + falsche
    // Anzeige am rechten Rand). Mit % 360 wird 360° korrekt zu 0° (Rot).
    QLinearGradient hueGrad(hueRect.left(), 0, hueRect.right(), 0);
    for (int i = 0; i <= 12; ++i)
        hueGrad.setColorAt(i / 12.0, QColor::fromHsv((int(i * 360 / 12)) % 360, 255, 255));
    p.setBrush(hueGrad);
    p.setPen(QPen(QColor(0, 0, 0, 80), 1));
    p.drawRoundedRect(hueRect, 3, 3);

    // Hue-Marker.
    {
        qreal x = hueRect.left() + (m_h / 360.0f) * hueRect.width();
        QPainterPath tri;
        tri.moveTo(x, hueRect.top() - 2);
        tri.lineTo(x - 5, hueRect.top() - 9);
        tri.lineTo(x + 5, hueRect.top() - 9);
        tri.closeSubpath();
        p.setBrush(Qt::black);
        p.setPen(QPen(Qt::white, 1));
        p.drawPath(tri);
        QPainterPath tri2;
        tri2.moveTo(x, hueRect.bottom() + 2);
        tri2.lineTo(x - 5, hueRect.bottom() + 9);
        tri2.lineTo(x + 5, hueRect.bottom() + 9);
        tri2.closeSubpath();
        p.drawPath(tri2);
    }

    // SV-Quadrat (aus Cache).
    if (!m_svCache.isNull())
        p.drawImage(svRect, m_svCache);

    // SV-Cursor.
    {
        qreal cx = svRect.left() + m_s * svRect.width();
        qreal cy = svRect.top()  + (1.0 - m_v) * svRect.height();
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(Qt::white, 2));
        p.drawEllipse(QPointF(cx, cy), 6, 6);
        p.setPen(QPen(Qt::black, 1));
        p.drawEllipse(QPointF(cx, cy), 7, 7);
    }
}

// --- Maus-Events: Drag-Ziel beim Druck festlegen -------------------

void ColorPicker::mousePressEvent(QMouseEvent* e) {
    if (e->button() != Qt::LeftButton) return;

    const int margin = 4;
    const int barH = 22;
    const int w = width() - margin * 2;
    QRectF hueRect(margin, margin, w, barH);
    QRectF svRect(margin, margin + barH + 6, w, w);
    QPointF pos = e->pos();

    // Herausfinden, in welchem Bereich geklickt wurde,
    // und das Drag-Ziel einmalig festlegen.
    if (hueRect.contains(pos)) {
        m_dragTarget = Hue;
        // Drag beginnt -> Farbänderungen werden zu EINEM Command
        // zusammengefasst (siehe endColorChange in mouseReleaseEvent).
        m_state->beginColorChange();
        handleHueDrag(pos);
    } else if (svRect.contains(pos)) {
        m_dragTarget = SV;
        m_state->beginColorChange();
        handleSvDrag(pos);
    }
    // Klick ausserhalb beider Bereiche -> nichts tun
}

void ColorPicker::mouseMoveEvent(QMouseEvent* e) {
    if (m_dragTarget == None) return;

    QPointF pos = e->pos();
    if (m_dragTarget == Hue)
        handleHueDrag(pos);
    else if (m_dragTarget == SV)
        handleSvDrag(pos);
}

void ColorPicker::mouseReleaseEvent(QMouseEvent*) {
    m_dragTarget = None;
    // Drag beendet -> falls sich die Farbe geändert hat, wird jetzt
    // ein einzelnes Command gepusht (vorher → nachher).
    m_state->endColorChange();
}

// --- Getrennte Handler fuer Hue und SV ------------------------------

void ColorPicker::handleHueDrag(const QPointF& pos) {
    const int margin = 4;
    const int w = width() - margin * 2;
    if (w <= 0) return;

    float ratio = float((pos.x() - margin) / w);
    m_h = qBound(0.0f, ratio * 360.0f, 359.9f);

    // Signal-Rueckkopplung blockieren, damit updateFromColor
    // nicht m_s und m_v ueberschreibt.
    m_selfUpdate = true;
    emitColor();
    m_selfUpdate = false;

    update();
}

void ColorPicker::handleSvDrag(const QPointF& pos) {
    const int margin = 4;
    const int barH = 22;
    const int w = width() - margin * 2;
    if (w <= 0) return;

    const int svSize = w;
    const float svLeft = float(margin);
    const float svTop  = float(margin + barH + 6);

    // Auch wenn die Maus ausserhalb des Quadrats ist,
    // den Wert auf 0..1 begrenzen (kein Bereichswechsel!).
    m_s = qBound(0.0f, float((pos.x() - svLeft) / svSize), 1.0f);
    m_v = qBound(0.0f, float(1.0 - (pos.y() - svTop) / svSize), 1.0f);

    // Signal-Rueckkopplung blockieren, damit updateFromColor
    // nicht m_h durch Fließkomma-Ungenauigkeit veraendert.
    m_selfUpdate = true;
    emitColor();
    m_selfUpdate = false;

    update();
}

void ColorPicker::emitColor() {
    // Qts native HSV-Konvertierung – identisch zur Anzeige im SV-Quadrat,
    // damit die ausgewählte Farbe EXAKT mit der gezeichneten Farbe
    // übereinstimmt (keine Diskrepanz durch eigene HSV-Routinen).
    QColor c = QColor::fromHsvF(m_h / 360.0f, m_s, m_v);
    c.setAlpha(m_state->activeColor().alpha());
    m_state->setActiveColor(c);
}

void ColorPicker::updateFromColor(const QColor& c) {
    float h, s, v;
    c.getHsvF(&h, &s, &v);
    if (h < 0) h = 0;
    m_h = h * 360.0f;
    m_s = s;
    m_v = v;
    if (qAbs(m_h - m_cachedHue) > 0.01f)
        rebuildSvImage();
    update();
}

// ===================================================================
// ColorSwatch — skalierbares Farbfeld
// ===================================================================
ColorSwatch::ColorSwatch(int index, QWidget* parent)
    : QFrame(parent), m_index(index)
{
    setFrameShape(QFrame::NoFrame);
    setLineWidth(0);
    setMinimumSize(8, 8);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setCursor(Qt::PointingHandCursor);
}

void ColorSwatch::setColor(const QColor& c) {
    m_color = c;
    updateStyle();
}

void ColorSwatch::setActive(bool a) {
    m_active = a;
    updateStyle();
}

void ColorSwatch::resizeEvent(QResizeEvent*) {
    updateStyle();
}

void ColorSwatch::updateStyle() {
    if (width() < 1) return;
    if (m_active) {
        // Aktiver Slot: klare blaue Border zur Hervorhebung.
        int bw = qMax(2, qMin(6, width() / 10));
        setStyleSheet(
            QStringLiteral("QFrame{ background:%1; border:%2px solid #3a7bd5; }")
                .arg(m_color.name(QColor::HexArgb))
                .arg(bw));
    } else {
        // Inaktive Slots: keine Border, Slots liegen direkt nebeneinander.
        setStyleSheet(
            QStringLiteral("QFrame{ background:%1; border:none; }")
                .arg(m_color.name(QColor::HexArgb)));
    }
}

void ColorSwatch::mousePressEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton) emit clicked(m_index);
    QFrame::mousePressEvent(e);
}

// ===================================================================
// PaletteSwatch — skalierbares Palette-Feld
// ===================================================================
PaletteSwatch::PaletteSwatch(const QColor& c, QWidget* parent)
    : QFrame(parent), m_color(c)
{
    setFrameShape(QFrame::Box);
    setLineWidth(0);
    setMinimumSize(6, 6);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setCursor(Qt::PointingHandCursor);
}

void PaletteSwatch::resizeEvent(QResizeEvent*) {
    updateStyle();
}

void PaletteSwatch::updateStyle() {
    if (width() < 1) return;
    // Rahmenbreite proportional zur Groesse (1px bei 16px, skaliert hoch)
    int bw = qMax(1, qMin(4, width() / 16));
    setStyleSheet(
        QStringLiteral("QFrame{ background:%1; border:%2px solid #333; }")
            .arg(m_color.name(QColor::HexArgb))
            .arg(bw));
}

void PaletteSwatch::mousePressEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton) emit picked(m_color);
    QFrame::mousePressEvent(e);
}

// ===================================================================
// ColorDock
// ===================================================================
ColorDock::ColorDock(ColorState* state, QWidget* parent)
    : QDockWidget("Farben", parent), m_state(state)
{
    setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    setFeatures(QDockWidget::DockWidgetMovable);

    // --- Inhalt aufbauen ---
    // Die 7 Farbslots sind ins "Palette"-Dock umgezogen. Dieses Dock
    // enthält nur die Farbauswahl (HSV-Picker + Transparenz) und eine
    // feste Palette mit gängigen Standardfarben zum schnellen Wählen.
    auto* content = new QWidget();
    auto* outer = new QVBoxLayout(content);
    outer->setContentsMargins(8, 10, 8, 8);
    outer->setSpacing(6);

    // --- Transparenz-Slider ---
    auto* alphaRow = new QHBoxLayout();
    alphaRow->setContentsMargins(0, 0, 0, 0);

    m_alphaSlider = new QSlider(Qt::Horizontal, this);
    m_alphaSlider->setRange(0, 255);
    m_alphaSlider->setValue(255);
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    m_alphaSlider->setClickOutsideSlider(true);
#endif
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
    m_alphaSlider->setClickOnAccess(QSlider::ClickOnAccess::SliderGroove);
#endif
    m_alphaSlider->setStyleSheet(
        "QSlider::groove:horizontal{ background:#444; height:6px; border-radius:3px; }"
        "QSlider::handle:horizontal{ background:#3a7bd5; width:14px; margin:-5px 0; border-radius:7px; }"
        "QSlider::sub-page:horizontal{ background:#3a7bd5; border-radius:3px; }");
    alphaRow->addWidget(m_alphaSlider, 1);

    m_alphaLabel = new QLabel("100%", this);
    m_alphaLabel->setStyleSheet("color:#aaa; min-width:32px;");
    m_alphaLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    alphaRow->addWidget(m_alphaLabel);
    outer->addLayout(alphaRow);

    connect(m_alphaSlider, &QSlider::valueChanged, this, [this](int val) {
        QColor c = m_state->activeColor();
        c.setAlpha(val);
        m_state->setActiveColor(c);
        m_alphaLabel->setText(QString::number(val * 100 / 255) + "%");
    });

    // --- ColorPicker (skalierbar) ---
    m_picker = new ColorPicker(m_state, this);
    outer->addWidget(m_picker, 1);

    // --- Feste Palette unten ---
    // Gängige Standardfarben zum schnellen Auswählen (Klick -> aktive Farbe).
    auto* palGrid = new QGridLayout();
    palGrid->setSpacing(2);
    palGrid->setContentsMargins(0, 0, 0, 0);
    const QColor palette[] = {
        Qt::black, QColor(64, 64, 64), QColor(128, 128, 128),
        QColor(192, 192, 192), Qt::white,
        QColor(255, 0, 0), QColor(255, 128, 0), QColor(255, 255, 0),
        QColor(128, 255, 0), QColor(0, 255, 0),
        QColor(0, 255, 128), QColor(0, 255, 255), QColor(0, 128, 255),
        QColor(0, 0, 255), QColor(128, 0, 255), QColor(255, 0, 255),
        QColor(255, 0, 128), QColor(128, 64, 0), QColor(160, 82, 45),
        QColor(255, 200, 100)
    };
    int col = 0, row = 0;
    for (const QColor& c : palette) {
        auto* sw = new PaletteSwatch(c, this);
        connect(sw, &PaletteSwatch::picked, m_state, &ColorState::setActiveColor);
        palGrid->addWidget(sw, row, col);
        if (++col >= 10) { col = 0; ++row; }
    }
    outer->addLayout(palGrid);

    // --- Widget direkt setzen ---
    setWidget(content);

    // --- Breiten-/Höhen-Grenzen ---
    // Früher wurde hier ein Aspect-Ratio erzwungen; das hat aber mit dem
    // Einklapp-Mechanismus kollidiert (das Dock hat beim Ausklappen die
    // gespeicherte Höhe überschrieben). Daher nur noch min-Größe.
    setMinimumSize(220, 400);
    setMaximumWidth(440);

    // --- ColorState-Signal -> Alpha-Slider aktuell halten ---
    // (Slots und Palette werden im PaletteDock gepflegt.)
    connect(m_state, &ColorState::activeColorChanged, this, [this](const QColor&) {
        int alpha = m_state->activeColor().alpha();
        m_alphaSlider->blockSignals(true);
        m_alphaSlider->setValue(alpha);
        m_alphaSlider->blockSignals(false);
        m_alphaLabel->setText(QString::number(alpha * 100 / 255) + "%");
    });
}

QSize ColorDock::sizeHint() const {
    return QSize(260, 440);
}
