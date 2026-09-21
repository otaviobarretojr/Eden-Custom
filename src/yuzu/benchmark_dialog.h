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
    void SetRunning(bool running);
    void ResetUi();
    QString BuildResultsText(const std::vector<double>& frame_times) const;

    QLabel* status_label{};
    QLabel* elapsed_label{};
    QLabel* fps_label{};
    QLabel* frametime_label{};
    QLabel* shader_label{};
    QPlainTextEdit* results_view{};
    QPushButton* start_button{};
    QPushButton* stop_button{};
    QPushButton* save_button{};

    QElapsedTimer elapsed_timer{};
    bool benchmark_running{};
    int shader_active_samples{};
    int max_shaders_building{};
    std::vector<double> fps_samples{};
    std::vector<double> interval_frametime_samples{};
    std::vector<double> last_frame_times{};
};
