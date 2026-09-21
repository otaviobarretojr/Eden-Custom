// SPDX-FileCopyrightText: Copyright 2026 Eden Custom Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "yuzu/benchmark_dialog.h"

#include <algorithm>
#include <cmath>
#include <numeric>

#include <QDateTime>
#include <QFile>
#include <QFileDialog>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTextStream>
#include <QVBoxLayout>

#include "core/core.h"
#include "core/perf_stats.h"
#include "qt_common/qt_common.h"

namespace {

double Mean(const std::vector<double>& values) {
    if (values.empty()) {
        return 0.0;
    }
    return std::accumulate(values.begin(), values.end(), 0.0) /
           static_cast<double>(values.size());
}

double Percentile(const std::vector<double>& sorted_values, double percentile) {
    if (sorted_values.empty()) {
        return 0.0;
    }

    const double position =
        std::clamp(percentile, 0.0, 1.0) * static_cast<double>(sorted_values.size() - 1);
    const auto lower = static_cast<std::size_t>(std::floor(position));
    const auto upper = static_cast<std::size_t>(std::ceil(position));

    if (lower == upper) {
        return sorted_values[lower];
    }

    const double fraction = position - static_cast<double>(lower);
    return sorted_values[lower] +
           (sorted_values[upper] - sorted_values[lower]) * fraction;
}

double LowFpsFromWorstFrames(const std::vector<double>& sorted_frame_times,
                             double fraction) {
    if (sorted_frame_times.empty()) {
        return 0.0;
    }

    const auto count = std::max<std::size_t>(
        1, static_cast<std::size_t>(
               std::ceil(static_cast<double>(sorted_frame_times.size()) * fraction)));
    const auto begin = sorted_frame_times.end() - static_cast<std::ptrdiff_t>(count);
    const double worst_mean =
        std::accumulate(begin, sorted_frame_times.end(), 0.0) / static_cast<double>(count);

    return worst_mean > 0.0 ? 1000.0 / worst_mean : 0.0;
}

} // namespace

BenchmarkDialog::BenchmarkDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("Eden Custom Benchmark"));
    setAttribute(Qt::WA_DeleteOnClose, true);
    resize(620, 520);

    auto* root = new QVBoxLayout(this);

    auto* intro = new QLabel(
        tr("Run the same route or scene in each build. Eden Custom records internal emulation "
           "frame times without clearing shader caches automatically."),
        this);
    intro->setWordWrap(true);
    root->addWidget(intro);

    auto* live_group = new QGroupBox(tr("Live session"), this);
    auto* live_layout = new QGridLayout(live_group);

    status_label = new QLabel(tr("Ready"), live_group);
    elapsed_label = new QLabel(QStringLiteral("0.0 s"), live_group);
    fps_label = new QLabel(QStringLiteral("--"), live_group);
    frametime_label = new QLabel(QStringLiteral("--"), live_group);
    shader_label = new QLabel(QStringLiteral("0"), live_group);

    live_layout->addWidget(new QLabel(tr("Status:"), live_group), 0, 0);
    live_layout->addWidget(status_label, 0, 1);
    live_layout->addWidget(new QLabel(tr("Elapsed:"), live_group), 1, 0);
    live_layout->addWidget(elapsed_label, 1, 1);
    live_layout->addWidget(new QLabel(tr("Game FPS:"), live_group), 2, 0);
    live_layout->addWidget(fps_label, 2, 1);
    live_layout->addWidget(new QLabel(tr("Emulation frame:"), live_group), 3, 0);
    live_layout->addWidget(frametime_label, 3, 1);
    live_layout->addWidget(new QLabel(tr("Shaders building:"), live_group), 4, 0);
    live_layout->addWidget(shader_label, 4, 1);
    root->addWidget(live_group);

    results_view = new QPlainTextEdit(this);
    results_view->setReadOnly(true);
    results_view->setPlaceholderText(tr("Benchmark results will appear here."));
    root->addWidget(results_view, 1);

    auto* buttons = new QHBoxLayout();
    start_button = new QPushButton(tr("Start benchmark"), this);
    stop_button = new QPushButton(tr("Stop && results"), this);
    save_button = new QPushButton(tr("Save frame-time CSV"), this);
    auto* close_button = new QPushButton(tr("Close"), this);

    buttons->addWidget(start_button);
    buttons->addWidget(stop_button);
    buttons->addWidget(save_button);
    buttons->addStretch();
    buttons->addWidget(close_button);
    root->addLayout(buttons);

    connect(start_button, &QPushButton::clicked, this, [this] { StartBenchmark(); });
    connect(stop_button, &QPushButton::clicked, this, [this] { StopBenchmark(); });
    connect(save_button, &QPushButton::clicked, this, [this] { SaveCsv(); });
    connect(close_button, &QPushButton::clicked, this, &QDialog::close);

    ResetUi();
}

BenchmarkDialog::~BenchmarkDialog() = default;

void BenchmarkDialog::ResetUi() {
    benchmark_running = false;
    fps_samples.clear();
    interval_frametime_samples.clear();
    last_frame_times.clear();
    shader_active_samples = 0;
    max_shaders_building = 0;

    status_label->setText(tr("Ready"));
    elapsed_label->setText(QStringLiteral("0.0 s"));
    fps_label->setText(QStringLiteral("--"));
    frametime_label->setText(QStringLiteral("--"));
    shader_label->setText(QStringLiteral("0"));
    stop_button->setEnabled(false);
    save_button->setEnabled(false);
}

void BenchmarkDialog::SetRunning(bool running) {
    benchmark_running = running;
    start_button->setEnabled(!running);
    stop_button->setEnabled(running);
}

void BenchmarkDialog::StartBenchmark() {
    if (!QtCommon::system || !QtCommon::system->IsPoweredOn()) {
        QMessageBox::information(this, tr("Benchmark"),
                                 tr("Start a game before beginning a benchmark session."));
        return;
    }

    QtCommon::system->GetPerfStats().ResetFrameTimeHistory();

    fps_samples.clear();
    interval_frametime_samples.clear();
    last_frame_times.clear();
    shader_active_samples = 0;
    max_shaders_building = 0;

    elapsed_timer.restart();
    results_view->clear();
    save_button->setEnabled(false);
    status_label->setText(tr("Running — follow the same test route used by the other build."));
    SetRunning(true);
}

void BenchmarkDialog::UpdateStats(const Core::PerfStatsResults& results, int shaders_building) {
    if (!benchmark_running) {
        return;
    }

    if (std::isfinite(results.average_game_fps) && results.average_game_fps >= 0.0) {
        fps_samples.push_back(results.average_game_fps);
    }

    const double frame_ms = results.frametime * 1000.0;
    if (std::isfinite(frame_ms) && frame_ms >= 0.0) {
        interval_frametime_samples.push_back(frame_ms);
    }

    if (shaders_building > 0) {
        ++shader_active_samples;
        max_shaders_building = std::max(max_shaders_building, shaders_building);
    }

    elapsed_label->setText(tr("%1 s").arg(elapsed_timer.elapsed() / 1000.0, 0, 'f', 1));
    fps_label->setText(tr("%1 FPS").arg(results.average_game_fps, 0, 'f', 1));
    frametime_label->setText(tr("%1 ms").arg(frame_ms, 0, 'f', 2));
    shader_label->setText(QString::number(shaders_building));
}

QString BenchmarkDialog::BuildResultsText(const std::vector<double>& frame_times) const {
    std::vector<double> sorted = frame_times;
    std::sort(sorted.begin(), sorted.end());

    const double mean_frame = Mean(frame_times);
    const double avg_fps = Mean(fps_samples);
    const double median_frame = Percentile(sorted, 0.50);
    const double p95_frame = Percentile(sorted, 0.95);
    const double p99_frame = Percentile(sorted, 0.99);
    const double low_1 = LowFpsFromWorstFrames(sorted, 0.01);
    const double low_01 = LowFpsFromWorstFrames(sorted, 0.001);

    return tr(
               "Duration: %1 s\n"
               "Frame-time samples: %2\n"
               "Average game FPS: %3\n"
               "Mean emulation frame: %4 ms\n"
               "Median emulation frame: %5 ms\n"
               "P95 emulation frame: %6 ms\n"
               "P99 emulation frame: %7 ms\n"
               "Derived 1%% low: %8 FPS\n"
               "Derived 0.1%% low: %9 FPS\n"
               "Intervals with shader compilation: %10\n"
               "Maximum simultaneous shaders building: %11\n\n"
               "Note: low-FPS values are derived from Eden's internal emulation frame-time "
               "history. Compare them only between runs made with the same scene and settings.")
        .arg(elapsed_timer.elapsed() / 1000.0, 0, 'f', 1)
        .arg(frame_times.size())
        .arg(avg_fps, 0, 'f', 2)
        .arg(mean_frame, 0, 'f', 3)
        .arg(median_frame, 0, 'f', 3)
        .arg(p95_frame, 0, 'f', 3)
        .arg(p99_frame, 0, 'f', 3)
        .arg(low_1, 0, 'f', 2)
        .arg(low_01, 0, 'f', 2)
        .arg(shader_active_samples)
        .arg(max_shaders_building);
}

void BenchmarkDialog::StopBenchmark() {
    if (!benchmark_running) {
        return;
    }

    SetRunning(false);
    status_label->setText(tr("Completed"));

    if (!QtCommon::system || !QtCommon::system->IsPoweredOn()) {
        results_view->setPlainText(
            tr("The game stopped before benchmark results could be collected."));
        return;
    }

    last_frame_times = QtCommon::system->GetPerfStats().GetFrameTimeHistory();
    if (last_frame_times.empty()) {
        results_view->setPlainText(
            tr("No frame-time samples were recorded. Run the benchmark for a longer period."));
        return;
    }

    results_view->setPlainText(BuildResultsText(last_frame_times));
    save_button->setEnabled(true);
}

void BenchmarkDialog::SaveCsv() {
    if (last_frame_times.empty()) {
        return;
    }

    const QString suggested =
        QStringLiteral("eden-benchmark-%1.csv")
            .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss")));
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Save benchmark frame times"), suggested, tr("CSV files (*.csv)"));
    if (path.isEmpty()) {
        return;
    }

    QFile file{path};
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::critical(this, tr("Benchmark"),
                              tr("The benchmark CSV could not be saved."));
        return;
    }

    QTextStream stream{&file};
    stream << "frame,frametime_ms\n";
    for (std::size_t i = 0; i < last_frame_times.size(); ++i) {
        stream << i << ',' << QString::number(last_frame_times[i], 'f', 6) << '\n';
    }

    status_label->setText(tr("CSV saved."));
}
