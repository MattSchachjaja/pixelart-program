#include "mainwindow.h"

#include <QApplication>
#include <QScreen>
#include <QRect>

int main(int argc, char* argv[]) {
    QApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
    QApplication app(argc, argv);
    // Fuer QSettings (Shortcut-Persistenz u.a.).
    app.setOrganizationName("Otrera");
    app.setApplicationName("MiniPaint");

    MainWindow w;
    w.setWindowFlag(Qt::WindowMaximizeButtonHint);
    w.showMaximized();

    return app.exec();
}
