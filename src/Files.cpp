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

QStringList collectImages(const QStringList& paths, int limit)
{
    QStringList out;
    QSet<QString> seen;
    auto add = [&](const QString& file) {
        const QString key = QFileInfo(file).absoluteFilePath().toLower();
        if (!seen.contains(key) && out.size() < limit) {
            seen.insert(key);
            out << QFileInfo(file).absoluteFilePath();
        }
    };
    for (const QString& p : paths) {
        const QFileInfo fi(p);
        if (fi.isFile() && isImageName(fi.fileName())) {
            add(fi.absoluteFilePath());
        } else if (fi.isDir()) {
            QDirIterator it(fi.absoluteFilePath(), QDir::Files | QDir::NoSymLinks, QDirIterator::Subdirectories);
            while (it.hasNext() && out.size() < limit) {
                const QString f = it.next();
                const QString rel = QDir(fi.absoluteFilePath()).relativeFilePath(QFileInfo(f).absolutePath());
                if (rel.split(u'/').contains(kOutputFolder, Qt::CaseInsensitive))
                    continue;   // risultati di una volta precedente
                if (isImageName(f))
                    add(f);
            }
        }
    }
    return out;
}

FileResult processFile(const QString& path, const Options& options, Destination destination)
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

    if (destination == Destination::Subfolder) {
        const QString dir = fi.absolutePath() + u'/' + kOutputFolder;
        if (!QDir().mkpath(dir)) {
            r.error = QStringLiteral("impossibile creare la cartella %1").arg(QDir::toNativeSeparators(dir));
            return r;
        }
        r.output = dir + u'/' + fi.fileName();
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
