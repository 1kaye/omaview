// Drives the real Main.qml against the real backend and short clips made by
// ffmpeg, offscreen, and fails on any QML warning. With OMAPLAY_SHOTS set to
// a directory, it also saves what the window looked like at each step.

#include <QDir>
#include <QFile>
#include <QMediaPlayer>
#include <QPainter>
#include <QVideoFrame>
#include <QProcess>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QtTest>

#include "backend.h"
#include "filepicker.h"
#include "inhibit.h"
#include "theme.h"
#include "translator.h"

namespace {

bool write(const QString &path, const QByteArray &contents) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(contents) == contents.size();
}

// A clip with a picture that changes, a tone, English subtitles and two chapters.
bool makeClip(const QString &directory, const QString &name, int seconds) {
    write(directory + "/subs.srt", "1\n00:00:00,300 --> 00:00:03,000\nWe kept driving until\nthe road ran out.\n\n"
                                   "2\n00:00:04,000 --> 00:00:07,000\nThen we walked.\n");
    const QByteArray split = QByteArray::number(std::min(4000, seconds * 500));
    write(directory + "/chapters.txt", ";FFMETADATA1\ntitle=Coast Road\n[CHAPTER]\nTIMEBASE=1/1000\nSTART=0\nEND=" + split
                                           + "\ntitle=Leaving the city\n[CHAPTER]\nTIMEBASE=1/1000\nSTART=" + split + "\nEND="
                                           + QByteArray::number(seconds * 1000) + "\ntitle=The coast road\n");
    QProcess ffmpeg;
    ffmpeg.setWorkingDirectory(directory);
    ffmpeg.start("ffmpeg", {"-v", "error", "-y", "-f", "lavfi", "-i",
                            QString("testsrc2=size=640x360:rate=25:duration=%1").arg(seconds), "-f", "lavfi", "-i",
                            QString("sine=frequency=440:duration=%1").arg(seconds), "-i", "subs.srt", "-i",
                            "chapters.txt", "-map", "0:v", "-map", "1:a", "-map", "2:s", "-map_metadata", "3",
                            "-map_chapters", "3", "-c:v", "libx264", "-g", "25", "-pix_fmt", "yuv420p", "-c:a", "aac", "-ac", "2",
                            "-c:s", "srt", "-metadata:s:a:0", "language=eng", "-metadata:s:s:0", "language=eng", name});
    return ffmpeg.waitForFinished(60000) && ffmpeg.exitCode() == 0;
}

class FakePicker : public FilePicker {
public:
    using FilePicker::FilePicker;
    void openMedia() override { ++opens; }
    void saveImage(const QUrl &url) override { suggested = url; }
    int opens = 0;
    QUrl suggested;
};

}  // namespace

class UiTests : public QObject {
    Q_OBJECT

    QTemporaryDir m_dir;
    QString m_clip;
    QString m_second;

    struct Window {
        std::unique_ptr<Translator> translator;
        std::unique_ptr<Theme> theme;
        std::unique_ptr<FakePicker> picker;
        std::unique_ptr<Backend> backend;
        std::unique_ptr<IdleInhibitor> idle;
        std::unique_ptr<QQmlApplicationEngine> engine;
        QQuickWindow *window = nullptr;
        QStringList warnings;
    };

    std::unique_ptr<Window> open(const QString &state) {
        auto w = std::make_unique<Window>();
        QDir().mkpath(state);
        w->translator = std::make_unique<Translator>(state + "/language");
        QCoreApplication::installTranslator(w->translator.get());
        w->theme = std::make_unique<Theme>(state);
        w->picker = std::make_unique<FakePicker>();
        Backend::Options options;
        options.positionsPath = state + "/positions.json";
        options.audio = false;
        options.picker = w->picker.get();
        options.previewDelayMs = 0;
        w->backend = std::make_unique<Backend>(options);
        w->idle = std::make_unique<IdleInhibitor>();
        w->engine = std::make_unique<QQmlApplicationEngine>();
        auto *warnings = &w->warnings;
        connect(w->engine.get(), &QQmlEngine::warnings, this, [warnings](const QList<QQmlError> &errors) {
            for (const auto &error : errors)
                *warnings << error.toString();
        });
        w->engine->rootContext()->setContextProperty("translator", w->translator.get());
        w->engine->rootContext()->setContextProperty("theme", w->theme.get());
        w->engine->rootContext()->setContextProperty("backend", w->backend.get());
        w->engine->rootContext()->setContextProperty("idle", w->idle.get());
        w->engine->load(QUrl("qrc:/Main.qml"));
        if (w->engine->rootObjects().isEmpty())
            return w;
        w->window = qobject_cast<QQuickWindow *>(w->engine->rootObjects().first());
        w->idle->setWindow(w->window);
        return w;
    }

    void close(std::unique_ptr<Window> &w) {
        if (w->window)
            w->window->close();
        w->backend->close();
        QCoreApplication::removeTranslator(w->translator.get());
        w->engine.reset();
        w.reset();
    }

    // The offscreen platform renders with Qt Quick's software path, which
    // can't draw a VideoOutput, so the frame on screen is painted where the
    // picture is and the window, with a clear background, over it.
    void shot(Window *w, const QString &name) {
        const QString directory = qEnvironmentVariable("OMAPLAY_SHOTS");
        if (directory.isEmpty())
            return;
        QDir().mkpath(directory);
        const QColor background = w->window->color();
        auto *backgroundItem = w->window->property("background").value<QQuickItem *>();
        w->window->setColor(Qt::transparent);
        backgroundItem->setVisible(false);
        QImage ui = w->window->grabWindow();
        backgroundItem->setVisible(true);
        w->window->setColor(background);
        QImage image(ui.size(), QImage::Format_ARGB32_Premultiplied);
        image.fill(background);
        QPainter painter(&image);
        auto *video = w->window->findChild<QQuickItem *>("video");
        QImage frame = w->backend->currentFrame().isValid() ? QVideoFrame(w->backend->currentFrame()).toImage() : QImage();
        if (video && !frame.isNull() && w->backend->hasVideo()) {
            const QRectF picture = video->property("contentRect").toRectF();
            const qreal ratio = qreal(ui.width()) / w->window->width();
            painter.drawImage(QRectF(picture.topLeft() * ratio, picture.size() * ratio), frame);
        }
        painter.drawImage(0, 0, ui);
        painter.end();
        image.save(directory + "/" + name + ".png");
    }

    template <typename T>
    T *find(Window *w, const char *name) {
        return w->window->findChild<T *>(name);
    }

private slots:
    void initTestCase() {
        QQuickStyle::setStyle("Material");
        // So a screenshot's window can be grabbed with a clear background.
        QQuickWindow::setDefaultAlphaBuffer(true);
        QVERIFY(m_dir.isValid());
        // Caches and state go to a temp home, and nothing here may reach the
        // desktop's clipboard.
        qputenv("XDG_CACHE_HOME", m_dir.filePath("cache").toUtf8());
        qputenv("XDG_STATE_HOME", m_dir.filePath("state").toUtf8());
        qunsetenv("WAYLAND_DISPLAY");
        QVERIFY(makeClip(m_dir.path(), "a-coast.mkv", 8));
        QVERIFY(makeClip(m_dir.path(), "b-pines.mkv", 3));
        m_clip = m_dir.filePath("a-coast.mkv");
        m_second = m_dir.filePath("b-pines.mkv");
    }

    void emptyWindowSaysWhatToDo() {
        auto w = open(m_dir.filePath("empty"));
        QVERIFY(w->window);
        QVERIFY(QTest::qWaitForWindowExposed(w->window));
        // Nothing to play starts no player.
        QVERIFY(!w->backend->playerReady());
        QTest::keyClick(w->window, Qt::Key_O, Qt::ControlModifier);
        QCOMPARE(w->picker->opens, 1);
        shot(w.get(), "0-empty");
        QVERIFY2(w->warnings.isEmpty(), qPrintable(w->warnings.join('\n')));
        close(w);
    }

    void playsAndAnswersTheKeyboard() {
        auto w = open(m_dir.filePath("play"));
        QVERIFY(w->window);
        w->window->resize(960, 540);
        QVERIFY(QTest::qWaitForWindowExposed(w->window));
        w->window->requestActivate();
        QVERIFY(QTest::qWaitForWindowActive(w->window));

        w->backend->open({m_clip});
        QTRY_VERIFY_WITH_TIMEOUT(w->backend->playing(), 10000);
        QTRY_VERIFY_WITH_TIMEOUT(w->backend->position() > 300, 10000);
        QVERIFY(w->backend->hasVideo());
        QCOMPARE(w->backend->videoSize(), QSize(640, 360));
        QCOMPARE(w->backend->playlistCount(), 2);
        QTRY_COMPARE(w->backend->chapters().size(), 2);
        QTRY_COMPARE(w->backend->audioLabel(), QString("English · stereo"));
        // The scrub previews follow, once the player knows enough about the file.
        QTRY_VERIFY_WITH_TIMEOUT(w->backend->previewCount() > 0, 30000);
        QVERIFY(w->backend->previewSource().toLocalFile().startsWith(m_dir.filePath("cache")));
        // English everywhere, so subtitles start off.
        QCOMPARE(w->backend->subtitleChoices(), 1);
        QCOMPARE(w->backend->subtitleLabel(), QString());

        auto *centerValue = find<QQuickItem>(w.get(), "centerValue");
        QTest::keyClick(w->window, Qt::Key_Space);
        QTRY_VERIFY(!w->backend->playing());
        QCOMPARE(centerValue->property("text").toString(), QString("Paused"));

        // Speed and volume say what they became.
        QTest::keyClick(w->window, Qt::Key_BracketRight);
        QCOMPARE(w->backend->rate(), 1.25);
        QCOMPARE(centerValue->property("text").toString(), QString("1.25×"));
        QTest::keyClick(w->window, Qt::Key_Backspace);
        QCOMPARE(w->backend->rate(), 1.0);
        const double volume = w->backend->volume();
        QTest::keyClick(w->window, Qt::Key_Down);
        QCOMPARE(w->backend->volume(), std::round((volume - 0.05) * 100) / 100);

        // Subtitles on with C, drawn by the window at their time.
        QTest::keyClick(w->window, Qt::Key_C);
        QCOMPARE(w->backend->subtitleLabel(), QString("English"));
        // Seeking into the middle of a line shows the line, once the track's
        // cues have been read out.
        w->backend->seek(1500);
        auto *subtitle = find<QQuickItem>(w.get(), "subtitle");
        QTRY_VERIFY_WITH_TIMEOUT(subtitle->property("text").toString().contains("driving"), 8000);
        w->backend->seek(4500);
        QTRY_COMPARE(subtitle->property("text").toString(), QString("Then we walked."));
        w->backend->seek(1500);
        shot(w.get(), "1-bare");

        // Seeking with the keys, and a tenth of the way with the digits.
        w->backend->seek(1000);
        QTest::keyClick(w->window, Qt::Key_Right);
        QCOMPARE(w->backend->position(), qint64(6000));
        QTest::keyClick(w->window, Qt::Key_3);
        QVERIFY(qAbs(w->backend->position() - w->backend->duration() * 3 / 10) < 100);
        QCOMPARE(w->backend->chapterTitle(), QString("Leaving the city"));
        QTest::keyClick(w->window, Qt::Key_PageDown);
        QCOMPARE(w->backend->position(), qint64(4000));
        QCOMPARE(w->backend->chapterTitle(), QString("The coast road"));

        // A frame step moves by the frame's own timestamps.
        QTRY_VERIFY_WITH_TIMEOUT(w->backend->currentFrame().isValid(), 5000);
        QTest::qWait(300);
        const qint64 before = w->backend->position();
        QTest::keyClick(w->window, Qt::Key_Period);
        QVERIFY(w->backend->position() > before);
        QVERIFY2(w->backend->position() - before <= 60, qPrintable(QString::number(w->backend->position() - before)));
        QTRY_VERIFY_WITH_TIMEOUT(w->backend->currentFrame().startTime() / 1000 >= before, 5000);
        const qint64 stepped = w->backend->position();
        QTest::keyClick(w->window, Qt::Key_Comma);
        QVERIFY(w->backend->position() < stepped);
        QVERIFY(stepped - w->backend->position() <= 60);

        // Tab brings the overlay in, and Esc puts it away.
        auto *overlay = find<QQuickItem>(w.get(), "overlay");
        QVERIFY(!overlay->property("item").value<QQuickItem *>());
        QTest::keyClick(w->window, Qt::Key_Tab);
        QTRY_VERIFY(overlay->property("item").value<QQuickItem *>());
        QTRY_COMPARE(overlay->property("item").value<QQuickItem *>()->opacity(), 1.0);
        shot(w.get(), "2-overlay");
        QTest::keyClick(w->window, Qt::Key_Escape);
        QTRY_VERIFY(!overlay->property("item").value<QQuickItem *>());

        // I and ? open their cards; Escape closes them.
        auto *info = w->window->findChild<QObject *>("info");
        QTest::keyClick(w->window, Qt::Key_I);
        QTRY_VERIFY(info->property("opened").toBool());
        shot(w.get(), "3-info");
        QTest::keyClick(w->window, Qt::Key_Escape);
        QTRY_VERIFY(!info->property("visible").toBool());
        // The key that opens a card closes it too, though the card holds the keys.
        QTest::keyClick(w->window, Qt::Key_I);
        QTRY_VERIFY(info->property("opened").toBool());
        QTest::keyClick(w->window, Qt::Key_I);
        QTRY_VERIFY(!info->property("visible").toBool());
        auto *help = w->window->findChild<QObject *>("help");
        QTest::keyClick(w->window, '?');
        QTRY_VERIFY(help->property("opened").toBool());
        shot(w.get(), "4-help");
        QTest::keyClick(w->window, Qt::Key_Escape);
        QTRY_VERIFY(!help->property("visible").toBool());
        QTest::keyClick(w->window, '?');
        QTRY_VERIFY(help->property("opened").toBool());
        QTest::keyClick(w->window, '?');
        QTRY_VERIFY(!help->property("visible").toBool());

        // Save frame asks the picker, suggesting a name with the time.
        QTest::keyClick(w->window, Qt::Key_S, Qt::ControlModifier);
        QVERIFY2(w->picker->suggested.fileName().startsWith("a-coast-00-0"),
                 qPrintable(w->picker->suggested.toString()));
        QVERIFY(w->picker->suggested.fileName().endsWith(".png"));

        QVERIFY2(w->warnings.isEmpty(), qPrintable(w->warnings.join('\n')));
        close(w);
    }

    void endOffersTheNextFile() {
        auto w = open(m_dir.filePath("end"));
        QVERIFY(w->window);
        QVERIFY(QTest::qWaitForWindowExposed(w->window));
        w->window->requestActivate();
        QVERIFY(QTest::qWaitForWindowActive(w->window));
        w->backend->open({m_clip});
        QTRY_VERIFY_WITH_TIMEOUT(w->backend->playing(), 10000);
        QTRY_VERIFY_WITH_TIMEOUT(w->backend->seekable(), 10000);
        w->backend->seek(w->backend->duration() - 300);
        QTRY_VERIFY_WITH_TIMEOUT(w->backend->ended(), 10000);
        QCOMPARE(w->backend->nextName(), QString("b-pines.mkv"));
        // The last frame stays rather than going black.
        QVERIFY(w->backend->currentFrame().isValid());
        shot(w.get(), "5-end");
        QTest::keyClick(w->window, Qt::Key_Return);
        QTRY_COMPARE(w->backend->fileName(), QString("b-pines.mkv"));
        QTRY_VERIFY_WITH_TIMEOUT(w->backend->playing(), 10000);
        // A second video is still a video: the picture, not the song view,
        // and the screen held on while it plays and only then.
        QTRY_VERIFY(w->backend->currentFrame().isValid());
        QVERIFY(w->backend->hasVideo());
        QVERIFY(w->idle->active());
        w->backend->pause();
        QVERIFY(!w->idle->active());
        w->backend->play();
        QCOMPARE(w->backend->nextName(), QString());
        QTest::keyClick(w->window, Qt::Key_N);
        QTRY_VERIFY(find<QQuickItem>(w.get(), "centerValue")->property("text").toString().contains("last"));
        QVERIFY2(w->warnings.isEmpty(), qPrintable(w->warnings.join('\n')));
        close(w);
    }

    void aSongShowsItsCover() {
        QVERIFY(QProcess::execute("ffmpeg", {"-v", "error", "-y", "-f", "lavfi", "-i", "testsrc2=size=300x300:duration=1",
                                             "-frames:v", "1", m_dir.filePath("cover.png")}) == 0);
        const QString song = m_dir.filePath("song.mp3");
        QVERIFY(QProcess::execute("ffmpeg", {"-v", "error", "-y", "-f", "lavfi", "-i", "sine=duration=4", "-i",
                                             m_dir.filePath("cover.png"), "-map", "0:a", "-map", "1:v", "-c:a",
                                             "libmp3lame", "-c:v", "mjpeg", "-disposition:v", "attached_pic",
                                             "-metadata", "title=Low Tide", "-metadata", "artist=The Pines", song}) == 0);
        auto w = open(m_dir.filePath("song"));
        QVERIFY(w->window);
        QVERIFY(QTest::qWaitForWindowExposed(w->window));
        w->backend->open({song});
        QTRY_VERIFY_WITH_TIMEOUT(w->backend->playing(), 10000);
        QTRY_COMPARE(w->backend->title(), QString("Low Tide"));
        QTRY_COMPARE(w->backend->artist(), QString("The Pines"));
        QTRY_VERIFY(w->backend->coverArt().startsWith("data:image/jpeg;base64,"));
        QVERIFY(!w->backend->hasVideo());
        // The screen is held on only for a picture, and a song gets a square.
        QVERIFY(!w->idle->active());
        QTRY_COMPARE(w->window->width(), w->window->height());
        shot(w.get(), "7-song");
        QVERIFY2(w->warnings.isEmpty(), qPrintable(w->warnings.join('\n')));
        close(w);
    }

    void aLightThemeKeepsThePictureDark() {
        const QString state = m_dir.filePath("light");
        QVERIFY(QDir().mkpath(state + "/theme"));
        QVERIFY(write(state + "/theme/colors.toml", "accent = \"#1e66f5\"\nbackground = \"#eff1f5\"\n"
                                                   "foreground = \"#4c4f69\"\nmuted = \"#8c8fa1\"\n"));
        auto w = open(state);
        QVERIFY(w->window);
        QVERIFY(w->theme->light());
        w->window->resize(960, 540);
        QVERIFY(QTest::qWaitForWindowExposed(w->window));
        w->window->requestActivate();
        QVERIFY(QTest::qWaitForWindowActive(w->window));
        w->backend->open({m_clip});
        QTRY_VERIFY_WITH_TIMEOUT(w->backend->playing(), 10000);
        QCOMPARE(w->window->color(), QColor("black"));
        QTest::keyClick(w->window, Qt::Key_Up);
        auto *centerValue = find<QQuickItem>(w.get(), "centerValue");
        QCOMPARE(centerValue->property("color").value<QColor>(), QColor("#1e66f5"));
        w->backend->pause();
        QTRY_VERIFY_WITH_TIMEOUT(w->backend->currentFrame().isValid(), 5000);
        QTRY_COMPARE(find<QQuickItem>(w.get(), "centerOsd")->opacity(), 1.0);
        shot(w.get(), "8-light");
        QVERIFY2(w->warnings.isEmpty(), qPrintable(w->warnings.join('\n')));
        close(w);
    }

    void aFileThatWontPlaySaysWhy() {
        auto w = open(m_dir.filePath("broken"));
        QVERIFY(w->window);
        QVERIFY(QTest::qWaitForWindowExposed(w->window));
        const QString broken = m_dir.filePath("broken.mkv");
        QVERIFY(write(broken, "this is not a video"));
        w->backend->open({broken});
        QTRY_VERIFY_WITH_TIMEOUT(!w->backend->error().isEmpty(), 10000);
        auto *error = find<QQuickItem>(w.get(), "error");
        QTRY_VERIFY(error->isVisible());
        shot(w.get(), "6-error");
        QVERIFY2(w->warnings.isEmpty(), qPrintable(w->warnings.join('\n')));
        close(w);
    }
};

QTEST_MAIN(UiTests)
#include "ui_tests.moc"
