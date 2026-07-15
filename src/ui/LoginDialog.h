#pragma once

#include <QDialog>
#include <QIcon>
#include <QPixmap>

class QEvent;
class QLabel;
class QLineEdit;
class QObject;
class QPushButton;
class QShowEvent;

class LoginDialog : public QDialog {
    Q_OBJECT

public:
    explicit LoginDialog(QWidget *parent = nullptr);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void showEvent(QShowEvent *event) override;

private slots:
    void submitCredentials();

private:
    void setupUi();
    void updateLogoPixmap();
    void showAuthenticationError(const QString &message);

    QLabel *logo_label_ = nullptr;
    QLineEdit *account_line_edit_ = nullptr;
    QLineEdit *password_line_edit_ = nullptr;
    QPushButton *login_button_ = nullptr;
    QLabel *error_label_ = nullptr;
    QIcon logo_icon_;
    QPixmap logo_pixmap_;
};
