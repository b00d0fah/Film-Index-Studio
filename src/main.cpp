#include "window.h"
#include <QApplication>
#include <QImageReader>
#include <QLocale>
#include <QStyleFactory>
#include <QTranslator>

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    app.setApplicationName("Film Index Studio");
    app.setOrganizationName("FilmIndexStudio");
    app.setApplicationVersion("1.0.2");
    QLocale::setDefault(QLocale(QLocale::Chinese, QLocale::China));
    QTranslator chinese;
    if (chinese.load(":/assets/translations/qtbase_zh_CN.qm"))
        app.installTranslator(&chinese);
    app.setStyle(QStyleFactory::create("Fusion"));
    QImageReader::setAllocationLimit(512);
    Window window;
    window.show();
    const auto args = app.arguments();
    int smoke = args.indexOf("--smoke-test");
    if (smoke >= 0 && smoke + 1 < args.size())
        QTimer::singleShot(100, &window, [&window, args, smoke] { window.startSmokeTest(args[smoke + 1]); });
    return app.exec();
}
