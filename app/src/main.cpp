#include "mainwindow.h"

#include <QApplication>
#include <QIcon>

int main(int argc, char* argv[])
{
    QApplication a(argc, argv);
    a.setStyle("fusion");
    a.setWindowIcon(QIcon(":/icons/logdor.png"));

    MainWindow w;
    w.show();
    const QStringList args = QApplication::arguments();
    if (args.size() > 1) {
        w.openFile(args.at(1));
    }
    return a.exec();
}
