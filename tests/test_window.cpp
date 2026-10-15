#include "window.h"
#include <QtTest>
#include <QtWidgets>

class WindowTest : public QObject {
    Q_OBJECT
  private slots:
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
        // Clicking the preview chooses the matching source photo.
        auto rect = photoRect(expected, 5);
        double scale = 1600.0 / baseSheetSize(expected).width();
        QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::NoModifier, view->mapFromScene(rect.center() * scale));
        QCOMPARE(list->currentRow(), 5);
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
