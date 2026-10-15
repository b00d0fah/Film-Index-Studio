#include "renderer.h"
#include <QBuffer>
#include <QColorSpace>
#include <QFile>
#include <QImageReader>
#include <QImageWriter>
#include <QTemporaryDir>
#include <QtTest>

class CoreTest : public QObject {
    Q_OBJECT
    QTemporaryDir temp;
    QString photo;
    Document make(int count = 1) {
        Document d;
        for (int i = 0; i < count; ++i)
            d.photos.append({photo, 0, 0, 0});
        return d;
    }
  private slots:
    void initTestCase() {
        QVERIFY(temp.isValid());
        photo = temp.filePath("测试 照片.png");
        QImage img(600, 600, QImage::Format_RGB32);
        img.fill(Qt::red);
        for (int y = 300; y < 600; ++y)
            for (int x = 0; x < 600; ++x)
                img.setPixelColor(x, y, Qt::blue);
        QVERIFY(img.save(photo));
    }
    void geometry() {
        auto d = make(7);
        QCOMPARE(baseSheetSize(d), QSize(4572, 1965));
        QCOMPARE(photoRect(d, 0), QRectF(61, 615, 720, 480));
        QCOMPARE(photoRect(d, 6), QRectF(61, 1340, 720, 480));
        d.options.header = false;
        QCOMPARE(baseSheetSize(d), QSize(4572, 1495));
        QCOMPARE(outputSize(d, 2286), QSize(2286, 748));
    }
    void allTemplates() {
        QCOMPARE(templateNames().size(), 9);
        // Each template's seven strip resources must decode and render.
        for (const auto &name : templateNames()) {
            auto d = make(42);
            d.options.filmTemplate = name;
            auto r = renderSheet(d, 600);
            QVERIFY2(r.error.isEmpty(), qPrintable(r.error));
            QVERIFY(!r.image.isNull());
            QCOMPARE(r.image.size(), outputSize(d, 600));
        }
    }
    void orientationAndCrop() {
        auto d = make();
        d.options.header = false;
        d.options.autoRotate = false;
        d.photos[0].cropY = -1;
        auto top = renderSheet(d, 4572);
        QVERIFY(top.error.isEmpty());
        d.photos[0].cropY = 1;
        auto bottom = renderSheet(d, 4572);
        QVERIFY(bottom.error.isEmpty());
        // A square source loses vertical content in a 3:2 frame. Compare near its middle.
        QPoint sample = photoRect(d, 0).center().toPoint() - QPoint(0, 80);
        QCOMPARE(top.image.pixelColor(sample), QColor(Qt::red));
        QCOMPARE(bottom.image.pixelColor(sample), QColor(Qt::blue));
        d.photos[0].rotation = 180;
        auto rotated = renderSheet(d, 4572);
        QVERIFY(rotated.error.isEmpty());
        QVERIFY(rotated.image != bottom.image);
        d.options.fit = true;
        auto fit = renderSheet(d, 4572);
        QVERIFY(fit.error.isEmpty());
        QCOMPARE(fit.image.pixelColor(photoRect(d, 0).topLeft().toPoint() + QPoint(10, 20)), QColor(Qt::black));
    }
    void projectRoundTrip() {
        auto d = make(2);
        d.options.title = "中文标题 / Test";
        d.options.gap = 60;
        d.options.headerColor = QColor("#234567");
        d.photos[1].rotation = 270;
        d.photos[1].cropX = .37;
        QString error;
        auto path = temp.filePath("中文 项目.flindex");
        QVERIFY2(saveDocument(d, path, &error), qPrintable(error));
        Document loaded;
        QVERIFY2(loadDocument(path, &loaded, &error), qPrintable(error));
        QCOMPARE(loaded.photos.size(), 2);
        QCOMPARE(loaded.photos[0].path, d.photos[0].path);
        QCOMPARE(loaded.photos[1].rotation, 270);
        QCOMPARE(loaded.photos[1].cropX, .37);
        QCOMPARE(loaded.options.title, d.options.title);
        QCOMPARE(loaded.options.headerColor, d.options.headerColor);
        QCOMPARE(loaded.options.gap, 60);
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("{}");
        f.close();
        QVERIFY(!loadDocument(path, &loaded, &error));
        QCOMPARE(loaded.photos.size(), 2);
    }
    void validationAndCancel() {
        QVERIFY(!renderSheet(Document(), 600).error.isEmpty());
        QVERIFY(!renderSheet(make(43), 600).error.isEmpty());
        auto d = make();
        d.photos[0].path = temp.filePath("missing.jpg");
        QVERIFY(renderSheet(d, 600).error.contains("无法读取"));
        d = make(42);
        d.options.outputWidth = 9000;
        d.options.headerHeight = 1000;
        d.options.gap = 200;
        QVERIFY(renderSheet(d).error.contains("1 亿"));
        auto control = std::make_shared<RenderControl>();
        control->canceled = true;
        QVERIFY(renderSheet(make(), 600, control).canceled);
        d = make();
        d.options.filmTemplate = "../bad";
        QVERIFY(!validateDocument(d).isEmpty());
    }
    void exportAndAtomicCancel() {
        auto r = renderSheet(make(7), 1200);
        QVERIFY(r.error.isEmpty());
        for (const QString &ext : {"png", "jpg"}) {
            QString path = temp.filePath("导出 空格." + ext);
            QVERIFY(writeImage(r.image, path, 94).isEmpty());
            QImageReader reader(path);
            QCOMPARE(reader.size(), r.image.size());
            QVERIFY(!reader.read().isNull());
            QFile original(path);
            QVERIFY(original.open(QIODevice::ReadOnly));
            auto old = original.readAll();
            original.close();
            auto cancel = std::make_shared<RenderControl>();
            cancel->canceled = true;
            QVERIFY(!writeImage(r.image, path, 50, cancel).isEmpty());
            QVERIFY(original.open(QIODevice::ReadOnly));
            QCOMPARE(original.readAll(), old);
        }
        QVERIFY(!writeImage(r.image, temp.path(), 94).isEmpty());
    }
    void imagePlugins() {
        auto formats = QImageReader::supportedImageFormats();
        QVERIFY(formats.contains("tiff"));
        QVERIFY(formats.contains("webp"));
        QImage img(photo);
        for (const QString &ext : {"tiff", "webp"}) {
            auto path = temp.filePath("format." + ext);
            QVERIFY(img.save(path));
            auto d = make();
            d.photos[0].path = path;
            QVERIFY(renderSheet(d, 600).error.isEmpty());
        }
    }
    void exifOrientation() {
        QImage source(600, 300, QImage::Format_RGB32);
        source.fill(Qt::red);
        for (int y = 0; y < 300; ++y)
            for (int x = 300; x < 600; ++x)
                source.setPixelColor(x, y, Qt::blue);
        QByteArray jpeg;
        QBuffer buffer(&jpeg);
        QVERIFY(buffer.open(QIODevice::WriteOnly));
        QVERIFY(source.save(&buffer, "JPG", 100));
        // EXIF IFD0 orientation 6: rotate 90 degrees clockwise before fitting.
        QByteArray exif = QByteArray::fromHex("45786966000049492a0008000000010012010300010000000600000000000000");
        QByteArray app1 = QByteArray::fromHex("ffe1");
        int length = exif.size() + 2;
        app1.append(char(length >> 8));
        app1.append(char(length & 255));
        app1 += exif;
        jpeg.insert(2, app1);
        QString path = temp.filePath("orientation6.jpg");
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(jpeg);
        f.close();
        auto d = make();
        d.photos[0].path = path;
        d.options.autoRotate = false;
        d.options.fit = true;
        auto result = renderSheet(d, 4572);
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
        QPoint center = photoRect(d, 0).center().toPoint();
        auto top = result.image.pixelColor(center - QPoint(0, 150));
        auto bottom = result.image.pixelColor(center + QPoint(0, 150));
        QVERIFY(top.red() > 240 && top.blue() < 15);
        QVERIFY(bottom.blue() > 240 && bottom.red() < 15);
        QCOMPARE(result.image.colorSpace(), QColorSpace(QColorSpace::SRgb));
    }
};
QTEST_MAIN(CoreTest)
#include "test_core.moc"
