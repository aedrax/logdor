#include "annotationdialog.h"

#include <QButtonGroup>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>

QStringList AnnotationDialog::presetColors()
{
    return { QString(), // no color
             QStringLiteral("#ffd54f"), QStringLiteral("#aed581"),
             QStringLiteral("#4fc3f7"), QStringLiteral("#f48fb1"),
             QStringLiteral("#ff8a65"), QStringLiteral("#b39ddb") };
}

AnnotationDialog::AnnotationDialog(QWidget* parent)
    : QDialog(parent)
    , m_noteEdit(new QPlainTextEdit(this))
    , m_colorGroup(new QButtonGroup(this))
    , m_tagEdit(new QLineEdit(this))
{
    setWindowTitle(tr("Annotation"));
    setMinimumWidth(420);

    auto* layout = new QFormLayout(this);
    m_noteEdit->setPlaceholderText(tr("Write a note..."));
    m_noteEdit->setTabChangesFocus(true);
    m_noteEdit->setMinimumHeight(90);
    layout->addRow(tr("Note:"), m_noteEdit);

    auto* colorRow = new QWidget(this);
    auto* colorLayout = new QHBoxLayout(colorRow);
    colorLayout->setContentsMargins(0, 0, 0, 0);
    m_colorGroup->setExclusive(true);
    const QStringList presets = presetColors();
    for (int i = 0; i < presets.size(); ++i) {
        auto* swatch = new QPushButton(colorRow);
        swatch->setCheckable(true);
        swatch->setFixedSize(28, 22);
        if (presets[i].isEmpty()) {
            swatch->setText(QStringLiteral("—"));
            swatch->setToolTip(tr("No color"));
            swatch->setChecked(true);
        } else {
            swatch->setStyleSheet(
                QStringLiteral("QPushButton { background-color: %1; }"
                               "QPushButton:checked { border: 2px solid black; }")
                    .arg(presets[i]));
            swatch->setToolTip(presets[i]);
        }
        m_colorGroup->addButton(swatch, i);
        colorLayout->addWidget(swatch);
    }
    colorLayout->addStretch();
    layout->addRow(tr("Color:"), colorRow);

    m_tagEdit->setPlaceholderText(tr("Optional tag, e.g. triage"));
    layout->addRow(tr("Tag:"), m_tagEdit);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addRow(buttons);

    m_noteEdit->setFocus();
}

void AnnotationDialog::setNote(const QString& note)
{
    m_noteEdit->setPlainText(note);
}

QString AnnotationDialog::note() const
{
    return m_noteEdit->toPlainText();
}

void AnnotationDialog::setColor(const QString& color)
{
    const int index = presetColors().indexOf(color);
    if (auto* button = m_colorGroup->button(qMax(0, index)))
        button->setChecked(true);
}

QString AnnotationDialog::color() const
{
    const int id = m_colorGroup->checkedId();
    return id > 0 ? presetColors().at(id) : QString();
}

void AnnotationDialog::setTag(const QString& tag)
{
    m_tagEdit->setText(tag);
}

QString AnnotationDialog::tag() const
{
    return m_tagEdit->text().trimmed();
}
