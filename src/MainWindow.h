#pragma once

// Finestra di Alleggerisci: si trascinano dentro foto, schermate o cartelle e partono subito; per ogni immagine il peso
// prima e dopo; in basso le scelte (qualità, dimensioni, dati personali, dove salvare) e il totale risparmiato.

#include "Files.h"

#include <QMainWindow>
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
    void addPaths(const QStringList& paths);
    bool busy() const { return pending_ > 0; }
    // Prove senza schermo: salva l'immagine del confronto prima/dopo della prima immagine elaborata.
    void saveFirstComparison(const QString& path);

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragLeaveEvent(QDragLeaveEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void closeEvent(QCloseEvent* event) override;

private:
    Options options() const;
    Destination destination() const;
    void start(const QStringList& files);
    void finished(QTreeWidgetItem* item, const FileResult& result);
    void updateState();
    void compare(QTreeWidgetItem* item);
    void openResults();
    void redo();
    void chooseFiles();
    void chooseFolder();
    void showAbout();
    void saveSettings();
    void highlightDrop(bool on);

    QSettings& settings_;
    QThreadPool pool_;
    QFrame* dropArea_ = nullptr;
    QLabel* dropTitle_ = nullptr;
    QLabel* dropHint_ = nullptr;
    QTreeWidget* list_ = nullptr;
    QButtonGroup* quality_ = nullptr;
    QComboBox* size_ = nullptr;
    QCheckBox* strip_ = nullptr;
    QComboBox* destination_ = nullptr;
    QLabel* summary_ = nullptr;
    QProgressBar* progress_ = nullptr;
    QPushButton* openButton_ = nullptr;
    QPushButton* redoButton_ = nullptr;
    QPushButton* clearButton_ = nullptr;
    int pending_ = 0;
    int total_ = 0;
    QStringList processed_;   // file elaborati (per "Rifai con queste scelte")
    QString lastFolder_;
};

} // namespace alleggerisci
