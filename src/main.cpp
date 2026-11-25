// Alleggerisci: foto e schermate più leggere senza perdita di qualità visibile.
//
// Avvio normale: la finestra (si possono passare file e cartelle, per esempio trascinandoli sull'icona).
// Riga di comando (per le prove e per chi la preferisce):
//   Alleggerisci.exe --riga-di-comando [--identica] [--lato N] [--tieni-dati] [--sostituisci] <file o cartelle>...
// stampa una riga JSON per immagine.
//
// Versione portabile: con il file "portabile.ini" accanto al programma le scelte si salvano lì (niente nel registro di
// Windows) e la cartella si può copiare su una chiavetta.

#include "Files.h"
#include "MainWindow.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QIcon>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QStyleFactory>
#include <QTimer>

#include <cstdio>
#include <memory>

using namespace alleggerisci;

namespace {

int runCommandLine(const QStringList& args)
{
    Options opt;
    Destination dest = Destination::Subfolder;
    QStringList paths;
    for (int i = 0; i < args.size(); ++i) {
        const QString& a = args[i];
        if (a == QLatin1String("--riga-di-comando"))
            continue;
        if (a == QLatin1String("--identica"))
            opt.quality = Quality::Exact;
        else if (a == QLatin1String("--tieni-dati"))
            opt.stripPrivate = false;
        else if (a == QLatin1String("--sostituisci"))
            dest = Destination::Replace;
        else if (a == QLatin1String("--lato") && i + 1 < args.size())
            opt.maxSide = std::clamp(args[++i].toInt(), 0, 100000);
        else if (a.startsWith(QLatin1String("--"))) {
            std::fprintf(stderr, "opzione sconosciuta: %s\n", qPrintable(a));
            return 2;
        } else
            paths << a;
    }
    const QStringList files = collectImages(paths);
    int errors = 0;
    qint64 before = 0, after = 0;
    for (const QString& f : files) {
        const FileResult r = processFile(f, opt, dest);
        QJsonObject o{{"file", QDir::toNativeSeparators(r.input)}, {"ok", r.ok}, {"prima", r.before}, {"dopo", r.after},
                      {"invariata", r.unchanged}, {"metodo", r.method}, {"risultato", QDir::toNativeSeparators(r.output)}};
        if (!r.ok) {
            o.insert("errore", r.error);
            ++errors;
        } else {
            before += r.before;
            after += r.after;
        }
        std::fprintf(stdout, "%s\n", QJsonDocument(o).toJson(QJsonDocument::Compact).constData());
        std::fflush(stdout);
    }
    std::fprintf(stdout, "%s\n",
                 QJsonDocument(QJsonObject{{"immagini", int(files.size())}, {"errori", errors}, {"prima", before}, {"dopo", after}})
                     .toJson(QJsonDocument::Compact)
                     .constData());
    return errors == 0 ? 0 : 1;
}

QString styleSheet()
{
    return QStringLiteral(R"(
        QMainWindow, QDialog { background: #f7f8fa; }
        QLabel#title { font-size: 22pt; font-weight: 600; color: #111827; }
        QLabel#subtitle { color: #4b5563; font-size: 10.5pt; }
        QFrame#drop { border: 2px dashed #9ca3af; border-radius: 16px; background: #ffffff; min-height: 110px; }
        QFrame#drop[active="true"] { border-color: #2563eb; background: #eff6ff; }
        QLabel#dropTitle { font-size: 15pt; font-weight: 600; color: #1f2937; }
        QLabel#dropHint { color: #6b7280; }
        QFrame#options { background: #ffffff; border: 1px solid #e5e7eb; border-radius: 12px; }
        QLabel#summary { font-size: 11pt; font-weight: 600; color: #14532d; }
        QPushButton { padding: 7px 14px; border-radius: 8px; border: 1px solid #d1d5db; background: #ffffff; }
        QPushButton:hover { background: #f3f4f6; }
        QPushButton:disabled { color: #9ca3af; }
        QPushButton#primary { background: #2563eb; color: white; border-color: #2563eb; font-weight: 600; }
        QPushButton#primary:hover { background: #1d4ed8; }
        QPushButton#primary:disabled { background: #93c5fd; border-color: #93c5fd; }
        QPushButton#flat { border: none; background: transparent; color: #2563eb; }
        QPushButton#segment { border-radius: 0; padding: 7px 14px; min-width: 150px; }
        QPushButton#segment:checked { background: #dbeafe; border-color: #2563eb; color: #1e3a8a; font-weight: 600; }
        QTreeWidget { background: #ffffff; border: 1px solid #e5e7eb; border-radius: 10px; alternate-background-color: #f9fafb; }
        QComboBox { padding: 5px 8px; border-radius: 8px; border: 1px solid #d1d5db; background: #ffffff; }
        QProgressBar { border: none; background: #e5e7eb; border-radius: 3px; }
        QProgressBar::chunk { background: #2563eb; border-radius: 3px; }
    )");
}

} // namespace

int main(int argc, char** argv)
{
    QStringList args;
    for (int i = 1; i < argc; ++i)
        args << QString::fromLocal8Bit(argv[i]);
    if (args.contains(QLatin1String("--riga-di-comando"))) {
        QCoreApplication app(argc, argv);
        QCoreApplication::setApplicationVersion(QStringLiteral(ALLEGGERISCI_VERSION));
        return runCommandLine(QCoreApplication::arguments().mid(1));
    }

    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Alleggerisci"));
    QApplication::setOrganizationName(QStringLiteral("Alleggerisci"));
    QApplication::setApplicationVersion(QStringLiteral(ALLEGGERISCI_VERSION));
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/alleggerisci.png")));
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    QFont font(QStringLiteral("Segoe UI"));
    font.setPointSizeF(10);
    QApplication::setFont(font);
    app.setStyleSheet(styleSheet());

    const QString portable = QCoreApplication::applicationDirPath() + QStringLiteral("/portabile.ini");
    std::unique_ptr<QSettings> settings = QFile::exists(portable)
                                              ? std::make_unique<QSettings>(portable, QSettings::IniFormat)
                                              : std::make_unique<QSettings>();
    MainWindow window(*settings);
    window.show();
    const QStringList given = QCoreApplication::arguments().mid(1);
    if (!given.isEmpty())
        window.addPaths(given);
    // Prove senza schermo: ALLEGGERISCI_SCHERMATA=<file.png> salva l'immagine della finestra (e, con
    // ALLEGGERISCI_CONFRONTO=<file.png>, quella del confronto della prima immagine) a elaborazione finita, poi esce.
    const QString shot = qEnvironmentVariable("ALLEGGERISCI_SCHERMATA");
    QTimer poll;
    if (!shot.isEmpty()) {
        QObject::connect(&poll, &QTimer::timeout, &window, [&window, &poll, shot] {
            if (window.busy())
                return;
            poll.stop();
            window.grab().save(shot);
            const QString cmp = qEnvironmentVariable("ALLEGGERISCI_CONFRONTO");
            if (!cmp.isEmpty())
                window.saveFirstComparison(cmp);
            QApplication::quit();
        });
        poll.start(500);
    }
    return app.exec();
}
