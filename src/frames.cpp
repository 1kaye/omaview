#include "frames.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QEventLoopLocker>
#include <QFile>
#include <QFileInfo>
#include <QPainter>
#include <QPointer>
#include <QProcess>
#include <QSaveFile>
#include <QStandardPaths>
#include <QtConcurrent>
#include <algorithm>
#include <cmath>
#include <sys/stat.h>
#include <unistd.h>

#include "positions.h"
#include "probe.h"
#include "texts.h"

namespace frames {

namespace {

QString seconds(double value) {
    return QString::number(std::max(0.0, value), 'f', 3);
}

// Runs ffmpeg and returns stdout, or empty with the reason in *error.
QByteArray ffmpeg(const QStringList &arguments, int timeoutMs, QString *error, bool lowPriority = false,
                  const std::atomic_bool *cancelled = nullptr) {
    const QString tool = probe::toolPath("ffmpeg");
    if (tool.isEmpty()) {
        if (error)
            *error = Texts::tr("ffmpeg was not found. Install ffmpeg.");
        return {};
    }
    QProcess process;
    if (lowPriority)
        process.setChildProcessModifier([] { (void)::nice(10); });
    process.start(tool, QStringList{"-nostdin", "-v", "error"} + arguments);
    // Waits in slices, so a cancelled preview stops its ffmpeg promptly.
    QElapsedTimer clock;
    clock.start();
    while (!process.waitForFinished(100)) {
        if (process.state() == QProcess::NotRunning)
            break;
        if ((cancelled && *cancelled) || clock.elapsed() > timeoutMs) {
            process.kill();
            process.waitForFinished(1000);
            if (error)
                *error = Texts::tr("ffmpeg took too long.");
            return {};
        }
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        if (error) {
            const QString message = QString::fromUtf8(process.readAllStandardError()).trimmed().section('\n', -1);
            *error = message.isEmpty() ? Texts::tr("ffmpeg could not read this file.")
                                       : message;
        }
        return {};
    }
    return process.readAllStandardOutput();
}

}  // namespace

namespace {

// Read rather than set and restored, which would race other threads making
// files; Linux reports it in /proc since 4.7.
mode_t processUmask() {
    QFile status("/proc/self/status");
    if (status.open(QIODevice::ReadOnly))
        for (const QByteArray &line : status.readAll().split('\n'))
            if (line.startsWith("Umask:")) {
                bool ok = false;
                const uint mask = line.mid(6).trimmed().toUInt(&ok, 8);
                if (ok)
                    return mode_t(mask);
            }
    return 022;
}

}  // namespace

QImage extract(const QString &path, double at, QString *error) {
    // -ss before -i seeks the input, which ffmpeg makes exact when decoding.
    const QByteArray png = ffmpeg({"-ss", seconds(at), "-i", path, "-map", "0:V:0", "-frames:v", "1",
                                   "-f", "image2pipe", "-c:v", "png", "-"},
                                  60000, error);
    if (png.isEmpty()) {
        if (error && error->isEmpty())
            *error = Texts::tr("There is no frame at that time.");
        return {};
    }
    QImage image = QImage::fromData(png, "PNG");
    if (image.isNull() && error)
        *error = Texts::tr("ffmpeg gave a frame that could not be read.");
    return image;
}

bool savePng(const QImage &image, const QString &path, QString *error) {
    QSaveFile file(path);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly) || !image.save(&file, "PNG")) {
        if (error)
            *error = file.errorString();
        file.cancelWriting();
        return false;
    }
    // A QSaveFile's temp file is 0600 whatever the umask; give it what a new
    // file would get, or what the file it replaces had.
    QFileDevice::Permissions permissions = QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                           | QFileDevice::ReadGroup | QFileDevice::ReadOther;
    if (QFileInfo::exists(path))
        permissions = QFile::permissions(path);
    else {
        const mode_t mask = processUmask();
        permissions = QFileDevice::Permissions();
        const mode_t mode = 0666 & ~mask;
        if (mode & 0400) permissions |= QFileDevice::ReadOwner | QFileDevice::ReadUser;
        if (mode & 0200) permissions |= QFileDevice::WriteOwner | QFileDevice::WriteUser;
        if (mode & 0040) permissions |= QFileDevice::ReadGroup;
        if (mode & 0020) permissions |= QFileDevice::WriteGroup;
        if (mode & 0004) permissions |= QFileDevice::ReadOther;
        if (mode & 0002) permissions |= QFileDevice::WriteOther;
    }
    file.setPermissions(permissions);
    if (!file.commit()) {
        if (error)
            *error = file.errorString();
        return false;
    }
    return true;
}

bool copyWithWlCopy(const QByteArray &png) {
    const QString tool = probe::toolPath("wl-copy");
    if (tool.isEmpty() || qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY"))
        return false;
    // wl-copy forks a server that holds the clipboard and returns at once.
    QProcess process;
    process.start(tool, {"--type", "image/png"});
    if (!process.waitForStarted(2000))
        return false;
    process.write(png);
    process.closeWriteChannel();
    return process.waitForFinished(5000) && process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
}

PreviewLayout PreviewLayout::forVideo(double duration, QSize videoSize) {
    PreviewLayout layout;
    if (duration <= 0 || videoSize.isEmpty())
        return layout;
    // One every two seconds for short clips, never more than 100, so a film
    // costs the same as a trailer and the atlas stays small.
    layout.count = std::clamp(int(duration / 2), 1, 100);
    layout.interval = duration / layout.count;
    layout.columns = std::min(10, layout.count);
    const int width = 192;
    const int height = std::clamp(int(std::lround(width * double(videoSize.height()) / videoSize.width())) & ~1,
                                  54, 384);
    layout.tileSize = QSize(width, height);
    return layout;
}

QString previewCacheDirectory() {
    return QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) + "/omaplay/previews";
}

QString previewCachePath(const QString &videoPath) {
    const QString key = Positions::key(videoPath);
    return key.isEmpty() ? QString() : previewCacheDirectory() + "/" + key + ".jpg";
}

void prunePreviewCache(const QString &directory, qint64 limitBytes) {
    QFileInfoList files = QDir(directory).entryInfoList({"*.jpg"}, QDir::Files, QDir::Time | QDir::Reversed);
    qint64 total = 0;
    for (const QFileInfo &file : files)
        total += file.size();
    for (const QFileInfo &file : files) {
        if (total <= limitBytes)
            break;
        total -= file.size();
        QFile::remove(file.absoluteFilePath());
    }
}

Previews::Previews(QObject *parent) : QObject(parent) {}

Previews::~Previews() {
    cancel();
}

void Previews::cancel() {
    if (m_cancelled)
        *m_cancelled = true;
    m_cancelled.reset();
    ++m_generation;
}

void Previews::start(const QString &path, double duration, QSize videoSize) {
    cancel();
    const PreviewLayout layout = PreviewLayout::forVideo(duration, videoSize);
    const QString cachePath = previewCachePath(path);
    if (layout.count == 0 || cachePath.isEmpty())
        return;
    if (QFileInfo::exists(cachePath)) {
        // Touched, so pruning removes what hasn't been watched lately first.
        QFile file(cachePath);
        if (file.open(QIODevice::ReadWrite))
            file.setFileTime(QDateTime::currentDateTime(), QFileDevice::FileModificationTime);
        emit ready(cachePath, layout);
        return;
    }

    auto cancelled = std::make_shared<std::atomic_bool>(false);
    m_cancelled = cancelled;
    const quint64 generation = m_generation;
    QPointer<Previews> self(this);
    (void)QtConcurrent::run([self, generation, cancelled, path, layout, cachePath] {
        // The work finishes, or is cancelled, even if the window closes first.
        QEventLoopLocker keepRunning;
        const int rows = (layout.count + layout.columns - 1) / layout.columns;
        QImage atlas(layout.tileSize.width() * layout.columns, layout.tileSize.height() * rows, QImage::Format_RGB32);
        atlas.fill(Qt::black);
        QPainter painter(&atlas);
        const QString scale = QString("scale=%1:%2:force_original_aspect_ratio=decrease")
                                  .arg(layout.tileSize.width()).arg(layout.tileSize.height());
        int made = 0;
        for (int i = 0; i < layout.count; ++i) {
            if (*cancelled)
                return;
            // A file ffmpeg can't read at all fails on every tile; stop early
            // rather than spawning the rest.
            if (i == 3 && made == 0)
                return;
            // A fast seek to the keyframe before each time: good enough for a
            // thumbnail, and far cheaper than decoding the film.
            const QByteArray bytes = ffmpeg({"-skip_frame", "nokey", "-ss", seconds((i + 0.5) * layout.interval),
                                             "-i", path, "-map", "0:V:0", "-frames:v", "1", "-vf", scale,
                                             "-f", "image2pipe", "-c:v", "bmp", "-"},
                                            20000, nullptr, true, cancelled.get());
            const QImage tile = QImage::fromData(bytes, "BMP");
            if (tile.isNull())
                continue;
            ++made;
            const QPoint cell((i % layout.columns) * layout.tileSize.width(),
                              (i / layout.columns) * layout.tileSize.height());
            painter.drawImage(cell + QPoint((layout.tileSize.width() - tile.width()) / 2,
                                            (layout.tileSize.height() - tile.height()) / 2),
                              tile);
        }
        painter.end();
        // Never cache a blank atlas: the next open tries again.
        if (*cancelled || made == 0)
            return;
        QDir().mkpath(previewCacheDirectory());
        QSaveFile out(cachePath);
        out.setDirectWriteFallback(false);
        if (!out.open(QIODevice::WriteOnly) || !atlas.save(&out, "JPG", 80) || !out.commit())
            return;
        prunePreviewCache(previewCacheDirectory(), 256LL * 1024 * 1024);
        QMetaObject::invokeMethod(
            qApp,
            [self, generation, cachePath, layout] {
                if (self && self->m_generation == generation)
                    emit self->ready(cachePath, layout);
            },
            Qt::QueuedConnection);
    });
}

}  // namespace frames
