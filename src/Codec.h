#pragma once

// Compressione di una foto (JPEG) o di una schermata (PNG) in memoria, senza cambiare formato.
//
// Livelli di qualità:
//  - Exact: "identica al pixel". Solo ottimizzazioni senza perdita: JPEG ricodificato senza decomprimere
//    (stessi coefficienti), PNG ricompresso; l'immagine decodificata è identica a quella di partenza.
//  - Maximum, Medium, Low: JPEG ricompresso alla qualità più bassa che resta sopra una soglia di somiglianza misurata
//    (SSIM) e PNG ridotto a una tavolozza di 256 colori se resta sopra la soglia del PNG. Massima: senza differenze
//    visibili; media: differenze minime, visibili solo ingrandendo; bassa: file molto leggeri, differenze visibili da
//    vicino. In ogni caso si prova anche l'ottimizzazione senza perdita e si tiene il file più piccolo.
//
// Il risultato non è mai più grande dell'originale: se nessuna prova guadagna, resta l'originale (unchanged).
// I file arrivano da chiunque: limiti su dimensione del file e numero di pixel, errori delle librerie gestiti
// senza far cadere il programma.

#include <QByteArray>
#include <QImage>
#include <QList>
#include <QPair>
#include <QString>

namespace alleggerisci {

enum class Quality { Exact, Maximum, Medium, Low };

struct Options {
    Quality quality = Quality::Maximum;
    int maxSide = 0;            // lato lungo massimo in pixel (0 = dimensioni originali; ignorato con Exact)
    bool stripPrivate = true;   // togli posizione GPS, dati della fotocamera, commenti e testi
};

enum class Kind { Unknown, Jpeg, Png };

struct Outcome {
    bool ok = false;
    QByteArray data;            // il file nuovo (uguale all'originale se unchanged)
    bool unchanged = false;     // nessun guadagno: resta l'originale
    QString method;             // come è stato ottenuto, per l'utente
    QString error;
};

// Limiti contro i file costruiti apposta.
constexpr qint64 kMaxFileBytes = 300LL * 1024 * 1024;
constexpr qint64 kMaxPixels = 120LL * 1000 * 1000;

Kind detect(const QByteArray& data);
Outcome compress(const QByteArray& data, const Options& options);

// Immagine decodificata come si vede (foto ruotate secondo l'EXIF), per il confronto prima/dopo. Nulla se il file non
// si legge.
QImage decodeImage(const QByteArray& data);

namespace testing {
// JPEG di prova: qualità, campionamento 4:2:0 e marcatori APPn aggiuntivi (codice, dati), per i test.
QByteArray encodeJpeg(const QImage& image, int quality, const QList<QPair<int, QByteArray>>& markers = {});
} // namespace testing

} // namespace alleggerisci
