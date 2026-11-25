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
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLocale>
#include <QMessageBox>
#include <QMimeData>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

namespace alleggerisci {

namespace {

enum Column { ColName, ColBefore, ColAfter, ColSaved, ColNote, ColCount };
constexpr int kResultRole = Qt::UserRole + 1;   // FileResult::output
constexpr int kInputRole = Qt::UserRole + 2;
constexpr int kBeforeRole = Qt::UserRole + 3;
constexpr int kAfterRole = Qt::UserRole + 4;

const int kSizes[] = {0, 3840, 2560, 1920, 1280};

QString percent(qint64 before, qint64 after)
{
    if (before <= 0)
        return {};
    const double p = (1.0 - double(after) / double(before)) * 100.0;
    return QStringLiteral("−%1%").arg(QLocale(QLocale::Italian).toString(p, 'f', p < 10 ? 1 : 0));
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
    auto* subtitle = new QLabel(tr("Foto e schermate più leggere, senza perdere qualità visibile. Tutto resta sul tuo computer."));
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

    // Zona in cui trascinare.
    dropArea_ = new QFrame;
    dropArea_->setObjectName(QStringLiteral("drop"));
    auto* dropLayout = new QVBoxLayout(dropArea_);
    dropLayout->setAlignment(Qt::AlignCenter);
    dropTitle_ = new QLabel(tr("Trascina qui foto e schermate"));
    dropTitle_->setObjectName(QStringLiteral("dropTitle"));
    dropTitle_->setAlignment(Qt::AlignCenter);
    dropHint_ = new QLabel(tr("JPEG e PNG, anche cartelle intere. Gli originali non si toccano: le copie leggere vanno nella cartella \"alleggerite\"."));
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

    // Risultati.
    list_ = new QTreeWidget;
    list_->setColumnCount(ColCount);
    list_->setHeaderLabels({tr("Immagine"), tr("Prima"), tr("Dopo"), tr("Risparmio"), tr("Note")});
    list_->setRootIsDecorated(false);
    list_->setUniformRowHeights(true);
    list_->setAlternatingRowColors(true);
    list_->header()->setSectionResizeMode(ColName, QHeaderView::Stretch);
    list_->header()->setSectionResizeMode(ColNote, QHeaderView::Stretch);
    for (int c : {ColBefore, ColAfter, ColSaved})
        list_->header()->setSectionResizeMode(c, QHeaderView::ResizeToContents);
    list_->setToolTip(tr("Doppio clic su un'immagine per confrontare prima e dopo"));
    list_->hide();
    connect(list_, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* item) { compare(item); });
    layout->addWidget(list_, 3);

    progress_ = new QProgressBar;
    progress_->setTextVisible(false);
    progress_->setFixedHeight(6);
    progress_->hide();
    layout->addWidget(progress_);

    // Scelte.
    auto* options = new QFrame;
    options->setObjectName(QStringLiteral("options"));
    auto* grid = new QHBoxLayout(options);
    grid->setSpacing(18);
    auto* qualityBox = new QVBoxLayout;
    qualityBox->addWidget(new QLabel(tr("Qualità")));
    auto* qualityRow = new QHBoxLayout;
    qualityRow->setSpacing(0);
    quality_ = new QButtonGroup(this);
    auto* visual = new QPushButton(tr("Senza perdita visibile"));
    auto* exact = new QPushButton(tr("Identica al pixel"));
    visual->setToolTip(tr("Foto ricompresse ad alta qualità e schermate con colori ottimizzati: a occhio non cambia nulla, il peso scende molto."));
    exact->setToolTip(tr("Solo ottimizzazioni senza perdita: ogni pixel resta identico, il peso scende meno."));
    for (auto* b : {visual, exact}) {
        b->setCheckable(true);
        b->setObjectName(QStringLiteral("segment"));
        qualityRow->addWidget(b);
    }
    quality_->addButton(visual, 0);
    quality_->addButton(exact, 1);
    quality_->setExclusive(true);
    qualityBox->addLayout(qualityRow);
    grid->addLayout(qualityBox);

    auto* sizeBox = new QVBoxLayout;
    sizeBox->addWidget(new QLabel(tr("Dimensioni")));
    size_ = new QComboBox;
    size_->addItems({tr("Originali"), tr("Lato lungo max 3840 px (4K)"), tr("Lato lungo max 2560 px"),
                     tr("Lato lungo max 1920 px (Full HD)"), tr("Lato lungo max 1280 px")});
    size_->setToolTip(tr("Rimpicciolire le foto grandi alleggerisce ancora di più (solo con \"Senza perdita visibile\")."));
    sizeBox->addWidget(size_);
    grid->addLayout(sizeBox);

    auto* destBox = new QVBoxLayout;
    destBox->addWidget(new QLabel(tr("Dove salvare")));
    destination_ = new QComboBox;
    destination_->addItems({tr("Copie nella cartella \"alleggerite\""), tr("Al posto degli originali (originali nel Cestino)")});
    destBox->addWidget(destination_);
    grid->addLayout(destBox);
    grid->addStretch();
    layout->addWidget(options);

    strip_ = new QCheckBox(tr("Togli posizione GPS e dati della fotocamera (consigliato prima di condividere)"));
    strip_->setToolTip(tr("Toglie posizione, modello della fotocamera, data e commenti nascosti nel file. Orientamento e profilo colore restano."));
    layout->addWidget(strip_);

    // Riepilogo e azioni.
    auto* bottom = new QHBoxLayout;
    summary_ = new QLabel;
    summary_->setObjectName(QStringLiteral("summary"));
    bottom->addWidget(summary_, 1);
    redoButton_ = new QPushButton(tr("Rifai con queste scelte"));
    clearButton_ = new QPushButton(tr("Svuota elenco"));
    openButton_ = new QPushButton(tr("Apri la cartella"));
    openButton_->setObjectName(QStringLiteral("primary"));
    connect(redoButton_, &QPushButton::clicked, this, &MainWindow::redo);
    connect(clearButton_, &QPushButton::clicked, this, [this] {
        list_->clear();
        processed_.clear();
        updateState();
    });
    connect(openButton_, &QPushButton::clicked, this, &MainWindow::openResults);
    bottom->addWidget(redoButton_);
    bottom->addWidget(clearButton_);
    bottom->addWidget(openButton_);
    layout->addLayout(bottom);
    setCentralWidget(central);

    // Scelte salvate.
    quality_->button(settings_.value(QStringLiteral("qualita"), 0).toInt() == 1 ? 1 : 0)->setChecked(true);
    size_->setCurrentIndex(std::clamp(settings_.value(QStringLiteral("dimensioni"), 0).toInt(), 0, int(std::size(kSizes)) - 1));
    strip_->setChecked(settings_.value(QStringLiteral("togliDati"), true).toBool());
    destination_->setCurrentIndex(settings_.value(QStringLiteral("destinazione"), 0).toInt() == 1 ? 1 : 0);
    lastFolder_ = settings_.value(QStringLiteral("ultimaCartella")).toString();
    if (!QFileInfo(lastFolder_).isDir())
        lastFolder_ = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    connect(quality_, &QButtonGroup::idClicked, this, [this] { updateState(); });
    connect(size_, &QComboBox::currentIndexChanged, this, [this] { updateState(); });
    connect(strip_, &QCheckBox::toggled, this, [this] { updateState(); });
    connect(destination_, &QComboBox::activated, this, [this](int index) {
        if (index == 1)
            QMessageBox::information(this, windowTitle(),
                                     tr("Le immagini alleggerite prenderanno il posto degli originali. Gli originali vanno nel Cestino "
                                        "di Windows: da lì si possono recuperare finché non lo si svuota.\n\n"
                                        "Le immagini già leggere restano come sono."));
        updateState();
    });

    restoreGeometry(settings_.value(QStringLiteral("finestra")).toByteArray());
    if (!settings_.contains(QStringLiteral("finestra")))
        resize(980, 680);
    updateState();
}

Options MainWindow::options() const
{
    Options o;
    o.quality = quality_->checkedId() == 1 ? Quality::Exact : Quality::Visual;
    o.maxSide = o.quality == Quality::Visual ? kSizes[std::clamp(size_->currentIndex(), 0, int(std::size(kSizes)) - 1)] : 0;
    o.stripPrivate = strip_->isChecked();
    return o;
}

Destination MainWindow::destination() const
{
    return destination_->currentIndex() == 1 ? Destination::Replace : Destination::Subfolder;
}

void MainWindow::addPaths(const QStringList& paths)
{
    const QStringList files = collectImages(paths);
    if (files.isEmpty()) {
        QMessageBox::information(this, windowTitle(), tr("Nessuna foto JPEG o immagine PNG tra quelle indicate."));
        return;
    }
    if (!paths.isEmpty())
        lastFolder_ = QFileInfo(paths.first()).isDir() ? paths.first() : QFileInfo(paths.first()).absolutePath();
    start(files);
}

void MainWindow::start(const QStringList& files)
{
    const Options opt = options();
    const Destination dest = destination();
    for (const QString& f : files) {
        auto* item = new QTreeWidgetItem(list_);
        item->setText(ColName, QFileInfo(f).fileName());
        item->setToolTip(ColName, QDir::toNativeSeparators(f));
        item->setText(ColNote, tr("in attesa…"));
        item->setData(ColName, kInputRole, f);
        for (int c : {ColBefore, ColAfter, ColSaved})
            item->setTextAlignment(c, Qt::AlignRight | Qt::AlignVCenter);
        ++pending_;
        ++total_;
        if (!processed_.contains(f))
            processed_ << f;
        auto* watcher = new QFutureWatcher<FileResult>(this);
        connect(watcher, &QFutureWatcher<FileResult>::finished, this, [this, watcher, item] {
            finished(item, watcher->result());
            watcher->deleteLater();
        });
        watcher->setFuture(QtConcurrent::run(&pool_, [f, opt, dest] { return processFile(f, opt, dest); }));
    }
    list_->scrollToBottom();
    updateState();
}

void MainWindow::finished(QTreeWidgetItem* item, const FileResult& r)
{
    --pending_;
    item->setText(ColBefore, formatBytes(r.before));
    if (r.ok) {
        item->setText(ColAfter, formatBytes(r.after));
        item->setText(ColSaved, r.unchanged ? QStringLiteral("—") : percent(r.before, r.after));
        item->setText(ColNote, r.method);
        item->setData(ColName, kResultRole, r.output);
        item->setData(ColName, kBeforeRole, r.before);
        item->setData(ColName, kAfterRole, r.after);
        if (!r.unchanged)
            item->setForeground(ColSaved, QColor(22, 128, 61));
    } else {
        item->setText(ColNote, r.error);
        item->setForeground(ColNote, QColor(185, 28, 28));
    }
    updateState();
}

void MainWindow::updateState()
{
    const bool any = list_->topLevelItemCount() > 0;
    list_->setVisible(any);
    dropArea_->setMaximumHeight(any ? 150 : QWIDGETSIZE_MAX);
    dropHint_->setVisible(!any);
    dropTitle_->setText(any ? tr("Trascina qui altre foto e schermate") : tr("Trascina qui foto e schermate"));
    size_->setEnabled(quality_->checkedId() != 1);
    progress_->setVisible(pending_ > 0);
    progress_->setMaximum(std::max(1, total_));
    progress_->setValue(total_ - pending_);
    if (pending_ == 0)
        total_ = 0;

    qint64 before = 0, after = 0;
    int done = 0, failed = 0;
    for (int i = 0; i < list_->topLevelItemCount(); ++i) {
        const QTreeWidgetItem* it = list_->topLevelItem(i);
        if (it->data(ColName, kResultRole).isValid()) {
            before += it->data(ColName, kBeforeRole).toLongLong();
            after += it->data(ColName, kAfterRole).toLongLong();
            ++done;
        } else if (!it->text(ColBefore).isEmpty()) {
            ++failed;
        }
    }
    QString text;
    if (pending_ > 0)
        text = tr("Elaborazione: %1 immagini in corso…").arg(pending_);
    else if (done > 0)
        text = tr("%1 %2: da %3 a %4, risparmiati %5 (%6)")
                   .arg(done)
                   .arg(done == 1 ? tr("immagine") : tr("immagini"))
                   .arg(formatBytes(before), formatBytes(after), formatBytes(before - after), percent(before, after));
    if (failed > 0)
        text += (text.isEmpty() ? QString() : QStringLiteral(" · ")) + tr("%1 non elaborate").arg(failed);
    summary_->setText(text);
    openButton_->setEnabled(done > 0);
    clearButton_->setEnabled(any && pending_ == 0);
    redoButton_->setEnabled(any && pending_ == 0 && destination() == Destination::Subfolder);
    redoButton_->setToolTip(destination() == Destination::Replace
                                ? tr("Non disponibile quando si sostituiscono gli originali (si ricomprimerebbe due volte)")
                                : tr("Rielabora gli stessi originali con le scelte attuali"));
    saveSettings();
}

void MainWindow::compare(QTreeWidgetItem* item)
{
    const QString output = item->data(ColName, kResultRole).toString();
    const QString input = item->data(ColName, kInputRole).toString();
    if (output.isEmpty())
        return;
    // Con "al posto degli originali" l'originale è nel Cestino: si confronta solo se c'è ancora.
    const QString originalPath = output != input ? input : QString();
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
        if (output.isEmpty() || output == input || !a.open(QIODevice::ReadOnly) || !b.open(QIODevice::ReadOnly))
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
    QTreeWidgetItem* item = list_->currentItem();
    QString path;
    for (int i = list_->topLevelItemCount() - 1; i >= 0 && path.isEmpty(); --i)
        if (!list_->topLevelItem(i)->data(ColName, kResultRole).toString().isEmpty())
            path = list_->topLevelItem(i)->data(ColName, kResultRole).toString();
    if (item && !item->data(ColName, kResultRole).toString().isEmpty())
        path = item->data(ColName, kResultRole).toString();
    if (!path.isEmpty())
        QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath()));
}

void MainWindow::redo()
{
    const QStringList files = processed_;
    list_->clear();
    processed_.clear();
    start(files);
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
    const QString dir = QFileDialog::getExistingDirectory(this, tr("Scegli una cartella"), lastFolder_);
    if (!dir.isEmpty())
        addPaths({dir});
}

void MainWindow::showAbout()
{
    QMessageBox box(this);
    box.setWindowTitle(tr("Informazioni"));
    box.setTextFormat(Qt::RichText);
    box.setText(tr("<h3>Alleggerisci %1</h3>"
                   "<p>Alleggerisce foto (JPEG) e schermate (PNG) senza perdita di qualità visibile, oppure identiche al pixel.</p>"
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
    settings_.setValue(QStringLiteral("qualita"), quality_->checkedId() == 1 ? 1 : 0);
    settings_.setValue(QStringLiteral("dimensioni"), size_->currentIndex());
    settings_.setValue(QStringLiteral("togliDati"), strip_->isChecked());
    settings_.setValue(QStringLiteral("destinazione"), destination_->currentIndex());
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
