#include "SnapshotSubvolumeDialog.h"
#include "ui_SnapshotSubvolumeDialog.h"

SnapshotSubvolumeDialog::SnapshotSubvolumeDialog(const QString &title, const QString &label, QWidget *parent) :
    QDialog(parent), m_ui(new Ui::SnapshotSubvolumeDialog)
{
    m_ui->setupUi(this);
    m_ui->questionLabel->setText(label);
    this->setWindowTitle(title);
}

SnapshotSubvolumeDialog::~SnapshotSubvolumeDialog()
{
    delete m_ui;
}

void SnapshotSubvolumeDialog::on_pushButton_yes_clicked()
{
    m_backupName = m_ui->backupNameLineEdit->displayText();
    accept();
}

void SnapshotSubvolumeDialog::on_pushButton_no_clicked()
{
    reject();
}

QString SnapshotSubvolumeDialog::backupName()
{
    return m_backupName;
}
