#pragma once

#include <QWidget>

class QLineEdit;
class QTimer;

namespace kh::gui::widgets {

// Toolbar filter input: search icon, built-in clear button, and a 300ms
// debounce so typing doesn't re-filter the kernel table on every keystroke.
class SearchFilter : public QWidget {
    Q_OBJECT

public:
    explicit SearchFilter(QWidget *parent = nullptr);

    QString text() const;
    void setPlaceholderText(const QString &text);
    void clear();

signals:
    // Emitted 300ms after the user stops typing (or immediately on clear()).
    void filterChanged(const QString &text);

private:
    QLineEdit *edit_ = nullptr;
    QTimer *debounce_timer_ = nullptr;
};

}  // namespace kh::gui::widgets
