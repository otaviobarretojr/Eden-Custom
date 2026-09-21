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
#include <QLineEdit>
#include <QStringList>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTextStream>
#include <QVBoxLayout>

#include "common/settings.h"
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

double StandardDeviation(const std::vector<double>& values) {
    if (values.empty()) {
        return 0.0;
    }

    const double mean = Mean(values);
    const double squared_sum =
        std::accumulate(values.begin(), values.end(), 0.0, [mean](double sum, double value) {
            const double delta = value - mean;
            return sum + delta * delta;
        });
    return std::sqrt(squared_sum / static_cast<double>(values.size()));
}

double CoefficientOfVariation(const std::vector<double>& values) {
    const double mean = Mean(values);
    if (values.empty() || std::abs(mean) < 0.0000001) {
        return 0.0;
    }
    return (StandardDeviation(values) / std::abs(mean)) * 100.0;
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



QString CurrentBenchmarkSettingsSignature() {
    const auto& values = Settings::values;
    const QStringList parts{
        QStringLiteral("backend=%1").arg(static_cast<int>(values.renderer_backend.GetValue())),
        QStringLiteral("multicore=%1").arg(values.use_multi_core.GetValue() ? 1 : 0),
        QStringLiteral("vsync=%1").arg(static_cast<int>(values.vsync_mode.GetValue())),
        QStringLiteral("speed_limit_enabled=%1")
            .arg(values.use_speed_limit.GetValue() ? 1 : 0),
        QStringLiteral("speed_limit=%1").arg(Settings::SpeedLimit()),
        QStringLiteral("cpu=%1").arg(static_cast<int>(values.cpu_accuracy.GetValue())),
        QStringLiteral("gpu=%1").arg(static_cast<int>(values.gpu_accuracy.GetValue())),
        QStringLiteral("resolution=%1").arg(static_cast<int>(values.resolution_setup.GetValue())),
        QStringLiteral("vram=%1").arg(static_cast<int>(values.vram_usage_mode.GetValue())),
        QStringLiteral("anisotropy=%1").arg(static_cast<int>(values.max_anisotropy.GetValue())),
        QStringLiteral("aa=%1").arg(static_cast<int>(values.anti_aliasing.GetValue())),
        QStringLiteral("astc=%1").arg(static_cast<int>(values.accelerate_astc.GetValue())),
        QStringLiteral("astc_recompression=%1")
            .arg(static_cast<int>(values.astc_recompression.GetValue())),
        QStringLiteral("frame_pacing=%1")
            .arg(static_cast<int>(values.frame_pacing_mode.GetValue())),
        QStringLiteral("async_gpu=%1")
            .arg(values.use_asynchronous_gpu_emulation.GetValue() ? 1 : 0),
        QStringLiteral("disk_shader_cache=%1")
            .arg(values.use_disk_shader_cache.GetValue() ? 1 : 0),
        QStringLiteral("driver_pipeline_cache=%1")
            .arg(values.use_vulkan_driver_pipeline_cache.GetValue() ? 1 : 0),
        QStringLiteral("async_shaders=%1")
            .arg(values.use_asynchronous_shaders.GetValue() ? 1 : 0),
        QStringLiteral("async_presentation=%1")
            .arg(values.async_presentation.GetValue() ? 1 : 0),
        QStringLiteral("force_max_clock=%1")
            .arg(values.renderer_force_max_clock.GetValue() ? 1 : 0),
    };
    return parts.join(QLatin1Char(';'));
}

struct BenchmarkCsvData {
    QString profile;
    QString commit;
    QString title_id;
    QString settings_signature;
    QString run_label;
    QString scene_tag;
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
    data.title_id = metadata.value(QStringLiteral("title_id")).trimmed().toUpper();
    data.settings_signature = metadata.value(QStringLiteral("settings_signature")).trimmed();
    data.run_label = metadata.value(QStringLiteral("run_label")).trimmed();
    data.scene_tag = metadata.value(QStringLiteral("scene_tag")).trimmed();
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

QString CsvEscape(const QString& value) {
    QString escaped = value;
    escaped.replace(QLatin1Char('"'), QStringLiteral("\"\""));
    if (escaped.contains(QLatin1Char(',')) || escaped.contains(QLatin1Char('"')) ||
        escaped.contains(QLatin1Char('\n')) || escaped.contains(QLatin1Char('\r'))) {
        return QLatin1Char('"') + escaped + QLatin1Char('"');
    }
    return escaped;
}

QString CsvRow(const QStringList& values) {
    QStringList escaped;
    escaped.reserve(values.size());
    for (const QString& value : values) {
        escaped.push_back(CsvEscape(value));
    }
    return escaped.join(QLatin1Char(',')) + QLatin1Char('\n');
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
    run_label_edit = new QLineEdit(live_group);
    scene_tag_edit = new QLineEdit(live_group);
    run_label_edit->setPlaceholderText(tr("Example: A1 or B1"));
    scene_tag_edit->setPlaceholderText(tr("Example: city route 01"));

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
    live_layout->addWidget(new QLabel(tr("Run label:"), live_group), 6, 0);
    live_layout->addWidget(run_label_edit, 6, 1);
    live_layout->addWidget(new QLabel(tr("Scene / route:"), live_group), 7, 0);
    live_layout->addWidget(scene_tag_edit, 7, 1);
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
    compare_set_button = new QPushButton(tr("Compare A/B set"), this);
    auto* close_button = new QPushButton(tr("Close"), this);

    buttons->addWidget(start_button);
    buttons->addWidget(stop_button);
    buttons->addWidget(save_button);
    buttons->addWidget(compare_button);
    buttons->addWidget(compare_set_button);
    buttons->addStretch();
    buttons->addWidget(close_button);
    root->addLayout(buttons);

    connect(start_button, &QPushButton::clicked, this, [this] { StartBenchmark(); });
    connect(stop_button, &QPushButton::clicked, this, [this] { StopBenchmark(); });
    connect(save_button, &QPushButton::clicked, this, [this] { SaveCsv(); });
    connect(compare_button, &QPushButton::clicked, this, [this] { CompareCsvs(); });
    connect(compare_set_button, &QPushButton::clicked, this, [this] { CompareCsvSet(); });
    connect(close_button, &QPushButton::clicked, this, &QDialog::close);

    ResetUi();
}

BenchmarkDialog::~BenchmarkDialog() = default;

void BenchmarkDialog::ResetUi() {
    benchmark_running = false;
    last_duration_seconds = 0.0;
    benchmark_title_id.clear();
    benchmark_settings_signature.clear();
    benchmark_run_label.clear();
    benchmark_scene_tag.clear();
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
    run_label_edit->setEnabled(!running);
    scene_tag_edit->setEnabled(!running);
}

void BenchmarkDialog::StartBenchmark() {
    if (!QtCommon::system || !QtCommon::system->IsPoweredOn()) {
        QMessageBox::information(this, tr("Benchmark"),
                                 tr("Start a game before beginning a benchmark session."));
        return;
    }

    const u64 title_id = QtCommon::system->GetApplicationProcessProgramID();
    benchmark_title_id =
        title_id == 0
            ? QString{}
            : QStringLiteral("%1").arg(title_id, 16, 16, QLatin1Char('0')).toUpper();
    benchmark_settings_signature = CurrentBenchmarkSettingsSignature();
    benchmark_run_label = run_label_edit->text().trimmed();
    benchmark_scene_tag = scene_tag_edit->text().trimmed();

    QtCommon::system->GetPerfStats().ResetFrameTimeHistory();

    last_duration_seconds = 0.0;
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

    QString build_header =
        tr("Build profile: %1\nCommit: %2\n").arg(build_profile, build_commit);
    if (!benchmark_run_label.isEmpty()) {
        build_header += tr("Run label: %1\n").arg(benchmark_run_label);
    }
    if (!benchmark_scene_tag.isEmpty()) {
        build_header += tr("Scene / route: %1\n").arg(benchmark_scene_tag);
    }
    build_header += QLatin1Char('\n');

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
    if (!benchmark_title_id.isEmpty()) {
        stream << "title_id," << benchmark_title_id << '\n';
    }
    stream << "settings_signature," << benchmark_settings_signature << '\n';
    if (!benchmark_run_label.isEmpty()) {
        stream << "run_label," << benchmark_run_label << '\n';
    }
    if (!benchmark_scene_tag.isEmpty()) {
        stream << "scene_tag," << benchmark_scene_tag << '\n';
    }
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
    text += MetricLine(tr("Title ID"),
                       a.title_id.isEmpty() ? not_available : a.title_id,
                       b.title_id.isEmpty() ? not_available : b.title_id, not_available);
    text += MetricLine(tr("Run label"),
                       a.run_label.isEmpty() ? not_available : a.run_label,
                       b.run_label.isEmpty() ? not_available : b.run_label, not_available);
    text += MetricLine(tr("Scene / route"),
                       a.scene_tag.isEmpty() ? not_available : a.scene_tag,
                       b.scene_tag.isEmpty() ? not_available : b.scene_tag, not_available);
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

    const bool has_settings_signatures =
        !a.settings_signature.isEmpty() && !b.settings_signature.isEmpty();
    const bool settings_match =
        has_settings_signatures && a.settings_signature == b.settings_signature;
    text += tr("Settings match: %1\n")
                .arg(has_settings_signatures
                         ? (settings_match ? tr("Yes") : tr("No"))
                         : not_available);
    const bool has_scene_tags = !a.scene_tag.isEmpty() && !b.scene_tag.isEmpty();
    const bool scene_tags_match = has_scene_tags && a.scene_tag == b.scene_tag;
    text += tr("Scene / route match: %1\n")
                .arg(has_scene_tags
                         ? (scene_tags_match ? tr("Yes") : tr("No"))
                         : not_available);

    QString warnings;
    if (has_settings_signatures && !settings_match) {
        warnings += tr("Warning: active benchmark settings differ. Match resolution, renderer, "
                       "accuracy, VRAM, filtering and asynchronous options before comparing "
                       "build performance.\n");
    }
    if (!a.title_id.isEmpty() && !b.title_id.isEmpty() && a.title_id != b.title_id) {
        warnings += tr("Warning: Title IDs differ. These benchmarks are from different games or "
                       "applications and should not be compared directly.\n");
    }
    if (has_scene_tags && !scene_tags_match) {
        warnings += tr("Warning: scene / route tags differ. Use the same test scene and route for "
                       "a fair comparison.\n");
    }
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


void BenchmarkDialog::CompareCsvSet() {
    const QStringList paths = QFileDialog::getOpenFileNames(
        this, tr("Select A/B benchmark CSVs"), {}, tr("CSV files (*.csv)"));
    if (paths.isEmpty()) {
        return;
    }
    if (paths.size() < 4) {
        QMessageBox::information(
            this, tr("Benchmark set comparison"),
            tr("Select at least four CSVs, ideally two runs from each build."));
        return;
    }

    struct LoadedRun {
        QString path;
        BenchmarkCsvData data;
    };

    std::vector<LoadedRun> runs;
    runs.reserve(static_cast<std::size_t>(paths.size()));

    for (const QString& path : paths) {
        BenchmarkCsvData csv_data;
        QString error;
        if (!LoadBenchmarkCsv(path, csv_data, error)) {
            QMessageBox::critical(
                this, tr("Benchmark set comparison"),
                tr("Could not read benchmark CSV:\n%1\n\nFile: %2").arg(error, path));
            return;
        }
        runs.push_back({path, std::move(csv_data)});
    }

    QStringList profiles;
    for (const auto& run : runs) {
        if (!profiles.contains(run.data.profile)) {
            profiles.push_back(run.data.profile);
        }
    }

    if (profiles.size() != 2) {
        QMessageBox::critical(
            this, tr("Benchmark set comparison"),
            tr("The selected CSVs must contain exactly two build profiles. Found: %1")
                .arg(profiles.join(QStringLiteral(", "))));
        return;
    }

    if (profiles.contains(QStringLiteral("Stable")) &&
        profiles.contains(QStringLiteral("Zen3-AVX2"))) {
        profiles = {QStringLiteral("Stable"), QStringLiteral("Zen3-AVX2")};
    }

    std::vector<const BenchmarkCsvData*> group_a;
    std::vector<const BenchmarkCsvData*> group_b;
    for (const auto& run : runs) {
        if (run.data.profile == profiles[0]) {
            group_a.push_back(&run.data);
        } else if (run.data.profile == profiles[1]) {
            group_b.push_back(&run.data);
        }
    }

    if (group_a.size() < 2 || group_b.size() < 2) {
        QMessageBox::information(
            this, tr("Benchmark set comparison"),
            tr("Each build profile needs at least two runs for a repeatability comparison."));
        return;
    }

    const QString not_available = tr("n/a");

    auto collect_metric = [](const std::vector<const BenchmarkCsvData*>& group,
                             const auto& getter) {
        std::vector<double> values;
        values.reserve(group.size());
        for (const auto* run : group) {
            values.push_back(getter(*run));
        }
        return values;
    };

    auto collect_optional = [](const std::vector<const BenchmarkCsvData*>& group,
                               const auto& available, const auto& getter) {
        std::vector<double> values;
        values.reserve(group.size());
        for (const auto* run : group) {
            if (available(*run)) {
                values.push_back(getter(*run));
            }
        }
        return values;
    };

    auto format_mean_sd = [&](const std::vector<double>& values, int decimals) {
        if (values.empty()) {
            return not_available;
        }
        return tr("%1 ± %2")
            .arg(Mean(values), 0, 'f', decimals)
            .arg(StandardDeviation(values), 0, 'f', decimals);
    };

    auto mean_delta = [&](const std::vector<double>& a, const std::vector<double>& b) {
        return a.empty() || b.empty() ? not_available : PercentageDelta(Mean(a), Mean(b));
    };

    const auto duration_a = collect_optional(
        group_a, [](const BenchmarkCsvData& run) { return run.has_duration; },
        [](const BenchmarkCsvData& run) { return run.duration_seconds; });
    const auto duration_b = collect_optional(
        group_b, [](const BenchmarkCsvData& run) { return run.has_duration; },
        [](const BenchmarkCsvData& run) { return run.duration_seconds; });
    const auto fps_a = collect_optional(
        group_a, [](const BenchmarkCsvData& run) { return run.has_average_game_fps; },
        [](const BenchmarkCsvData& run) { return run.average_game_fps; });
    const auto fps_b = collect_optional(
        group_b, [](const BenchmarkCsvData& run) { return run.has_average_game_fps; },
        [](const BenchmarkCsvData& run) { return run.average_game_fps; });

    const auto mean_a = collect_metric(
        group_a, [](const BenchmarkCsvData& run) { return run.mean_frame; });
    const auto mean_b = collect_metric(
        group_b, [](const BenchmarkCsvData& run) { return run.mean_frame; });
    const auto median_a = collect_metric(
        group_a, [](const BenchmarkCsvData& run) { return run.median_frame; });
    const auto median_b = collect_metric(
        group_b, [](const BenchmarkCsvData& run) { return run.median_frame; });
    const auto p95_a = collect_metric(
        group_a, [](const BenchmarkCsvData& run) { return run.p95_frame; });
    const auto p95_b = collect_metric(
        group_b, [](const BenchmarkCsvData& run) { return run.p95_frame; });
    const auto p99_a = collect_metric(
        group_a, [](const BenchmarkCsvData& run) { return run.p99_frame; });
    const auto p99_b = collect_metric(
        group_b, [](const BenchmarkCsvData& run) { return run.p99_frame; });
    const auto low1_a = collect_metric(
        group_a, [](const BenchmarkCsvData& run) { return run.low_1; });
    const auto low1_b = collect_metric(
        group_b, [](const BenchmarkCsvData& run) { return run.low_1; });
    const auto low01_a = collect_metric(
        group_a, [](const BenchmarkCsvData& run) { return run.low_01; });
    const auto low01_b = collect_metric(
        group_b, [](const BenchmarkCsvData& run) { return run.low_01; });
    const auto samples_a = collect_metric(
        group_a, [](const BenchmarkCsvData& run) {
            return static_cast<double>(run.frame_times.size());
        });
    const auto samples_b = collect_metric(
        group_b, [](const BenchmarkCsvData& run) {
            return static_cast<double>(run.frame_times.size());
        });
    const auto shaders_a = collect_optional(
        group_a, [](const BenchmarkCsvData& run) { return run.shader_active_intervals >= 0; },
        [](const BenchmarkCsvData& run) {
            return static_cast<double>(run.shader_active_intervals);
        });
    const auto shaders_b = collect_optional(
        group_b, [](const BenchmarkCsvData& run) { return run.shader_active_intervals >= 0; },
        [](const BenchmarkCsvData& run) {
            return static_cast<double>(run.shader_active_intervals);
        });
    const auto max_shaders_a = collect_optional(
        group_a, [](const BenchmarkCsvData& run) { return run.max_shaders_building >= 0; },
        [](const BenchmarkCsvData& run) {
            return static_cast<double>(run.max_shaders_building);
        });
    const auto max_shaders_b = collect_optional(
        group_b, [](const BenchmarkCsvData& run) { return run.max_shaders_building >= 0; },
        [](const BenchmarkCsvData& run) {
            return static_cast<double>(run.max_shaders_building);
        });

    QString text =
        tr("Profile A: %1 (%2 runs)\nProfile B: %3 (%4 runs)\n\n")
            .arg(profiles[0])
            .arg(group_a.size())
            .arg(profiles[1])
            .arg(group_b.size());
    text += tr("Metric | A mean ± SD | B mean ± SD | Delta B vs A\n");
    text += QStringLiteral("--------------------------------------------------------------\n");
    text += MetricLine(tr("Duration (s)"), format_mean_sd(duration_a, 2),
                       format_mean_sd(duration_b, 2), mean_delta(duration_a, duration_b));
    text += MetricLine(tr("Average game FPS"), format_mean_sd(fps_a, 2),
                       format_mean_sd(fps_b, 2), mean_delta(fps_a, fps_b));
    text += MetricLine(tr("Mean emulation frame (ms)"), format_mean_sd(mean_a, 3),
                       format_mean_sd(mean_b, 3), mean_delta(mean_a, mean_b));
    text += MetricLine(tr("Median emulation frame (ms)"), format_mean_sd(median_a, 3),
                       format_mean_sd(median_b, 3), mean_delta(median_a, median_b));
    text += MetricLine(tr("P95 emulation frame (ms)"), format_mean_sd(p95_a, 3),
                       format_mean_sd(p95_b, 3), mean_delta(p95_a, p95_b));
    text += MetricLine(tr("P99 emulation frame (ms)"), format_mean_sd(p99_a, 3),
                       format_mean_sd(p99_b, 3), mean_delta(p99_a, p99_b));
    text += MetricLine(tr("Derived 1% low (FPS)"), format_mean_sd(low1_a, 2),
                       format_mean_sd(low1_b, 2), mean_delta(low1_a, low1_b));
    text += MetricLine(tr("Derived 0.1% low (FPS)"), format_mean_sd(low01_a, 2),
                       format_mean_sd(low01_b, 2), mean_delta(low01_a, low01_b));
    text += MetricLine(tr("Frame-time samples"), format_mean_sd(samples_a, 0),
                       format_mean_sd(samples_b, 0), mean_delta(samples_a, samples_b));
    text += MetricLine(tr("Intervals with shader compilation"), format_mean_sd(shaders_a, 1),
                       format_mean_sd(shaders_b, 1), mean_delta(shaders_a, shaders_b));
    text += MetricLine(tr("Maximum simultaneous shaders building"),
                       format_mean_sd(max_shaders_a, 1), format_mean_sd(max_shaders_b, 1),
                       mean_delta(max_shaders_a, max_shaders_b));

    text += QStringLiteral("\n");
    text += tr("Run-to-run CV — Average FPS: A %1% | B %2%\n")
                .arg(fps_a.empty() ? not_available
                                   : QString::number(CoefficientOfVariation(fps_a), 'f', 2))
                .arg(fps_b.empty() ? not_available
                                   : QString::number(CoefficientOfVariation(fps_b), 'f', 2));
    text += tr("Run-to-run CV — P99 frame: A %1% | B %2%\n")
                .arg(QString::number(CoefficientOfVariation(p99_a), 'f', 2))
                .arg(QString::number(CoefficientOfVariation(p99_b), 'f', 2));

    QStringList title_ids;
    QStringList settings_signatures;
    QStringList scene_tags;
    QStringList commits;
    std::vector<double> all_durations;
    std::vector<double> all_samples;
    bool missing_title_id = false;
    bool missing_settings_signature = false;
    bool missing_scene_tag = false;
    bool missing_average_fps = false;
    bool shaders_active = false;

    for (const auto& run : runs) {
        if (run.data.title_id.isEmpty()) {
            missing_title_id = true;
        } else if (!title_ids.contains(run.data.title_id)) {
            title_ids.push_back(run.data.title_id);
        }

        if (run.data.settings_signature.isEmpty()) {
            missing_settings_signature = true;
        } else if (!settings_signatures.contains(run.data.settings_signature)) {
            settings_signatures.push_back(run.data.settings_signature);
        }

        if (run.data.scene_tag.isEmpty()) {
            missing_scene_tag = true;
        } else if (!scene_tags.contains(run.data.scene_tag)) {
            scene_tags.push_back(run.data.scene_tag);
        }

        if (!run.data.commit.isEmpty() && run.data.commit != QStringLiteral("unknown") &&
            !commits.contains(run.data.commit)) {
            commits.push_back(run.data.commit);
        }

        if (run.data.has_duration) {
            all_durations.push_back(run.data.duration_seconds);
        }
        all_samples.push_back(static_cast<double>(run.data.frame_times.size()));

        if (!run.data.has_average_game_fps) {
            missing_average_fps = true;
        }
        if (run.data.shader_active_intervals > 0) {
            shaders_active = true;
        }
    }

    const bool title_ids_match = !missing_title_id && title_ids.size() == 1;
    const bool settings_match =
        !missing_settings_signature && settings_signatures.size() == 1;
    const bool scene_tags_match = !missing_scene_tag && scene_tags.size() == 1;

    text += tr("Title ID match across set: %1\n")
                .arg(title_ids_match ? tr("Yes") : (title_ids.isEmpty() ? not_available : tr("No")));
    text += tr("Settings match across set: %1\n")
                .arg(settings_match ? tr("Yes")
                                    : (settings_signatures.isEmpty() ? not_available : tr("No")));
    text += tr("Scene / route match across set: %1\n")
                .arg(scene_tags_match ? tr("Yes")
                                      : (scene_tags.isEmpty() ? not_available : tr("No")));

    auto relative_spread = [](const std::vector<double>& values) {
        if (values.size() < 2) {
            return 0.0;
        }
        const auto [minimum, maximum] = std::minmax_element(values.begin(), values.end());
        if (*maximum <= 0.0) {
            return 0.0;
        }
        return (*maximum - *minimum) / *maximum;
    };

    QString warnings;
    if (!title_ids_match) {
        warnings += tr("Warning: Title IDs are missing or differ across the selected runs.\n");
    }
    if (!settings_match) {
        warnings +=
            tr("Warning: benchmark settings are missing or differ across the selected runs.\n");
    }
    if (!scene_tags_match) {
        warnings += tr("Warning: scene / route tags are missing or differ across the selected "
                       "runs. Use the same scene and route for every run.\n");
    }
    if (commits.size() > 1) {
        warnings += tr("Warning: commit hashes differ across the selected runs. Results may include "
                       "code changes beyond the build profile.\n");
    }
    if (all_durations.size() == runs.size() && relative_spread(all_durations) > 0.05) {
        warnings += tr("Warning: benchmark durations differ by more than 5% across the set.\n");
    }
    if (relative_spread(all_samples) > 0.10) {
        warnings += tr("Warning: frame-time sample counts differ by more than 10% across the set.\n");
    }
    if (group_a.size() != group_b.size()) {
        warnings += tr("Warning: the build profiles have different numbers of runs.\n");
    }
    if (missing_average_fps) {
        warnings += tr("Warning: at least one CSV is missing average FPS metadata. FPS aggregation "
                       "uses only runs where it is available.\n");
    }
    if (shaders_active) {
        warnings += tr("Warning: shader compilation was active during at least one run. Consider "
                       "warming the same route before measuring build performance.\n");
    }

    if (!warnings.isEmpty()) {
        text += QStringLiteral("\n") + warnings;
    }

    text += QStringLiteral("\n") +
            tr("Mean ± SD summarizes repeated runs. CV is the run-to-run coefficient of variation; "
               "lower CV means better repeatability. Delta is profile B relative to profile A. "
               "Positive frametime means B took longer; positive FPS means B was higher. No overall "
               "winner is declared automatically.");

    QString report_csv =
        QStringLiteral("record_type,name,profile_a,profile_b,delta_b_vs_a,details\n");
    report_csv += CsvRow({QStringLiteral("metadata"), QStringLiteral("profiles"), profiles[0],
                          profiles[1], QString{}, tr("%1 runs vs %2 runs")
                                                        .arg(group_a.size())
                                                        .arg(group_b.size())});
    report_csv += CsvRow({QStringLiteral("metric"), tr("Duration (s)"),
                          format_mean_sd(duration_a, 2), format_mean_sd(duration_b, 2),
                          mean_delta(duration_a, duration_b), QString{}});
    report_csv += CsvRow({QStringLiteral("metric"), tr("Average game FPS"),
                          format_mean_sd(fps_a, 2), format_mean_sd(fps_b, 2),
                          mean_delta(fps_a, fps_b), QString{}});
    report_csv += CsvRow({QStringLiteral("metric"), tr("Mean emulation frame (ms)"),
                          format_mean_sd(mean_a, 3), format_mean_sd(mean_b, 3),
                          mean_delta(mean_a, mean_b), QString{}});
    report_csv += CsvRow({QStringLiteral("metric"), tr("Median emulation frame (ms)"),
                          format_mean_sd(median_a, 3), format_mean_sd(median_b, 3),
                          mean_delta(median_a, median_b), QString{}});
    report_csv += CsvRow({QStringLiteral("metric"), tr("P95 emulation frame (ms)"),
                          format_mean_sd(p95_a, 3), format_mean_sd(p95_b, 3),
                          mean_delta(p95_a, p95_b), QString{}});
    report_csv += CsvRow({QStringLiteral("metric"), tr("P99 emulation frame (ms)"),
                          format_mean_sd(p99_a, 3), format_mean_sd(p99_b, 3),
                          mean_delta(p99_a, p99_b), QString{}});
    report_csv += CsvRow({QStringLiteral("metric"), tr("Derived 1% low (FPS)"),
                          format_mean_sd(low1_a, 2), format_mean_sd(low1_b, 2),
                          mean_delta(low1_a, low1_b), QString{}});
    report_csv += CsvRow({QStringLiteral("metric"), tr("Derived 0.1% low (FPS)"),
                          format_mean_sd(low01_a, 2), format_mean_sd(low01_b, 2),
                          mean_delta(low01_a, low01_b), QString{}});
    report_csv += CsvRow({QStringLiteral("metric"), tr("Frame-time samples"),
                          format_mean_sd(samples_a, 0), format_mean_sd(samples_b, 0),
                          mean_delta(samples_a, samples_b), QString{}});
    report_csv += CsvRow({QStringLiteral("metric"), tr("Intervals with shader compilation"),
                          format_mean_sd(shaders_a, 1), format_mean_sd(shaders_b, 1),
                          mean_delta(shaders_a, shaders_b), QString{}});
    report_csv += CsvRow({QStringLiteral("metric"),
                          tr("Maximum simultaneous shaders building"),
                          format_mean_sd(max_shaders_a, 1), format_mean_sd(max_shaders_b, 1),
                          mean_delta(max_shaders_a, max_shaders_b), QString{}});
    report_csv += CsvRow({QStringLiteral("repeatability"),
                          tr("Run-to-run CV — Average FPS (%)"),
                          fps_a.empty() ? not_available
                                        : QString::number(CoefficientOfVariation(fps_a), 'f', 2),
                          fps_b.empty() ? not_available
                                        : QString::number(CoefficientOfVariation(fps_b), 'f', 2),
                          QString{}, tr("Lower CV means better repeatability.")});
    report_csv += CsvRow({QStringLiteral("repeatability"),
                          tr("Run-to-run CV — P99 frame (%)"),
                          QString::number(CoefficientOfVariation(p99_a), 'f', 2),
                          QString::number(CoefficientOfVariation(p99_b), 'f', 2),
                          QString{}, tr("Lower CV means better repeatability.")});
    report_csv += CsvRow({QStringLiteral("validation"), tr("Title ID match"),
                          title_ids_match ? tr("Yes") : tr("No"), QString{}, QString{},
                          title_ids.join(QStringLiteral(" | "))});
    report_csv += CsvRow({QStringLiteral("validation"), tr("Settings match"),
                          settings_match ? tr("Yes") : tr("No"), QString{}, QString{},
                          QString{}});
    report_csv += CsvRow({QStringLiteral("validation"), tr("Scene / route match"),
                          scene_tags_match ? tr("Yes") : tr("No"), QString{}, QString{},
                          scene_tags.join(QStringLiteral(" | "))});
    for (const auto& run : runs) {
        report_csv += CsvRow({QStringLiteral("run"), QFileInfo{run.path}.fileName(),
                              run.data.profile, run.data.run_label, QString{},
                              run.data.scene_tag});
    }
    if (!warnings.isEmpty()) {
        report_csv += CsvRow({QStringLiteral("warnings"), QStringLiteral("summary"), QString{},
                              QString{}, QString{}, warnings.trimmed()});
    }

    auto* dialog = new QDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose, true);
    dialog->setWindowTitle(tr("Benchmark set comparison"));
    dialog->resize(900, 680);

    auto* root = new QVBoxLayout(dialog);
    auto* intro = new QLabel(
        tr("Aggregate repeated benchmark CSVs from two build profiles. For the recommended A/B/A/B "
           "test, select all four CSVs together."),
        dialog);
    intro->setWordWrap(true);
    root->addWidget(intro);

    auto* view = new QPlainTextEdit(dialog);
    view->setReadOnly(true);
    view->setPlainText(text);
    root->addWidget(view, 1);

    auto* export_button = new QPushButton(tr("Export report CSV"), dialog);
    auto* close_button = new QPushButton(tr("Close"), dialog);
    auto* buttons = new QHBoxLayout();
    buttons->addWidget(export_button);
    buttons->addStretch();
    buttons->addWidget(close_button);
    root->addLayout(buttons);
    connect(export_button, &QPushButton::clicked, dialog, [this, dialog, report_csv] {
        const QString suggested =
            QStringLiteral("eden-benchmark-report-%1.csv")
                .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss")));
        const QString report_path = QFileDialog::getSaveFileName(
            dialog, tr("Export benchmark report"), suggested, tr("CSV files (*.csv)"));
        if (report_path.isEmpty()) {
            return;
        }

        QFile report_file{report_path};
        if (!report_file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QMessageBox::critical(dialog, tr("Benchmark set comparison"),
                                  tr("The benchmark report could not be saved."));
            return;
        }

        QTextStream report_stream{&report_file};
        report_stream << report_csv;
    });
    connect(close_button, &QPushButton::clicked, dialog, &QDialog::close);

    dialog->show();
}
