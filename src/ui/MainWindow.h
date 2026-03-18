#pragma once

#include "app/ApplicationContext.h"
#include "core/RecordingTypes.h"

#include <QMainWindow>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(ApplicationContext& context, QWidget* parent = nullptr);

private:
    void buildUi();
    void wireSignals();
    void refreshCaptureTargetList();
    void updateWindowSelector(const QList<WindowInfo>& windows);
    void updateDisplaySelector(const QList<DisplayInfo>& displays);
    void updateTargetSelectors();
    void updateControls();
    RecordingOptions collectOptions() const;
    void appendLog(const QString& message);

    ApplicationContext& context_;
    QList<WindowInfo> windows_;
    QList<DisplayInfo> displays_;

    QComboBox* captureTypeSelector_ {nullptr};
    QComboBox* windowSelector_ {nullptr};
    QComboBox* displaySelector_ {nullptr};
    QPushButton* refreshButton_ {nullptr};
    QLineEdit* outputPathEdit_ {nullptr};
    QPushButton* browseButton_ {nullptr};
    QCheckBox* systemAudioCheck_ {nullptr};
    QCheckBox* microphoneCheck_ {nullptr};
    QSpinBox* fpsSpin_ {nullptr};
    QComboBox* maxWidthCombo_ {nullptr};
    QSpinBox* bitrateSpin_ {nullptr};
    QPushButton* startButton_ {nullptr};
    QPushButton* stopButton_ {nullptr};
    QLabel* statusLabel_ {nullptr};
    QPlainTextEdit* logView_ {nullptr};
};
