// SPDX-FileCopyrightText: Copyright 2026 Eden Custom Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "yuzu/mod_catalog_dialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QUrl>
#include <QVBoxLayout>

namespace {
constexpr int EntryIndexRole = Qt::UserRole + 1;

QString DisplayCategory(const QString& category) {
    if (category == QStringLiteral("dynamic_fps")) {
        return QObject::tr("Dynamic FPS");
    }
    if (category == QStringLiteral("60_fps")) {
        return QObject::tr("60 FPS");
    }
    if (category == QStringLiteral("120_fps")) {
        return QObject::tr("120 FPS");
    }
    if (category == QStringLiteral("30_fps")) {
        return QObject::tr("30 FPS");
    }
    if (category == QStringLiteral("fps")) {
        return QObject::tr("FPS");
    }
    if (category == QStringLiteral("aspect_ratio")) {
        return QObject::tr("Aspect ratio");
    }
    if (category == QStringLiteral("resolution")) {
        return QObject::tr("Resolution");
    }
    if (category == QStringLiteral("graphics")) {
        return QObject::tr("Graphics");
    }
    if (category == QStringLiteral("cheat")) {
        return QObject::tr("Cheat");
    }
    return QObject::tr("Other");
}

QString RelativeSourcePath(QString path) {
    const QString root = QStringLiteral("Switch-Emulator-Mod-Database-develop/");
    if (path.startsWith(root, Qt::CaseInsensitive)) {
        path.remove(0, root.size());
    }
    return path;
}

} // namespace

ModCatalogDialog::ModCatalogDialog(u64 title_id_, const QString& observed_build_id_,
                                   QWidget* parent)
    : QDialog(parent), title_id{title_id_},
      observed_build_id{EdenCustom::NormalizeBuildId(observed_build_id_)} {
    QString error;
    entries = EdenCustom::LoadModCatalog(title_id, &error);

    BuildUi();

    if (!error.isEmpty()) {
        QMessageBox::warning(this, tr("Mod Catalog"), error);
    }

    ReloadTable();
}

void ModCatalogDialog::BuildUi() {
    setWindowTitle(tr("Eden Custom — Compatible Mods"));
    resize(980, 620);

    auto* layout = new QVBoxLayout(this);

    const QString title_id_text =
        QStringLiteral("%1").arg(title_id, 16, 16, QLatin1Char('0')).toUpper();

    summary_label = new QLabel(this);
    summary_label->setText(tr("Title ID: %1").arg(title_id_text));
    summary_label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(summary_label);

    build_id_label = new QLabel(this);
    build_id_label->setWordWrap(true);
    if (observed_build_id.isEmpty()) {
        build_id_label->setText(
            tr("Build ID has not been observed yet. Launch this game once with Eden Custom, "
               "then close it and reopen this screen to verify exact mod compatibility."));
    } else {
        build_id_label->setText(tr("Observed Build ID: %1").arg(observed_build_id));
        build_id_label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    }
    layout->addWidget(build_id_label);

    auto* filters = new QHBoxLayout();
    category_filter = new QComboBox(this);
    category_filter->addItem(tr("All categories"), QString{});
    category_filter->addItem(tr("FPS"), QStringLiteral("framerate"));
    category_filter->addItem(tr("Graphics"), QStringLiteral("graphics"));
    category_filter->addItem(tr("Resolution"), QStringLiteral("resolution"));
    category_filter->addItem(tr("Aspect ratio"), QStringLiteral("aspect_ratio"));
    category_filter->addItem(tr("Cheats"), QStringLiteral("cheat"));

    compatible_only = new QCheckBox(tr("Show exact Build ID matches only"), this);
    compatible_only->setChecked(!observed_build_id.isEmpty());
    compatible_only->setEnabled(!observed_build_id.isEmpty());

    filters->addWidget(category_filter);
    filters->addWidget(compatible_only);
    filters->addStretch();
    layout->addLayout(filters);

    table = new QTableWidget(this);
    table->setColumnCount(6);
    table->setHorizontalHeaderLabels(
        {tr("Mod"), tr("Category"), tr("Version"), tr("Build ID"), tr("Status"), tr("Conflict")});
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setAlternatingRowColors(true);
    table->setSortingEnabled(true);
    table->verticalHeader()->setVisible(false);
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(5, QHeaderView::ResizeToContents);
    layout->addWidget(table, 1);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    source_button = new QPushButton(tr("Open source"), this);
    source_button->setEnabled(false);
    buttons->addButton(source_button, QDialogButtonBox::ActionRole);
    layout->addWidget(buttons);

    connect(category_filter, &QComboBox::currentIndexChanged, this,
            &ModCatalogDialog::ReloadTable);
    connect(compatible_only, &QCheckBox::toggled, this, &ModCatalogDialog::ReloadTable);
    connect(table, &QTableWidget::itemSelectionChanged, this, [this] {
        source_button->setEnabled(!table->selectedItems().isEmpty());
    });
    connect(table, &QTableWidget::cellDoubleClicked, this,
            [this](int, int) { OpenSelectedSource(); });
    connect(source_button, &QPushButton::clicked, this, &ModCatalogDialog::OpenSelectedSource);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

bool ModCatalogDialog::IsExactMatch(const EdenCustom::ModCatalogEntry& entry) const {
    return !observed_build_id.isEmpty() && !entry.build_id.isEmpty() &&
           EdenCustom::BuildIdMatches(entry.build_id, observed_build_id);
}

QString ModCatalogDialog::CompatibilityText(const EdenCustom::ModCatalogEntry& entry) const {
    if (entry.build_id.isEmpty()) {
        return tr("Unverified");
    }
    if (observed_build_id.isEmpty()) {
        return tr("Launch once to verify");
    }
    return IsExactMatch(entry) ? tr("Compatible") : tr("Different Build ID");
}

void ModCatalogDialog::ReloadTable() {
    table->setSortingEnabled(false);
    table->setRowCount(0);

    const QString filter = category_filter->currentData().toString();
    for (int index = 0; index < entries.size(); ++index) {
        const auto& entry = entries.at(index);

        bool category_matches = filter.isEmpty();
        if (filter == QStringLiteral("framerate")) {
            category_matches = entry.conflict_group == QStringLiteral("framerate");
        } else if (!filter.isEmpty()) {
            category_matches = entry.category == filter || entry.conflict_group == filter;
        }

        if (!category_matches) {
            continue;
        }
        if (compatible_only->isChecked() && !IsExactMatch(entry)) {
            continue;
        }

        const int row = table->rowCount();
        table->insertRow(row);

        auto* name = new QTableWidgetItem(entry.name);
        name->setData(EntryIndexRole, index);
        table->setItem(row, 0, name);
        table->setItem(row, 1, new QTableWidgetItem(DisplayCategory(entry.category)));
        table->setItem(row, 2, new QTableWidgetItem(entry.version.isEmpty() ? tr("Unknown") : entry.version));
        table->setItem(row, 3, new QTableWidgetItem(entry.build_id.isEmpty() ? tr("Unknown") : entry.build_id));
        table->setItem(row, 4, new QTableWidgetItem(CompatibilityText(entry)));
        table->setItem(
            row, 5,
            new QTableWidgetItem(entry.conflict_group.isEmpty() ? QStringLiteral("—")
                                                                 : entry.conflict_group));
    }

    const QString title_id_text =
        QStringLiteral("%1").arg(title_id, 16, 16, QLatin1Char('0')).toUpper();
    if (observed_build_id.isEmpty()) {
        summary_label->setText(
            tr("Title ID: %1 — %2 catalog record(s)").arg(title_id_text).arg(entries.size()));
    } else {
        const int all_exact_matches =
            std::count_if(entries.cbegin(), entries.cend(),
                          [this](const auto& entry) { return IsExactMatch(entry); });
        summary_label->setText(
            tr("Title ID: %1 — %2 exact Build ID match(es) of %3 catalog record(s)")
                .arg(title_id_text)
                .arg(all_exact_matches)
                .arg(entries.size()));
    }

    table->setSortingEnabled(true);
}

void ModCatalogDialog::OpenSelectedSource() {
    const auto selected = table->selectionModel()->selectedRows();
    if (selected.isEmpty()) {
        return;
    }

    const auto* item = table->item(selected.first().row(), 0);
    if (!item) {
        return;
    }

    const int entry_index = item->data(EntryIndexRole).toInt();
    if (entry_index < 0 || entry_index >= entries.size()) {
        return;
    }

    const auto& entry = entries.at(entry_index);
    QString source_path = entry.container.isEmpty() ? entry.source_path : entry.container;
    source_path = RelativeSourcePath(source_path);

    QUrl url{QStringLiteral("https://github.com")};
    url.setPath(QStringLiteral("/%1/blob/%2/%3")
                    .arg(entry.source_repository, entry.source_commit, source_path));

    if (!QDesktopServices::openUrl(url)) {
        QMessageBox::warning(this, tr("Mod Catalog"),
                             tr("Unable to open the mod source in the browser."));
    }
}
