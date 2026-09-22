// SPDX-FileCopyrightText: Copyright 2026 Eden Custom Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>

#include <QString>
#include <QVector>

#include "common/common_types.h"

namespace EdenCustom {

struct ModCatalogEntry {
    QString title;
    QString name;
    QString kind;
    QString category;
    QString conflict_group;
    QString build_id;
    QString version;
    QString compatibility;
    QString source_repository;
    QString source_commit;
    QString source_path;
    QString container;
};

QString NormalizeBuildId(QString build_id);
QString BuildIdToString(const std::array<u8, 0x20>& build_id);
bool BuildIdMatches(const QString& expected, const QString& actual);

void RecordObservedBuildId(u64 title_id, const std::array<u8, 0x20>& build_id);
QString ReadObservedBuildId(u64 title_id);

QVector<ModCatalogEntry> LoadModCatalog(u64 title_id, QString* error = nullptr);
QString ModCatalogPath();

} // namespace EdenCustom
