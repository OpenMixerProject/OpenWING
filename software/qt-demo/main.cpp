#include <QApplication>
#include "mainwindow.h"

int main(int argc, char *argv[])
{
    // Enable support for standard linuxfb/evdev touch configuration
    // (Ensure environment variables take precedence)
    qputenv("QT_QPA_FB_TSLIB", "0");
    qputenv("QT_QPA_GENERIC_PLUGINS", "evdevtouch");

    QApplication a(argc, argv);

    MainWindow w;
    // On the embedded framebuffer, we want to run in full screen
    w.showFullScreen();

    return a.exec();
}
