#include "main_window.hpp"

#include <QApplication>

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    ime::linux::gui::MainWindow window;
    window.show();
    return app.exec();
}
