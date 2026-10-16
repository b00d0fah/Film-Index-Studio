#include "renderer.h"
#include <QBuffer>
#include <QColorSpace>
#include <QCoreApplication>
#include <QFile>
#include <QFontDatabase>
#include <QImageReader>
#include <QImageWriter>
#include <QJsonDocument>
#include <QJsonObject>
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
        QFontDatabase::addApplicationFont("C:/Windows/Fonts/msyh.ttc");
        QVERIFY(temp.isValid());
        photo = temp.filePath("测试 照片.png");
        QImage img(600, 600, QImage::Format_RGB32);
        img.fill(Qt::red);
        for (int y = 300; y < 600; ++y)
            for (int x = 0; x < 600; ++x)
                img.setPixelColor(x, y, Qt::blue);
        QVERIFY(img.save(photo));
    }
    void watermarkDefaultsAndCoordinateMapping() {
        Options options;
        QCOMPARE(options.watermark.x, 50);
        QCOMPARE(options.watermark.y, 5);
        const QRectF area(20, 20, 4532, 4000);
        const auto rect = overlayRect(options.watermark, area, false);
        QVERIFY(qAbs(rect.center().x() - area.center().x()) < 0.001);
        QVERIFY(qAbs(rect.top() - (area.top() + (area.height() - rect.height()) * 0.05)) < 0.001);
        Document d = make();
        QString error;
        auto path = temp.filePath("legacy-defaults.findex");
        QVERIFY(saveDocument(d, path, &error));
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        auto json = QJsonDocument::fromJson(file.readAll()).object();
        file.close();
        auto opts = json["options"].toObject();
        opts.remove("watermark");
        json["options"] = opts;
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write(QJsonDocument(json).toJson());
        file.close();
        Document restored;
        QVERIFY2(loadDocument(path, &restored, &error), qUtf8Printable(error));
        QCOMPARE(restored.options.watermark.x, 50);
        QCOMPARE(restored.options.watermark.y, 5);
        QCOMPARE(restored.options.watermark.size, 300);
    }
    void inactiveImagesDoNotBlockRendering() {
        auto d = make();
        const auto missing = temp.filePath("不存在的背景.png");
        d.options.header = false;
        d.options.headerImage.path = missing;
        d.options.indexImage = {missing, 0, 50, 50};
        d.options.logo.enabled = true;
        d.options.logo.path = missing;
        d.options.watermark.enabled = true;
        d.options.watermark.type = 0;
        d.options.watermark.path = missing;
        auto result = renderSheet(d, 600);
        QVERIFY2(result.error.isEmpty(), qUtf8Printable(result.error));
        QVERIFY(!result.image.isNull());
        d.options.watermark.type = 1;
        result = renderSheet(d, 600);
        QVERIFY(result.error.contains(missing));
        d.options.watermark.opacity = 0;
        QVERIFY(renderSheet(d, 600).error.isEmpty());
        d.options.header = true;
        QVERIFY(renderSheet(d, 600).error.contains(missing));
    }
    void retryCacheAndDecoderErrors() {
        auto d = make();
        const auto path = temp.filePath("cache-retry.bmp");
        QImage img(400, 300, QImage::Format_RGB32);
        img.fill(Qt::red);
        QVERIFY(img.save(path));
        d.photos[0].path = path;
        const auto previousTime = QFileInfo(path).lastModified();
        const auto first = renderSheet(d, 600);
        QVERIFY(first.error.isEmpty());
        img.fill(Qt::blue);
        QVERIFY(img.save(path));
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadWrite));
        QVERIFY(file.setFileTime(previousTime, QFileDevice::FileModificationTime));
        file.close();
        clearPreviewCache();
        const auto second = renderSheet(d, 600);
        QVERIFY(second.error.isEmpty());
        QVERIFY(first.image != second.image);
        QVERIFY(QFile::remove(path));
        QVERIFY(renderSheet(d, 600).error.contains(path));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("not an image");
        file.close();
        auto failed = renderSheet(d, 600);
        QVERIFY(failed.error.contains("第 1 张照片"));
        QVERIFY(failed.error.contains('\n'));
        auto control = std::make_shared<RenderControl>();
        control->canceled = true;
        QVERIFY(renderSheet(d, 600, control).canceled);
    }
    void geometry() {
        auto d = make(7);
        QCOMPARE(baseSheetSize(d), QSize(4572, 1975));
        QCOMPARE(photoRect(d, 0), QRectF(61, 625, 720, 480));
        QCOMPARE(photoRect(d, 6), QRectF(61, 1350, 720, 480));
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
        d.options.headerAlignment = 1;
        QString error;
        auto path = temp.filePath("中文 项目.findex");
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
        QCOMPARE(loaded.options.headerAlignment, 1);
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("{}");
        f.close();
        QVERIFY(!loadDocument(path, &loaded, &error));
        QCOMPARE(loaded.photos.size(), 2);
    }
    void headerAlignmentKeepsMetadataInItsColumn() {
        const QRectF leftColumn(72, 0, 2000, 100);
        const qreal textWidth = 420;
        const qreal xLeft = alignedTextX(leftColumn, textWidth, 0);
        const qreal xCenter = alignedTextX(leftColumn, textWidth, 1);
        const qreal xRight = alignedTextX(leftColumn, textWidth, 2);
        QCOMPARE(xLeft, leftColumn.left());
        QVERIFY(xLeft < xCenter && xCenter < xRight);
        QCOMPARE(xCenter, leftColumn.left() + (leftColumn.width() - textWidth) / 2);
        QCOMPARE(xRight, leftColumn.right() - textWidth);
        const QRectF rightColumn(2500, 0, 1800, 100);
        // Right-column metadata owns its alignment, independent of the left-side selector.
        QCOMPARE(alignedTextX(rightColumn, 600, 0), rightColumn.left());
        QCOMPARE(alignedTextX(rightColumn, 600, 0), alignedTextX(rightColumn, 600, 0));
        auto d = make(1);
        d.options.title = "CITY WALK";
        d.options.film = "COLOR FILM 400";
        d.options.location = "SHANGHAI";
        d.options.camera = "RANGEFINDER";
        d.options.note = "CONTACT SHEET";
        d.options.brand = "LONG MAIN TITLE";
        d.options.outputWidth = 1600;
        QImage results[3];
        for (int align = 0; align < 3; ++align) {
            d.options.headerAlignment = align;
            auto rendered = renderSheet(d, 1600);
            QVERIFY2(rendered.error.isEmpty(), qPrintable(rendered.error));
            results[align] = rendered.image;
        }
        QVERIFY(!results[0].isNull());
        QVERIFY(results[0].size() == results[1].size());
        QVERIFY(results[1].size() == results[2].size());
        const auto size = results[0].size();
        // The right metadata column must remain pixel-identical when the left brand/note
        // alignment changes, proving the two blocks do not collide or inherit each other's alignment.
        const int rightStart = qRound(size.width() * .53);
        const int headerHeight = qRound(d.options.headerHeight * (double(size.width()) / baseSheetSize(d).width()));
        for (int y = 0; y < headerHeight; ++y)
            for (int x = rightStart; x < size.width(); ++x)
                QCOMPARE(results[0].pixel(x, y), results[2].pixel(x, y));
        d.options.details = false;
        d.options.headerAlignment = 1;
        auto singleColumn = renderSheet(d, 1600);
        QVERIFY(singleColumn.error.isEmpty());
        QVERIFY(!singleColumn.image.isNull());
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
    void headerGapHasNoSeparatorAndBackgroundsCrop() {
        auto d = make();
        d.options.brand.clear();
        d.options.details = false;
        d.options.headerGap = 180;
        d.options.headerColor = QColor("#204060");
        d.options.background = QColor("#80a0c0");
        auto r = renderSheet(d, 4572);
        QVERIFY2(r.error.isEmpty(), qPrintable(r.error));
        for (int y = d.options.headerHeight - 20; y < d.options.headerHeight + 180; ++y)
            QCOMPARE(r.image.pixelColor(2, y),
                     y < d.options.headerHeight ? d.options.headerColor : d.options.background);
        QImage bg(100, 200, QImage::Format_RGB32);
        bg.fill(Qt::red);
        for (int y = 100; y < 200; ++y)
            for (int x = 0; x < 100; ++x)
                bg.setPixelColor(x, y, Qt::blue);
        const QString path = temp.filePath("background.png");
        QVERIFY(bg.save(path));
        d.options.headerImage.path = path;
        d.options.headerImage.cropY = 0;
        d.options.indexImage.path = path;
        d.options.indexImage.cropY = 100;
        r = renderSheet(d, 4572);
        QVERIFY(r.error.isEmpty());
        QCOMPARE(r.image.pixelColor(2, 200), QColor(Qt::red));
        QCOMPARE(r.image.pixelColor(2, 550), QColor(Qt::blue));
        d.options.headerImage.opacity = 0;
        d.options.indexImage.opacity = 0;
        r = renderSheet(d, 4572);
        QCOMPARE(r.image.pixelColor(2, 200), d.options.headerColor);
        QCOMPARE(r.image.pixelColor(2, 550), d.options.background);
        d.options.headerImage.opacity = 50;
        r = renderSheet(d, 4572);
        auto c = r.image.pixelColor(2, 200);
        QVERIFY(c.red() > 130 && c.red() < 150 && c.blue() > 40 && c.blue() < 55);
        d.options.header = false;
        QCOMPARE(photoRect(d, 0).top(), 145.0); // headerGap has no effect with header disabled
    }
    void independentStylesAndCustomBounds() {
        auto d = make();
        auto &o = d.options;
        o.brand = "主标题 Main";
        o.title = "RIGHT TITLE";
        o.note = "备注";
        auto original = renderSheet(d, 1600);
        QVERIFY(original.error.isEmpty());
        o.rightAlignment = 2;
        auto right = renderSheet(d, 1600);
        QVERIFY(right.error.isEmpty());
        const int hh = qRound(o.headerHeight * 1600.0 / baseSheetSize(d).width());
        QCOMPARE(original.image.copy(0, 0, 750, hh), right.image.copy(0, 0, 750, hh));
        QVERIFY(original.image.copy(850, 0, 750, hh) != right.image.copy(850, 0, 750, hh));
        o.headerAlignment = 3;
        o.rightAlignment = 3;
        o.leftX = 4502;
        o.leftY = 1000;
        o.rightX = 4502;
        o.rightY = 1000;
        const auto left = headerBlockRect(o, 4572, 0), metadata = headerBlockRect(o, 4572, 2);
        QVERIFY(left.right() < metadata.left());
        QVERIFY(left.top() >= 20);
        QVERIFY(metadata.bottom() <= o.headerHeight - 19);
        o.headerAlignment = 0;
        o.rightAlignment = 0;
        // Custom coordinates must not affect standard alignments.
        auto ignored = renderSheet(d, 1600);
        QCOMPARE(ignored.image, original.image);
        o.textStyles[0].color = Qt::red;
        auto styled = renderSheet(d, 1600);
        QVERIFY(styled.image != original.image);
        QCOMPARE(styled.image.copy(850, 0, 750, hh), original.image.copy(850, 0, 750, hh));
    }
    void logoWatermarkAndPhysicalUnits() {
        auto d = make();
        auto baseline = renderSheet(d, 1600);
        QVERIFY(baseline.error.isEmpty());
        d.options.logo.enabled = true;
        d.options.logo.x = 100;
        d.options.logo.y = 100;
        d.options.logo.size = 220;
        auto logo = renderSheet(d, 1600);
        QVERIFY2(logo.error.isEmpty(), qPrintable(logo.error));
        QVERIFY(logo.image != baseline.image);
        d.options.logo.opacity = 0;
        QCOMPARE(renderSheet(d, 1600).image, baseline.image);
        d.options.watermark.enabled = true;
        d.options.watermark.text = "WATERMARK";
        auto mark = renderSheet(d, 1600);
        QVERIFY(mark.error.isEmpty());
        QVERIFY(mark.image != baseline.image);
        d.options.watermark.type = 1;
        d.options.watermark.path = photo;
        d.options.watermark.opacity = 100;
        auto bitmap = renderSheet(d, 1600);
        QVERIFY(bitmap.error.isEmpty());
        QVERIFY(bitmap.image != mark.image);
        d.options.watermark.opacity = 0;
        QCOMPARE(renderSheet(d, 1600).image, baseline.image);
        QCOMPARE(widthInPixels(25.4, 1, 300), 300);
        QCOMPARE(widthInPixels(2.54, 2, 300), 300);
        QCOMPARE(widthInPixels(1, 3, 600), 600);
        QCOMPARE(widthInUnit(300, 1, 300), 25.4);
        for (int unit = 0; unit < 4; ++unit)
            QCOMPARE(widthInPixels(widthInUnit(4572, unit, 300), unit, 300), 4572);
        d.options.dpi = 300;
        auto output = renderSheet(d, 600);
        QCOMPARE(output.image.dotsPerMeterX(), 11811);
    }
    void externalTemplateAndPortablePreset() {
        const QString name = "自定义模板-" + QUuid::createUuid().toString(QUuid::Id128);
        const QString source = temp.filePath(name);
        QVERIFY(QDir().mkpath(source));
        const QString imported = templatesDirectory() + '/' + name;
        struct Cleanup {
            QString path;
            ~Cleanup() {
                QDir(path).removeRecursively();
            }
        } cleanup{imported};
        auto o = Options();
        for (int row = 0; row < 7; ++row)
            QVERIFY(QFile::copy(templateFile(o, row), source + '/' + QFileInfo(templateFile(o, row)).fileName()));
        QString error, actual;
        QVERIFY2(importTemplate(source, &actual, &error), qUtf8Printable(error));
        QCOMPARE(actual, name);
        QVERIFY(templateNames().contains(name));
        QVERIFY(!templateNames().contains("kedak-version3-5203-50d"));
        o.filmTemplate = name;
        o.previewWidth = 2400;
        o.headerGap = 99;
        o.rightAlignment = 3;
        o.rightX = 147;
        o.textStyles[1].color = Qt::green;
        o.headerImage.path = photo;
        o.headerImage.opacity = 33;
        o.watermark.enabled = true;
        o.watermark.text = "TEST";
        o.watermark.opacity = 64;
        o.logo.enabled = true;
        const QString preset = temp.filePath("预设.fipreset");
        QVERIFY2(savePreset(o, preset, &error), qPrintable(error));
        QFile f(preset);
        QVERIFY(f.open(QIODevice::ReadOnly));
        auto root = QJsonDocument::fromJson(f.readAll()).object();
        QVERIFY(!root.contains("photos"));
        QVERIFY(!root["options"].toObject()["headerImage"].toObject()["path"].toString().contains(temp.path()));
        QVERIFY(QDir(imported).removeRecursively()); // preset must remain independent of original template installation
        Options loaded;
        QVERIFY2(loadPreset(preset, &loaded, &error), qPrintable(error));
        QCOMPARE(loaded.previewWidth, 2400);
        QCOMPARE(loaded.headerGap, 99);
        QCOMPARE(loaded.rightX, 147);
        QCOMPARE(loaded.textStyles[1].color, QColor(Qt::green));
        QCOMPARE(loaded.watermark.opacity, 64);
        QVERIFY(QFileInfo::exists(loaded.headerImage.path));
        auto d = make(42);
        d.options = loaded;
        QVERIFY2(renderSheet(d, 600).error.isEmpty(), qPrintable(renderSheet(d, 600).error));
        QVERIFY(QFile::setPermissions(source + "/37-42.png", QFile::ReadOwner | QFile::WriteOwner));
        QVERIFY(QFile::remove(source + "/37-42.png"));
        QVERIFY(!validateTemplate(source).isEmpty());
        QVERIFY(!importTemplate(source, &actual, &error));
    }
    void importedFontPresetAndInvalidAssets() {
        const QString systemFont = "C:/Windows/Fonts/arial.ttf";
        if (!QFileInfo::exists(systemFont))
            QSKIP("Windows font fixture unavailable");
        const QString font = temp.filePath("user-font.ttf");
        QVERIFY(QFile::copy(systemFont, font));
        Options o;
        o.textStyles[0].fontFile = font;
        o.watermark.style.fontFile = font;
        QString error;
        QVERIFY2(activateFonts(&o, &error), qUtf8Printable(error));
        const QString preset = temp.filePath("font-preset.fipreset");
        QVERIFY2(savePreset(o, preset, &error), qUtf8Printable(error));
        QFile::setPermissions(font, QFile::ReadOwner | QFile::WriteOwner);
        QVERIFY(QFile::remove(font));
        Options restored;
        QVERIFY2(loadPreset(preset, &restored, &error), qUtf8Printable(error));
        QVERIFY(!restored.textStyles[0].fontFile.isEmpty());
        QVERIFY(QFileInfo::exists(restored.textStyles[0].fontFile));
        QCOMPARE(restored.textStyles[0].family, o.textStyles[0].family);
        auto d = make();
        d.options = restored;
        QVERIFY(renderSheet(d, 1200).error.isEmpty());
        d.options.headerImage.path = temp.filePath("does-not-exist.png");
        QVERIFY(!validateDocument(d).isEmpty());
        d.options.headerImage.path.clear();
        d.options.rightAlignment = 4;
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
