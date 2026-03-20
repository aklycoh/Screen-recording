#include "ui/MainWindow.h"

#include "common/OperationResult.h"
#include "ui/RegionSelectionOverlay.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QStandardPaths>
#include <QSystemTrayIcon>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>

#include <windows.h>

namespace
{

QString defaultOutputPath()
{
    QString dir = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    if (dir.isEmpty())
        dir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    if (dir.isEmpty())
        dir = QDir::homePath();

    const QString timestamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss"));
    return QDir::toNativeSeparators(
        QDir(dir).filePath(QStringLiteral("recording_%1.mp4").arg(timestamp)));
}

QString formatWindowLabel(const WindowInfo& info)
{
    return QStringLiteral("%1  [%2x%3]  (PID %4)")
        .arg(info.title)
        .arg(info.windowSize.width())
        .arg(info.windowSize.height())
        .arg(info.processId);
}

QString formatDisplayLabel(const DisplayInfo& info)
{
    return QStringLiteral("%1  [%2x%3]  (%4, DPI x%5)")
        .arg(info.name)
        .arg(info.pixelSize.width())
        .arg(info.pixelSize.height())
        .arg(info.deviceName)
        .arg(QString::number(info.dpiScale, 'f', 2));
}

QString regionSummaryText(const CaptureRegion& region)
{
    if (!isValidCaptureRegion(region))
        return QStringLiteral("No region selected.");

    return QStringLiteral("Selected area: %1").arg(describeCaptureRegion(region));
}

QIcon createIcon(const QColor& fillColor)
{
    QPixmap pix(32, 32);
    pix.fill(Qt::transparent);
    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing);
    p.setBrush(fillColor);
    p.setPen(Qt::NoPen);
    p.drawEllipse(2, 2, 28, 28);
    return QIcon(pix);
}

QString formatElapsed(qint64 ms)
{
    const int total = static_cast<int>(ms / 1000);
    const int h = total / 3600;
    const int m = (total % 3600) / 60;
    const int s = total % 60;
    return QStringLiteral("%1:%2:%3")
        .arg(h, 2, 10, QChar('0'))
        .arg(m, 2, 10, QChar('0'))
        .arg(s, 2, 10, QChar('0'));
}

constexpr auto kStartButtonStyle = R"(
    QPushButton {
        background-color: #4CAF50; color: white; border: none;
        padding: 8px 24px; font-size: 13px; font-weight: bold; border-radius: 4px;
    }
    QPushButton:hover { background-color: #43A047; }
    QPushButton:pressed { background-color: #388E3C; }
    QPushButton:disabled { background-color: #A5D6A7; color: #E8E8E8; }
)";

constexpr auto kStopButtonStyle = R"(
    QPushButton {
        background-color: #F44336; color: white; border: none;
        padding: 8px 24px; font-size: 13px; font-weight: bold; border-radius: 4px;
    }
    QPushButton:hover { background-color: #E53935; }
    QPushButton:pressed { background-color: #C62828; }
    QPushButton:disabled { background-color: #EF9A9A; color: #E8E8E8; }
)";

constexpr auto kLinkButtonStyle = R"(
    QPushButton {
        background: none; border: none; color: #1976D2;
        text-decoration: underline; padding: 2px 8px;
    }
    QPushButton:hover { color: #1565C0; }
)";

}

MainWindow::MainWindow(ApplicationContext& context, QWidget* parent)
    : QMainWindow(parent)
    , context_(context)
{
    buildUi();
    buildTrayIcon();
    wireSignals();
    registerGlobalHotkey();
    refreshCaptureTargetList();
    updateControls();
}

MainWindow::~MainWindow()
{
    unregisterGlobalHotkey();
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    if (context_.recordingController().isRecording() && trayIcon_ && trayIcon_->isVisible()) {
        hide();
        event->ignore();
        trayIcon_->showMessage(
            QStringLiteral("MP4 Recorder"),
            QStringLiteral("Recording continues in the background.\nCtrl+Shift+R to stop."),
            QSystemTrayIcon::Information, 3000);
        return;
    }

    if (trayIcon_)
        trayIcon_->hide();
    unregisterGlobalHotkey();
    QMainWindow::closeEvent(event);
}

bool MainWindow::nativeEvent(const QByteArray& eventType, void* message, qintptr* result)
{
    if (eventType == "windows_generic_MSG") {
        const auto* msg = static_cast<const MSG*>(message);
        if (msg->message == WM_HOTKEY && static_cast<int>(msg->wParam) == kHotkeyId) {
            toggleRecording();
            return true;
        }
    }

    return QMainWindow::nativeEvent(eventType, message, result);
}

void MainWindow::buildUi()
{
    setWindowTitle(QStringLiteral("MP4 Recorder"));
    setWindowIcon(createIcon(QColor(220, 50, 50)));
    resize(720, 520);

    auto* central = new QWidget(this);
    auto* root = new QVBoxLayout(central);

    // --- Capture Target ---
    auto* captureGroup = new QGroupBox(QStringLiteral("Capture Target"), central);
    auto* captureLayout = new QFormLayout(captureGroup);

    captureTypeSelector_ = new QComboBox(captureGroup);
    captureTypeSelector_->addItem(QStringLiteral("Window"), static_cast<int>(CaptureTargetType::Window));
    captureTypeSelector_->addItem(QStringLiteral("Display (Full Screen)"), static_cast<int>(CaptureTargetType::Display));
    captureTypeSelector_->addItem(QStringLiteral("Display Region"), static_cast<int>(CaptureTargetType::Region));

    windowSelector_ = new QComboBox(captureGroup);
    displaySelector_ = new QComboBox(captureGroup);

    regionControlsWidget_ = new QWidget(captureGroup);
    auto* regionLayout = new QHBoxLayout(regionControlsWidget_);
    regionLayout->setContentsMargins(0, 0, 0, 0);
    selectRegionButton_ = new QPushButton(QStringLiteral("Select Region"), regionControlsWidget_);
    selectRegionButton_->setStyleSheet(QString::fromLatin1(kStartButtonStyle));
    regionSummaryLabel_ = new QLabel(regionControlsWidget_);
    regionSummaryLabel_->setWordWrap(true);
    regionLayout->addWidget(selectRegionButton_);
    regionLayout->addWidget(regionSummaryLabel_, 1);

    refreshButton_ = new QPushButton(QStringLiteral("Refresh"), captureGroup);

    captureLayout->addRow(QStringLiteral("Type"), captureTypeSelector_);
    captureLayout->addRow(QStringLiteral("Window"), windowSelector_);
    captureLayout->addRow(QStringLiteral("Display"), displaySelector_);
    captureLayout->addRow(QStringLiteral("Region"), regionControlsWidget_);
    captureLayout->addRow(QStringLiteral("Targets"), refreshButton_);

    // --- Output ---
    auto* outputGroup = new QGroupBox(QStringLiteral("Output"), central);
    auto* outputLayout = new QHBoxLayout(outputGroup);
    outputPathEdit_ = new QLineEdit(outputGroup);
    outputPathEdit_->setPlaceholderText(QStringLiteral("Choose an MP4 output path"));
    outputPathEdit_->setText(defaultOutputPath());
    browseButton_ = new QPushButton(QStringLiteral("Browse"), outputGroup);
    outputLayout->addWidget(outputPathEdit_, 1);
    outputLayout->addWidget(browseButton_);

    // --- Recording Options ---
    auto* optionsGroup = new QGroupBox(QStringLiteral("Recording Options"), central);
    auto* optionsLayout = new QFormLayout(optionsGroup);

    systemAudioCheck_ = new QCheckBox(QStringLiteral("Capture system audio"), optionsGroup);
    systemAudioCheck_->setChecked(true);
    microphoneCheck_ = new QCheckBox(QStringLiteral("Capture microphone"), optionsGroup);
    microphoneCheck_->setEnabled(false);
    microphoneCheck_->setToolTip(QStringLiteral("Microphone capture will be connected in a later implementation step."));

    fpsSpin_ = new QSpinBox(optionsGroup);
    fpsSpin_->setRange(15, 60);
    fpsSpin_->setValue(30);

    maxWidthCombo_ = new QComboBox(optionsGroup);
    maxWidthCombo_->addItem(QStringLiteral("720"), 720);
    maxWidthCombo_->addItem(QStringLiteral("960"), 960);
    maxWidthCombo_->addItem(QStringLiteral("1080"), 1080);
    maxWidthCombo_->addItem(QStringLiteral("1280"), 1280);
    maxWidthCombo_->addItem(QStringLiteral("1920"), 1920);
    maxWidthCombo_->addItem(QStringLiteral("Original"), 0);
    maxWidthCombo_->setCurrentIndex(maxWidthCombo_->findData(1280));

    bitrateSpin_ = new QSpinBox(optionsGroup);
    bitrateSpin_->setRange(2000, 12000);
    bitrateSpin_->setSingleStep(500);
    bitrateSpin_->setValue(4500);

    optionsLayout->addRow(systemAudioCheck_);
    optionsLayout->addRow(microphoneCheck_);
    optionsLayout->addRow(QStringLiteral("Target FPS"), fpsSpin_);
    optionsLayout->addRow(QStringLiteral("Max Output Width"), maxWidthCombo_);
    optionsLayout->addRow(QStringLiteral("Video Bitrate (kbps)"), bitrateSpin_);

    // --- Action Bar ---
    auto* actionLayout = new QHBoxLayout();

    startButton_ = new QPushButton(QStringLiteral("Start Recording"), central);
    startButton_->setStyleSheet(QString::fromLatin1(kStartButtonStyle));

    stopButton_ = new QPushButton(QStringLiteral("Stop"), central);
    stopButton_->setStyleSheet(QString::fromLatin1(kStopButtonStyle));

    recordingDot_ = new QLabel(central);
    recordingDot_->setFixedSize(14, 14);
    recordingDot_->setStyleSheet(
        QStringLiteral("background-color: #F44336; border-radius: 7px; margin-top: 2px;"));
    recordingDot_->setVisible(false);

    timerLabel_ = new QLabel(QStringLiteral("00:00:00"), central);
    timerLabel_->setFont(QFont(QStringLiteral("Consolas"), 12, QFont::Bold));
    timerLabel_->setVisible(false);

    openFileButton_ = new QPushButton(QStringLiteral("Open File"), central);
    openFileButton_->setStyleSheet(QString::fromLatin1(kLinkButtonStyle));
    openFileButton_->setCursor(Qt::PointingHandCursor);
    openFileButton_->setVisible(false);

    openFolderButton_ = new QPushButton(QStringLiteral("Open Folder"), central);
    openFolderButton_->setStyleSheet(QString::fromLatin1(kLinkButtonStyle));
    openFolderButton_->setCursor(Qt::PointingHandCursor);
    openFolderButton_->setVisible(false);

    statusLabel_ = new QLabel(QStringLiteral("Idle"), central);

    actionLayout->addWidget(startButton_);
    actionLayout->addWidget(stopButton_);
    actionLayout->addSpacing(8);
    actionLayout->addWidget(recordingDot_);
    actionLayout->addWidget(timerLabel_);
    actionLayout->addSpacing(8);
    actionLayout->addWidget(openFileButton_);
    actionLayout->addWidget(openFolderButton_);
    actionLayout->addStretch(1);
    actionLayout->addWidget(new QLabel(QStringLiteral("State:"), central));
    actionLayout->addWidget(statusLabel_);

    // --- Session Log ---
    logGroup_ = new QGroupBox(QStringLiteral("Session Log"), central);
    auto* logLayout = new QVBoxLayout(logGroup_);
    logView_ = new QPlainTextEdit(logGroup_);
    logView_->setReadOnly(true);
    logView_->setMaximumBlockCount(500);
    logLayout->addWidget(logView_);

    // --- Root Layout ---
    root->addWidget(captureGroup);
    root->addWidget(outputGroup);
    root->addWidget(optionsGroup);
    root->addLayout(actionLayout);
    root->addWidget(logGroup_, 1);

    setCentralWidget(central);

    // --- Timers ---
    recordingTimer_ = new QTimer(this);
    recordingTimer_->setInterval(500);

    blinkTimer_ = new QTimer(this);
    blinkTimer_->setInterval(600);

    updateRegionSummary();
    appendLog(QStringLiteral("Ready. Global hotkey: Ctrl+Shift+R to start/stop recording."));
}

void MainWindow::buildTrayIcon()
{
    if (!QSystemTrayIcon::isSystemTrayAvailable())
        return;

    trayIcon_ = new QSystemTrayIcon(createIcon(QColor(128, 128, 128)), this);

    auto* menu = new QMenu(this);
    trayToggleAction_ = menu->addAction(
        QStringLiteral("Start Recording"), this, &MainWindow::toggleRecording);
    menu->addAction(QStringLiteral("Show Window"), this, [this]() {
        show();
        raise();
        activateWindow();
    });
    menu->addSeparator();
    menu->addAction(QStringLiteral("Quit"), this, [this]() {
        if (context_.recordingController().isRecording())
            context_.recordingController().stopRecording();
        if (trayIcon_)
            trayIcon_->hide();
        QApplication::quit();
    });

    trayIcon_->setContextMenu(menu);
    trayIcon_->setToolTip(QStringLiteral("MP4 Recorder \u2014 Idle"));
    trayIcon_->show();

    connect(trayIcon_, &QSystemTrayIcon::activated, this,
        [this](QSystemTrayIcon::ActivationReason reason) {
            if (reason == QSystemTrayIcon::DoubleClick) {
                show();
                raise();
                activateWindow();
            }
        });
}

void MainWindow::wireSignals()
{
    auto& controller = context_.recordingController();

    connect(refreshButton_, &QPushButton::clicked, this, &MainWindow::refreshCaptureTargetList);
    connect(captureTypeSelector_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) {
        updateTargetSelectors();
        updateControls();
    });
    connect(windowSelector_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) {
        updateControls();
    });
    connect(displaySelector_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) {
        clearSelectedRegion();
        updateControls();
    });
    connect(outputPathEdit_, &QLineEdit::textChanged, this, [this]() {
        updateControls();
    });
    connect(selectRegionButton_, &QPushButton::clicked, this, &MainWindow::selectRegion);

    connect(browseButton_, &QPushButton::clicked, this, [this]() {
        const QString initial = outputPathEdit_->text().trimmed().isEmpty()
            ? defaultOutputPath()
            : outputPathEdit_->text().trimmed();
        const QString path = QFileDialog::getSaveFileName(
            this, QStringLiteral("Choose Output MP4"), initial,
            QStringLiteral("MP4 Files (*.mp4)"));
        if (!path.isEmpty())
            outputPathEdit_->setText(path);
    });

    connect(startButton_, &QPushButton::clicked, this, [this]() {
        const RecordingOptions options = collectOptions();
        const OperationResult result = context_.recordingController().startRecording(options);
        if (!result.ok) {
            QMessageBox::warning(this, QStringLiteral("Unable to Start"), result.message);
            appendLog(result.message);
            return;
        }
        appendLog(result.message);
        updateControls();
    });

    connect(stopButton_, &QPushButton::clicked, this, [this]() {
        const OperationResult result = context_.recordingController().stopRecording();
        if (!result.ok) {
            QMessageBox::information(this, QStringLiteral("Stop"), result.message);
            appendLog(result.message);
            return;
        }
        appendLog(result.message);
        updateControls();
    });

    connect(openFileButton_, &QPushButton::clicked, this, [this]() {
        const QString path = openFileButton_->property("outputPath").toString();
        if (!path.isEmpty())
            QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    });

    connect(openFolderButton_, &QPushButton::clicked, this, [this]() {
        const QString path = openFolderButton_->property("outputPath").toString();
        if (!path.isEmpty())
            QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath()));
    });

    connect(&controller, &RecordingController::windowsChanged, this, &MainWindow::updateWindowSelector);
    connect(&controller, &RecordingController::displaysChanged, this, &MainWindow::updateDisplaySelector);
    connect(&controller, &RecordingController::stateChanged, this, &MainWindow::onStateChanged);
    connect(&controller, &RecordingController::logMessage, this, &MainWindow::appendLog);

    connect(recordingTimer_, &QTimer::timeout, this, &MainWindow::updateRecordingTimer);
    connect(blinkTimer_, &QTimer::timeout, this, [this]() {
        dotVisible_ = !dotVisible_;
        recordingDot_->setVisible(dotVisible_);
    });
}

void MainWindow::registerGlobalHotkey()
{
    RegisterHotKey(
        reinterpret_cast<HWND>(winId()), kHotkeyId,
        MOD_CONTROL | MOD_SHIFT | MOD_NOREPEAT, 'R');
}

void MainWindow::unregisterGlobalHotkey()
{
    UnregisterHotKey(reinterpret_cast<HWND>(winId()), kHotkeyId);
}

void MainWindow::refreshCaptureTargetList()
{
    const OperationResult result = context_.recordingController().refreshCaptureTargets();
    appendLog(result.message);
    if (!result.ok)
        QMessageBox::information(this, QStringLiteral("Capture Targets"), result.message);
}

void MainWindow::updateWindowSelector(const QList<WindowInfo>& windows)
{
    windows_ = windows;
    windowSelector_->clear();
    for (const auto& w : windows_)
        windowSelector_->addItem(formatWindowLabel(w));
    updateTargetSelectors();
    updateControls();
}

void MainWindow::updateDisplaySelector(const QList<DisplayInfo>& displays)
{
    displays_ = displays;
    displaySelector_->clear();
    for (const auto& d : displays_)
        displaySelector_->addItem(formatDisplayLabel(d));
    clearSelectedRegion();
    updateTargetSelectors();
    updateControls();
}

void MainWindow::updateTargetSelectors()
{
    const auto type = static_cast<CaptureTargetType>(captureTypeSelector_->currentData().toInt());
    const bool useWindow = type == CaptureTargetType::Window;
    const bool useRegion = type == CaptureTargetType::Region;
    windowSelector_->setVisible(useWindow);
    displaySelector_->setVisible(!useWindow);
    regionControlsWidget_->setVisible(useRegion);
}

void MainWindow::updateControls()
{
    const bool recording = context_.recordingController().isRecording();
    const auto type = static_cast<CaptureTargetType>(captureTypeSelector_->currentData().toInt());
    const bool hasWindow = !windows_.isEmpty() && windowSelector_->currentIndex() >= 0;
    const bool hasDisplay = !displays_.isEmpty() && displaySelector_->currentIndex() >= 0;
    const bool hasRegion = isValidCaptureRegion(selectedRegion_);
    const bool hasTarget = type == CaptureTargetType::Window
        ? hasWindow
        : (type == CaptureTargetType::Region ? hasDisplay && hasRegion : hasDisplay);
    const bool hasOutput = !outputPathEdit_->text().trimmed().isEmpty();

    startButton_->setEnabled(!recording && hasTarget && hasOutput);
    stopButton_->setEnabled(recording);
    refreshButton_->setEnabled(!recording);
    captureTypeSelector_->setEnabled(!recording);
    windowSelector_->setEnabled(!recording && type == CaptureTargetType::Window);
    displaySelector_->setEnabled(!recording && type != CaptureTargetType::Window);
    selectRegionButton_->setEnabled(!recording && type == CaptureTargetType::Region && hasDisplay);
    browseButton_->setEnabled(!recording);
    outputPathEdit_->setEnabled(!recording);
}

void MainWindow::updateRegionSummary()
{
    regionSummaryLabel_->setText(regionSummaryText(selectedRegion_));
}

void MainWindow::clearSelectedRegion()
{
    selectedRegion_ = {};
    updateRegionSummary();
}

void MainWindow::selectRegion()
{
    const int idx = displaySelector_->currentIndex();
    if (idx < 0 || idx >= displays_.size()) {
        QMessageBox::information(this, QStringLiteral("Select Region"),
            QStringLiteral("Choose a display first."));
        return;
    }

    RegionSelectionOverlay overlay(displays_.at(idx), this);
    if (overlay.exec() != QDialog::Accepted || !overlay.hasSelection()) {
        appendLog(QStringLiteral("Region selection was cancelled."));
        return;
    }

    selectedRegion_ = overlay.selectedRegion();
    updateRegionSummary();
    appendLog(QStringLiteral("Selected recording region on %1: %2")
        .arg(displays_.at(idx).name, describeCaptureRegion(selectedRegion_)));
    updateControls();
}

void MainWindow::toggleRecording()
{
    if (context_.recordingController().isRecording()) {
        stopButton_->click();
    } else if (startButton_->isEnabled()) {
        startButton_->click();
    }
}

void MainWindow::updateRecordingTimer()
{
    if (recordingStartMs_ <= 0)
        return;

    const qint64 elapsed = QDateTime::currentMSecsSinceEpoch() - recordingStartMs_;
    timerLabel_->setText(formatElapsed(elapsed));

    if (trayIcon_)
        trayIcon_->setToolTip(
            QStringLiteral("MP4 Recorder \u2014 Recording  %1").arg(formatElapsed(elapsed)));
}

void MainWindow::onStateChanged(RecordingState state, const QString& detail)
{
    const RecordingState prev = lastState_;
    lastState_ = state;

    statusLabel_->setText(describeRecordingState(state));
    appendLog(detail);

    if (state == RecordingState::Recording) {
        recordingStartMs_ = QDateTime::currentMSecsSinceEpoch();
        recordingTimer_->start();
        blinkTimer_->start();
        timerLabel_->setText(QStringLiteral("00:00:00"));
        timerLabel_->setVisible(true);
        recordingDot_->setVisible(true);
        dotVisible_ = true;
        openFileButton_->setVisible(false);
        openFolderButton_->setVisible(false);

        if (trayIcon_) {
            trayIcon_->setIcon(createIcon(Qt::red));
            trayIcon_->setToolTip(QStringLiteral("MP4 Recorder \u2014 Recording..."));
        }
        if (trayToggleAction_)
            trayToggleAction_->setText(QStringLiteral("Stop Recording"));
    } else {
        recordingTimer_->stop();
        blinkTimer_->stop();
        recordingDot_->setVisible(false);

        const bool justFinished = (state == RecordingState::Idle)
            && (prev == RecordingState::Recording || prev == RecordingState::Finalizing);

        if (justFinished) {
            const QString path = outputPathEdit_->text().trimmed();
            if (!path.isEmpty() && QFileInfo::exists(path)) {
                openFileButton_->setProperty("outputPath", path);
                openFolderButton_->setProperty("outputPath", path);
                openFileButton_->setVisible(true);
                openFolderButton_->setVisible(true);
            }
            outputPathEdit_->setText(defaultOutputPath());
        }

        timerLabel_->setVisible(state == RecordingState::Finalizing);

        if (trayIcon_) {
            trayIcon_->setIcon(createIcon(QColor(128, 128, 128)));
            trayIcon_->setToolTip(
                QStringLiteral("MP4 Recorder \u2014 %1").arg(describeRecordingState(state)));
        }
        if (trayToggleAction_)
            trayToggleAction_->setText(QStringLiteral("Start Recording"));
    }

    updateControls();
}

RecordingOptions MainWindow::collectOptions() const
{
    RecordingOptions options;

    const auto targetType = static_cast<CaptureTargetType>(captureTypeSelector_->currentData().toInt());
    options.target.type = targetType;

    if (targetType == CaptureTargetType::Display || targetType == CaptureTargetType::Region) {
        const int index = displaySelector_->currentIndex();
        if (index >= 0 && index < displays_.size())
            options.target.display = displays_.at(index);
        if (targetType == CaptureTargetType::Region)
            options.target.region = selectedRegion_;
    } else {
        const int index = windowSelector_->currentIndex();
        if (index >= 0 && index < windows_.size())
            options.target.window = windows_.at(index);
    }

    options.audio.captureSystemAudio = systemAudioCheck_->isChecked();
    options.audio.captureMicrophone = microphoneCheck_->isChecked();
    options.video.targetFps = fpsSpin_->value();
    options.video.maxOutputWidth = maxWidthCombo_->currentData().toInt();
    options.video.videoBitrateKbps = bitrateSpin_->value();
    options.output.outputFilePath = outputPathEdit_->text().trimmed();
    return options;
}

void MainWindow::appendLog(const QString& message)
{
    if (message.trimmed().isEmpty())
        return;

    const QString ts = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"));
    logView_->appendPlainText(QStringLiteral("[%1] %2").arg(ts, message));
}
