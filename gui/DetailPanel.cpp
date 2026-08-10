#include "gui/DetailPanel.h"

#include <QFocusEvent>
#include <QFormLayout>
#include <QLabel>
#include <QListWidget>
#include <QTabWidget>
#include <QVBoxLayout>

#include "core/model/SourceId.h"
#include "gui/models/KernelTableModel.h"
#include "gui/widgets/SourceBadge.h"
#include "gui/widgets/StatusBadge.h"

namespace kh::gui {

NoteTextEdit::NoteTextEdit(QWidget *parent) : QTextEdit(parent) {}

void NoteTextEdit::focusOutEvent(QFocusEvent *event) {
    QTextEdit::focusOutEvent(event);
    emit editingFinished();
}

DetailPanel::DetailPanel(kh::state::StateStore *state_store, QWidget *parent)
    : QDockWidget(QStringLiteral("Kernel Details"), parent), state_store_(state_store) {
    setObjectName(QStringLiteral("kernelDetailPanel"));
    setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);

    auto *content = new QWidget(this);
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(10);
    layout->addWidget(createHeader());

    auto *tabs = new QTabWidget(content);
    tabs->addTab(createChangelogTab(), QStringLiteral("Changelog"));
    tabs->addTab(createFilesTab(), QStringLiteral("Files"));
    tabs->addTab(createDependenciesTab(), QStringLiteral("Dependencies"));
    tabs->addTab(createNotesTab(), QStringLiteral("Notes"));
    layout->addWidget(tabs, 1);

    setWidget(content);
    setKernel({});
}

void DetailPanel::setKernel(const kh::model::KernelInfo &kernel) {
    kernel_ = kernel;
    updateHeader();
    updateTabs();
}

kh::model::KernelInfo DetailPanel::kernel() const {
    return kernel_;
}

void DetailPanel::saveNote() {
    if (loading_note_ || kernel_.version.isEmpty() || notes_edit_ == nullptr) {
        return;
    }
    const QString note = notes_edit_->toPlainText();
    kernel_.notes = note;
    if (state_store_ != nullptr && state_store_->isOpen()) {
        state_store_->setNote(kernel_.version, note);
    }
    emit noteSaved(kernel_.version, note);
}

QWidget *DetailPanel::createHeader() {
    auto *header = new QWidget(this);
    auto *layout = new QFormLayout(header);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setLabelAlignment(Qt::AlignRight);

    source_badge_ = new widgets::SourceBadge(header);
    version_label_ = new QLabel(header);
    version_label_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    release_label_ = new QLabel(header);
    status_badge_ = new widgets::StatusBadge(header);

    layout->addRow(QStringLiteral("Source:"), source_badge_);
    layout->addRow(QStringLiteral("Version:"), version_label_);
    layout->addRow(QStringLiteral("Released:"), release_label_);
    layout->addRow(QStringLiteral("Status:"), status_badge_);
    return header;
}

QWidget *DetailPanel::createChangelogTab() {
    changelog_text_ = new QTextEdit(this);
    changelog_text_->setReadOnly(true);
    changelog_text_->setLineWrapMode(QTextEdit::WidgetWidth);
    return changelog_text_;
}

QWidget *DetailPanel::createFilesTab() {
    files_list_ = new QListWidget(this);
    files_list_->setSelectionMode(QAbstractItemView::NoSelection);
    return files_list_;
}

QWidget *DetailPanel::createDependenciesTab() {
    dependencies_text_ = new QTextEdit(this);
    dependencies_text_->setReadOnly(true);
    dependencies_text_->setLineWrapMode(QTextEdit::WidgetWidth);
    return dependencies_text_;
}

QWidget *DetailPanel::createNotesTab() {
    notes_edit_ = new NoteTextEdit(this);
    notes_edit_->setAcceptRichText(false);
    notes_edit_->setPlaceholderText(QStringLiteral("Add notes for this kernel version."));
    connect(notes_edit_, &NoteTextEdit::editingFinished, this, &DetailPanel::saveNote);
    return notes_edit_;
}

void DetailPanel::updateHeader() {
    source_badge_->setSource(kernel_.sourceId, kernel_.sourceDisplayName);
    version_label_->setText(kernel_.version.isEmpty() ? QStringLiteral("No kernel selected")
                                                      : kernel_.version);
    release_label_->setText(kernel_.releaseDate.isValid()
                                ? kernel_.releaseDate.toLocalTime().toString(QStringLiteral("yyyy-MM-dd hh:mm"))
                                : QStringLiteral("Unknown"));
    status_badge_->setStatus(kernel_.status);
    status_badge_->setPinned(kernel_.isPinned);
}

void DetailPanel::updateTabs() {
    changelog_text_->setPlainText(kernel_.changelog.isEmpty() ? fallbackChangelogText()
                                                              : kernel_.changelog);

    files_list_->clear();
    if (kernel_.subPackages.isEmpty()) {
        files_list_->addItem(QStringLiteral("No installed file manifest is available yet."));
    } else {
        for (const QString &package_name : kernel_.subPackages) {
            files_list_->addItem(package_name);
        }
    }

    QString dependencies = QStringLiteral("Dependency information is not loaded yet.");
    if (!kernel_.subPackages.isEmpty()) {
        dependencies = QStringLiteral("Packages in this kernel set:\n\n%1")
                           .arg(kernel_.subPackages.join(QStringLiteral("\n")));
    }
    dependencies_text_->setPlainText(dependencies);

    loading_note_ = true;
    const QString stored_note =
        state_store_ != nullptr && state_store_->isOpen() && !kernel_.version.isEmpty()
            ? state_store_->note(kernel_.version)
            : kernel_.notes;
    notes_edit_->setPlainText(stored_note);
    loading_note_ = false;
}

QString DetailPanel::fallbackChangelogText() const {
    if (kernel_.version.isEmpty()) {
        return QStringLiteral("Select a kernel to view details.");
    }
    return QStringLiteral(
        "No changelog is available for this build yet. When the package backend "
        "provides RPM changelog data, it will appear here.");
}

}  // namespace kh::gui
