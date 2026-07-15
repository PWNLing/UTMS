#include "LoginDialog.h"

#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QShowEvent>
#include <QSizePolicy>
#include <QVBoxLayout>

#include "core/LoginCredentialValidator.h"

namespace {

constexpr int kInitialWidthPx = 900;
constexpr int kInitialHeightPx = 520;
constexpr int kMinimumWidthPx = 720;
constexpr int kMinimumHeightPx = 420;

} // namespace

LoginDialog::LoginDialog(QWidget *parent)
    : QDialog(parent), logo_icon_(QStringLiteral(":/images/guet_logo.svg")),
      logo_pixmap_(QStringLiteral(":/images/guet_logo.svg"))
{
    setupUi();
}

bool LoginDialog::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == logo_label_ && event->type() == QEvent::Resize) {
        updateLogoPixmap();
    }
    return QDialog::eventFilter(watched, event);
}

void LoginDialog::showEvent(QShowEvent *event)
{
    QDialog::showEvent(event);
    updateLogoPixmap();
    password_line_edit_->setFocus();
}

void LoginDialog::submitCredentials()
{
    const utms::LoginValidationResult result =
        utms::LoginCredentialValidator::validate(account_line_edit_->text(), password_line_edit_->text());
    switch (result) {
    case utms::LoginValidationResult::kAccepted:
        accept();
        return;
    case utms::LoginValidationResult::kMissingCredentials:
        showAuthenticationError(tr("请输入账号和密码"));
        return;
    case utms::LoginValidationResult::kInvalidCredentials:
        showAuthenticationError(tr("账号或密码错误"));
        return;
    }
}

void LoginDialog::setupUi()
{
    setWindowTitle(tr("GUET-UTMS 用户登录"));
    resize(kInitialWidthPx, kInitialHeightPx);
    setMinimumSize(kMinimumWidthPx, kMinimumHeightPx);
    setModal(true);
    setStyleSheet(QStringLiteral("QDialog { background: #ffffff; }"));

    auto *main_layout = new QHBoxLayout(this);
    main_layout->setContentsMargins(0, 0, 0, 0);
    main_layout->setSpacing(0);

    auto *brand_panel = new QFrame(this);
    brand_panel->setObjectName(QStringLiteral("brandPanel"));
    brand_panel->setStyleSheet(QStringLiteral("QFrame#brandPanel { background: #edf5fc; border-right: "
                                              "1px solid #d6e3ef; }"
                                              "QLabel { color: #0a4c8d; background: transparent; }"));
    auto *brand_layout = new QVBoxLayout(brand_panel);
    brand_layout->setContentsMargins(40, 38, 40, 38);
    brand_layout->setSpacing(12);

    logo_label_ = new QLabel(brand_panel);
    logo_label_->setObjectName(QStringLiteral("logoLabel"));
    logo_label_->setAlignment(Qt::AlignCenter);
    logo_label_->setMinimumSize(150, 150);
    logo_label_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    logo_label_->installEventFilter(this);

    auto *university_label = new QLabel(tr("桂林电子科技大学"), brand_panel);
    university_label->setAlignment(Qt::AlignCenter);
    university_label->setStyleSheet(QStringLiteral("font-size: 24px; font-weight: 700;"));

    auto *platform_label = new QLabel(tr("GUET-UTMS 雷达显控平台"), brand_panel);
    platform_label->setAlignment(Qt::AlignCenter);
    platform_label->setStyleSheet(QStringLiteral("font-size: 16px; color: #436785;"));

    brand_layout->addWidget(logo_label_, 1);
    brand_layout->addWidget(university_label);
    brand_layout->addWidget(platform_label);

    auto *form_panel = new QFrame(this);
    form_panel->setObjectName(QStringLiteral("formPanel"));
    auto *form_panel_layout = new QVBoxLayout(form_panel);
    form_panel_layout->setContentsMargins(60, 48, 60, 48);
    form_panel_layout->addStretch();

    auto *form_container = new QFrame(form_panel);
    form_container->setMaximumWidth(400);
    form_container->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    auto *form_layout = new QVBoxLayout(form_container);
    form_layout->setContentsMargins(0, 0, 0, 0);
    form_layout->setSpacing(12);

    auto *title_label = new QLabel(tr("用户登录"), form_container);
    title_label->setStyleSheet(QStringLiteral("font-size: 30px; font-weight: 700; color: #17324d;"));
    auto *subtitle_label = new QLabel(tr("请输入演示账号和密码进入系统"), form_container);
    subtitle_label->setStyleSheet(QStringLiteral("font-size: 14px; color: #6b7c8d;"));

    auto *account_label = new QLabel(tr("账号"), form_container);
    account_label->setStyleSheet(QStringLiteral("font-size: 14px; color: #334e68;"));
    account_line_edit_ = new QLineEdit(form_container);
    account_line_edit_->setObjectName(QStringLiteral("accountLineEdit"));
    account_line_edit_->setText(QStringLiteral("root"));
    account_line_edit_->setClearButtonEnabled(true);
    account_line_edit_->setMinimumHeight(42);
    account_line_edit_->setStyleSheet(QStringLiteral("QLineEdit { border: 1px solid #b8c8d8; border-radius: "
                                                     "6px; padding: 0 12px; font-size: 15px; }"
                                                     "QLineEdit:focus { border: 2px solid #0a65ad; }"));

    auto *password_label = new QLabel(tr("密码"), form_container);
    password_label->setStyleSheet(QStringLiteral("font-size: 14px; color: #334e68;"));
    password_line_edit_ = new QLineEdit(form_container);
    password_line_edit_->setObjectName(QStringLiteral("passwordLineEdit"));
    password_line_edit_->setEchoMode(QLineEdit::Password);
    // password_line_edit_->setText(QStringLiteral("123456"));
    password_line_edit_->setMinimumHeight(42);
    password_line_edit_->setStyleSheet(account_line_edit_->styleSheet());

    error_label_ = new QLabel(form_container);
    error_label_->setObjectName(QStringLiteral("errorLabel"));
    error_label_->setMinimumHeight(22);
    error_label_->setStyleSheet(QStringLiteral("font-size: 14px; color: #c0392b;"));

    login_button_ = new QPushButton(tr("登录"), form_container);
    login_button_->setObjectName(QStringLiteral("loginButton"));
    login_button_->setMinimumHeight(44);
    login_button_->setDefault(true);
    login_button_->setAutoDefault(true);
    login_button_->setCursor(Qt::PointingHandCursor);
    login_button_->setStyleSheet(QStringLiteral("QPushButton { background: #0a65ad; border: none; "
                                                "border-radius: 6px; color: white; font-size: 16px; "
                                                "font-weight: 600; }"
                                                "QPushButton:hover { background: #08558f; }"
                                                "QPushButton:pressed { background: #074775; }"));

    form_layout->addWidget(title_label);
    form_layout->addWidget(subtitle_label);
    form_layout->addSpacing(18);
    form_layout->addWidget(account_label);
    form_layout->addWidget(account_line_edit_);
    form_layout->addSpacing(4);
    form_layout->addWidget(password_label);
    form_layout->addWidget(password_line_edit_);
    form_layout->addWidget(error_label_);
    form_layout->addWidget(login_button_);

    form_panel_layout->addWidget(form_container, 0, Qt::AlignHCenter);
    form_panel_layout->addStretch();

    main_layout->addWidget(brand_panel, 45);
    main_layout->addWidget(form_panel, 55);

    setTabOrder(account_line_edit_, password_line_edit_);
    setTabOrder(password_line_edit_, login_button_);
    connect(login_button_, &QPushButton::clicked, this, &LoginDialog::submitCredentials);
    connect(account_line_edit_, &QLineEdit::returnPressed, this, &LoginDialog::submitCredentials);
    connect(password_line_edit_, &QLineEdit::returnPressed, this, &LoginDialog::submitCredentials);
}

void LoginDialog::updateLogoPixmap()
{
    if (logo_icon_.isNull() || logo_pixmap_.isNull() || logo_label_ == nullptr || logo_label_->size().isEmpty()) {
        return;
    }
    const QSize logo_size_px = logo_pixmap_.size().scaled(logo_label_->size(), Qt::KeepAspectRatio);
    const qreal device_pixel_ratio = logo_label_->devicePixelRatioF();
    QPixmap rendered_logo = logo_icon_.pixmap(logo_size_px, device_pixel_ratio);
    rendered_logo.setDevicePixelRatio(device_pixel_ratio);
    logo_label_->setPixmap(rendered_logo);
}

void LoginDialog::showAuthenticationError(const QString &message)
{
    error_label_->setText(message);
    password_line_edit_->clear();
    password_line_edit_->setFocus();
}
