// SPDX-FileCopyrightText: Copyright 2026 Eden Custom Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <vector>

#include <QDialog>
#include <QElapsedTimer>

namespace Core {
struct PerfStatsResults;
}

class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;

class BenchmarkDialog final : public QDialog {
    Q_OBJECT

public:
    explicit BenchmarkDialog(QWidget* parent = nullptr);
    ~BenchmarkDialog() override;

    void UpdateStats(const Core::PerfStatsResults& results, int shaders_building);

private:
    void StartBenchmark();
    void StopBenchmark();
    void SaveCsv();
    void CompareCsvs();
    void CompareCsvSet();
    void SetRunning(bool running);
    void ResetUi();
    QString BuildResultsText(const std::vector<double>& frame_times) const;

    QLabel* build_label{};
    QLabel* status_label{};
    QLabel* elapsed_label{};
    QLabel* fps_label{};
    QLabel* frametime_label{};
    QLabel* shader_label{};
    QLineEdit* run_label_edit{};
    QLineEdit* scene_tag_edit{};
    QPlainTextEdit* results_view{};
    QPushButton* start_button{};
    QPushButton* stop_button{};
    QPushButton* save_button{};
    QPushButton* compare_button{};
    QPushButton* compare_set_button{};

    QElapsedTimer elapsed_timer{};
    QString build_profile;
    QString build_commit;
    QString benchmark_title_id;
    QString benchmark_settings_signature;
    QString benchmark_run_label;
    QString benchmark_scene_tag;
    bool benchmark_running{};
    double last_duration_seconds{};
    int shader_active_samples{};
    int max_shaders_building{};
    std::vector<double> fps_samples{};
    std::vector<double> interval_frametime_samples{};
    std::vector<double> last_frame_times{};
};
