#pragma once

// Confronto prima/dopo: le due immagini sovrapposte con una linea divisoria da trascinare (a sinistra l'originale, a
// destra quella alleggerita), zoom con la rotellina, spostamento trascinando, doppio clic per il 100%.

#include <QDialog>
#include <QImage>
#include <QWidget>

namespace alleggerisci {

class CompareView : public QWidget {
    Q_OBJECT

public:
    CompareView(const QImage& before, const QImage& after, QWidget* parent = nullptr);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    double fitScale() const;
    QRectF imageRect() const;
    void zoomAt(QPointF at, double factor);

    QImage before_, after_;
    double zoom_ = 1.0;        // rispetto all'adattamento alla finestra
    QPointF pan_;              // spostamento dal centro, in pixel dello schermo
    double split_ = 0.5;       // posizione della linea (0 = tutta "dopo", 1 = tutta "prima")
    enum class Drag { None, Split, Pan } drag_ = Drag::None;
    QPointF last_;
};

class CompareDialog : public QDialog {
    Q_OBJECT

public:
    CompareDialog(const QString& title, const QImage& before, const QString& beforeText, const QImage& after,
                  const QString& afterText, QWidget* parent = nullptr);
};

} // namespace alleggerisci
