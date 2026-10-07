#pragma once

#include <QString>
#include <QImage>
#include <QRect>
#include <QList>

class Document;

// Befehlsschnittstelle fuer das Undo/Redo-System.
class ICommand {
public:
    virtual ~ICommand() = default;
    virtual void execute() = 0;
    virtual void undo() = 0;
    virtual QString name() const = 0;
};

class AddLayerCommand : public ICommand {
public:
    AddLayerCommand(Document* doc, const QString& layerName);
    void execute() override;
    void undo() override;
    QString name() const override { return "Ebene hinzufuegen"; }
private:
    Document* m_doc;
    QString   m_name;
    int       m_index = -1;
};

class RemoveLayerCommand : public ICommand {
public:
    RemoveLayerCommand(Document* doc, int index);
    void execute() override;
    void undo() override;
    QString name() const override { return "Ebene loeschen"; }
private:
    Document* m_doc;
    int       m_index;
    QString   m_name;
    QImage    m_image;
    bool      m_visible = true;
    float     m_opacity = 1.0f;
};

class MoveLayerCommand : public ICommand {
public:
    MoveLayerCommand(Document* doc, int from, int to);
    void execute() override;
    void undo() override;
    QString name() const override;
private:
    Document* m_doc;
    int       m_from;
    int       m_to;
};

class DuplicateLayerCommand : public ICommand {
public:
    DuplicateLayerCommand(Document* doc, int index);
    void execute() override;
    void undo() override;
    QString name() const override { return "Ebene duplizieren"; }
private:
    Document* m_doc;
    int       m_index;      // Urspruenglicher Index der zu kopierenden Ebene
    int       m_newIndex = -1; // Index der eingefuegten Kopie nach execute()
    QImage    m_copy;      // Kopie des Bildes (fuer Undo)
    QString   m_name;      // Name der neuen Ebene
};

class MergeLayerCommand : public ICommand {
public:
    MergeLayerCommand(Document* doc, int upperIndex);
    void execute() override;
    void undo() override;
    QString name() const override { return "Ebenen zusammenfuegen"; }
private:
    Document* m_doc;
    int       m_upper, m_below;
    QString   m_name, m_lowerName;
    QImage    m_upperImg, m_lowerImg;
    bool      m_lowerVis;
    float     m_lowerOp;
};

class StrokeCommand : public ICommand {
public:
    StrokeCommand(Document* doc, int layerIndex,
                  const QRect& dirtyRect,
                  const QImage& before,
                  const QImage& after,
                  const QString& actionName = "Bleistiftstrich");
    void execute() override;
    void undo() override;
    QString name() const override { return m_actionName; }
private:
    Document* m_doc;
    int       m_layer;
    QRect     m_rect;
    QImage    m_before;
    QImage    m_after;
    QString   m_actionName;
};

class ResizeCanvasCommand : public ICommand {
public:
    ResizeCanvasCommand(Document* doc, int newW, int newH);
    void execute() override;
    void undo() override;
    QString name() const override { return "Leinwandgroesse geaendert"; }
private:
    Document*     m_doc;
    int           m_oldW, m_oldH, m_newW, m_newH;
    QList<QImage> m_oldImages;
};

// === Farbslot-Commands (für Undo/Redo der Palette) ===
class ColorState;

class ColorChangeCommand : public ICommand {
public:
    ColorChangeCommand(ColorState* state, int slotIndex,
                       const QColor& oldColor, const QColor& newColor);
    void execute() override;
    void undo() override;
    QString name() const override { return "Farbe geändert"; }
private:
    ColorState* m_state;
    int         m_slotIndex;
    QColor      m_oldColor;
    QColor      m_newColor;
};

class ColorInsertCommand : public ICommand {
public:
    ColorInsertCommand(ColorState* state, int index, const QColor& color);
    void execute() override;
    void undo() override;
    QString name() const override { return "Farbslot hinzugefügt"; }
private:
    ColorState* m_state;
    int         m_index;
    QColor      m_color;
};

class ColorRemoveCommand : public ICommand {
public:
    ColorRemoveCommand(ColorState* state, int index, const QColor& color);
    void execute() override;
    void undo() override;
    QString name() const override { return "Farbslot entfernt"; }
private:
    ColorState* m_state;
    int         m_index;
    QColor      m_color;
};
