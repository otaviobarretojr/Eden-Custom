// SPDX-FileCopyrightText: Copyright 2026 Eden Custom Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstddef>
#include <functional>

#include <QString>
#include <QStringList>

namespace QtCommon::Archive {

using ProgressCallback = std::function<bool(std::size_t total, std::size_t progress)>;

QStringList ExtractZip(const QString& archive_path, const QString& destination,
                       ProgressCallback callback = {});

} // namespace QtCommon::Archive
