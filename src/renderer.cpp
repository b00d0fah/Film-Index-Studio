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

// Immutable decoded previews are shared across edits; metadata invalidates changed sources.
static QImage readPhoto(QImageReader &reader, int edge, bool cachePreview) {
    static QCache<QString, QImage> cache(128 * 1024);
    static QMutex mutex;
    const QFileInfo info(reader.fileName());
    const QString key = info.absoluteFilePath() + '|' + QString::number(info.lastModified().toMSecsSinceEpoch()) + '|' +
                        QString::number(info.size()) + '|' + QString::number(edge);
    if (cachePreview) {
        QMutexLocker lock(&mutex);
        if (auto image = cache.object(key))
            return *image;
    }
    const QSize input = reader.size();
    if (input.isValid() && qMax(input.width(), input.height()) > edge)
        reader.setScaledSize(input.scaled(edge, edge, Qt::KeepAspectRatio));
    QImage image = reader.read();
    if (!image.isNull() && image.colorSpace().isValid())
        image.convertToColorSpace(QColorSpace::SRgb);
    if (cachePreview && !image.isNull()) {
        QMutexLocker lock(&mutex);
        cache.insert(key, new QImage(image), int(image.sizeInBytes() / 1024) + 1);
    }
    return image;
}

static constexpr int stripWidth = 4502, stripHeight = 700;
QSize baseSheetSize(const Document &d) {
    const auto &o = d.options;
    int rows = (d.photos.size() + 5) / 6;
    return {stripWidth + 2 * o.border,
            (o.header ? o.headerHeight + o.gap : o.border) + rows * stripHeight + qMax(0, rows - 1) * o.gap + o.border};
}
QRectF photoRect(const Document &d, int i) {
    const auto &o = d.options;
    return {double(o.border + 26 + (i % 6) * 746),
            double((o.header ? o.headerHeight + o.gap : o.border) + (i / 6) * (700 + o.gap) + 110), 720, 480};
}
QSize outputSize(const Document &d, int width) {
    const QSize size = baseSheetSize(d);
    if (!width)
        width = d.options.outputWidth;
    return {width, qRound(double(size.height()) * width / size.width())};
}
static void textLine(QPainter &p, const QRectF &rect, const QString &s, int size, const QColor &color,
                     bool bold = false) {
    QFont f("Microsoft YaHei UI");
    f.setPixelSize(size);
    f.setBold(bold);
    p.setFont(f);
    p.setPen(color);
    p.drawText(rect, Qt::AlignLeft | Qt::AlignVCenter, QFontMetricsF(f).elidedText(s, Qt::ElideRight, rect.width()));
}
static void header(QPainter &p, const Options &o, int width) {
    p.fillRect(QRect(0, 0, width, o.headerHeight), o.headerColor);
    p.fillRect(QRect(0, o.headerHeight - 12, width, 5), o.textColor);
    double leftWidth = o.details ? width * .50 - 144 : width - 144;
    double noteHeight = o.note.isEmpty() ? 0 : o.infoSize * 1.7;
    double brandHeight = o.brand.isEmpty() ? 0 : o.brandSize * 1.6;
    double h = brandHeight + noteHeight;
    double compression = qMin(1.0, (o.headerHeight - 40.0) / qMax(1.0, h));
    p.save();
    p.setClipRect(0, 0, width, o.headerHeight - 12);
    double y = (o.headerHeight - h * compression) / 2;
    textLine(p, {72, y, leftWidth, brandHeight * compression}, o.brand, qMax(1, qRound(o.brandSize * compression)),
             o.textColor, true);
    textLine(p, {72, y + brandHeight * compression, leftWidth, noteHeight * compression}, o.note,
             qMax(1, qRound(o.infoSize * compression)), o.textColor);
    if (o.details) {
        QStringList lines;
        QVector<int> sizes;
        auto add = [&](const QString &s, int fs) {
            if (!s.trimmed().isEmpty()) {
                lines << s;
                sizes << fs;
            }
        };
        auto joined = [](QStringList values) {
            values.removeAll("");
            return values.join("  ·  ");
        };
        add(o.title, o.titleSize);
        add(joined({o.film, o.iso.isEmpty() ? QString() : "ISO " + o.iso}), o.infoSize);
        add(joined({o.location, o.date}), o.infoSize);
        add(joined({o.camera, o.lens}), o.infoSize);
        double total = 0;
        for (int fs : sizes)
            total += fs * 1.7;
        double factor = qMin(1.0, (o.headerHeight - 40.0) / qMax(1.0, total));
        double yy = (o.headerHeight - total * factor) / 2;
        for (int i = 0; i < lines.size(); ++i) {
            double lineH = sizes[i] * 1.7 * factor;
            textLine(p, {width * .53, yy, width * .47 - 72, lineH}, lines[i], qMax(1, qRound(sizes[i] * factor)),
                     o.textColor, i == 0 && !o.title.isEmpty());
            yy += lineH;
        }
    }
    p.restore();
}
RenderResult renderSheet(const Document &d, int width, const std::shared_ptr<RenderControl> &control) {
    RenderResult r;
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
    if (o.header)
        header(painter, o, baseSheetSize(d).width());
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
        const QString asset =
            QString(":/assets/film_strips/135/%1/%2-%3.png").arg(o.filmTemplate).arg(row * 6 + 1).arg(row * 6 + 6);
        QImage strip(asset);
        if (strip.isNull() || strip.size() != QSize(stripWidth, stripHeight)) {
            r.error = "胶片模板损坏：" + asset;
            return r;
        }
        int sy = (o.header ? o.headerHeight + o.gap : o.border) + row * (700 + o.gap);
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
            QImage img = readPhoto(reader, decodeEdge, width > 0 && width <= 1600);
            if (img.isNull()) {
                r.error = QString("第 %1 张照片无法读取：%2\n%3").arg(i + 1).arg(photo.path, reader.errorString());
                return r;
            }
            if (o.autoRotate && img.height() > img.width())
                img = img.transformed(QTransform().rotate(90));
            if (photo.rotation)
                img = img.transformed(QTransform().rotate(photo.rotation));
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
    painter.end();
    if (canceled()) {
        r.canceled = true;
        return r;
    }
    r.image = canvas;
    return r;
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
