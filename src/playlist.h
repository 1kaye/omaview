#pragma once

#include <QString>
#include <QStringList>

namespace playlist {

// What a file is, by its extension.
enum class Kind { None, Picture, Video, Audio };
Kind kind(const QString &path);

// Whether a file looks like something Omaview shows or plays.
bool playable(const QString &path);
bool audioOnly(const QString &path);
bool picture(const QString &path);

// The files in a file's folder that belong with it, the file among them, in
// natural order (2 before 10). Videos go with everything, but songs and
// pictures don't mix: a folder of photos doesn't play its voice memos, and an
// album doesn't show its cover.jpg as a track.
QStringList siblings(const QString &path);

// Natural, case-insensitive order.
void sort(QStringList &paths);

}  // namespace playlist
