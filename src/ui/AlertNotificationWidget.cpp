#include "ui/AlertNotificationWidget.h"

#include <QLabel>
#include <QTimer>
#include <QVBoxLayout>

namespace utms {

AlertNotificationWidget::AlertNotificationWidget(QWidget *parent)
    : QFrame(parent), title_label_(new QLabel(this)), detail_label_(new QLabel(this)), hide_timer_(new QTimer(this)) {
    setObjectName(QStringLiteral("alertNotification"));
    setStyleSheet(QStringLiteral("#alertNotification { background: #fff2f0; border: 2px "
                                 "solid #cf1322; border-radius: 6px; }"
                                 "#alertNotification QLabel { color: #5c0011; background: "
                                 "transparent; }"));
    setMinimumWidth(300);
    setMaximumWidth(420);
    setAttribute(Qt::WA_TransparentForMouseEvents);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(14, 10, 14, 10);
    title_label_->setStyleSheet(QStringLiteral("font-weight: 700; font-size: 14px;"));
    detail_label_->setWordWrap(true);
    layout->addWidget(title_label_);
    layout->addWidget(detail_label_);

    hide_timer_->setSingleShot(true);
    hide_timer_->setInterval(5'000);
    connect(hide_timer_, &QTimer::timeout, this, [this]() {
        hide();
        emit notificationFinished();
    });
    hide();
}

void AlertNotificationWidget::showNotification(const QString &title, const QString &detail) {
    title_label_->setText(title);
    detail_label_->setText(detail);
    adjustSize();
    show();
    hide_timer_->start();
}

} // namespace utms
