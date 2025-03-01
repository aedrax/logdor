#include "pgnviewer.h"
#include <QHeaderView>
#include <QSet>
#include <QKeyEvent>
#include <algorithm>
#include <QtConcurrent/QtConcurrent>
#include <numeric>

PgnViewer::PgnViewer(QObject* parent)
    : PluginInterface(parent)
    , m_container(new QWidget())
    , m_layout(new QVBoxLayout(m_container))
    , m_toolbar(new QToolBar())
    , m_splitter(new QSplitter(Qt::Horizontal))
    , m_tableView(new QTableView())
    , m_model(new PgnTableModel(this))
    , m_board(new ChessboardWidget())
{
    // Setup navigation buttons
    m_firstButton = new QPushButton(tr("⏮"));
    m_prevButton = new QPushButton(tr("⏪"));
    m_nextButton = new QPushButton(tr("⏩"));
    m_lastButton = new QPushButton(tr("⏭"));
    
    m_firstButton->setToolTip(tr("First Move (Home)"));
    m_prevButton->setToolTip(tr("Previous Move (Left)"));
    m_nextButton->setToolTip(tr("Next Move (Right)"));
    m_lastButton->setToolTip(tr("Last Move (End)"));
    
    m_toolbar->addWidget(m_firstButton);
    m_toolbar->addWidget(m_prevButton);
    m_toolbar->addWidget(m_nextButton);
    m_toolbar->addWidget(m_lastButton);
    
    // Setup table view
    m_tableView->setModel(m_model);
    m_tableView->horizontalHeader()->setSectionResizeMode(PgnColumn::MoveNumber, QHeaderView::ResizeToContents);
    m_tableView->horizontalHeader()->setSectionResizeMode(PgnColumn::WhiteMove, QHeaderView::Stretch);
    m_tableView->horizontalHeader()->setSectionResizeMode(PgnColumn::BlackMove, QHeaderView::Stretch);
    m_tableView->verticalHeader()->hide();
    m_tableView->setAlternatingRowColors(true);
    m_tableView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tableView->setSelectionMode(QAbstractItemView::SingleSelection);
    
    // Setup splitter
    m_splitter->addWidget(m_tableView);
    m_splitter->addWidget(m_board);
    m_splitter->setStretchFactor(0, 1);  // Table view gets 1 stretch
    m_splitter->setStretchFactor(1, 2);  // Board gets 2 stretch
    
    // Setup layout
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->addWidget(m_toolbar);
    m_layout->addWidget(m_splitter);
    
    // Connect navigation buttons
    connect(m_firstButton, &QPushButton::clicked, this, &PgnViewer::firstMove);
    connect(m_prevButton, &QPushButton::clicked, this, &PgnViewer::previousMove);
    connect(m_nextButton, &QPushButton::clicked, this, &PgnViewer::nextMove);
    connect(m_lastButton, &QPushButton::clicked, this, &PgnViewer::lastMove);
    
    // Connect selection changes
    connect(m_tableView->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, &PgnViewer::onSelectionChanged);
    connect(m_tableView->selectionModel(), &QItemSelectionModel::currentRowChanged,
            this, [this](const QModelIndex& current, const QModelIndex&) {
                if (current.isValid()) {
                    updateBoard(current.row());
                }
            });
    
    // Install event filter for keyboard navigation
    m_container->installEventFilter(this);
}

PgnViewer::~PgnViewer()
{
    delete m_container;  // This will delete all child widgets
}

bool PgnViewer::eventFilter(QObject* obj, QEvent* event)
{
    if (obj == m_container && event->type() == QEvent::KeyPress) {
        QKeyEvent* keyEvent = static_cast<QKeyEvent*>(event);
        switch (keyEvent->key()) {
            case Qt::Key_Left:
                previousMove();
                return true;
            case Qt::Key_Right:
                nextMove();
                return true;
            case Qt::Key_Home:
                firstMove();
                return true;
            case Qt::Key_End:
                lastMove();
                return true;
        }
    }
    return QObject::eventFilter(obj, event);
}

void PgnViewer::nextMove()
{
    QModelIndex current = m_tableView->currentIndex();
    if (!current.isValid()) {
        // Select first move if nothing selected
        m_tableView->selectRow(0);
        m_showingBlackMove = false;
        updateBoard(0);
        return;
    }
    
    // If showing white's move, check if black has a move
    if (!m_showingBlackMove) {
        QString blackMove = m_model->data(m_model->index(current.row(), PgnColumn::BlackMove)).toString();
        if (!blackMove.isEmpty()) {
            m_showingBlackMove = true;
            updateBoard(current.row());
            return;
        }
    }
    
    // Move to next row's white move
    int nextRow = current.row() + 1;
    if (nextRow < m_model->rowCount()) {
        m_tableView->selectRow(nextRow);
        m_tableView->scrollTo(m_model->index(nextRow, 0));
        m_showingBlackMove = false;
        updateBoard(nextRow);
    }
}

void PgnViewer::previousMove()
{
    QModelIndex current = m_tableView->currentIndex();
    if (!current.isValid()) {
        return;
    }
    
    // If showing black's move, show white's move
    if (m_showingBlackMove) {
        m_showingBlackMove = false;
        updateBoard(current.row());
        return;
    }
    
    // Move to previous row's black move
    int prevRow = current.row() - 1;
    if (prevRow >= 0) {
        m_tableView->selectRow(prevRow);
        m_tableView->scrollTo(m_model->index(prevRow, 0));
        QString blackMove = m_model->data(m_model->index(prevRow, PgnColumn::BlackMove)).toString();
        if (!blackMove.isEmpty()) {
            m_showingBlackMove = true;
        }
        updateBoard(prevRow);
    }
}

void PgnViewer::firstMove()
{
    if (m_model->rowCount() > 0) {
        m_tableView->selectRow(0);
        m_tableView->scrollTo(m_model->index(0, 0));
        m_showingBlackMove = false;
        updateBoard(0);
    }
}

void PgnViewer::lastMove()
{
    int lastRow = m_model->rowCount() - 1;
    if (lastRow >= 0) {
        m_tableView->selectRow(lastRow);
        m_tableView->scrollTo(m_model->index(lastRow, 0));
        QString blackMove = m_model->data(m_model->index(lastRow, PgnColumn::BlackMove)).toString();
        m_showingBlackMove = !blackMove.isEmpty();
        updateBoard(lastRow);
    }
}

bool PgnViewer::setLogs(const QList<LogEntry>& content)
{
    m_model->setLogEntries(content);
    m_board->resetBoard();  // Reset to starting position
    
    // Select first move
    if (m_model->rowCount() > 0) {
        m_tableView->selectRow(0);
    }
    
    return true;
}

void PgnViewer::setFilter(const FilterOptions& options)
{
    m_model->setFilter(options);
}

QList<FieldInfo> PgnViewer::availableFields() const
{
    return QList<FieldInfo>({
        {tr("Move #"), DataType::Integer},
        {tr("White"), DataType::String},
        {tr("Black"), DataType::String}
    });
}

QSet<int> PgnViewer::filteredLines() const
{
    return QSet<int>();
}

void PgnViewer::synchronizeFilteredLines(const QSet<int>& lines)
{
    Q_UNUSED(lines);
}

void PgnViewer::onSelectionChanged(const QItemSelection& selected, const QItemSelection& deselected)
{
    Q_UNUSED(selected);
    Q_UNUSED(deselected);
    
    QList<int> selectedLines;
    const auto indexes = m_tableView->selectionModel()->selectedRows();
    for (const auto& index : indexes) {
        selectedLines.append(m_model->mapToSourceRow(index.row()));
    }
    
    emit pluginEvent(PluginEvent::LinesSelected, QVariant::fromValue(selectedLines));
}

void PgnViewer::onPluginEvent(PluginEvent event, const QVariant& data)
{
    if (event == PluginEvent::LinesSelected) {
        QList<int> selectedLines = data.value<QList<int>>();
        if (selectedLines.isEmpty()) {
            return;
        }

        // For all selected lines, select them in the table view
        QItemSelection selection;
        for (int line : selectedLines) {
            const int row = m_model->mapFromSourceRow(line);
            selection.select(m_model->index(row, PgnColumn::MoveNumber), 
                           m_model->index(row, PgnColumn::BlackMove));
        }
        m_tableView->selectionModel()->select(selection, QItemSelectionModel::ClearAndSelect);

        // Scroll to the first selected line
        if (!selectedLines.isEmpty()) {
            m_tableView->scrollTo(m_model->index(m_model->mapFromSourceRow(selectedLines.first()), 0));
            updateBoard(m_model->mapFromSourceRow(selectedLines.first()));
        }
    }
}

void PgnViewer::updateBoard(int row)
{
    QString fen = calculateFenForMove(row, m_showingBlackMove);
    m_board->setPosition(fen);
}

QString PgnViewer::calculateFenForMove(int moveRow, bool includeBlackMove)
{
    // Reset game to starting position
    m_game.reset();
    
    if (moveRow < 0) {
        return m_game.getFen();
    }
    
    // Execute all moves up to the selected row
    for (int i = 0; i < moveRow; i++) {
        QModelIndex whiteIdx = m_model->index(i, PgnColumn::WhiteMove);
        QModelIndex blackIdx = m_model->index(i, PgnColumn::BlackMove);
        
        QString whiteMove = m_model->data(whiteIdx, Qt::DisplayRole).toString();
        if (!whiteMove.isEmpty()) {
            m_game.makeMove(whiteMove);
        }
        
        QString blackMove = m_model->data(blackIdx, Qt::DisplayRole).toString();
        if (!blackMove.isEmpty()) {
            m_game.makeMove(blackMove);
        }
    }
    
    // Execute current row's moves
    QModelIndex whiteIdx = m_model->index(moveRow, PgnColumn::WhiteMove);
    QString whiteMove = m_model->data(whiteIdx, Qt::DisplayRole).toString();
    if (!whiteMove.isEmpty()) {
        m_game.makeMove(whiteMove);
    }
    
    if (includeBlackMove) {
        QModelIndex blackIdx = m_model->index(moveRow, PgnColumn::BlackMove);
        QString blackMove = m_model->data(blackIdx, Qt::DisplayRole).toString();
        if (!blackMove.isEmpty()) {
            m_game.makeMove(blackMove);
        }
    }
    
    return m_game.getFen();
}
