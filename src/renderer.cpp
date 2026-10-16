#include "renderer.h"
#include <QCache>
#include <QColorSpace>
#include <QDateTime>
#include <QFileInfo>
#include <QFontMetricsF>
#include <QImageReader>
#include <QImageWriter>
#include <QMutex>
#include <QPainter>
#include <QSaveFile>
#include <QtMath>
#include <exception>
#include <new>

struct PreviewCache {
    QCache<QString, QImage> images{128 * 1024};
    QMutex mutex;
};
static PreviewCache &previewCache() {
    static PreviewCache cache;
    return cache;
}
void clearPreviewCache() {
    auto &cache = previewCache();
    QMutexLocker lock(&cache.mutex);
    cache.images.clear();
}

// Immutable decoded previews are shared across edits; metadata invalidates changed sources.
static QImage readPhoto(QImageReader &reader, int edge, bool cachePreview) {
    auto &cache = previewCache();
    const QFileInfo info(reader.fileName());
    const QString key = info.absoluteFilePath() + '|' + QString::number(info.lastModified().toMSecsSinceEpoch()) + '|' +
                        QString::number(info.size()) + '|' + QString::number(edge);
    if (cachePreview && info.exists()) {
        QMutexLocker lock(&cache.mutex);
        if (auto image = cache.images.object(key))
            return *image;
    }
    const QSize input = reader.size();
    if (input.isValid() && qMax(input.width(), input.height()) > edge)
        reader.setScaledSize(input.scaled(edge, edge, Qt::KeepAspectRatio));
    QImage image = reader.read();
    if (!image.isNull() && image.colorSpace().isValid())
        image.convertToColorSpace(QColorSpace::SRgb);
    if (cachePreview && !image.isNull()) {
        QMutexLocker lock(&cache.mutex);
        cache.images.insert(key, new QImage(image), int(image.sizeInBytes() / 1024) + 1);
    }
    return image;
}

static constexpr int stripWidth = 4502, stripHeight = 700;
QSize baseSheetSize(const Document &d) {
    const auto &o = d.options;
    int rows = (d.photos.size() + 5) / 6;
    return {stripWidth + 2 * o.border, (o.header ? o.headerHeight + o.headerGap : o.border) + rows * stripHeight +
                                           qMax(0, rows - 1) * o.gap + o.border};
}
QRectF photoRect(const Document &d, int i) {
    const auto &o = d.options;
    return {double(o.border + 26 + (i % 6) * 746),
            double((o.header ? o.headerHeight + o.headerGap : o.border) + (i / 6) * (700 + o.gap) + 110), 720, 480};
}
QSize outputSize(const Document &d, int width) {
    const QSize size = baseSheetSize(d);
    if (!width)
        width = d.options.outputWidth;
    return {width, qRound(double(size.height()) * width / size.width())};
}
qreal alignedTextX(const QRectF &column, qreal textWidth, int alignment) {
    const qreal available = qMax<qreal>(0, column.width() - textWidth);
    if (alignment == 1)
        return column.x() + available / 2;
    if (alignment == 2)
        return column.x() + available;
    return column.x();
}

struct HeaderLine {
    QString text;
    QRectF rect;
    QFont font;
    QColor color;
    int block;
};
static QVector<HeaderLine> headerLines(const Options &o, int width) {
    QVector<HeaderLine> result;
    auto joined = [](QStringList v) {
        v.removeAll("");
        return v.join("  ·  ");
    };
    const QStringList values{o.brand,
                             o.note,
                             o.title,
                             joined({o.film, o.iso.isEmpty() ? QString() : "ISO " + o.iso}),
                             joined({o.location, o.date}),
                             joined({o.camera, o.lens})};
    const int sizes[]{o.brandSize, o.infoSize, o.titleSize, o.infoSize, o.infoSize, o.infoSize};
    for (int side = 0; side < (o.details ? 2 : 1); ++side) {
        const QRectF column = side == 0
                                  ? QRectF(72, 20, o.details ? width * .50 - 144 : width - 144, o.headerHeight - 40)
                                  : QRectF(width * .53, 20, width * .47 - 72, o.headerHeight - 40);
        const int begin = side == 0 ? 0 : 2, end = side == 0 ? 2 : 6;
        const int align = side == 0 ? o.headerAlignment : o.rightAlignment;
        double height = 0;
        for (int i = begin; i < end; ++i)
            if (!values[i].isEmpty())
                height += sizes[i] * 1.7;
        const double compression = qMin(1.0, column.height() / qMax(1.0, height));
        height *= compression;
        double y = column.top() + (align == 3 ? qMin(column.height() - height, double(side == 0 ? o.leftY : o.rightY))
                                              : (column.height() - height) * .5);
        // A common left edge in custom mode keeps each block intact. Its widest line sets the travel range.
        double blockWidth = 0;
        for (int i = begin; i < end; ++i) {
            QFont f(o.textStyles[i].family);
            f.setPixelSize(qMax(1, qRound(sizes[i] * compression)));
            f.setBold(i == 0 || i == 2);
            blockWidth = qMax(blockWidth, QFontMetricsF(f).horizontalAdvance(values[i]));
        }
        blockWidth = qMin(blockWidth, column.width());
        const double customX =
            column.left() + qMin(column.width() - blockWidth, double(side == 0 ? o.leftX : o.rightX));
        for (int i = begin; i < end; ++i) {
            if (values[i].isEmpty())
                continue;
            QFont f(o.textStyles[i].family);
            f.setPixelSize(qMax(1, qRound(sizes[i] * compression)));
            f.setBold(i == 0 || i == 2);
            QFontMetricsF metrics(f);
            const QString visible = metrics.elidedText(values[i], Qt::ElideRight, column.width());
            double tw = qMin(column.width(), metrics.horizontalAdvance(visible));
            double x = align == 3 ? customX : alignedTextX(column, tw, align);
            const double lineHeight = sizes[i] * 1.7 * compression;
            result.append({visible, {x, y, tw, lineHeight}, f, o.textStyles[i].color, i});
            y += lineHeight;
        }
    }
    return result;
}
QRectF headerBlockRect(const Options &o, int width, int block) {
    for (const auto &line : headerLines(o, width))
        if (line.block == block)
            return line.rect;
    return block < 2 ? QRectF(72, 20, width * .45, o.headerHeight - 40)
                     : QRectF(width * .53, 20, width * .45, o.headerHeight - 40);
}
static QString backgroundImage(QPainter &p, const QRectF &dest, const ImageLayer &layer, double scale) {
    if (layer.path.isEmpty() || layer.opacity == 0)
        return {};
    QImageReader reader(layer.path);
    reader.setAutoTransform(true);
    QImage image = readPhoto(reader, qMax(512, qCeil(qMax(dest.width(), dest.height()) * scale)), true);
    if (image.isNull())
        return "背景图片无法读取：" + layer.path + "\n" + reader.errorString();
    double w = image.width(), h = image.height();
    const double aspect = dest.width() / dest.height();
    if (w / h > aspect)
        w = h * aspect;
    else
        h = w / aspect;
    QRectF source((image.width() - w) * layer.cropX / 100.0, (image.height() - h) * layer.cropY / 100.0, w, h);
    p.save();
    p.setClipRect(dest);
    p.setOpacity(layer.opacity / 100.0);
    p.drawImage(dest, image, source);
    p.restore();
    return {};
}
static void header(QPainter &p, const Options &o, int width) {
    for (const auto &line : headerLines(o, width)) {
        p.save();
        // Clip each column, including glyph overhang from italic/custom fonts.
        p.setClipRect(line.block < 2 ? QRectF(72, 0, o.details ? width * .50 - 144 : width - 144, o.headerHeight)
                                     : QRectF(width * .53, 0, width * .47 - 72, o.headerHeight));
        p.setFont(line.font);
        p.setPen(line.color);
        p.drawText(line.rect, Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine, line.text);
        p.restore();
    }
}
QRectF overlayRect(const Overlay &o, const QRectF &area, bool logo) {
    QSizeF size;
    if (!logo && o.type == 0) {
        QFont f(o.style.family);
        f.setPixelSize(o.size);
        const QFontMetricsF metrics(f);
        size = {qMax(1.0, metrics.horizontalAdvance(o.text)), metrics.height()};
    } else {
        QImageReader r(o.path.isEmpty() && logo ? ":/assets/logo.png" : o.path);
        size = r.size();
        if (size.isEmpty())
            size = {1, 1};
        size.scale(o.size, o.size, Qt::KeepAspectRatio);
    }
    if (size.width() > area.width() || size.height() > area.height())
        size.scale(area.size(), Qt::KeepAspectRatio);
    return {area.left() + (area.width() - size.width()) * o.x / 100.0,
            area.top() + (area.height() - size.height()) * o.y / 100.0, size.width(), size.height()};
}
static QString drawOverlay(QPainter &p, const Overlay &o, const QRectF &area, bool logo) {
    if (!o.enabled || o.opacity == 0)
        return {};
    const QRectF rect = overlayRect(o, area, logo);
    p.save();
    p.setClipRect(area);
    p.setOpacity(o.opacity / 100.0);
    QString error;
    if (!logo && o.type == 0) {
        QFont f(o.style.family);
        f.setPixelSize(o.size);
        QFontMetricsF metrics(f);
        double scale =
            qMin(rect.width() / qMax(1.0, metrics.horizontalAdvance(o.text)), rect.height() / metrics.height());
        f.setPixelSize(qMax(1, qFloor(o.size * scale)));
        p.setFont(f);
        p.setPen(o.style.color);
        p.drawText(rect, Qt::AlignCenter | Qt::TextSingleLine, o.text);
    } else {
        QImageReader reader(o.path.isEmpty() && logo ? ":/assets/logo.png" : o.path);
        reader.setAutoTransform(true);
        QImage img = readPhoto(reader, 2048, true);
        if (img.isNull())
            error = "Logo / 水印图片无法读取：" + reader.fileName() + "\n" + reader.errorString();
        else
            p.drawImage(rect, img);
    }
    p.restore();
    return error;
}
static RenderResult renderSheetImpl(const Document &d, int width, const std::shared_ptr<RenderControl> &control) {
    RenderResult r;
    if (control && control->canceled.load()) {
        r.canceled = true;
        return r;
    }
    r.error = validateDocument(d);
    if (!r.error.isEmpty())
        return r;
    QSize size = outputSize(d, width);
    if (size.width() < 1 || size.height() < 1 || qint64(size.width()) * size.height() > 100000000) {
        r.error = "输出超过 1 亿像素，请减小导出宽度。";
        return r;
    }
    auto canceled = [&] { return control && control->canceled.load(); };
    QImage canvas(size, QImage::Format_RGB32);
    if (canvas.isNull()) {
        r.error = "图像内存分配失败，请降低输出宽度。";
        return r;
    }
    canvas.fill(d.options.background);
    canvas.setColorSpace(QColorSpace::SRgb);
    QPainter painter(&canvas);
    painter.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform | QPainter::TextAntialiasing);
    double scale = double(size.width()) / baseSheetSize(d).width();
    painter.scale(scale, scale);
    const auto &o = d.options;
    canvas.setDotsPerMeterX(qRound(o.dpi / 0.0254));
    canvas.setDotsPerMeterY(qRound(o.dpi / 0.0254));
    const QSize base = baseSheetSize(d);
    const double indexTop = o.header ? o.headerHeight : 0;
    r.error =
        backgroundImage(painter, {0, indexTop, double(base.width()), base.height() - indexTop}, o.indexImage, scale);
    if (!r.error.isEmpty())
        return r;
    if (o.header) {
        painter.fillRect(QRectF(0, 0, base.width(), o.headerHeight), o.headerColor);
        r.error = backgroundImage(painter, {0, 0, double(base.width()), double(o.headerHeight)}, o.headerImage, scale);
        if (!r.error.isEmpty())
            return r;
        header(painter, o, base.width());
        r.error = drawOverlay(painter, o.logo, {20, 20, double(base.width() - 40), double(o.headerHeight - 40)}, true);
        if (!r.error.isEmpty())
            return r;
    }
    const QImage sprocket(":/assets/film_strips/135/sprocket_strip.png");
    if (sprocket.isNull()) {
        r.error = "内置齿孔资源缺失。";
        return r;
    }
    for (int row = 0; row < (d.photos.size() + 5) / 6; ++row) {
        if (canceled()) {
            r.canceled = true;
            return r;
        }
        const QString asset = templateFile(o, row);
        QImageReader stripReader(asset);
        QImage strip = stripReader.read();
        if (strip.isNull() || strip.size() != QSize(stripWidth, stripHeight)) {
            r.error = "胶片模板无法读取或尺寸不是 4502 × 700：" + asset + "\n" + stripReader.errorString();
            return r;
        }
        int sy = (o.header ? o.headerHeight + o.headerGap : o.border) + row * (700 + o.gap);
        painter.drawImage(QPoint(o.border, sy), strip);
        for (int col = 0; col < 6; ++col) {
            int i = row * 6 + col;
            QRectF dest = photoRect(d, i);
            painter.fillRect(dest, o.matColor);
            if (i >= d.photos.size())
                continue;
            if (canceled()) {
                r.canceled = true;
                return r;
            }
            const auto &photo = d.photos[i];
            QImageReader reader(photo.path);
            reader.setAutoTransform(true);
            int decodeEdge = qMax(720, qCeil(1100 * scale));
            QImage img = readPhoto(reader, decodeEdge, width > 0);
            if (img.isNull()) {
                r.error = QString("第 %1 张照片无法读取：%2\n%3").arg(i + 1).arg(photo.path, reader.errorString());
                return r;
            }
            if (o.autoRotate && img.height() > img.width())
                img = img.transformed(QTransform().rotate(90));
            if (photo.rotation)
                img = img.transformed(QTransform().rotate(photo.rotation));
            if (img.isNull()) {
                r.error =
                    QString("第 %1 张照片旋转时内存不足，请降低预览分辨率后重试。\n%2").arg(i + 1).arg(photo.path);
                return r;
            }
            if (o.fit) {
                QSizeF fitted = QSizeF(img.size()).scaled(dest.size(), Qt::KeepAspectRatio);
                QRectF target(dest.center().x() - fitted.width() / 2, dest.center().y() - fitted.height() / 2,
                              fitted.width(), fitted.height());
                painter.drawImage(target, img);
            } else {
                double w = img.width(), h = img.height();
                if (w / h > 1.5)
                    w = h * 1.5;
                else
                    h = w / 1.5;
                QRectF source((img.width() - w) * (photo.cropX + 1) / 2, (img.height() - h) * (photo.cropY + 1) / 2, w,
                              h);
                painter.drawImage(dest, img, source);
            }
            if (control)
                control->progress.store(i + 1);
        }
        painter.drawImage(QPoint(o.border + 28, sy + 40), sprocket);
        painter.drawImage(QPoint(o.border + 28, sy + 600), sprocket);
    }
    r.error = drawOverlay(painter, o.watermark, {20, 20, double(base.width() - 40), double(base.height() - 40)}, false);
    if (!r.error.isEmpty())
        return r;
    painter.end();
    if (canceled()) {
        r.canceled = true;
        return r;
    }
    r.image = canvas;
    return r;
}
RenderResult renderSheet(const Document &d, int width, const std::shared_ptr<RenderControl> &control) {
    try {
        return renderSheetImpl(d, width, control);
    } catch (const std::bad_alloc &) {
        return {{}, "渲染内存不足，请降低预览分辨率或输出宽度，然后重新生成。", false};
    } catch (const std::exception &e) {
        return {{}, "渲染发生异常：" + QString::fromLocal8Bit(e.what()), false};
    } catch (...) {
        return {{}, "渲染发生未知异常，请重新生成；若仍失败，请查看诊断日志。", false};
    }
}
QString writeImage(const QImage &image, const QString &path, int quality,
                   const std::shared_ptr<RenderControl> &control) {
    if (image.isNull())
        return "没有可导出的图像。";
    QByteArray format = QFileInfo(path).suffix().toLower().toLatin1();
    if (format == "jpeg")
        format = "jpg";
    if (format != "jpg" && format != "png")
        return "请选择 .jpg 或 .png 格式。";
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return file.errorString();
    QImageWriter writer(&file, format);
    writer.setQuality(quality);
    if (format == "jpg") {
        writer.setOptimizedWrite(true);
        writer.setProgressiveScanWrite(true);
    }
    if (!writer.write(image))
        return writer.errorString();
    if (control && control->canceled.load()) {
        file.cancelWriting();
        return "导出已取消。";
    }
    if (!file.commit())
        return file.errorString();
    return {};
}
