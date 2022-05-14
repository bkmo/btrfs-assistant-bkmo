#include "SnapshotSubvolumeDialog.h"
#include "ui_SnapshotSubvolumeDialog.h"

#include <QMessageBox>

SnapshotSubvolumeDialog::SnapshotSubvolumeDialog(QWidget *parent) : QDialog(parent), ui(new Ui::SnapshotSubvolumeDialog)
{
    ui->setupUi(this);

    disconnect(ui->buttonBox, &QDialogButtonBox::accepted, nullptr, nullptr);

    connect(ui->buttonBox, &QDialogButtonBox::accepted, this, [this]() {
        if (ui->destinationLineEdit->text().trimmed().isEmpty()) {
            QMessageBox::warning(this, tr("Btrfs Assistant"), tr("The destination path cannot be empty"));
            ui->destinationLineEdit->setFocus();
        } else {
            accept();
        }
    });
}

SnapshotSubvolumeDialog::~SnapshotSubvolumeDialog() { delete ui; }

QString SnapshotSubvolumeDialog::destination() const { return ui->destinationLineEdit->text().trimmed(); }

void SnapshotSubvolumeDialog::setDestination(const QString &text) { ui->destinationLineEdit->setText(text); }

bool SnapshotSubvolumeDialog::isReadOnly() const { return ui->readOnlyCheckBox->isChecked(); }

void SnapshotSubvolumeDialog::setReadOnly(bool value) { ui->readOnlyCheckBox->setChecked(value); }

void SnapshotSubvolumeDialog::selectAllTextAndSetFocus()
{
    ui->destinationLineEdit->selectAll();
    ui->destinationLineEdit->setFocus();
}
