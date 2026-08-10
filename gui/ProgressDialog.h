#pragma once

#include <QDialog>

class QDialogButtonBox;
class QLabel;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QToolButton;

namespace kh::gui {

class ProgressDialog : public QDialog {
    Q_OBJECT

public:
    explicit ProgressDialog(const QString &title, QWidget *parent = nullptr);

    void setProgress(int percent);
    void setStatusText(const QString &text);
    void appendDetail(const QString &line);
    void setCancellationEnabled(bool enabled);
    void finish(bool success, const QString &message);

signals:
    void cancellationRequested();

private slots:
    void toggleDetails(bool checked);

private:
    QProgressBar *progress_bar_ = nullptr;
    QLabel *status_label_ = nullptr;
    QToolButton *details_button_ = nullptr;
    QPlainTextEdit *details_log_ = nullptr;
    QPushButton *cancel_button_ = nullptr;
    QPushButton *close_button_ = nullptr;
};

}  // namespace kh::gui
