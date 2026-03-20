#pragma once

#include "app/ApplicationContext.h"
#include "core/RecordingTypes.h"

#include <QMainWindow>

class QAction;
class QCheckBox;
class QComboBox;
class QGroupBox;
class QLabel;
class QLineEdit;
class QMenu;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QSystemTrayIcon;
class QTimer;
class QWidget;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(ApplicationContext& context, QWidget* parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent* event) override;
    bool nativeEvent(const QByteArray& eventType, void* message, qintptr* result) override;

private:
    void buildUi();
    void buildTrayIcon();
    void wireSignals();
    void registerGlobalHotkey();
    void unregisterGlobalHotkey();
    void refreshCaptureTargetList();
    void updateWindowSelector(const QList<WindowInfo>& windows);
    void updateDisplaySelector(const QList<DisplayInfo>& displays);
    void updateTargetSelectors();
    void updateControls();
    void updateRegionSummary();
    void clearSelectedRegion();
    void selectRegion();
    void toggleRecording();
    void updateRecordingTimer();
    void onStateChanged(RecordingState state, const QString& detail);
    RecordingOptions collectOptions() const;
    void appendLog(const QString& message);

    ApplicationContext& context_;
    QList<WindowInfo> windows_;
    QList<DisplayInfo> displays_;

    QComboBox* captureTypeSelector_ {nullptr};
    QComboBox* windowSelector_ {nullptr};
    QComboBox* displaySelector_ {nullptr};
    QWidget* regionControlsWidget_ {nullptr};
    QPushButton* selectRegionButton_ {nullptr};
    QLabel* regionSummaryLabel_ {nullptr};
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
    QLabel* timerLabel_ {nullptr};
    QLabel* recordingDot_ {nullptr};
    QPushButton* openFileButton_ {nullptr};
    QPushButton* openFolderButton_ {nullptr};
    QGroupBox* logGroup_ {nullptr};
    QPlainTextEdit* logView_ {nullptr};

    QSystemTrayIcon* trayIcon_ {nullptr};
    QAction* trayToggleAction_ {nullptr};

    QTimer* recordingTimer_ {nullptr};
    QTimer* blinkTimer_ {nullptr};
    qint64 recordingStartMs_ {0};
    bool dotVisible_ {true};
    RecordingState lastState_ {RecordingState::Idle};

    CaptureRegion selectedRegion_;

    static constexpr int kHotkeyId = 1;
};
