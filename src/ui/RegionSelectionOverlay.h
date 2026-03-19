#pragma once

#include "core/RecordingTypes.h"

#include <QDialog>

class QKeyEvent;
class QMouseEvent;
class QPaintEvent;
class QScreen;
class QShowEvent;

class RegionSelectionOverlay : public QDialog
{
public:
    explicit RegionSelectionOverlay(const DisplayInfo& display, QWidget* parent = nullptr);

    bool hasSelection() const;
    CaptureRegion selectedRegion() const;

protected:
    void showEvent(QShowEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    QScreen* resolveScreen() const;
    QRect selectionRect() const;
    QRect clampedSelectionRect() const;
    QRect fallbackGeometry() const;
    CaptureRegion selectionInNativePixels(const QRect& logicalRect) const;

    DisplayInfo display_;
    QPoint dragStart_;
    QPoint dragCurrent_;
    QRect selectedLogicalRect_;
    bool dragging_ {false};
    bool positioned_ {false};
    CaptureRegion selectedRegion_;
};
