#include "playlist.h"

#include <QCollator>
#include <QDir>
#include <QFileInfo>
#include <QSet>
#include <algorithm>

namespace playlist {

namespace {

const QSet<QString> &videoExtensions() {
    static const QSet<QString> set{"mkv", "mp4", "m4v", "mov", "webm", "avi", "wmv", "flv", "mpg", "mpeg",
                                   "m2ts", "mts", "ts", "ogv", "3gp", "3g2", "asf", "vob", "divx", "f4v"};
    return set;
}

const QSet<QString> &audioExtensions() {
    static const QSet<QString> set{"mp3", "flac", "ogg", "oga", "opus", "m4a", "aac", "wav", "wma", "alac",
                                   "aiff", "aif", "ape", "mka", "wv"};
    return set;
}

}  // namespace

bool playable(const QString &path) {
    const QString suffix = QFileInfo(path).suffix().toLower();
    return videoExtensions().contains(suffix) || audioExtensions().contains(suffix);
}

bool audioOnly(const QString &path) {
    return audioExtensions().contains(QFileInfo(path).suffix().toLower());
}

void sort(QStringList &paths) {
    QCollator collator;
    collator.setNumericMode(true);
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    std::sort(paths.begin(), paths.end(), [&](const QString &a, const QString &b) {
        const int order = collator.compare(QFileInfo(a).fileName(), QFileInfo(b).fileName());
        return order != 0 ? order < 0 : a < b;
    });
}

QStringList siblings(const QString &path) {
    const QFileInfo file(path);
    QStringList found;
    for (const QFileInfo &entry : file.dir().entryInfoList(QDir::Files | QDir::Readable))
        if (playable(entry.fileName()))
            found << entry.absoluteFilePath();
    const QString self = file.absoluteFilePath();
    if (!found.contains(self))
        found << self;
    sort(found);
    return found;
}

}  // namespace playlist
