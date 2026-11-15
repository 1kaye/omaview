#pragma once

#include <QQuickAsyncImageProvider>
#include <QThreadPool>

// image://thumbnail/<percent-encoded path>: a picture decoded small, or a
// video's poster frame, for the grid; image://thumbnail/frame/<n>/<path>, a
// video's nth preview frame. Off the GUI thread, several at a time.
class ThumbnailProvider : public QQuickAsyncImageProvider {
public:
    ThumbnailProvider();
    ~ThumbnailProvider() override;
    QQuickImageResponse *requestImageResponse(const QString &id, const QSize &requestedSize) override;

private:
    QThreadPool m_pool;
};
