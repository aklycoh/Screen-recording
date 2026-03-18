#include "ui/MainWindow.h"

#include "common/OperationResult.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QWidget>

namespace
{
QString defaultOutputPath()
{
    return QDir::toNativeSeparators(QStringLiteral("D:/下载/recording.mp4"));
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

QString describeState(RecordingState state)
{
    switch (state) {
    case RecordingState::Idle:
        return QStringLiteral("Idle");
    case RecordingState::Preparing:
        return QStringLiteral("Preparing");
    case RecordingState::Recording:
        return QStringLiteral("Recording");
    case RecordingState::Finalizing:
        return QStringLiteral("Finalizing");
    case RecordingState::Failed:
        return QStringLiteral("Failed");
    }

    return QStringLiteral("Unknown");
}
}

MainWindow::MainWindow(ApplicationContext& context, QWidget* parent)
    : QMainWindow(parent)
    , context_(context)
{
    buildUi();
    wireSignals();
    refreshCaptureTargetList();
    updateControls();
}

void MainWindow::buildUi()
{
    setWindowTitle(QStringLiteral("MP4 Recorder"));
    resize(860, 640);

    auto* central = new QWidget(this);
    auto* rootLayout = new QVBoxLayout(central);

    auto* captureGroup = new QGroupBox(QStringLiteral("Capture Target"), central);
    auto* captureLayout = new QFormLayout(captureGroup);
    captureTypeSelector_ = new QComboBox(captureGroup);
    captureTypeSelector_->addItem(QStringLiteral("Window"), static_cast<int>(CaptureTargetType::Window));
    captureTypeSelector_->addItem(QStringLiteral("Display (Full Screen)"), static_cast<int>(CaptureTargetType::Display));
    windowSelector_ = new QComboBox(captureGroup);
    displaySelector_ = new QComboBox(captureGroup);
    refreshButton_ = new QPushButton(QStringLiteral("Refresh"), captureGroup);
    captureLayout->addRow(QStringLiteral("Type"), captureTypeSelector_);
    captureLayout->addRow(QStringLiteral("Window"), windowSelector_);
    captureLayout->addRow(QStringLiteral("Display"), displaySelector_);
    captureLayout->addRow(QStringLiteral("Targets"), refreshButton_);

    auto* outputGroup = new QGroupBox(QStringLiteral("Output"), central);
    auto* outputLayout = new QHBoxLayout(outputGroup);
    outputPathEdit_ = new QLineEdit(outputGroup);
    outputPathEdit_->setPlaceholderText(QStringLiteral("Choose an MP4 output path"));
    outputPathEdit_->setText(defaultOutputPath());
    browseButton_ = new QPushButton(QStringLiteral("Browse"), outputGroup);
    outputLayout->addWidget(outputPathEdit_, 1);
    outputLayout->addWidget(browseButton_);

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

    auto* actionLayout = new QHBoxLayout();
    startButton_ = new QPushButton(QStringLiteral("Start Recording"), central);
    stopButton_ = new QPushButton(QStringLiteral("Stop"), central);
    statusLabel_ = new QLabel(QStringLiteral("Idle"), central);
    actionLayout->addWidget(startButton_);
    actionLayout->addWidget(stopButton_);
    actionLayout->addStretch(1);
    actionLayout->addWidget(new QLabel(QStringLiteral("State:"), central));
    actionLayout->addWidget(statusLabel_);

    auto* logGroup = new QGroupBox(QStringLiteral("Session Log"), central);
    auto* logLayout = new QVBoxLayout(logGroup);
    logView_ = new QPlainTextEdit(logGroup);
    logView_->setReadOnly(true);
    logLayout->addWidget(logView_);

    rootLayout->addWidget(captureGroup);
    rootLayout->addWidget(outputGroup);
    rootLayout->addWidget(optionsGroup);
    rootLayout->addLayout(actionLayout);
    rootLayout->addWidget(logGroup, 1);

    setCentralWidget(central);
    appendLog(QStringLiteral("This build records a selected window or full display to MP4. Microphone capture is not wired yet."));
}

void MainWindow::wireSignals()
{
    auto& controller = context_.recordingController();

    connect(refreshButton_, &QPushButton::clicked, this, &MainWindow::refreshCaptureTargetList);
    connect(captureTypeSelector_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) {
        updateTargetSelectors();
        updateControls();
    });
    connect(outputPathEdit_, &QLineEdit::textChanged, this, [this]() {
        updateControls();
    });

    connect(browseButton_, &QPushButton::clicked, this, [this]() {
        const QString initialPath = outputPathEdit_->text().trimmed().isEmpty()
            ? defaultOutputPath()
            : outputPathEdit_->text().trimmed();
        const QString path = QFileDialog::getSaveFileName(
            this,
            QStringLiteral("Choose Output MP4"),
            initialPath,
            QStringLiteral("MP4 Files (*.mp4)"));

        if (!path.isEmpty()) {
            outputPathEdit_->setText(path);
        }
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

    connect(&controller, &RecordingController::windowsChanged, this, &MainWindow::updateWindowSelector);
    connect(&controller, &RecordingController::displaysChanged, this, &MainWindow::updateDisplaySelector);
    connect(&controller, &RecordingController::stateChanged, this, [this](RecordingState state, const QString& detail) {
        statusLabel_->setText(describeState(state));
        appendLog(detail);
        updateControls();
    });
    connect(&controller, &RecordingController::logMessage, this, &MainWindow::appendLog);
}

void MainWindow::refreshCaptureTargetList()
{
    const OperationResult result = context_.recordingController().refreshCaptureTargets();
    appendLog(result.message);
    if (!result.ok) {
        QMessageBox::information(this, QStringLiteral("Capture Targets"), result.message);
    }
}

void MainWindow::updateWindowSelector(const QList<WindowInfo>& windows)
{
    windows_ = windows;
    windowSelector_->clear();

    for (const auto& window : windows_) {
        windowSelector_->addItem(formatWindowLabel(window));
    }

    updateTargetSelectors();
    updateControls();
}

void MainWindow::updateDisplaySelector(const QList<DisplayInfo>& displays)
{
    displays_ = displays;
    displaySelector_->clear();

    for (const auto& display : displays_) {
        displaySelector_->addItem(formatDisplayLabel(display));
    }

    updateTargetSelectors();
    updateControls();
}

void MainWindow::updateTargetSelectors()
{
    const auto targetType = static_cast<CaptureTargetType>(captureTypeSelector_->currentData().toInt());
    const bool useWindow = targetType == CaptureTargetType::Window;
    windowSelector_->setVisible(useWindow);
    displaySelector_->setVisible(!useWindow);
}

void MainWindow::updateControls()
{
    const bool recording = context_.recordingController().isRecording();
    const auto targetType = static_cast<CaptureTargetType>(captureTypeSelector_->currentData().toInt());
    const bool hasWindowSelection = !windows_.isEmpty() && windowSelector_->currentIndex() >= 0;
    const bool hasDisplaySelection = !displays_.isEmpty() && displaySelector_->currentIndex() >= 0;
    const bool hasTarget = targetType == CaptureTargetType::Display ? hasDisplaySelection : hasWindowSelection;
    const bool hasOutputPath = !outputPathEdit_->text().trimmed().isEmpty();

    startButton_->setEnabled(!recording && hasTarget && hasOutputPath);
    stopButton_->setEnabled(recording);
    refreshButton_->setEnabled(!recording);
    captureTypeSelector_->setEnabled(!recording);
    windowSelector_->setEnabled(!recording && targetType == CaptureTargetType::Window);
    displaySelector_->setEnabled(!recording && targetType == CaptureTargetType::Display);
    browseButton_->setEnabled(!recording);
    outputPathEdit_->setEnabled(!recording);
}

RecordingOptions MainWindow::collectOptions() const
{
    RecordingOptions options;

    const auto targetType = static_cast<CaptureTargetType>(captureTypeSelector_->currentData().toInt());
    options.target.type = targetType;

    if (targetType == CaptureTargetType::Display) {
        const int index = displaySelector_->currentIndex();
        if (index >= 0 && index < displays_.size()) {
            options.target.display = displays_.at(index);
        }
    } else {
        const int index = windowSelector_->currentIndex();
        if (index >= 0 && index < windows_.size()) {
            options.target.window = windows_.at(index);
        }
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
    if (message.trimmed().isEmpty()) {
        return;
    }
    logView_->appendPlainText(message);
}
