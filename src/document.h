#pragma once
#include <QColor>
#include <QStringList>
#include <QVector>

struct Photo {
    QString path;
    int rotation = 0;
    double cropX = 0, cropY = 0;
};
struct Options {
    QString filmTemplate = "default";
    int headerAlignment = 0; // 0 left, 1 center, 2 right
    bool header = true, details = true, autoRotate = true, fit = false;
    int headerHeight = 480, border = 35, gap = 25, outputWidth = 4572, quality = 94;
    QColor background = Qt::white, headerColor = QColor("#101010"), textColor = QColor("#f6f6f2"), matColor = Qt::black;
    QString brand = "Film Index Studio", title, note, film, iso, location, date, camera, lens;
    int brandSize = 112, titleSize = 72, infoSize = 42;
};
struct Document {
    Options options;
    QVector<Photo> photos;
};
QStringList templateNames();
QString validateDocument(const Document &doc, bool requirePhotos = true);
bool saveDocument(const Document &doc, const QString &path, QString *error);
bool loadDocument(const QString &path, Document *doc, QString *error);
