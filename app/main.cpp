#include <QApplication>
#include "MainWindow.h"

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    MainWindow window;
    // A book named on the command line is opened straight away, so the window
    // can be brought up on one rather than empty. A name, not a path: the books
    // all live in one folder and are chosen by name. See Books.spectable.
    if (argc > 1) window.openNamed(QString::fromLocal8Bit(argv[1]));
    window.show();
    return app.exec();
}
