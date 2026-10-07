#include "mainwindow.h"

#include <QApplication>
#include <QScreen>
#include <QRect>

int main(int argc, char* argv[]) {
    QApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
    QApplication app(argc, argv);

    MainWindow w;
    w.setWindowFlag(Qt::WindowMaximizeButtonHint);
    w.showMaximized();

    return app.exec();
}
