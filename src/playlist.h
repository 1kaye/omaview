#pragma once

#include <QString>
#include <QStringList>

namespace playlist {

// Whether a file looks like something Showtime plays, by its extension.
bool playable(const QString &path);
bool audioOnly(const QString &path);

// The playable files in a file's folder, the file among them, in natural
// order (2 before 10).
QStringList siblings(const QString &path);

// Natural, case-insensitive order.
void sort(QStringList &paths);

}  // namespace playlist
