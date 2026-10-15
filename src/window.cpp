#include "window.h"
#include <QCollator>
#include <QImageReader>
#include <QMimeData>
#include <QtConcurrent/QtConcurrentRun>
#include <QtWidgets>

static QStringList urlPaths(const QMimeData *mime) {
    QStringList paths;
    for (const auto &u : mime->urls())
        if (u.isLocalFile())
            paths << u.toLocalFile();
    return paths;
}
PhotoList::PhotoList(QWidget *p) : QListWidget(p) {
    setSelectionMode(QAbstractItemView::ExtendedSelection);
    setDragDropMode(QAbstractItemView::InternalMove);
    setDefaultDropAction(Qt::MoveAction);
    setIconSize({76, 56});
    setSpacing(3);
    setAcceptDrops(true);
    setObjectName("photoList");
}
void PhotoList::dragEnterEvent(QDragEnterEvent *e) {
    if (e->mimeData()->hasUrls())
        e->acceptProposedAction();
    else
        QListWidget::dragEnterEvent(e);
}
void PhotoList::dragMoveEvent(QDragMoveEvent *e) {
    if (e->mimeData()->hasUrls())
        e->acceptProposedAction();
    else
        QListWidget::dragMoveEvent(e);
}
void PhotoList::dropEvent(QDropEvent *e) {
    if (e->mimeData()->hasUrls()) {
        emit filesDropped(urlPaths(e->mimeData()));
        e->acceptProposedAction();
    } else
        QListWidget::dropEvent(e);
}
Preview::Preview(QWidget *p) : QGraphicsView(p) {
    setScene(new QGraphicsScene(this));
    setFrameShape(QFrame::NoFrame);
    setBackgroundBrush(QColor("#171c20"));
    setDragMode(ScrollHandDrag);
    setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
    setTransformationAnchor(AnchorUnderMouse);
    setObjectName("preview");
}
void Preview::clearImage() {
    scene()->clear();
    selection = nullptr;
    frames.clear();
    scene()->setSceneRect({});
}
void Preview::setImage(const QImage &image, const Document &d) {
    clearImage();
    scene()->addPixmap(QPixmap::fromImage(image));
    scene()->setSceneRect(QRectF(image.rect()));
    double s = double(image.width()) / baseSheetSize(d).width();
    for (int i = 0; i < d.photos.size(); ++i) {
        auto rect = photoRect(d, i);
        frames << QRectF(rect.x() * s, rect.y() * s, rect.width() * s, rect.height() * s);
    }
    QPen pen(QColor("#53e0bb"), 2);
    pen.setCosmetic(true);
    selection = scene()->addRect({}, pen);
    selection->hide();
    if (fitted)
        fit();
}
void Preview::fit() {
    fitted = true;
    if (!scene()->sceneRect().isEmpty())
        fitInView(scene()->sceneRect().adjusted(-20, -20, 20, 20), Qt::KeepAspectRatio);
}
void Preview::zoom(double f) {
    fitted = false;
    double next = transform().m11() * f;
    if (next >= .08 && next <= 8)
        scale(f, f);
}
void Preview::selectPhoto(int i) {
    if (selection) {
        selection->setVisible(i >= 0 && i < frames.size());
        if (i >= 0 && i < frames.size())
            selection->setRect(frames[i]);
    }
}
void Preview::resizeEvent(QResizeEvent *e) {
    QGraphicsView::resizeEvent(e);
    if (fitted)
        fit();
}
void Preview::wheelEvent(QWheelEvent *e) {
    if (e->modifiers() & Qt::ControlModifier) {
        zoom(e->angleDelta().y() > 0 ? 1.15 : 1 / 1.15);
        e->accept();
    } else
        QGraphicsView::wheelEvent(e);
}
void Preview::mousePressEvent(QMouseEvent *e) {
    if (e->button() == Qt::LeftButton) {
        auto point = mapToScene(e->position().toPoint());
        for (int i = 0; i < frames.size(); ++i)
            if (frames[i].contains(point)) {
                emit photoClicked(i);
                break;
            }
    }
    QGraphicsView::mousePressEvent(e);
}
static QImage thumbnail(const QString &path) {
    QImageReader r(path);
    r.setAutoTransform(true);
    if (r.size().isValid())
        r.setScaledSize(r.size().scaled(152, 112, Qt::KeepAspectRatio));
    return r.read();
}
static Photo itemPhoto(const QListWidgetItem *item) {
    const QVariant data = item->data(Qt::UserRole);
    return data.value<Photo>();
}
Window::Window() {
    setAcceptDrops(true);
    resize(1440, 900);
    setMinimumSize(1080, 680);
    buildUi();
    debounce.setSingleShot(true);
    debounce.setInterval(160);
    connect(&debounce, &QTimer::timeout, this, &Window::startPreview);
    connect(&previewWatcher, &QFutureWatcher<RenderResult>::finished, this, [this] {
        if (renderingRevision != revision) {
            debounce.start(0);
            return;
        }
        auto result = previewWatcher.result();
        if (result.canceled)
            return;
        if (!result.error.isEmpty()) {
            preview->clearImage();
            previewInfo->setText("预览失败 · 请检查照片");
            statusBar()->showMessage(result.error);
            return;
        }
        preview->setImage(result.image, doc);
        preview->selectPhoto(photos->currentRow());
        const auto size = outputSize(doc);
        previewInfo->setText(QString("实时预览  ·  导出 %1 × %2 px  ·  %3 张照片")
                                 .arg(size.width())
                                 .arg(size.height())
                                 .arg(doc.photos.size()));
        statusBar()->showMessage("预览已更新 · 点击照片可选中 · Ctrl + 滚轮缩放，拖动平移");
        if (!smokeDir.isEmpty() && !smokeExported) {
            smokeExported = true;
            QTimer::singleShot(300, this, [this] {
                grab().save(smokeDir + "/界面预览.png");
                exportImage(smokeDir + "/示例索引图.jpg");
            });
        }
    });
    syncControls();
    updateTitle();
    statusBar()->showMessage("添加照片或将图片拖入窗口，开始制作一张索引图。");
}
Window::~Window() {
    if (previewControl)
        previewControl->canceled.store(true);
    if (exportControl)
        exportControl->canceled.store(true);
    previewWatcher.waitForFinished();
    exportWatcher.waitForFinished();
}
void Window::buildUi() {
    auto root = new QWidget;
    auto outer = new QVBoxLayout(root);
    outer->setContentsMargins(20, 16, 20, 12);
    outer->setSpacing(14);
    auto top = new QHBoxLayout;
    auto heading = new QLabel("FILM  /  INDEX STUDIO");
    heading->setObjectName("heading");
    top->addWidget(heading);
    top->addStretch();
    auto tagline = new QLabel("单张索引图工作台");
    tagline->setObjectName("muted");
    top->addWidget(tagline);
    outer->addLayout(top);
    auto bar = new QHBoxLayout;
    auto button = [&](QHBoxLayout *layout, QString label, auto fn, const QString &name = QString()) {
        auto b = new QPushButton(label);
        if (!name.isEmpty())
            b->setObjectName(name);
        layout->addWidget(b);
        connect(b, &QPushButton::clicked, this, fn);
        return b;
    };
    button(
        bar, "＋ 添加照片",
        [this] {
            addFiles(QFileDialog::getOpenFileNames(
                this, "添加照片", {}, "照片 (*.jpg *.jpeg *.png *.tif *.tiff *.webp *.bmp);;所有文件 (*)"));
        },
        "addPhotos");
    button(bar, "添加文件夹", [this] {
        auto dir = QFileDialog::getExistingDirectory(this, "选择照片文件夹");
        if (!dir.isEmpty())
            addFiles({dir});
    });
    button(bar, "打开项目", [this] { openProject(); });
    button(bar, "保存项目", [this] { saveProject(); });
    button(bar, "新建", [this] {
        if (confirmDiscard()) {
            doc = {};
            projectPath.clear();
            rebuildPhotos();
            syncControls();
            dirty = false;
            changed();
            dirty = false;
            updateTitle();
        }
    });
    bar->addStretch();
    button(bar, "导出索引图", [this] { exportImage(); }, "exportButton");
    outer->addLayout(bar);
    auto split = new QSplitter;
    split->setChildrenCollapsible(false);
    auto left = new QWidget;
    auto ll = new QVBoxLayout(left);
    ll->setContentsMargins(0, 0, 0, 0);
    count = new QLabel("照片  0 / 42");
    count->setObjectName("sectionTitle");
    ll->addWidget(count);
    auto hint = new QLabel("拖动排序 · Ctrl / Shift 多选");
    hint->setObjectName("muted");
    ll->addWidget(hint);
    photos = new PhotoList;
    ll->addWidget(photos);
    auto row = new QHBoxLayout;
    button(row, "↑", [this] {
        int i = photos->currentRow();
        if (i > 0) {
            auto item = photos->takeItem(i);
            photos->insertItem(i - 1, item);
            photos->setCurrentItem(item);
            collectPhotos();
            changed();
        }
    });
    button(row, "↓", [this] {
        int i = photos->currentRow();
        if (i >= 0 && i + 1 < photos->count()) {
            auto item = photos->takeItem(i);
            photos->insertItem(i + 1, item);
            photos->setCurrentItem(item);
            collectPhotos();
            changed();
        }
    });
    button(row, "移除", [this] {
        for (auto item : photos->selectedItems())
            delete photos->takeItem(photos->row(item));
        collectPhotos();
        changed();
    });
    ll->addLayout(row);
    auto sortRow = new QHBoxLayout;
    button(sortRow, "按文件名排序", [this] {
        collectPhotos();
        QCollator coll;
        coll.setNumericMode(true);
        coll.setCaseSensitivity(Qt::CaseInsensitive);
        std::stable_sort(doc.photos.begin(), doc.photos.end(), [&](const Photo &a, const Photo &b) {
            return coll.compare(QFileInfo(a.path).fileName(), QFileInfo(b.path).fileName()) < 0;
        });
        rebuildPhotos();
        changed();
    });
    ll->addLayout(sortRow);
    left->setMinimumWidth(200);
    split->addWidget(left);
    auto center = new QWidget;
    auto cl = new QVBoxLayout(center);
    cl->setContentsMargins(4, 0, 4, 0);
    auto viewBar = new QHBoxLayout;
    previewInfo = new QLabel("实时预览");
    previewInfo->setObjectName("previewInfo");
    viewBar->addWidget(previewInfo);
    viewBar->addStretch();
    button(viewBar, "−", [this] { preview->zoom(1 / 1.2); });
    button(viewBar, "＋", [this] { preview->zoom(1.2); });
    button(viewBar, "适应", [this] { preview->fit(); });
    cl->addLayout(viewBar);
    preview = new Preview;
    cl->addWidget(preview);
    auto help = new QLabel("点击预览中的照片进行调整  ·  Ctrl + 滚轮缩放  ·  拖动平移");
    help->setObjectName("muted");
    help->setAlignment(Qt::AlignCenter);
    cl->addWidget(help);
    split->addWidget(center);
    auto tabs = new QTabWidget;
    tabs->setMinimumWidth(300);
    tabs->setMaximumWidth(390);
    auto formTab = [&](QString title) {
        auto scroll = new QScrollArea;
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        auto page = new QWidget;
        auto form = new QFormLayout(page);
        form->setContentsMargins(14, 16, 14, 16);
        form->setVerticalSpacing(14);
        form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        scroll->setWidget(page);
        tabs->addTab(scroll, title);
        return form;
    };
    auto layout = formTab("排版");
    auto text = formTab("文字");
    auto photo = formTab("照片");
    auto spin = [&](QFormLayout *f, QString label, int *value, int min, int max, const QString &name = QString()) {
        auto w = new QSpinBox;
        w->setRange(min, max);
        w->setObjectName(name);
        f->addRow(label, w);
        bindings << [w, value] {
            QSignalBlocker block(w);
            w->setValue(*value);
        };
        connect(w, &QSpinBox::valueChanged, this, [this, value](int v) {
            *value = v;
            changed();
        });
    };
    auto check = [&](QFormLayout *f, QString label, bool *value) {
        auto w = new QCheckBox(label);
        f->addRow(w);
        bindings << [w, value] {
            QSignalBlocker block(w);
            w->setChecked(*value);
        };
        connect(w, &QCheckBox::toggled, this, [this, value](bool v) {
            *value = v;
            changed();
        });
    };
    auto line = [&](QFormLayout *f, QString label, QString *value, const QString &name = QString()) {
        auto w = new QLineEdit;
        w->setMaxLength(250);
        w->setClearButtonEnabled(true);
        w->setObjectName(name);
        f->addRow(label, w);
        bindings << [w, value] {
            QSignalBlocker block(w);
            w->setText(*value);
        };
        connect(w, &QLineEdit::textChanged, this, [this, value](const QString &v) {
            *value = v;
            changed();
        });
    };
    auto color = [&](QFormLayout *f, QString label, QColor *value) {
        auto w = new QPushButton;
        f->addRow(label, w);
        auto refresh = [w, value] {
            w->setText(value->name().toUpper());
            w->setStyleSheet(QString("QPushButton{border-left:14px solid %1;text-align:left;padding-left:12px;}")
                                 .arg(value->name()));
        };
        bindings << refresh;
        connect(w, &QPushButton::clicked, this, [this, value, refresh] {
            auto c = QColorDialog::getColor(*value, this, "选择颜色");
            if (c.isValid()) {
                *value = c;
                refresh();
                changed();
            }
        });
    };
    auto &o = doc.options;
    auto templates = new QComboBox;
    templates->setObjectName("templateCombo");
    templates->addItems(templateNames());
    layout->addRow("胶片模板", templates);
    bindings << [this, templates] {
        QSignalBlocker b(templates);
        templates->setCurrentText(doc.options.filmTemplate);
    };
    connect(templates, &QComboBox::currentTextChanged, this, [this](const QString &s) {
        doc.options.filmTemplate = s;
        changed();
    });
    check(layout, "显示标题栏", &o.header);
    check(layout, "显示右侧胶卷信息", &o.details);
    spin(layout, "标题栏高度", &o.headerHeight, 180, 1000, "headerHeight");
    spin(layout, "外边框", &o.border, 0, 200, "borderSize");
    spin(layout, "胶片条间距", &o.gap, 0, 200, "stripGap");
    color(layout, "外边框颜色", &o.background);
    color(layout, "标题栏颜色", &o.headerColor);
    color(layout, "文字颜色", &o.textColor);
    auto outputLabel = new QLabel("导出设置");
    outputLabel->setObjectName("sectionTitle");
    layout->addRow(outputLabel);
    spin(layout, "输出宽度 px", &o.outputWidth, 600, 9000, "outputWidth");
    spin(layout, "JPEG 质量", &o.quality, 1, 100);
    auto tip = new QLabel("高度随排版自动计算。预览采用轻量分辨率，导出使用上述尺寸。单张最多 42 张照片。");
    tip->setWordWrap(true);
    tip->setObjectName("muted");
    layout->addRow(tip);
    line(text, "主标题", &o.brand, "mainTitle");
    auto alignment = new QComboBox;
    alignment->setObjectName("headerAlignment");
    alignment->addItems({"左对齐", "水平居中", "右对齐"});
    text->addRow("左侧顶部文字对齐", alignment);
    bindings << [this, alignment] {
        QSignalBlocker blocker(alignment);
        alignment->setCurrentIndex(doc.options.headerAlignment);
    };
    connect(alignment, &QComboBox::currentIndexChanged, this, [this](int index) {
        doc.options.headerAlignment = index;
        changed();
    });
    line(text, "左侧备注", &o.note);
    line(text, "胶卷标题", &o.title, "titleText");
    line(text, "胶片型号", &o.film);
    line(text, "ISO", &o.iso);
    line(text, "拍摄地点", &o.location);
    line(text, "拍摄日期", &o.date);
    line(text, "相机", &o.camera);
    line(text, "镜头", &o.lens);
    spin(text, "主标题字号", &o.brandSize, 20, 180);
    spin(text, "标题字号", &o.titleSize, 20, 120);
    spin(text, "信息字号", &o.infoSize, 16, 80);
    auto textTip = new QLabel("文字过长时自动省略；标题栏较矮时自动缩小文字。字号、间距以模板原始像素为单位。");
    textTip->setWordWrap(true);
    textTip->setObjectName("muted");
    text->addRow(textTip);
    check(photo, "竖幅照片自动顺时针转横", &o.autoRotate);
    check(photo, "完整显示照片（留边）", &o.fit);
    color(photo, "照片留边颜色", &o.matColor);
    photoInfo = new QLabel("选中一张照片，调整旋转与裁切。");
    photoInfo->setWordWrap(true);
    photo->addRow(photoInfo);
    auto rotateRow = new QHBoxLayout;
    button(rotateRow, "↶ 90°", [this] { changeSelected(-90); });
    button(rotateRow, "↷ 90°", [this] { changeSelected(90); });
    photo->addRow(rotateRow);
    cropX = new QSlider(Qt::Horizontal);
    cropX->setRange(-100, 100);
    cropX->setObjectName("cropX");
    photo->addRow("裁切位置 · 横", cropX);
    cropY = new QSlider(Qt::Horizontal);
    cropY->setRange(-100, 100);
    cropY->setObjectName("cropY");
    photo->addRow("裁切位置 · 纵", cropY);
    auto cropChanged = [this] {
        if (syncing || !photos->currentItem())
            return;
        auto item = photos->currentItem();
        auto p = itemPhoto(item);
        p.cropX = cropX->value() / 100.0;
        p.cropY = cropY->value() / 100.0;
        item->setData(Qt::UserRole, QVariant::fromValue(p));
        collectPhotos();
        changed();
    };
    connect(cropX, &QSlider::valueChanged, this, cropChanged);
    connect(cropY, &QSlider::valueChanged, this, cropChanged);
    auto reset = new QPushButton("重置所选照片调整");
    photo->addRow(reset);
    connect(reset, &QPushButton::clicked, this, [this] { changeSelected(0, true); });
    auto cropTip = new QLabel(
        "默认中心裁切为 3:2。横/纵滑块仅在该方向有溢出内容时生效；完整显示模式下不裁切。旋转按钮可作用于多选照片。");
    cropTip->setWordWrap(true);
    cropTip->setObjectName("muted");
    photo->addRow(cropTip);
    split->addWidget(tabs);
    split->setSizes({240, 850, 330});
    split->setStretchFactor(1, 1);
    outer->addWidget(split, 1);
    setCentralWidget(root);
    connect(photos, &PhotoList::filesDropped, this, &Window::addFiles);
    connect(photos, &QListWidget::currentRowChanged, this, &Window::selectPhoto);
    connect(photos->model(), &QAbstractItemModel::rowsMoved, this, [this] {
        collectPhotos();
        changed();
    });
    connect(preview, &Preview::photoClicked, this, [this](int i) { photos->setCurrentRow(i); });
    auto shortcut = [&](const QKeySequence &key, auto fn) {
        auto action = new QAction(this);
        action->setShortcut(key);
        addAction(action);
        connect(action, &QAction::triggered, this, fn);
    };
    shortcut(QKeySequence::Save, [this] { saveProject(); });
    shortcut(QKeySequence::SaveAs, [this] { saveProject(true); });
    shortcut(QKeySequence::Open, [this] { openProject(); });
    shortcut(QKeySequence("Ctrl+E"), [this] { exportImage(); });
    auto remove = new QShortcut(QKeySequence::Delete, photos);
    remove->setContext(Qt::WidgetShortcut);
    connect(remove, &QShortcut::activated, this, [this] {
        for (auto item : photos->selectedItems())
            delete photos->takeItem(photos->row(item));
        collectPhotos();
        changed();
    });
    setStyleSheet(R"(
        QMainWindow,QWidget{background:#22292e;color:#edf1f2;font-family:'Microsoft YaHei UI';font-size:12px;}
        QLabel#heading{font-size:20px;font-weight:700;letter-spacing:2px;} QLabel#muted{color:#9caeb7;font-size:11px;}
        QLabel#sectionTitle,QLabel#previewInfo{font-weight:600;font-size:13px;} QPushButton{background:#303b43;border:1px solid #43515a;border-radius:5px;padding:7px 10px;}
        QPushButton:hover{background:#40505a;} QPushButton:pressed{background:#1b5350;} QPushButton:disabled{color:#71818b;}
        QPushButton#exportButton{background:#80dfba;color:#102b22;font-weight:700;border:0;padding:9px 25px;}
        QPushButton#exportButton:hover{background:#a0efd0;} QLineEdit,QSpinBox,QComboBox{background:#192126;border:1px solid #45545d;border-radius:4px;padding:6px;selection-background-color:#327f6b;}
        QLineEdit:focus,QSpinBox:focus,QComboBox:focus{border:1px solid #80dfba;}
        QListWidget{background:#1b2227;border:1px solid #34424b;border-radius:6px;outline:0;}
        QListWidget::item{padding:5px;border-radius:4px;} QListWidget::item:selected{background:#32564f;color:#edfff8;}
        QTabWidget::pane{border:1px solid #34424b;border-radius:5px;} QTabBar::tab{background:#29343b;padding:10px 18px;}
        QTabBar::tab:selected{color:#9aeacb;border-bottom:2px solid #80dfba;background:#35443f;}
        QCheckBox{spacing:8px;} QCheckBox::indicator{width:16px;height:16px;}
        QSlider::groove:horizontal{background:#45545d;height:4px;border-radius:2px;} QSlider::handle:horizontal{background:#80dfba;width:14px;margin:-5px 0;border-radius:7px;}
        QScrollBar:vertical{background:#22292e;width:10px;} QScrollBar::handle:vertical{background:#53636e;min-height:20px;border-radius:4px;}
        QScrollBar::add-line:vertical,QScrollBar::sub-line:vertical{height:0;} QStatusBar{color:#a8b9c2;}
        QToolTip{background:#eff6f3;color:#172b23;border:1px solid #80dfba;padding:6px;}
    )");
}
void Window::syncControls() {
    syncing = true;
    for (auto &fn : bindings)
        fn();
    syncing = false;
    selectPhoto();
}
void Window::changed() {
    if (syncing)
        return;
    dirty = true;
    ++revision;
    updateTitle();
    count->setText(QString("照片  %1 / 42").arg(doc.photos.size()));
    if (previewControl)
        previewControl->canceled.store(true);
    if (doc.photos.isEmpty()) {
        preview->clearImage();
        previewInfo->setText("添加照片以显示实时预览");
    } else
        previewInfo->setText("预览更新中…");
    cropX->setEnabled(photos->currentItem() && !doc.options.fit);
    cropY->setEnabled(photos->currentItem() && !doc.options.fit);
    debounce.start();
}
void Window::updateTitle() {
    setWindowTitle(
        QString("%1%2 — Film Index Studio")
            .arg(projectPath.isEmpty() ? "未命名索引图" : QFileInfo(projectPath).fileName(), dirty ? " *" : ""));
}
void Window::startPreview() {
    if (previewWatcher.isRunning() || doc.photos.isEmpty())
        return;
    renderingRevision = revision;
    previewControl = std::make_shared<RenderControl>();
    Document snapshot = doc;
    auto control = previewControl;
    previewWatcher.setFuture(QtConcurrent::run([snapshot, control] { return renderSheet(snapshot, 1600, control); }));
}
void Window::collectPhotos() {
    doc.photos.clear();
    for (int i = 0; i < photos->count(); ++i) {
        auto item = photos->item(i);
        auto p = itemPhoto(item);
        doc.photos << p;
        item->setText(QString("%1   %2").arg(i + 1, 2, 10, QChar('0')).arg(QFileInfo(p.path).fileName()));
    }
    selectPhoto();
}
void Window::rebuildPhotos() {
    QSignalBlocker block(photos);
    photos->clear();
    for (int i = 0; i < doc.photos.size(); ++i) {
        const auto &p = doc.photos[i];
        auto item =
            new QListWidgetItem(QIcon(QPixmap::fromImage(thumbnail(p.path))),
                                QString("%1   %2").arg(i + 1, 2, 10, QChar('0')).arg(QFileInfo(p.path).fileName()));
        item->setData(Qt::UserRole, QVariant::fromValue(p));
        item->setToolTip(p.path);
        item->setSizeHint({180, 68});
        photos->addItem(item);
    }
    count->setText(QString("照片  %1 / 42").arg(doc.photos.size()));
    if (photos->count())
        photos->setCurrentRow(0);
    selectPhoto();
}
void Window::addFiles(QStringList paths) {
    if (exporting || paths.isEmpty())
        return;
    QStringList expanded, rejected;
    for (const auto &path : paths) {
        QFileInfo fi(path);
        if (fi.isDir()) {
            const auto files = QDir(path).entryInfoList(QDir::Files);
            for (const auto &f : files)
                if (QImageReader::supportedImageFormats().contains(f.suffix().toLower().toLatin1()) ||
                    f.suffix().compare("tif", Qt::CaseInsensitive) == 0)
                    expanded << f.absoluteFilePath();
        } else
            expanded << fi.absoluteFilePath();
    }
    QCollator coll;
    coll.setNumericMode(true);
    coll.setCaseSensitivity(Qt::CaseInsensitive);
    std::stable_sort(expanded.begin(), expanded.end(), [&](const QString &a, const QString &b) {
        return coll.compare(QFileInfo(a).fileName(), QFileInfo(b).fileName()) < 0;
    });
    int before = doc.photos.size(), overflow = 0;
    for (const auto &path : expanded) {
        bool exists = false;
        for (const auto &p : doc.photos)
            if (p.path.compare(path, Qt::CaseInsensitive) == 0) {
                exists = true;
                break;
            }
        if (exists)
            continue;
        if (doc.photos.size() >= 42) {
            ++overflow;
            continue;
        }
        QImageReader reader(path);
        if (!reader.canRead()) {
            rejected << QFileInfo(path).fileName();
            continue;
        }
        doc.photos.append({path, 0, 0, 0});
    }
    if (doc.photos.size() != before) {
        rebuildPhotos();
        changed();
    }
    if (!rejected.isEmpty() || overflow)
        report(QString("未添加：%1 个文件无法读取，%2 个文件超过 42 张上限。\n%3")
                   .arg(rejected.size())
                   .arg(overflow)
                   .arg(rejected.mid(0, 8).join('\n')));
}
void Window::selectPhoto() {
    if (!cropX)
        return;
    QSignalBlocker bx(cropX), by(cropY);
    auto item = photos->currentItem();
    cropX->setEnabled(item && !doc.options.fit);
    cropY->setEnabled(item && !doc.options.fit);
    if (item) {
        auto p = itemPhoto(item);
        cropX->setValue(qRound(p.cropX * 100));
        cropY->setValue(qRound(p.cropY * 100));
        photoInfo->setText(QString("第 %1 张 · 手动旋转 %2°\n%3")
                               .arg(photos->currentRow() + 1)
                               .arg(p.rotation)
                               .arg(QFileInfo(p.path).fileName()));
    } else {
        cropX->setValue(0);
        cropY->setValue(0);
        photoInfo->setText("选中照片，调整旋转与裁切。");
    }
    preview->selectPhoto(photos->currentRow());
}
void Window::changeSelected(int degrees, bool reset) {
    if (photos->selectedItems().isEmpty())
        return;
    for (auto item : photos->selectedItems()) {
        auto p = itemPhoto(item);
        p.rotation = reset ? 0 : (p.rotation + degrees + 360) % 360;
        if (reset)
            p.cropX = p.cropY = 0;
        item->setData(Qt::UserRole, QVariant::fromValue(p));
    }
    collectPhotos();
    changed();
}
void Window::report(const QString &message) {
    statusBar()->showMessage(message);
    if (smokeDir.isEmpty())
        QMessageBox::warning(this, "Film Index Studio", message);
}
bool Window::confirmDiscard() {
    if (!dirty)
        return true;
    auto answer =
        QMessageBox::question(this, "保存当前项目？", "当前照片排列和参数尚未保存。",
                              QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    return answer == QMessageBox::Discard || (answer == QMessageBox::Save && saveProject());
}
bool Window::saveProject(bool saveAs) {
    QString path = projectPath;
    if (path.isEmpty() || saveAs)
        path = QFileDialog::getSaveFileName(this, "保存项目", path, "Film Index Studio 项目 (*.findex)");
    if (path.isEmpty())
        return false;
    if (!path.endsWith(".findex", Qt::CaseInsensitive))
        path += ".findex";
    QString error;
    if (!saveDocument(doc, path, &error)) {
        report(error);
        return false;
    }
    projectPath = path;
    dirty = false;
    updateTitle();
    statusBar()->showMessage("项目已保存 · 项目引用原图路径，不复制原图。");
    return true;
}
void Window::openProject(const QString &given) {
    if (exporting || !confirmDiscard())
        return;
    QString path = given.isEmpty()
                       ? QFileDialog::getOpenFileName(this, "打开项目", {}, "Film Index Studio 项目 (*.findex)")
                       : given;
    if (path.isEmpty())
        return;
    Document next;
    QString error;
    if (!loadDocument(path, &next, &error)) {
        report(error);
        return;
    }
    doc = next;
    projectPath = path;
    rebuildPhotos();
    syncControls();
    changed();
    dirty = false;
    updateTitle();
}
void Window::exportImage(const QString &given) {
    if (exporting)
        return;
    QString error = validateDocument(doc);
    if (!error.isEmpty()) {
        report(error);
        return;
    }
    if (qint64(outputSize(doc).width()) * outputSize(doc).height() > 100000000) {
        report("输出超过 1 亿像素，请降低输出宽度。");
        return;
    }
    QString path = given;
    if (path.isEmpty()) {
        QString filter = "JPEG 图像 (*.jpg)";
        path =
            QFileDialog::getSaveFileName(this, "导出当前索引图", lastExport.isEmpty() ? "Film-index.jpg" : lastExport,
                                         "JPEG 图像 (*.jpg);;PNG 图像 (*.png)", &filter);
        if (path.isEmpty())
            return;
        if (QFileInfo(path).suffix().isEmpty())
            path += filter.startsWith("PNG") ? ".png" : ".jpg";
    }
    QString target = QFileInfo(path).absoluteFilePath();
    for (const auto &p : doc.photos)
        if (QFileInfo(p.path).absoluteFilePath().compare(target, Qt::CaseInsensitive) == 0 ||
            (!QFileInfo(path).canonicalFilePath().isEmpty() &&
             QFileInfo(p.path).canonicalFilePath().compare(QFileInfo(path).canonicalFilePath(), Qt::CaseInsensitive) ==
                 0)) {
            report("导出路径与原照片相同，请选择其他文件名。");
            return;
        }
    exporting = true;
    lastExport = path;
    exportControl = std::make_shared<RenderControl>();
    auto progress = new QProgressDialog("正在渲染当前索引图…", "取消", 0, doc.photos.size() + 1, this);
    progress->setWindowTitle("导出索引图");
    progress->setWindowModality(Qt::ApplicationModal);
    progress->setMinimumDuration(0);
    progress->setAutoClose(false);
    progress->setAutoReset(false);
    progress->setValue(0);
    connect(progress, &QProgressDialog::canceled, this, [this] { exportControl->canceled.store(true); });
    auto poll = new QTimer(progress);
    poll->setInterval(100);
    connect(poll, &QTimer::timeout, this, [this, progress] {
        progress->setValue(exportControl->progress.load());
        if (exportControl->progress.load() == doc.photos.size())
            progress->setLabelText("正在编码并保存图像…");
    });
    poll->start();
    connect(&exportWatcher, &QFutureWatcher<QString>::finished, progress, [this, progress, poll, path] {
        poll->stop();
        progress->close();
        progress->deleteLater();
        exporting = false;
        QString result = exportWatcher.result();
        if (exportControl->canceled.load())
            statusBar()->showMessage("导出已取消。");
        else if (!result.isEmpty())
            report(result);
        else {
            statusBar()->showMessage("已导出：" + path);
            if (smokeDir.isEmpty()) {
                QMessageBox done(QMessageBox::Information, "导出完成",
                                 "索引图已保存：\n" + QDir::toNativeSeparators(path), QMessageBox::Ok, this);
                auto open = done.addButton("打开图片", QMessageBox::ActionRole);
                done.exec();
                if (done.clickedButton() == open)
                    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
            }
        }
        if (!smokeDir.isEmpty()) {
            QFile f(smokeDir + "/gui-smoke-result.txt");
            f.open(QIODevice::WriteOnly);
            f.write(result.isEmpty() ? "PASS\n" : result.toUtf8());
            f.close();
            dirty = false;
            QTimer::singleShot(100, qApp, [result] { qApp->exit(result.isEmpty() ? 0 : 1); });
        }
    });
    Document snapshot = doc;
    auto control = exportControl;
    exportWatcher.setFuture(QtConcurrent::run([snapshot, control, path] {
        auto result = renderSheet(snapshot, 0, control);
        if (result.canceled)
            return QString("导出已取消。");
        if (!result.error.isEmpty())
            return result.error;
        return writeImage(result.image, path, snapshot.options.quality, control);
    }));
}
void Window::closeEvent(QCloseEvent *e) {
    if (exporting) {
        report("当前正在导出，请先完成或取消导出。");
        e->ignore();
        return;
    }
    if (confirmDiscard())
        e->accept();
    else
        e->ignore();
}
void Window::dragEnterEvent(QDragEnterEvent *e) {
    if (e->mimeData()->hasUrls())
        e->acceptProposedAction();
}
void Window::dropEvent(QDropEvent *e) {
    auto paths = urlPaths(e->mimeData());
    if (paths.size() == 1 && paths[0].endsWith(".findex", Qt::CaseInsensitive))
        openProject(paths[0]);
    else
        addFiles(paths);
    e->acceptProposedAction();
}
void Window::startSmokeTest(const QString &directory) {
    smokeDir = QFileInfo(directory).absoluteFilePath();
    QDir().mkpath(smokeDir + "/samples");
    doc.options.title = "城市漫游 · 秋日光影";
    doc.options.brand = "Film Index Studio";
    doc.options.headerAlignment = 1;
    doc.options.note = "2026 / CONTACT SHEET";
    doc.options.film = "KODAK GOLD 200";
    doc.options.iso = "200";
    doc.options.location = "SHANGHAI";
    doc.options.date = "2026.10";
    doc.options.camera = "Nikon F3";
    doc.options.lens = "50mm f/1.4";
    doc.options.filmTemplate = "kodak-gc-400";
    for (int i = 0; i < 18; ++i) {
        QImage img(900, 600, QImage::Format_RGB32);
        QPainter p(&img);
        QLinearGradient g(0, 0, 900, 600);
        g.setColorAt(0, QColor::fromHsl((i * 27 + 180) % 360, 65, 100));
        g.setColorAt(1, QColor::fromHsl((i * 27 + 220) % 360, 100, 35));
        p.fillRect(img.rect(), g);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(245, 218, 170, 190));
        p.drawEllipse(QPointF(650 - i % 3 * 100, 150), 65, 65);
        for (int j = 0; j < 10; ++j) {
            p.setBrush(QColor(15 + j * 3, 30 + j * 3, 37 + j * 3, 220));
            int h = 100 + (i * 37 + j * 71) % 240;
            p.drawRect(j * 100, 600 - h, 90, h);
        }
        p.setPen(QColor("#f4efdf"));
        QFont f("Microsoft YaHei UI");
        f.setPixelSize(30);
        p.setFont(f);
        p.drawText(QRect(35, 30, 800, 60), "FILM STUDY   /   " + QString::number(i + 1).rightJustified(2, '0'));
        p.end();
        QString path = smokeDir + QString("/samples/frame-%1.png").arg(i + 1, 2, 10, QChar('0'));
        img.save(path);
        doc.photos.append({path, 0, 0, 0});
    }
    rebuildPhotos();
    syncControls();
    changed();
    // Exercise real widget signals, not a separate preview implementation.
    findChild<QSpinBox *>("stripGap")->setValue(45);
    findChild<QLineEdit *>("titleText")->setText("城市漫游 · 秋日光影");
    photos->setCurrentRow(3);
    changeSelected(180);
    cropX->setValue(20);
    QString error;
    saveDocument(doc, smokeDir + "/示例项目.findex", &error);
    QTimer::singleShot(60000, this, [this] {
        if (exporting && exportControl)
            exportControl->canceled.store(true);
        qApp->exit(2);
    });
}
