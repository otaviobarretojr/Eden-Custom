// SPDX-FileCopyrightText: Copyright 2026 Eden Custom Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "yuzu/mod_catalog.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include "common/fs/path_util.h"
#include "common/hex_util.h"

namespace EdenCustom {
namespace {

QString BuildIdCachePath() {
    const auto cache_dir = Common::FS::GetEdenPath(Common::FS::EdenPath::CacheDir);
    const QDir root{QString::fromStdString(cache_dir.string())};
    return root.filePath(QStringLiteral("eden_custom/mod_catalog_build_ids.json"));
}

QJsonObject ReadBuildIdCache() {
    QFile input{BuildIdCachePath()};
    if (!input.open(QIODevice::ReadOnly)) {
        return {};
    }

    QJsonParseError parse_error{};
    const auto document = QJsonDocument::fromJson(input.readAll(), &parse_error);
    if (parse_error.error != QJsonParseError::NoError || !document.isObject()) {
        return {};
    }
    return document.object();
}

} // namespace

QString NormalizeBuildId(QString build_id) {
    build_id = build_id.trimmed().toUpper();
    build_id.remove(QLatin1Char(' '));

    while (build_id.size() > 16 && build_id.endsWith(QLatin1Char('0'))) {
        build_id.chop(1);
    }
    return build_id;
}

QString BuildIdToString(const std::array<u8, 0x20>& build_id) {
    return NormalizeBuildId(QString::fromStdString(Common::HexToString(build_id)));
}

bool BuildIdMatches(const QString& expected, const QString& actual) {
    const QString expected_normalized = NormalizeBuildId(expected);
    const QString actual_normalized = NormalizeBuildId(actual);

    if (expected_normalized.isEmpty() || actual_normalized.isEmpty()) {
        return false;
    }

    // Atmosphere cheat files use the first eight Build ID bytes (16 hex characters).
    if (expected_normalized.size() == 16) {
        return actual_normalized.startsWith(expected_normalized, Qt::CaseInsensitive);
    }

    return QString::compare(expected_normalized, actual_normalized, Qt::CaseInsensitive) == 0;
}

void RecordObservedBuildId(u64 title_id, const std::array<u8, 0x20>& build_id) {
    if (title_id == 0) {
        return;
    }

    const QString normalized = BuildIdToString(build_id);
    if (normalized.isEmpty()) {
        return;
    }

    const QString path = BuildIdCachePath();
    const QFileInfo file_info{path};
    QDir().mkpath(file_info.absolutePath());

    QJsonObject root = ReadBuildIdCache();
    const QString title_key =
        QStringLiteral("%1").arg(title_id, 16, 16, QLatin1Char('0')).toUpper();
    root.insert(title_key, normalized);

    QSaveFile output{path};
    if (!output.open(QIODevice::WriteOnly)) {
        return;
    }
    output.write(QJsonDocument{root}.toJson(QJsonDocument::Compact));
    output.commit();
}

QString ReadObservedBuildId(u64 title_id) {
    if (title_id == 0) {
        return {};
    }

    const QString title_key =
        QStringLiteral("%1").arg(title_id, 16, 16, QLatin1Char('0')).toUpper();
    return NormalizeBuildId(ReadBuildIdCache().value(title_key).toString());
}

QString ModCatalogPath() {
    return QDir{QCoreApplication::applicationDirPath()}
        .filePath(QStringLiteral("catalog/eden_mod_catalog.json"));
}

QVector<ModCatalogEntry> LoadModCatalog(u64 title_id, QString* error) {
    QVector<ModCatalogEntry> result;

    QFile input{ModCatalogPath()};
    if (!input.open(QIODevice::ReadOnly)) {
        if (error) {
            *error = QObject::tr("Mod catalog not found: %1").arg(QDir::toNativeSeparators(input.fileName()));
        }
        return result;
    }

    QJsonParseError parse_error{};
    const auto document = QJsonDocument::fromJson(input.readAll(), &parse_error);
    if (parse_error.error != QJsonParseError::NoError || !document.isObject()) {
        if (error) {
            *error = QObject::tr("Mod catalog is invalid: %1").arg(parse_error.errorString());
        }
        return result;
    }

    const QString title_key =
        QStringLiteral("%1").arg(title_id, 16, 16, QLatin1Char('0')).toUpper();
    const QJsonObject titles = document.object().value(QStringLiteral("titles")).toObject();
    const QJsonArray entries = titles.value(title_key).toArray();

    result.reserve(entries.size());
    for (const auto& value : entries) {
        const QJsonObject object = value.toObject();
        ModCatalogEntry entry;
        entry.title = object.value(QStringLiteral("title")).toString();
        entry.name = object.value(QStringLiteral("name")).toString();
        entry.kind = object.value(QStringLiteral("kind")).toString();
        entry.category = object.value(QStringLiteral("category")).toString();
        entry.conflict_group = object.value(QStringLiteral("conflict_group")).toString();
        entry.build_id = NormalizeBuildId(object.value(QStringLiteral("build_id")).toString());
        entry.version = object.value(QStringLiteral("version")).toString();
        entry.compatibility = object.value(QStringLiteral("compatibility")).toString();
        entry.source_repository = object.value(QStringLiteral("source_repository")).toString();
        entry.source_commit = object.value(QStringLiteral("source_commit")).toString();
        entry.source_path = object.value(QStringLiteral("source_path")).toString();
        entry.container = object.value(QStringLiteral("container")).toString();
        result.push_back(std::move(entry));
    }

    return result;
}

} // namespace EdenCustom
