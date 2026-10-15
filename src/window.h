#pragma once
#include "renderer.h"
#include <QMainWindow>
#include <QFutureWatcher>
#include <QListWidget>
#include <QGraphicsView>
#include <QTimer>
#include <functional>

class QLabel;
class QSlider;
class QProgressDialog;
class QComboBox;
class QCloseEvent;
Q_DECLARE_METATYPE(Photo)

class PhotoList : public QListWidget {
    Q_OBJECT
public:
    explicit PhotoList(QWidget *parent = nullptr);
signals:
    void filesDropped(const QStringList &paths);
protected:
    void dragEnterEvent(QDragEnterEvent *) override;
    void dragMoveEvent(QDragMoveEvent *) override;
    void dropEvent(QDropEvent *) override;
};
class Preview : public QGraphicsView {
    Q_OBJECT
public:
    explicit Preview(QWidget *parent = nullptr);
    void setImage(const QImage &image, const Document &d);
    void clearImage();
    void fit();
    void zoom(double factor);
    void selectPhoto(int index);
signals:
    void photoClicked(int index);
protected:
    void resizeEvent(QResizeEvent *) override;
    void wheelEvent(QWheelEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
private:
    bool fitted = true;
    QVector<QRectF> frames;
    QGraphicsRectItem *selection = nullptr;
};
class Window : public QMainWindow {
    Q_OBJECT
public:
    explicit Window();
    ~Window() override;
    void startSmokeTest(const QString &directory);
protected:
    void closeEvent(QCloseEvent *) override;
    void dragEnterEvent(QDragEnterEvent *) override;
    void dropEvent(QDropEvent *) override;
private:
    Document doc;
    PhotoList *photos = nullptr;
    Preview *preview = nullptr;
    QLabel *count = nullptr, *previewInfo = nullptr, *photoInfo = nullptr;
    QSlider *cropX = nullptr, *cropY = nullptr;
    QTimer debounce;
    QFutureWatcher<RenderResult> previewWatcher;
    QFutureWatcher<QString> exportWatcher;
    std::shared_ptr<RenderControl> previewControl, exportControl;
    QVector<std::function<void()>> bindings;
    QString projectPath, lastExport, smokeDir;
    bool dirty = false, syncing = false, exporting = false, smokeExported = false;
    int revision = 0, renderingRevision = 0;
    void buildUi();
    void syncControls();
    void changed();
    void updateTitle();
    void startPreview();
    void collectPhotos();
    void rebuildPhotos();
    void addFiles(QStringList paths);
    void selectPhoto();
    void changeSelected(int rotationDelta, bool reset = false);
    bool confirmDiscard();
    bool saveProject(bool saveAs = false);
    void openProject(const QString &path = {});
    void exportImage(const QString &path = {});
    void report(const QString &message);
};
