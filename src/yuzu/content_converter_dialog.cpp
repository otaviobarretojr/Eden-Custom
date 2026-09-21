// SPDX-FileCopyrightText: Copyright 2026 Eden Custom Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "yuzu/content_converter_dialog.h"

#include <algorithm>

#include <QApplication>
#include <QCheckBox>
#include <QCoreApplication>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QCryptographicHash>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QListWidget>
#include <QMessageBox>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QProcess>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUrl>
#include <QVBoxLayout>

#include "common/fs/path_util.h"

namespace {
constexpr auto kManagedConverterUrl =
    "https://github.com/nicoboss/nsz/releases/download/5.0.0/nsz-cli-windows-x64.exe";
constexpr auto kManagedConverterSha256 =
    "341b395c18679bf4c01f0bf2fb0e22e315a64b34fad51064781f5155a978883d";
} // namespace

ContentConverterDialog::ContentConverterDialog(QWidget* parent)
    : QDialog(parent), process(new QProcess(this)), network_manager(new QNetworkAccessManager(this)) {
    BuildUi();
    setAcceptDrops(true);

    connect(process, &QProcess::readyReadStandardOutput, this, &ContentConverterDialog::ReadProcessOutput);
    connect(process, &QProcess::readyReadStandardError, this, &ContentConverterDialog::ReadProcessOutput);
    connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            &ContentConverterDialog::ProcessFinished);

    QString program;
    QStringList prefix_arguments;
    if (ResolveConverter(program, prefix_arguments)) {
        converter_path->setText(program);
        status_label->setText(tr("NSZ converter detected."));
    } else {
        status_label->setText(tr("NSZ converter not found. You can install the verified official tool automatically."));
    }
}

ContentConverterDialog::~ContentConverterDialog() = default;

void ContentConverterDialog::BuildUi() {
    setWindowTitle(tr("Content Converter — NSZ to NSP"));
    resize(780, 620);

    auto* root = new QVBoxLayout(this);

    auto* intro = new QLabel(
        tr("Convert compressed NSZ files to standard NSP files before running or installing them. "
           "The original NSZ file is always preserved."),
        this);
    intro->setWordWrap(true);
    root->addWidget(intro);

    auto* files_group = new QGroupBox(tr("Files"), this);
    auto* files_layout = new QVBoxLayout(files_group);
    file_list = new QListWidget(files_group);
    file_list->setSelectionMode(QAbstractItemView::ExtendedSelection);
    file_list->setAlternatingRowColors(true);
    files_layout->addWidget(file_list);

    auto* files_buttons = new QHBoxLayout();
    add_button = new QPushButton(tr("Add NSZ files"), files_group);
    remove_button = new QPushButton(tr("Remove selected"), files_group);
    clear_button = new QPushButton(tr("Clear"), files_group);
    files_buttons->addWidget(add_button);
    files_buttons->addWidget(remove_button);
    files_buttons->addWidget(clear_button);
    files_buttons->addStretch();
    files_layout->addLayout(files_buttons);
    root->addWidget(files_group, 1);

    auto* paths_group = new QGroupBox(tr("Conversion settings"), this);
    auto* paths_layout = new QGridLayout(paths_group);

    paths_layout->addWidget(new QLabel(tr("Output folder:"), paths_group), 0, 0);
    output_directory = new QLineEdit(paths_group);
    output_button = new QPushButton(tr("Browse..."), paths_group);
    paths_layout->addWidget(output_directory, 0, 1);
    paths_layout->addWidget(output_button, 0, 2);

    paths_layout->addWidget(new QLabel(tr("NSZ converter:"), paths_group), 1, 0);
    converter_path = new QLineEdit(paths_group);
    converter_button = new QPushButton(tr("Browse..."), paths_group);
    download_button = new QPushButton(tr("Install official"), paths_group);
    paths_layout->addWidget(converter_path, 1, 1);
    paths_layout->addWidget(converter_button, 1, 2);
    paths_layout->addWidget(download_button, 1, 3);

    verify_checkbox = new QCheckBox(tr("Verify integrity during conversion"), paths_group);
    verify_checkbox->setChecked(true);
    paths_layout->addWidget(verify_checkbox, 2, 1, 1, 2);
    root->addWidget(paths_group);

    auto* progress_group = new QGroupBox(tr("Progress"), this);
    auto* progress_layout = new QVBoxLayout(progress_group);
    current_file_label = new QLabel(tr("Waiting"), progress_group);
    progress_bar = new QProgressBar(progress_group);
    progress_bar->setRange(0, 100);
    progress_bar->setValue(0);
    status_label = new QLabel(progress_group);
    status_label->setWordWrap(true);
    progress_layout->addWidget(current_file_label);
    progress_layout->addWidget(progress_bar);
    progress_layout->addWidget(status_label);
    root->addWidget(progress_group);

    log_view = new QPlainTextEdit(this);
    log_view->setReadOnly(true);
    log_view->setMaximumBlockCount(2000);
    log_view->setPlaceholderText(tr("Conversion log"));
    root->addWidget(log_view, 1);

    auto* bottom = new QHBoxLayout();
    bottom->addStretch();
    start_button = new QPushButton(tr("Convert"), this);
    cancel_button = new QPushButton(tr("Cancel"), this);
    cancel_button->setEnabled(false);
    bottom->addWidget(start_button);
    bottom->addWidget(cancel_button);
    root->addLayout(bottom);

    connect(add_button, &QPushButton::clicked, this, &ContentConverterDialog::AddFiles);
    connect(remove_button, &QPushButton::clicked, this, &ContentConverterDialog::RemoveSelectedFiles);
    connect(clear_button, &QPushButton::clicked, this, &ContentConverterDialog::ClearFiles);
    connect(output_button, &QPushButton::clicked, this, &ContentConverterDialog::ChooseOutputDirectory);
    connect(converter_button, &QPushButton::clicked, this,
            &ContentConverterDialog::ChooseConverterExecutable);
    connect(download_button, &QPushButton::clicked, this,
            &ContentConverterDialog::DownloadConverter);
    connect(start_button, &QPushButton::clicked, this, &ContentConverterDialog::StartConversion);
    connect(cancel_button, &QPushButton::clicked, this, &ContentConverterDialog::CancelConversion);
}

void ContentConverterDialog::dragEnterEvent(QDragEnterEvent* event) {
    if (!event->mimeData()->hasUrls()) {
        return;
    }

    const auto urls = event->mimeData()->urls();
    const bool has_nsz = std::any_of(urls.cbegin(), urls.cend(), [](const QUrl& url) {
        return url.isLocalFile() && url.toLocalFile().endsWith(QStringLiteral(".nsz"), Qt::CaseInsensitive);
    });

    if (has_nsz) {
        event->acceptProposedAction();
    }
}

void ContentConverterDialog::dropEvent(QDropEvent* event) {
    for (const QUrl& url : event->mimeData()->urls()) {
        if (url.isLocalFile()) {
            AddFile(url.toLocalFile());
        }
    }
    event->acceptProposedAction();
}

void ContentConverterDialog::AddFiles() {
    const QStringList files = QFileDialog::getOpenFileNames(
        this, tr("Select NSZ files"), QString{}, tr("Nintendo Submission Zip (*.nsz)"));
    for (const QString& file : files) {
        AddFile(file);
    }
}

void ContentConverterDialog::AddFile(const QString& path) {
    const QFileInfo info(path);
    if (!info.exists() || !info.isFile() ||
        !info.fileName().endsWith(QStringLiteral(".nsz"), Qt::CaseInsensitive)) {
        return;
    }

    for (int i = 0; i < file_list->count(); ++i) {
        if (QFileInfo(file_list->item(i)->text()) == info) {
            return;
        }
    }

    file_list->addItem(info.absoluteFilePath());

    if (output_directory->text().isEmpty()) {
        output_directory->setText(info.absolutePath() + QDir::separator() + QStringLiteral("Converted"));
    }
}

void ContentConverterDialog::RemoveSelectedFiles() {
    const auto selected = file_list->selectedItems();
    for (QListWidgetItem* item : selected) {
        delete file_list->takeItem(file_list->row(item));
    }
}

void ContentConverterDialog::ClearFiles() {
    file_list->clear();
}

void ContentConverterDialog::ChooseOutputDirectory() {
    const QString dir = QFileDialog::getExistingDirectory(
        this, tr("Select output folder"), output_directory->text());
    if (!dir.isEmpty()) {
        output_directory->setText(QDir::toNativeSeparators(dir));
    }
}

void ContentConverterDialog::ChooseConverterExecutable() {
    const QString executable = QFileDialog::getOpenFileName(
        this, tr("Select NSZ converter"), converter_path->text(),
        tr("NSZ CLI (nsz-cli-windows-x64.exe nsz.exe);;Executable files (*.exe);;All files (*.*)"));
    if (!executable.isEmpty()) {
        converter_path->setText(QDir::toNativeSeparators(executable));
        status_label->setText(tr("NSZ converter selected."));
    }
}

QString ContentConverterDialog::EdenKeysDirectory() const {
    const auto keys = Common::FS::GetEdenPath(Common::FS::EdenPath::KeysDir);
    return QString::fromStdString(keys.string());
}

QString ContentConverterDialog::ManagedConverterPath() const {
    const auto eden_dir = Common::FS::GetEdenPath(Common::FS::EdenPath::EdenDir);
    const auto converter =
        eden_dir / "tools" / "nsz" / "nsz-cli-windows-x64.exe";
    return QString::fromStdString(converter.string());
}

void ContentConverterDialog::DownloadConverter() {
    if (download_reply != nullptr) {
        return;
    }

    const QString target = ManagedConverterPath();
    const QFileInfo target_info(target);
    if (!QDir().mkpath(target_info.absolutePath())) {
        QMessageBox::critical(this, tr("NSZ converter"),
                              tr("The converter folder could not be created."));
        return;
    }

    QNetworkRequest request{QUrl{QString::fromLatin1(kManagedConverterUrl)}};
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);

    progress_bar->setValue(0);
    current_file_label->setText(tr("Downloading NSZ converter"));
    status_label->setText(tr("Downloading the pinned official NSZ 5.0.0 Windows x64 tool..."));
    AppendLog(tr("Downloading official NSZ 5.0.0 converter."));
    download_button->setEnabled(false);
    converter_button->setEnabled(false);
    start_button->setEnabled(false);

    download_reply = network_manager->get(request);

    connect(download_reply, &QNetworkReply::downloadProgress, this,
            [this](qint64 received, qint64 total) {
                if (total > 0) {
                    const int percent =
                        qBound(0, static_cast<int>((received * 100) / total), 100);
                    progress_bar->setValue(percent);
                    status_label->setText(
                        tr("Downloading converter... %1 / %2 MB")
                            .arg(QString::number(received / 1024.0 / 1024.0, 'f', 1))
                            .arg(QString::number(total / 1024.0 / 1024.0, 'f', 1)));
                }
            });

    connect(download_reply, &QNetworkReply::finished, this,
            &ContentConverterDialog::FinishConverterDownload);
}

void ContentConverterDialog::FinishConverterDownload() {
    QNetworkReply* reply = download_reply;
    download_reply = nullptr;

    download_button->setEnabled(true);
    converter_button->setEnabled(true);
    start_button->setEnabled(true);

    if (reply == nullptr) {
        return;
    }

    const auto cleanup = qScopeGuard([reply] { reply->deleteLater(); });

    if (reply->error() != QNetworkReply::NoError) {
        status_label->setText(tr("Converter download failed."));
        AppendLog(tr("Download error: %1").arg(reply->errorString()));
        QMessageBox::critical(this, tr("NSZ converter"),
                              tr("The official converter could not be downloaded.\n\n%1")
                                  .arg(reply->errorString()));
        return;
    }

    const QByteArray payload = reply->readAll();
    const QByteArray digest =
        QCryptographicHash::hash(payload, QCryptographicHash::Sha256).toHex();

    if (digest != QByteArray{kManagedConverterSha256}) {
        status_label->setText(tr("Downloaded converter failed SHA-256 validation."));
        AppendLog(tr("Security validation failed. Expected %1, received %2.")
                      .arg(QString::fromLatin1(kManagedConverterSha256),
                           QString::fromLatin1(digest)));
        QMessageBox::critical(
            this, tr("NSZ converter"),
            tr("The downloaded converter did not match the pinned SHA-256 checksum and was not "
               "saved."));
        return;
    }

    const QString target = ManagedConverterPath();
    QSaveFile output{target};
    if (!output.open(QIODevice::WriteOnly) || output.write(payload) != payload.size() ||
        !output.commit()) {
        status_label->setText(tr("Converter could not be saved."));
        QMessageBox::critical(this, tr("NSZ converter"),
                              tr("The verified converter could not be saved to:\n%1").arg(target));
        return;
    }

    converter_path->setText(QDir::toNativeSeparators(target));
    progress_bar->setValue(100);
    current_file_label->setText(tr("NSZ converter ready"));
    status_label->setText(tr("Official NSZ 5.0.0 converter installed and SHA-256 verified."));
    AppendLog(tr("Installed verified NSZ converter: %1").arg(target));
}

bool ContentConverterDialog::ResolveConverter(QString& program, QStringList& prefix_arguments) const {
    prefix_arguments.clear();

    const QString configured = converter_path ? converter_path->text().trimmed() : QString{};
    if (!configured.isEmpty() && QFileInfo::exists(configured)) {
        program = configured;
        return true;
    }

    const QString app_dir = QCoreApplication::applicationDirPath();
    const QStringList candidates{
        ManagedConverterPath(),
        app_dir + QStringLiteral("/tools/nsz/nsz-cli-windows-x64.exe"),
        app_dir + QStringLiteral("/nsz-cli-windows-x64.exe"),
        app_dir + QStringLiteral("/nsz.exe"),
    };

    for (const QString& candidate : candidates) {
        if (QFileInfo::exists(candidate)) {
            program = candidate;
            return true;
        }
    }

    for (const QString& executable :
         {QStringLiteral("nsz-cli-windows-x64.exe"), QStringLiteral("nsz.exe"), QStringLiteral("nsz")}) {
        const QString found = QStandardPaths::findExecutable(executable);
        if (!found.isEmpty()) {
            program = found;
            return true;
        }
    }

    return false;
}

void ContentConverterDialog::StartConversion() {
    if (file_list->count() == 0) {
        QMessageBox::information(this, tr("Content Converter"), tr("Add at least one NSZ file."));
        return;
    }

    const QString output = output_directory->text().trimmed();
    if (output.isEmpty()) {
        QMessageBox::information(this, tr("Content Converter"), tr("Select an output folder."));
        return;
    }

    if (!QDir().mkpath(output)) {
        QMessageBox::critical(this, tr("Content Converter"),
                              tr("The output folder could not be created."));
        return;
    }

    QString program;
    QStringList prefix_arguments;
    if (!ResolveConverter(program, prefix_arguments)) {
        QMessageBox::warning(
            this, tr("NSZ converter not found"),
            tr("The NSZ converter is not installed yet. Use 'Install official' to download the "
               "pinned and checksum-verified NSZ 5.0.0 tool. Eden Custom does not include console "
               "keys or game content."));
        return;
    }
    converter_path->setText(QDir::toNativeSeparators(program));

    queue.clear();
    for (int i = 0; i < file_list->count(); ++i) {
        queue.append(file_list->item(i)->text());
    }

    queue_index = 0;
    cancel_requested = false;
    log_view->clear();
    SetBusy(true);
    StartNextFile();
}

void ContentConverterDialog::StartNextFile() {
    if (cancel_requested) {
        SetBusy(false);
        status_label->setText(tr("Conversion cancelled."));
        return;
    }

    if (queue_index >= queue.size()) {
        progress_bar->setValue(100);
        current_file_label->setText(tr("Completed"));
        status_label->setText(tr("All files were converted successfully."));
        AppendLog(tr("All conversions completed."));
        SetBusy(false);
        return;
    }

    QString program;
    QStringList prefix_arguments;
    if (!ResolveConverter(program, prefix_arguments)) {
        SetBusy(false);
        QMessageBox::critical(this, tr("NSZ converter"),
                              tr("The configured NSZ converter is no longer available."));
        return;
    }

    const QString input = queue.at(queue_index);
    current_file_label->setText(
        tr("%1 of %2 — %3").arg(queue_index + 1).arg(queue.size()).arg(QFileInfo(input).fileName()));
    status_label->setText(tr("Converting..."));
    progress_bar->setValue(0);

    QStringList arguments = prefix_arguments;
    arguments << QStringLiteral("--minimal-output");
    if (verify_checkbox->isChecked()) {
        arguments << QStringLiteral("--verify");
    }

    const QString keys_dir = EdenKeysDirectory();
    if (QFileInfo::exists(QDir(keys_dir).filePath(QStringLiteral("prod.keys")))) {
        arguments << QStringLiteral("--keys") << keys_dir;
    }

    arguments << QStringLiteral("--output") << output_directory->text().trimmed();
    arguments << QStringLiteral("-D") << input;

    AppendLog(tr("Starting: %1").arg(QFileInfo(input).fileName()));
    process->setProcessChannelMode(QProcess::SeparateChannels);
    process->start(program, arguments);

    if (!process->waitForStarted(5000)) {
        AppendLog(tr("Failed to start converter: %1").arg(process->errorString()));
        status_label->setText(tr("Converter could not be started."));
        SetBusy(false);
    }
}

void ContentConverterDialog::CancelConversion() {
    cancel_requested = true;
    status_label->setText(tr("Cancelling..."));
    if (process->state() != QProcess::NotRunning) {
        process->terminate();
        if (!process->waitForFinished(2500)) {
            process->kill();
        }
    } else {
        SetBusy(false);
    }
}

void ContentConverterDialog::ReadProcessOutput() {
    const QString standard_output = QString::fromUtf8(process->readAllStandardOutput());
    const QString standard_error = QString::fromUtf8(process->readAllStandardError());

    if (!standard_output.isEmpty()) {
        AppendLog(standard_output.trimmed());
        UpdateProgressFromText(standard_output);
    }
    if (!standard_error.isEmpty()) {
        AppendLog(standard_error.trimmed());
        UpdateProgressFromText(standard_error);
    }
}

void ContentConverterDialog::UpdateProgressFromText(const QString& text) {
    static const QRegularExpression progress_pattern(QStringLiteral(R"((\d+(?:\.\d+)?)%)"));
    auto iterator = progress_pattern.globalMatch(text);
    int newest_progress = -1;

    while (iterator.hasNext()) {
        const auto match = iterator.next();
        newest_progress = qBound(0, qRound(match.captured(1).toDouble()), 100);
    }

    if (newest_progress >= 0) {
        progress_bar->setValue(newest_progress);
    }
}

void ContentConverterDialog::ProcessFinished(int exit_code, QProcess::ExitStatus exit_status) {
    ReadProcessOutput();

    if (cancel_requested) {
        SetBusy(false);
        progress_bar->setValue(0);
        current_file_label->setText(tr("Cancelled"));
        status_label->setText(tr("Conversion cancelled. Original files were preserved."));
        return;
    }

    if (exit_status != QProcess::NormalExit || exit_code != 0) {
        SetBusy(false);
        status_label->setText(tr("Conversion failed. Check the log for details."));
        QMessageBox::critical(
            this, tr("Conversion failed"),
            tr("The converter returned an error while processing %1. The original NSZ file was "
               "not modified.")
                .arg(QFileInfo(queue.value(queue_index)).fileName()));
        return;
    }

    progress_bar->setValue(100);
    AppendLog(tr("Completed: %1").arg(QFileInfo(queue.at(queue_index)).fileName()));
    ++queue_index;
    StartNextFile();
}

void ContentConverterDialog::SetBusy(bool busy) {
    add_button->setEnabled(!busy);
    remove_button->setEnabled(!busy);
    clear_button->setEnabled(!busy);
    output_button->setEnabled(!busy);
    converter_button->setEnabled(!busy);
    download_button->setEnabled(!busy);
    output_directory->setEnabled(!busy);
    converter_path->setEnabled(!busy);
    verify_checkbox->setEnabled(!busy);
    start_button->setEnabled(!busy);
    cancel_button->setEnabled(busy);
    file_list->setEnabled(!busy);
}

void ContentConverterDialog::AppendLog(const QString& text) {
    if (!text.isEmpty()) {
        log_view->appendPlainText(text);
    }
}
