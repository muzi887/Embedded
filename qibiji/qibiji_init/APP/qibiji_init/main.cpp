#include "mainwindow.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    QApplication::setApplicationName(QStringLiteral("qibiji_init"));
    QApplication::setOrganizationName(QStringLiteral("qibiji"));

    MainWindow window;
    window.show();

    return QApplication::exec();
}
