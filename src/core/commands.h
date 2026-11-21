#pragma once

#include <QString>
#include <QImage>
#include <QRect>
#include <QList>
#include <QDataStream>
#include <QVector>

class Document;
class ColorState;

// Befehlsschnittstelle fuer das Undo/Redo-System.
class ICommand {
public:
    virtual ~ICommand() = default;
    virtual void execute() = 0;
    virtual void undo() = 0;
    virtual QString name() const = 0;

    // Serialisierung fuers Projektformat (.otrp).
    virtual int type() const = 0;
    virtual void write(QDataStream& out) const = 0;
};

// Liest einen Command aus dem Projektformat (Typ-ID wurde bereits gelesen).
// version = Version des Projektformats (aeltere Dateien enthalten z.B.
// noch keine Paletten-Zeilenstruktur in den Farbslot-Commands).
ICommand* loadCommandFromStream(QDataStream& in, int type,
                                Document* doc, ColorState* colors,
                                int version = 2);

// Typ-IDs fuer das Projektformat (stabil halten!).
enum CommandType {
    CmdAddLayer      = 1,
    CmdRemoveLayer   = 2,
    CmdMoveLayer     = 3,
    CmdDuplicateLayer= 4,
    CmdMergeLayer    = 5,
    CmdStroke        = 6,
    CmdResizeCanvas  = 7,
    CmdColorChange   = 8,
    CmdColorInsert   = 9,
    CmdColorRemove   = 10,
    CmdFlipCanvas    = 11
};

class AddLayerCommand : public ICommand {
public:
    AddLayerCommand(Document* doc, const QString& layerName);
    void execute() override;
    void undo() override;
    QString name() const override { return "Ebene hinzufuegen"; }
    int  type() const override { return CmdAddLayer; }
    void write(QDataStream& out) const override;
private:
    friend ICommand* loadCommandFromStream(QDataStream&, int, Document*, ColorState*, int);
    Document* m_doc;
    QString   m_name;
    int       m_index = -1;
    int       m_prevActive = -1;
};

class RemoveLayerCommand : public ICommand {
public:
    RemoveLayerCommand(Document* doc, int index);
    void execute() override;
    void undo() override;
    QString name() const override { return "Ebene loeschen"; }
    int  type() const override { return CmdRemoveLayer; }
    void write(QDataStream& out) const override;
private:
    friend ICommand* loadCommandFromStream(QDataStream&, int, Document*, ColorState*, int);
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
    int  type() const override { return CmdMoveLayer; }
    void write(QDataStream& out) const override;
private:
    friend ICommand* loadCommandFromStream(QDataStream&, int, Document*, ColorState*, int);
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
    int  type() const override { return CmdDuplicateLayer; }
    void write(QDataStream& out) const override;
private:
    friend ICommand* loadCommandFromStream(QDataStream&, int, Document*, ColorState*, int);
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
    int  type() const override { return CmdMergeLayer; }
    void write(QDataStream& out) const override;
private:
    friend ICommand* loadCommandFromStream(QDataStream&, int, Document*, ColorState*, int);
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
    int  type() const override { return CmdStroke; }
    void write(QDataStream& out) const override;
private:
    friend ICommand* loadCommandFromStream(QDataStream&, int, Document*, ColorState*, int);
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
    int  type() const override { return CmdResizeCanvas; }
    void write(QDataStream& out) const override;
private:
    friend ICommand* loadCommandFromStream(QDataStream&, int, Document*, ColorState*, int);
    Document*     m_doc;
    int           m_oldW, m_oldH, m_newW, m_newH;
    QList<QImage> m_oldImages;
};

// Spiegelt die komplette Leinwand (alle Ebenen + Auswahl).
// Zweimaliges Spiegeln = Original, daher ist undo() identisch zu execute().
class FlipCanvasCommand : public ICommand {
public:
    FlipCanvasCommand(Document* doc, bool horizontal, bool vertical);
    void execute() override;
    void undo() override;
    QString name() const override;
    int  type() const override { return CmdFlipCanvas; }
    void write(QDataStream& out) const override;
private:
    friend ICommand* loadCommandFromStream(QDataStream&, int, Document*, ColorState*, int);
    Document* m_doc;
    bool      m_h, m_v;
};

// === Farbslot-Commands (für Undo/Redo der Palette) ===

class ColorChangeCommand : public ICommand {
public:
    ColorChangeCommand(ColorState* state, int slotIndex,
                       const QColor& oldColor, const QColor& newColor);
    void execute() override;
    void undo() override;
    QString name() const override { return "Farbe geändert"; }
    int  type() const override { return CmdColorChange; }
    void write(QDataStream& out) const override;
private:
    friend ICommand* loadCommandFromStream(QDataStream&, int, Document*, ColorState*, int);
    ColorState* m_state;
    int         m_slotIndex;
    QColor      m_oldColor;
    QColor      m_newColor;
};

class ColorInsertCommand : public ICommand {
public:
    ColorInsertCommand(ColorState* state, int index, const QColor& color,
                       const QVector<int>& rowsBefore = {},
                       const QVector<int>& rowsAfter = {});
    void execute() override;
    void undo() override;
    QString name() const override { return "Farbslot hinzugefügt"; }
    int  type() const override { return CmdColorInsert; }
    void write(QDataStream& out) const override;
private:
    friend ICommand* loadCommandFromStream(QDataStream&, int, Document*, ColorState*, int);
    ColorState* m_state;
    int         m_index;
    QColor      m_color;
    QVector<int> m_rowsBefore;   // Paletten-Zeilen vor der Aenderung
    QVector<int> m_rowsAfter;    // Paletten-Zeilen nach der Aenderung
};

class ColorRemoveCommand : public ICommand {
public:
    ColorRemoveCommand(ColorState* state, int index, const QColor& color,
                       const QVector<int>& rowsBefore = {},
                       const QVector<int>& rowsAfter = {});
    void execute() override;
    void undo() override;
    QString name() const override { return "Farbslot entfernt"; }
    int  type() const override { return CmdColorRemove; }
    void write(QDataStream& out) const override;
private:
    friend ICommand* loadCommandFromStream(QDataStream&, int, Document*, ColorState*, int);
    ColorState* m_state;
    int         m_index;
    QColor      m_color;
    QVector<int> m_rowsBefore;   // Paletten-Zeilen vor der Aenderung
    QVector<int> m_rowsAfter;    // Paletten-Zeilen nach der Aenderung
};
