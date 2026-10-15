#include "window.h"
#include <QApplication>
#include <QImageReader>
#include <QStyleFactory>

int main(int argc, char **argv) {
    QApplication app(argc,argv);
    app.setApplicationName("FilmLog Index Studio");app.setOrganizationName("FilmLogIndexStudio");app.setApplicationVersion("1.0.0");
    app.setStyle(QStyleFactory::create("Fusion"));
    QImageReader::setAllocationLimit(512);
    Window window;window.show();
    const auto args=app.arguments();
    int smoke=args.indexOf("--smoke-test");
    if(smoke>=0 && smoke+1<args.size())QTimer::singleShot(100,&window,[&window,args,smoke]{window.startSmokeTest(args[smoke+1]);});
    return app.exec();
}
