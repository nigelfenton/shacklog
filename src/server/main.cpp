// shacklog-server — Phase 0 spike.
//
// Proves the three-surface coexistence promised in the design doc:
//   * HTTP server  on port 8080  (plain QTcpServer; spike-grade parser)
//   * N3FJP server on port 1100  (plain QTcpServer; PROGRAM command only)
//   * One Qt event loop, no threads
//
// Phase 0 acceptance:
//   curl http://localhost:8080/             -> "ShackLog Server alive\n"
//   curl http://localhost:8080/api/state    -> JSON with phase=0
//   printf '<CMD><PROGRAM></CMD>\r\n' | nc localhost 1100
//     -> <CMD><PROGRAMRESPONSE><PGM>ShackLog</PGM>...
//
// Phase 1 will layer in LogbookModel (reused from the desktop), the
// FD-minimum N3FJP command subset, and the WebSocket fanout for the SPA.

#include "HttpServer.h"
#include "N3fjpServer.h"

#include <QCoreApplication>
#include <QCommandLineParser>
#include <QDebug>
#include <QLoggingCategory>

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName("shacklog-server");
    QCoreApplication::setApplicationVersion("0.1.0-spike");
    QCoreApplication::setOrganizationName("G0JKN");

    QCommandLineParser cli;
    cli.setApplicationDescription("ShackLog server — web UI + N3FJP-compat API");
    cli.addHelpOption();
    cli.addVersionOption();
    QCommandLineOption httpPortOpt(
        QStringList{QStringLiteral("http-port"), QStringLiteral("p")},
        QStringLiteral("HTTP port (default 8080)"),
        QStringLiteral("port"), QStringLiteral("8080"));
    QCommandLineOption n3fjpPortOpt(
        QStringList{QStringLiteral("n3fjp-port")},
        QStringLiteral("N3FJP-compat TCP port (default 1100)"),
        QStringLiteral("port"), QStringLiteral("1100"));
    cli.addOption(httpPortOpt);
    cli.addOption(n3fjpPortOpt);
    cli.process(app);

    const quint16 httpPort  = cli.value(httpPortOpt).toUShort();
    const quint16 n3fjpPort = cli.value(n3fjpPortOpt).toUShort();

    using namespace ShackLog::Server;

    HttpServer http;
    if (!http.start(httpPort)) return 1;

    N3fjpServer n3fjp;
    if (!n3fjp.start(n3fjpPort)) return 1;

    qInfo() << "shacklog-server spike ready.";
    qInfo() << "  HTTP  : http://localhost:" << httpPort;
    qInfo() << "  N3FJP : nc localhost"      << n3fjpPort;
    qInfo() << "Ctrl+C to stop.";

    return app.exec();
}
