#include "document.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <cmath>

QStringList templateNames() {
    return QDir(":/assets/film_strips/135").entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
}
QString validateDocument(const Document &d, bool requirePhotos) {
    const auto &o = d.options;
    if (requirePhotos && d.photos.isEmpty())
        return "请先添加照片。";
    if (d.photos.size() > 42)
        return "135 模板单张索引图最多支持 42 张照片。";
    if (!templateNames().contains(o.filmTemplate))
        return "胶片模板不存在。";
    if (o.headerAlignment < 0 || o.headerAlignment > 2 || o.headerHeight < 180 || o.headerHeight > 1000 ||
        o.border < 0 || o.border > 200 || o.gap < 0 || o.gap > 200 || o.outputWidth < 600 || o.outputWidth > 9000 ||
        o.quality < 1 || o.quality > 100 || o.brandSize < 20 || o.brandSize > 180 || o.titleSize < 20 ||
        o.titleSize > 120 || o.infoSize < 16 || o.infoSize > 80)
        return "排版参数超出支持范围。";
    if (!o.background.isValid() || !o.headerColor.isValid() || !o.textColor.isValid() || !o.matColor.isValid())
        return "颜色值无效。";
    for (const auto &p : d.photos) {
        if (p.path.isEmpty() || p.rotation < 0 || p.rotation > 270 || p.rotation % 90 != 0 || !std::isfinite(p.cropX) ||
            !std::isfinite(p.cropY) || std::abs(p.cropX) > 1 || std::abs(p.cropY) > 1)
            return "照片旋转或裁切参数无效。";
    }
    return {};
}
bool saveDocument(const Document &d, const QString &path, QString *error) {
    *error = validateDocument(d, false);
    if (!error->isEmpty())
        return false;
    const auto &o = d.options;
    QJsonObject opts;
#define SAVE_FIELD(f) opts[#f] = o.f
    SAVE_FIELD(filmTemplate);
    SAVE_FIELD(header);
    SAVE_FIELD(details);
    SAVE_FIELD(autoRotate);
    SAVE_FIELD(fit);
    SAVE_FIELD(headerAlignment);
    SAVE_FIELD(headerHeight);
    SAVE_FIELD(border);
    SAVE_FIELD(gap);
    SAVE_FIELD(outputWidth);
    SAVE_FIELD(quality);
    SAVE_FIELD(brand);
    SAVE_FIELD(title);
    SAVE_FIELD(note);
    SAVE_FIELD(film);
    SAVE_FIELD(iso);
    SAVE_FIELD(location);
    SAVE_FIELD(date);
    SAVE_FIELD(camera);
    SAVE_FIELD(lens);
    SAVE_FIELD(brandSize);
    SAVE_FIELD(titleSize);
    SAVE_FIELD(infoSize);
#undef SAVE_FIELD
    opts["background"] = o.background.name();
    opts["headerColor"] = o.headerColor.name();
    opts["textColor"] = o.textColor.name();
    opts["matColor"] = o.matColor.name();
    QJsonArray photos;
    const QDir base = QFileInfo(path).absoluteDir();
    for (const auto &p : d.photos)
        photos.append(QJsonObject{
            {"path", base.relativeFilePath(p.path)}, {"rotation", p.rotation}, {"cropX", p.cropX}, {"cropY", p.cropY}});
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        *error = file.errorString();
        return false;
    }
    QByteArray data =
        QJsonDocument(QJsonObject{{"format", "FilmIndexStudio"}, {"version", 1}, {"options", opts}, {"photos", photos}})
            .toJson();
    if (file.write(data) != data.size() || !file.commit()) {
        *error = file.errorString();
        return false;
    }
    return true;
}
bool loadDocument(const QString &path, Document *doc, QString *error) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        *error = f.errorString();
        return false;
    }
    if (f.size() > 1024 * 1024) {
        *error = "项目文件过大。";
        return false;
    }
    QJsonParseError pe;
    const auto root = QJsonDocument::fromJson(f.readAll(), &pe).object();
    if (pe.error != QJsonParseError::NoError || root["format"] != "FilmIndexStudio" || root["version"].toInt() != 1 ||
        !root["options"].isObject() || !root["photos"].isArray()) {
        *error = "不是有效的 Film Index Studio 项目。";
        return false;
    }
    Document result;
    auto &o = result.options;
    const auto opts = root["options"].toObject();
#define READ_STRING(f)                                                                                                 \
    if (opts.contains(#f))                                                                                             \
    o.f = opts[#f].toString(o.f)
#define READ_INT(f)                                                                                                    \
    if (opts.contains(#f))                                                                                             \
    o.f = opts[#f].toInt(o.f)
#define READ_BOOL(f)                                                                                                   \
    if (opts.contains(#f))                                                                                             \
    o.f = opts[#f].toBool(o.f)
    READ_STRING(filmTemplate);
    READ_STRING(brand);
    READ_STRING(title);
    READ_STRING(note);
    READ_STRING(film);
    READ_STRING(iso);
    READ_STRING(location);
    READ_STRING(date);
    READ_STRING(camera);
    READ_STRING(lens);
    READ_INT(headerHeight);
    READ_INT(border);
    READ_INT(gap);
    READ_INT(outputWidth);
    READ_INT(quality);
    READ_INT(brandSize);
    READ_INT(titleSize);
    READ_INT(infoSize);
    READ_INT(headerAlignment);
    READ_BOOL(header);
    READ_BOOL(details);
    READ_BOOL(autoRotate);
    READ_BOOL(fit);
#undef READ_STRING
#undef READ_INT
#undef READ_BOOL
    for (auto pair :
         {qMakePair(QString("background"), &o.background), qMakePair(QString("headerColor"), &o.headerColor),
          qMakePair(QString("textColor"), &o.textColor), qMakePair(QString("matColor"), &o.matColor)})
        if (opts.contains(pair.first))
            *pair.second = QColor(opts[pair.first].toString());
    const QDir base = QFileInfo(path).absoluteDir();
    for (const auto &v : root["photos"].toArray()) {
        if (!v.isObject() || v.toObject()["path"].toString().isEmpty()) {
            *error = "项目中存在无效照片记录。";
            return false;
        }
        const auto p = v.toObject();
        result.photos.append({QDir::cleanPath(base.absoluteFilePath(p["path"].toString())), p["rotation"].toInt(),
                              p["cropX"].toDouble(), p["cropY"].toDouble()});
    }
    *error = validateDocument(result, false);
    if (!error->isEmpty())
        return false;
    *doc = result;
    return true;
}
