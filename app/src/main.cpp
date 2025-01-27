#include "mainwindow.h"

#include <QApplication>
#include <QIcon>

int main(int argc, char* argv[])
{
    QApplication::setStyle("fusion");
    QApplication a(argc, argv);
    a.setWindowIcon(QIcon(":/icons/logdor.png"));

    MainWindow w;
    w.show();
    return a.exec();
}
