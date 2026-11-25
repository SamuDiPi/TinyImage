# Alleggerisci

Foto e schermate più leggere, senza perdita di qualità visibile. Un programmino per Windows: si trascinano dentro le immagini (o cartelle intere) e partono subito.

- **Foto JPEG e immagini PNG**: restano dello stesso formato, quindi si aprono ovunque (Word, posta, telefoni).
- **Senza perdita visibile** (default): le foto si ricomprimono alla qualità più bassa che resta sopra una soglia di somiglianza misurata (SSIM); le schermate passano a una tavolozza di 256 colori solo se la differenza resta sotto la soglia. Risparmio tipico: 30-50% sulle foto, 50-70% sulle schermate.
- **Identica al pixel**: solo ottimizzazioni senza perdita. Ogni pixel resta identico, il risparmio è minore (5-30%).
- **Gli originali non si toccano**: le copie leggere vanno nella cartella `alleggerite` accanto a loro. A scelta, al posto degli originali, che vanno nel Cestino di Windows.
- **Confronto prima/dopo**: doppio clic su una riga, con una linea da trascinare e lo zoom.
- **Dimensioni** (facoltativo): lato lungo massimo 3840, 2560, 1920 o 1280 pixel.
- Un file non diventa mai più grande: se non c'è guadagno resta com'era.

## Scaricare

Dalle [versioni pubblicate](https://github.com/SamuDiPi/TinyImage/releases):

- `Alleggerisci-X.Y.Z-portabile.zip`: **versione portabile**. Si scompatta dove si vuole (anche su una chiavetta) e si avvia `Alleggerisci.exe`. Non installa nulla e non scrive nel registro di Windows: le scelte restano nel file `portabile.ini` della cartella.
- `Alleggerisci-X.Y.Z-installazione.exe`: installatore per l'utente, senza diritti di amministratore, con la voce facoltativa *Invia a > Alleggerisci* nel clic destro.

Windows 10 o 11 a 64 bit. Il programma non è firmato digitalmente: al primo avvio Windows può chiedere conferma (*Ulteriori informazioni > Esegui comunque*).

## Privacy e sicurezza

- **Nessun collegamento a internet, nessun dato raccolto.** Le immagini si elaborano sul computer e non vengono inviate a nessuno. Non ci sono account, statistiche né controlli di aggiornamento.
- **Dati personali nelle foto.** Di default le copie non contengono posizione GPS, modello della fotocamera, data e commenti (EXIF, XMP, testi dei PNG): utile prima di condividere. Restano l'orientamento e il profilo colore. L'opzione si può spegnere.
- **Cosa resta sul computer:** le scelte (qualità, dove salvare, ultima cartella usata, posizione della finestra) nelle impostazioni dell'utente (`HKEY_CURRENT_USER\Software\Alleggerisci`), oppure in `portabile.ini` nella versione portabile.
- **File da fonti non fidate.** I file si leggono con libjpeg-turbo e libpng aggiornate; ci sono limiti di dimensione (300 MB, 120 milioni di pixel) e i file danneggiati vengono rifiutati senza toccarli. I test alterano centinaia di file a caso per controllarlo.
- Per segnalare una vulnerabilità: [SECURITY.md](SECURITY.md).

## Riga di comando

```
Alleggerisci.exe --riga-di-comando [--identica] [--lato N] [--tieni-dati] [--sostituisci] <file o cartelle>...
```

Stampa una riga JSON per immagine e una di riepilogo.

## Compilare

Serve [MSYS2](https://www.msys2.org) (ambiente UCRT64) con Qt 6, libjpeg-turbo, libimagequant e Zopfli:

```
pacman -S mingw-w64-ucrt-x86_64-{toolchain,cmake,ninja,qt6-base,libjpeg-turbo,libimagequant,zopfli}
cmake -S . -B build -G Ninja
cmake --build build
build/alleggerisci_tests.exe
scripts/package.sh <cartella di uscita>     # installatore (Inno Setup 7), zip portabile, sorgenti delle librerie
```

| File | Cosa contiene |
|---|---|
| `src/Codec.*` | Compressione in memoria: JPEG (libjpeg-turbo), PNG (libpng tramite Qt, libimagequant, Zopfli), somiglianza SSIM |
| `src/Files.*` | Ricerca delle immagini nelle cartelle, scrittura dei risultati, Cestino |
| `src/MainWindow.*`, `src/CompareDialog.*` | Finestra e confronto prima/dopo |
| `tests/tests.cpp` | Test del motore senza finestre |

## Licenza

Copyright (C) 2026 SamuDiPi. Alleggerisci è software libero distribuito secondo la [GNU General Public License](LICENSE) versione 3 o successiva, **senza alcuna garanzia**.

Componenti di terze parti, ciascuno con la sua licenza: Qt (LGPL-3.0), libjpeg-turbo (IJG e BSD), libpng e zlib, libimagequant (GPL-3.0-or-later), Zopfli (Apache-2.0) e le librerie da cui dipendono. Ogni pacchetto contiene `LICENZE-TERZE-PARTI.txt` con versioni, licenze e indirizzi dei sorgenti, e la cartella `licenze` con i testi; a ogni versione è allegato l'archivio dei sorgenti delle librerie.

Alleggerisci è un progetto indipendente: non è collegato ad altri programmi o servizi di compressione di immagini. JPEG e PNG indicano solo il formato dei file; Windows è un marchio di Microsoft; i marchi appartengono ai rispettivi titolari.
