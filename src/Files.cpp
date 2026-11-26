#include "Files.h"

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QSaveFile>
#include <QSet>

namespace alleggerisci {

namespace {

bool isImageName(const QString& name)
{
    const QString n = name.toLower();
    return n.endsWith(QLatin1String(".jpg")) || n.endsWith(QLatin1String(".jpeg")) || n.endsWith(QLatin1String(".jpe")) ||
           n.endsWith(QLatin1String(".png"));
}

bool writeFile(const QString& path, const QByteArray& data, QString* error)
{
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly) || f.write(data) != data.size() || !f.commit()) {
        *error = QStringLiteral("impossibile scrivere %1").arg(QDir::toNativeSeparators(path));
        return false;
    }
    return true;
}

void keepTime(const QString& path, const QDateTime& modified)
{
    QFile f(path);
    if (modified.isValid() && f.open(QIODevice::ReadWrite))
        f.setFileTime(modified, QFileDevice::FileModificationTime);
}

} // namespace

QList<ImageItem> collectImages(const QStringList& paths, const QString& exclude, int limit)
{
    QList<ImageItem> out;
    QSet<QString> seen;
    const QString excluded = exclude.isEmpty() ? QString() : QFileInfo(exclude).absoluteFilePath().toLower() + u'/';
    auto add = [&](const QString& file, const QString& relative) {
        const QString abs = QFileInfo(file).absoluteFilePath();
        const QString key = abs.toLower();
        if (!excluded.isEmpty() && key.startsWith(excluded))
            return;   // risultati già scritti nella cartella scelta
        if (!seen.contains(key) && out.size() < limit) {
            seen.insert(key);
            out.append({abs, relative});
        }
    };
    for (const QString& p : paths) {
        const QFileInfo fi(p);
        if (fi.isFile() && isImageName(fi.fileName())) {
            add(fi.absoluteFilePath(), fi.fileName());
        } else if (fi.isDir()) {
            const QDir base(fi.absoluteFilePath());
            // La cartella trascinata diventa una sottocartella del risultato (la radice di un disco no).
            const QString prefix = base.isRoot() ? QString() : base.dirName() + u'/';
            QDirIterator it(fi.absoluteFilePath(), QDir::Files | QDir::NoSymLinks, QDirIterator::Subdirectories);
            while (it.hasNext() && out.size() < limit) {
                const QString f = it.next();
                const QString rel = base.relativeFilePath(f);
                if (rel.split(u'/').contains(kOutputFolder, Qt::CaseInsensitive))
                    continue;   // risultati di una volta precedente
                if (isImageName(f))
                    add(f, prefix + rel);
            }
        }
    }
    return out;
}

QString folderOutputPath(const QString& folder, const ImageItem& item, QSet<QString>* used, const QSet<QString>& owned)
{
    const QString base = QDir(folder).absoluteFilePath(item.relative.isEmpty() ? QFileInfo(item.path).fileName() : item.relative);
    const QFileInfo fi(base);
    const QString dir = fi.absolutePath() + u'/';
    const QString stem = fi.completeBaseName(), ext = fi.suffix().isEmpty() ? QString() : u'.' + fi.suffix();
    const QString original = QFileInfo(item.path).absoluteFilePath().toLower();
    QString candidate = base;
    if (candidate.toLower() == original)   // cartella scelta = cartella dell'originale: mai sovrascriverlo
        candidate = dir + stem + QStringLiteral("-alleggerita") + ext;
    const QString first = candidate;
    auto taken = [&](const QString& c) {
        const QString key = c.toLower();
        return (used && used->contains(key)) || (QFileInfo::exists(c) && !owned.contains(key));
    };
    for (int n = 2; taken(candidate) && n < 100000; ++n) {
        const QFileInfo f(first);
        candidate = dir + f.completeBaseName() + QStringLiteral(" (%1)").arg(n) + ext;
    }
    if (used)
        used->insert(candidate.toLower());
    return candidate;
}

FileResult processFile(const QString& path, const Options& options, Destination destination, const QString& outputPath)
{
    FileResult r;
    r.input = path;
    const QFileInfo fi(path);
    r.before = fi.size();
    if (!fi.isFile()) {
        r.error = QStringLiteral("file non trovato");
        return r;
    }
    if (fi.size() > kMaxFileBytes) {
        r.error = QStringLiteral("file troppo grande (oltre 300 MB)");
        return r;
    }
    QFile in(path);
    if (!in.open(QIODevice::ReadOnly)) {
        r.error = QStringLiteral("impossibile leggere il file");
        return r;
    }
    const QByteArray data = in.readAll();
    in.close();
    const QDateTime modified = fi.lastModified();

    const Outcome o = compress(data, options);
    if (!o.ok) {
        r.error = o.error;
        return r;
    }
    r.method = o.method;
    r.unchanged = o.unchanged;
    r.after = o.data.size();

    if (destination == Destination::Subfolder || destination == Destination::Folder) {
        r.output = destination == Destination::Folder ? outputPath : fi.absolutePath() + u'/' + kOutputFolder + u'/' + fi.fileName();
        if (r.output.isEmpty() || QFileInfo(r.output).absoluteFilePath().compare(fi.absoluteFilePath(), Qt::CaseInsensitive) == 0) {
            r.error = QStringLiteral("il risultato prenderebbe il posto dell'originale");   // mai, salvo con "al posto degli originali"
            return r;
        }
        const QString dir = QFileInfo(r.output).absolutePath();
        if (!QDir().mkpath(dir)) {
            r.error = QStringLiteral("impossibile creare la cartella %1").arg(QDir::toNativeSeparators(dir));
            return r;
        }
        if (!writeFile(r.output, o.data, &r.error))
            return r;
        keepTime(r.output, modified);
        r.ok = true;
        return r;
    }

    // Al posto dell'originale: niente da fare se non c'è guadagno; altrimenti l'originale va nel Cestino (si può
    // recuperare) e il file nuovo prende il suo nome. Se il Cestino non è disponibile l'originale resta com'è.
    r.output = fi.absoluteFilePath();
    if (o.unchanged) {
        r.ok = true;
        return r;
    }
    const QString temp = fi.absolutePath() + u'/' + fi.fileName() + QStringLiteral(".alleggerisci-tmp");
    if (!writeFile(temp, o.data, &r.error))
        return r;
    QFile original(path);
    if (!original.moveToTrash()) {
        QFile::remove(temp);
        r.error = QStringLiteral("impossibile mettere l'originale nel Cestino: lasciato com'era");
        return r;
    }
    if (!QFile::rename(temp, path)) {
        r.error = QStringLiteral("originale nel Cestino, ma il file nuovo è rimasto in %1").arg(QDir::toNativeSeparators(temp));
        return r;
    }
    keepTime(path, modified);
    r.ok = true;
    return r;
}

QString formatBytes(qint64 bytes)
{
    const QLocale it(QLocale::Italian);
    if (bytes < 1024)
        return QStringLiteral("%1 B").arg(bytes);
    if (bytes < 1024 * 1024)
        return QStringLiteral("%1 KB").arg(it.toString(bytes / 1024.0, 'f', bytes < 10 * 1024 ? 1 : 0));
    if (bytes < 1024LL * 1024 * 1024)
        return QStringLiteral("%1 MB").arg(it.toString(bytes / (1024.0 * 1024.0), 'f', 1));
    return QStringLiteral("%1 GB").arg(it.toString(bytes / (1024.0 * 1024.0 * 1024.0), 'f', 2));
}

} // namespace alleggerisci
