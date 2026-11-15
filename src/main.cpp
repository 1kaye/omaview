// Omaview — Omarchy's media player, Video Player in the launcher: open a video or a song and watch it, from
// the keyboard, with nothing on screen but the picture.

#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QUrl>
#include <cstdio>

#include "backend.h"
#include "cli.h"
#include "inhibit.h"
#include "mpris.h"
#include "theme.h"
#include "translator.h"

int main(int argc, char *argv[]) {
    // QGuiApplication aborts without a display before it has seen its
    // arguments, so commands, --help and --version decide that first.
    const QByteArray first = argc > 1 ? QByteArray(argv[1]) : QByteArray();
    const bool command = isCliCommand(QString::fromLocal8Bit(first));
    const bool helpOrVersion = first == "--help" || first == "-h" || first == "--version" || first == "-v";
    if (command || helpOrVersion)
        qputenv("QT_QPA_PLATFORM", "offscreen");

    QGuiApplication app(argc, argv);
    app.setApplicationName("omaview");
    app.setApplicationVersion("0.1.0");

    if (command)
        return runCli(app.arguments());
    if (first == "--version" || first == "-v") {
        fprintf(stdout, "omaview %s\n", qPrintable(app.applicationVersion()));
        return 0;
    }
    if (helpOrVersion)
        return runCli({app.arguments().first(), "help"});

    // Matches the window to omaview.desktop, so the compositor (Wayland
    // app_id) and the launcher pick up the installed icon. The compiled-in
    // copy covers a run from build/, and needs qt6-svg's icon engine.
    app.setDesktopFileName("omaview");
    app.setWindowIcon(QIcon::fromTheme("omaview", QIcon(":/omaview.svg")));

    QQuickStyle::setStyle("Material");

    // Installed before anything is translated, and every qsTr() binding is
    // re-evaluated when the Omarchy language changes.
    Translator translator;
    app.installTranslator(&translator);

    Theme theme;
    Backend backend;
    backend.setLanguage(translator.language());
    QObject::connect(&translator, &Translator::changed, &backend,
                     [&] { backend.setLanguage(translator.language()); });
    Mpris mpris(&backend);
    IdleInhibitor idle;

    QQmlApplicationEngine engine;
    QObject::connect(&translator, &Translator::changed, &engine, &QQmlEngine::retranslate);
    engine.rootContext()->setContextProperty("translator", &translator);
    engine.rootContext()->setContextProperty("theme", &theme);
    engine.rootContext()->setContextProperty("backend", &backend);
    engine.rootContext()->setContextProperty("idle", &idle);
    engine.load(QUrl("qrc:/Main.qml"));
    if (engine.rootObjects().isEmpty())
        return 1;

    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    idle.setWindow(window);
    // The player's frames share the window's GPU context, set on the render
    // thread as the scene graph starts and stops.
    QObject::connect(window, &QQuickWindow::sceneGraphInitialized, window,
                     [&backend, window] { backend.setRhi(window->rhi()); }, Qt::DirectConnection);
    QObject::connect(window, &QQuickWindow::sceneGraphInvalidated, window,
                     [&backend] { backend.setRhi(nullptr); }, Qt::DirectConnection);
    QObject::connect(&backend, &Backend::raiseRequested, window, [window] {
        window->show();
        window->raise();
        window->requestActivate();
    });
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &backend, &Backend::close);

    // `omaview open a.mkv` and `omaview a.mkv`, which launchers use, alike.
    // The file opens once the window has drawn, since starting the player
    // probes the GPU's decoders on this thread.
    QStringList files = app.arguments().mid(1);
    if (!files.isEmpty() && files.first() == "open")
        files.removeFirst();
    if (!files.isEmpty())
        QObject::connect(window, &QQuickWindow::frameSwapped, &backend, [&backend, files] { backend.open(files); },
                         Qt::ConnectionType(Qt::QueuedConnection | Qt::SingleShotConnection));

    return app.exec();
}
