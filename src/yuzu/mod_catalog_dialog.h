// SPDX-FileCopyrightText: Copyright 2026 Eden Custom Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QDialog>

#include "common/common_types.h"
#include "yuzu/mod_catalog.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;
class QTableWidget;

class ModCatalogDialog final : public QDialog {
    Q_OBJECT

public:
    explicit ModCatalogDialog(u64 title_id, const QString& observed_build_id,
                              QWidget* parent = nullptr);
    ~ModCatalogDialog() override = default;

private:
    void BuildUi();
    void ReloadTable();
    void OpenSelectedSource();
    QString CompatibilityText(const EdenCustom::ModCatalogEntry& entry) const;
    bool IsExactMatch(const EdenCustom::ModCatalogEntry& entry) const;

    u64 title_id{};
    QString observed_build_id;
    QVector<EdenCustom::ModCatalogEntry> entries;

    QLabel* summary_label{};
    QLabel* build_id_label{};
    QComboBox* category_filter{};
    QCheckBox* compatible_only{};
    QTableWidget* table{};
    QPushButton* source_button{};
};
