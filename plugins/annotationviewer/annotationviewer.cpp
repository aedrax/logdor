#include "annotationviewer.h"

#include "../../app/src/annotationdialog.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QPushButton>
#include <QVBoxLayout>

namespace {

constexpr int kIdRole = Qt::UserRole;
constexpr int kStartRole = Qt::UserRole + 1;
constexpr int kEndRole = Qt::UserRole + 2;

} // namespace

AnnotationViewer::AnnotationViewer(QObject* parent)
    : PluginInterface(parent)
    , m_container(new QWidget())
    , m_tree(new QTreeWidget())
    , m_summary(new QLabel())
{
    auto* layout = new QVBoxLayout(m_container);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);

    auto* header = new QWidget();
    auto* headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(4, 2, 4, 2);
    headerLayout->addWidget(m_summary);
    headerLayout->addStretch();
    auto* reanchorButton = new QPushButton(tr("Re-anchor"));
    reanchorButton->setToolTip(
        tr("Search the file again for notes whose lines moved"));
    connect(reanchorButton, &QPushButton::clicked, this, [this]() {
        if (m_hub)
            m_hub->startReanchor();
    });
    headerLayout->addWidget(reanchorButton);
    layout->addWidget(header);

    m_tree->setColumnCount(4);
    m_tree->setHeaderLabels({ tr("Line(s)"), tr("Note"), tr("Author"),
                              tr("Modified") });
    m_tree->setRootIsDecorated(false);
    m_tree->setAlternatingRowColors(true);
    m_tree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_tree->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    layout->addWidget(m_tree);

    connect(m_tree, &QTreeWidget::itemClicked, this,
            [this](QTreeWidgetItem* item, int) { onItemClicked(item); });
    connect(m_tree, &QTreeWidget::itemActivated, this,
            [this](QTreeWidgetItem* item, int) { onItemActivated(item); });

    m_tree->setContextMenuPolicy(Qt::ActionsContextMenu);
    auto* editAction = new QAction(tr("Edit note..."), m_tree);
    connect(editAction, &QAction::triggered, this, [this]() {
        if (auto* item = m_tree->currentItem())
            editAnnotation(QUuid(item->data(0, kIdRole).toString()));
    });
    auto* removeAction = new QAction(tr("Remove note"), m_tree);
    removeAction->setShortcut(QKeySequence::Delete);
    removeAction->setShortcutContext(Qt::WidgetShortcut);
    connect(removeAction, &QAction::triggered, this,
            &AnnotationViewer::removeSelected);
    m_tree->addAction(editAction);
    m_tree->addAction(removeAction);

    m_summary->setText(tr("No notes"));
}

AnnotationViewer::~AnnotationViewer()
{
    delete m_container;
}

void AnnotationViewer::setAnnotationHub(AnnotationHub* hub)
{
    m_hub = hub;
    connect(m_hub, &AnnotationHub::annotationsChanged, this,
            &AnnotationViewer::rebuild);
    connect(m_hub, &AnnotationHub::reanchorFinished, this,
            [this](int, int reanchored, int orphaned) {
                m_summary->setText(
                    tr("%1 notes (%2 re-anchored, %3 orphaned)")
                        .arg(m_hub->set().size())
                        .arg(reanchored)
                        .arg(orphaned));
            });
    rebuild();
}

void AnnotationViewer::rebuild()
{
    m_tree->clear();
    if (!m_hub)
        return;

    int orphaned = 0;
    for (const logdor::Annotation& annotation : m_hub->set().annotations()) {
        auto* item = new QTreeWidgetItem(m_tree);
        const QString lines = annotation.startLine == annotation.endLine
            ? QString::number(annotation.startLine + 1)
            : QStringLiteral("%1-%2").arg(annotation.startLine + 1)
                  .arg(annotation.endLine + 1);
        item->setText(0, lines);
        item->setText(1, annotation.note.simplified());
        item->setText(2, annotation.author);
        item->setText(3, annotation.modifiedAt.toLocalTime().toString(
                             QStringLiteral("yyyy-MM-dd hh:mm")));
        item->setToolTip(1, annotation.note + QStringLiteral("\n\n")
                                + annotation.snippet);
        item->setData(0, kIdRole, annotation.id.toString());
        item->setData(0, kStartRole, qlonglong(annotation.startLine));
        item->setData(0, kEndRole, qlonglong(annotation.endLine));

        if (!annotation.color.isEmpty()) {
            QPixmap swatch(10, 10);
            swatch.fill(QColor(annotation.color));
            item->setIcon(0, QIcon(swatch));
        }
        if (annotation.orphaned) {
            ++orphaned;
            QFont italic = item->font(1);
            italic.setItalic(true);
            for (int column = 0; column < 4; ++column) {
                item->setFont(column, italic);
                item->setForeground(column, QBrush(Qt::gray));
            }
            item->setText(0, tr("? %1").arg(lines));
            item->setToolTip(0,
                tr("Orphaned: this line was not found in the current file. "
                   "Use Re-anchor to search again."));
        }
    }

    const auto count = m_hub->set().size();
    m_summary->setText(count == 0
        ? tr("No notes")
        : orphaned > 0 ? tr("%1 notes, %2 orphaned").arg(count).arg(orphaned)
                       : tr("%1 notes").arg(count));
}

void AnnotationViewer::onItemClicked(QTreeWidgetItem* item)
{
    if (!item)
        return;
    const qint64 start = item->data(0, kStartRole).toLongLong();
    const qint64 end = item->data(0, kEndRole).toLongLong();
    QList<int> lines;
    for (qint64 line = start; line <= end && lines.size() < 10000; ++line)
        lines.append(int(line));
    emit pluginEvent(PluginEvent::LinesSelected, QVariant::fromValue(lines));
}

void AnnotationViewer::onItemActivated(QTreeWidgetItem* item)
{
    if (item)
        editAnnotation(QUuid(item->data(0, kIdRole).toString()));
}

void AnnotationViewer::editAnnotation(const QUuid& id)
{
    if (!m_hub)
        return;
    const logdor::Annotation* found = m_hub->set().find(id);
    if (!found)
        return;
    logdor::Annotation annotation = *found;

    AnnotationDialog dialog(m_container);
    dialog.setNote(annotation.note);
    dialog.setColor(annotation.color);
    dialog.setTag(annotation.tag);
    if (dialog.exec() != QDialog::Accepted)
        return;
    annotation.note = dialog.note();
    annotation.color = dialog.color();
    annotation.tag = dialog.tag();
    m_hub->updateAnnotation(std::move(annotation));
}

void AnnotationViewer::removeSelected()
{
    if (!m_hub)
        return;
    const auto items = m_tree->selectedItems();
    for (QTreeWidgetItem* item : items)
        m_hub->removeAnnotation(QUuid(item->data(0, kIdRole).toString()));
}
