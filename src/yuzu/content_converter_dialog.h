// SPDX-FileCopyrightText: Copyright 2026 Eden Custom Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QByteArray>
#include <QDialog>
#include <QProcess>
#include <QString>
#include <QStringList>

class QDragEnterEvent;
class QDropEvent;
class QLabel;
class QLineEdit;
class QNetworkAccessManager;
class QNetworkReply;
class QListWidget;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QCheckBox;

class ContentConverterDialog final : public QDialog {
    Q_OBJECT

public:
    explicit ContentConverterDialog(QWidget* parent = nullptr,
                                    const QStringList& initial_files = {});
    ~ContentConverterDialog() override;

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

signals:
    void InstallConvertedContentRequested(const QStringList& files);
    void AddConvertedDirectoriesRequested(const QStringList& directories);

private slots:
    void AddFiles();
    void RemoveSelectedFiles();
    void ClearFiles();
    void ChooseOutputDirectory();
    void ChooseConverterExecutable();
    void DownloadConverter();
    void FinishConverterDownload();
    void StartConversion();
    void CancelConversion();
    void ReadProcessOutput();
    void ProcessFinished(int exit_code, QProcess::ExitStatus exit_status);

private:
    void BuildUi();
    void AddFile(const QString& path);
    void SetBusy(bool busy);
    void StartNextFile();
    void AppendLog(const QString& text);
    void UpdateProgressFromText(const QString& text);
    void InspectConvertedFile(const QString& input_path);
    void BeginCompatibilityFallback();
    bool InstallCompatibilityFallback(const QByteArray& payload, QString* error);
    bool PrepareCompatibilityFallbackKeys(QString* error);
    void CleanupCompatibilityFallbackKeys();
    bool IsCompatibilityFallbackProgram(const QString& program) const;
    bool ResolveConverter(QString& program, QStringList& prefix_arguments) const;
    QString EdenKeysDirectory() const;
    QString ManagedConverterPath() const;
    QString CompatibilityFallbackRootPath() const;
    QString CompatibilityFallbackArchivePath() const;
    QString CompatibilityFallbackConverterPath() const;

    QListWidget* file_list{};
    QLineEdit* output_directory{};
    QLineEdit* converter_path{};
    QLabel* current_file_label{};
    QLabel* status_label{};
    QProgressBar* progress_bar{};
    QPlainTextEdit* log_view{};
    QCheckBox* verify_checkbox{};
    QCheckBox* auto_install_checkbox{};
    QCheckBox* auto_library_checkbox{};
    QPushButton* add_button{};
    QPushButton* remove_button{};
    QPushButton* clear_button{};
    QPushButton* output_button{};
    QPushButton* converter_button{};
    QPushButton* download_button{};
    QPushButton* start_button{};
    QPushButton* cancel_button{};

    QProcess* process{};
    QNetworkAccessManager* network_manager{};
    QNetworkReply* download_reply{};
    QStringList queue{};
    QString compatibility_fallback_keys_path{};
    QStringList pending_install_files{};
    QStringList pending_library_dirs{};
    int queue_index{};
    bool cancel_requested{};
    bool downloading_compatibility_fallback{};
    bool force_compatibility_fallback{};
    bool fallback_retry_attempted{};
    bool current_process_uses_fallback{};
};
