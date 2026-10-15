#include "document.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QSaveFile>
#include <QTemporaryFile>
#include <QUuid>
#include <QtMath>
#include <cmath>

static QJsonObject styleJson(const TextStyle &s) {
    return {{"family", s.family}, {"fontFile", s.fontFile}, {"color", s.color.name(QColor::HexArgb)}};
}
static TextStyle readStyle(const QJsonObject &v, TextStyle s = {}) {
    s.family = v["family"].toString(s.family);
    s.fontFile = v["fontFile"].toString();
    s.color = QColor(v["color"].toString(s.color.name(QColor::HexArgb)));
    return s;
}
static QJsonObject imageJson(const ImageLayer &v) {
    return {{"path", v.path}, {"opacity", v.opacity}, {"cropX", v.cropX}, {"cropY", v.cropY}};
}
static ImageLayer readImage(const QJsonObject &v) {
    return {v["path"].toString(), v["opacity"].toInt(100), v["cropX"].toInt(50), v["cropY"].toInt(50)};
}
static QJsonObject overlayJson(const Overlay &v) {
    return {{"enabled", v.enabled},       {"x", v.x},       {"y", v.y},       {"size", v.size},
            {"opacity", v.opacity},       {"path", v.path}, {"text", v.text}, {"type", v.type},
            {"style", styleJson(v.style)}};
}
static Overlay readOverlay(const QJsonObject &v) {
    Overlay o;
    o.enabled = v["enabled"].toBool();
    o.x = v["x"].toInt(50);
    o.y = v["y"].toInt(50);
    o.size = v["size"].toInt(160);
    o.opacity = v["opacity"].toInt(100);
    o.path = v["path"].toString();
    o.text = v["text"].toString();
    o.type = v["type"].toInt();
    o.style = readStyle(v["style"].toObject());
    return o;
}
QString templatesDirectory() {
    return QCoreApplication::applicationDirPath() + "/templates";
}
QString templateFile(const Options &o, int row) {
    QString root = o.templatePath;
    if (root.isEmpty()) {
        root = templatesDirectory() + '/' + o.filmTemplate;
        if (!QFileInfo(root).isDir())
            root = ":/assets/film_strips/135/" + o.filmTemplate;
    }
    return root + QString("/%1-%2.png").arg(row * 6 + 1).arg(row * 6 + 6);
}
QString validateTemplate(const QString &directory) {
    for (int row = 0; row < 7; ++row) {
        const QString file = directory + QString("/%1-%2.png").arg(row * 6 + 1).arg(row * 6 + 6);
        QImageReader reader(file);
        if (reader.size() != QSize(4502, 700) || reader.read().isNull())
            return "模板需要 7 张 4502 × 700 PNG（1-6.png 至 37-42.png）。无效文件：" + file;
    }
    return {};
}
bool importTemplate(const QString &directory, QString *name, QString *error) {
    *error = validateTemplate(directory);
    if (!error->isEmpty())
        return false;
    *name = QFileInfo(directory).fileName();
    const QString dest = templatesDirectory() + '/' + *name;
    if (QFileInfo(dest).canonicalFilePath() == QFileInfo(directory).canonicalFilePath())
        return true;
    if (templateNames().contains(*name)) {
        *error = "已有同名模板，请先修改导入文件夹名称。";
        return false;
    }
    const QString stage = templatesDirectory() + "/.import-" + QUuid::createUuid().toString(QUuid::Id128);
    if (!QDir().mkpath(stage)) {
        *error = "无法写入软件的 templates 文件夹。";
        return false;
    }
    for (int row = 0; row < 7; ++row) {
        const QString file = QString("/%1-%2.png").arg(row * 6 + 1).arg(row * 6 + 6);
        QFile source(directory + file);
        if (!source.copy(stage + file)) {
            QDir(stage).removeRecursively();
            *error = "复制模板失败：" + source.fileName() + "\n" + source.errorString();
            return false;
        }
        QFile::setPermissions(stage + file, QFile::ReadOwner | QFile::WriteOwner | QFile::ReadGroup | QFile::ReadOther);
    }
    if (!QDir(templatesDirectory()).rename(QFileInfo(stage).fileName(), *name)) {
        *error = "保存模板失败：" + stage + " → " + dest;
        QDir(stage).removeRecursively();
        return false;
    }
    return true;
}
double widthInUnit(int pixels, int unit, int dpi) {
    return unit == 0 ? pixels : double(pixels) / dpi * (unit == 1 ? 25.4 : unit == 2 ? 2.54 : 1.0);
}
int widthInPixels(double value, int unit, int dpi) {
    return qRound(unit == 0 ? value : value * dpi / (unit == 1 ? 25.4 : unit == 2 ? 2.54 : 1.0));
}
static QHash<QString, QString> importedFamilies;
QString importedFontFile(const QString &family) {
    return importedFamilies.value(family);
}
bool importFont(TextStyle *style, const QString &path, QString *error) {
    static QHash<QString, QStringList> loaded;
    if (!QFileInfo::exists(path)) {
        *error = "字体文件不存在：" + path;
        return false;
    }
    const auto key = QFileInfo(path).absoluteFilePath();
    if (!loaded.contains(key)) {
        int id = QFontDatabase::addApplicationFont(key);
        const auto families = QFontDatabase::applicationFontFamilies(id);
        if (families.isEmpty()) {
            *error = "无法加载字体：" + path;
            return false;
        }
        loaded.insert(key, families);
    }
    // Keep an explicitly selected family from multi-family TTC files when possible.
    const auto families = loaded.value(key);
    if (!families.contains(style->family))
        style->family = families.first();
    for (const auto &family : families)
        importedFamilies.insert(family, key);
    style->fontFile = key;
    importedFamilies.insert(style->family, key);
    return true;
}
bool activateFonts(Options *o, QString *error) {
    auto activate = [&](TextStyle &s) {
        if (s.fontFile.isEmpty())
            return true;
        return importFont(&s, s.fontFile, error);
    };
    for (auto &s : o->textStyles)
        if (!activate(s))
            return false;
    return activate(o->watermark.style);
}

QStringList templateNames() {
    QStringList names = QDir(":/assets/film_strips/135").entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    names.append(QDir(templatesDirectory()).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name));
    names.removeDuplicates();
    names.sort(Qt::CaseInsensitive);
    return names;
}
QString validateDocument(const Document &d, bool requirePhotos) {
    const auto &o = d.options;
    if (requirePhotos && d.photos.isEmpty())
        return "请先添加照片。";
    if (d.photos.size() > 42)
        return "135 模板单张索引图最多支持 42 张照片。";
    if (o.templatePath.isEmpty() && !templateNames().contains(o.filmTemplate))
        return "胶片模板不存在。";
    if (o.headerAlignment < 0 || o.headerAlignment > 3 || o.headerHeight < 180 || o.headerHeight > 1000 ||
        o.border < 0 || o.border > 200 || o.gap < 0 || o.gap > 200 || o.outputWidth < 600 || o.outputWidth > 9000 ||
        o.quality < 1 || o.quality > 100 || o.brandSize < 20 || o.brandSize > 180 || o.titleSize < 20 ||
        o.titleSize > 120 || o.infoSize < 16 || o.infoSize > 80)
        return "排版参数超出支持范围。";
    if (!o.background.isValid() || !o.headerColor.isValid() || !o.textColor.isValid() || !o.matColor.isValid())
        return "颜色值无效。";
    auto percent = [](int v) { return v >= 0 && v <= 100; };
    if (o.rightAlignment < 0 || o.rightAlignment > 3 || o.headerGap < 0 || o.headerGap > 500 || o.previewWidth < 600 ||
        o.previewWidth > 4572 || o.outputUnit < 0 || o.outputUnit > 3 || o.dpi < 72 || o.dpi > 1200 || o.leftX < 0 ||
        o.leftX > 4502 || o.rightX < 0 || o.rightX > 4502 || o.leftY < 0 || o.leftY > 1000 || o.rightY < 0 ||
        o.rightY > 1000)
        return "对齐、间距或分辨率参数无效。";
    for (auto layer : {o.headerImage, o.indexImage}) {
        if (!percent(layer.opacity) || !percent(layer.cropX) || !percent(layer.cropY))
            return "背景图片参数无效。";
        if (!layer.path.isEmpty() && !QFileInfo::exists(layer.path))
            return "背景图片不存在：" + layer.path;
    }
    for (auto v : {o.logo, o.watermark}) {
        if (!percent(v.x) || !percent(v.y) || !percent(v.opacity) || v.size < 16 || v.size > 2000 || v.type < 0 ||
            v.type > 1 || !v.style.color.isValid())
            return "Logo 或水印参数无效。";
        if (v.enabled && !v.path.isEmpty() && !QFileInfo::exists(v.path))
            return "Logo 或水印图片不存在：" + v.path;
    }
    for (const auto &style : o.textStyles)
        if (!style.color.isValid())
            return "文本块颜色无效。";
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
    SAVE_FIELD(rightAlignment);
    SAVE_FIELD(leftX);
    SAVE_FIELD(leftY);
    SAVE_FIELD(rightX);
    SAVE_FIELD(rightY);
    SAVE_FIELD(headerGap);
    SAVE_FIELD(previewWidth);
    SAVE_FIELD(outputUnit);
    SAVE_FIELD(dpi);
    SAVE_FIELD(templatePath);
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
    QJsonArray styles;
    for (const auto &s : o.textStyles)
        styles.append(styleJson(s));
    opts["textStyles"] = styles;
    opts["headerImage"] = imageJson(o.headerImage);
    opts["indexImage"] = imageJson(o.indexImage);
    opts["logo"] = overlayJson(o.logo);
    opts["watermark"] = overlayJson(o.watermark);
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
    READ_INT(rightAlignment);
    READ_INT(leftX);
    READ_INT(leftY);
    READ_INT(rightX);
    READ_INT(rightY);
    READ_INT(headerGap);
    READ_INT(previewWidth);
    READ_INT(outputUnit);
    READ_INT(dpi);
    READ_STRING(templatePath);
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
    if (opts.contains("textStyles")) {
        auto styles = opts["textStyles"].toArray();
        if (styles.size() != 6) {
            *error = "文本样式数量无效。";
            return false;
        }
        for (int i = 0; i < 6; ++i)
            o.textStyles[i] = readStyle(styles[i].toObject());
    } else
        for (auto &s : o.textStyles)
            s.color = o.textColor;
    o.headerImage = readImage(opts["headerImage"].toObject());
    o.indexImage = readImage(opts["indexImage"].toObject());
    o.logo = readOverlay(opts["logo"].toObject());
    o.watermark = readOverlay(opts["watermark"].toObject());
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

// Presets reuse the validated options codec, but never contain a photo list.
// External assets are content-addressed so replacing a preset cannot corrupt its previous assets.
bool savePreset(const Options &options, const QString &path, QString *error) {
    Document d;
    d.options = options;
    const QDir base = QFileInfo(path).absoluteDir();
    const QString assets = base.filePath("assets");
    if (!QDir().mkpath(assets)) {
        *error = "无法写入预设目录。请把软件放在可写目录。";
        return false;
    }
    auto copy = [&](QString &file) {
        if (file.isEmpty() || file.startsWith(":/"))
            return true;
        QFile f(file);
        if (!f.open(QIODevice::ReadOnly)) {
            *error = "无法读取预设素材：" + file;
            return false;
        }
        QCryptographicHash hash(QCryptographicHash::Sha256);
        hash.addData(&f);
        QString dest = assets + '/' + hash.result().toHex() + '.' + QFileInfo(file).suffix();
        if (!QFileInfo::exists(dest) && !QFile::copy(file, dest)) {
            *error = "无法保存预设素材：" + file;
            return false;
        }
        file = dest;
        return true;
    };
    auto &o = d.options;
    for (auto file :
         {&o.headerImage.path, &o.indexImage.path, &o.logo.path, &o.watermark.path, &o.watermark.style.fontFile})
        if (!copy(*file))
            return false;
    for (auto &s : o.textStyles)
        if (!copy(s.fontFile))
            return false;
    if (!templateFile(o, 0).startsWith(":/")) {
        QCryptographicHash hash(QCryptographicHash::Sha256);
        QStringList files;
        for (int row = 0; row < 7; ++row) {
            QString file = templateFile(o, row);
            QFile f(file);
            if (!f.open(QIODevice::ReadOnly)) {
                *error = "模板缺失：" + file;
                return false;
            }
            hash.addData(&f);
            files << file;
        }
        const QString folder = assets + "/template-" + hash.result().toHex();
        if (!QDir().mkpath(folder)) {
            *error = "无法保存预设模板。";
            return false;
        }
        for (const auto &file : files) {
            QString target = folder + '/' + QFileInfo(file).fileName();
            if (!QFileInfo::exists(target) && !QFile::copy(file, target)) {
                *error = "模板复制失败。";
                return false;
            }
        }
        o.templatePath = folder;
    }
    // Use an in-directory temporary document for the existing validated encoder.
    const QString temporary = base.filePath(".preset-" + QUuid::createUuid().toString(QUuid::Id128));
    if (!saveDocument(d, temporary, error))
        return false;
    QFile f(temporary);
    f.open(QIODevice::ReadOnly);
    auto root = QJsonDocument::fromJson(f.readAll()).object();
    f.close();
    QFile::remove(temporary);
    auto opts = root["options"].toObject();
    auto relative = [&](QString &value) {
        if (!value.isEmpty() && !value.startsWith(":/"))
            value = base.relativeFilePath(value);
    };
    relative(o.headerImage.path);
    relative(o.indexImage.path);
    relative(o.logo.path);
    relative(o.watermark.path);
    relative(o.watermark.style.fontFile);
    relative(o.templatePath);
    QJsonArray styles;
    for (auto &s : o.textStyles) {
        relative(s.fontFile);
        styles.append(styleJson(s));
    }
    opts["textStyles"] = styles;
    opts["headerImage"] = imageJson(o.headerImage);
    opts["indexImage"] = imageJson(o.indexImage);
    opts["logo"] = overlayJson(o.logo);
    opts["watermark"] = overlayJson(o.watermark);
    opts["templatePath"] = o.templatePath;
    root["options"] = opts;
    root["format"] = "FilmIndexPreset";
    root.remove("photos");
    QSaveFile out(path);
    if (!out.open(QIODevice::WriteOnly)) {
        *error = out.errorString();
        return false;
    }
    auto bytes = QJsonDocument(root).toJson();
    if (out.write(bytes) != bytes.size() || !out.commit()) {
        *error = out.errorString();
        return false;
    }
    return true;
}
bool loadPreset(const QString &path, Options *options, QString *error) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 1024 * 1024) {
        *error = "预设无法读取或超过大小限制。";
        return false;
    }
    QJsonParseError pe;
    auto root = QJsonDocument::fromJson(file.readAll(), &pe).object();
    if (pe.error != QJsonParseError::NoError || root["format"] != "FilmIndexPreset" || root["version"].toInt() != 1) {
        *error = "不是有效的 Film Index 预设。";
        return false;
    }
    auto opts = root["options"].toObject();
    const QDir base = QFileInfo(path).absoluteDir();
    auto absolute = [&](QJsonObject &v, const QString &key) {
        QString value = v[key].toString();
        if (!value.isEmpty() && !value.startsWith(":/"))
            v[key] = QDir::cleanPath(base.absoluteFilePath(value));
    };
    absolute(opts, "templatePath");
    for (const auto &key : {"headerImage", "indexImage", "logo", "watermark"}) {
        auto v = opts[key].toObject();
        absolute(v, "path");
        if (v.contains("style")) {
            auto style = v["style"].toObject();
            absolute(style, "fontFile");
            v["style"] = style;
        }
        opts[key] = v;
    }
    auto styles = opts["textStyles"].toArray();
    for (int i = 0; i < styles.size(); ++i) {
        auto v = styles[i].toObject();
        absolute(v, "fontFile");
        styles[i] = v;
    }
    opts["textStyles"] = styles;
    root["options"] = opts;
    root["format"] = "FilmIndexStudio";
    root["photos"] = QJsonArray();
    QTemporaryFile temporary;
    if (!temporary.open()) {
        *error = "无法创建预设读取缓冲。";
        return false;
    }
    temporary.write(QJsonDocument(root).toJson());
    temporary.flush();
    Document d;
    if (!loadDocument(temporary.fileName(), &d, error))
        return false;
    if (!d.options.templatePath.isEmpty()) {
        *error = validateTemplate(d.options.templatePath);
        if (!error->isEmpty())
            return false;
    }
    if (!activateFonts(&d.options, error))
        return false;
    *options = d.options;
    return true;
}
