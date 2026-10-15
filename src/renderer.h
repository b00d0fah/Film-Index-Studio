#pragma once
#include "document.h"
#include <QImage>
#include <QRectF>
#include <atomic>
#include <memory>

struct RenderControl {
    std::atomic_bool canceled{false};
    std::atomic_int progress{0};
};
struct RenderResult {
    QImage image;
    QString error;
    bool canceled = false;
};
QSize baseSheetSize(const Document &d);
QRectF photoRect(const Document &d, int index);
QSize outputSize(const Document &d, int width = 0);
RenderResult renderSheet(const Document &d, int width = 0, const std::shared_ptr<RenderControl> &control = {});
QString writeImage(const QImage &image, const QString &path, int quality, const std::shared_ptr<RenderControl> &control = {});
