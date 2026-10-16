#include "singleinstance.h"
#include "window.h"
#include <QApplication>
#include <QImageReader>
#include <QLocale>
#include <QMessageBox>
#include <QStyleFactory>
#include <QTranslator>
#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

int main(int argc, char **argv) {
    SingleInstance instance;
    if (!instance.isPrimary() && instance.error().isEmpty())
        return 0;
    QApplication app(argc, argv);
    if (!instance.error().isEmpty()) {
        QMessageBox::critical(nullptr, "Film Index Studio", instance.error());
        return 1;
    }
    app.setApplicationName("Film Index Studio");
    app.setOrganizationName("FilmIndexStudio");
    app.setApplicationVersion("1.0.4");
    QLocale::setDefault(QLocale(QLocale::Chinese, QLocale::China));
    QTranslator chinese;
    if (chinese.load(":/assets/translations/qtbase_zh_CN.qm"))
        app.installTranslator(&chinese);
    app.setStyle(QStyleFactory::create("Fusion"));
    QImageReader::setAllocationLimit(512);
    Window window;
    window.show();
    QTimer activation;
    QObject::connect(&activation, &QTimer::timeout, &window, [&] {
        if (instance.takeActivationRequest()) {
            if (window.isMinimized())
                window.showNormal();
            window.show();
            window.raise();
            window.activateWindow();
#ifdef Q_OS_WIN
            // Also restore windows launched hidden by a shell: Qt may already consider them visible.
            const HWND handle = reinterpret_cast<HWND>(window.winId());
            ShowWindow(handle, IsIconic(handle) ? SW_RESTORE : SW_SHOW);
            SetForegroundWindow(handle);
#endif
            QApplication::alert(&window);
        }
    });
    activation.start(200);
    const auto args = app.arguments();
    int smoke = args.indexOf("--smoke-test");
    if (smoke >= 0 && smoke + 1 < args.size())
        QTimer::singleShot(100, &window, [&window, args, smoke] { window.startSmokeTest(args[smoke + 1]); });
    return app.exec();
}
