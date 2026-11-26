#pragma once

// Finestra di Alleggerisci, in tre passi: 1) si trascinano dentro (o si scelgono) foto, schermate o cartelle, che entrano
// nell'elenco come "pronte"; 2) si scelgono qualità, dimensioni, dati personali e dove salvare; 3) "Avanti" le elabora.
// Per ogni immagine il peso prima e dopo; in basso il totale risparmiato.

#include "Files.h"

#include <QMainWindow>
#include <QSet>
#include <QThreadPool>

class QButtonGroup;
class QCheckBox;
class QComboBox;
class QFrame;
class QLabel;
class QProgressBar;
class QPushButton;
class QSettings;
class QTreeWidget;
class QTreeWidgetItem;

namespace alleggerisci {

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QSettings& settings, QWidget* parent = nullptr);
    // Aggiunge all'elenco le immagini indicate (file o cartelle), pronte per "Avanti".
    void addPaths(const QStringList& paths);
    // "Avanti": elabora le immagini pronte con le scelte attuali.
    void go();
    bool busy() const { return pending_ > 0; }
    int readyCount() const;
    // Prove senza schermo: salva l'immagine del confronto prima/dopo della prima immagine elaborata.
    void saveFirstComparison(const QString& path);

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragLeaveEvent(QDragLeaveEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void closeEvent(QCloseEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    Options options() const;
    Destination destination() const;
    bool chooseOutputFolder();
    void run(const QList<QTreeWidgetItem*>& items);
    void finished(QTreeWidgetItem* item, const FileResult& result);
    void removeSelected();
    void updateState();
    void compare(QTreeWidgetItem* item);
    void openResults();
    void redo();
    void chooseFiles();
    void chooseFolder();
    void showAbout();
    void showListMenu(const QPoint& pos);
    void saveSettings();
    void highlightDrop(bool on);

    QSettings& settings_;
    QThreadPool pool_;
    QFrame* dropArea_ = nullptr;
    QLabel* dropTitle_ = nullptr;
    QLabel* dropHint_ = nullptr;
    QTreeWidget* list_ = nullptr;
    QButtonGroup* quality_ = nullptr;
    QLabel* qualityHelp_ = nullptr;
    QComboBox* size_ = nullptr;
    QCheckBox* strip_ = nullptr;
    QComboBox* destination_ = nullptr;
    QLabel* folderLabel_ = nullptr;
    QPushButton* folderButton_ = nullptr;
    QLabel* summary_ = nullptr;
    QProgressBar* progress_ = nullptr;
    QPushButton* goButton_ = nullptr;
    QPushButton* openButton_ = nullptr;
    QPushButton* redoButton_ = nullptr;
    QPushButton* clearButton_ = nullptr;
    int pending_ = 0;
    int total_ = 0;
    QString lastFolder_;
    QString outputFolder_;     // cartella scelta per i risultati
    QSet<QString> written_;    // file scritti in questa sessione nella cartella scelta (si possono sovrascrivere)
};

} // namespace alleggerisci
