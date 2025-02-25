#ifndef TAGLABEL_H
#define TAGLABEL_H

#include <QFrame>

class TagLabel : public QFrame {
    Q_OBJECT
public:
    TagLabel(const QString& tag, QWidget* parent = nullptr);
    QString tag() const { return m_tag; }

signals:
    void removed();

private:
    QString m_tag;
    QHBoxLayout* m_layout;
    QLabel* m_label;
    QPushButton* m_removeButton;
};

#endif // TAGLABEL_H