#pragma once

#include <QImage>
#include <QObject>
#include <QSize>
#include <QString>
#include <atomic>
#include <memory>

namespace frames {

// The exact frame at a time, at the video's own size, through ffmpeg. Runs a
// process synchronously, so call it off the GUI thread.
QImage extract(const QString &path, double seconds, QString *error = nullptr);

// Writes a PNG atomically, with the permissions a new file would have.
bool savePng(const QImage &image, const QString &path, QString *error = nullptr);

// Puts a PNG on the Wayland clipboard through wl-copy, which keeps serving it
// after Omaview quits; false when wl-copy isn't there or fails.
bool copyWithWlCopy(const QByteArray &png);

// The scrub previews' layout: count tiles of tileSize in rows of columns,
// one every interval seconds, the first at interval / 2.
struct PreviewLayout {
    int count = 0;
    int columns = 10;
    QSize tileSize;
    double interval = 0;
    static PreviewLayout forVideo(double duration, QSize videoSize);
};

QString previewCacheDirectory();
// The cached atlas for a file, if its path, size and time still match.
QString previewCachePath(const QString &videoPath);
// Keeps the cache under a size, removing the least recently used first.
void prunePreviewCache(const QString &directory, qint64 limitBytes);

// Builds a file's previews in the background, a fast seek per tile at low
// priority, and caches them. One at a time: starting another cancels it.
class Previews : public QObject {
    Q_OBJECT

public:
    explicit Previews(QObject *parent = nullptr);
    ~Previews() override;

    void start(const QString &path, double duration, QSize videoSize);
    void cancel();

signals:
    // The atlas as a file URL the window can load, and its layout.
    void ready(const QString &atlasPath, const frames::PreviewLayout &layout);

private:
    std::shared_ptr<std::atomic_bool> m_cancelled;
    quint64 m_generation = 0;
};

}  // namespace frames
