#include "ui/RegionSelectionOverlay.h"

#include <QGuiApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QScreen>
#include <QShowEvent>

#include <algorithm>
#include <cmath>

namespace
{
QRect logicalGeometryForDisplay(const DisplayInfo& display)
{
    const double scale = display.dpiScale > 0.0 ? display.dpiScale : 1.0;
    return QRect(
        static_cast<int>(std::lround(display.geometry.x() / scale)),
        static_cast<int>(std::lround(display.geometry.y() / scale)),
        std::max(1, static_cast<int>(std::lround(display.geometry.width() / scale))),
        std::max(1, static_cast<int>(std::lround(display.geometry.height() / scale))));
}
}

RegionSelectionOverlay::RegionSelectionOverlay(const DisplayInfo& display, QWidget* parent)
    : QDialog(parent)
    , display_(display)
{
    setWindowFlag(Qt::FramelessWindowHint, true);
    setWindowFlag(Qt::WindowStaysOnTopHint, true);
    setWindowFlag(Qt::Tool, true);
    setWindowModality(Qt::ApplicationModal);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setAttribute(Qt::WA_DeleteOnClose, false);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setCursor(Qt::CrossCursor);
}

bool RegionSelectionOverlay::hasSelection() const
{
    return isValidCaptureRegion(selectedRegion_);
}

CaptureRegion RegionSelectionOverlay::selectedRegion() const
{
    return selectedRegion_;
}

void RegionSelectionOverlay::showEvent(QShowEvent* event)
{
    if (!positioned_) {
        if (auto* screen = resolveScreen()) {
            setGeometry(screen->geometry());
        } else {
            setGeometry(fallbackGeometry());
        }
        positioned_ = true;
    }

    QDialog::showEvent(event);
    raise();
    activateWindow();
    setFocus();
}

void RegionSelectionOverlay::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.fillRect(rect(), QColor(0, 0, 0, 120));

    const QRect selected = clampedSelectionRect();
    if (!selected.isEmpty()) {
        painter.setCompositionMode(QPainter::CompositionMode_Clear);
        painter.fillRect(selected, Qt::transparent);
        painter.setCompositionMode(QPainter::CompositionMode_SourceOver);

        painter.setRenderHint(QPainter::Antialiasing, false);
        painter.setPen(QPen(Qt::white, 2));
        painter.drawRect(selected.adjusted(0, 0, -1, -1));

        painter.setPen(Qt::white);
        const CaptureRegion region = selectionInNativePixels(selected);
        painter.drawText(
            selected.adjusted(8, 8, -8, -8),
            Qt::AlignLeft | Qt::AlignTop,
            QStringLiteral("%1x%2").arg(region.width).arg(region.height));
    }

    painter.setPen(Qt::white);
    const QString instructions = hasSelection()
        ? QStringLiteral("Press Enter to confirm the selected region, drag again to reselect, or press Esc to cancel.")
        : QStringLiteral("Drag to select a capture region on %1. Press Esc to cancel.")
              .arg(display_.name);
    painter.drawText(
        rect().adjusted(16, 16, -16, -16),
        Qt::AlignLeft | Qt::AlignTop,
        instructions);
}

void RegionSelectionOverlay::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        dragging_ = true;
        dragStart_ = event->position().toPoint();
        dragCurrent_ = dragStart_;
        selectedLogicalRect_ = {};
        update();
        event->accept();
        return;
    }

    if (event->button() == Qt::RightButton) {
        reject();
        event->accept();
        return;
    }

    QDialog::mousePressEvent(event);
}

void RegionSelectionOverlay::mouseMoveEvent(QMouseEvent* event)
{
    if (!dragging_) {
        QDialog::mouseMoveEvent(event);
        return;
    }

    dragCurrent_ = event->position().toPoint();
    update();
    event->accept();
}

void RegionSelectionOverlay::mouseReleaseEvent(QMouseEvent* event)
{
    if (dragging_ && event->button() == Qt::LeftButton) {
        dragCurrent_ = event->position().toPoint();
        selectedLogicalRect_ = clampedSelectionRect();
        dragging_ = false;
        selectedRegion_ = selectionInNativePixels(selectedLogicalRect_);
        if (!hasSelection()) {
            selectedLogicalRect_ = {};
            selectedRegion_ = {};
        }
        update();
        event->accept();
        return;
    }

    QDialog::mouseReleaseEvent(event);
}

void RegionSelectionOverlay::keyPressEvent(QKeyEvent* event)
{
    if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) && hasSelection()) {
        accept();
        return;
    }

    if (event->key() == Qt::Key_Escape) {
        reject();
        return;
    }

    QDialog::keyPressEvent(event);
}

QScreen* RegionSelectionOverlay::resolveScreen() const
{
    const auto screens = QGuiApplication::screens();
    for (QScreen* screen : screens) {
        if (screen != nullptr && screen->name() == display_.deviceName) {
            return screen;
        }
    }

    return nullptr;
}

QRect RegionSelectionOverlay::selectionRect() const
{
    if (dragging_) {
        return QRect(dragStart_, dragCurrent_).normalized();
    }

    return selectedLogicalRect_;
}

QRect RegionSelectionOverlay::clampedSelectionRect() const
{
    return selectionRect().intersected(rect());
}

QRect RegionSelectionOverlay::fallbackGeometry() const
{
    if (!display_.geometry.isEmpty()) {
        return logicalGeometryForDisplay(display_);
    }

    if (auto* screen = QGuiApplication::primaryScreen()) {
        return screen->geometry();
    }

    return QRect(0, 0, 1280, 720);
}

CaptureRegion RegionSelectionOverlay::selectionInNativePixels(const QRect& logicalRect) const
{
    const QRect selected = logicalRect.intersected(rect());
    if (selected.isEmpty() || width() <= 0 || height() <= 0 || display_.pixelSize.isEmpty()) {
        return {};
    }

    const double scaleX = static_cast<double>(display_.pixelSize.width()) / static_cast<double>(width());
    const double scaleY = static_cast<double>(display_.pixelSize.height()) / static_cast<double>(height());

    const int left = std::clamp(
        static_cast<int>(std::floor(selected.x() * scaleX)),
        0,
        display_.pixelSize.width());
    const int top = std::clamp(
        static_cast<int>(std::floor(selected.y() * scaleY)),
        0,
        display_.pixelSize.height());
    const int right = std::clamp(
        static_cast<int>(std::ceil((selected.x() + selected.width()) * scaleX)),
        0,
        display_.pixelSize.width());
    const int bottom = std::clamp(
        static_cast<int>(std::ceil((selected.y() + selected.height()) * scaleY)),
        0,
        display_.pixelSize.height());

    CaptureRegion region;
    region.x = left;
    region.y = top;
    region.width = std::max(0, right - left);
    region.height = std::max(0, bottom - top);
    return region;
}
