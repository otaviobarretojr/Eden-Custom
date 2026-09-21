// SPDX-FileCopyrightText: Copyright 2026 Eden Custom Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "yuzu/shader_preparation_status.h"

#include <filesystem>

#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include <fmt/format.h>

#include "common/fs/path_util.h"

namespace EdenCustom {
namespace {

std::filesystem::path CacheDirectory(u64 title_id) {
    return Common::FS::GetEdenPath(Common::FS::EdenPath::ShaderDir) /
           fmt::format("{:016x}", title_id);
}

std::filesystem::path MetadataPath(u64 title_id) {
    return CacheDirectory(title_id) / "eden_custom_shader_status.json";
}

QJsonObject ReadMetadata(u64 title_id) {
    QFile file{QString::fromStdString(MetadataPath(title_id).string())};
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }

    const auto document = QJsonDocument::fromJson(file.readAll());
    return document.isObject() ? document.object() : QJsonObject{};
}

void WriteMetadata(u64 title_id, const QJsonObject& object) {
    const auto directory = CacheDirectory(title_id);
    std::error_code ec;
    std::filesystem::create_directories(directory, ec);
    if (ec) {
        return;
    }

    QSaveFile file{QString::fromStdString(MetadataPath(title_id).string())};
    if (!file.open(QIODevice::WriteOnly)) {
        return;
    }

    file.write(QJsonDocument{object}.toJson(QJsonDocument::Indented));
    file.commit();
}

quint64 FileSize(const std::filesystem::path& path) {
    const QFileInfo info{QString::fromStdString(path.string())};
    return info.exists() && info.isFile() ? static_cast<quint64>(info.size()) : 0;
}

} // namespace

void RecordShaderPreparationStart(u64 title_id, std::size_t total) {
    if (title_id == 0) {
        return;
    }

    auto object = ReadMetadata(title_id);
    object[QStringLiteral("title_id")] =
        QStringLiteral("%1").arg(title_id, 16, 16, QLatin1Char('0')).toUpper();
    object[QStringLiteral("known_pipelines")] = static_cast<qint64>(total);
    object[QStringLiteral("last_started_utc")] =
        QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    object[QStringLiteral("last_completed")] = false;
    WriteMetadata(title_id, object);
}

void RecordShaderPreparationComplete(u64 title_id) {
    if (title_id == 0) {
        return;
    }

    auto object = ReadMetadata(title_id);
    object[QStringLiteral("title_id")] =
        QStringLiteral("%1").arg(title_id, 16, 16, QLatin1Char('0')).toUpper();
    object[QStringLiteral("last_completed")] = true;
    object[QStringLiteral("last_completed_utc")] =
        QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    WriteMetadata(title_id, object);
}

ShaderPreparationStatus ReadShaderPreparationStatus(u64 title_id) {
    ShaderPreparationStatus status{};

    const auto directory = CacheDirectory(title_id);
    status.cache_directory = QString::fromStdString(directory.string());

    const auto metadata_path = MetadataPath(title_id);
    status.has_record = std::filesystem::exists(metadata_path);

    const auto object = ReadMetadata(title_id);
    status.known_pipelines =
        static_cast<std::size_t>(object.value(QStringLiteral("known_pipelines")).toInteger());
    status.last_completed =
        object.value(QStringLiteral("last_completed")).toBool(false);
    status.last_started =
        object.value(QStringLiteral("last_started_utc")).toString();
    status.last_completed_at =
        object.value(QStringLiteral("last_completed_utc")).toString();

    status.transferable_cache_bytes = FileSize(directory / "vulkan.bin");
    status.driver_cache_bytes = FileSize(directory / "vulkan_pipelines.bin");
    status.opengl_cache_bytes = FileSize(directory / "opengl.bin");

    return status;
}

QString FormatBytes(quint64 bytes) {
    constexpr double KiB = 1024.0;
    constexpr double MiB = KiB * 1024.0;
    constexpr double GiB = MiB * 1024.0;

    if (bytes >= static_cast<quint64>(GiB)) {
        return QStringLiteral("%1 GiB").arg(bytes / GiB, 0, 'f', 2);
    }
    if (bytes >= static_cast<quint64>(MiB)) {
        return QStringLiteral("%1 MiB").arg(bytes / MiB, 0, 'f', 2);
    }
    if (bytes >= static_cast<quint64>(KiB)) {
        return QStringLiteral("%1 KiB").arg(bytes / KiB, 0, 'f', 1);
    }
    return QStringLiteral("%1 B").arg(bytes);
}

} // namespace EdenCustom
