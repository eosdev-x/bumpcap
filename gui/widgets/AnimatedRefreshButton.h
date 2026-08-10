#pragma once

#include <QToolButton>

class QPropertyAnimation;

namespace kh::gui::widgets {

// QToolButton whose icon spins continuously while a refresh/long-running
// operation is in progress. Exposes a "rotation" Q_PROPERTY so it can be
// driven by gui::theme::animations::spin().
class AnimatedRefreshButton : public QToolButton {
    Q_OBJECT
    Q_PROPERTY(qreal rotation READ rotation WRITE setRotation)

public:
    explicit AnimatedRefreshButton(QWidget *parent = nullptr);

    void startSpinning();
    void stopSpinning();
    bool isSpinning() const;

    qreal rotation() const;
    void setRotation(qreal degrees);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    qreal rotation_ = 0.0;
    QPropertyAnimation *animation_ = nullptr;
};

}  // namespace kh::gui::widgets
