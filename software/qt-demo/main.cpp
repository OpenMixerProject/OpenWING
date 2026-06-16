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
#if defined(__APPLE__)
    // On macOS host, run in normal windowed mode (1280x800) for testing
    w.show();
#else
    // On the embedded framebuffer, run in full screen
    w.showFullScreen();
#endif

    return a.exec();
}
