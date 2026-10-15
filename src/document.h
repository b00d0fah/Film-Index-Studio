#pragma once
#include <QColor>
#include <QStringList>
#include <QVector>
#include <array>

struct TextStyle {
    QString family = "Microsoft YaHei UI", fontFile;
    QColor color = QColor("#f6f6f2");
};
struct ImageLayer {
    QString path;
    int opacity = 100, cropX = 50, cropY = 50;
};
struct Overlay {
    bool enabled = false;
    int x = 50, y = 50, size = 160, opacity = 100;
    QString path, text;
    TextStyle style;
    int type = 0; // watermark: 0 text, 1 image
};

struct Photo {
    QString path;
    int rotation = 0;
    double cropX = 0, cropY = 0;
};
struct Options {
    QString filmTemplate = "default";
    int headerAlignment = 0, rightAlignment = 0;      // 0 left, 1 center, 2 right, 3 custom
    int leftX = 0, leftY = 0, rightX = 0, rightY = 0; // template pixels, relative to each text column
    bool header = true, details = true, autoRotate = true, fit = false;
    int headerHeight = 480, border = 35, gap = 25, headerGap = 35, outputWidth = 4572, quality = 94;
    int previewWidth = 1600, outputUnit = 0, dpi = 300;
    QColor background = Qt::white, headerColor = QColor("#101010"), textColor = QColor("#f6f6f2"), matColor = Qt::black;
    QString brand = "Film Index", title, note, film, iso, location, date, camera, lens;
    QString templatePath;
    int brandSize = 112, titleSize = 72, infoSize = 60;
    std::array<TextStyle, 6> textStyles; // main, note, roll title, film/ISO, place/date, camera/lens
    ImageLayer headerImage, indexImage;
    Overlay logo, watermark;
    Options() {
        logo.x = 3;
        watermark.text = "Film Index";
        watermark.size = 72;
        watermark.opacity = 25;
    }
};
struct Document {
    Options options;
    QVector<Photo> photos;
};
QStringList templateNames();
QString templatesDirectory();
QString templateFile(const Options &options, int row);
QString validateTemplate(const QString &directory);
bool importTemplate(const QString &directory, QString *name, QString *error);
bool savePreset(const Options &options, const QString &path, QString *error);
bool loadPreset(const QString &path, Options *options, QString *error);
bool activateFonts(Options *options, QString *error);
bool importFont(TextStyle *style, const QString &path, QString *error);
QString importedFontFile(const QString &family);
double widthInUnit(int pixels, int unit, int dpi);
int widthInPixels(double value, int unit, int dpi);
QString validateDocument(const Document &doc, bool requirePhotos = true);
bool saveDocument(const Document &doc, const QString &path, QString *error);
bool loadDocument(const QString &path, Document *doc, QString *error);
