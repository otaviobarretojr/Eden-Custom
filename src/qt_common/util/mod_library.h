// SPDX-FileCopyrightText: Copyright 2026 Eden Custom Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>

#include "common/common_types.h"

namespace QtCommon::ModLibrary {

struct PackageInfo {
    QString id;
    QString name;
    QString file_name;
    QString imported_at;
    quint64 size_bytes{};
    int entry_count{};
    QSet<QString> title_ids;
    QSet<QString> entries;
    bool already_imported{};
};

QString LibraryRootPath();
QString PackagesPath();
QString NormalizeEntryPath(QString path);

bool ImportPackage(const QString& source_zip, PackageInfo& package, QString* error = nullptr);
QVector<PackageInfo> ListPackages(QString* error = nullptr);
QVector<PackageInfo> PackagesForTitle(u64 title_id, QString* error = nullptr);

bool PackageContainsSource(const PackageInfo& package, const QString& source_path,
                           const QString& container_path = {});

} // namespace QtCommon::ModLibrary
