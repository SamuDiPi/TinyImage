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
#include <QPalette>
#include <QStyleHints>
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

// Tavolozza chiara completa. `fromDark` (solo per le prove): parte da una tavolozza scura come quella che arriva dal
// tema scuro di Windows, per controllare che nessun colore resti quello del sistema.
QPalette lightPalette(bool fromDark)
{
    QPalette p;
    if (fromDark)
        for (int g = 0; g < QPalette::NColorGroups; ++g)
            for (int r = 0; r < QPalette::NColorRoles; ++r)
                p.setColor(QPalette::ColorGroup(g), QPalette::ColorRole(r),
                           r == QPalette::WindowText || r == QPalette::Text || r == QPalette::ButtonText || r == QPalette::BrightText
                                   || r == QPalette::ToolTipText || r == QPalette::HighlightedText || r == QPalette::PlaceholderText
                               ? QColor(255, 255, 255) : QColor(32, 32, 32));
    const QColor text(17, 24, 39), muted(156, 163, 175);
    p.setColor(QPalette::Window, QColor(247, 248, 250));
    p.setColor(QPalette::WindowText, text);
    p.setColor(QPalette::Base, Qt::white);
    p.setColor(QPalette::AlternateBase, QColor(249, 250, 251));
    p.setColor(QPalette::Text, text);
    p.setColor(QPalette::Button, Qt::white);
    p.setColor(QPalette::ButtonText, text);
    p.setColor(QPalette::BrightText, Qt::white);
    p.setColor(QPalette::ToolTipBase, Qt::white);
    p.setColor(QPalette::ToolTipText, text);
    p.setColor(QPalette::PlaceholderText, muted);
    p.setColor(QPalette::Highlight, QColor(37, 99, 235));
    p.setColor(QPalette::HighlightedText, Qt::white);
    p.setColor(QPalette::Link, QColor(37, 99, 235));
    p.setColor(QPalette::LinkVisited, QColor(37, 99, 235));
    p.setColor(QPalette::Light, Qt::white);
    p.setColor(QPalette::Midlight, QColor(229, 231, 235));
    p.setColor(QPalette::Mid, QColor(209, 213, 219));
    p.setColor(QPalette::Dark, QColor(156, 163, 175));
    p.setColor(QPalette::Shadow, QColor(107, 114, 128));
    for (QPalette::ColorRole r : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
        p.setColor(QPalette::Disabled, r, muted);
    return p;
}

QString styleSheet()
{
    // Ogni colore è fissato qui (testo compreso): l'aspetto non dipende dal tema chiaro o scuro di Windows.
    return QStringLiteral(R"(
        QWidget { color: #111827; }
        QMainWindow, QDialog, QMessageBox { background: #f7f8fa; }
        QLabel { background: transparent; color: #111827; }
        QCheckBox { color: #111827; background: transparent; }
        QCheckBox::indicator { width: 16px; height: 16px; border: 1px solid #9ca3af; border-radius: 4px; background: #ffffff; }
        QCheckBox::indicator:checked { background: #2563eb; border-color: #2563eb; image: url(:/spunta.png); }
        QToolTip { color: #111827; background: #ffffff; border: 1px solid #d1d5db; }
        QHeaderView::section { color: #374151; background: #f3f4f6; border: none; border-right: 1px solid #e5e7eb; padding: 4px 6px; }
        QComboBox QAbstractItemView { color: #111827; background: #ffffff; selection-background-color: #dbeafe; selection-color: #111827; }
        QComboBox:disabled { color: #9ca3af; background: #f3f4f6; }
        QScrollBar:vertical { background: #f3f4f6; width: 12px; }
        QScrollBar::handle:vertical { background: #c7cbd1; border-radius: 5px; min-height: 24px; }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
        QLabel#title { font-size: 22pt; font-weight: 600; color: #111827; }
        QLabel#subtitle { color: #4b5563; font-size: 10.5pt; }
        QFrame#drop { border: 2px dashed #9ca3af; border-radius: 16px; background: #ffffff; min-height: 110px; }
        QFrame#drop[active="true"] { border-color: #2563eb; background: #eff6ff; }
        QLabel#dropTitle { font-size: 15pt; font-weight: 600; color: #1f2937; }
        QLabel#dropHint { color: #6b7280; }
        QFrame#options { background: #ffffff; border: 1px solid #e5e7eb; border-radius: 12px; }
        QLabel#summary { font-size: 11pt; font-weight: 600; color: #14532d; }
        QPushButton { padding: 7px 14px; border-radius: 8px; border: 1px solid #d1d5db; background: #ffffff; color: #111827; }
        QPushButton:hover { background: #f3f4f6; }
        QPushButton:disabled { color: #9ca3af; }
        QPushButton#primary { background: #2563eb; color: white; border-color: #2563eb; font-weight: 600; }
        QPushButton#primary:hover { background: #1d4ed8; }
        QPushButton#primary:disabled { background: #93c5fd; border-color: #93c5fd; }
        QPushButton#flat { border: none; background: transparent; color: #2563eb; }
        QPushButton#segment { border-radius: 0; padding: 7px 14px; min-width: 150px; }
        QPushButton#segment:checked { background: #dbeafe; border-color: #2563eb; color: #1e3a8a; font-weight: 600; }
        QTreeWidget { color: #111827; background: #ffffff; border: 1px solid #e5e7eb; border-radius: 10px; alternate-background-color: #f9fafb; }
        QComboBox { padding: 5px 8px; border-radius: 8px; border: 1px solid #d1d5db; background: #ffffff; color: #111827; }
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
    // Tema chiaro fisso: con il tema scuro di Windows il testo diventava bianco sugli sfondi chiari del programma.
    QGuiApplication::styleHints()->setColorScheme(Qt::ColorScheme::Light);
    QApplication::setPalette(lightPalette(qEnvironmentVariableIsSet("ALLEGGERISCI_PROVA_TEMA_SCURO")));
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
