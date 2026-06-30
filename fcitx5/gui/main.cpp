#include "main_window.hpp"

#include <QApplication>

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    ime::fcitx5::gui::MainWindow window;
    window.show();
    return app.exec();
}
