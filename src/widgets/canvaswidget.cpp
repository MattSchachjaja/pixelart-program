#include "canvaswidget.h"
#include "document.h"
#include "tools.h"

#include <QOpenGLShaderProgram>
#include <QOpenGLTexture>
#include <QSurfaceFormat>
#include <QMatrix4x4>
#include <QPainter>
#include <QPen>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QEnterEvent>
#include <QKeyEvent>
#include <QTimer>
#include <cmath>

// Diskrete Zoom-Stufen (wie in typischen Pixel-Art-Programmen).
// Das sicher gestufte Zoomen verhindert, dass das Raster bei jeder
// Zoom-Aenderung subpixel-verschoben ist und "flackert".
namespace {
const float kZoomSteps[] = {
    0.0625f, 0.125f, 0.25f, 0.5f, 1.0f, 2.0f, 4.0f,
    8.0f, 16.0f, 32.0f
};
constexpr int kZoomStepCount = sizeof(kZoomSteps) / sizeof(kZoomSteps[0]);
}

CanvasWidget::CanvasWidget(Document* doc, ToolManager* tools, QWidget* parent)
    : QOpenGLWidget(parent), m_doc(doc), m_tools(tools)
{
    QSurfaceFormat fmt;
    fmt.setVersion(3, 3);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    fmt.setSamples(4);
    setFormat(fmt);
    setMouseTracking(true);
    setContextMenuPolicy(Qt::NoContextMenu);
    setFocusPolicy(Qt::StrongFocus);   // für Tastatur-Eingabe (Text)

    m_docConnChanged = connect(m_doc, &Document::changed, this, [this]() { m_dirty = true; update(); });
    m_docConnSel = connect(m_doc, &Document::selectionChanged, this, [this]() {
        m_selectionOverlayDirty = true;
        update();
    });

    // Marching-Ants-Animation: sorgt dafür, dass die Auswahl-Umrandung auf
    // jedem Hintergrund deutlich erkennbar bleibt (alternierende schwarz/
    // weiß gestrichelte Linien, die wandern). Der Timer läuft permanent,
    // aber das Update wird nur ausgelöst, wenn wirklich eine Auswahl
    // existiert – sonst bleibt die Leinwand ruhig.
    m_marchTimer = new QTimer(this);
    m_marchTimer->setInterval(120);
    connect(m_marchTimer, &QTimer::timeout, this, [this]() {
        if (m_doc && m_doc->hasSelection() && !m_selectionEdges.isEmpty()) {
            m_marchOffset = std::fmod(m_marchOffset + 1.0, 8.0);
            update();
        }
    });
    m_marchTimer->start();
}

// Multi-Document: Leinwand auf ein anderes Document umschwenken.
// Trennt die alten Signal-Verbindungen, verbindet das neue Document und
// erzwingt einen Voll-Reset (Textur + VBO + Auswahl-Overlay + Ansicht).
void CanvasWidget::setDocument(Document* doc) {
    if (!doc || doc == m_doc) return;

    // Alte Signal-Verbindungen trennen.
    QObject::disconnect(m_docConnChanged);
    QObject::disconnect(m_docConnSel);

    m_doc = doc;

    // Neue Signal-Verbindungen zum aktiven Document.
    m_docConnChanged = connect(m_doc, &Document::changed, this, [this]() { m_dirty = true; update(); });
    m_docConnSel = connect(m_doc, &Document::selectionChanged, this, [this]() {
        m_selectionOverlayDirty = true;
        update();
    });

    // Textur im GL-Kontext neu aufbauen (Größe kann abweichen).
    m_dirty = true;
    m_vboDirty = true;
    m_selectionOverlayDirty = true;
    resetView();
    // Geladenes/geschaltetes Document an das Fenster anpassen, damit die
    // gesamte Leinwand bearbeitbar bleibt (z. B. nach "Öffnen" oder Session-Wechsel).
    // Über das deferred-Flag, damit der GL-Kontext beim Resize aktiv ist.
    m_needsFit = true;
    update();
}

CanvasWidget::~CanvasWidget() {
    if (context()) {
        makeCurrent();
        m_tex.reset(); m_prog.reset();
        m_vbo.destroy(); m_vao.destroy();
        doneCurrent();
    }
}

void CanvasWidget::resetView() {
    m_zoom = 1.0f;
    centerDocument();
    snapPanToPixels();
    m_dirty = true;
    m_vboDirty = true;
    update();
    emit zoomChanged(m_zoom);
}

void CanvasWidget::initializeGL() {
    initializeOpenGLFunctions();

    m_prog = std::make_unique<QOpenGLShaderProgram>();
    m_prog->addShaderFromSourceCode(QOpenGLShader::Vertex,
        "#version 330 core\n"
        "layout(location=0) in vec2 a_pos;\n"
        "layout(location=1) in vec2 a_uv;\n"
        "uniform mat4 u_mvp;\n"
        "out vec2 v_uv;\n"
        "void main(){ v_uv = a_uv; gl_Position = u_mvp * vec4(a_pos,0,1); }\n");
    m_prog->addShaderFromSourceCode(QOpenGLShader::Fragment,
        "#version 330 core\n"
        "in vec2 v_uv;\n"
        "out vec4 frag;\n"
        "uniform sampler2D u_tex;\n"
        "uniform vec2 u_checkerCount;\n"
        "void main(){\n"
        "    vec4 tex = texture(u_tex, v_uv);\n"
        "    vec2 ch = floor(v_uv * u_checkerCount);\n"
        "    float c = mod(ch.x + ch.y, 2.0);\n"
        "    vec3 c1 = vec3(60.0/255.0);\n"
        "    vec3 c2 = vec3(90.0/255.0);\n"
        "    vec3 checker = mix(c1, c2, c);\n"
        "    frag = vec4(tex.rgb + (1.0 - tex.a) * checker, 1.0);\n"
        "}\n");
    m_prog->link();

    rebuildVBO();

    recreateTexture();

    m_dirty = false;
    glClearColor(0.12f, 0.12f, 0.14f, 1.0f);
}

void CanvasWidget::resizeGL(int w, int h) {
    glViewport(0, 0, w, h);
    // Nur beim allerersten Resize (Erstanzeige) das Dokument anpassen.
    // Bei späteren Größenänderungen (Vollbild, Fenster vergrößern/verkleinern)
    // bleibt die vom Nutzer gewählte Position und Zoom-Stufe erhalten,
    // sodass die Leinwand überall positioniert werden kann.
    if (m_firstResize) {
        m_firstResize = false;
        m_needsFit = true;
    }
    update();
}

void CanvasWidget::paintGL() {
    // Document an Widget-Größe anpassen (hier ist der GL-Kontext aktiv,
    // daher kommen keine Texture-Fehler mehr beim anschließenden Neuaufbau).
    if (m_needsFit) {
        m_needsFit = false;
        fitDocumentToWidget();
    }
    if (m_vboDirty) { rebuildVBO(); m_vboDirty = false; }
    if (!m_tex) return;
    if (m_dirty) {
        if (m_tex->width()  != m_doc->width() ||
            m_tex->height() != m_doc->height()) {
            recreateTexture();
        } else {
            // Format_RGBA8888 erzwingt die Speicherbyte-Reihenfolge R,G,B,A,
            // damit OpenGL (das hier als RGBA liest) die Kanäle richtig zuordnet.
            // Format_ARGB32 speichert auf Little-Endian als B,G,R,A -> ohne diese
            // Konvertierung würden Rot und Blau vertauscht (Blau erschiene rot).
            QImage img = m_doc->composite().convertToFormat(QImage::Format_RGBA8888_Premultiplied);
            m_tex->setData(QOpenGLTexture::RGBA, QOpenGLTexture::UInt8, img.constBits());
        }
        m_dirty = false;
    }

    glClear(GL_COLOR_BUFFER_BIT);

    // --- Dokumenttextur zeichnen ---
    m_prog->bind();
    QMatrix4x4 mvp;
    mvp.ortho(0, float(width()), float(height()), 0, -1, 1);
    mvp.translate(m_pan.x(), m_pan.y());
    mvp.scale(m_zoom, m_zoom);
    m_prog->setUniformValue("u_mvp", mvp);
    m_tex->bind(0);
    m_prog->setUniformValue("u_tex", 0);
    // Karomuster: 8 Dokument-Pixel pro Quadrat (wie in der Vorschau).
    m_prog->setUniformValue("u_checkerCount",
        QVector2D(m_doc->width() / 8.0f, m_doc->height() / 8.0f));
    m_vao.bind();
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    m_vao.release();
    m_prog->release();

    // --- Auswahl: Rahmen immer, blaue Füllung nur bei Auswahl-Werkzeug ---
    if (m_doc->hasSelection()) {
        if (m_selectionOverlayDirty) rebuildSelectionOverlay();

        // Blaue Füllung (hart, einfarbig) nur, wenn ein Auswahl-Werkzeug
        // aktiv ist. Bei anderen Werkzeugen bleibt sie ausgeblendet.
        if (isSelectionToolActive() && !m_selectionOverlay.isNull()) {
            QPainter painter(this);
            painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
            QRectF target(m_pan.x(), m_pan.y(),
                          m_doc->width() * m_zoom, m_doc->height() * m_zoom);
            QRectF source(0, 0, m_doc->width(), m_doc->height());
            painter.drawImage(target, m_selectionOverlay, source);
        }

        // Marching-Ants-Umrandung: schwarz/weiß gestrichelt mit halbperiodig
        // versetzten Strichen. So entsteht das klassische, wandern­de
        // Ameisen-Muster, das auf jedem Hintergrund (hell/dunkel/bunt)
        // gut sichtbar bleibt. Cosmetic-Pen: Strichbreite in Bildschirm-
        // Pixeln, unabhängig vom Zoom.
        if (!m_selectionEdges.isEmpty()) {
            QPainter painter(this);
            painter.setRenderHint(QPainter::Antialiasing, false);
            const qreal zx = m_pan.x(), zy = m_pan.y(), z = m_zoom;

            // Weißer Strich (Offset 0) und schwarzer Strich (Offset +4 =
            // halbe Periode) übereinander → überall mindestens ein
            // sichtbarer Strich, unabhängig vom Hintergrund.
            QPen whitePen(QColor(255, 255, 255, 230), 1);
            whitePen.setDashPattern(QVector<qreal>{4, 4});
            whitePen.setDashOffset(m_marchOffset);
            whitePen.setCosmetic(true);

            QPen blackPen(QColor(0, 0, 0, 230), 1);
            blackPen.setDashPattern(QVector<qreal>{4, 4});
            blackPen.setDashOffset(m_marchOffset + 4.0);
            blackPen.setCosmetic(true);

            painter.setPen(whitePen);
            for (const QLineF& l : m_selectionEdges)
                painter.drawLine(QLineF(zx + l.x1() * z, zy + l.y1() * z,
                                        zx + l.x2() * z, zy + l.y2() * z));
            painter.setPen(blackPen);
            for (const QLineF& l : m_selectionEdges)
                painter.drawLine(QLineF(zx + l.x1() * z, zy + l.y1() * z,
                                        zx + l.x2() * z, zy + l.y2() * z));
        }
    }

    // --- Werkzeug-Overlays ---
    if (m_tools->active()) {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.translate(m_pan.x(), m_pan.y());
        painter.scale(m_zoom, m_zoom);
        m_tools->active()->paintOverlay(painter);
    }

    // Pixelraster ab Zoom 8x.
    // Zeichnung pixel-perfekt: Linienpositionen werden auf int+0.5
    // gesnappt (Qt zeichnet 1px-Pen zentriert um die Pfadkoordinate,
    // d.h. auf halben Pixeln liegt die Linie exakt auf einem Pixel).
    // Antialiasing aus + cosmetic pen -> alle Linien exakt gleich dick.
    if (m_zoom >= 8.0f) {
        QPainter gp(this);
        gp.setRenderHint(QPainter::Antialiasing, false);
        gp.setRenderHint(QPainter::SmoothPixmapTransform, false);
        QPen gridPen(QColor(0, 0, 0, 90), 1);
        gridPen.setCosmetic(true);  // Breite in Bildschirm-Pixeln, zoom-unabhaengig
        gp.setPen(gridPen);

        const int docW = m_doc->width();
        const int docH = m_doc->height();
        // pan ist bereits ganzzahlig (snapPanToPixels), zoom ist diskret.
        // Daher liegen sx/sy auf ganzen Zahlen -> +0.5 = perfekte 1px-Linie.
        const int firstX = qMax(0, int(std::floor(-m_pan.x() / m_zoom)));
        const int lastX  = qMin(docW, int(std::ceil((width()  - m_pan.x()) / m_zoom)));
        for (int x = firstX; x <= lastX; ++x) {
            const int sx = int(std::round(m_pan.x() + x * m_zoom));
            gp.drawLine(QLineF(sx + 0.5, 0, sx + 0.5, height()));
        }
        const int firstY = qMax(0, int(std::floor(-m_pan.y() / m_zoom)));
        const int lastY  = qMin(docH, int(std::ceil((height() - m_pan.y()) / m_zoom)));
        for (int y = firstY; y <= lastY; ++y) {
            const int sy = int(std::round(m_pan.y() + y * m_zoom));
            gp.drawLine(QLineF(0, sy + 0.5, width(), sy + 0.5));
        }
        // Dokument-Rahmen: 2px weiß, auch cosmetic.
        QPen borderPen(QColor(255, 255, 255, 220), 2);
        borderPen.setCosmetic(true);
        gp.setPen(borderPen);
        const int bx = int(std::round(m_pan.x()));
        const int by = int(std::round(m_pan.y()));
        const int bw = int(std::round(docW * m_zoom));
        const int bh = int(std::round(docH * m_zoom));
        gp.drawRect(QRect(bx, by, bw, bh));
    }
}

void CanvasWidget::mousePressEvent(QMouseEvent* e) {
    if (e->button() == Qt::MiddleButton) {
        m_panning = true; m_lastPos = e->pos();
        setCursor(Qt::ClosedHandCursor);
        return;
    }
    if ((e->button() == Qt::LeftButton || e->button() == Qt::RightButton) && m_tools->active()) {
        QPointF docPos = screenToDoc(e->position());
        m_tools->active()->mousePress(docPos, e->button(), e->modifiers());
        updateCursor(); // Cursor nach Klick aktualisieren
    }
}

void CanvasWidget::mouseMoveEvent(QMouseEvent* e) {
    if (m_panning) {
        // delta ist bereits ganzzahlig (Mauskoordinaten sind es). Wenn
        // m_pan vor dem Drag ganzzahlig war, bleibt er es auch hier.
        m_pan += e->pos() - m_lastPos;
        m_lastPos = e->pos();
        update();
        return;
    }
    // Cursor basierend auf aktivem Tool setzen
    updateCursor();
    if (m_tools->active()) {
        QPointF docPos = screenToDoc(e->position());
        m_tools->active()->mouseMove(docPos, e->modifiers());
    }
    // Statusleiste über aktuelle Position informieren (immer, auch wenn
    // kein Werkzeug aktiv ist – nützlich beim Zoomen/Plazieren).
    emit mousePosChanged(screenToDoc(e->position()));
}

void CanvasWidget::mouseReleaseEvent(QMouseEvent* e) {
    if (e->button() == Qt::MiddleButton) {
        m_panning = false;
        setCursor(Qt::ArrowCursor);
        return;
    }
    if (m_tools->active()) {
        QPointF docPos = screenToDoc(e->position());
        m_tools->active()->mouseRelease(docPos, e->button());
        updateCursor();
    }
}

void CanvasWidget::wheelEvent(QWheelEvent* e) {
    const QPointF docBefore = screenToDoc(e->position());

    // Pixel-Grafik-Zoom: diskrete Stufen (6.25% .. 3200%) statt
    // kontinuierlichem Faktor. So bleibt das Bild pixel-perfekt
    // ausgerichtet und das Raster flackert nicht.
    const int curIdx = zoomStepIndex(m_zoom);
    int newIdx = curIdx;
    if (e->angleDelta().y() > 0) {
        if (curIdx < kZoomStepCount - 1) newIdx = curIdx + 1;
    } else {
        if (curIdx > 0) newIdx = curIdx - 1;
    }
    const float newZoom = kZoomSteps[newIdx];
    if (newZoom == m_zoom) return;
    m_zoom = newZoom;

    // Cursor-Dokumentposition erhalten (wie bisher), danach auf
    // ganze Bildschirm-Pixel snapshen -> pixel-perfekt.
    const QPointF screenAfter = docBefore * m_zoom + m_pan;
    m_pan += e->position() - screenAfter;
    snapPanToPixels();
    update();
    emit zoomChanged(m_zoom);
}

// Findet die Stufe in der Leiter, die z am naechsten liegt.
int CanvasWidget::zoomStepIndex(float z) const {
    int best = 0;
    float bestDist = std::abs(z - kZoomSteps[0]);
    for (int i = 1; i < kZoomStepCount; ++i) {
        const float d = std::abs(z - kZoomSteps[i]);
        if (d < bestDist) { bestDist = d; best = i; }
    }
    return best;
}

float CanvasWidget::snapZoomToStep(float z) const {
    return kZoomSteps[zoomStepIndex(z)];
}

// Rundet m_pan auf ganzzahlige Bildschirmkoordinaten. Zusammen mit
// diskreten Zoom-Stufen sorgt das dafuer, dass Dokument-Pixel und
// Rasterlinien immer sauber auf Bildschirm-Pixeln liegen -> kein
// Flackern, keine ungleich dicken Rasterlinien.
void CanvasWidget::snapPanToPixels() {
    m_pan.setX(std::round(m_pan.x()));
    m_pan.setY(std::round(m_pan.y()));
}

void CanvasWidget::enterEvent(QEnterEvent* event) {
    Q_UNUSED(event);
    updateCursor();
}

void CanvasWidget::keyPressEvent(QKeyEvent* e) {
    // Delete-Taste: Auswahl-Inhalt löschen.
    // Wird direkt hier behandelt, damit es zuverlässig greift, sobald die
    // Leinwand den Fokus hat. Ein QShortcut auf dem MainWindow ist bei
    // QOpenGLWidget nicht immer zuverlässig.
    // Ausgenommen ist die laufende Texteingabe (TextTool), damit die
    // Entf-Taste dort nicht versehentlich den Auswahl-Inhalt löscht.
    const bool isTextEditing =
        m_tools && m_tools->active()
        && dynamic_cast<TextTool*>(m_tools->active())
        && static_cast<TextTool*>(m_tools->active())->isEditing();
    if (e->key() == Qt::Key_Delete && !isTextEditing) {
        emit deleteRequested();
        e->accept();
        return;
    }

    // Auswahl aufheben (Default: Leertaste). QShortcut ist bei
    // QOpenGLWidget nicht zuverlässig – daher direkt hier prüfen.
    // Während der Texteingabe nicht auslösen, damit die Taste zum Text gehört.
    if (!isTextEditing && !m_clearSelectionKey.isEmpty()) {
        // KeypadModifier stört bei Vergleichen – herausfiltern.
        const Qt::KeyboardModifiers mods = e->modifiers() & ~Qt::KeypadModifier;
        const QKeySequence pressed(e->key() | int(mods));
        if (pressed.matches(m_clearSelectionKey) == QKeySequence::ExactMatch) {
            emit clearSelectionRequested();
            e->accept();
            return;
        }
    }

    // An aktives Werkzeug weiterreichen (z. B. Text-Eingabe).
    if (m_tools && m_tools->active())
        m_tools->active()->keyPress(e);
    QOpenGLWidget::keyPressEvent(e);
}

void CanvasWidget::centerDocument() {
    m_pan = QPointF((width()  - m_doc->width()  * m_zoom) / 2.0f,
                    (height() - m_doc->height() * m_zoom) / 2.0f);
    snapPanToPixels();
}

// Erzeugt die GL-Textur sauber neu (vermeidet Qt6-Fehlermeldungen wie
// "Cannot change format once storage has been allocated"). Wird immer
// dann aufgerufen, wenn sich die Document-Größe geändert hat.
void CanvasWidget::recreateTexture() {
    if (!m_doc) return;
    // Siehe paintGL: RGBA8888 sorgt für korrekte Kanalreihenfolge beim
    // Upload in die OpenGL-Textur (verhindert Rot/Blau-Vertauschung).
    QImage img = m_doc->composite().convertToFormat(QImage::Format_RGBA8888_Premultiplied);
    if (img.isNull()) return;

    m_tex = std::make_unique<QOpenGLTexture>(QOpenGLTexture::Target2D);
    m_tex->setFormat(QOpenGLTexture::RGBA8_UNorm);
    m_tex->setSize(img.width(), img.height());
    m_tex->setMipLevels(1);
    m_tex->setMinificationFilter(QOpenGLTexture::Nearest);
    m_tex->setMagnificationFilter(QOpenGLTexture::Nearest);
    m_tex->allocateStorage(QOpenGLTexture::RGBA, QOpenGLTexture::UInt8);
    m_tex->setData(QOpenGLTexture::RGBA, QOpenGLTexture::UInt8, img.constBits());
}

// Passt die ANSICHT (Zoom + Pan) an die Widget-Größe an, sodass das
// Dokument mit seiner konfigurierten Pixelanzahl vollständig sichtbar ist.
// Die Pixelgröße des Dokuments bleibt dabei UNVERÄNDERT – sie richtet sich
// ausschließlich nach den Leinwandgrößen-Einstellungen (x/y Pixelanzahl).
//
// WICHTIG: Der Zoom wird auf die nächstkleinere diskrete Stufe gesnappt,
// damit die Ansicht pixel-perfekt bleibt (kein Flackern, sauberes Raster).
void CanvasWidget::fitDocumentToWidget() {
    if (!m_doc) return;
    int w = width();
    int h = height();
    if (w <= 0 || h <= 0) return;

    const float docW = float(m_doc->width());
    const float docH = float(m_doc->height());
    const float zx = float(w) / docW;
    const float zy = float(h) / docH;
    const float fit = qMin(zx, zy);

    // Kleinste Stufe wählen, in die das Dokument noch komplett passt,
    // oder – falls keine passt – die größte verfügbare.
    float target = kZoomSteps[0];
    for (int i = 0; i < kZoomStepCount; ++i) {
        if (kZoomSteps[i] <= fit) target = kZoomSteps[i];
    }
    m_zoom = target;

    centerDocument();
    emit zoomChanged(m_zoom);
}

void CanvasWidget::rebuildVBO() {
    const float W = float(m_doc->width());
    const float H = float(m_doc->height());
    const float verts[] = { 0,0,0,0,  W,0,1,0,  0,H,0,1,  W,H,1,1 };
    if (!m_vao.isCreated()) m_vao.create();
    if (!m_vbo.isCreated()) m_vbo.create();
    m_vao.bind();
    m_vbo.bind();
    m_vbo.allocate(verts, sizeof(verts));
    const int stride = 4 * sizeof(float);
    m_prog->enableAttributeArray(0);
    m_prog->setAttributeBuffer(0, GL_FLOAT, 0,                 2, stride);
    m_prog->enableAttributeArray(1);
    m_prog->setAttributeBuffer(1, GL_FLOAT, 2 * sizeof(float), 2, stride);
    m_vao.release();
}

void CanvasWidget::updateCursor() {
    if (!m_tools->active()) {
        setCursor(Qt::ArrowCursor);
        return;
    }
    // Alle Werkzeuge verwenden wie das Pixelstiftwerkzeug ein Fadenkreuz,
    // solange sich die Maus über der Leinwand befindet.
    setCursor(Qt::CrossCursor);
}

// Baut die blaue Einfärbung (harte Kante, einfarbig) UND die Umriss-Kanten
// der Auswahl auf. Wird nur bei Änderung der Auswahl aufgerufen
// (selectionChanged), nicht pro Paint-Frame.
void CanvasWidget::rebuildSelectionOverlay() {
    QImage mask = m_doc->selectionMask();
    m_selectionEdges.clear();
    if (mask.isNull() || mask.size() != QSize(m_doc->width(), m_doc->height())) {
        m_selectionOverlay = QImage();
        m_selectionOverlayDirty = false;
        return;
    }

    const int w   = mask.width();
    const int h   = mask.height();
    const int bpl = mask.bytesPerLine();
    const uchar* bits = mask.constBits();
    auto sel = [&](int x, int y) -> bool {
        if (x < 0 || x >= w || y < 0 || y >= h) return false;
        return bits[y * bpl + x] != 0;
    };

    // --- Umriss-Kanten als horizontale/vertikale Linien ---
    // Horizontale Kanten: zwischen Zeile y-1 und y.
    for (int y = 0; y <= h; ++y) {
        int runStart = -1;
        for (int x = 0; x <= w; ++x) {
            bool edge = (x < w) && (sel(x, y) != sel(x, y - 1));
            if (edge && runStart < 0) runStart = x;
            if (!edge && runStart >= 0) {
                m_selectionEdges.append(QLineF(runStart, y, x, y));
                runStart = -1;
            }
        }
    }
    // Vertikale Kanten: zwischen Spalte x-1 und x.
    for (int x = 0; x <= w; ++x) {
        int runStart = -1;
        for (int y = 0; y <= h; ++y) {
            bool edge = (y < h) && (sel(x, y) != sel(x - 1, y));
            if (edge && runStart < 0) runStart = y;
            if (!edge && runStart >= 0) {
                m_selectionEdges.append(QLineF(x, runStart, x, y));
                runStart = -1;
            }
        }
    }

    // --- Blaue Füllung (hart, einfarbig): Blau + auf Maske begrenzen ---
    m_selectionOverlay = QImage(w, h, QImage::Format_ARGB32_Premultiplied);
    m_selectionOverlay.fill(QColor(0, 100, 255, 80));
    QPainter p(&m_selectionOverlay);
    p.setCompositionMode(QPainter::CompositionMode_DestinationIn);
    p.drawImage(0, 0, mask);
    p.end();

    m_selectionOverlayDirty = false;
}

// true, wenn ein auswahlerzeugendes Werkzeug aktiv ist (dann wird die
// blaue Füllung angezeigt; sonst nur der Rahmen).
// Der Verschieber zählt NICHT dazu – dort stört die Füllung.
bool CanvasWidget::isSelectionToolActive() const {
    ITool* t = m_tools ? m_tools->active() : nullptr;
    if (!t) return false;
    return dynamic_cast<RectSelectTool*>(t)
        || dynamic_cast<MagicWandTool*>(t);
}
