#pragma once

#include <QImage>
#include <QString>
#include <QColor>

// Eine einzelne Bildebene.
struct Layer {
    QImage  image;
    QString name;
    bool    visible = true;
    float   opacity = 1.0f;
    bool    locked  = false;

    Layer(int w, int h, const QString& n)
        : image(w, h, QImage::Format_ARGB32_Premultiplied), name(n)
    { image.fill(Qt::transparent); }
};
