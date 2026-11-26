// Test del motore di Alleggerisci, senza finestre: immagini costruite qui (foto e schermate sintetiche), confronti
// pixel per pixel e con un indice di somiglianza strutturale (SSIM) sulla luminanza.

#include "Codec.h"
#include "Files.h"

#include <QBuffer>
#include <QColorSpace>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageWriter>
#include <QRandomGenerator>
#include <QTemporaryDir>

#include <cmath>
#include <cstdio>
#include <tuple>

using namespace alleggerisci;

namespace {

int failures = 0;
int checks = 0;

void check(bool ok, const QString& what)
{
    ++checks;
    if (!ok) {
        ++failures;
        std::fprintf(stderr, "ERRORE: %s\n", qPrintable(what));
    }
}

// Foto sintetica: sfumature, rumore come la grana di un sensore, bordi netti, dettagli fini.
QImage photo(int w, int h, quint32 seed = 7, int noise = 2)
{
    QImage img(w, h, QImage::Format_RGB888);
    QRandomGenerator rng(seed);
    for (int y = 0; y < h; ++y) {
        uchar* row = img.scanLine(y);
        for (int x = 0; x < w; ++x) {
            double r = 128 + 90 * std::sin(x * 0.011) * std::cos(y * 0.007);
            double g = 120 + 80 * std::sin((x + y) * 0.006);
            double b = 110 + 70 * std::cos(x * 0.004 - y * 0.009);
            if (x > w / 3 && x < w / 2 && y > h / 4 && y < h / 2) {   // oggetto con bordi netti
                r = 220; g = 60; b = 40;
            }
            if ((x / 3 + y / 3) % 2 == 0 && x > w * 2 / 3 && y > h * 2 / 3) {   // tessuto fine
                r *= 0.8; g *= 0.8; b *= 0.8;
            }
            const double n = double(rng.bounded(2 * noise + 1)) - noise;
            row[x * 3 + 0] = uchar(std::clamp(r + n, 0.0, 255.0));
            row[x * 3 + 1] = uchar(std::clamp(g + n, 0.0, 255.0));
            row[x * 3 + 2] = uchar(std::clamp(b + n, 0.0, 255.0));
        }
    }
    return img;
}

// Schermata sintetica: aree piatte, barre, "testo" (tratti sottili con bordi sfumati), una sfumatura, trasparenza.
QImage screenshot(int w, int h, bool alpha)
{
    QImage img(w, h, alpha ? QImage::Format_ARGB32 : QImage::Format_RGB32);
    img.fill(QColor(245, 246, 248));
    for (int y = 0; y < h; ++y) {
        QRgb* row = reinterpret_cast<QRgb*>(img.scanLine(y));
        for (int x = 0; x < w; ++x) {
            if (y < 40)
                row[x] = qRgb(40, 90, 160);                                        // barra del titolo
            else if (x < 180)
                row[x] = qRgb(230, 232, 236);                                      // pannello laterale
            else if (y > h - 120)
                row[x] = qRgb(60 + (x * 120) / w, 120, 200 - (x * 80) / w);         // sfumatura
            // "testo": righe di tratti con antialias
            const int line = (y - 60) % 22;
            if (x > 200 && x < w - 40 && y > 60 && y < h - 140 && line < 12 && (x / 7) % 5 != 4) {
                const int edge = std::min({line, 11 - line, (x % 7), 6 - (x % 7)});
                const int v = edge <= 0 ? 150 : edge == 1 ? 70 : 20;
                row[x] = qRgb(v, v, v + 10);
            }
            if (alpha && x > w - 30)
                row[x] = qRgba(qRed(row[x]), qGreen(row[x]), qBlue(row[x]), (y * 255) / h);
        }
    }
    return img;
}

double ssim(const QImage& a0, const QImage& b0)
{
    if (a0.size() != b0.size())
        return 0.0;
    const QImage a = a0.convertToFormat(QImage::Format_ARGB32), b = b0.convertToFormat(QImage::Format_ARGB32);
    auto luma = [](QRgb c) { return 0.299 * qRed(c) + 0.587 * qGreen(c) + 0.114 * qBlue(c) + 0.25 * (255 - qAlpha(c)); };
    const double c1 = 6.5025, c2 = 58.5225;
    double total = 0.0;
    int n = 0;
    for (int by = 0; by + 8 <= a.height(); by += 8)
        for (int bx = 0; bx + 8 <= a.width(); bx += 8) {
            double ma = 0, mb = 0, va = 0, vb = 0, cov = 0;
            for (int y = 0; y < 8; ++y)
                for (int x = 0; x < 8; ++x) {
                    ma += luma(a.pixel(bx + x, by + y));
                    mb += luma(b.pixel(bx + x, by + y));
                }
            ma /= 64;
            mb /= 64;
            for (int y = 0; y < 8; ++y)
                for (int x = 0; x < 8; ++x) {
                    const double da = luma(a.pixel(bx + x, by + y)) - ma, db = luma(b.pixel(bx + x, by + y)) - mb;
                    va += da * da;
                    vb += db * db;
                    cov += da * db;
                }
            va /= 63;
            vb /= 63;
            cov /= 63;
            total += ((2 * ma * mb + c1) * (2 * cov + c2)) / ((ma * ma + mb * mb + c1) * (va + vb + c2));
            ++n;
        }
    return n ? total / n : 0.0;
}

bool samePixels(const QImage& a, const QImage& b)
{
    if (a.size() != b.size())
        return false;
    const bool deep = a.depth() > 32 || b.depth() > 32;
    const QImage::Format f = deep ? QImage::Format_RGBA64 : QImage::Format_ARGB32;
    return a.convertToFormat(f) == b.convertToFormat(f);
}

QByteArray png(const QImage& img, int compression = 9)
{
    QByteArray bytes;
    QBuffer buf(&bytes);
    buf.open(QIODevice::WriteOnly);
    QImageWriter w(&buf, "png");
    w.setCompression(compression * 11);   // Qt: 0-100, cioè zlib 0-9
    w.write(img);
    return bytes;
}

// Blocco PNG con CRC.
QByteArray pngChunk(const char* type, const QByteArray& data)
{
    QByteArray body = QByteArray(type, 4) + data;
    quint32 crc = 0xFFFFFFFFu;
    for (char ch : body) {
        crc ^= static_cast<unsigned char>(ch);
        for (int k = 0; k < 8; ++k)
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    crc ^= 0xFFFFFFFFu;
    QByteArray out;
    const quint32 len = quint32(data.size());
    for (int s = 24; s >= 0; s -= 8)
        out += char((len >> s) & 0xFF);
    out += body;
    for (int s = 24; s >= 0; s -= 8)
        out += char((crc >> s) & 0xFF);
    return out;
}

QByteArray insertAfterIhdr(const QByteArray& pngData, const QByteArray& chunk)
{
    return pngData.left(8 + 25) + chunk + pngData.mid(8 + 25);
}

// EXIF con orientamento e un puntatore al blocco GPS (tag 0x8825) con latitudine finta.
QByteArray exifWithGps(int orientation)
{
    const unsigned char b[] = {'E', 'x', 'i', 'f', 0, 0, 'I', 'I', 42, 0, 8, 0, 0, 0,
                               2, 0,                                                       // 2 voci
                               0x12, 0x01, 3, 0, 1, 0, 0, 0, static_cast<unsigned char>(orientation), 0, 0, 0,   // orientamento
                               0x25, 0x88, 4, 0, 1, 0, 0, 0, 38, 0, 0, 0,                  // GPS IFD a 38
                               0, 0, 0, 0,                                                 // nessun IFD successivo
                               1, 0, 0x01, 0, 2, 0, 2, 0, 0, 0, 'N', 0, 0, 0, 0, 0, 0, 0};   // GPS: LatitudeRef "N"
    return QByteArray(reinterpret_cast<const char*>(b), sizeof b);
}

void testJpeg()
{
    const QImage original = photo(1600, 1200);
    const QByteArray q95 = testing::encodeJpeg(original, 95);
    check(!q95.isEmpty(), "JPEG di prova");

    Options visual;
    const Outcome v = compress(q95, visual);
    check(v.ok && !v.unchanged && v.data.size() < q95.size() * 0.8, QStringLiteral("JPEG q95 visivo: %1 -> %2 (%3)").arg(q95.size()).arg(v.data.size()).arg(v.method));
    const double s = ssim(decodeImage(q95), decodeImage(v.data));
    check(s >= 0.975, QStringLiteral("JPEG visivo: SSIM %1").arg(s));
    // Foto con grana forte: si ricomprime solo se resta sopra la soglia, altrimenti solo senza perdita.
    const QByteArray grainy = testing::encodeJpeg(photo(1200, 900, 9, 6), 95);
    const Outcome g = compress(grainy, visual);
    check(g.ok && g.data.size() <= grainy.size() && ssim(decodeImage(grainy), decodeImage(g.data)) >= 0.975,
          QStringLiteral("foto con grana: %1 -> %2 (%3)").arg(grainy.size()).arg(g.data.size()).arg(g.method));

    Options exact;
    exact.quality = Quality::Exact;
    const Outcome e = compress(q95, exact);
    check(e.ok && e.data.size() <= q95.size(), QStringLiteral("JPEG identico: %1 -> %2").arg(q95.size()).arg(e.data.size()));
    check(samePixels(decodeImage(q95), decodeImage(e.data)), "JPEG identico: stessi pixel");

    // Già compresso (qualità 70): niente ricompressione, solo ottimizzazione senza perdita.
    const QByteArray q70 = testing::encodeJpeg(original, 70);
    const Outcome v70 = compress(q70, visual);
    check(v70.ok && v70.data.size() <= q70.size() && samePixels(decodeImage(q70), decodeImage(v70.data)),
          QStringLiteral("JPEG q70: non ricompresso (%1)").arg(v70.method));

    // Ridimensionamento.
    Options small = visual;
    small.maxSide = 800;
    const Outcome r = compress(q95, small);
    const QImage ri = decodeImage(r.data);
    check(r.ok && ri.width() == 800 && ri.height() == 600, QStringLiteral("JPEG ridimensionato: %1 x %2").arg(ri.width()).arg(ri.height()));

    // GPS e orientamento: foto "girata" (orientamento 6 = ruotare di 90° in senso orario).
    const QByteArray withGps = testing::encodeJpeg(photo(600, 400, 3), 95, {{0xE1, exifWithGps(6)}});
    const QImage shown = decodeImage(withGps);
    check(shown.width() == 400 && shown.height() == 600, "foto ruotata: decodificata in verticale");
    for (Quality q : {Quality::Maximum, Quality::Exact}) {
        Options o;
        o.quality = q;
        const Outcome out = compress(withGps, o);
        const QImage oi = decodeImage(out.data);
        const QString name = q == Quality::Maximum ? QStringLiteral("visivo") : QStringLiteral("identico");
        check(out.ok && out.data.size() < withGps.size(), QStringLiteral("GPS tolto (%1)").arg(name));
        check(!QByteArray(out.data).contains(QByteArray::fromRawData("\x25\x88\x04\x00", 4)), QStringLiteral("nessun puntatore GPS (%1)").arg(name));
        check(oi.width() == 400 && oi.height() == 600, QStringLiteral("orientamento conservato (%1): %2 x %3").arg(name).arg(oi.width()).arg(oi.height()));
        if (q == Quality::Exact)
            check(samePixels(shown, oi), "foto ruotata, identica: stessi pixel");
        else
            check(ssim(shown, oi) >= 0.975, "foto ruotata, visiva: somiglianza");
        o.stripPrivate = false;
        const Outcome keep = compress(withGps, o);
        check(keep.ok && keep.data.contains(QByteArray::fromRawData("\x25\x88\x04\x00", 4)), QStringLiteral("dati tenuti a richiesta (%1)").arg(name));
        check(decodeImage(keep.data).height() == 600, QStringLiteral("dati tenuti: orientamento giusto (%1)").arg(name));
    }

    // Livelli: più bassi = più leggeri, ciascuno sopra la sua soglia di somiglianza.
    qint64 previous = v.data.size();
    for (auto [q, threshold, name] : {std::tuple{Quality::Medium, 0.95, "media"}, std::tuple{Quality::Low, 0.92, "bassa"}}) {
        Options o;
        o.quality = q;
        const Outcome lv = compress(q95, o);
        const double sl = ssim(decodeImage(q95), decodeImage(lv.data));
        check(lv.ok && lv.data.size() < previous && sl >= threshold,
              QStringLiteral("JPEG qualità %1: %2 byte, SSIM %3 (%4)").arg(name).arg(lv.data.size()).arg(sl).arg(lv.method));
        previous = lv.data.size();
    }
    // Una foto già compressa a qualità 70 si ricomprime solo con i livelli che scendono sotto il 70.
    const Outcome low70 = compress(q70, [] { Options o; o.quality = Quality::Low; return o; }());
    check(low70.ok && low70.data.size() < q70.size() * 0.9 && ssim(decodeImage(q70), decodeImage(low70.data)) >= 0.92,
          QStringLiteral("JPEG q70, qualità bassa: %1 -> %2").arg(q70.size()).arg(low70.data.size()));

    // Mai più grande: il risultato ricompresso non cresce.
    const Outcome again = compress(v.data, visual);
    check(again.ok && again.data.size() <= v.data.size(), "JPEG già alleggerito: non cresce");
}

void testPng()
{
    const QImage shot = screenshot(1280, 800, false);
    const QByteArray p = png(shot, 6);
    Options visual;
    const Outcome v = compress(p, visual);
    check(v.ok && !v.unchanged && v.data.size() < p.size(), QStringLiteral("PNG visivo: %1 -> %2 (%3)").arg(p.size()).arg(v.data.size()).arg(v.method));
    const double s = ssim(shot, decodeImage(v.data));
    check(s > 0.98, QStringLiteral("PNG visivo: SSIM %1").arg(s));

    Options exact;
    exact.quality = Quality::Exact;
    const Outcome e = compress(p, exact);
    check(e.ok && e.data.size() < p.size(), QStringLiteral("PNG identico: %1 -> %2").arg(p.size()).arg(e.data.size()));
    check(samePixels(shot, decodeImage(e.data)), "PNG identico: stessi pixel");

    // Trasparenza.
    const QImage shotA = screenshot(900, 600, true);
    const QByteArray pa = png(shotA, 6);
    const Outcome ea = compress(pa, exact);
    check(ea.ok && samePixels(shotA, decodeImage(ea.data)), "PNG con trasparenza, identico: stessi pixel");
    const Outcome va = compress(pa, visual);
    check(va.ok && ssim(shotA, decodeImage(va.data)) > 0.97, "PNG con trasparenza, visivo: somiglianza");

    // 16 bit per canale: identico resta identico.
    QImage deep = shot.convertToFormat(QImage::Format_RGBA64);
    for (int y = 0; y < 50; ++y)
        for (int x = 0; x < deep.width(); ++x) {
            QRgba64* row = reinterpret_cast<QRgba64*>(deep.scanLine(y));
            row[x] = qRgba64(quint16(x * 51), quint16(y * 1300), 12345, 65535);
        }
    const QByteArray p16 = png(deep, 6);
    const Outcome e16 = compress(p16, exact);
    check(e16.ok && samePixels(deep, decodeImage(e16.data)), "PNG a 16 bit, identico: stessi pixel");

    // Testi (autore, posizione): tolti di default, tenuti a richiesta. Profilo colore sempre tenuto.
    const QByteArray withText = insertAfterIhdr(p, pngChunk("tEXt", QByteArray("Author\0Mario Rossi", 18)));
    const Outcome t = compress(withText, visual);
    check(t.ok && !t.data.contains("Mario Rossi"), "PNG: testi tolti");
    Options keep = visual;
    keep.stripPrivate = false;
    const Outcome tk = compress(withText, keep);
    check(tk.ok && tk.data.contains("Mario Rossi"), "PNG: testi tenuti a richiesta");
    QImage p3 = shot;
    p3.setColorSpace(QColorSpace(QColorSpace::DisplayP3));
    const QByteArray withIcc = png(p3, 6);
    check(withIcc.contains("iCCP"), "PNG di prova con profilo colore");
    for (Quality q : {Quality::Maximum, Quality::Exact}) {
        Options o;
        o.quality = q;
        const Outcome c = compress(withIcc, o);
        check(c.ok && c.data.contains("iCCP") && decodeImage(c.data).colorSpace() == QColorSpace(QColorSpace::DisplayP3),
              QStringLiteral("PNG: profilo colore conservato (%1)").arg(q == Quality::Maximum ? "visivo" : "identico"));
    }

    // Animazione: lasciata com'era.
    const QByteArray apng = insertAfterIhdr(p, pngChunk("acTL", QByteArray(8, '\0')));
    const Outcome an = compress(apng, visual);
    check(an.ok && an.unchanged && an.data == apng, "PNG animato: lasciato com'era");

    // Foto salvata in PNG: senza perdita visibile anche qui.
    const QImage ph = photo(800, 600);
    const QByteArray pp = png(ph, 6);
    const Outcome vp = compress(pp, visual);
    check(vp.ok && vp.data.size() <= pp.size() && ssim(ph, decodeImage(vp.data)) > 0.97,
          QStringLiteral("foto in PNG: %1 -> %2 (%3)").arg(pp.size()).arg(vp.data.size()).arg(vp.method));

    const Outcome again = compress(v.data, visual);
    check(again.ok && again.data.size() <= v.data.size(), "PNG già alleggerito: non cresce");

    // Livelli più bassi: la foto salvata in PNG passa a 256 colori quando la soglia lo consente.
    qint64 previous = vp.data.size();
    for (auto [q, threshold, name] : {std::tuple{Quality::Medium, 0.965, "media"}, std::tuple{Quality::Low, 0.94, "bassa"}}) {
        Options o;
        o.quality = q;
        const Outcome lv = compress(pp, o);
        const double sl = ssim(ph, decodeImage(lv.data));
        check(lv.ok && lv.data.size() <= previous && (lv.unchanged || sl >= threshold),
              QStringLiteral("foto in PNG, qualità %1: %2 byte, SSIM %3 (%4)").arg(name).arg(lv.data.size()).arg(sl).arg(lv.method));
        previous = lv.data.size();
    }
}

// File danneggiati o costruiti apposta: nessun arresto, mai un risultato più grande, mai "identico" con pixel diversi.
void testDamaged()
{
    QRandomGenerator rng(42);
    const QByteArray jpg = testing::encodeJpeg(photo(320, 240), 92, {{0xE1, exifWithGps(3)}});
    const QByteArray pn = png(screenshot(320, 240, true), 6);
    int handled = 0;
    for (int i = 0; i < 400; ++i) {
        QByteArray d = (i % 2) ? jpg : pn;
        const int flips = 1 + int(rng.bounded(8));
        for (int k = 0; k < flips; ++k)
            d[int(rng.bounded(d.size()))] = char(rng.bounded(256));
        if (i % 7 == 0)
            d.truncate(int(rng.bounded(d.size())));
        Options o;
        o.quality = (i % 3) ? Quality::Maximum : Quality::Exact;
        const Outcome out = compress(d, o);
        check(!out.ok || out.data.size() <= d.size() || o.maxSide > 0, QStringLiteral("danneggiato %1: risultato più grande").arg(i));
        ++handled;
    }
    check(handled == 400, "file danneggiati: tutti gestiti senza arresti");
    check(!compress(QByteArray("GIF89a.........."), Options()).ok, "formato non gestito: errore");
    check(!compress(QByteArray(), Options()).ok, "file vuoto: errore");
}

void testFiles()
{
    QTemporaryDir dir;
    const QString base = dir.path();
    QDir().mkpath(base + "/viaggio/giorno1");
    const QByteArray jpg = testing::encodeJpeg(photo(1200, 900), 95);
    const QByteArray pn = png(screenshot(800, 600, false), 6);
    auto put = [](const QString& path, const QByteArray& d) {
        QFile f(path);
        if (f.open(QIODevice::WriteOnly))
            f.write(d);
    };
    put(base + "/viaggio/mare.jpg", jpg);
    put(base + "/viaggio/giorno1/schermata.PNG", pn);
    put(base + "/viaggio/giorno1/note.txt", "non è un'immagine");
    const QList<ImageItem> found = collectImages({base + "/viaggio"});
    check(found.size() == 2, QStringLiteral("immagini trovate nella cartella: %1").arg(found.size()));
    for (const ImageItem& f : found) {
        const FileResult r = processFile(f.path, Options(), Destination::Subfolder);
        check(r.ok && QFile::exists(r.output) && r.after < r.before, QStringLiteral("cartella alleggerite: %1").arg(r.output));
    }
    check(QFile(base + "/viaggio/mare.jpg").size() == jpg.size(), "originale non toccato");
    const QList<ImageItem> again = collectImages({base + "/viaggio"});
    check(again.size() == 2, "le cartelle alleggerite non si rielaborano");

    // Cartella scelta: sottocartelle conservate, nomi uguali distinti, mai sopra un originale o un file estraneo.
    const QString out = base + "/scelta";
    QDir().mkpath(out);
    put(out + "/mare.jpg", "file dell'utente da non toccare");
    QDir().mkpath(base + "/altra");
    put(base + "/altra/mare.jpg", jpg);
    const QList<ImageItem> items = collectImages({base + "/viaggio", base + "/altra/mare.jpg", out}, out);
    check(items.size() == 3, QStringLiteral("cartella scelta esclusa dalla ricerca: %1 immagini").arg(items.size()));
    QSet<QString> used;
    QStringList outputs;
    for (const ImageItem& it : items) {
        const QString target = folderOutputPath(out, it, &used);
        const FileResult r = processFile(it.path, Options(), Destination::Folder, target);
        check(r.ok && QFile::exists(target), QStringLiteral("cartella scelta: %1").arg(target));
        outputs << QDir(out).relativeFilePath(target);
    }
    outputs.sort();
    check(outputs == QStringList({"mare (2).jpg", "viaggio/giorno1/schermata.PNG", "viaggio/mare.jpg"}),
          QStringLiteral("nomi nella cartella scelta: %1").arg(outputs.join(", ")));
    QFile mine(out + "/mare.jpg");
    check(mine.open(QIODevice::ReadOnly) && mine.readAll() == "file dell'utente da non toccare", "file estraneo non sovrascritto");
    const ImageItem self{base + "/viaggio/mare.jpg", "mare.jpg"};
    check(folderOutputPath(base + "/viaggio", self, nullptr).endsWith("mare-alleggerita.jpg"), "cartella dell'originale: nome nuovo");
    check(!processFile(self.path, Options(), Destination::Folder, self.path).ok, "mai al posto dell'originale");
    const FileResult missing = processFile(base + "/non-esiste.jpg", Options(), Destination::Subfolder);
    check(!missing.ok, "file mancante: errore");
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    if (argc > 2 && QByteArray(argv[1]) == "--misura-file") {   // foto vere: come una fotocamera a qualità 95, poi ricompresse
        for (int i = 2; i < argc; ++i) {
            QFile f(QString::fromLocal8Bit(argv[i]));
            if (!f.open(QIODevice::ReadOnly))
                continue;
            const QImage img = decodeImage(f.readAll()).convertToFormat(QImage::Format_RGB888);
            const QByteArray src = testing::encodeJpeg(img, 95);
            const QImage ref = decodeImage(src);
            std::printf("%s:", qPrintable(QFileInfo(f.fileName()).fileName()));
            for (int q : {80, 85, 88, 90, 92}) {
                const QByteArray out = testing::encodeJpeg(ref, q);
                std::printf("  q%d SSIM %.4f peso %.0f%%", q, ssim(ref, decodeImage(out)), 100.0 * out.size() / src.size());
            }
            std::printf("\n");
        }
        return 0;
    }
    if (argc > 1 && QByteArray(argv[1]) == "--misura-qualita") {   // tabella SSIM / peso per scegliere la qualità
        for (int grain : {1, 3, 6}) {
            QImage img = photo(1600, 1200, 11);
            QRandomGenerator rng(5);
            if (grain != 6)   // grana diversa: rifà il rumore
                for (int y = 0; y < img.height(); ++y)
                    for (int x = 0; x < img.width() * 3; ++x)
                        img.scanLine(y)[x] = uchar(std::clamp(int(img.scanLine(y)[x]) + int(rng.bounded(2 * grain + 1)) - grain, 0, 255));
            const QByteArray src = testing::encodeJpeg(img, 95);
            const QImage ref = decodeImage(src);
            for (int q : {80, 85, 88, 90, 92}) {
                const QByteArray out = testing::encodeJpeg(ref, q);
                std::printf("grana %d qualita %d: SSIM %.4f, peso %.0f%%\n", grain, q, ssim(ref, decodeImage(out)), 100.0 * out.size() / src.size());
            }
        }
        return 0;
    }
    testJpeg();
    testPng();
    testDamaged();
    testFiles();
    std::printf("%d controlli, %d errori\n", checks, failures);
    if (failures == 0)
        std::printf("Tutti i test superati.\n");
    return failures == 0 ? 0 : 1;
}
