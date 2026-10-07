#pragma once

#include <QPointF>
#include <QString>
#include <Qt>

class QPainter;
class QKeyEvent;
class Document;
class History;

// Schnittstelle für alle Zeichenwerkzeuge.
class ITool {
public:
    virtual ~ITool() = default;
    virtual QString name() const = 0;
    virtual bool mousePress(const QPointF& docPos, Qt::MouseButton button,
                            Qt::KeyboardModifiers mods) = 0;
    virtual void mouseMove(const QPointF& docPos,
                           Qt::KeyboardModifiers mods) = 0;
    virtual void mouseRelease(const QPointF& docPos, Qt::MouseButton button) = 0;
    virtual void paintOverlay(QPainter&) const {}
    virtual void keyPress(QKeyEvent*) {}   // für z.B. Text-Eingabe

    // Multi-Document: tauscht das Document/History, auf dem das Tool arbeitet.
    // Default tut nichts; Tools mit Member-Pointern überschreiben dies.
    virtual void setDocument(Document*) {}
    virtual void setHistory(History*) {}
};
