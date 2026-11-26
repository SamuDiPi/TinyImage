#pragma once

// File su disco: quali immagini elaborare (anche dentro le cartelle) e dove scrivere il risultato.

#include "Codec.h"

#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>

namespace alleggerisci {

// Dove va il file alleggerito.
enum class Destination {
    Subfolder,   // nella cartella "alleggerite" accanto all'originale (default: gli originali non si toccano)
    Folder,      // in una cartella scelta dall'utente, con le sottocartelle delle cartelle trascinate
    Replace      // al posto dell'originale, che va nel Cestino
};

// Un'immagine da elaborare: il file e il percorso relativo con cui va in una cartella scelta ("foto.jpg" per un file
// indicato da solo, "Viaggio/giorno1/foto.jpg" per un file trovato nella cartella "Viaggio").
struct ImageItem {
    QString path;
    QString relative;
};

inline const QString kOutputFolder = QStringLiteral("alleggerite");

struct FileResult {
    QString input;
    QString output;
    qint64 before = 0;
    qint64 after = 0;
    bool ok = false;
    bool unchanged = false;
    QString method;
    QString error;
};

// Le immagini JPEG e PNG indicate o contenute (anche nelle sottocartelle) nelle cartelle indicate, senza doppioni, senza
// le cartelle "alleggerite" già prodotte e senza quello che sta dentro `exclude` (la cartella scelta per i risultati).
// Al massimo `limit` file.
QList<ImageItem> collectImages(const QStringList& paths, const QString& exclude = {}, int limit = 100000);

// Percorso del risultato nella cartella scelta. Mai uguale a un originale (diventa "nome-alleggerita.jpg"), mai uguale a
// quello di un'altra immagine dello stesso lavoro (`used`) e mai sopra un file che c'era già, salvo quelli scritti dal
// programma in questa sessione (`owned`): in quei casi diventa "nome (2).jpg". Percorsi in minuscolo negli insiemi.
QString folderOutputPath(const QString& folder, const ImageItem& item, QSet<QString>* used, const QSet<QString>& owned = {});

// Alleggerisce un file. Con Destination::Folder il risultato va in `outputPath`. Il risultato è scritto prima in un file
// temporaneo e poi messo al suo posto; la data di modifica resta quella dell'originale.
FileResult processFile(const QString& path, const Options& options, Destination destination, const QString& outputPath = {});

QString formatBytes(qint64 bytes);

} // namespace alleggerisci
