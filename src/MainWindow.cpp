#include "MainWindow.h"
#include "CompareDialog.h"

#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QDragEnterEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QFutureWatcher>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLocale>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QStyle>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

namespace alleggerisci {

namespace {

enum Column { ColName, ColBefore, ColAfter, ColSaved, ColNote, ColCount };
constexpr int kInputRole = Qt::UserRole + 1;      // percorso dell'originale
constexpr int kRelativeRole = Qt::UserRole + 2;   // percorso relativo nella cartella scelta
constexpr int kStateRole = Qt::UserRole + 3;
constexpr int kResultRole = Qt::UserRole + 4;     // percorso del risultato
constexpr int kBeforeRole = Qt::UserRole + 5;
constexpr int kAfterRole = Qt::UserRole + 6;
enum State { Ready, Working, Done, Failed };

const int kSizes[] = {0, 3840, 2560, 1920, 1280};

// Pulsanti della qualità, nell'ordine in cui compaiono.
struct LevelChoice {
    Quality quality;
    const char* label;
    const char* help;
};
const LevelChoice kLevels[] = {
    {Quality::Maximum, QT_TRANSLATE_NOOP("MainWindow", "Massima"),
     QT_TRANSLATE_NOOP("MainWindow", "Nessuna differenza visibile. Foto circa −30/−50%, schermate fino a −70%.")},
    {Quality::Medium, QT_TRANSLATE_NOOP("MainWindow", "Media"),
     QT_TRANSLATE_NOOP("MainWindow", "Differenze minime, che si notano solo ingrandendo. Foto circa −50/−70%.")},
    {Quality::Low, QT_TRANSLATE_NOOP("MainWindow", "Bassa"),
     QT_TRANSLATE_NOOP("MainWindow", "File molto leggeri, differenze visibili da vicino. Foto circa −70/−80%.")},
    {Quality::Exact, QT_TRANSLATE_NOOP("MainWindow", "Identica al pixel"),
     QT_TRANSLATE_NOOP("MainWindow", "Ogni pixel resta uguale: solo ottimizzazioni senza perdita, circa −5/−30%.")},
};

enum DestinationIndex { DestSubfolder, DestFolder, DestReplace };

QString percent(qint64 before, qint64 after)
{
    if (before <= 0)
        return {};
    const double p = (1.0 - double(after) / double(before)) * 100.0;
    return QStringLiteral("−%1%").arg(QLocale(QLocale::Italian).toString(p, 'f', p < 10 ? 1 : 0));
}

int stateOf(const QTreeWidgetItem* item)
{
    return item->data(ColName, kStateRole).toInt();
}

} // namespace

MainWindow::MainWindow(QSettings& settings, QWidget* parent) : QMainWindow(parent), settings_(settings)
{
    setWindowTitle(QStringLiteral("Alleggerisci"));
    setAcceptDrops(true);
    // Al massimo 4 immagini insieme: ognuna tiene in memoria i pixel decodificati (centinaia di MB per le foto grandi).
    pool_.setMaxThreadCount(std::clamp(QThread::idealThreadCount() - 1, 1, 4));

    auto* central = new QWidget(this);
    auto* layout = new QVBoxLayout(central);
    layout->setContentsMargins(20, 16, 20, 16);
    layout->setSpacing(12);

    // Titolo.
    auto* header = new QHBoxLayout;
    auto* titles = new QVBoxLayout;
    auto* title = new QLabel(QStringLiteral("Alleggerisci"));
    title->setObjectName(QStringLiteral("title"));
    auto* subtitle = new QLabel(tr("Foto e schermate più leggere. Scegli le immagini, la qualità e dove salvarle, poi premi Avanti. "
                                   "Tutto resta sul tuo computer."));
    subtitle->setObjectName(QStringLiteral("subtitle"));
    subtitle->setWordWrap(true);
    titles->addWidget(title);
    titles->addWidget(subtitle);
    header->addLayout(titles, 1);
    auto* about = new QPushButton(tr("Informazioni"));
    about->setObjectName(QStringLiteral("flat"));
    connect(about, &QPushButton::clicked, this, &MainWindow::showAbout);
    header->addWidget(about, 0, Qt::AlignTop);
    layout->addLayout(header);

    // 1. Immagini.
    dropArea_ = new QFrame;
    dropArea_->setObjectName(QStringLiteral("drop"));
    auto* dropLayout = new QVBoxLayout(dropArea_);
    dropLayout->setAlignment(Qt::AlignCenter);
    dropTitle_ = new QLabel(tr("Trascina qui foto e schermate"));
    dropTitle_->setObjectName(QStringLiteral("dropTitle"));
    dropTitle_->setAlignment(Qt::AlignCenter);
    dropHint_ = new QLabel(tr("JPEG e PNG, anche cartelle intere. Prima entrano nell'elenco: partono solo quando premi Avanti."));
    dropHint_->setObjectName(QStringLiteral("dropHint"));
    dropHint_->setAlignment(Qt::AlignCenter);
    dropHint_->setWordWrap(true);
    auto* chooseRow = new QHBoxLayout;
    chooseRow->setAlignment(Qt::AlignCenter);
    auto* chooseFiles = new QPushButton(tr("Scegli immagini…"));
    chooseFiles->setObjectName(QStringLiteral("primary"));
    auto* chooseFolder = new QPushButton(tr("Scegli una cartella…"));
    connect(chooseFiles, &QPushButton::clicked, this, &MainWindow::chooseFiles);
    connect(chooseFolder, &QPushButton::clicked, this, &MainWindow::chooseFolder);
    chooseRow->addWidget(chooseFiles);
    chooseRow->addWidget(chooseFolder);
    dropLayout->addWidget(dropTitle_);
    dropLayout->addWidget(dropHint_);
    dropLayout->addSpacing(8);
    dropLayout->addLayout(chooseRow);
    layout->addWidget(dropArea_, 1);

    list_ = new QTreeWidget;
    list_->setColumnCount(ColCount);
    list_->setHeaderLabels({tr("Immagine"), tr("Prima"), tr("Dopo"), tr("Risparmio"), tr("Note")});
    list_->setRootIsDecorated(false);
    list_->setUniformRowHeights(true);
    list_->setAlternatingRowColors(true);
    list_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    list_->setContextMenuPolicy(Qt::CustomContextMenu);
    list_->header()->setSectionResizeMode(ColName, QHeaderView::Stretch);
    list_->header()->setSectionResizeMode(ColNote, QHeaderView::Stretch);
    for (int c : {ColBefore, ColAfter, ColSaved})
        list_->header()->setSectionResizeMode(c, QHeaderView::ResizeToContents);
    list_->setToolTip(tr("Canc toglie le immagini selezionate · doppio clic su un'immagine elaborata per confrontare prima e dopo"));
    list_->hide();
    list_->installEventFilter(this);
    connect(list_, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* item) { compare(item); });
    connect(list_, &QTreeWidget::customContextMenuRequested, this, &MainWindow::showListMenu);
    layout->addWidget(list_, 3);

    progress_ = new QProgressBar;
    progress_->setTextVisible(false);
    progress_->setFixedHeight(6);
    progress_->hide();
    layout->addWidget(progress_);

    // 2. Scelte.
    auto* options = new QFrame;
    options->setObjectName(QStringLiteral("options"));
    auto* grid = new QGridLayout(options);
    grid->setHorizontalSpacing(18);
    grid->setVerticalSpacing(6);

    grid->addWidget(new QLabel(tr("Qualità")), 0, 0);
    auto* qualityRow = new QHBoxLayout;
    qualityRow->setSpacing(0);
    quality_ = new QButtonGroup(this);
    for (int i = 0; i < int(std::size(kLevels)); ++i) {
        auto* b = new QPushButton(tr(kLevels[i].label));
        b->setCheckable(true);
        b->setObjectName(QStringLiteral("segment"));
        b->setToolTip(tr(kLevels[i].help));
        quality_->addButton(b, i);
        qualityRow->addWidget(b);
    }
    quality_->setExclusive(true);
    qualityRow->addStretch();
    grid->addLayout(qualityRow, 1, 0);
    qualityHelp_ = new QLabel;
    qualityHelp_->setObjectName(QStringLiteral("help"));
    qualityHelp_->setWordWrap(true);
    grid->addWidget(qualityHelp_, 2, 0);

    grid->addWidget(new QLabel(tr("Dimensioni")), 0, 1);
    size_ = new QComboBox;
    size_->addItems({tr("Originali"), tr("Lato lungo max 3840 px (4K)"), tr("Lato lungo max 2560 px"),
                     tr("Lato lungo max 1920 px (Full HD)"), tr("Lato lungo max 1280 px")});
    size_->setToolTip(tr("Rimpicciolire le foto grandi alleggerisce ancora di più (non con \"Identica al pixel\")."));
    grid->addWidget(size_, 1, 1);

    grid->addWidget(new QLabel(tr("Dove salvare")), 3, 0, 1, 2);
    auto* destRow = new QHBoxLayout;
    destination_ = new QComboBox;
    destination_->addItems({tr("Nella cartella \"alleggerite\" accanto agli originali"), tr("In una cartella a scelta"),
                            tr("Al posto degli originali (originali nel Cestino)")});
    destRow->addWidget(destination_);
    folderButton_ = new QPushButton(tr("Scegli la cartella…"));
    connect(folderButton_, &QPushButton::clicked, this, [this] { chooseOutputFolder(); });
    destRow->addWidget(folderButton_);
    folderLabel_ = new QLabel;
    folderLabel_->setObjectName(QStringLiteral("help"));
    folderLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    destRow->addWidget(folderLabel_, 1);
    grid->addLayout(destRow, 4, 0, 1, 2);

    strip_ = new QCheckBox(tr("Togli posizione GPS e dati della fotocamera (consigliato prima di condividere)"));
    strip_->setToolTip(tr("Toglie posizione, modello della fotocamera, data e commenti nascosti nel file. Orientamento e profilo colore restano."));
    grid->addWidget(strip_, 5, 0, 1, 2);
    grid->setColumnStretch(0, 3);
    grid->setColumnStretch(1, 2);
    layout->addWidget(options);

    // 3. Avanti, riepilogo e azioni.
    auto* bottom = new QHBoxLayout;
    summary_ = new QLabel;
    summary_->setObjectName(QStringLiteral("summary"));
    summary_->setWordWrap(true);
    bottom->addWidget(summary_, 1);
    redoButton_ = new QPushButton(tr("Rifai con queste scelte"));
    clearButton_ = new QPushButton(tr("Svuota elenco"));
    openButton_ = new QPushButton(tr("Apri la cartella"));
    goButton_ = new QPushButton(tr("Avanti  ›"));
    goButton_->setObjectName(QStringLiteral("primary"));
    goButton_->setMinimumWidth(130);
    goButton_->setDefault(true);
    connect(redoButton_, &QPushButton::clicked, this, &MainWindow::redo);
    connect(clearButton_, &QPushButton::clicked, this, [this] {
        list_->clear();
        updateState();
    });
    connect(openButton_, &QPushButton::clicked, this, &MainWindow::openResults);
    connect(goButton_, &QPushButton::clicked, this, &MainWindow::go);
    bottom->addWidget(redoButton_);
    bottom->addWidget(clearButton_);
    bottom->addWidget(openButton_);
    bottom->addWidget(goButton_);
    layout->addLayout(bottom);
    setCentralWidget(central);

    // Scelte salvate.
    const int level = std::clamp(settings_.value(QStringLiteral("livello"), 0).toInt(), 0, int(std::size(kLevels)) - 1);
    quality_->button(level)->setChecked(true);
    size_->setCurrentIndex(std::clamp(settings_.value(QStringLiteral("dimensioni"), 0).toInt(), 0, int(std::size(kSizes)) - 1));
    strip_->setChecked(settings_.value(QStringLiteral("togliDati"), true).toBool());
    destination_->setCurrentIndex(std::clamp(settings_.value(QStringLiteral("destinazione"), 0).toInt(), 0, 2));
    outputFolder_ = settings_.value(QStringLiteral("cartellaScelta")).toString();
    if (!outputFolder_.isEmpty() && !QFileInfo(outputFolder_).isDir())
        outputFolder_.clear();
    lastFolder_ = settings_.value(QStringLiteral("ultimaCartella")).toString();
    if (!QFileInfo(lastFolder_).isDir())
        lastFolder_ = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    connect(quality_, &QButtonGroup::idClicked, this, [this] { updateState(); });
    connect(size_, &QComboBox::currentIndexChanged, this, [this] { updateState(); });
    connect(strip_, &QCheckBox::toggled, this, [this] { updateState(); });
    connect(destination_, &QComboBox::activated, this, [this](int index) {
        if (index == DestFolder && outputFolder_.isEmpty())
            chooseOutputFolder();
        updateState();
    });

    restoreGeometry(settings_.value(QStringLiteral("finestra")).toByteArray());
    if (!settings_.contains(QStringLiteral("finestra")))
        resize(1000, 720);
    updateState();
}

Options MainWindow::options() const
{
    Options o;
    o.quality = kLevels[std::clamp(quality_->checkedId(), 0, int(std::size(kLevels)) - 1)].quality;
    o.maxSide = o.quality != Quality::Exact ? kSizes[std::clamp(size_->currentIndex(), 0, int(std::size(kSizes)) - 1)] : 0;
    o.stripPrivate = strip_->isChecked();
    return o;
}

Destination MainWindow::destination() const
{
    switch (destination_->currentIndex()) {
    case DestFolder: return Destination::Folder;
    case DestReplace: return Destination::Replace;
    default: return Destination::Subfolder;
    }
}

bool MainWindow::chooseOutputFolder()
{
    const QString start = outputFolder_.isEmpty() ? lastFolder_ : outputFolder_;
    const QString dir = QFileDialog::getExistingDirectory(this, tr("Dove salvare le immagini alleggerite"), start);
    if (dir.isEmpty())
        return false;
    outputFolder_ = QFileInfo(dir).absoluteFilePath();
    written_.clear();   // cartella nuova: i file che c'erano non si sovrascrivono
    updateState();
    return true;
}

int MainWindow::readyCount() const
{
    int n = 0;
    for (int i = 0; i < list_->topLevelItemCount(); ++i)
        n += stateOf(list_->topLevelItem(i)) == Ready ? 1 : 0;
    return n;
}

void MainWindow::addPaths(const QStringList& paths)
{
    const QList<ImageItem> files = collectImages(paths, destination() == Destination::Folder ? outputFolder_ : QString());
    if (files.isEmpty()) {
        QMessageBox::information(this, windowTitle(), tr("Nessuna foto JPEG o immagine PNG tra quelle indicate."));
        return;
    }
    if (!paths.isEmpty())
        lastFolder_ = QFileInfo(paths.first()).isDir() ? paths.first() : QFileInfo(paths.first()).absolutePath();
    // Già in elenco e non ancora elaborate: non si ripetono.
    QSet<QString> present;
    for (int i = 0; i < list_->topLevelItemCount(); ++i)
        if (stateOf(list_->topLevelItem(i)) == Ready)
            present.insert(list_->topLevelItem(i)->data(ColName, kInputRole).toString().toLower());
    for (const ImageItem& f : files) {
        if (present.contains(f.path.toLower()))
            continue;
        auto* item = new QTreeWidgetItem(list_);
        item->setText(ColName, QFileInfo(f.path).fileName());
        item->setToolTip(ColName, QDir::toNativeSeparators(f.path));
        item->setText(ColBefore, formatBytes(QFileInfo(f.path).size()));
        item->setText(ColNote, tr("pronta"));
        item->setData(ColName, kInputRole, f.path);
        item->setData(ColName, kRelativeRole, f.relative);
        item->setData(ColName, kStateRole, Ready);
        for (int c : {ColBefore, ColAfter, ColSaved})
            item->setTextAlignment(c, Qt::AlignRight | Qt::AlignVCenter);
    }
    list_->scrollToBottom();
    updateState();
}

void MainWindow::go()
{
    QList<QTreeWidgetItem*> items;
    for (int i = 0; i < list_->topLevelItemCount(); ++i)
        if (stateOf(list_->topLevelItem(i)) == Ready)
            items << list_->topLevelItem(i);
    if (items.isEmpty())
        return;
    if (destination() == Destination::Folder && outputFolder_.isEmpty() && !chooseOutputFolder())
        return;
    if (destination() == Destination::Replace &&
        QMessageBox::question(this, windowTitle(),
                              tr("Le %1 immagini alleggerite prenderanno il posto degli originali. Gli originali vanno nel Cestino di "
                                 "Windows: da lì si possono recuperare finché non lo si svuota. Le immagini già leggere restano come sono.\n\n"
                                 "Continuare?").arg(items.size())) != QMessageBox::Yes)
        return;
    run(items);
}

void MainWindow::run(const QList<QTreeWidgetItem*>& items)
{
    const Options opt = options();
    const Destination dest = destination();
    QSet<QString> used;   // percorsi già assegnati in questo giro nella cartella scelta
    for (QTreeWidgetItem* item : items) {
        const QString input = item->data(ColName, kInputRole).toString();
        QString target;
        if (dest == Destination::Folder) {
            const ImageItem image{input, item->data(ColName, kRelativeRole).toString()};
            target = folderOutputPath(outputFolder_, image, &used, written_);
            written_.insert(target.toLower());
        }
        item->setData(ColName, kStateRole, Working);
        item->setData(ColName, kResultRole, QVariant());
        item->setText(ColAfter, QString());
        item->setText(ColSaved, QString());
        item->setText(ColNote, tr("in corso…"));
        item->setForeground(ColNote, QBrush());
        ++pending_;
        ++total_;
        auto* watcher = new QFutureWatcher<FileResult>(this);
        connect(watcher, &QFutureWatcher<FileResult>::finished, this, [this, watcher, item] {
            finished(item, watcher->result());
            watcher->deleteLater();
        });
        watcher->setFuture(QtConcurrent::run(&pool_, [input, opt, dest, target] { return processFile(input, opt, dest, target); }));
    }
    updateState();
}

void MainWindow::finished(QTreeWidgetItem* item, const FileResult& r)
{
    --pending_;
    item->setText(ColBefore, formatBytes(r.before));
    if (r.ok) {
        item->setData(ColName, kStateRole, Done);
        item->setText(ColAfter, formatBytes(r.after));
        item->setText(ColSaved, r.unchanged ? QStringLiteral("—") : percent(r.before, r.after));
        item->setText(ColNote, r.method);
        item->setToolTip(ColNote, QDir::toNativeSeparators(r.output));
        item->setData(ColName, kResultRole, r.output);
        item->setData(ColName, kBeforeRole, r.before);
        item->setData(ColName, kAfterRole, r.after);
        item->setForeground(ColSaved, r.unchanged ? QBrush() : QBrush(QColor(22, 128, 61)));
    } else {
        item->setData(ColName, kStateRole, Failed);
        item->setText(ColNote, r.error);
        item->setForeground(ColNote, QColor(185, 28, 28));
    }
    updateState();
}

void MainWindow::removeSelected()
{
    for (QTreeWidgetItem* item : list_->selectedItems())
        if (stateOf(item) != Working)
            delete item;
    updateState();
}

void MainWindow::showListMenu(const QPoint& pos)
{
    QTreeWidgetItem* item = list_->itemAt(pos);
    if (!item)
        return;
    QMenu menu(this);
    if (stateOf(item) == Done) {
        menu.addAction(tr("Confronta prima e dopo"), this, [this, item] { compare(item); });
        menu.addAction(tr("Apri la cartella del risultato"), this, [item] {
            QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(item->data(ColName, kResultRole).toString()).absolutePath()));
        });
        menu.addSeparator();
    }
    menu.addAction(tr("Togli dall'elenco"), this, &MainWindow::removeSelected)->setEnabled(stateOf(item) != Working);
    menu.exec(list_->viewport()->mapToGlobal(pos));
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == list_ && event->type() == QEvent::KeyPress) {
        const auto* key = static_cast<QKeyEvent*>(event);
        if (key->key() == Qt::Key_Delete || key->key() == Qt::Key_Backspace) {
            removeSelected();
            return true;
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::updateState()
{
    const bool any = list_->topLevelItemCount() > 0;
    list_->setVisible(any);
    dropArea_->setMaximumHeight(any ? 150 : QWIDGETSIZE_MAX);
    dropHint_->setVisible(!any);
    dropTitle_->setText(any ? tr("Trascina qui altre foto e schermate") : tr("Trascina qui foto e schermate"));
    const int levelIndex = std::clamp(quality_->checkedId(), 0, int(std::size(kLevels)) - 1);
    qualityHelp_->setText(tr(kLevels[levelIndex].help));
    size_->setEnabled(kLevels[levelIndex].quality != Quality::Exact);
    const bool folder = destination() == Destination::Folder;
    folderButton_->setVisible(folder);
    folderLabel_->setVisible(folder);
    folderButton_->setText(outputFolder_.isEmpty() ? tr("Scegli la cartella…") : tr("Cambia…"));
    folderLabel_->setText(outputFolder_.isEmpty() ? tr("nessuna cartella scelta") : QDir::toNativeSeparators(outputFolder_));
    progress_->setVisible(pending_ > 0);
    progress_->setMaximum(std::max(1, total_));
    progress_->setValue(total_ - pending_);
    if (pending_ == 0)
        total_ = 0;

    qint64 before = 0, after = 0, readyBytes = 0;
    int done = 0, failed = 0, ready = 0;
    for (int i = 0; i < list_->topLevelItemCount(); ++i) {
        const QTreeWidgetItem* it = list_->topLevelItem(i);
        switch (stateOf(it)) {
        case Done:
            before += it->data(ColName, kBeforeRole).toLongLong();
            after += it->data(ColName, kAfterRole).toLongLong();
            ++done;
            break;
        case Failed: ++failed; break;
        case Ready:
            ++ready;
            readyBytes += QFileInfo(it->data(ColName, kInputRole).toString()).size();
            break;
        default: break;
        }
    }
    QStringList parts;
    if (pending_ > 0)
        parts << tr("Elaborazione: %1 %2 in corso…").arg(pending_).arg(pending_ == 1 ? tr("immagine") : tr("immagini"));
    else if (done > 0)
        parts << tr("%1 %2: da %3 a %4, risparmiati %5 (%6)")
                     .arg(done)
                     .arg(done == 1 ? tr("immagine") : tr("immagini"))
                     .arg(formatBytes(before), formatBytes(after), formatBytes(before - after), percent(before, after));
    if (ready > 0)
        parts << tr("%1 %2 (%3): premi Avanti").arg(ready).arg(ready == 1 ? tr("pronta") : tr("pronte")).arg(formatBytes(readyBytes));
    if (failed > 0)
        parts << tr("%1 non elaborate").arg(failed);
    if (parts.isEmpty())
        parts << tr("Aggiungi le immagini da alleggerire.");
    summary_->setText(parts.join(QStringLiteral(" · ")));
    summary_->setProperty("waiting", done == 0 && pending_ == 0);
    summary_->style()->unpolish(summary_);
    summary_->style()->polish(summary_);

    goButton_->setEnabled(ready > 0);
    goButton_->setText(ready > 0 ? tr("Avanti: %1 %2  ›").arg(ready).arg(ready == 1 ? tr("immagine") : tr("immagini")) : tr("Avanti  ›"));
    openButton_->setEnabled(done > 0);
    clearButton_->setEnabled(any && pending_ == 0);
    const bool canRedo = (done + failed) > 0 && pending_ == 0 && destination() != Destination::Replace;
    redoButton_->setVisible((done + failed) > 0);
    redoButton_->setEnabled(canRedo);
    redoButton_->setToolTip(destination() == Destination::Replace
                                ? tr("Non disponibile quando si sostituiscono gli originali (si ricomprimerebbe due volte)")
                                : tr("Rielabora gli stessi originali con le scelte attuali"));
    saveSettings();
}

void MainWindow::compare(QTreeWidgetItem* item)
{
    if (stateOf(item) != Done)
        return;
    const QString output = item->data(ColName, kResultRole).toString();
    const QString input = item->data(ColName, kInputRole).toString();
    // Con "al posto degli originali" l'originale è nel Cestino: si confronta solo se c'è ancora.
    const QString originalPath = output.compare(input, Qt::CaseInsensitive) != 0 ? input : QString();
    QFile a(originalPath), b(output);
    if (originalPath.isEmpty() || !a.open(QIODevice::ReadOnly) || !b.open(QIODevice::ReadOnly)) {
        QMessageBox::information(this, windowTitle(), tr("L'originale non è più disponibile per il confronto (è nel Cestino)."));
        return;
    }
    const QImage before = decodeImage(a.readAll()), after = decodeImage(b.readAll());
    if (before.isNull() || after.isNull())
        return;
    CompareDialog dialog(item->text(ColName), before,
                         tr("Prima: %1 · %2 × %3").arg(formatBytes(item->data(ColName, kBeforeRole).toLongLong())).arg(before.width()).arg(before.height()),
                         after,
                         tr("Dopo: %1 · %2 × %3").arg(formatBytes(item->data(ColName, kAfterRole).toLongLong())).arg(after.width()).arg(after.height()),
                         this);
    dialog.exec();
}

void MainWindow::saveFirstComparison(const QString& path)
{
    for (int i = 0; i < list_->topLevelItemCount(); ++i) {
        QTreeWidgetItem* item = list_->topLevelItem(i);
        const QString output = item->data(ColName, kResultRole).toString(), input = item->data(ColName, kInputRole).toString();
        QFile a(input), b(output);
        if (stateOf(item) != Done || output == input || !a.open(QIODevice::ReadOnly) || !b.open(QIODevice::ReadOnly))
            continue;
        CompareDialog dialog(item->text(ColName), decodeImage(a.readAll()), tr("Prima"), decodeImage(b.readAll()), tr("Dopo"), this);
        dialog.resize(1000, 700);
        dialog.show();
        QApplication::processEvents();
        dialog.grab().save(path);
        return;
    }
}

void MainWindow::openResults()
{
    QString path;
    QTreeWidgetItem* current = list_->currentItem();
    if (current && stateOf(current) == Done)
        path = current->data(ColName, kResultRole).toString();
    for (int i = list_->topLevelItemCount() - 1; i >= 0 && path.isEmpty(); --i)
        if (stateOf(list_->topLevelItem(i)) == Done)
            path = list_->topLevelItem(i)->data(ColName, kResultRole).toString();
    if (path.isEmpty())
        return;
    // Cartella scelta: si apre quella (anche se il file è in una sottocartella).
    const QString dir = destination() == Destination::Folder && !outputFolder_.isEmpty() &&
                                QFileInfo(path).absoluteFilePath().startsWith(outputFolder_, Qt::CaseInsensitive)
                            ? outputFolder_
                            : QFileInfo(path).absolutePath();
    QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
}

void MainWindow::redo()
{
    QList<QTreeWidgetItem*> items;
    for (int i = 0; i < list_->topLevelItemCount(); ++i) {
        QTreeWidgetItem* item = list_->topLevelItem(i);
        if (stateOf(item) == Done || stateOf(item) == Failed || stateOf(item) == Ready)
            items << item;
    }
    if (destination() == Destination::Folder && outputFolder_.isEmpty() && !chooseOutputFolder())
        return;
    run(items);
}

void MainWindow::chooseFiles()
{
    const QStringList files = QFileDialog::getOpenFileNames(this, tr("Scegli le immagini"), lastFolder_,
                                                            tr("Immagini (*.jpg *.jpeg *.jpe *.png)"));
    if (!files.isEmpty())
        addPaths(files);
}

void MainWindow::chooseFolder()
{
    const QString dir = QFileDialog::getExistingDirectory(this, tr("Scegli una cartella di immagini"), lastFolder_);
    if (!dir.isEmpty())
        addPaths({dir});
}

void MainWindow::showAbout()
{
    QMessageBox box(this);
    box.setWindowTitle(tr("Informazioni"));
    box.setTextFormat(Qt::RichText);
    box.setText(tr("<h3>Alleggerisci %1</h3>"
                   "<p>Alleggerisce foto (JPEG) e schermate (PNG) in qualità massima, media o bassa, oppure lasciandole "
                   "identiche al pixel.</p>"
                   "<p><b>Privacy:</b> il programma non si collega a internet e non raccoglie dati. Le immagini restano sul "
                   "computer; le scelte sono salvate solo qui (%2).</p>"
                   "<p>Software libero secondo la GNU General Public License v3.0 o successiva, senza alcuna garanzia. "
                   "Sorgenti: <a href=\"https://github.com/SamuDiPi/TinyImage\">github.com/SamuDiPi/TinyImage</a>.</p>"
                   "<p>Usa libjpeg-turbo, libpng, zlib, libimagequant, Zopfli e Qt: licenze e sorgenti in "
                   "LICENZE-TERZE-PARTI.txt nella cartella del programma.</p>")
                    .arg(QApplication::applicationVersion(),
                         settings_.format() == QSettings::IniFormat ? tr("nel file portabile.ini accanto al programma")
                                                                    : tr("nelle impostazioni di Windows dell'utente")));
    box.setTextInteractionFlags(Qt::TextBrowserInteraction);
    box.exec();
}

void MainWindow::saveSettings()
{
    settings_.setValue(QStringLiteral("livello"), std::max(0, quality_->checkedId()));
    settings_.setValue(QStringLiteral("dimensioni"), size_->currentIndex());
    settings_.setValue(QStringLiteral("togliDati"), strip_->isChecked());
    settings_.setValue(QStringLiteral("destinazione"), destination_->currentIndex());
    settings_.setValue(QStringLiteral("cartellaScelta"), outputFolder_);
    settings_.remove(QStringLiteral("qualita"));   // scelta della 0.1.0 (due livelli), sostituita da "livello"
}

void MainWindow::highlightDrop(bool on)
{
    dropArea_->setProperty("active", on);
    dropArea_->style()->unpolish(dropArea_);
    dropArea_->style()->polish(dropArea_);
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event)
{
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
        highlightDrop(true);
    }
}

void MainWindow::dragLeaveEvent(QDragLeaveEvent*)
{
    highlightDrop(false);
}

void MainWindow::dropEvent(QDropEvent* event)
{
    highlightDrop(false);
    QStringList paths;
    for (const QUrl& url : event->mimeData()->urls())
        if (url.isLocalFile())
            paths << url.toLocalFile();
    if (!paths.isEmpty()) {
        event->acceptProposedAction();
        addPaths(paths);
    }
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    if (pending_ > 0 &&
        QMessageBox::question(this, windowTitle(), tr("Ci sono ancora immagini in elaborazione. Chiudere comunque? Quelle già iniziate vengono completate, "
                                                      "le altre non si elaborano; gli originali restano com'erano.")) != QMessageBox::Yes) {
        event->ignore();
        return;
    }
    pool_.clear();
    pool_.waitForDone();
    settings_.setValue(QStringLiteral("finestra"), saveGeometry());
    settings_.setValue(QStringLiteral("ultimaCartella"), lastFolder_);
    event->accept();
}

} // namespace alleggerisci
