#include "SnapshotSubvolumeDialog.h"
#include "ui_SnapshotSubvolumeDialog.h"

SnapshotSubvolumeDialog::SnapshotSubvolumeDialog(const QString &title, const QString &label, QWidget *parent) :
    QDialog(parent), s_ui(new Ui::SnapshotSubvolumeDialog)
{
    s_ui->setupUi(this);
    s_ui->questionLabel->setText(label);
    this->setWindowTitle(title);
}

SnapshotSubvolumeDialog::~SnapshotSubvolumeDialog()
{
    delete s_ui;
}

void SnapshotSubvolumeDialog::on_pushButton_yes_clicked()
{
    s_backupName = s_ui->backupNameLineEdit->displayText();
    s_isConfirmed = true;
    s_isClicked = true;
}

void SnapshotSubvolumeDialog::on_pushButton_no_clicked()
{
    s_isConfirmed = false;
    s_isClicked = true;
}

QString SnapshotSubvolumeDialog::getBackupInputText()
{
    return s_backupName;
}

void SnapshotSubvolumeDialog::showDialog()
{
    this->show();
    s_isClicked = false;
    while (!s_isClicked) {
        QCoreApplication::processEvents();
    }
}

bool SnapshotSubvolumeDialog::isComfirmed()
{
    return s_isConfirmed;
}
