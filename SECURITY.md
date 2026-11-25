# Sicurezza

## Versioni seguite

Solo l'ultima versione pubblicata nella pagina Releases e il ramo `main`.

## Segnalare una vulnerabilità

Non aprire una issue pubblica. Usa la segnalazione privata di GitHub: scheda **Security** del repository, **Report a vulnerability**. Indica come riprodurre il problema (per esempio l'immagine che lo provoca), la versione di Alleggerisci (finestra Informazioni) e il sistema operativo.

Alleggerisci è un progetto personale e gratuito: le segnalazioni si esaminano appena possibile, di solito entro due settimane. Le correzioni escono in una nuova versione, con la vulnerabilità indicata nelle note.

## Come sono trattati i file

- Il programma non si collega a internet: nessun aggiornamento automatico, nessun invio di dati.
- Limiti prima di decodificare: 300 MB per file, 120 milioni di pixel per immagine.
- **JPEG**: letti e scritti con libjpeg-turbo. Un file che la libreria segnala come danneggiato (anche con un solo avviso) non viene ricodificato.
- **PNG**: i byte del file li legge solo libpng (tramite Qt). Zopfli contiene un proprio lettore PNG più vecchio (LodePNG): riceve solo PNG scritti da Alleggerisci, mai il file dell'utente. I blocchi accessori dell'originale (profilo colore, risoluzione e, a richiesta, testi) si copiano con un lettore che controlla lunghezze e limiti.
- Il risultato si scrive prima in un file temporaneo e solo alla fine prende il suo nome. Con "Al posto degli originali" l'originale va nel Cestino; se il Cestino non è disponibile resta com'è.
- I test (`tests/tests.cpp`) alterano a caso centinaia di JPEG e PNG e controllano che il programma non si arresti e non produca file più grandi.

## Componenti di terze parti

Elenco completo, con versioni, in `LICENZE-TERZE-PARTI.txt` della versione distribuita; a ogni versione il programma si ricompila con le librerie aggiornate di MSYS2. Verifica del 25 novembre 2026 per la 0.1.0:

- **libjpeg-turbo 3.2.1**: CVE-2026-75466 riguarda la 3.2.0 e il caricamento di PNG dell'interfaccia TurboJPEG, che Alleggerisci non usa. La segnalazione ANT-2026-5ETTYD84 riguarda `jpeg_crop_scanline()` con dati grezzi dopo un'interruzione: funzioni non usate.
- **libpng 1.6.59**: contiene le correzioni note fino a CVE-2026-46675.
- **Zopfli 1.0.3** (con LodePNG): CVE-2022-44081 è nello strumento `pngdetail`, non distribuito; in ogni caso LodePNG non legge mai file dell'utente.
- **libimagequant 4.4.1**, **zlib 1.4**, **Qt 6.12.0** (solo Core, Gui, Widgets: le vulnerabilità note della 6.12.0 sono nei moduli di rete, non distribuiti): nessuna vulnerabilità nota che riguardi l'uso fatto qui.
