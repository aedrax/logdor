#include "mainwindow.h"

#include <QApplication>
#include <QIcon>

int main(int argc, char* argv[])
{
    QApplication a(argc, argv);
    // Without an explicit name Qt derives it from argv[0], which for an
    // AppImage is the AppImage's own filename, AppDataLocation would then
    // move whenever the file is renamed.
    QCoreApplication::setApplicationName(QStringLiteral("logdor"));
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
