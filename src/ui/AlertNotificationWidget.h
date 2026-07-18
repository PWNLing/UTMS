#pragma once

#include <QFrame>

class QLabel;
class QTimer;

namespace utms {

class AlertNotificationWidget : public QFrame {
    Q_OBJECT

  public:
    explicit AlertNotificationWidget(QWidget *parent);

    void showNotification(const QString &title, const QString &detail);

  signals:
    void notificationFinished();

  private:
    QLabel *title_label_ = nullptr;
    QLabel *detail_label_ = nullptr;
    QTimer *hide_timer_ = nullptr;
};

} // namespace utms
