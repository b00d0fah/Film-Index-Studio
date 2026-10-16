#include "window.h"
#include <QtTest>
#include <QtWidgets>

class ResizeRecorder : public QObject {
  public:
    int count = 0;
    bool eventFilter(QObject *, QEvent *event) override {
        if (event->type() == QEvent::Resize)
            ++count;
        return false;
    }
};

class WindowTest : public QObject {
    Q_OBJECT
  private slots:
    void initTestCase() {
        QFontDatabase::addApplicationFont("C:/Windows/Fonts/msyh.ttc");
    }
    void startupPresetPersistsAndRecovers() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        Options saved;
        saved.brand = "我的启动预设";
        saved.watermark.y = 77;
        saved.watermark.opacity = 23;
        QString error;
        const auto path = temp.filePath("日常预设.fipreset");
        QVERIFY2(savePreset(saved, path, &error), qUtf8Printable(error));
        {
            Window window(temp.path());
            auto presets = window.findChild<QComboBox *>("startupPreset");
            QVERIFY(presets);
            QCOMPARE(presets->count(), 2);
            presets->setCurrentIndex(1);
            QCOMPARE(window.findChild<QLineEdit *>("mainTitle")->text(), QString("Film Index"));
        }
        {
            Window window(temp.path());
            QCOMPARE(window.findChild<QLineEdit *>("mainTitle")->text(), saved.brand);
            QCOMPARE(window.findChild<QSpinBox *>("watermarkY")->value(), 77);
            QCOMPARE(window.findChild<QSpinBox *>("watermarkOpacity")->value(), 23);
            QCOMPARE(window.findChild<PhotoList *>("photoList")->count(), 0);
            window.findChild<QComboBox *>("startupPreset")->setCurrentIndex(0);
            QCOMPARE(window.findChild<QLineEdit *>("mainTitle")->text(), saved.brand);
        }
        {
            Window window(temp.path());
            QCOMPARE(window.findChild<QLineEdit *>("mainTitle")->text(), QString("Film Index"));
            QCOMPARE(window.findChild<QSpinBox *>("watermarkX")->value(), 50);
            QCOMPARE(window.findChild<QSpinBox *>("watermarkY")->value(), 5);
            QCOMPARE(window.findChild<QSpinBox *>("watermarkSize")->value(), 300);
            QCOMPARE(window.findChild<QSpinBox *>("watermarkOpacity")->value(), 100);
            QVERIFY(!window.findChild<QCheckBox *>("showWatermark")->isChecked());
            for (auto button : window.findChildren<QPushButton *>())
                QVERIFY(!button->text().contains("导入字体"));
            window.findChild<QComboBox *>("startupPreset")->setCurrentIndex(1);
        }
        // Broken and missing defaults must never prevent startup or partially apply settings.
        QFile corrupt(path);
        QVERIFY(corrupt.open(QIODevice::WriteOnly | QIODevice::Truncate));
        corrupt.write("invalid preset");
        corrupt.close();
        {
            Window window(temp.path());
            QCOMPARE(window.findChild<QLineEdit *>("mainTitle")->text(), QString("Film Index"));
            QVERIFY(window.statusBar()->currentMessage().contains("不可用"));
        }
        QVERIFY(QFile::remove(path));
        {
            Window window(temp.path());
            auto presets = window.findChild<QComboBox *>("startupPreset");
            QVERIFY(presets->currentText().contains("不可用"));
            QCOMPARE(window.findChild<QLineEdit *>("mainTitle")->text(), QString("Film Index"));
            presets->setCurrentIndex(0);
        }
    }
    void wheelUpdatesKeepPreviewGeometryStable() {
        QTemporaryDir temp;
        QImage source(400, 300, QImage::Format_RGB32);
        source.fill(Qt::blue);
        auto path = temp.filePath("photo.png");
        QVERIFY(source.save(path));
        Window window(temp.path());
        window.show();
        auto view = window.findChild<Preview *>("preview");
        auto bar = window.findChild<QProgressBar *>("previewProgress");
        auto title = window.findChild<QSpinBox *>("borderSize");
        window.findChild<PhotoList *>("photoList")->filesDropped({path});
        QTRY_COMPARE_WITH_TIMEOUT(bar->format(), QString("预览已更新"), 10000);
        view->zoom(4);
        QTest::qWait(50);
        const auto size = view->viewport()->size();
        const double zoom = view->transform().m11();
        ResizeRecorder recorder;
        view->viewport()->installEventFilter(&recorder);
        const int previous = title->value();
        for (int i = 0; i < 14; ++i) {
            const QPointF local(title->rect().center());
            QWheelEvent wheel(local, title->mapToGlobal(local.toPoint()), QPoint(), QPoint(0, 120), Qt::NoButton,
                              Qt::NoModifier, Qt::NoScrollPhase, false);
            QApplication::sendEvent(title, &wheel);
            QTest::qWait(i < 7 ? 35 : 180);
            QCOMPARE(view->viewport()->size(), size);
            QVERIFY(bar->isVisible());
        }
        QVERIFY(title->value() > previous);
        QTRY_COMPARE_WITH_TIMEOUT(bar->format(), QString("预览已更新"), 10000);
        QCOMPARE(view->viewport()->size(), size);
        QCOMPARE(recorder.count, 0);
        QVERIFY(qAbs(view->transform().m11() - zoom) < 0.00001);
        view->viewport()->removeEventFilter(&recorder);
    }
    void chineseDialogsAndPresetKeepsPhotos() {
        QTranslator chinese;
        QVERIFY(chinese.load(":/assets/translations/qtbase_zh_CN.qm"));
        qApp->installTranslator(&chinese);
        QLineEdit input;
        auto menu = input.createStandardContextMenu();
        bool translated = false;
        for (auto action : menu->actions())
            if (action->text().contains("撤"))
                translated = true;
        delete menu;
        QVERIFY(translated);
        QColorDialog colors;
        colors.setOption(QColorDialog::DontUseNativeDialog);
        auto buttons = colors.findChild<QDialogButtonBox *>();
        QVERIFY(buttons);
        QVERIFY(buttons->button(QDialogButtonBox::Cancel)->text().contains("取消"));
        qApp->removeTranslator(&chinese);
        QTemporaryDir temp;
        QImage image(300, 200, QImage::Format_RGB32);
        image.fill(Qt::green);
        auto path = temp.filePath("photo.png");
        QVERIFY(image.save(path));
        Options options;
        options.brand = "预设恢复测试";
        options.previewWidth = 900;
        QString error;
        auto preset = temp.filePath("test.fipreset");
        QVERIFY2(savePreset(options, preset, &error), qUtf8Printable(error));
        Window window;
        window.show();
        auto list = window.findChild<PhotoList *>("photoList");
        list->filesDropped({path});
        QCOMPARE(list->count(), 1);
        QMimeData mime;
        mime.setUrls({QUrl::fromLocalFile(preset)});
        QDragEnterEvent enter(QPoint(20, 20), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(&window, &enter);
        QDropEvent drop(QPointF(20, 20), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(&window, &drop);
        QCOMPARE(list->count(), 1);
        QCOMPARE(window.findChild<QLineEdit *>("mainTitle")->text(), options.brand);
        QTRY_COMPARE_WITH_TIMEOUT(int(window.findChild<Preview *>("preview")->sceneRect().width()), 900, 10000);
    }
    void zoomPreservesViewAndResolution() {
        Preview view;
        view.resize(800, 500);
        view.show();
        Document d;
        d.photos.resize(42);
        QImage img(1600, 2200, QImage::Format_RGB32);
        img.fill(Qt::white);
        view.setImage(img, d);
        view.zoom(6);
        view.centerOn(1000, 1600);
        QTest::qWait(30);
        QPointF before = view.mapToScene(view.viewport()->rect().center());
        double zoom = view.transform().m11();
        view.setImage(img, d);
        QVERIFY(QLineF(before, view.mapToScene(view.viewport()->rect().center())).length() < 4);
        QCOMPARE(view.transform().m11(), zoom);
        QImage larger(3200, 4400, QImage::Format_RGB32);
        larger.fill(Qt::white);
        view.setImage(larger, d);
        QVERIFY(QLineF(before * 2, view.mapToScene(view.viewport()->rect().center())).length() < 5);
        QCOMPARE(view.transform().m11(), zoom / 2);
        view.focusRegion(photoRect(d, 25), baseSheetSize(d).width());
        auto target = photoRect(d, 25).center() * (3200.0 / baseSheetSize(d).width());
        QVERIFY(QLineF(target, view.mapToScene(view.viewport()->rect().center())).length() < 5);
        view.fit();
        zoom = view.transform().m11();
        view.focusRegion(photoRect(d, 25), baseSheetSize(d).width());
        QCOMPARE(view.transform().m11(), zoom);
    }
    void realtimePreviewAndEditing() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        QImage source(1000, 700, QImage::Format_RGB32);
        source.fill(QColor("#ce8554"));
        QStringList paths;
        for (int i = 1; i <= 12; ++i) {
            auto path = temp.filePath(QString("照片%1.png").arg(i));
            QVERIFY(source.save(path));
            paths << path;
        }
        Window window;
        window.show();
        auto list = window.findChild<PhotoList *>("photoList");
        QVERIFY(list);
        auto info = window.findChild<QLabel *>("previewInfo");
        QVERIFY(info);
        auto view = window.findChild<Preview *>("preview");
        QVERIFY(view);
        list->filesDropped(paths);
        QCOMPARE(list->count(), 12);
        QVERIFY(list->item(1)->text().endsWith("照片2.png"));
        QTRY_VERIFY_WITH_TIMEOUT(info->text().contains("4572 ×"), 10000);
        // Several edits while worker previews are running must settle on the latest settings.
        auto border = window.findChild<QSpinBox *>("borderSize");
        auto gap = window.findChild<QSpinBox *>("stripGap");
        auto title = window.findChild<QLineEdit *>("titleText");
        auto mainTitle = window.findChild<QLineEdit *>("mainTitle");
        auto alignment = window.findChild<QComboBox *>("headerAlignment");
        QStringList formLabels;
        for (auto label : window.findChildren<QLabel *>())
            if (label->buddy())
                formLabels << label->text();
        QVERIFY(formLabels.contains("主标题"));
        QVERIFY(formLabels.contains("主标题字号"));
        QVERIFY(!formLabels.contains("品牌文字"));
        QVERIFY(!formLabels.contains("品牌字号"));
        for (int i = 0; i < 5; ++i) {
            border->setValue(40 + i * 10);
            gap->setValue(30 + i * 5);
            title->setText(QString("实时修改 %1").arg(i));
            QTest::qWait(170);
        }
        Document expected;
        for (const auto &p : paths)
            expected.photos.append({p, 0, 0, 0});
        expected.options.border = 80;
        expected.options.gap = 50;
        expected.options.title = "实时修改 4";
        auto size = outputSize(expected);
        QString dimensions = QString("%1 × %2").arg(size.width()).arg(size.height());
        QTRY_VERIFY_WITH_TIMEOUT(info->text().contains(dimensions), 10000);
        auto expectedImage = renderSheet(expected, 1600).image;
        QImage shown;
        for (auto item : view->scene()->items())
            if (auto pix = qgraphicsitem_cast<QGraphicsPixmapItem *>(item))
                shown = pix->pixmap().toImage();
        QCOMPARE(shown.convertToFormat(QImage::Format_RGB32), expectedImage.convertToFormat(QImage::Format_RGB32));
        QVERIFY(alignment);
        QVERIFY(mainTitle);
        QCOMPARE(mainTitle->text(), QString("Film Index"));
        QCOMPARE(window.findChild<QSpinBox *>("infoSize")->value(), 60);
        auto customX = window.findChild<QSpinBox *>("leftX");
        QVERIFY(customX);
        QVERIFY(!customX->isEnabled());
        alignment->setCurrentIndex(3);
        QVERIFY(customX->isEnabled());
        alignment->setCurrentIndex(1);
        expected.options.headerAlignment = 1;
        QTRY_VERIFY_WITH_TIMEOUT(info->text().contains(dimensions), 10000);
        QTRY_VERIFY_WITH_TIMEOUT(
            [&] {
                for (auto item : view->scene()->items())
                    if (auto pix = qgraphicsitem_cast<QGraphicsPixmapItem *>(item))
                        return pix->pixmap().toImage().convertToFormat(QImage::Format_RGB32) ==
                               renderSheet(expected, 1600).image.convertToFormat(QImage::Format_RGB32);
                return false;
            }(),
            10000);
        // Clicking the preview chooses the matching source photo.
        auto rect = photoRect(expected, 5);
        double scale = 1600.0 / baseSheetSize(expected).width();
        QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::NoModifier, view->mapFromScene(rect.center() * scale));
        QCOMPARE(list->currentRow(), 5);
        view->zoom(4);
        auto crop = window.findChild<QSlider *>("cropX");
        crop->setValue(75);
        QTRY_COMPARE_WITH_TIMEOUT(window.findChild<QProgressBar *>("previewProgress")->format(), QString("预览已更新"),
                                  10000);
        const QPointF target = photoRect(expected, 5).center() * scale;
        // Near sheet edges the view clamps scrolling; the edited photo must at least be visible.
        QVERIFY(view->viewport()->rect().contains(view->mapFromScene(target)));
        auto quality = window.findChild<QSpinBox *>("previewWidth");
        QVERIFY(quality);
        quality->setValue(2400);
        QTRY_COMPARE_WITH_TIMEOUT(int(view->sceneRect().width()), 2400, 10000);
        auto progress = window.findChild<QProgressBar *>("thumbnailProgress");
        QVERIFY(progress);
        QTRY_VERIFY_WITH_TIMEOUT(!progress->isVisible(), 10000);
        QVERIFY(!list->item(0)->icon().isNull());
        for (auto b : window.findChildren<QPushButton *>())
            QVERIFY(b->text() != "新建" && b->text() != "打开项目" && b->text() != "保存项目");
        // Removal during preview work must not leave a stale image behind.
        list->selectAll();
        list->setFocus();
        QTest::keyClick(list, Qt::Key_Delete);
        QCOMPARE(list->count(), 0);
        QTRY_VERIFY_WITH_TIMEOUT(view->scene()->items().isEmpty(), 5000);
        QTest::qWait(250);
        QVERIFY(view->scene()->items().isEmpty());
    }
};
QTEST_MAIN(WindowTest)
#include "test_window.moc"
