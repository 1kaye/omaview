#include "cli.h"

#include <QCommandLineParser>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <cstdio>

#include "frames.h"
#include "positions.h"
#include "probe.h"

namespace {

void print(FILE *stream, const QString &text) { fprintf(stream, "%s\n", qPrintable(text)); }
void print(const QJsonObject &object) { fprintf(stdout, "%s", QJsonDocument(object).toJson().constData()); }

int fail(const QString &message) {
    print(stderr, message);
    return 1;
}

// A parser for one command, with --help and its own description.
struct Command {
    QCommandLineParser parser;
    QCommandLineOption json{"json", "Print JSON instead of text."};
    Command(const QString &name, const QString &description, bool withJson = true) {
        parser.setApplicationDescription(description);
        parser.addHelpOption();
        if (withJson)
            parser.addOption(json);
        parser.addPositionalArgument(name, "", name);
    }
    QString argument(int i) const { return parser.positionalArguments().value(i); }
};

QString clock(double seconds) {
    const qint64 total = qint64(seconds);
    const QString fraction = QString::number(seconds - total, 'f', 3).mid(1);
    return QString("%1:%2:%3%4")
        .arg(total / 3600)
        .arg(total / 60 % 60, 2, 10, QChar('0'))
        .arg(total % 60, 2, 10, QChar('0'))
        .arg(fraction);
}

int info(const QStringList &arguments) {
    Command command("info", "Duration, streams, chapters, and where Showtime would resume.");
    command.parser.addPositionalArgument("file", "The video or music file.");
    command.parser.process(arguments);
    const QString path = command.argument(1);
    if (path.isEmpty())
        return fail("Usage: showtime info <file> [--json]");
    if (!QFileInfo(path).isFile())
        return fail("No such file: " + path);
    const probe::Info info = probe::run(QFileInfo(path).absoluteFilePath());
    if (!info.ok())
        return fail(info.error);
    const qint64 resume = Positions().lookup(QFileInfo(path).absoluteFilePath());

    if (command.parser.isSet(command.json)) {
        QJsonObject object = info.toJson();
        object.insert("resume", resume >= 0 ? QJsonValue(resume / 1000.0) : QJsonValue());
        print(object);
        return 0;
    }
    print(stdout, QFileInfo(path).fileName());
    if (!info.title.isEmpty())
        print(stdout, "  Title     " + info.title);
    print(stdout, "  Format    " + info.format);
    print(stdout, "  Duration  " + clock(info.duration));
    for (const probe::Stream &stream : info.streams) {
        QStringList parts{stream.codec};
        if (stream.type == "video" && stream.width > 0)
            parts << QString("%1x%2").arg(stream.width).arg(stream.height)
                  << QString::number(stream.frameRate, 'f', 3) + " fps";
        if (stream.type == "audio" && stream.channels > 0)
            parts << (stream.channelLayout.isEmpty() ? QString::number(stream.channels) + " channels"
                                                     : stream.channelLayout);
        if (!stream.language.isEmpty())
            parts << stream.language;
        if (!stream.title.isEmpty())
            parts << '"' + stream.title + '"';
        if (stream.type == "subtitle" && !stream.textSubtitle())
            parts << "(bitmap, not shown)";
        if (stream.attachedPicture)
            parts << "(cover art)";
        print(stdout, QString("  #%1 %2  %3").arg(stream.index).arg(stream.type, -8).arg(parts.join(", ")));
    }
    for (const probe::Chapter &chapter : info.chapters)
        print(stdout, "  Chapter   " + clock(chapter.start) + "  " + chapter.title);
    if (resume >= 0)
        print(stdout, "  Resumes   " + clock(resume / 1000.0));
    return 0;
}

int frame(const QStringList &arguments) {
    Command command("frame", "Write the exact frame at a time to a PNG.");
    command.parser.addPositionalArgument("file", "The video file.");
    QCommandLineOption at({"a", "at"}, "The time: seconds, mm:ss or hh:mm:ss.mmm (default 0).", "time", "0");
    QCommandLineOption output({"o", "output"}, "The PNG to write.", "file");
    command.parser.addOption(at);
    command.parser.addOption(output);
    command.parser.process(arguments);
    const QString path = command.argument(1);
    if (path.isEmpty() || !command.parser.isSet(output))
        return fail("Usage: showtime frame <file> --at <time> -o <out.png> [--json]");
    if (!QFileInfo(path).isFile())
        return fail("No such file: " + path);
    const double seconds = parseTime(command.parser.value(at));
    if (seconds < 0)
        return fail("Not a time: " + command.parser.value(at));
    QString error;
    const QImage image = frames::extract(QFileInfo(path).absoluteFilePath(), seconds, &error);
    if (image.isNull())
        return fail(error);
    const QString out = QFileInfo(command.parser.value(output)).absoluteFilePath();
    if (!frames::savePng(image, out, &error))
        return fail("Could not write " + out + ": " + error);
    if (command.parser.isSet(command.json))
        print({{"output", out}, {"at", seconds}, {"width", image.width()}, {"height", image.height()}});
    else
        print(stdout, QString("Wrote %1 (%2x%3)").arg(out).arg(image.width()).arg(image.height()));
    return 0;
}

// As the hey and basecamp tools and Hype do: one shared copy, linked into
// Claude Code, found directly by Codex.
int skill(const QStringList &arguments) {
    Command command("skill", "Print the agent skill, or install it for your coding agents with: skill install.", false);
    command.parser.addPositionalArgument("install", "Copy to ~/.agents/skills/showtime and link into ~/.claude/skills", "[install]");
    command.parser.process(arguments);
    QFile bundled(":/skill.md");
    if (!bundled.open(QIODevice::ReadOnly))
        return 1;
    const QByteArray text = bundled.readAll();
    if (command.argument(1).isEmpty()) {
        fputs(text.constData(), stdout);
        return 0;
    }
    if (command.argument(1) != "install")
        return fail("Use showtime skill to print the skill, or showtime skill install.");
    const QDir home = QDir::home();
    const QString directory = home.filePath(".agents/skills/showtime");
    QSaveFile file(directory + "/SKILL.md");
    if (!QDir().mkpath(directory) || !file.open(QIODevice::WriteOnly) || file.write(text) < 0 || !file.commit())
        return fail("Could not write " + file.fileName());
    print(stdout, "Installed " + file.fileName());
    if (!home.exists(".claude"))
        return 0;
    const QString link = home.filePath(".claude/skills/showtime");
    const QFileInfo existing(link);
    if (existing.isSymLink())
        QFile::remove(link);
    else if (existing.exists())
        return fail(link + " exists and is not a link; leaving it alone.");
    if (!home.mkpath(".claude/skills") || !QFile::link("../../.agents/skills/showtime", link))
        return fail("Could not link " + link);
    print(stdout, "Linked " + link);
    return 0;
}

int help(const QStringList &arguments) {
    const QString topic = arguments.value(2);
    if (isCliCommand(topic) && topic != "help")
        return runCli({arguments.value(0), topic, "--help"});
    print(stdout, "Usage: showtime [open] [files or URLs...]\n       showtime <command> [options]\n\n" + cliSummary());
    return 0;
}

}  // namespace

double parseTime(const QString &text) {
    const QStringList parts = text.trimmed().split(':');
    if (parts.isEmpty() || parts.size() > 3)
        return -1;
    double seconds = 0;
    for (const QString &part : parts) {
        bool ok = false;
        const double value = part.toDouble(&ok);
        if (!ok || value < 0)
            return -1;
        seconds = seconds * 60 + value;
    }
    return seconds;
}

QString cliSummary() {
    return "Player:\n"
           "  open [files or URLs...]        Play, in a window; several files are the playlist\n\n"
           "Commands that need no display (showtime help <command> for options):\n"
           "  info <file>                    Duration, streams, chapters, the resume position\n"
           "  frame <file> --at T -o F.png   The exact frame at a time, as a PNG\n"
           "  skill [install]                Print the skill for coding agents, or install it";
}

bool isCliCommand(const QString &word) {
    return QStringList{"info", "frame", "skill", "help"}.contains(word);
}

int runCli(const QStringList &arguments) {
    const QString command = arguments.value(1);
    return command == "info"    ? info(arguments)
           : command == "frame" ? frame(arguments)
           : command == "skill" ? skill(arguments)
                                : help(arguments);
}
