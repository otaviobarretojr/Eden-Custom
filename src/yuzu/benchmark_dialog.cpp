// SPDX-FileCopyrightText: Copyright 2026 Eden Custom Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "yuzu/benchmark_dialog.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numeric>
#include <utility>

#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHash>
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

std::pair<QString, QString> ReadBuildIdentity() {
    QString profile = QObject::tr("Developer build");
    QString commit = QStringLiteral("unknown");

    QFile file{QCoreApplication::applicationDirPath() +
               QStringLiteral("/PERFORMANCE_PROFILE.txt")};
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {profile, commit};
    }

    QTextStream stream{&file};
    while (!stream.atEnd()) {
        const QString line = stream.readLine().trimmed();
        if (line.startsWith(QStringLiteral("Profile:"))) {
            profile = line.section(QLatin1Char(':'), 1).trimmed();
        } else if (line.startsWith(QStringLiteral("Commit:"))) {
            commit = line.section(QLatin1Char(':'), 1).trimmed();
        }
    }

    return {profile, commit};
}

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


struct BenchmarkCsvData {
    QString profile;
    QString commit;
    bool has_duration{};
    double duration_seconds{};
    bool has_average_game_fps{};
    double average_game_fps{};
    int shader_active_intervals{-1};
    int max_shaders_building{-1};
    std::vector<double> frame_times;
    double mean_frame{};
    double median_frame{};
    double p95_frame{};
    double p99_frame{};
    double low_1{};
    double low_01{};
};

bool ReadMetadataDouble(const QHash<QString, QString>& metadata, const QString& key,
                        double& value) {
    const auto it = metadata.constFind(key);
    if (it == metadata.cend()) {
        return false;
    }

    bool ok = false;
    const double parsed = it.value().toDouble(&ok);
    if (!ok || !std::isfinite(parsed)) {
        return false;
    }

    value = parsed;
    return true;
}

int ReadMetadataInt(const QHash<QString, QString>& metadata, const QString& key) {
    const auto it = metadata.constFind(key);
    if (it == metadata.cend()) {
        return -1;
    }

    bool ok = false;
    const int parsed = it.value().toInt(&ok);
    return ok ? parsed : -1;
}

bool LoadBenchmarkCsv(const QString& path, BenchmarkCsvData& data, QString& error) {
    QFile file{path};
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        error = QCoreApplication::translate("BenchmarkDialog", "Could not open the file.");
        return false;
    }

    QHash<QString, QString> metadata;
    bool reading_frames = false;
    QTextStream stream{&file};

    while (!stream.atEnd()) {
        QString line = stream.readLine().trimmed();
        if (line.startsWith(QChar::ByteOrderMark)) {
            line.remove(0, 1);
        }
        if (line.isEmpty()) {
            continue;
        }

        if (line == QStringLiteral("frame,frametime_ms")) {
            reading_frames = true;
            continue;
        }

        const qsizetype comma = line.indexOf(QLatin1Char(','));
        if (comma <= 0) {
            continue;
        }

        if (!reading_frames) {
            const QString key = line.left(comma).trimmed();
            if (key != QStringLiteral("metadata")) {
                metadata.insert(key, line.mid(comma + 1).trimmed());
            }
            continue;
        }

        bool ok = false;
        const double frame_time = line.mid(comma + 1).trimmed().toDouble(&ok);
        if (ok && std::isfinite(frame_time) && frame_time >= 0.0) {
            data.frame_times.push_back(frame_time);
        }
    }

    if (data.frame_times.empty()) {
        error = QCoreApplication::translate("BenchmarkDialog",
                                            "No valid frame-time samples were found.");
        return false;
    }

    data.profile = metadata.value(QStringLiteral("profile"), QFileInfo{path}.baseName());
    data.commit = metadata.value(QStringLiteral("commit"), QStringLiteral("unknown"));
    data.has_duration =
        ReadMetadataDouble(metadata, QStringLiteral("duration_seconds"), data.duration_seconds);
    data.has_average_game_fps = ReadMetadataDouble(
        metadata, QStringLiteral("average_game_fps"), data.average_game_fps);
    data.shader_active_intervals =
        ReadMetadataInt(metadata, QStringLiteral("shader_active_intervals"));
    data.max_shaders_building =
        ReadMetadataInt(metadata, QStringLiteral("max_shaders_building"));

    std::vector<double> sorted = data.frame_times;
    std::sort(sorted.begin(), sorted.end());
    data.mean_frame = Mean(data.frame_times);
    data.median_frame = Percentile(sorted, 0.50);
    data.p95_frame = Percentile(sorted, 0.95);
    data.p99_frame = Percentile(sorted, 0.99);
    data.low_1 = LowFpsFromWorstFrames(sorted, 0.01);
    data.low_01 = LowFpsFromWorstFrames(sorted, 0.001);
    return true;
}

QString PercentageDelta(double a, double b) {
    if (!std::isfinite(a) || !std::isfinite(b) || std::abs(a) < 0.0000001) {
        return QCoreApplication::translate("BenchmarkDialog", "n/a");
    }

    const double delta = ((b - a) / a) * 100.0;
    QString text = QString::number(delta, 'f', 2);
    if (delta > 0.0) {
        text.prepend(QLatin1Char('+'));
    }
    return text + QLatin1Char('%');
}

QString MetricLine(const QString& label, const QString& a, const QString& b,
                   const QString& delta) {
    return QStringLiteral("%1 | %2 | %3 | %4\n").arg(label, a, b, delta);
}

} // namespace

BenchmarkDialog::BenchmarkDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("Eden Custom Benchmark"));
    setAttribute(Qt::WA_DeleteOnClose, true);
    resize(620, 520);

    const auto build_identity = ReadBuildIdentity();
    build_profile = build_identity.first;
    build_commit = build_identity.second;

    auto* root = new QVBoxLayout(this);

    auto* intro = new QLabel(
        tr("Run the same route or scene in each build. Eden Custom records internal emulation "
           "frame times without clearing shader caches automatically."),
        this);
    intro->setWordWrap(true);
    root->addWidget(intro);

    auto* live_group = new QGroupBox(tr("Live session"), this);
    auto* live_layout = new QGridLayout(live_group);

    const QString short_commit =
        build_commit == QStringLiteral("unknown") ? build_commit : build_commit.left(12);
    build_label = new QLabel(tr("%1 • %2").arg(build_profile, short_commit), live_group);
    status_label = new QLabel(tr("Ready"), live_group);
    elapsed_label = new QLabel(QStringLiteral("0.0 s"), live_group);
    fps_label = new QLabel(QStringLiteral("--"), live_group);
    frametime_label = new QLabel(QStringLiteral("--"), live_group);
    shader_label = new QLabel(QStringLiteral("0"), live_group);

    live_layout->addWidget(new QLabel(tr("Build:"), live_group), 0, 0);
    live_layout->addWidget(build_label, 0, 1);
    live_layout->addWidget(new QLabel(tr("Status:"), live_group), 1, 0);
    live_layout->addWidget(status_label, 1, 1);
    live_layout->addWidget(new QLabel(tr("Elapsed:"), live_group), 2, 0);
    live_layout->addWidget(elapsed_label, 2, 1);
    live_layout->addWidget(new QLabel(tr("Game FPS:"), live_group), 3, 0);
    live_layout->addWidget(fps_label, 3, 1);
    live_layout->addWidget(new QLabel(tr("Emulation frame:"), live_group), 4, 0);
    live_layout->addWidget(frametime_label, 4, 1);
    live_layout->addWidget(new QLabel(tr("Shaders building:"), live_group), 5, 0);
    live_layout->addWidget(shader_label, 5, 1);
    root->addWidget(live_group);

    results_view = new QPlainTextEdit(this);
    results_view->setReadOnly(true);
    results_view->setPlaceholderText(tr("Benchmark results will appear here."));
    root->addWidget(results_view, 1);

    auto* buttons = new QHBoxLayout();
    start_button = new QPushButton(tr("Start benchmark"), this);
    stop_button = new QPushButton(tr("Stop && results"), this);
    save_button = new QPushButton(tr("Save frame-time CSV"), this);
    compare_button = new QPushButton(tr("Compare CSVs"), this);
    auto* close_button = new QPushButton(tr("Close"), this);

    buttons->addWidget(start_button);
    buttons->addWidget(stop_button);
    buttons->addWidget(save_button);
    buttons->addWidget(compare_button);
    buttons->addStretch();
    buttons->addWidget(close_button);
    root->addLayout(buttons);

    connect(start_button, &QPushButton::clicked, this, [this] { StartBenchmark(); });
    connect(stop_button, &QPushButton::clicked, this, [this] { StopBenchmark(); });
    connect(save_button, &QPushButton::clicked, this, [this] { SaveCsv(); });
    connect(compare_button, &QPushButton::clicked, this, [this] { CompareCsvs(); });
    connect(close_button, &QPushButton::clicked, this, &QDialog::close);

    ResetUi();
}

BenchmarkDialog::~BenchmarkDialog() = default;

void BenchmarkDialog::ResetUi() {
    benchmark_running = false;
    last_duration_seconds = 0.0;
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

    elapsed_label->setText(tr("%1 s").arg(last_duration_seconds > 0.0 ? last_duration_seconds
                                         : elapsed_timer.elapsed() / 1000.0,
             0, 'f', 1));
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

    const QString build_header =
        tr("Build profile: %1\nCommit: %2\n\n").arg(build_profile, build_commit);

    return build_header +
           tr(
               "Duration: %1 s\n"
               "Frame-time samples: %2\n"
               "Average game FPS: %3\n"
               "Mean emulation frame: %4 ms\n"
               "Median emulation frame: %5 ms\n"
               "P95 emulation frame: %6 ms\n"
               "P99 emulation frame: %7 ms\n"
               "Derived 1% low: %8 FPS\n"
               "Derived 0.1% low: %9 FPS\n"
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
    last_duration_seconds = elapsed_timer.elapsed() / 1000.0;
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

    QString profile_slug = build_profile;
    profile_slug.replace(QLatin1Char(' '), QLatin1Char('-'));
    profile_slug.replace(QLatin1Char('/'), QLatin1Char('-'));

    const QString suggested =
        QStringLiteral("eden-benchmark-%1-%2.csv")
            .arg(profile_slug,
                 QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss")));
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

    std::vector<double> sorted = last_frame_times;
    std::sort(sorted.begin(), sorted.end());
    const double average_game_fps = Mean(fps_samples);
    const double mean_frame = Mean(last_frame_times);
    const double median_frame = Percentile(sorted, 0.50);
    const double p95_frame = Percentile(sorted, 0.95);
    const double p99_frame = Percentile(sorted, 0.99);
    const double low_1 = LowFpsFromWorstFrames(sorted, 0.01);
    const double low_01 = LowFpsFromWorstFrames(sorted, 0.001);

    QTextStream stream{&file};
    stream << "metadata,value\n";
    stream << "profile," << build_profile << '\n';
    stream << "commit," << build_commit << '\n';
    stream << "duration_seconds," << QString::number(last_duration_seconds, 'f', 3) << '\n';
    stream << "frame_time_samples," << last_frame_times.size() << '\n';
    stream << "average_game_fps," << QString::number(average_game_fps, 'f', 6) << '\n';
    stream << "mean_emulation_frame_ms," << QString::number(mean_frame, 'f', 6) << '\n';
    stream << "median_emulation_frame_ms," << QString::number(median_frame, 'f', 6) << '\n';
    stream << "p95_emulation_frame_ms," << QString::number(p95_frame, 'f', 6) << '\n';
    stream << "p99_emulation_frame_ms," << QString::number(p99_frame, 'f', 6) << '\n';
    stream << "derived_1pct_low_fps," << QString::number(low_1, 'f', 6) << '\n';
    stream << "derived_0_1pct_low_fps," << QString::number(low_01, 'f', 6) << '\n';
    stream << "shader_active_intervals," << shader_active_samples << '\n';
    stream << "max_shaders_building," << max_shaders_building << '\n';
    stream << '\n';
    stream << "frame,frametime_ms\n";
    for (std::size_t i = 0; i < last_frame_times.size(); ++i) {
        stream << i << ',' << QString::number(last_frame_times[i], 'f', 6) << '\n';
    }

    status_label->setText(tr("CSV saved."));
}


void BenchmarkDialog::CompareCsvs() {
    const QString path_a = QFileDialog::getOpenFileName(
        this, tr("Select benchmark A"), {}, tr("CSV files (*.csv)"));
    if (path_a.isEmpty()) {
        return;
    }

    const QString path_b = QFileDialog::getOpenFileName(
        this, tr("Select benchmark B"), {}, tr("CSV files (*.csv)"));
    if (path_b.isEmpty()) {
        return;
    }

    BenchmarkCsvData a;
    BenchmarkCsvData b;
    QString error;

    if (!LoadBenchmarkCsv(path_a, a, error)) {
        QMessageBox::critical(this, tr("Benchmark comparison"),
                              tr("Could not read benchmark A:\n%1").arg(error));
        return;
    }
    if (!LoadBenchmarkCsv(path_b, b, error)) {
        QMessageBox::critical(this, tr("Benchmark comparison"),
                              tr("Could not read benchmark B:\n%1").arg(error));
        return;
    }

    const QString not_available = tr("n/a");
    auto format = [](double value, int decimals) {
        return QString::number(value, 'f', decimals);
    };
    auto optional_metric = [&](bool available, double value, int decimals) {
        return available ? format(value, decimals) : not_available;
    };
    auto optional_delta = [&](bool available_a, double value_a, bool available_b,
                              double value_b) {
        return available_a && available_b ? PercentageDelta(value_a, value_b) : not_available;
    };
    auto integer_metric = [&](int value) {
        return value >= 0 ? QString::number(value) : not_available;
    };

    QString text =
        tr("Run A: %1 • %2\nRun B: %3 • %4\n\n").arg(a.profile, a.commit, b.profile, b.commit);
    text += tr("Metric | A | B | Delta B vs A\n");
    text += QStringLiteral("-----------------------------------------------\n");
    text += MetricLine(tr("Duration (s)"),
                       optional_metric(a.has_duration, a.duration_seconds, 2),
                       optional_metric(b.has_duration, b.duration_seconds, 2),
                       optional_delta(a.has_duration, a.duration_seconds, b.has_duration,
                                      b.duration_seconds));
    text += MetricLine(tr("Average game FPS"),
                       optional_metric(a.has_average_game_fps, a.average_game_fps, 2),
                       optional_metric(b.has_average_game_fps, b.average_game_fps, 2),
                       optional_delta(a.has_average_game_fps, a.average_game_fps,
                                      b.has_average_game_fps, b.average_game_fps));
    text += MetricLine(tr("Mean emulation frame (ms)"), format(a.mean_frame, 3),
                       format(b.mean_frame, 3), PercentageDelta(a.mean_frame, b.mean_frame));
    text += MetricLine(tr("Median emulation frame (ms)"), format(a.median_frame, 3),
                       format(b.median_frame, 3),
                       PercentageDelta(a.median_frame, b.median_frame));
    text += MetricLine(tr("P95 emulation frame (ms)"), format(a.p95_frame, 3),
                       format(b.p95_frame, 3), PercentageDelta(a.p95_frame, b.p95_frame));
    text += MetricLine(tr("P99 emulation frame (ms)"), format(a.p99_frame, 3),
                       format(b.p99_frame, 3), PercentageDelta(a.p99_frame, b.p99_frame));
    text += MetricLine(tr("Derived 1% low (FPS)"), format(a.low_1, 2), format(b.low_1, 2),
                       PercentageDelta(a.low_1, b.low_1));
    text += MetricLine(tr("Derived 0.1% low (FPS)"), format(a.low_01, 2),
                       format(b.low_01, 2), PercentageDelta(a.low_01, b.low_01));
    text += MetricLine(tr("Frame-time samples"), QString::number(a.frame_times.size()),
                       QString::number(b.frame_times.size()),
                       PercentageDelta(static_cast<double>(a.frame_times.size()),
                                       static_cast<double>(b.frame_times.size())));
    text += MetricLine(tr("Intervals with shader compilation"),
                       integer_metric(a.shader_active_intervals),
                       integer_metric(b.shader_active_intervals), not_available);
    text += MetricLine(tr("Maximum simultaneous shaders building"),
                       integer_metric(a.max_shaders_building),
                       integer_metric(b.max_shaders_building), not_available);

    QString warnings;
    if (a.has_duration && b.has_duration) {
        const double largest_duration = std::max(a.duration_seconds, b.duration_seconds);
        if (largest_duration > 0.0 &&
            std::abs(a.duration_seconds - b.duration_seconds) / largest_duration > 0.05) {
            warnings += tr("Warning: benchmark durations differ by more than 5%. Use the same "
                           "route and duration for a fair comparison.\n");
        }
    }

    const double largest_sample_count =
        static_cast<double>(std::max(a.frame_times.size(), b.frame_times.size()));
    if (largest_sample_count > 0.0 &&
        std::abs(static_cast<double>(a.frame_times.size()) -
                 static_cast<double>(b.frame_times.size())) /
                largest_sample_count >
            0.10) {
        warnings += tr("Warning: frame-time sample counts differ by more than 10%. Check that both "
                       "runs used the same route and duration.\n");
    }

    if (!warnings.isEmpty()) {
        text += QStringLiteral("\n") + warnings;
    }
    text += QStringLiteral("\n") +
            tr("Delta is B relative to A. Positive frametime means B took longer; positive FPS "
               "means B was higher. Compare only runs made with the same game, scene, settings "
               "and route.");

    auto* dialog = new QDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose, true);
    dialog->setWindowTitle(tr("Benchmark comparison"));
    dialog->resize(820, 620);

    auto* root = new QVBoxLayout(dialog);
    auto* intro = new QLabel(
        tr("Compare two Eden Custom benchmark CSVs. New-format CSVs include FPS and shader "
           "metadata; older CSVs remain compatible for frame-time metrics."),
        dialog);
    intro->setWordWrap(true);
    root->addWidget(intro);

    auto* view = new QPlainTextEdit(dialog);
    view->setReadOnly(true);
    view->setPlainText(text);
    root->addWidget(view, 1);

    auto* close_button = new QPushButton(tr("Close"), dialog);
    auto* buttons = new QHBoxLayout();
    buttons->addStretch();
    buttons->addWidget(close_button);
    root->addLayout(buttons);
    connect(close_button, &QPushButton::clicked, dialog, &QDialog::close);

    dialog->show();
}
