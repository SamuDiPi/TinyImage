#pragma once

// File su disco: quali immagini elaborare (anche dentro le cartelle) e dove scrivere il risultato.

#include "Codec.h"

#include <QString>
#include <QStringList>

namespace alleggerisci {

// Dove va il file alleggerito.
enum class Destination {
    Subfolder,   // nella cartella "alleggerite" accanto all'originale (default: gli originali non si toccano)
    Replace      // al posto dell'originale, che va nel Cestino
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

// Le immagini JPEG e PNG indicate o contenute (anche nelle sottocartelle) nelle cartelle indicate, senza doppioni e
// senza le cartelle "alleggerite" già prodotte. Al massimo `limit` file.
QStringList collectImages(const QStringList& paths, int limit = 100000);

// Alleggerisce un file. Il risultato è scritto prima in un file temporaneo e poi messo al suo posto; la data di
// modifica resta quella dell'originale.
FileResult processFile(const QString& path, const Options& options, Destination destination);

QString formatBytes(qint64 bytes);

} // namespace alleggerisci
