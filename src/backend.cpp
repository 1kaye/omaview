#include "backend.h"

#include <QAudioDevice>
#include <QAudioOutput>
#include <QBuffer>
#include <QClipboard>
#include <QDir>
#include <QEventLoopLocker>
#include <QFileInfo>
#include <QGuiApplication>
#include <QLocale>
#include <QMediaMetaData>
#include <QMediaPlayer>
#include <QMutexLocker>
#include <QPointer>
#include <QProcess>
#include <QStandardPaths>
#include <QTime>
#include <QVideoSink>
#include <QtAudio>
#include <QtConcurrent>
#include <cmath>

#include "filepicker.h"
#include "playlist.h"
#include "portalfilepicker.h"
#include "texts.h"

namespace {

QString clock(qint64 ms) {
    const qint64 total = std::max<qint64>(0, ms / 1000);
    const qint64 hours = total / 3600, minutes = total / 60 % 60, seconds = total % 60;
    return hours > 0 ? QString("%1:%2:%3").arg(hours).arg(minutes, 2, 10, QChar('0')).arg(seconds, 2, 10, QChar('0'))
                     : QString("%1:%2").arg(minutes, 2, 10, QChar('0')).arg(seconds, 2, 10, QChar('0'));
}

// Codec names as people know them, rather than ffmpeg's identifiers.
QString codecName(const probe::Stream &stream) {
    static const QHash<QString, QString> names{
        {"h264", "H.264"}, {"hevc", "HEVC"}, {"av1", "AV1"}, {"vp9", "VP9"}, {"vp8", "VP8"},
        {"mpeg4", "MPEG-4"}, {"mpeg2video", "MPEG-2"}, {"mpeg1video", "MPEG-1"}, {"prores", "ProRes"},
        {"theora", "Theora"}, {"wmv3", "WMV"}, {"aac", "AAC"}, {"opus", "Opus"}, {"vorbis", "Vorbis"},
        {"mp3", "MP3"}, {"flac", "FLAC"}, {"ac3", "Dolby Digital"}, {"eac3", "Dolby Digital Plus"},
        {"truehd", "Dolby TrueHD"}, {"dts", "DTS"}, {"alac", "ALAC"}, {"pcm_s16le", "PCM"},
        {"pcm_s24le", "PCM"}, {"subrip", "SubRip"}, {"ass", "ASS"}, {"ssa", "SSA"}, {"webvtt", "WebVTT"},
        {"mov_text", "MP4 text"}, {"hdmv_pgs_subtitle", "PGS"}, {"dvd_subtitle", "VobSub"},
        {"dvb_subtitle", "DVB"}, {"mjpeg", "JPEG"}, {"png", "PNG"},
    };
    return names.value(stream.codec, stream.codec.toUpper());
}

QString channelName(const probe::Stream &stream) {
    const QString layout = stream.channelLayout.section('(', 0, 0);
    if (layout == "mono" || stream.channels == 1)
        return Texts::tr("mono");
    if (layout == "stereo" || stream.channels == 2)
        return Texts::tr("stereo");
    if (!layout.isEmpty() && layout.front().isDigit())
        return layout;
    return stream.channels > 0 ? QString::number(stream.channels) : QString();
}

QString languageOf(const QMediaMetaData &metadata) {
    const QVariant value = metadata.value(QMediaMetaData::Language);
    if (!value.isValid())
        return {};
    const auto language = value.value<QLocale::Language>();
    if (language == QLocale::AnyLanguage || language == QLocale::C)
        return {};
    return QLocale::languageToCode(language, QLocale::ISO639Part1);
}

QString join(const QStringList &parts) {
    QStringList kept;
    for (const QString &part : parts)
        if (!part.trimmed().isEmpty())
            kept << part.trimmed();
    return kept.join(QStringLiteral(" · "));
}

}  // namespace

Backend::Backend(QObject *parent) : Backend(Options{}, parent) {}

Backend::Backend(const Options &options, QObject *parent)
    : QObject(parent), m_options(options), m_positions(options.positionsPath) {
    if (m_options.picker) {
        m_picker = m_options.picker;
    } else {
        m_ownPicker = std::make_unique<PortalFilePicker>();
        m_picker = m_ownPicker.get();
    }
    connect(m_picker, &FilePicker::openSelected, this, &Backend::openUrls);
    connect(m_picker, &FilePicker::failed, this, &Backend::notice);
    connect(m_picker, &FilePicker::saveSelected, this, [this](const QUrl &url) {
        const QImage image = m_pendingFrame;
        m_pendingFrame = QImage();
        if (image.isNull())
            return;
        const QString path = url.toLocalFile();
        QPointer<Backend> self(this);
        (void)QtConcurrent::run([self, image, path] {
            QEventLoopLocker keepRunning;
            QString error;
            const bool saved = frames::savePng(image, path, &error);
            QMetaObject::invokeMethod(qApp, [self, saved, path, error] {
                if (!self)
                    return;
                emit self->notice(saved ? tr("Saved %1").arg(QFileInfo(path).fileName())
                                        : tr("Could not save the frame: %1").arg(error));
            }, Qt::QueuedConnection);
        });
    });

    const QJsonObject preferences = m_positions.preferences();
    m_volume = std::clamp(preferences.value("volume").toDouble(1.0), 0.0, 1.0);
    m_muted = preferences.value("muted").toBool(false);

    m_previewTimer.setSingleShot(true);
    m_previewTimer.setInterval(m_options.previewDelayMs);
    connect(&m_previewTimer, &QTimer::timeout, this, &Backend::startPreviews);
    // The player tells what it knows about a file in its own order, so the
    // previews start when the last of what they need arrives.
    for (auto signal : {&Backend::seekableChanged, &Backend::hasVideoChanged, &Backend::durationChanged,
                        &Backend::videoSizeChanged, &Backend::infoChanged})
        connect(this, signal, this, [this] {
            if (m_previewsWanted && !m_previewTimer.isActive())
                m_previewTimer.start();
        });
    connect(&m_previews, &frames::Previews::ready, this,
            [this](const QString &path, const frames::PreviewLayout &layout) {
                m_previewSource = QUrl::fromLocalFile(path);
                m_previewLayout = layout;
                emit previewsChanged();
            });
}

Backend::~Backend() {
    m_previews.cancel();
    if (m_player) {
        // No frame may reach a backend that is going: detach the sink before
        // the decoder's thread can call into it again.
        m_sink->disconnect(this);
        m_player->disconnect(this);
        m_player->setVideoSink(nullptr);
        m_player->stop();
    }
}

void Backend::setLanguage(const QString &language) {
    m_language = language.section('-', 0, 0);
    if (m_tracksReady)
        buildInfo();
}

QString Backend::fileName() const {
    if (m_source.isEmpty())
        return {};
    return m_source.isLocalFile() ? QFileInfo(m_source.toLocalFile()).fileName() : m_source.toString();
}

QString Backend::title() const {
    return !m_title.isEmpty() ? m_title : fileName();
}

bool Backend::playing() const {
    return m_player && m_player->playbackState() == QMediaPlayer::PlayingState;
}

bool Backend::loading() const {
    return m_player && m_player->mediaStatus() == QMediaPlayer::LoadingMedia;
}

bool Backend::buffering() const {
    return m_player && (m_player->mediaStatus() == QMediaPlayer::BufferingMedia
                        || m_player->mediaStatus() == QMediaPlayer::StalledMedia);
}

QString Backend::nextName() const {
    if (m_index < 0 || m_index + 1 >= m_playlist.size())
        return {};
    const QString next = m_playlist.at(m_index + 1);
    const QUrl url = QUrl::fromUserInput(next, QDir::currentPath(), QUrl::AssumeLocalFile);
    return url.isLocalFile() ? QFileInfo(url.toLocalFile()).fileName() : next;
}

bool Backend::canTrim() const {
    return !QStandardPaths::findExecutable("omacut").isEmpty();
}

QString Backend::subtitleLabel() const {
    if (m_subtitleChoice < 0 || m_subtitleChoice >= m_subtitleChoices.size())
        return {};
    return m_subtitleChoices.at(m_subtitleChoice).label;
}

QString Backend::audioLabel() const {
    if (m_audioTrack < 0 || m_audioTrack >= m_audioLabels.size())
        return {};
    return m_audioLabels.at(m_audioTrack);
}

void Backend::ensurePlayer() {
    if (m_player)
        return;
    m_player = new QMediaPlayer(this);
    if (m_options.audio) {
        m_audio = new QAudioOutput(this);
        m_player->setAudioOutput(m_audio);
        applyVolume();
    }
    // The player draws into a sink of ours, which hands each frame on to the
    // window's and keeps the subtitle text, so the window draws embedded and
    // sidecar subtitles in one style and the frame on screen is at hand.
    m_sink = new QVideoSink(this);
    m_player->setVideoSink(m_sink);
    // Frames are handed on from the decoder's thread, as Qt's own player
    // hands them to a VideoOutput, so a busy GUI thread never makes the
    // picture stutter. The sink shares the window's GPU context, so a frame
    // decoded on the GPU stays there.
    connect(
        m_sink, &QVideoSink::videoFrameChanged, this,
        [this](const QVideoFrame &frame) {
            // The player clears the sink at the end of a file; the last frame
            // stays on screen instead, and a new file clears it in load().
            if (!frame.isValid())
                return;
            QSize size;
            {
                QMutexLocker locker(&m_frameLock);
                m_frame = frame;
                if (frame.size() != m_frameSize)
                    size = m_frameSize = frame.size();
            }
            // Its own lock, never held with the frame's: the window's sink
            // may wait on the GUI thread, which takes the frame's lock to
            // read the frame on screen.
            {
                QMutexLocker locker(&m_outputLock);
                if (m_output) {
                    if (m_sink->rhi() != m_output->rhi())
                        m_sink->setRhi(m_output->rhi());
                    m_output->setVideoFrame(frame);
                }
            }
            if (size.isValid())
                QMetaObject::invokeMethod(this, [this, size] {
                    if (size != m_videoSize) {
                        m_videoSize = size;
                        emit videoSizeChanged();
                    }
                }, Qt::QueuedConnection);
        },
        Qt::DirectConnection);
    connect(m_sink, &QVideoSink::subtitleTextChanged, this, [this](const QString &text) {
        m_embeddedText = text;
        updateSubtitleText();
    });

    connect(m_player, &QMediaPlayer::playbackStateChanged, this, [this] { emit playingChanged(); });
    connect(m_player, &QMediaPlayer::mediaStatusChanged, this, &Backend::onStatus);
    connect(m_player, &QMediaPlayer::positionChanged, this, [this](qint64 ms) {
        if (m_ended && m_player->mediaStatus() == QMediaPlayer::EndOfMedia)
            return;
        m_position = ms;
        emit positionChanged();
        updateSubtitleText();
        updateChapter();
    });
    connect(m_player, &QMediaPlayer::durationChanged, this, [this](qint64 ms) {
        m_duration = ms;
        emit durationChanged();
    });
    connect(m_player, &QMediaPlayer::seekableChanged, this, [this](bool seekable) {
        m_seekable = seekable;
        emit seekableChanged();
    });
    connect(m_player, &QMediaPlayer::hasVideoChanged, this, [this](bool video) {
        m_hasVideo = video;
        emit hasVideoChanged();
    });
    connect(m_player, &QMediaPlayer::tracksChanged, this, &Backend::onTracks);
    connect(m_player, &QMediaPlayer::metaDataChanged, this, &Backend::updateMetadata);
    connect(m_player, &QMediaPlayer::errorOccurred, this, [this](QMediaPlayer::Error error, const QString &message) {
        if (error == QMediaPlayer::NoError)
            return;
        setError(message.isEmpty() ? tr("This file could not be played.") : message);
    });
    if (m_player->pitchCompensationAvailability() != QMediaPlayer::PitchCompensationAvailability::Unavailable)
        m_player->setPitchCompensation(true);
    emit playerReadyChanged();
}

void Backend::setVideoSink(QVideoSink *sink) {
    const QVideoFrame frame = currentFrame();
    QMutexLocker locker(&m_outputLock);
    if (m_output)
        disconnect(m_output, &QObject::destroyed, this, nullptr);
    m_output = sink;
    if (!m_output)
        return;
    // The decoder's thread must never hand a frame to a VideoOutput that has
    // gone; close() stops the player before the window goes, and this covers
    // a window that goes first anyway.
    connect(m_output, &QObject::destroyed, this, [this] {
        QMutexLocker locker(&m_outputLock);
        m_output = nullptr;
    }, Qt::DirectConnection);
    if (frame.isValid())
        m_output->setVideoFrame(frame);
}

void Backend::open(const QStringList &arguments) {
    QStringList items;
    for (const QString &argument : arguments) {
        const QUrl url = QUrl::fromUserInput(argument, QDir::currentPath(), QUrl::AssumeLocalFile);
        items << (url.isLocalFile() ? QFileInfo(url.toLocalFile()).absoluteFilePath() : url.toString());
    }
    if (items.isEmpty())
        return;
    rememberPosition();
    int index = 0;
    if (items.size() == 1 && QFileInfo(items.first()).isFile()) {
        m_playlist = playlist::siblings(items.first());
        index = std::max<int>(0, m_playlist.indexOf(items.first()));
    } else {
        m_playlist = items;
    }
    m_index = -1;
    load(index);
}

void Backend::openUrls(const QList<QUrl> &urls) {
    QStringList items;
    for (const QUrl &url : urls)
        items << (url.isLocalFile() ? url.toLocalFile() : url.toString());
    open(items);
}

void Backend::openDialog() {
    m_picker->openMedia();
}

void Backend::load(int index) {
    if (index < 0 || index >= m_playlist.size())
        return;
    ensurePlayer();
    m_previews.cancel();
    m_previewTimer.stop();
    m_previewsWanted = false;
    ++m_generation;
    m_index = index;
    const QString item = m_playlist.at(index);
    m_source = QUrl::fromUserInput(item, QDir::currentPath(), QUrl::AssumeLocalFile);

    m_title.clear();
    m_artist.clear();
    m_coverArt.clear();
    m_summary.clear();
    m_error.clear();
    m_position = 0;
    m_ended = false;
    m_loaded = false;
    m_hasVideo = false;
    {
        QMutexLocker locker(&m_frameLock);
        m_frame = QVideoFrame();
        m_frameSize = QSize();
    }
    {
        QMutexLocker locker(&m_outputLock);
        if (m_output)
            m_output->setVideoFrame(QVideoFrame());
    }
    m_videoSize = QSize();
    m_probe = probe::Info();
    m_probed = false;
    m_tracksReady = false;
    m_tracksChosen = false;
    m_pendingResume = -1;
    m_resumedAt = -1;
    m_sidecars.clear();
    m_subtitleChoices.clear();
    m_subtitleChoice = -1;
    m_cues.clear();
    m_embeddedText.clear();
    m_subtitleText.clear();
    m_audioLabels.clear();
    m_audioLanguages.clear();
    m_audioTrack = -1;
    m_chapters.clear();
    m_chapterTitle.clear();
    m_info.clear();
    m_previewSource.clear();
    m_previewLayout = frames::PreviewLayout();

    if (m_source.isLocalFile()) {
        const QString path = m_source.toLocalFile();
        if (!QFileInfo(path).isReadable()) {
            m_player->setSource(QUrl());
            emit mediaChanged();
            emit playlistChanged();
            setError(QFileInfo::exists(path) ? tr("%1 can't be read.").arg(QFileInfo(path).fileName())
                                             : tr("%1 doesn't exist.").arg(QFileInfo(path).fileName()));
            return;
        }
        m_pendingResume = m_positions.lookup(path);
        m_sidecars = subtitles::find(path);
        // ffprobe runs beside the player's own opening of the file, so
        // chapters and track details arrive without holding up the picture.
        const quint64 generation = m_generation;
        QPointer<Backend> self(this);
        (void)QtConcurrent::run([self, path, generation] {
            QEventLoopLocker keepRunning;
            const probe::Info info = probe::run(path);
            QMetaObject::invokeMethod(qApp, [self, info, generation] {
                if (self)
                    self->onProbed(info, generation);
            }, Qt::QueuedConnection);
        });
    } else {
        m_probed = true;
    }

    // Setting the source the player already has does nothing, so reopening
    // the same file clears it first.
    if (m_player->source() == m_source)
        m_player->setSource(QUrl());
    m_player->setSource(m_source);
    m_player->setPlaybackRate(m_rate);
    emit mediaChanged();
    emit metadataChanged();
    emit errorChanged();
    emit positionChanged();
    emit statusChanged();
    emit subtitleTextChanged();
    emit tracksChanged();
    emit chapterChanged();
    emit infoChanged();
    emit resumedAtChanged();
    emit playlistChanged();
    emit previewsChanged();
    emit hasVideoChanged();
    m_player->play();
}

void Backend::setError(const QString &error) {
    if (m_error == error)
        return;
    m_error = error;
    emit errorChanged();
}

void Backend::onStatus() {
    const auto status = m_player->mediaStatus();
    if (status == QMediaPlayer::LoadedMedia || status == QMediaPlayer::BufferedMedia) {
        if (!m_loaded) {
            m_loaded = true;
            if (m_pendingResume > 0 && m_player->isSeekable()) {
                m_resumedAt = m_pendingResume;
                // A few seconds back, so the sentence that was interrupted
                // starts again.
                m_player->setPosition(std::max<qint64>(0, m_pendingResume - 3000));
                emit resumedAtChanged();
            }
            m_pendingResume = -1;
            m_previewsWanted = m_options.previews && m_source.isLocalFile();
            if (m_previewsWanted)
                m_previewTimer.start();
        }
    } else if (status == QMediaPlayer::EndOfMedia) {
        m_ended = true;
        m_position = m_duration;
        emit positionChanged();
        if (m_source.isLocalFile())
            m_positions.forget(m_source.toLocalFile());
    } else if (status == QMediaPlayer::InvalidMedia) {
        if (m_error.isEmpty())
            setError(tr("This file could not be played."));
    }
    emit statusChanged();
}

void Backend::onProbed(const probe::Info &info, quint64 generation) {
    if (generation != m_generation)
        return;
    m_probe = info;
    m_probed = true;
    m_chapters.clear();
    for (const probe::Chapter &chapter : m_probe.chapters)
        m_chapters << QVariantMap{{"start", qint64(chapter.start * 1000)}, {"title", chapter.title}};
    updateChapter();
    if (m_title.isEmpty() && !m_probe.title.isEmpty())
        m_title = m_probe.title;
    if (m_artist.isEmpty() && !m_probe.artist.isEmpty())
        m_artist = m_probe.artist;
    buildInfo();
    emit metadataChanged();
    emit infoChanged();
    chooseTracks();
}

void Backend::onTracks() {
    m_tracksReady = true;
    chooseTracks();
}

void Backend::chooseTracks() {
    if (!m_player || !m_tracksReady || !m_probed || m_tracksChosen)
        return;
    m_tracksChosen = true;

    // Audio: Qt's tracks named by Qt's metadata, with ffprobe's details,
    // matched by order within the type.
    const QList<QMediaMetaData> audio = m_player->audioTracks();
    const QList<probe::Stream> probedAudio = m_probe.ofType("audio");
    m_audioLabels.clear();
    m_audioLanguages.clear();
    for (int i = 0; i < audio.size(); ++i) {
        QString language = languageOf(audio.at(i));
        QString title = audio.at(i).value(QMediaMetaData::Title).toString();
        QString channels;
        if (i < probedAudio.size()) {
            if (language.isEmpty())
                language = probe::shortLanguage(probedAudio.at(i).language);
            if (title.isEmpty())
                title = probedAudio.at(i).title;
            channels = channelName(probedAudio.at(i));
        }
        QString label = join({probe::languageName(language), title, channels});
        if (label.isEmpty())
            label = tr("Track %1").arg(i + 1);
        m_audioLabels << label;
        m_audioLanguages << language;
    }

    // Subtitles: only text tracks, which Qt's subtitle path can show, then
    // the files beside the video.
    const QList<QMediaMetaData> embedded = m_player->subtitleTracks();
    const QList<probe::Stream> probedSubtitles = m_probe.ofType("subtitle");
    m_subtitleChoices.clear();
    for (int i = 0; i < embedded.size(); ++i) {
        // Without ffprobe's word on the codec, a bitmap track can't be ruled
        // out, so none are offered.
        if (i >= probedSubtitles.size() || !probedSubtitles.at(i).textSubtitle())
            continue;
        SubtitleChoice choice;
        choice.embedded = i;
        choice.language = languageOf(embedded.at(i));
        if (choice.language.isEmpty())
            choice.language = probe::shortLanguage(probedSubtitles.at(i).language);
        QString title = embedded.at(i).value(QMediaMetaData::Title).toString();
        if (title.isEmpty())
            title = probedSubtitles.at(i).title;
        choice.label = join({probe::languageName(choice.language), title});
        if (choice.label.isEmpty())
            choice.label = tr("Subtitles %1").arg(i + 1);
        m_subtitleChoices << choice;
    }
    for (int i = 0; i < m_sidecars.size(); ++i) {
        SubtitleChoice choice;
        choice.sidecar = i;
        choice.language = probe::shortLanguage(m_sidecars.at(i).language);
        choice.label = m_sidecars.at(i).label;
        m_subtitleChoices << choice;
    }

    const QJsonObject preferences = m_positions.preferences();
    const QString audioPreference = preferences.value("audioLanguage").toString();
    int track = m_player->activeAudioTrack();
    for (const QString &wanted : {audioPreference, m_language}) {
        if (wanted.isEmpty())
            continue;
        const int found = int(m_audioLanguages.indexOf(wanted));
        if (found >= 0) {
            track = found;
            break;
        }
    }
    if (!m_audioLabels.isEmpty())
        applyAudio(std::clamp(track, 0, int(m_audioLabels.size()) - 1), false);

    // Subtitles start off, unless they were on last time in a language this
    // file has, or the film speaks another language and has the reader's.
    const QString subtitlePreference = preferences.value("subtitleLanguage").toString();
    int subtitle = -1;
    auto find = [&](const QString &language) {
        for (int i = 0; i < m_subtitleChoices.size(); ++i)
            if (m_subtitleChoices.at(i).language == language)
                return i;
        return -1;
    };
    if (!subtitlePreference.isEmpty() && subtitlePreference != "off") {
        subtitle = find(subtitlePreference);
    } else if (subtitlePreference.isEmpty()) {
        const QString spoken = m_audioTrack >= 0 ? m_audioLanguages.value(m_audioTrack) : QString();
        if (!spoken.isEmpty() && spoken != m_language)
            subtitle = find(m_language);
    }
    applySubtitle(subtitle, false);
    buildInfo();
    emit tracksChanged();
    emit infoChanged();
}

void Backend::applyAudio(int track, bool remember) {
    if (!m_player || track < 0 || track >= m_audioLabels.size())
        return;
    m_audioTrack = track;
    if (m_player->activeAudioTrack() != track)
        m_player->setActiveAudioTrack(track);
    if (remember && !m_audioLanguages.at(track).isEmpty())
        m_positions.setPreference("audioLanguage", m_audioLanguages.at(track));
    emit tracksChanged();
}

void Backend::applySubtitle(int index, bool remember) {
    if (!m_player)
        return;
    m_subtitleChoice = index >= 0 && index < m_subtitleChoices.size() ? index : -1;
    m_cues.clear();
    m_embeddedText.clear();
    if (m_subtitleChoice < 0) {
        m_player->setActiveSubtitleTrack(-1);
        if (remember)
            m_positions.setPreference("subtitleLanguage", "off");
    } else {
        const SubtitleChoice &choice = m_subtitleChoices.at(m_subtitleChoice);
        // An embedded track shows through Qt at once, which only shows a cue
        // when playback reaches its start; its cues are read out in the
        // background too, and once they are in, Showtime draws them itself,
        // so a seek into the middle of a line shows the line.
        m_player->setActiveSubtitleTrack(choice.embedded);
        loadCues(m_subtitleChoice);
        if (remember && !choice.language.isEmpty())
            m_positions.setPreference("subtitleLanguage", choice.language);
    }
    updateSubtitleText();
    emit tracksChanged();
}

void Backend::loadCues(int index) {
    SubtitleChoice &choice = m_subtitleChoices[index];
    if (choice.cuesLoaded) {
        m_cues = choice.cues;
        if (choice.embedded >= 0)
            m_player->setActiveSubtitleTrack(-1);
        return;
    }
    const bool embedded = choice.embedded >= 0;
    const QString path = embedded ? m_source.toLocalFile() : m_sidecars.at(choice.sidecar).path;
    // ffmpeg's 0:s:N counts subtitle streams the way Qt numbers its tracks.
    const int stream = embedded ? choice.embedded : 0;
    if (path.isEmpty())
        return;
    const quint64 generation = m_generation;
    QPointer<Backend> self(this);
    (void)QtConcurrent::run([self, path, stream, generation, index, embedded] {
        QEventLoopLocker keepRunning;
        QString error;
        const QList<subtitles::Cue> cues = subtitles::load(path, stream, &error);
        QMetaObject::invokeMethod(qApp, [self, cues, error, generation, index, embedded] {
            if (!self || self->m_generation != generation || index >= self->m_subtitleChoices.size())
                return;
            SubtitleChoice &choice = self->m_subtitleChoices[index];
            // An embedded track whose cues couldn't be read stays with Qt.
            if (embedded && cues.isEmpty())
                return;
            choice.cues = cues;
            choice.cuesLoaded = true;
            if (self->m_subtitleChoice != index)
                return;
            self->m_cues = cues;
            if (embedded)
                self->m_player->setActiveSubtitleTrack(-1);
            if (cues.isEmpty() && !error.isEmpty())
                emit self->notice(error);
            self->updateSubtitleText();
        }, Qt::QueuedConnection);
    });
}


void Backend::updateSubtitleText() {
    QString text;
    if (m_subtitleChoice >= 0 && m_subtitleChoice < m_subtitleChoices.size()) {
        const SubtitleChoice &choice = m_subtitleChoices.at(m_subtitleChoice);
        text = choice.sidecar >= 0 || choice.cuesLoaded ? subtitles::at(m_cues, m_position) : m_embeddedText;
    }
    if (text != m_subtitleText) {
        m_subtitleText = text;
        emit subtitleTextChanged();
    }
}

void Backend::updateChapter() {
    const QString title = chapterAt(m_position);
    if (title != m_chapterTitle) {
        m_chapterTitle = title;
        emit chapterChanged();
    }
}

QString Backend::chapterAt(qint64 ms) const {
    QString title;
    for (const QVariant &value : m_chapters) {
        const QVariantMap chapter = value.toMap();
        if (chapter.value("start").toLongLong() <= ms)
            title = chapter.value("title").toString();
        else
            break;
    }
    return title;
}

void Backend::updateMetadata() {
    if (!m_player)
        return;
    const QMediaMetaData metadata = m_player->metaData();
    const QString title = metadata.stringValue(QMediaMetaData::Title);
    if (!title.isEmpty())
        m_title = title;
    QString artist = metadata.stringValue(QMediaMetaData::ContributingArtist);
    if (artist.isEmpty())
        artist = metadata.stringValue(QMediaMetaData::AlbumArtist);
    if (!artist.isEmpty())
        m_artist = artist;
    QImage cover = metadata.value(QMediaMetaData::CoverArtImage).value<QImage>();
    if (!cover.isNull()) {
        // Small enough to hand QML as a data URL, which needs no provider
        // whose lifetime has to outlast the engine's.
        if (cover.width() > 1024 || cover.height() > 1024)
            cover = cover.scaled(1024, 1024, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        QByteArray bytes;
        QBuffer buffer(&bytes);
        buffer.open(QIODevice::WriteOnly);
        cover.save(&buffer, "JPG", 90);
        m_coverArt = "data:image/jpeg;base64," + QString::fromLatin1(bytes.toBase64());
    }
    const QSize resolution = metadata.value(QMediaMetaData::Resolution).toSize();
    if (resolution.isValid() && m_videoSize.isEmpty() && m_player->hasVideo()) {
        m_videoSize = resolution;
        emit videoSizeChanged();
    }
    emit metadataChanged();
}

void Backend::buildInfo() {
    m_info.clear();
    auto row = [this](const QString &label, const QString &value) {
        if (!value.isEmpty())
            m_info << QVariantMap{{"label", label}, {"value", value}};
    };
    const QLocale locale;
    row(tr("File"), fileName());
    if (!m_probe.ok()) {
        row(tr("Problem"), m_probe.error);
        return;
    }
    row(tr("Title"), m_probe.title);
    row(tr("Container"), m_probe.format);
    if (m_probe.duration > 0)
        row(tr("Duration"), clock(qint64(m_probe.duration * 1000)));
    if (m_probe.size > 0)
        row(tr("Size"), locale.formattedDataSize(m_probe.size));
    if (m_probe.bitRate > 0)
        row(tr("Bit rate"), tr("%1 kb/s").arg(locale.toString(m_probe.bitRate / 1000)));

    QStringList summary;
    int audioIndex = 0, subtitleIndex = 0;
    for (const probe::Stream &stream : m_probe.streams) {
        if (stream.type == "video" && !stream.attachedPicture) {
            QStringList parts{codecName(stream)};
            if (stream.width > 0)
                parts << QString("%1 × %2").arg(stream.width).arg(stream.height);
            if (stream.frameRate > 0)
                parts << tr("%1 fps").arg(locale.toString(stream.frameRate, 'f', stream.frameRate == std::floor(stream.frameRate) ? 0 : 3));
            row(tr("Video"), join(parts));
            if (summary.isEmpty())
                summary << (stream.width > 0 ? QString("%1 × %2").arg(stream.width).arg(stream.height) : QString())
                        << codecName(stream);
        } else if (stream.type == "audio") {
            ++audioIndex;
            row(tr("Audio %1").arg(audioIndex),
                join({probe::languageName(stream.language), stream.title, codecName(stream), channelName(stream)}));
            if (audioIndex == 1 && summary.size() < 3)
                summary << channelName(stream);
        } else if (stream.type == "subtitle") {
            ++subtitleIndex;
            row(tr("Subtitles %1").arg(subtitleIndex),
                join({probe::languageName(stream.language), stream.title, codecName(stream),
                      stream.textSubtitle() ? QString() : tr("not supported")}));
        }
    }
    for (const subtitles::Sidecar &sidecar : m_sidecars)
        row(tr("Subtitle file"), QFileInfo(sidecar.path).fileName());
    if (!m_probe.chapters.isEmpty())
        row(tr("Chapters"), QString::number(m_probe.chapters.size()));
    m_summary = join(summary);
}

void Backend::applyVolume() {
    if (!m_audio)
        return;
    // Steps that sound even: the slider is perceptual, the output linear.
    m_audio->setVolume(float(QtAudio::convertVolume(m_volume, QtAudio::LogarithmicVolumeScale,
                                                    QtAudio::LinearVolumeScale)));
    m_audio->setMuted(m_muted);
}

void Backend::togglePlay() {
    if (!m_player)
        return;
    if (playing())
        pause();
    else
        play();
}

void Backend::play() {
    if (!m_player)
        return;
    if (m_ended) {
        m_ended = false;
        emit statusChanged();
        m_player->setPosition(0);
    }
    m_player->play();
}

void Backend::pause() {
    if (m_player)
        m_player->pause();
}

void Backend::stop() {
    if (!m_player)
        return;
    rememberPosition();
    m_player->pause();
    m_player->setPosition(0);
}

void Backend::setPosition(qint64 ms) {
    if (!m_player || !m_seekable)
        return;
    const qint64 limit = m_duration > 0 ? m_duration : ms;
    ms = std::clamp<qint64>(ms, 0, limit);
    if (m_ended && ms < m_duration) {
        m_ended = false;
        emit statusChanged();
    }
    m_player->setPosition(ms);
    m_position = ms;
    emit positionChanged();
    emit seeked(ms);
    updateSubtitleText();
    updateChapter();
}

void Backend::seek(qint64 ms) {
    setPosition(ms);
}

void Backend::seekBy(qint64 ms) {
    setPosition(m_position + ms);
}

void Backend::seekFraction(double fraction) {
    if (m_duration > 0)
        setPosition(qint64(std::clamp(fraction, 0.0, 1.0) * m_duration));
}

void Backend::startOver() {
    setPosition(0);
    if (m_resumedAt >= 0) {
        m_resumedAt = -1;
        emit resumedAtChanged();
    }
}

void Backend::stepFrame(int direction) {
    if (!m_player || !m_seekable || !m_hasVideo)
        return;
    pause();
    // Qt has no frame step, so this seeks by the shown frame's own
    // timestamps: just past its end, or just before its start, which is right
    // for variable frame rates too.
    const QVideoFrame frame = currentFrame();
    if (frame.isValid() && frame.startTime() >= 0 && frame.endTime() > frame.startTime()) {
        if (direction > 0)
            setPosition(frame.endTime() / 1000 + 1);
        else
            setPosition(std::max<qint64>(0, frame.startTime() / 1000 - 1));
        return;
    }
    const probe::Stream *video = m_probe.video();
    const qint64 frameMs = video && video->frameRate > 0 ? qint64(std::ceil(1000.0 / video->frameRate)) : 40;
    setPosition(m_position + direction * frameMs);
}

void Backend::changeRate(double delta) {
    m_rate = std::clamp(std::round((m_rate + delta) * 4) / 4, 0.25, 4.0);
    if (m_player)
        m_player->setPlaybackRate(m_rate);
    emit rateChanged();
}

void Backend::resetRate() {
    m_rate = 1.0;
    if (m_player)
        m_player->setPlaybackRate(m_rate);
    emit rateChanged();
}

void Backend::setVolume(double volume) {
    m_volume = std::clamp(volume, 0.0, 1.0);
    if (m_muted && m_volume > 0)
        m_muted = false;
    applyVolume();
    m_positions.setPreference("volume", std::round(m_volume * 100) / 100);
    m_positions.setPreference("muted", m_muted);
    emit volumeChanged();
}

void Backend::changeVolume(double delta) {
    setVolume(std::round((m_volume + delta) * 100) / 100);
}

void Backend::toggleMute() {
    m_muted = !m_muted;
    applyVolume();
    m_positions.setPreference("muted", m_muted);
    emit volumeChanged();
}

void Backend::cycleSubtitles() {
    if (m_subtitleChoices.isEmpty())
        return;
    // Off, then each choice, then off again.
    const int next = m_subtitleChoice + 1 >= m_subtitleChoices.size() ? -1 : m_subtitleChoice + 1;
    applySubtitle(next, true);
}

void Backend::cycleAudio() {
    if (m_audioLabels.size() < 2)
        return;
    applyAudio((m_audioTrack + 1) % int(m_audioLabels.size()), true);
}

bool Backend::next() {
    if (m_index + 1 >= m_playlist.size())
        return false;
    rememberPosition();
    load(m_index + 1);
    return true;
}

bool Backend::previous() {
    if (m_index <= 0)
        return false;
    rememberPosition();
    load(m_index - 1);
    return true;
}

void Backend::jumpChapter(int direction) {
    if (m_chapters.isEmpty())
        return;
    qint64 target = -1;
    if (direction > 0) {
        for (const QVariant &value : m_chapters) {
            const qint64 start = value.toMap().value("start").toLongLong();
            if (start > m_position + 500) {
                target = start;
                break;
            }
        }
    } else {
        // Back to the start of this chapter, or the one before when already
        // within its first few seconds, as a CD player's previous does.
        for (auto it = m_chapters.crbegin(); it != m_chapters.crend(); ++it) {
            const qint64 start = it->toMap().value("start").toLongLong();
            if (start < m_position - 3000) {
                target = start;
                break;
            }
        }
        if (target < 0)
            target = 0;
    }
    if (target >= 0)
        setPosition(target);
}

void Backend::copyFrame() {
    // The frame on screen now, latched before anything else can move it.
    QVideoFrame frame = currentFrame();
    if (!frame.isValid())
        return;
    const QImage image = frame.toImage();
    if (image.isNull()) {
        emit notice(tr("Could not copy this frame."));
        return;
    }
    const QString at = clock(m_position);
    QPointer<Backend> self(this);
    (void)QtConcurrent::run([self, image, at] {
        QEventLoopLocker keepRunning;
        QByteArray png;
        QBuffer buffer(&png);
        buffer.open(QIODevice::WriteOnly);
        image.save(&buffer, "PNG");
        const bool copied = frames::copyWithWlCopy(png);
        QMetaObject::invokeMethod(qApp, [self, image, copied, at] {
            // Without wl-copy, Qt's clipboard holds it while Showtime runs.
            if (!copied)
                QGuiApplication::clipboard()->setImage(image);
            if (self)
                emit self->notice(tr("Copied the frame at %1").arg(at));
        }, Qt::QueuedConnection);
    });
}

void Backend::saveFrame() {
    QVideoFrame frame = currentFrame();
    if (!frame.isValid())
        return;
    const QImage image = frame.toImage();
    if (image.isNull()) {
        emit notice(tr("Could not save this frame."));
        return;
    }
    m_pendingFrame = image;
    QString folder = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    if (!QDir(folder).exists())
        folder = QDir::homePath();
    const QString base = m_source.isLocalFile() ? QFileInfo(m_source.toLocalFile()).completeBaseName() : "showtime";
    const QString name = QString("%1-%2.png").arg(base, clock(m_position).replace(':', '-'));
    m_picker->saveImage(QUrl::fromLocalFile(folder + "/" + name));
}

bool Backend::takeFirstRunHint() {
    if (m_positions.preferences().value("hinted").toBool())
        return false;
    m_positions.setPreference("hinted", true);
    return true;
}

void Backend::trimInOmacut() {
    const QString omacut = QStandardPaths::findExecutable("omacut");
    if (omacut.isEmpty() || !m_source.isLocalFile())
        return;
    pause();
    if (QProcess::startDetached(omacut, {m_source.toLocalFile()}))
        emit notice(tr("Opened in Omacut"));
    else
        emit notice(tr("Omacut could not be started."));
}

void Backend::rememberPosition() {
    if (!m_player || !m_source.isLocalFile() || !m_loaded || !m_error.isEmpty())
        return;
    if (m_ended)
        m_positions.forget(m_source.toLocalFile());
    else
        m_positions.update(m_source.toLocalFile(), m_position, m_duration);
}

void Backend::close() {
    rememberPosition();
    m_previews.cancel();
    m_previewTimer.stop();
    if (m_player)
        m_player->stop();
}

void Backend::startPreviews() {
    if (!m_previewsWanted || !m_source.isLocalFile() || !m_seekable || !m_hasVideo || m_duration <= 0)
        return;
    QSize size = m_videoSize;
    if (const probe::Stream *video = m_probe.video(); size.isEmpty() && video)
        size = QSize(video->width, video->height);
    if (size.isEmpty())
        return;
    m_previewsWanted = false;
    m_previews.start(m_source.toLocalFile(), m_duration / 1000.0, size);
}
