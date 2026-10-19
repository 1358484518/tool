#include "EvlinkLoginDialog.h"

#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

EvlinkLoginDialog::EvlinkLoginDialog(QWidget *parent)
    : QDialog(parent)
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(16, 16, 16, 16);
    root->setSpacing(8);

    auto *emailRow = new QHBoxLayout();
    m_emailLabel = new QLabel(this);
    m_emailEdit = new QLineEdit(this);
    emailRow->addWidget(m_emailLabel);
    emailRow->addWidget(m_emailEdit, 1);
    root->addLayout(emailRow);

    auto *passwordRow = new QHBoxLayout();
    m_passwordLabel = new QLabel(this);
    m_passwordEdit = new QLineEdit(this);
    m_passwordEdit->setEchoMode(QLineEdit::Password);
    passwordRow->addWidget(m_passwordLabel);
    passwordRow->addWidget(m_passwordEdit, 1);
    root->addLayout(passwordRow);

    m_status = new QLabel(this);
    m_status->setWordWrap(true);
    root->addWidget(m_status);

    auto *buttons = new QHBoxLayout();
    buttons->addStretch();
    m_signIn = new QPushButton(this);
    m_signIn->setDefault(true);
    m_cancel = new QPushButton(this);
    buttons->addWidget(m_signIn);
    buttons->addWidget(m_cancel);
    root->addLayout(buttons);

    connect(m_signIn, &QPushButton::clicked, this, &EvlinkLoginDialog::submit);
    connect(m_cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_passwordEdit, &QLineEdit::returnPressed, this, &EvlinkLoginDialog::submit);
    connect(m_emailEdit, &QLineEdit::returnPressed, this, [this]() { m_passwordEdit->setFocus(); });
    retranslateUi();
    setMinimumWidth(360);
}

void EvlinkLoginDialog::setAccount(const QString &email, const QString &password)
{
    m_emailEdit->setText(email);
    m_passwordEdit->setText(password);
}

QString EvlinkLoginDialog::email() const
{
    return m_emailEdit->text().trimmed();
}

QString EvlinkLoginDialog::password() const
{
    return m_passwordEdit->text();
}

void EvlinkLoginDialog::showFailure(const QString &message)
{
    setBusy(false);
    m_status->setText(message.isEmpty() ? tr("Sign-in failed: %1").arg(tr("No token")) : message);
}

void EvlinkLoginDialog::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::LanguageChange)
        retranslateUi();
    QDialog::changeEvent(event);
}

void EvlinkLoginDialog::retranslateUi()
{
    setWindowTitle(tr("Sign in to evlink"));
    m_emailLabel->setText(tr("Email"));
    m_passwordLabel->setText(tr("Password:"));
    m_signIn->setText(tr("Sign in"));
    m_cancel->setText(tr("Cancel"));
}

void EvlinkLoginDialog::submit()
{
    if (m_busy)
        return;
    if (email().isEmpty() || password().isEmpty()) {
        m_status->setText(tr("Sign in to evlink first"));
        return;
    }
    setBusy(true);
    m_status->setText(tr("Signing in..."));
    emit signInRequested(email(), password());
}

void EvlinkLoginDialog::setBusy(bool busy)
{
    m_busy = busy;
    m_emailEdit->setEnabled(!busy);
    m_passwordEdit->setEnabled(!busy);
    m_signIn->setEnabled(!busy);
}
