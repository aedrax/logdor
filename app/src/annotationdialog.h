#ifndef ANNOTATIONDIALOG_H
#define ANNOTATIONDIALOG_H

#include "logdorexport.h"

#include <QDialog>

class QButtonGroup;
class QLineEdit;
class QPlainTextEdit;

/**
 * Modal editor for one annotation: note text, a small preset color row,
 * and an optional tag. Shared by the log viewers and the Annotations panel.
 */
class LOGDOR_INTERFACE_EXPORT AnnotationDialog : public QDialog {
    Q_OBJECT
public:
    explicit AnnotationDialog(QWidget* parent = nullptr);

    void setNote(const QString& note);
    QString note() const;
    void setColor(const QString& color); // "#RRGGBB" or empty
    QString color() const;
    void setTag(const QString& tag);
    QString tag() const;

    /// Preset swatches offered in the dialog (first entry = no color).
    static QStringList presetColors();

private:
    QPlainTextEdit* m_noteEdit;
    QButtonGroup* m_colorGroup;
    QLineEdit* m_tagEdit;
};

#endif // ANNOTATIONDIALOG_H
