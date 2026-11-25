#include "CompareDialog.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace alleggerisci {

CompareView::CompareView(const QImage& before, const QImage& after, QWidget* parent)
    : QWidget(parent), before_(before), after_(after)
{
    setMouseTracking(true);
    setMinimumSize(480, 320);
    setCursor(Qt::OpenHandCursor);
    setToolTip(tr("Trascina la linea per confrontare. Rotellina: zoom. Doppio clic: 100% / adatta."));
}

double CompareView::fitScale() const
{
    if (before_.isNull())
        return 1.0;
    return std::min({1.0, width() / double(before_.width()), height() / double(before_.height())});
}

QRectF CompareView::imageRect() const
{
    const double s = fitScale() * zoom_;
    const QSizeF size(before_.width() * s, before_.height() * s);
    const QPointF center = QPointF(width() / 2.0, height() / 2.0) + pan_;
    return QRectF(center - QPointF(size.width() / 2.0, size.height() / 2.0), size);
}

void CompareView::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.fillRect(rect(), QColor(40, 42, 46));
    if (before_.isNull())
        return;
    // Scacchiera sotto le immagini con trasparenza.
    const QRectF r = imageRect();
    const QRectF visible = r.intersected(QRectF(rect()));
    for (int y = int(visible.top()); y < visible.bottom(); y += 12)
        for (int x = int(visible.left()); x < visible.right(); x += 12)
            p.fillRect(QRectF(x, y, 12, 12).intersected(visible), ((x / 12 + y / 12) % 2) ? QColor(200, 200, 200) : QColor(235, 235, 235));
    p.setRenderHint(QPainter::SmoothPixmapTransform, zoom_ * fitScale() < 1.0);
    const double splitX = std::clamp(width() * split_, 0.0, double(width()));
    p.save();
    p.setClipRect(QRectF(0, 0, splitX, height()));
    p.drawImage(r, before_);
    p.restore();
    p.save();
    p.setClipRect(QRectF(splitX, 0, width() - splitX, height()));
    p.drawImage(r, after_.size() == before_.size() ? after_ : after_.scaled(before_.size()));
    p.restore();
    // Linea e maniglia.
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(QColor(255, 255, 255), 2));
    p.drawLine(QPointF(splitX, 0), QPointF(splitX, height()));
    const QPointF c(splitX, height() / 2.0);
    p.setBrush(QColor(37, 99, 235));
    p.drawEllipse(c, 16, 16);
    p.setPen(QPen(Qt::white, 2));
    p.drawLine(c + QPointF(-7, 0), c + QPointF(-3, -4));
    p.drawLine(c + QPointF(-7, 0), c + QPointF(-3, 4));
    p.drawLine(c + QPointF(7, 0), c + QPointF(3, -4));
    p.drawLine(c + QPointF(7, 0), c + QPointF(3, 4));
    // Etichette.
    auto badge = [&](const QString& text, bool left) {
        QFont f = font();
        f.setBold(true);
        p.setFont(f);
        const QRectF box = p.boundingRect(QRectF(0, 0, 400, 30), Qt::AlignLeft, text).adjusted(-10, -5, 10, 5);
        const QPointF at = left ? QPointF(12, 12) : QPointF(width() - box.width() - 12, 12);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, 150));
        p.drawRoundedRect(QRectF(at, box.size()), 8, 8);
        p.setPen(Qt::white);
        p.drawText(QRectF(at, box.size()), Qt::AlignCenter, text);
    };
    badge(tr("Prima"), true);
    badge(tr("Dopo"), false);
    const double pct = fitScale() * zoom_ * 100.0;
    p.setPen(QColor(220, 220, 220));
    p.drawText(rect().adjusted(0, 0, -12, -10), Qt::AlignRight | Qt::AlignBottom, QStringLiteral("%1%").arg(qRound(pct)));
}

void CompareView::mousePressEvent(QMouseEvent* event)
{
    last_ = event->position();
    drag_ = std::abs(event->position().x() - width() * split_) < 24 ? Drag::Split : Drag::Pan;
    if (drag_ == Drag::Pan)
        setCursor(Qt::ClosedHandCursor);
}

void CompareView::mouseMoveEvent(QMouseEvent* event)
{
    if (drag_ == Drag::Split) {
        split_ = std::clamp(event->position().x() / std::max(1, width()), 0.0, 1.0);
        update();
    } else if (drag_ == Drag::Pan) {
        pan_ += event->position() - last_;
        last_ = event->position();
        update();
    } else {
        setCursor(std::abs(event->position().x() - width() * split_) < 24 ? Qt::SplitHCursor : Qt::OpenHandCursor);
    }
}

void CompareView::mouseReleaseEvent(QMouseEvent*)
{
    drag_ = Drag::None;
    setCursor(Qt::OpenHandCursor);
}

void CompareView::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (std::abs(fitScale() * zoom_ - 1.0) < 1e-3) {   // al 100%: torna ad adattare
        zoom_ = 1.0;
        pan_ = {};
        update();
    } else {
        zoomAt(event->position(), 1.0 / (fitScale() * zoom_));
    }
}

void CompareView::wheelEvent(QWheelEvent* event)
{
    zoomAt(event->position(), std::pow(1.0015, event->angleDelta().y()));
}

void CompareView::zoomAt(QPointF at, double factor)
{
    const double newZoom = std::clamp(zoom_ * factor, 0.2, 40.0 / std::max(1e-6, fitScale()));
    factor = newZoom / zoom_;
    const QPointF center = QPointF(width() / 2.0, height() / 2.0) + pan_;
    pan_ += (center - at) * (factor - 1.0);
    zoom_ = newZoom;
    update();
}

void CompareView::resizeEvent(QResizeEvent*)
{
    update();
}

CompareDialog::CompareDialog(const QString& title, const QImage& before, const QString& beforeText, const QImage& after,
                             const QString& afterText, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(title);
    auto* layout = new QVBoxLayout(this);
    auto* info = new QHBoxLayout;
    auto* left = new QLabel(beforeText);
    auto* right = new QLabel(afterText);
    right->setAlignment(Qt::AlignRight);
    info->addWidget(left);
    info->addStretch();
    info->addWidget(right);
    layout->addLayout(info);
    layout->addWidget(new CompareView(before, after, this), 1);
    auto* hint = new QLabel(tr("Trascina la linea per confrontare · rotellina per ingrandire · doppio clic per il 100%"));
    hint->setStyleSheet(QStringLiteral("color: #6b7280;"));
    layout->addWidget(hint);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    buttons->button(QDialogButtonBox::Close)->setText(tr("Chiudi"));
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
    resize(1000, 720);
}

} // namespace alleggerisci
