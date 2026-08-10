#pragma once

#include <QDockWidget>
#include <QTextEdit>

#include "core/model/KernelInfo.h"
#include "core/state/StateStore.h"

class QLabel;
class QListWidget;
class QTabWidget;

namespace kh::gui {

namespace widgets {
class SourceBadge;
class StatusBadge;
}  // namespace widgets

class NoteTextEdit : public QTextEdit {
    Q_OBJECT

public:
    explicit NoteTextEdit(QWidget *parent = nullptr);

signals:
    void editingFinished();

protected:
    void focusOutEvent(QFocusEvent *event) override;
};

class DetailPanel : public QDockWidget {
    Q_OBJECT

public:
    explicit DetailPanel(kh::state::StateStore *state_store, QWidget *parent = nullptr);

    void setKernel(const kh::model::KernelInfo &kernel);
    kh::model::KernelInfo kernel() const;

signals:
    void noteSaved(QString version, QString note);

private slots:
    void saveNote();

private:
    QWidget *createHeader();
    QWidget *createChangelogTab();
    QWidget *createFilesTab();
    QWidget *createDependenciesTab();
    QWidget *createNotesTab();
    void updateHeader();
    void updateTabs();
    QString fallbackChangelogText() const;

    kh::state::StateStore *state_store_ = nullptr;
    kh::model::KernelInfo kernel_;
    widgets::SourceBadge *source_badge_ = nullptr;
    QLabel *version_label_ = nullptr;
    QLabel *release_label_ = nullptr;
    widgets::StatusBadge *status_badge_ = nullptr;
    QTextEdit *changelog_text_ = nullptr;
    QListWidget *files_list_ = nullptr;
    QTextEdit *dependencies_text_ = nullptr;
    NoteTextEdit *notes_edit_ = nullptr;
    bool loading_note_ = false;
};

}  // namespace kh::gui
