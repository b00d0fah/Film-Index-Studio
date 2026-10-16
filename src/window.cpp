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
    setResizeAnchor(AnchorViewCenter);
    setObjectName("preview");
}
void Preview::clearImage() {
    scene()->clear();
    selection = nullptr;
    pixmapItem = nullptr;
    frames.clear();
    scene()->setSceneRect({});
}
void Preview::markStale() {
    frames.clear();
    if (selection)
        selection->hide();
}
void Preview::setImage(const QImage &image, const Document &d) {
    const QRectF old = scene()->sceneRect();
    const QPointF center = mapToScene(viewport()->rect().center());
    const QPointF relative =
        old.isEmpty() ? QPointF(.5, .5) : QPointF(center.x() / old.width(), center.y() / old.height());
    if (pixmapItem)
        pixmapItem->setPixmap(QPixmap::fromImage(image));
    else
        pixmapItem = scene()->addPixmap(QPixmap::fromImage(image));
    frames.clear();
    scene()->setSceneRect(QRectF(image.rect()));
    if (!fitted && !old.isEmpty()) {
        setTransformationAnchor(NoAnchor);
        const double ratio = old.width() / image.width();
        scale(ratio, ratio);
        centerOn(relative.x() * image.width(), relative.y() * image.height());
        setTransformationAnchor(AnchorUnderMouse);
    }
    double s = double(image.width()) / baseSheetSize(d).width();
    for (int i = 0; i < d.photos.size(); ++i) {
        auto rect = photoRect(d, i);
        frames << QRectF(rect.x() * s, rect.y() * s, rect.width() * s, rect.height() * s);
    }
    QPen pen(QColor("#53e0bb"), 2);
    pen.setCosmetic(true);
    if (!selection)
        selection = scene()->addRect({}, pen);
    selection->hide();
    if (fitted)
        fit();
}
void Preview::focusRegion(const QRectF &baseRect, double baseWidth) {
    if (fitted || !pixmapItem || baseWidth <= 0)
        return;
    const double ratio = scene()->sceneRect().width() / baseWidth;
    centerOn(baseRect.center() * ratio);
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
Window::Window(const QString &presetDirectory)
    : presetFolder(presetDirectory.isEmpty() ? QCoreApplication::applicationDirPath() + "/presets" : presetDirectory) {
    QString startupMessage;
    QSettings settings(presetFolder + "/startup.ini", QSettings::IniFormat);
    const QString selected = settings.value("startup/preset").toString();
    if (!selected.isEmpty()) {
        Options loaded;
        QString error;
        if (QFileInfo(selected).fileName() == selected && loadPreset(presetFolder + '/' + selected, &loaded, &error)) {
            doc.options = loaded;
            projectPath = presetFolder + '/' + selected;
            startupMessage = "已加载启动预设：" + QFileInfo(selected).completeBaseName();
        } else {
            startupMessage = "启动预设不可用，已使用内置默认值。请在排版 → 启动预设重新选择。";
        }
    }
    setAcceptDrops(true);
    resize(1440, 900);
    setMinimumSize(1080, 680);
    setWindowIcon(QIcon(":/assets/logo.png"));
    buildUi();
    progressTimer.setInterval(60);
    connect(&progressTimer, &QTimer::timeout, this, [this] {
        if (previewWatcher.isRunning() && previewControl && renderingRevision == revision && !debounce.isActive() &&
            renderElapsed.elapsed() >= 250) {
            previewProgress->setFormat("正在渲染预览 %v / %m");
            previewProgress->setRange(0, qMax(1, int(doc.photos.size())));
            previewProgress->setValue(previewControl->progress.load());
        }
        if (thumbnailWatcher.isRunning() && thumbnailControl)
            thumbnailProgress->setValue(thumbnailControl->progress.load());
    });
    progressTimer.start();
    connect(&thumbnailWatcher, &QFutureWatcher<QVector<QPair<QString, QImage>>>::finished, this, [this] {
        thumbnailBusy = false;
        for (const auto &entry : thumbnailWatcher.result())
            thumbnails.insert(entry.first, entry.second);
        QStringList failed;
        for (int i = 0; i < photos->count(); ++i) {
            auto item = photos->item(i);
            const QString path = itemPhoto(item).path;
            if (thumbnails.contains(path)) {
                const auto img = thumbnails.value(path);
                item->setIcon(QPixmap::fromImage(img));
                if (img.isNull()) {
                    item->setToolTip("无法解码照片：" + path);
                    failed << QFileInfo(path).fileName();
                }
            }
        }
        thumbnailProgress->hide();
        if (!failed.isEmpty())
            statusBar()->showMessage("缩略图读取失败：" + failed.join("、"));
        startThumbnails();
    });
    debounce.setSingleShot(true);
    debounce.setInterval(160);
    connect(&debounce, &QTimer::timeout, this, &Window::startPreview);
    connect(&previewWatcher, &QFutureWatcher<RenderResult>::finished, this, [this] {
        previewBusy = false;
        if (renderingRevision != revision) {
            if (!debounce.isActive())
                debounce.start();
            return;
        }
        auto result = previewWatcher.result();
        if (result.canceled) {
            if (!doc.photos.isEmpty())
                debounce.start();
            return;
        }
        if (!result.error.isEmpty() || result.image.isNull()) {
            previewFailed(result.error.isEmpty() ? "渲染返回空图像，请降低预览分辨率后重试。" : result.error);
            return;
        }
        previewError.clear();
        failureButton->setEnabled(false);
        previewInfo->setToolTip({});
        previewProgress->setToolTip({});
        previewProgress->setRange(0, 100);
        previewProgress->setValue(100);
        previewProgress->setFormat("预览已更新");
        preview->setImage(result.image, doc);
        preview->selectPhoto(photos->currentRow());
        const auto base = baseSheetSize(doc);
        QRectF target;
        if (pendingFocus >= 1 && pendingFocus <= 6 && doc.options.header)
            target = headerBlockRect(doc.options, base.width(), pendingFocus - 1);
        else if (pendingFocus == 7 && photos->currentRow() >= 0)
            target = photoRect(doc, photos->currentRow());
        else if (pendingFocus == 8 && doc.options.header)
            target = {0, 0, double(base.width()), double(doc.options.headerHeight)};
        else if (pendingFocus == 9)
            target = {0, double(doc.options.header ? doc.options.headerHeight + doc.options.headerGap : 0),
                      double(base.width()), 700};
        else if (pendingFocus == 10 && doc.options.header)
            target = overlayRect(doc.options.logo,
                                 {20, 20, double(base.width() - 40), double(doc.options.headerHeight - 40)}, true);
        else if (pendingFocus == 11)
            target = overlayRect(doc.options.watermark, {20, 20, double(base.width() - 40), double(base.height() - 40)},
                                 false);
        else if (pendingFocus == 12 && doc.options.header)
            target = {0, double(doc.options.headerHeight), double(base.width()),
                      double(qMax(1, doc.options.headerGap))};
        if (!target.isEmpty())
            preview->focusRegion(target, base.width());
        pendingFocus = 0;
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
                if (auto tabs = findChild<QTabWidget *>()) {
                    tabs->setCurrentIndex(1);
                    grab().save(smokeDir + "/文字设置.png");
                }
                exportImage(smokeDir + "/示例索引图.jpg");
            });
        }
    });
    syncControls();
    updateTitle();
    statusBar()->showMessage(startupMessage.isEmpty() ? "添加照片或将图片拖入窗口，开始制作一张索引图。"
                                                      : startupMessage);
}
Window::~Window() {
    if (previewControl)
        previewControl->canceled.store(true);
    if (exportControl)
        exportControl->canceled.store(true);
    if (thumbnailControl)
        thumbnailControl->canceled.store(true);
    thumbnailWatcher.waitForFinished();
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
    button(bar, "打开预设", [this] { openProject(); }, "openPreset");
    button(bar, "保存预设", [this] { saveProject(true); }, "savePreset");
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
    thumbnailProgress = new QProgressBar;
    thumbnailProgress->setObjectName("thumbnailProgress");
    thumbnailProgress->setFormat("生成缩略图 %v / %m");
    thumbnailProgress->hide();
    ll->addWidget(thumbnailProgress);
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
    previewInfo->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    viewBar->addWidget(previewInfo, 1);
    button(viewBar, "−", [this] { preview->zoom(1 / 1.2); });
    button(viewBar, "＋", [this] { preview->zoom(1.2); });
    button(viewBar, "适应", [this] { preview->fit(); });
    cl->addLayout(viewBar);
    preview = new Preview;
    cl->addWidget(preview);
    previewProgress = new QProgressBar;
    previewProgress->setObjectName("previewProgress");
    previewProgress->setFormat("等待添加照片");
    previewProgress->setRange(0, 100);
    previewProgress->setValue(0);
    previewProgress->setFixedHeight(20);
    auto progressRow = new QHBoxLayout;
    progressRow->addWidget(previewProgress, 1);
    retryButton = button(progressRow, "重新生成", [this] { retryPreview(); }, "retryPreview");
    retryButton->setEnabled(false);
    retryButton->setToolTip("重新读取源文件并生成预览；保留当前照片、设置和缩放。");
    failureButton = button(
        progressRow, "失败详情",
        [this] {
            QMessageBox details(this);
            details.setWindowTitle("预览失败原因");
            details.setTextFormat(Qt::PlainText);
            details.setIcon(QMessageBox::Warning);
            details.setText(preview->scene()->items().isEmpty() ? "预览生成失败，修复原因后可重新生成。"
                                                                : "预览生成失败，上次成功的画面已保留。");
            details.setInformativeText(
                previewError + "\n\n请检查相关文件是否存在、能否打开；内存不足时降低预览分辨率。修复后点击重新生成。");
            details.setDetailedText("诊断日志：" + previewLogPath + "\n\n" + previewError);
            auto retry = details.addButton("重新生成", QMessageBox::AcceptRole);
            details.addButton("关闭", QMessageBox::RejectRole);
            details.exec();
            if (details.clickedButton() == retry)
                retryPreview();
        },
        "previewFailureDetails");
    failureButton->setEnabled(false);
    cl->addLayout(progressRow);
    auto help = new QLabel("点击预览中的照片进行调整  ·  Ctrl + 滚轮缩放  ·  拖动平移");
    help->setObjectName("muted");
    help->setAlignment(Qt::AlignCenter);
    cl->addWidget(help);
    split->addWidget(center);
    auto tabs = new QTabWidget;
    tabs->setMinimumWidth(360);
    tabs->setMaximumWidth(460);
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
    photo->setProperty("focusRegion", 7);
    auto tag = [](QWidget *w, QFormLayout *f) { w->setProperty("focusRegion", f->property("focusRegion")); };
    auto section = [&](QFormLayout *parent, const QString &title, int focus) {
        auto container = new QFrame;
        container->setObjectName("settingsSection");
        auto box = new QVBoxLayout(container);
        box->setContentsMargins(1, 1, 1, 1);
        box->setSpacing(0);
        auto toggle = new QToolButton;
        toggle->setText(title);
        toggle->setObjectName("sectionToggle");
        toggle->setCheckable(true);
        toggle->setChecked(false);
        toggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        toggle->setArrowType(Qt::RightArrow);
        toggle->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        auto body = new QWidget;
        body->setObjectName("sectionBody");
        auto f = new QFormLayout(body);
        f->setContentsMargins(10, 12, 10, 12);
        f->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        f->setVerticalSpacing(10);
        f->setProperty("focusRegion", focus);
        body->setVisible(false);
        box->addWidget(toggle);
        box->addWidget(body);
        parent->addRow(container);
        connect(toggle, &QToolButton::toggled, this, [body, toggle](bool on) {
            body->setVisible(on);
            toggle->setArrowType(on ? Qt::DownArrow : Qt::RightArrow);
        });
        return f;
    };
    auto tip = [](QFormLayout *f, const QString &message) {
        auto l = new QLabel(message);
        l->setWordWrap(true);
        l->setObjectName("muted");
        f->addRow(l);
    };
    auto spin = [&](QFormLayout *f, QString label, int *value, int min, int max, const QString &name = QString()) {
        auto w = new QSpinBox;
        w->setRange(min, max);
        w->setObjectName(name);
        tag(w, f);
        f->addRow(label, w);
        bindings << [w, value] {
            QSignalBlocker b(w);
            w->setValue(*value);
        };
        connect(w, &QSpinBox::valueChanged, this, [this, value](int v) {
            *value = v;
            changed();
        });
        return w;
    };
    auto check = [&](QFormLayout *f, QString label, bool *value) {
        auto w = new QCheckBox(label);
        tag(w, f);
        f->addRow(w);
        bindings << [w, value] {
            QSignalBlocker b(w);
            w->setChecked(*value);
        };
        connect(w, &QCheckBox::toggled, this, [this, value](bool v) {
            *value = v;
            changed();
        });
        return w;
    };
    auto line = [&](QFormLayout *f, QString label, QString *value, const QString &name = QString()) {
        auto w = new QLineEdit;
        w->setMaxLength(250);
        w->setClearButtonEnabled(true);
        w->setObjectName(name);
        tag(w, f);
        f->addRow(label, w);
        bindings << [w, value] {
            QSignalBlocker b(w);
            w->setText(*value);
        };
        connect(w, &QLineEdit::textChanged, this, [this, value](const QString &v) {
            *value = v;
            changed();
        });
        return w;
    };
    auto color = [&](QFormLayout *f, QString label, QColor *value) {
        auto w = new QPushButton;
        tag(w, f);
        w->setObjectName("colorButton");
        w->setIconSize({24, 18});
        w->setStyleSheet("text-align:left;");
        f->addRow(label, w);
        auto refresh = [w, value] {
            QPixmap swatch(48, 36);
            swatch.fill(*value);
            swatch.setDevicePixelRatio(2);
            w->setIcon(QIcon(swatch));
            w->setText(value->name().toUpper());
        };
        bindings << refresh;
        connect(w, &QPushButton::clicked, this, [this, value, refresh] {
            QColorDialog dialog(*value, this);
            dialog.setWindowTitle("选择颜色");
            dialog.setOption(QColorDialog::DontUseNativeDialog);
            if (dialog.exec() == QDialog::Accepted) {
                *value = dialog.selectedColor();
                refresh();
                changed();
            }
        });
        return w;
    };
    auto imagePicker = [&](QFormLayout *f, QString *path, const QString &label) {
        auto w = new QWidget;
        auto row = new QHBoxLayout(w);
        row->setContentsMargins(0, 0, 0, 0);
        auto pick = new QPushButton("选择图片…"), clear = new QPushButton("清除");
        tag(pick, f);
        tag(clear, f);
        row->addWidget(pick, 1);
        row->addWidget(clear);
        f->addRow(label, w);
        auto refresh = [pick, path] {
            pick->setText(path->isEmpty() ? "选择图片…" : QFileInfo(*path).fileName());
            pick->setToolTip(*path);
        };
        bindings << refresh;
        connect(pick, &QPushButton::clicked, this, [this, path, refresh] {
            QString file = QFileDialog::getOpenFileName(this, "选择图片", {},
                                                        "图片 (*.png *.jpg *.jpeg *.webp *.bmp *.tif *.tiff *.svg)");
            if (file.isEmpty())
                return;
            QImageReader reader(file);
            if (!reader.canRead()) {
                report("无法读取该图片。");
                return;
            }
            *path = file;
            refresh();
            changed();
        });
        connect(clear, &QPushButton::clicked, this, [this, path, refresh] {
            path->clear();
            refresh();
            changed();
        });
    };
    auto fontControl = [&](QFormLayout *f, TextStyle *style) {
        auto family = new QFontComboBox;
        family->setMinimumWidth(90);
        tag(family, f);
        f->addRow("字体", family);
        bindings << [family, style] {
            QSignalBlocker b(family);
            family->setCurrentFont(QFont(style->family));
        };
        connect(family, &QFontComboBox::currentFontChanged, this, [this, style](const QFont &font) {
            style->family = font.family();
            style->fontFile = importedFontFile(font.family());
            changed();
        });
        color(f, "文本颜色", &style->color);
    };
    auto &o = doc.options;
    auto templates = new QComboBox;
    templates->setObjectName("templateCombo");
    templates->setMinimumWidth(90);
    templates->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    templates->setProperty("focusRegion", 9);
    layout->addRow("胶片模板", templates);
    auto refreshTemplates = [this, templates] {
        QSignalBlocker b(templates);
        templates->clear();
        templates->addItems(templateNames());
        if (templates->findText(doc.options.filmTemplate) < 0)
            templates->addItem(doc.options.filmTemplate);
        templates->setCurrentText(doc.options.filmTemplate);
    };
    bindings << refreshTemplates;
    connect(templates, &QComboBox::currentTextChanged, this, [this](const QString &name) {
        doc.options.filmTemplate = name;
        doc.options.templatePath.clear();
        changed(9);
    });
    auto templateActions = new QHBoxLayout;
    button(templateActions, "导入模板", [this, refreshTemplates] {
        QString dir = QFileDialog::getExistingDirectory(this, "选择胶片模板文件夹");
        if (dir.isEmpty())
            return;
        QString name, error;
        if (!importTemplate(dir, &name, &error)) {
            report(error);
            return;
        }
        doc.options.filmTemplate = name;
        doc.options.templatePath.clear();
        refreshTemplates();
        changed(9);
    });
    button(templateActions, "刷新", [refreshTemplates] { refreshTemplates(); });
    button(templateActions, "模板目录", [this] {
        if (!QDir().mkpath(templatesDirectory())) {
            report("无法创建模板目录。");
            return;
        }
        QDesktopServices::openUrl(QUrl::fromLocalFile(templatesDirectory()));
    });
    layout->addRow(templateActions);
    check(layout, "显示标题栏", &o.header)->setProperty("focusRegion", 8);
    check(layout, "显示右侧胶卷信息", &o.details)->setProperty("focusRegion", 3);
    spin(layout, "标题栏高度", &o.headerHeight, 180, 1000, "headerHeight")->setProperty("focusRegion", 8);
    spin(layout, "标题栏与索引间距", &o.headerGap, 0, 500, "headerGap")->setProperty("focusRegion", 12);
    spin(layout, "外边框", &o.border, 0, 200, "borderSize");
    spin(layout, "胶片条间距", &o.gap, 0, 200, "stripGap")->setProperty("focusRegion", 9);
    color(layout, "索引背景颜色", &o.background)->setProperty("focusRegion", 9);
    color(layout, "标题栏颜色", &o.headerColor)->setProperty("focusRegion", 8);
    auto backgroundControls = [&](QFormLayout *f, ImageLayer *v) {
        imagePicker(f, &v->path, "背景图片");
        spin(f, "不透明度 %", &v->opacity, 0, 100);
        spin(f, "裁切焦点 · 横 %", &v->cropX, 0, 100);
        spin(f, "裁切焦点 · 纵 %", &v->cropY, 0, 100);
        tip(f, "自动等比填满区域并裁切；0% 不透明度完全透明，100% 完全显示。裁切焦点控制保留位置。");
    };
    backgroundControls(section(layout, "标题栏图片背景", 8), &o.headerImage);
    backgroundControls(section(layout, "索引区域图片背景", 9), &o.indexImage);
    auto overlayControls = [&](QFormLayout *f, Overlay *v, bool isLogo) {
        check(f, isLogo ? "显示 Logo" : "显示水印", &v->enabled)->setObjectName(isLogo ? "showLogo" : "showWatermark");
        if (!isLogo) {
            auto mode = new QComboBox;
            mode->addItems({"文字", "图片"});
            tag(mode, f);
            f->addRow("水印类型", mode);
            bindings << [mode, v] {
                QSignalBlocker b(mode);
                mode->setCurrentIndex(v->type);
            };
            connect(mode, &QComboBox::currentIndexChanged, this, [this, v](int i) {
                v->type = i;
                changed(11);
            });
            line(f, "水印文本", &v->text, "watermarkText");
            fontControl(f, &v->style);
        }
        imagePicker(f, &v->path, isLogo ? "自定义 Logo" : "水印图片");
        spin(f, "水平 X（横）%", &v->x, 0, 100, isLogo ? "logoX" : "watermarkX");
        spin(f, "垂直 Y（纵）%", &v->y, 0, 100, isLogo ? "logoY" : "watermarkY");
        spin(f, isLogo ? "Logo 尺寸" : "字号 / 图片尺寸", &v->size, 16, 2000, isLogo ? "logoSize" : "watermarkSize");
        spin(f, "不透明度 %", &v->opacity, 0, 100, isLogo ? "logoOpacity" : "watermarkOpacity");
        if (!isLogo) {
            auto resetPosition = new QPushButton("恢复默认位置（横 50 / 纵 5）");
            resetPosition->setObjectName("resetWatermarkPosition");
            f->addRow(resetPosition);
            connect(resetPosition, &QPushButton::clicked, this, [this, v] {
                v->x = 50;
                v->y = 5;
                syncControls();
                changed(11);
            });
        }
        tip(f, isLogo ? "Logo 显示在标题栏；未选图片时使用内置 Logo。坐标按可移动空间计算，0% 靠左/上，100% 靠右/下。"
                      : "水印位于整张索引图最上层；图片保持比例，位置以可移动空间百分比表示。");
    };
    overlayControls(section(layout, "Logo", 10), &o.logo, true);
    overlayControls(section(layout, "自定义水印", 11), &o.watermark, false);
    auto startup = section(layout, "启动预设", 0);
    startupPresets = new QComboBox;
    startupPresets->setObjectName("startupPreset");
    startupPresets->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    startupPresets->setMinimumContentsLength(12);
    startup->addRow("每次启动使用", startupPresets);
    auto refreshPresets = new QPushButton("刷新预设列表");
    startup->addRow(refreshPresets);
    connect(refreshPresets, &QPushButton::clicked, this, &Window::refreshStartupPresets);
    refreshStartupPresets();
    connect(startupPresets, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (index < 0)
            return;
        const QString name = startupPresets->currentData().toString();
        QString error;
        Options checked;
        if (!name.isEmpty() && !loadPreset(presetFolder + '/' + name, &checked, &error)) {
            refreshStartupPresets();
            report("无法设为启动预设：" + error);
            return;
        }
        QDir().mkpath(presetFolder);
        QSettings settings(presetFolder + "/startup.ini", QSettings::IniFormat);
        settings.setValue("startup/preset", name);
        settings.sync();
        if (settings.status() != QSettings::NoError) {
            refreshStartupPresets();
            report("无法保存启动设置，请检查软件目录是否可写。");
            return;
        }
        statusBar()->showMessage("启动预设已设置，下次启动生效；当前编辑保持不变。");
    });
    tip(startup, "仅列出软件 presets 文件夹内已保存的预设。选择后下次启动生效；选择内置默认值可取消自动加载。");
    auto output = section(layout, "预览与输出", 0);
    spin(output, "预览宽度 px", &o.previewWidth, 600, 4572, "previewWidth");
    auto unit = new QComboBox;
    unit->addItems({"px（像素）", "毫米 mm", "厘米 cm", "英寸 in"});
    unit->setObjectName("outputUnit");
    output->addRow("输出单位", unit);
    auto width = new QDoubleSpinBox;
    width->setObjectName("outputWidth");
    width->setKeyboardTracking(false);
    output->addRow("输出宽度", width);
    auto dpi = new QSpinBox;
    dpi->setRange(72, 1200);
    dpi->setObjectName("outputDpi");
    output->addRow("输出 DPI", dpi);
    auto updateWidth = [this, width, unit, dpi] {
        QSignalBlocker bw(width), bu(unit), bd(dpi);
        const auto &v = doc.options;
        unit->setCurrentIndex(v.outputUnit);
        dpi->setValue(v.dpi);
        width->setDecimals(v.outputUnit == 0 ? 0 : 3);
        width->setRange(widthInUnit(600, v.outputUnit, v.dpi), widthInUnit(9000, v.outputUnit, v.dpi));
        width->setValue(widthInUnit(v.outputWidth, v.outputUnit, v.dpi));
    };
    bindings << updateWidth;
    connect(unit, &QComboBox::currentIndexChanged, this, [this, updateWidth](int i) {
        doc.options.outputUnit = i;
        updateWidth();
        changed(0);
    });
    connect(width, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        doc.options.outputWidth = qBound(600, widthInPixels(v, doc.options.outputUnit, doc.options.dpi), 9000);
        changed(0);
    });
    connect(dpi, &QSpinBox::valueChanged, this, [this, width, updateWidth](int v) {
        auto &o = doc.options;
        double physical = width->value();
        o.dpi = v;
        if (o.outputUnit)
            o.outputWidth = qBound(600, widthInPixels(physical, o.outputUnit, o.dpi), 9000);
        updateWidth();
        changed(0);
    });
    spin(output, "JPEG 质量", &o.quality, 1, 100);
    tip(output, "预览分辨率与导出独立。像素宽度限定 600–9000；实物单位根据 DPI 换算。高度随排版计算。");
    auto alignControls = [&](bool right) {
        auto f = section(text, right ? "右侧顶部文字位置" : "左侧顶部文字位置", right ? 3 : 1);
        auto mode = new QComboBox;
        mode->setObjectName(right ? "rightAlignment" : "headerAlignment");
        mode->addItems({"左对齐", "水平居中", "右对齐", "自定义位置"});
        tag(mode, f);
        f->addRow("对齐方式", mode);
        int *value = right ? &o.rightAlignment : &o.headerAlignment;
        auto x = spin(f, "自定义横向 px", right ? &o.rightX : &o.leftX, 0, 4502, right ? "rightX" : "leftX");
        auto y = spin(f, "自定义纵向 px", right ? &o.rightY : &o.leftY, 0, 1000, right ? "rightY" : "leftY");
        bindings << [mode, value, x, y] {
            QSignalBlocker b(mode);
            mode->setCurrentIndex(*value);
            x->setEnabled(*value == 3);
            y->setEnabled(*value == 3);
        };
        connect(mode, &QComboBox::currentIndexChanged, this, [this, value, x, y](int v) {
            *value = v;
            x->setEnabled(v == 3);
            y->setEnabled(v == 3);
            changed();
        });
    };
    alignControls(false);
    alignControls(true);
    tip(text, "自定义坐标以各栏左上角为原点、按模板原始像素调整，越界自动限制在栏内；普通对齐时忽略自定义坐标。长文本自"
              "动省略。");
    auto main = section(text, "主标题", 1);
    line(main, "主标题", &o.brand, "mainTitle");
    spin(main, "主标题字号", &o.brandSize, 20, 180);
    fontControl(main, &o.textStyles[0]);
    auto note = section(text, "左侧备注", 2);
    line(note, "左侧备注", &o.note);
    fontControl(note, &o.textStyles[1]);
    auto title = section(text, "胶卷标题", 3);
    line(title, "胶卷标题", &o.title, "titleText");
    spin(title, "标题字号", &o.titleSize, 20, 120);
    fontControl(title, &o.textStyles[2]);
    auto film = section(text, "胶片 / ISO", 4);
    line(film, "胶片型号", &o.film);
    line(film, "ISO", &o.iso);
    fontControl(film, &o.textStyles[3]);
    auto place = section(text, "地点 / 日期", 5);
    line(place, "拍摄地点", &o.location);
    line(place, "拍摄日期", &o.date);
    fontControl(place, &o.textStyles[4]);
    auto camera = section(text, "相机 / 镜头", 6);
    line(camera, "相机", &o.camera);
    line(camera, "镜头", &o.lens);
    fontControl(camera, &o.textStyles[5]);
    spin(text, "信息字号", &o.infoSize, 16, 80, "infoSize")->setProperty("focusRegion", 4);
    tip(text,
        "字号和间距按模板原始像素计算。信息字号用于备注及胶卷详情；标题栏较矮时自动缩小文字。字体文件随预设保存。");

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
        changed(7);
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
        QPushButton#exportButton:hover{background:#a0efd0;} QLineEdit,QSpinBox,QDoubleSpinBox,QComboBox{background:#192126;border:1px solid #45545d;border-radius:4px;padding:6px;selection-background-color:#327f6b;}
        QLineEdit:focus,QSpinBox:focus,QComboBox:focus{border:1px solid #80dfba;}
        QListWidget{background:#1b2227;border:1px solid #34424b;border-radius:6px;outline:0;}
        QListWidget::item{padding:5px;border-radius:4px;} QListWidget::item:selected{background:#32564f;color:#edfff8;}
        QTabWidget::pane{border:1px solid #34424b;border-radius:5px;} QTabBar::tab{background:#29343b;padding:10px 18px;}
        QTabBar::tab:selected{color:#9aeacb;border-bottom:2px solid #80dfba;background:#35443f;}
        QCheckBox{spacing:8px;} QCheckBox::indicator{width:16px;height:16px;}
        QSlider::groove:horizontal{background:#45545d;height:4px;border-radius:2px;} QSlider::handle:horizontal{background:#80dfba;width:14px;margin:-5px 0;border-radius:7px;}
        QScrollBar:vertical{background:#22292e;width:10px;} QScrollBar::handle:vertical{background:#53636e;min-height:20px;border-radius:4px;}
        QScrollBar::add-line:vertical,QScrollBar::sub-line:vertical{height:0;} QStatusBar{color:#a8b9c2;}
        QFrame#settingsSection{background:#2b353d;border:1px solid #4b606b;border-radius:7px;}
        QWidget#sectionBody{background:#29343b;border-bottom-left-radius:6px;border-bottom-right-radius:6px;}
        QWidget#sectionBody QLabel,QWidget#sectionBody QCheckBox{background:transparent;}
        QToolButton#sectionToggle{background:#354650;color:#e6efef;border:0;border-left:3px solid #739b91;border-radius:5px;padding:10px 9px;text-align:left;font-weight:600;}
        QToolButton#sectionToggle:hover{background:#425963;}
        QToolButton#sectionToggle:checked{background:#35534e;color:#b8f4d9;border-left:3px solid #80dfba;}
        QToolButton#sectionToggle:focus{border:1px solid #80dfba;}
        QGroupBox{border:1px solid #43515a;border-radius:5px;margin-top:12px;padding-top:12px;}
        QGroupBox::title{subcontrol-origin:margin;left:8px;color:#9aeacb;}
        QProgressBar{border:1px solid #43515a;border-radius:3px;text-align:center;min-height:16px;}
        QProgressBar::chunk{background:#327f6b;}
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
void Window::changed(int focus) {
    if (focus < 0) {
        auto object = sender();
        focus = object ? object->property("focusRegion").toInt() : 0;
    }
    pendingFocus = focus;
    if (syncing)
        return;
    dirty = true;
    ++revision;
    updateTitle();
    count->setText(QString("照片  %1 / 42").arg(doc.photos.size()));
    if (previewControl)
        previewControl->canceled.store(true);
    retryButton->setEnabled(!doc.photos.isEmpty());
    if (doc.photos.isEmpty()) {
        previewError.clear();
        failureButton->setEnabled(false);
        previewInfo->setToolTip({});
        previewProgress->setToolTip({});
        previewProgress->setRange(0, 100);
        previewProgress->setValue(0);
        previewProgress->setFormat("等待添加照片");
        preview->clearImage();
        previewInfo->setText("添加照片以显示实时预览");
    } else {
        previewProgress->setFormat("等待更新…");
    }
    cropX->setEnabled(photos->currentItem() && !doc.options.fit);
    cropY->setEnabled(photos->currentItem() && !doc.options.fit);
    debounce.start();
}
void Window::updateTitle() {
    setWindowTitle(
        QString("%1%2 — Film Index Studio")
            .arg(projectPath.isEmpty() ? "当前索引图" : QFileInfo(projectPath).fileName(), dirty ? " *" : ""));
}
void Window::startPreview() {
    // A finished worker still has a queued finished signal. Do not replace its future before consuming it.
    if (previewBusy || doc.photos.isEmpty())
        return;
    previewBusy = true;
    renderingRevision = revision;
    renderElapsed.start();
    if (retryFresh) {
        clearPreviewCache();
        retryFresh = false;
    }
    previewControl = std::make_shared<RenderControl>();
    Document snapshot = doc;
    auto control = previewControl;
    previewWatcher.setFuture(QtConcurrent::run(
        [snapshot, control] { return renderSheet(snapshot, snapshot.options.previewWidth, control); }));
}
void Window::retryPreview() {
    if (doc.photos.isEmpty())
        return;
    ++revision;
    pendingFocus = 0;
    retryFresh = true;
    if (previewControl)
        previewControl->canceled.store(true);
    previewProgress->setFormat("等待重新生成…");
    // Null thumbnail entries are not permanent failures: source files may have become readable again.
    for (auto it = thumbnails.begin(); it != thumbnails.end();) {
        if (it.value().isNull())
            it = thumbnails.erase(it);
        else
            ++it;
    }
    startThumbnails();
    debounce.start(0);
}
void Window::previewFailed(const QString &error) {
    previewError = error;
    preview->markStale();
    previewInfo->setText("预览失败 · " + error.section('\n', 0, 0).section(QChar(0xff1a), 0, 0));
    previewInfo->setToolTip(error);
    previewProgress->setRange(0, 100);
    previewProgress->setValue(0);
    previewProgress->setFormat("生成失败 · 点击失败详情查看原因");
    previewProgress->setToolTip(error);
    failureButton->setEnabled(true);
    statusBar()->showMessage(preview->scene()->items().isEmpty()
                                 ? "预览失败。查看失败详情后可重新生成。"
                                 : "预览失败，已保留上次成功画面。查看失败详情后可重新生成。");
    const QString record =
        QString("[%1] Film Index Studio %2 | preview=%3 | photos=%4\n%5\n\n")
            .arg(QDateTime::currentDateTime().toString(Qt::ISODate), QCoreApplication::applicationVersion())
            .arg(doc.options.previewWidth)
            .arg(doc.photos.size())
            .arg(error);
    previewLogPath = "日志写入失败：请复制上方错误信息。";
    for (const auto &folder : {QFileInfo(presetFolder).absolutePath() + "/logs",
                               QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/logs"}) {
        if (!QDir().mkpath(folder))
            continue;
        QFile log(folder + "/preview-errors.log");
        if (!log.open(QIODevice::WriteOnly | (log.size() > 1024 * 1024 ? QIODevice::Truncate : QIODevice::Append)))
            continue;
        const auto bytes = record.toUtf8();
        if (log.write(bytes) == bytes.size() && log.flush()) {
            previewLogPath = log.fileName();
            break;
        }
    }
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
            new QListWidgetItem(QIcon(QPixmap::fromImage(thumbnails.value(p.path))),
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
    startThumbnails();
}
void Window::startThumbnails() {
    if (thumbnailBusy)
        return;
    QStringList paths;
    for (const auto &p : doc.photos)
        if (!thumbnails.contains(p.path))
            paths << p.path;
    paths.removeDuplicates();
    if (paths.isEmpty())
        return;
    thumbnailBusy = true;
    thumbnailControl = std::make_shared<RenderControl>();
    auto control = thumbnailControl;
    thumbnailProgress->setRange(0, paths.size());
    thumbnailProgress->setValue(0);
    thumbnailProgress->show();
    thumbnailWatcher.setFuture(QtConcurrent::run([paths, control] {
        QVector<QPair<QString, QImage>> results;
        for (const auto &path : paths) {
            if (control->canceled.load())
                break;
            results.append({path, thumbnail(path)});
            control->progress.fetch_add(1);
        }
        return results;
    }));
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
    changed(7);
}
void Window::report(const QString &message) {
    statusBar()->showMessage(message);
    if (smokeDir.isEmpty())
        QMessageBox::warning(this, "Film Index Studio", message);
}
bool Window::confirmDiscard() {
    return true;
}
void Window::refreshStartupPresets() {
    QSignalBlocker blocker(startupPresets);
    QSettings settings(presetFolder + "/startup.ini", QSettings::IniFormat);
    const QString selected = settings.value("startup/preset").toString();
    startupPresets->clear();
    startupPresets->addItem("内置默认值", QString());
    for (const auto &name : QDir(presetFolder).entryList({"*.fipreset"}, QDir::Files, QDir::Name))
        startupPresets->addItem(QFileInfo(name).completeBaseName(), name);
    int index = startupPresets->findData(selected);
    if (index < 0 && !selected.isEmpty()) {
        startupPresets->addItem("不可用：" + QFileInfo(selected).completeBaseName(), selected);
        index = startupPresets->count() - 1;
    }
    startupPresets->setCurrentIndex(qMax(0, index));
}
bool Window::saveProject(bool) {
    const QString folder = presetFolder;
    if (!QDir().mkpath(folder)) {
        report("无法写入软件目录，请移动软件到可写目录。");
        return false;
    }
    bool ok = false;
    QString name =
        QInputDialog::getText(this, "保存预设", "预设名称", QLineEdit::Normal,
                              projectPath.isEmpty() ? "我的预设" : QFileInfo(projectPath).completeBaseName(), &ok)
            .trimmed();
    if (!ok || name.isEmpty())
        return false;
    if (name.contains(QRegularExpression("[<>:\"/\\\\|?*]")) || name.endsWith('.') || name == "..") {
        report("名称不能包含路径或文件名特殊字符。");
        return false;
    }
    const QString path = folder + '/' + name + ".fipreset";
    if (QFileInfo::exists(path) &&
        QMessageBox::question(this, "覆盖预设？", "同名预设已存在，是否替换？", QMessageBox::Yes | QMessageBox::No,
                              QMessageBox::No) != QMessageBox::Yes)
        return false;
    QString error;
    if (!savePreset(doc.options, path, &error)) {
        report(error);
        return false;
    }
    projectPath = path;
    dirty = false;
    updateTitle();
    refreshStartupPresets();
    statusBar()->showMessage("预设已保存到软件目录，包含全部设置及引用素材，不包含照片列表。");
    return true;
}
void Window::openProject(const QString &given) {
    if (exporting)
        return;
    QString path = given.isEmpty()
                       ? QFileDialog::getOpenFileName(this, "打开预设", presetFolder, "Film Index 预设 (*.fipreset)")
                       : given;
    if (path.isEmpty())
        return;
    Options next;
    QString error;
    if (!loadPreset(path, &next, &error)) {
        report(error);
        return;
    }
    doc.options = next;
    projectPath = path;
    syncControls();
    changed(0);
    dirty = false;
    updateTitle();
    statusBar()->showMessage("预设已应用，当前照片及照片调整保持不变。");
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
    QStringList sources{doc.options.headerImage.path, doc.options.indexImage.path, doc.options.logo.path,
                        doc.options.watermark.path};
    for (int row = 0; row < 7; ++row)
        sources << templateFile(doc.options, row);
    for (const auto &p : doc.photos)
        sources << p.path;
    for (const auto &source : sources) {
        if (source.isEmpty() || source.startsWith(":/"))
            continue;
        if (QFileInfo(source).absoluteFilePath().compare(target, Qt::CaseInsensitive) == 0 ||
            (!QFileInfo(path).canonicalFilePath().isEmpty() &&
             QFileInfo(source).canonicalFilePath().compare(QFileInfo(path).canonicalFilePath(), Qt::CaseInsensitive) ==
                 0)) {
            report("导出路径与原照片或正在使用的素材相同，请选择其他文件名。");
            return;
        }
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
    if (paths.size() == 1 && paths[0].endsWith(".fipreset", Qt::CaseInsensitive))
        openProject(paths[0]);
    else
        addFiles(paths);
    e->acceptProposedAction();
}
void Window::startSmokeTest(const QString &directory) {
    smokeDir = QFileInfo(directory).absoluteFilePath();
    QDir().mkpath(smokeDir + "/samples");
    doc.options.title = "城市漫游 · 秋日光影";
    doc.options.brand = "Film Index";
    doc.options.headerAlignment = 1;
    doc.options.note = "2026 / CONTACT SHEET";
    doc.options.film = "KODAK GOLD 200";
    doc.options.iso = "200";
    doc.options.location = "SHANGHAI";
    doc.options.date = "2026.10";
    doc.options.camera = "Nikon F3";
    doc.options.lens = "50mm f/1.4";
    doc.options.filmTemplate = "kodak-gc-400";
    doc.options.rightAlignment = 2;
    doc.options.logo.enabled = true;
    doc.options.logo.size = 240;
    doc.options.watermark.enabled = true;
    doc.options.watermark.text = "FILM INDEX / 2026";
    doc.options.watermark.x = 50;
    doc.options.watermark.y = 5;
    doc.options.watermark.style.color = QColor("#ffffff");
    doc.options.textStyles[1].color = QColor("#80dfba");
    QImage backdrop(1200, 800, QImage::Format_RGB32);
    {
        QPainter p(&backdrop);
        QLinearGradient gradient(0, 0, 1200, 800);
        gradient.setColorAt(0, QColor("#1f685d"));
        gradient.setColorAt(1, QColor("#c1935e"));
        p.fillRect(backdrop.rect(), gradient);
    }
    const QString backgroundPath = smokeDir + "/background.png";
    backdrop.save(backgroundPath);
    doc.options.headerImage = {backgroundPath, 25, 50, 50};
    doc.options.indexImage = {backgroundPath, 12, 50, 50};
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
    Options restored;
    if (!savePreset(doc.options, smokeDir + "/示例预设.fipreset", &error) ||
        !loadPreset(smokeDir + "/示例预设.fipreset", &restored, &error)) {
        QFile result(smokeDir + "/gui-smoke-result.txt");
        if (result.open(QIODevice::WriteOnly))
            result.write(error.toUtf8());
        QTimer::singleShot(0, qApp, [] { qApp->exit(1); });
        return;
    }
    QTimer::singleShot(60000, this, [this] {
        if (exporting && exportControl)
            exportControl->canceled.store(true);
        qApp->exit(2);
    });
}
