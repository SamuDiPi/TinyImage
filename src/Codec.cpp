#include "Codec.h"

#include <QBuffer>
#include <QColorSpace>
#include <QImage>
#include <QImageReader>
#include <QImageWriter>
#include <QTransform>

#include <algorithm>
#include <csetjmp>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

extern "C" {
#include <jpeglib.h>
}
#include <libimagequant.h>
#include <zopflipng_lib.h>

namespace alleggerisci {

namespace {

// ================================================================== JPEG (libjpeg-turbo)
//
// Gli errori di libjpeg arrivano con longjmp: le funzioni che lo usano hanno solo variabili semplici (nessun oggetto
// con distruttore tra setjmp e le chiamate alla libreria) e copiano i risultati negli oggetti Qt alla fine.

struct JpegErr {
    jpeg_error_mgr pub;
    jmp_buf jump;
};

void jpegErrorExit(j_common_ptr cinfo)
{
    longjmp(reinterpret_cast<JpegErr*>(cinfo->err)->jump, 1);
}

void jpegQuiet(j_common_ptr, int) {}   // avvisi e messaggi: contati in num_warnings, non stampati

void setupErr(JpegErr& e)
{
    jpeg_std_error(&e.pub);
    e.pub.error_exit = jpegErrorExit;
    e.pub.emit_message = jpegQuiet;
}

struct Marker {
    int code = 0;
    QByteArray data;
};

struct JpegInfo {
    bool ok = false;
    int width = 0, height = 0, components = 0;
    J_COLOR_SPACE colorSpace = JCS_UNKNOWN;
    int hSamp = 2, vSamp = 2;   // campionamento della luminanza (2x2 = 4:2:0, 1x1 = 4:4:4)
    int quality = -1;           // stimata dalla tabella di quantizzazione della luminanza
    int orientation = 1;        // EXIF
    QByteArray icc;
    std::vector<Marker> markers;   // APPn e COM, nell'ordine del file
};

// Tabella di quantizzazione standard della luminanza (IJG, ordine naturale).
const int kStdLuminance[64] = {16, 11, 10, 16, 24,  40,  51,  61,  12, 12, 14, 19, 26,  58,  60,  55,
                               14, 13, 16, 24, 40,  57,  69,  56,  14, 17, 22, 29, 51,  87,  80,  62,
                               18, 22, 37, 56, 68,  109, 103, 77,  24, 35, 55, 64, 81,  104, 113, 92,
                               49, 64, 78, 87, 103, 121, 120, 101, 72, 92, 95, 98, 112, 100, 103, 99};

int estimateQuality(const JQUANT_TBL* t)
{
    if (!t)
        return -1;
    double sum = 0.0;
    for (int i = 0; i < 64; ++i)
        sum += 100.0 * t->quantval[i] / kStdLuminance[i];
    const double scale = sum / 64.0;
    if (scale <= 0.0)
        return 100;
    const double q = scale <= 100.0 ? (200.0 - scale) / 2.0 : 5000.0 / scale;
    return std::clamp(static_cast<int>(q + 0.5), 1, 100);
}

// Orientamento nel blocco EXIF (APP1 "Exif\0\0" + TIFF). Con `set` > 0 lo riscrive. Controlla ogni limite.
int exifOrientation(QByteArray* exif, int set = 0)
{
    if (exif->size() < 14 || std::memcmp(exif->constData(), "Exif\0\0", 6) != 0)
        return 1;
    auto* p = reinterpret_cast<unsigned char*>(exif->data()) + 6;
    const qsizetype n = exif->size() - 6;
    const bool le = p[0] == 'I' && p[1] == 'I';
    if (!le && !(p[0] == 'M' && p[1] == 'M'))
        return 1;
    auto u16 = [&](qsizetype o) -> unsigned { return le ? p[o] | (p[o + 1] << 8) : (p[o] << 8) | p[o + 1]; };
    auto u32 = [&](qsizetype o) -> quint32 {
        return le ? quint32(p[o]) | (quint32(p[o + 1]) << 8) | (quint32(p[o + 2]) << 16) | (quint32(p[o + 3]) << 24)
                  : (quint32(p[o]) << 24) | (quint32(p[o + 1]) << 16) | (quint32(p[o + 2]) << 8) | quint32(p[o + 3]);
    };
    if (u16(2) != 42)
        return 1;
    const quint32 ifd = u32(4);
    if (ifd < 8 || qsizetype(ifd) + 2 > n)
        return 1;
    const unsigned count = u16(ifd);
    for (unsigned i = 0; i < count; ++i) {
        const qsizetype e = qsizetype(ifd) + 2 + qsizetype(i) * 12;
        if (e + 12 > n)
            return 1;
        if (u16(e) == 0x0112 && u16(e + 2) == 3) {
            const unsigned v = u16(e + 8);
            if (set > 0) {
                p[e + 8] = le ? static_cast<unsigned char>(set) : 0;
                p[e + 9] = le ? 0 : static_cast<unsigned char>(set);
            }
            return v >= 1 && v <= 8 ? int(v) : 1;
        }
    }
    return 1;
}

// Blocco EXIF minimo con il solo orientamento (per le foto ruotate quando si tolgono i dati personali).
QByteArray minimalExif(int orientation)
{
    const unsigned char b[] = {'E', 'x', 'i', 'f', 0, 0, 'I', 'I', 42, 0, 8, 0, 0, 0, 1, 0, 0x12, 0x01, 3, 0, 1, 0, 0, 0,
                               static_cast<unsigned char>(orientation), 0, 0, 0, 0, 0, 0, 0};
    return QByteArray(reinterpret_cast<const char*>(b), sizeof b);
}

bool startsWith(const QByteArray& d, const char* prefix, int len)
{
    return d.size() >= len && std::memcmp(d.constData(), prefix, size_t(len)) == 0;
}

JpegInfo readJpegInfo(const QByteArray& data)
{
    JpegInfo info;
    jpeg_decompress_struct cinfo;
    JpegErr err;
    setupErr(err);
    cinfo.err = &err.pub;
    unsigned char* icc = nullptr;
    unsigned int iccLen = 0;
    if (setjmp(err.jump)) {
        jpeg_destroy_decompress(&cinfo);
        std::free(icc);
        return JpegInfo{};
    }
    jpeg_create_decompress(&cinfo);
    jpeg_mem_src(&cinfo, reinterpret_cast<const unsigned char*>(data.constData()), static_cast<unsigned long>(data.size()));
    jpeg_save_markers(&cinfo, JPEG_COM, 0xFFFF);
    for (int m = 0; m < 16; ++m)
        jpeg_save_markers(&cinfo, JPEG_APP0 + m, 0xFFFF);
    jpeg_read_header(&cinfo, TRUE);
    jpeg_read_icc_profile(&cinfo, &icc, &iccLen);
    // Da qui niente più chiamate che possono fallire: i dati vanno negli oggetti Qt.
    info.ok = true;
    info.width = int(cinfo.image_width);
    info.height = int(cinfo.image_height);
    info.components = cinfo.num_components;
    info.colorSpace = cinfo.jpeg_color_space;
    if (cinfo.num_components > 0) {
        info.hSamp = cinfo.comp_info[0].h_samp_factor;
        info.vSamp = cinfo.comp_info[0].v_samp_factor;
    }
    info.quality = estimateQuality(cinfo.quant_tbl_ptrs[0]);
    for (jpeg_saved_marker_ptr m = cinfo.marker_list; m; m = m->next)
        info.markers.push_back({int(m->marker), QByteArray(reinterpret_cast<const char*>(m->data), int(m->data_length))});
    if (icc && iccLen > 0)
        info.icc = QByteArray(reinterpret_cast<const char*>(icc), int(iccLen));
    std::free(icc);
    jpeg_destroy_decompress(&cinfo);
    for (Marker& m : info.markers)
        if (m.code == JPEG_APP0 + 1 && startsWith(m.data, "Exif\0\0", 6)) {
            info.orientation = exifOrientation(&m.data);
            break;
        }
    return info;
}

// Marcatori da riscrivere: con stripPrivate solo il profilo colore (ICC) e, se la foto è ruotata, un EXIF minimo con
// l'orientamento; altrimenti tutti (EXIF con l'orientamento corretto se i pixel sono già stati ruotati).
std::vector<Marker> markersToWrite(const JpegInfo& info, bool stripPrivate, int orientation)
{
    std::vector<Marker> out;
    for (const Marker& m : info.markers) {
        if (m.code == JPEG_APP0 && startsWith(m.data, "JFIF\0", 5))
            continue;   // lo scrive la libreria
        if (m.code == JPEG_APP0 + 14 && startsWith(m.data, "Adobe", 5))
            continue;   // idem (CMYK)
        const bool icc = m.code == JPEG_APP0 + 2 && startsWith(m.data, "ICC_PROFILE\0", 12);
        if (stripPrivate && !icc)
            continue;
        Marker copy = m;
        if (copy.code == JPEG_APP0 + 1 && startsWith(copy.data, "Exif\0\0", 6))
            exifOrientation(&copy.data, orientation);
        out.push_back(std::move(copy));
    }
    if (stripPrivate && orientation > 1)
        out.insert(out.begin(), {JPEG_APP0 + 1, minimalExif(orientation)});
    return out;
}

// JPEG ricodificato senza decomprimere (stessi coefficienti DCT): progressivo e con tabelle di Huffman ottimizzate.
// L'immagine decodificata è identica. Fallisce con i file danneggiati (anche solo con avvisi della libreria).
bool jpegLossless(const QByteArray& data, const std::vector<Marker>& markers, QByteArray* result)
{
    jpeg_decompress_struct src;
    jpeg_compress_struct dst;
    JpegErr err;   // un solo gestore per lettura e scrittura: un solo punto di ritorno
    setupErr(err);
    src.err = &err.pub;
    dst.err = &err.pub;
    unsigned char* out = nullptr;
    unsigned long outSize = 0;
    volatile bool srcReady = false, dstReady = false;
    if (setjmp(err.jump)) {
        if (dstReady)
            jpeg_destroy_compress(&dst);
        if (srcReady)
            jpeg_destroy_decompress(&src);
        std::free(out);
        return false;
    }
    jpeg_create_decompress(&src);
    srcReady = true;
    jpeg_mem_src(&src, reinterpret_cast<const unsigned char*>(data.constData()), static_cast<unsigned long>(data.size()));
    jpeg_read_header(&src, TRUE);
    jvirt_barray_ptr* coef = jpeg_read_coefficients(&src);
    if (err.pub.num_warnings > 0)
        longjmp(err.jump, 1);
    jpeg_create_compress(&dst);
    dstReady = true;
    jpeg_mem_dest(&dst, &out, &outSize);
    jpeg_copy_critical_parameters(&src, &dst);
    dst.optimize_coding = TRUE;
    jpeg_simple_progression(&dst);
    jpeg_write_coefficients(&dst, coef);
    for (const Marker& m : markers)
        jpeg_write_marker(&dst, m.code, reinterpret_cast<const JOCTET*>(m.data.constData()), static_cast<unsigned int>(m.data.size()));
    jpeg_finish_compress(&dst);
    jpeg_finish_decompress(&src);
    const bool clean = err.pub.num_warnings == 0;
    jpeg_destroy_compress(&dst);
    jpeg_destroy_decompress(&src);
    if (clean && out && outSize > 0)
        *result = QByteArray(reinterpret_cast<const char*>(out), qsizetype(outSize));
    std::free(out);
    return clean && !result->isEmpty();
}

// Decodifica in RGB888 (o Grayscale8), senza applicare l'orientamento.
bool jpegDecode(const QByteArray& data, QImage* image)
{
    jpeg_decompress_struct cinfo;
    JpegErr err;
    setupErr(err);
    cinfo.err = &err.pub;
    unsigned char* volatile pixels = nullptr;
    if (setjmp(err.jump)) {
        jpeg_destroy_decompress(&cinfo);
        std::free(pixels);
        return false;
    }
    jpeg_create_decompress(&cinfo);
    jpeg_mem_src(&cinfo, reinterpret_cast<const unsigned char*>(data.constData()), static_cast<unsigned long>(data.size()));
    jpeg_read_header(&cinfo, TRUE);
    const bool gray = cinfo.jpeg_color_space == JCS_GRAYSCALE;
    cinfo.out_color_space = gray ? JCS_GRAYSCALE : JCS_RGB;
    jpeg_start_decompress(&cinfo);
    const size_t w = cinfo.output_width, h = cinfo.output_height, comps = size_t(cinfo.output_components);
    if (w == 0 || h == 0 || qint64(w) * qint64(h) > kMaxPixels)
        longjmp(err.jump, 1);
    const size_t stride = w * comps;
    pixels = static_cast<unsigned char*>(std::malloc(stride * h));
    if (!pixels)
        longjmp(err.jump, 1);
    while (cinfo.output_scanline < cinfo.output_height) {
        JSAMPROW row = pixels + size_t(cinfo.output_scanline) * stride;
        jpeg_read_scanlines(&cinfo, &row, 1);
    }
    jpeg_finish_decompress(&cinfo);
    const bool clean = cinfo.err->num_warnings == 0;
    jpeg_destroy_decompress(&cinfo);
    if (clean) {
        QImage img(int(w), int(h), gray ? QImage::Format_Grayscale8 : QImage::Format_RGB888);
        if (!img.isNull()) {
            for (size_t y = 0; y < h; ++y)
                std::memcpy(img.scanLine(int(y)), pixels + y * stride, stride);
            *image = img;
        }
    }
    std::free(pixels);
    return clean && !image->isNull();
}

// Codifica JPEG: progressiva, Huffman ottimizzato, stesso campionamento della luminanza dell'originale, profilo ICC e
// marcatori indicati.
bool jpegEncode(const QImage& image, int quality, int hSamp, int vSamp, const std::vector<Marker>& markers,
                const QByteArray& icc, QByteArray* result)
{
    const bool gray = image.format() == QImage::Format_Grayscale8;
    const QImage img = gray ? image : image.convertToFormat(QImage::Format_RGB888);
    jpeg_compress_struct cinfo;
    JpegErr err;
    setupErr(err);
    cinfo.err = &err.pub;
    unsigned char* out = nullptr;
    unsigned long outSize = 0;
    if (setjmp(err.jump)) {
        jpeg_destroy_compress(&cinfo);
        std::free(out);
        return false;
    }
    jpeg_create_compress(&cinfo);
    jpeg_mem_dest(&cinfo, &out, &outSize);
    cinfo.image_width = JDIMENSION(img.width());
    cinfo.image_height = JDIMENSION(img.height());
    cinfo.input_components = gray ? 1 : 3;
    cinfo.in_color_space = gray ? JCS_GRAYSCALE : JCS_RGB;
    jpeg_set_defaults(&cinfo);
    jpeg_set_quality(&cinfo, quality, TRUE);
    if (!gray) {
        cinfo.comp_info[0].h_samp_factor = std::clamp(hSamp, 1, 2);
        cinfo.comp_info[0].v_samp_factor = std::clamp(vSamp, 1, 2);
    }
    cinfo.optimize_coding = TRUE;
    jpeg_simple_progression(&cinfo);
    if (img.dotsPerMeterX() > 0 && img.dotsPerMeterY() > 0) {
        cinfo.density_unit = 1;   // punti per pollice
        cinfo.X_density = UINT16(std::clamp(qRound(img.dotsPerMeterX() * 0.0254), 1, 65535));
        cinfo.Y_density = UINT16(std::clamp(qRound(img.dotsPerMeterY() * 0.0254), 1, 65535));
    }
    jpeg_start_compress(&cinfo, TRUE);
    if (!icc.isEmpty())
        jpeg_write_icc_profile(&cinfo, reinterpret_cast<const JOCTET*>(icc.constData()), static_cast<unsigned int>(icc.size()));
    for (const Marker& m : markers)
        if (!(m.code == JPEG_APP0 + 2 && startsWith(m.data, "ICC_PROFILE\0", 12)))   // il profilo è già scritto
            jpeg_write_marker(&cinfo, m.code, reinterpret_cast<const JOCTET*>(m.data.constData()), static_cast<unsigned int>(m.data.size()));
    while (cinfo.next_scanline < cinfo.image_height) {
        JSAMPROW row = const_cast<JSAMPROW>(img.constScanLine(int(cinfo.next_scanline)));
        jpeg_write_scanlines(&cinfo, &row, 1);
    }
    jpeg_finish_compress(&cinfo);
    jpeg_destroy_compress(&cinfo);
    if (out && outSize > 0)
        *result = QByteArray(reinterpret_cast<const char*>(out), qsizetype(outSize));
    std::free(out);
    return !result->isEmpty();
}

// Pixel nell'orientamento giusto (EXIF 1-8).
QImage oriented(const QImage& img, int orientation)
{
    QTransform rot90;
    rot90.rotate(90);
    QTransform rot180;
    rot180.rotate(180);
    QTransform rot270;
    rot270.rotate(270);
    switch (orientation) {
    case 2: return img.flipped(Qt::Horizontal);
    case 3: return img.transformed(rot180);
    case 4: return img.flipped(Qt::Vertical);
    case 5: return img.transformed(rot90).flipped(Qt::Horizontal);
    case 6: return img.transformed(rot90);
    case 7: return img.transformed(rot90).flipped(Qt::Vertical);
    case 8: return img.transformed(rot270);
    default: return img;
    }
}

QImage limitSide(const QImage& img, int maxSide, bool* resized)
{
    *resized = false;
    if (maxSide <= 0 || std::max(img.width(), img.height()) <= maxSide)
        return img;
    *resized = true;
    return img.width() >= img.height() ? img.scaledToWidth(maxSide, Qt::SmoothTransformation)
                                       : img.scaledToHeight(maxSide, Qt::SmoothTransformation);
}

// ------------------------------------------------------------------ somiglianza (SSIM)
//
// "Senza perdita visibile" è misurato: indice di somiglianza strutturale (SSIM) sulla luminanza, a blocchi di 8x8, tra i
// pixel di partenza e quelli del risultato. Le trasparenze contano come fusione su fondo bianco.

struct Luma {
    int w = 0, h = 0;
    std::vector<unsigned char> y;   // 1 byte per pixel: con le foto grandi la memoria conta
};

Luma lumaOf(const QImage& image)
{
    const QImage img = image.format() == QImage::Format_RGB888 || image.format() == QImage::Format_Grayscale8
                           ? image
                           : image.convertToFormat(QImage::Format_ARGB32);
    Luma l;
    l.w = img.width();
    l.h = img.height();
    l.y.resize(size_t(l.w) * size_t(l.h));
    for (int y = 0; y < l.h; ++y) {
        unsigned char* out = &l.y[size_t(y) * size_t(l.w)];
        if (img.format() == QImage::Format_Grayscale8) {
            std::memcpy(out, img.constScanLine(y), size_t(l.w));
        } else if (img.format() == QImage::Format_RGB888) {
            const uchar* row = img.constScanLine(y);
            for (int x = 0; x < l.w; ++x)
                out[x] = static_cast<unsigned char>((299 * row[x * 3] + 587 * row[x * 3 + 1] + 114 * row[x * 3 + 2] + 500) / 1000);
        } else {
            const QRgb* row = reinterpret_cast<const QRgb*>(img.constScanLine(y));
            for (int x = 0; x < l.w; ++x) {
                const QRgb c = row[x];
                const int a = qAlpha(c);
                const int v = (299 * qRed(c) + 587 * qGreen(c) + 114 * qBlue(c) + 500) / 1000;
                out[x] = static_cast<unsigned char>((v * a + 255 * (255 - a) + 127) / 255);
            }
        }
    }
    return l;
}

double ssim(const Luma& a, const Luma& b)
{
    if (a.w != b.w || a.h != b.h || a.w < 8 || a.h < 8)
        return 0.0;
    const double c1 = 6.5025, c2 = 58.5225;
    double total = 0.0;
    long n = 0;
    for (int by = 0; by + 8 <= a.h; by += 8)
        for (int bx = 0; bx + 8 <= a.w; bx += 8) {
            long sa = 0, sb = 0, saa = 0, sbb = 0, sab = 0;
            for (int y = 0; y < 8; ++y) {
                const unsigned char* ra = &a.y[size_t(by + y) * size_t(a.w) + size_t(bx)];
                const unsigned char* rb = &b.y[size_t(by + y) * size_t(b.w) + size_t(bx)];
                for (int x = 0; x < 8; ++x) {
                    sa += ra[x];
                    sb += rb[x];
                    saa += long(ra[x]) * ra[x];
                    sbb += long(rb[x]) * rb[x];
                    sab += long(ra[x]) * rb[x];
                }
            }
            const double ma = sa / 64.0, mb = sb / 64.0;
            const double va = (saa - 64 * ma * ma) / 63, vb = (sbb - 64 * mb * mb) / 63, cov = (sab - 64 * ma * mb) / 63;
            total += ((2 * ma * mb + c1) * (2 * cov + c2)) / ((ma * ma + mb * mb + c1) * (va + vb + c2));
            ++n;
        }
    return n ? total / double(n) : 0.0;
}

// Soglie: misurate su foto vere (raccolta Kodak) e schermate; sotto, le differenze si cominciano a vedere da vicino.
constexpr double kMinSimilarityPhoto = 0.975;
constexpr double kMinSimilarityPng = 0.985;
// Qualità JPEG provate, dalla più bassa: si tiene la prima che resta sopra la soglia. Le foto già compresse sotto la
// prima qualità utile non si ricomprimono (perderebbero altro senza guadagno).
const int kJpegQualities[] = {80, 84, 87, 90, 92};
constexpr int kJpegRecompressAbove = 84;

// La qualità più bassa che resta sopra la soglia (ricerca binaria: la somiglianza cresce con la qualità).
bool jpegEncodeSimilar(const QImage& img, int hSamp, int vSamp, const std::vector<Marker>& markers, const QByteArray& icc,
                       QByteArray* result, int* quality, double* similarity)
{
    const Luma ref = lumaOf(img);
    int lo = 0, hi = int(std::size(kJpegQualities)) - 1, found = -1;
    QByteArray best;
    double bestSim = 0.0;
    while (lo <= hi) {
        const int mid = (lo + hi) / 2;
        QByteArray enc;
        QImage dec;
        if (!jpegEncode(img, kJpegQualities[mid], hSamp, vSamp, markers, icc, &enc) || !jpegDecode(enc, &dec))
            return false;
        const double s = ssim(ref, lumaOf(dec));
        if (s >= kMinSimilarityPhoto) {
            found = mid;
            best = enc;
            bestSim = s;
            hi = mid - 1;
        } else {
            lo = mid + 1;
        }
    }
    if (found < 0)
        return false;
    *result = best;
    *quality = kJpegQualities[found];
    *similarity = bestSim;
    return true;
}

Outcome compressJpeg(const QByteArray& data, const Options& opt)
{
    Outcome o;
    const JpegInfo info = readJpegInfo(data);
    if (!info.ok) {
        o.error = QStringLiteral("JPEG non valido o danneggiato");
        return o;
    }
    if (qint64(info.width) * info.height > kMaxPixels) {
        o.error = QStringLiteral("immagine troppo grande (oltre 120 milioni di pixel)");
        return o;
    }
    struct Candidate {
        QByteArray data;
        QString method;
    };
    std::vector<Candidate> candidates;
    const bool cmyk = info.colorSpace == JCS_CMYK || info.colorSpace == JCS_YCCK;
    const bool visual = opt.quality == Quality::Visual && !cmyk;
    const int longSide = std::max(info.width, info.height);
    const bool resize = visual && opt.maxSide > 0 && longSide > opt.maxSide;

    if (!resize) {
        QByteArray lossless;
        if (jpegLossless(data, markersToWrite(info, opt.stripPrivate, info.orientation), &lossless))
            candidates.push_back({lossless, QStringLiteral("ottimizzata senza perdita")});
    }
    if (visual && (resize || info.quality > kJpegRecompressAbove || info.quality < 0)) {
        QImage img;
        if (jpegDecode(data, &img)) {
            bool resized = false;
            img = limitSide(oriented(img, info.orientation), resize ? opt.maxSide : 0, &resized);
            QByteArray encoded;
            int quality = 0;
            double similarity = 0.0;
            // I pixel sono già nell'orientamento giusto: nell'EXIF eventualmente tenuto l'orientamento diventa 1.
            const auto markers = markersToWrite(info, opt.stripPrivate, 1);
            bool done = jpegEncodeSimilar(img, info.hSamp, info.vSamp, markers, info.icc, &encoded, &quality, &similarity);
            if (!done && resized) {   // ridimensionata: serve comunque un risultato, alla qualità più alta provata
                quality = kJpegQualities[std::size(kJpegQualities) - 1];
                done = jpegEncode(img, quality, info.hSamp, info.vSamp, markers, info.icc, &encoded);
            }
            const QString sim = similarity > 0 ? QStringLiteral(", somiglianza %1%").arg(QString::number(similarity * 100.0, 'f', 1).replace(u'.', u',')) : QString();
            if (done)
                candidates.push_back({encoded, resized ? QStringLiteral("ridimensionata a %1 × %2, qualità %3").arg(img.width()).arg(img.height()).arg(quality) + sim
                                                       : QStringLiteral("ricompressa a qualità %1 (era circa %2)").arg(quality).arg(info.quality) + sim});
        }
    }
    if (candidates.empty()) {
        o.error = QStringLiteral("JPEG danneggiato: non si può ricodificare senza rischi");
        return o;
    }
    const auto best = std::min_element(candidates.begin(), candidates.end(),
                                       [](const Candidate& a, const Candidate& b) { return a.data.size() < b.data.size(); });
    o.ok = true;
    if (!resize && best->data.size() >= data.size()) {
        o.unchanged = true;
        o.data = data;
        o.method = QStringLiteral("già leggera: lasciata com'era");
    } else {
        o.data = best->data;
        o.method = best->method;
    }
    return o;
}

// ================================================================== PNG

struct Chunk {
    QByteArray type;
    qsizetype offset = 0;   // inizio del blocco (lunghezza)
    qsizetype size = 0;     // blocco intero: lunghezza + tipo + dati + CRC
};

const char kPngSig[8] = {char(0x89), 'P', 'N', 'G', '\r', '\n', char(0x1A), '\n'};

std::vector<Chunk> pngChunks(const QByteArray& d)
{
    std::vector<Chunk> out;
    if (!startsWith(d, kPngSig, 8))
        return out;
    qsizetype pos = 8;
    while (pos + 12 <= d.size()) {
        const auto* p = reinterpret_cast<const unsigned char*>(d.constData()) + pos;
        const quint32 len = (quint32(p[0]) << 24) | (quint32(p[1]) << 16) | (quint32(p[2]) << 8) | quint32(p[3]);
        if (len > 0x7FFFFFFFu || pos + 12 + qsizetype(len) > d.size())
            break;
        out.push_back({d.mid(pos + 4, 4), pos, 12 + qsizetype(len)});
        pos += 12 + qsizetype(len);
        if (out.back().type == "IEND")
            break;
    }
    return out;
}

bool hasChunk(const std::vector<Chunk>& chunks, const char* type)
{
    return std::any_of(chunks.begin(), chunks.end(), [&](const Chunk& c) { return c.type == type; });
}

int pngColorType(const QByteArray& d, const std::vector<Chunk>& chunks)
{
    if (chunks.empty() || chunks.front().type != "IHDR" || chunks.front().size < 25)
        return -1;
    return static_cast<unsigned char>(d.at(chunks.front().offset + 8 + 9));
}

// Blocchi accessori: del PNG scritto qui restano solo quelli essenziali (IHDR, PLTE, tRNS, IDAT, IEND); dall'originale
// si riprendono colore (profilo, gamma, cromaticità, sRGB) e risoluzione e, se non si tolgono i dati personali, testi,
// EXIF e data. Vanno subito dopo IHDR (prima di PLTE e IDAT). Il profilo colore non si riporta se l'immagine è passata
// da colori a grigi o viceversa (non sarebbe più valido).
QByteArray transplantChunks(const QByteArray& original, const QByteArray& result, bool stripPrivate)
{
    std::vector<QByteArray> types = {"iCCP", "sRGB", "gAMA", "cHRM", "pHYs"};
    if (!stripPrivate)
        types.insert(types.end(), {"tEXt", "zTXt", "iTXt", "eXIf", "tIME"});
    auto wanted = [&](const QByteArray& t) { return std::find(types.begin(), types.end(), t) != types.end(); };
    auto essential = [](const QByteArray& t) { return t == "IHDR" || t == "PLTE" || t == "tRNS" || t == "IDAT" || t == "IEND"; };
    const auto src = pngChunks(original), dst = pngChunks(result);
    if (dst.empty() || dst.front().type != "IHDR" || dst.back().type != "IEND")
        return {};
    const int srcType = pngColorType(original, src), dstType = pngColorType(result, dst);
    const bool srcGray = srcType == 0 || srcType == 4, dstGray = dstType == 0 || dstType == 4;
    QByteArray out = result.left(8);
    out += result.mid(dst.front().offset, dst.front().size);
    for (const Chunk& c : src)
        if (wanted(c.type) && !(srcGray != dstGray && c.type == "iCCP"))
            out += original.mid(c.offset, c.size);
    for (size_t i = 1; i < dst.size(); ++i)
        if (essential(dst[i].type))
            out += result.mid(dst[i].offset, dst[i].size);
    return out;
}

// Ricompressione senza perdita con zopflipng (riduzione del tipo di colore quando possibile, scelta dei filtri).
// Accurata: deflate di Zopfli, il più compatto ma lento (secondi per megabyte). Altrimenti deflate normale con tre
// strategie di filtro: quasi tutto il guadagno in una frazione del tempo.
bool pngLossless(const QByteArray& png, bool thorough, QByteArray* result)
{
    ZopfliPNGOptions o;
    o.lossy_transparent = false;
    o.lossy_8bit = false;
    o.keepchunks.clear();   // i blocchi accessori li rimette transplantChunks
    o.use_zopfli = thorough;
    o.auto_filter_strategy = false;
    if (thorough)
        o.filter_strategies = {kStrategyZero, kStrategyMinSum};
    else
        o.filter_strategies = {kStrategyZero, kStrategyMinSum, kStrategyEntropy};
    o.num_iterations = 2;
    o.num_iterations_large = 1;
    std::vector<unsigned char> in(png.begin(), png.end()), out;
    if (ZopfliPNGOptimize(in, o, false, &out) != 0 || out.empty())
        return false;
    *result = QByteArray(reinterpret_cast<const char*>(out.data()), qsizetype(out.size()));
    return true;
}

// PNG con la compressione normale di libpng (zlib al massimo): stima veloce per confrontare le prove e risultato per le
// immagini troppo grandi per Zopfli.
QByteArray writePng(const QImage& img)
{
    QByteArray bytes;
    QBuffer buf(&bytes);
    buf.open(QIODevice::WriteOnly);
    QImageWriter w(&buf, "png");
    w.setCompression(100);   // per il PNG Qt va da 0 (nessuna) a 100 (zlib al massimo)
    if (!w.write(img))
        return {};
    return bytes;
}

constexpr double kZopfliMaxSeconds = 6.0;

// Tavolozza di 256 colori (libimagequant) se la qualità resta almeno kPngMinQuality: senza differenze visibili.
constexpr int kPngMinQuality = 90;

bool pngQuantize(const QImage& image, QImage* result, int* quality)
{
    const QImage rgba = image.convertToFormat(QImage::Format_RGBA8888);
    liq_attr* attr = liq_attr_create();
    if (!attr)
        return false;
    liq_set_quality(attr, kPngMinQuality, 100);
    liq_set_speed(attr, 4);
    std::vector<void*> rows(size_t(rgba.height()));
    for (int y = 0; y < rgba.height(); ++y)
        rows[size_t(y)] = const_cast<uchar*>(rgba.constScanLine(y));
    liq_image* li = liq_image_create_rgba_rows(attr, rows.data(), rgba.width(), rgba.height(), 0);
    liq_result* res = nullptr;
    bool ok = li && liq_image_quantize(li, attr, &res) == LIQ_OK && res;
    if (ok) {
        liq_set_dithering_level(res, 1.0f);
        QImage indexed(rgba.width(), rgba.height(), QImage::Format_Indexed8);
        std::vector<unsigned char*> out(size_t(rgba.height()));
        for (int y = 0; y < rgba.height(); ++y)
            out[size_t(y)] = indexed.scanLine(y);
        ok = !indexed.isNull() && liq_write_remapped_image_rows(res, li, out.data()) == LIQ_OK;
        if (ok) {
            const liq_palette* pal = liq_get_palette(res);
            QList<QRgb> table;
            for (unsigned i = 0; i < pal->count; ++i)
                table.append(qRgba(pal->entries[i].r, pal->entries[i].g, pal->entries[i].b, pal->entries[i].a));
            indexed.setColorTable(table);
            indexed.setDotsPerMeterX(image.dotsPerMeterX());
            indexed.setDotsPerMeterY(image.dotsPerMeterY());
            *quality = liq_get_quantization_quality(res);
            *result = indexed;
        }
    }
    if (res)
        liq_result_destroy(res);
    if (li)
        liq_image_destroy(li);
    liq_attr_destroy(attr);
    return ok;
}

Outcome compressPng(const QByteArray& data, const Options& opt)
{
    Outcome o;
    const auto chunks = pngChunks(data);
    if (chunks.empty() || chunks.front().type != "IHDR" || !hasChunk(chunks, "IDAT")) {
        o.error = QStringLiteral("PNG non valido o danneggiato");
        return o;
    }
    if (hasChunk(chunks, "acTL")) {   // animazione: le prove la perderebbero
        o.ok = true;
        o.unchanged = true;
        o.data = data;
        o.method = QStringLiteral("animata: lasciata com'era");
        return o;
    }
    QBuffer buf(const_cast<QByteArray*>(&data));
    buf.open(QIODevice::ReadOnly);
    QImageReader reader(&buf, "png");
    reader.setAutoTransform(false);
    const QSize size = reader.size();
    if (!size.isValid() || qint64(size.width()) * size.height() > kMaxPixels) {
        o.error = size.isValid() ? QStringLiteral("immagine troppo grande (oltre 120 milioni di pixel)")
                                 : QStringLiteral("PNG non valido o danneggiato");
        return o;
    }
    // I byte del file li legge solo libpng (tramite Qt), aggiornata e collaudata sui file costruiti apposta. Zopfli, che
    // contiene un suo lettore PNG più vecchio, riceve solo PNG scritti qui.
    const QImage original = reader.read();
    if (original.isNull()) {
        o.error = QStringLiteral("PNG danneggiato: non si può ricomprimere");
        return o;
    }
    // Le prove si confrontano con la compressione normale (veloce); quella accurata si fa solo sulla migliore.
    struct Candidate {
        QByteArray png;
        double zopfliSeconds = 0.0;   // stima del tempo della compressione accurata
        QString method;
    };
    std::vector<Candidate> candidates;
    auto add = [&](const QImage& img, const QString& method) {
        const QByteArray png = writePng(img);
        // Misurato: circa 2,4 s per MB di dati con tavolozza o grigi, 6,3 s per MB a colori pieni.
        const double megabytes = double(img.width()) * img.height() * (img.depth() / 8.0) / 1e6;
        if (!png.isEmpty())
            candidates.push_back({png, megabytes * (img.depth() <= 8 ? 2.4 : 6.3), method});
    };
    const bool visual = opt.quality == Quality::Visual;
    const bool resize = visual && opt.maxSide > 0 && std::max(size.width(), size.height()) > opt.maxSide;

    if (!resize)
        add(original, QStringLiteral("ottimizzata senza perdita"));
    if (visual) {
        bool resized = false;
        QImage img = limitSide(original, resize ? opt.maxSide : 0, &resized);
        const QString prefix = resized ? QStringLiteral("ridimensionata a %1 × %2, ").arg(img.width()).arg(img.height()) : QString();
        if (img.depth() > 32) {   // 16 bit per canale: a occhio non si distinguono da 8
            img = img.convertToFormat(img.hasAlphaChannel() ? QImage::Format_ARGB32 : QImage::Format_RGB32);
            add(img, prefix + QStringLiteral("8 bit per canale"));
        } else if (resized) {
            add(img, QStringLiteral("ridimensionata a %1 × %2").arg(img.width()).arg(img.height()));
        }
        QImage indexed;
        int quality = 0;
        if (pngQuantize(img, &indexed, &quality) && ssim(lumaOf(img), lumaOf(indexed)) >= kMinSimilarityPng)
            add(indexed, prefix + QStringLiteral("256 colori (qualità %1/100)").arg(quality));
    }
    if (candidates.empty()) {
        o.error = QStringLiteral("PNG danneggiato: non si può ricomprimere");
        return o;
    }
    const auto best = std::min_element(candidates.begin(), candidates.end(),
                                       [](const Candidate& a, const Candidate& b) { return a.png.size() < b.png.size(); });
    QByteArray result = transplantChunks(data, best->png, opt.stripPrivate);
    // Ricompressione della prova migliore: accurata (Zopfli) se si fa in pochi secondi, altrimenti quella veloce; le
    // immagini enormi restano con la compressione normale.
    if (best->zopfliSeconds <= kZopfliMaxSeconds * 40) {
        QByteArray thorough;
        if (pngLossless(best->png, best->zopfliSeconds <= kZopfliMaxSeconds, &thorough)) {
            thorough = transplantChunks(data, thorough, opt.stripPrivate);
            if (!thorough.isEmpty() && (result.isEmpty() || thorough.size() < result.size()))
                result = thorough;
        }
    }
    if (result.isEmpty()) {
        o.error = QStringLiteral("PNG danneggiato: non si può ricomprimere");
        return o;
    }
    o.ok = true;
    if (!resize && result.size() >= data.size()) {
        o.unchanged = true;
        o.data = data;
        o.method = QStringLiteral("già leggera: lasciata com'era");
    } else {
        o.data = result;
        o.method = best->method;
    }
    return o;
}

} // namespace

Kind detect(const QByteArray& data)
{
    if (data.size() >= 3 && static_cast<unsigned char>(data[0]) == 0xFF && static_cast<unsigned char>(data[1]) == 0xD8 &&
        static_cast<unsigned char>(data[2]) == 0xFF)
        return Kind::Jpeg;
    if (startsWith(data, kPngSig, 8))
        return Kind::Png;
    return Kind::Unknown;
}

QImage decodeImage(const QByteArray& data)
{
    if (data.size() > kMaxFileBytes)
        return {};
    if (detect(data) == Kind::Jpeg) {
        const JpegInfo info = readJpegInfo(data);
        QImage img;
        if (!info.ok || qint64(info.width) * info.height > kMaxPixels || !jpegDecode(data, &img))
            return {};
        return oriented(img, info.orientation);
    }
    if (detect(data) == Kind::Png) {
        QBuffer buf(const_cast<QByteArray*>(&data));
        buf.open(QIODevice::ReadOnly);
        QImageReader reader(&buf, "png");
        const QSize size = reader.size();
        if (!size.isValid() || qint64(size.width()) * size.height() > kMaxPixels)
            return {};
        return reader.read();
    }
    return {};
}

QByteArray testing::encodeJpeg(const QImage& image, int quality, const QList<QPair<int, QByteArray>>& markers)
{
    std::vector<Marker> list;
    for (const auto& m : markers)
        list.push_back({m.first, m.second});
    QByteArray out;
    jpegEncode(image, quality, 2, 2, list, {}, &out);
    return out;
}

Outcome compress(const QByteArray& data, const Options& options)
{
    if (data.size() > kMaxFileBytes) {
        Outcome o;
        o.error = QStringLiteral("file troppo grande (oltre 300 MB)");
        return o;
    }
    switch (detect(data)) {
    case Kind::Jpeg: return compressJpeg(data, options);
    case Kind::Png: return compressPng(data, options);
    default: {
        Outcome o;
        o.error = QStringLiteral("non è una foto JPEG né un'immagine PNG");
        return o;
    }
    }
}

} // namespace alleggerisci
