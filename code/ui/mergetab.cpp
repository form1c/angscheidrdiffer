#include "mergetab.h"
#include "difftextedit.h"
#include "iconloader.h"

#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSaveFile>
#include <QScrollBar>
#include <QSplitter>
#include <QTextBlock>
#include <QToolBar>
#include <QVBoxLayout>

namespace {

struct MergeColors {
    QColor mine, theirs, both, conflict, resolved;
};

MergeColors themeMergeColors(const QPalette &pal)
{
    const bool dark = pal.color(QPalette::Base).lightness() < 128;
    if (dark) {
        return {QColor(0x2e, 0x44, 0x2e),   // mine, green
                QColor(0x2e, 0x3f, 0x5a),   // theirs (blau)
                QColor(0x3d, 0x44, 0x2a),   // both same (oliv)
                QColor(0x5a, 0x2e, 0x2e),   // conflict (rot)
                QColor(0x2a, 0x44, 0x3a)};  // resolved, greenish
    }
    return {QColor(0xdd, 0xf5, 0xdd),
            QColor(0xd7, 0xe6, 0xf8),
            QColor(0xe8, 0xf0, 0xc8),
            QColor(0xf8, 0xd0, 0xd0),
            QColor(0xd5, 0xef, 0xdf)};
}

void appendLineSelections(QList<QTextEdit::ExtraSelection> &out,
                          QTextDocument *doc, int startLine, int count,
                          const QColor &color)
{
    for (int i = 0; i < count; ++i) {
        const QTextBlock block = doc->findBlockByNumber(startLine + i);
        if (!block.isValid())
            continue;
        QTextEdit::ExtraSelection sel;
        sel.cursor = QTextCursor(block);
        sel.cursor.movePosition(QTextCursor::EndOfBlock,
                                QTextCursor::KeepAnchor);
        sel.format.setBackground(color);
        sel.format.setProperty(QTextFormat::FullWidthSelection, true);
        out.append(sel);
    }
}

QStringList readLines(const QString &path, bool *ok, bool *hadCrLf)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        *ok = false;
        return {};
    }
    *ok = true;
    QString text = QString::fromUtf8(f.readAll());
    if (hadCrLf)
        *hadCrLf = text.contains(QLatin1String("\r\n"));
    text.replace(QLatin1String("\r\n"), QLatin1String("\n"));
    text.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    return text.split(QLatin1Char('\n'));
}

} // namespace

MergeTab::MergeTab(const QString &basePath, const QString &minePath,
                   const QString &theirsPath, const QString &outputPath,
                   QWidget *parent)
    : CompareTab(parent)
    , m_basePath(basePath)
    , m_minePath(minePath)
    , m_theirsPath(theirsPath)
    , m_outputPath(outputPath)
{
    setupUi();
    loadFiles();
}

QString MergeTab::tabTitle() const
{
    const QString name = QFileInfo(
        m_outputPath.isEmpty() ? m_minePath : m_outputPath).fileName();
    return QStringLiteral("🔀 %1").arg(name);
}

QString MergeTab::currentStatusText() const
{
    return m_statusText;
}

bool MergeTab::hasUnresolvedWork() const
{
    return m_dirty || m_output.unresolvedConflicts > 0;
}

void MergeTab::setupUi()
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    const auto makePane = [this](const QString &title, DiffTextEdit **edit,
                                 bool readOnly) {
        auto *box = new QWidget(this);
        auto *v = new QVBoxLayout(box);
        v->setContentsMargins(0, 0, 0, 0);
        v->setSpacing(0);
        auto *label = new QLabel(title, box);
        label->setAlignment(Qt::AlignCenter);
        label->setContentsMargins(0, 3, 0, 3);
        QFont f = label->font();
        f.setBold(true);
        label->setFont(f);
        v->addWidget(label);
        *edit = new DiffTextEdit(box);
        (*edit)->setReadOnly(readOnly);
        v->addWidget(*edit, 1);
        return box;
    };

    auto *split = new QSplitter(Qt::Vertical, this);

    auto *top = new QSplitter(Qt::Horizontal, split);
    top->addWidget(makePane(tr("Base"), &m_baseEdit, true));
    top->addWidget(makePane(tr("Mine (working copy)"), &m_mineEdit, true));
    top->addWidget(makePane(tr("Theirs"), &m_theirsEdit, true));
    split->addWidget(top);

    // --- The result area with its own bar ---
    auto *bottom = new QWidget(split);
    auto *bv = new QVBoxLayout(bottom);
    bv->setContentsMargins(0, 0, 0, 0);
    bv->setSpacing(0);

    auto *bar = new QToolBar(bottom);
    bar->setMovable(false);
    bar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);

    auto *prevAction = new QAction(actionIcon(QStringLiteral("Prev-Conflict")),
                                   tr("Previous Conflict"), this);
    connect(prevAction, &QAction::triggered,
            this, &MergeTab::gotoPrevConflict);
    bar->addAction(prevAction);
    auto *nextAction = new QAction(actionIcon(QStringLiteral("Next-Conflict")),
                                   tr("Next Conflict"), this);
    connect(nextAction, &QAction::triggered,
            this, &MergeTab::gotoNextConflict);
    bar->addAction(nextAction);

    m_counterLabel = new QLabel(this);
    m_counterLabel->setContentsMargins(8, 0, 8, 0);
    bar->addWidget(m_counterLabel);
    bar->addSeparator();

    const auto addChoice = [this, bar](const QIcon &icon, const QString &text,
                                       Merge3Choice choice) {
        auto *btn = new QPushButton(icon, text, this);
        btn->setToolTip(tr("Resolve the current conflict with: %1").arg(text));
        connect(btn, &QPushButton::clicked, this,
                [this, choice]() { chooseForCurrent(choice); });
        bar->addWidget(btn);
        m_choiceButtons.append(btn);
    };
    addChoice(actionIcon(QStringLiteral("Choose-Base")),
              tr("Base"), Merge3Choice::Base);
    addChoice(actionIcon(QStringLiteral("Choose-Mine")),
              tr("Mine"), Merge3Choice::Mine);
    addChoice(actionIcon(QStringLiteral("Choose-Theirs")),
              tr("Theirs"), Merge3Choice::Theirs);
    // The two combinations have no motif of their own. The neutral overview
    // icon of the choice family stands for "both sides".
    addChoice(actionIcon(QStringLiteral("Merge-Selection-Overview")),
              tr("Mine+Theirs"), Merge3Choice::MineThenTheirs);
    addChoice(actionIcon(QStringLiteral("Merge-Selection-Overview")),
              tr("Theirs+Mine"), Merge3Choice::TheirsThenMine);
    bar->addSeparator();

    auto *saveAction = new QAction(actionIcon(QStringLiteral("Save-Result")),
                                   tr("Save Result"), this);
    saveAction->setShortcut(QKeySequence::Save);
    connect(saveAction, &QAction::triggered,
            this, [this]() { saveResult(false); });
    bar->addAction(saveAction);
    auto *saveAsAction = new QAction(tr("Save Result As..."), this);
    connect(saveAsAction, &QAction::triggered,
            this, [this]() { saveResult(true); });
    bar->addAction(saveAsAction);

    bv->addWidget(bar);

    auto *resultLabel = new QLabel(tr("Result"), bottom);
    resultLabel->setAlignment(Qt::AlignCenter);
    resultLabel->setContentsMargins(0, 3, 0, 3);
    QFont bf = resultLabel->font();
    bf.setBold(true);
    resultLabel->setFont(bf);
    bv->addWidget(resultLabel);

    m_resultEdit = new DiffTextEdit(bottom);
    bv->addWidget(m_resultEdit, 1);
    split->addWidget(bottom);
    split->setStretchFactor(0, 3);
    split->setStretchFactor(1, 2);

    layout->addWidget(split);

    connect(m_resultEdit, &QPlainTextEdit::textChanged, this, [this]() {
        if (m_applyingResult)
            return;
        m_resultEdited = true;
        m_dirty = true;
    });

    // Scrolling of all four panes, synchronised through the chunk mapping.
    for (DiffTextEdit *e : {m_baseEdit, m_mineEdit, m_theirsEdit,
                            m_resultEdit}) {
        connect(e->verticalScrollBar(), &QScrollBar::valueChanged,
                this, [this, e]() { applySyncScroll(e); });
    }

    // Drag and drop: a file dropped on base, mine or theirs replaces that
    // input. The merge is rebuilt, so unsaved work is confirmed first.
    const auto connectDrop = [this](DiffTextEdit *edit, QString *pathMember) {
        connect(edit, &DiffTextEdit::fileDropped, this,
                [this, pathMember](const QString &path) {
            if (m_dirty) {
                const auto ret = QMessageBox::question(
                    this, tr("3-Way Merge"),
                    tr("Load \"%1\" and discard the current merge state?")
                        .arg(QFileInfo(path).fileName()));
                if (ret != QMessageBox::Yes)
                    return;
            }
            *pathMember = path;
            m_dirty = false;
            loadFiles();
            Q_EMIT titleChanged();
        });
    };
    connectDrop(m_baseEdit, &m_basePath);
    connectDrop(m_mineEdit, &m_minePath);
    connectDrop(m_theirsEdit, &m_theirsPath);
}

void MergeTab::loadFiles()
{
    bool okB = false, okM = false, okT = false;
    m_base   = readLines(m_basePath, &okB, nullptr);
    m_mine   = readLines(m_minePath, &okM, &m_mineCrLf);
    m_theirs = readLines(m_theirsPath, &okT, nullptr);
    if (!okB || !okM || !okT) {
        QMessageBox::warning(this, tr("3-Way Merge"),
                             tr("Cannot read input files:\n%1\n%2\n%3")
                                 .arg(m_basePath, m_minePath, m_theirsPath));
    }

    m_applyingResult = true;
    m_baseEdit->setPlainText(m_base.join(QLatin1Char('\n')));
    m_mineEdit->setPlainText(m_mine.join(QLatin1Char('\n')));
    m_theirsEdit->setPlainText(m_theirs.join(QLatin1Char('\n')));
    m_applyingResult = false;

    m_choices.clear();
    recomputeMerge(false);

    // Jump to the first conflict.
    if (!m_conflictChunks.isEmpty())
        gotoConflict(0);
}

// Number of result lines of a chunk, from its neighbours in chunkStart.
int MergeTab::resultChunkCount(int chunkIdx) const
{
    if (chunkIdx < 0 || chunkIdx >= m_output.chunkStart.size())
        return 0;
    const int start = m_output.chunkStart[chunkIdx];
    const int end = chunkIdx + 1 < m_output.chunkStart.size()
                        ? m_output.chunkStart[chunkIdx + 1]
                        : m_output.lines.size();
    return end - start;
}

int MergeTab::paneLine(const Merge3Chunk &c, const DiffTextEdit *pane) const
{
    if (pane == m_baseEdit)   return c.baseStart;
    if (pane == m_mineEdit)   return c.mineStart;
    if (pane == m_theirsEdit) return c.theirsStart;
    const int idx = int(&c - m_chunks.constData());
    return m_output.chunkStart.value(idx, 0);
}

int MergeTab::paneCount(const Merge3Chunk &c, const DiffTextEdit *pane) const
{
    if (pane == m_baseEdit)   return c.baseCount;
    if (pane == m_mineEdit)   return c.mineCount;
    if (pane == m_theirsEdit) return c.theirsCount;
    return resultChunkCount(int(&c - m_chunks.constData()));
}

void MergeTab::recomputeMerge(bool keepScroll)
{
    const int scrollValue = m_resultEdit->verticalScrollBar()->value();

    m_chunks = Merge3Engine::compare(m_base, m_mine, m_theirs);
    m_conflictChunks.clear();
    for (int i = 0; i < m_chunks.size(); ++i) {
        if (m_chunks[i].type == Merge3ChunkType::Conflict)
            m_conflictChunks.append(i);
    }
    if (m_currentConflict >= m_conflictChunks.size())
        m_currentConflict = m_conflictChunks.size() - 1;

    m_output = Merge3Engine::buildResult(m_chunks, m_base, m_mine, m_theirs,
                                         m_choices);

    m_applyingResult = true;
    m_resultEdit->setPlainText(m_output.lines.join(QLatin1Char('\n')));
    m_applyingResult = false;
    m_resultEdited = false;

    if (keepScroll)
        m_resultEdit->verticalScrollBar()->setValue(scrollValue);

    applyHighlights();
    updateStatus();
}

void MergeTab::applyHighlights()
{
    const MergeColors colors = themeMergeColors(palette());
    QList<QTextEdit::ExtraSelection> base, mine, theirs, result;

    for (int i = 0; i < m_chunks.size(); ++i) {
        const Merge3Chunk &c = m_chunks[i];
        switch (c.type) {
        case Merge3ChunkType::Stable:
            break;
        case Merge3ChunkType::OnlyMine:
            appendLineSelections(mine, m_mineEdit->document(),
                                 c.mineStart, c.mineCount, colors.mine);
            break;
        case Merge3ChunkType::OnlyTheirs:
            appendLineSelections(theirs, m_theirsEdit->document(),
                                 c.theirsStart, c.theirsCount, colors.theirs);
            break;
        case Merge3ChunkType::BothSame:
            appendLineSelections(mine, m_mineEdit->document(),
                                 c.mineStart, c.mineCount, colors.both);
            appendLineSelections(theirs, m_theirsEdit->document(),
                                 c.theirsStart, c.theirsCount, colors.both);
            break;
        case Merge3ChunkType::Conflict: {
            appendLineSelections(base, m_baseEdit->document(),
                                 c.baseStart, c.baseCount, colors.conflict);
            appendLineSelections(mine, m_mineEdit->document(),
                                 c.mineStart, c.mineCount, colors.conflict);
            appendLineSelections(theirs, m_theirsEdit->document(),
                                 c.theirsStart, c.theirsCount, colors.conflict);
            const bool resolved =
                m_choices.value(i, Merge3Choice::Unresolved)
                != Merge3Choice::Unresolved;
            appendLineSelections(result, m_resultEdit->document(),
                                 m_output.chunkStart.value(i, 0),
                                 resultChunkCount(i),
                                 resolved ? colors.resolved : colors.conflict);
            break;
        }
        }
    }

    m_baseEdit->setDiffSelections(base);
    m_mineEdit->setDiffSelections(mine);
    m_theirsEdit->setDiffSelections(theirs);
    m_resultEdit->setDiffSelections(result);
}

// Piecewise linear mapping through the chunks, as mapLine does for the
// two-way comparison, here for four panes.
void MergeTab::applySyncScroll(DiffTextEdit *from)
{
    if (m_syncing)
        return;
    m_syncing = true;

    const int line = from->firstVisibleLine();
    int chunkIdx = m_chunks.size() - 1;
    for (int i = 0; i < m_chunks.size(); ++i) {
        const int s = paneLine(m_chunks[i], from);
        const int c = paneCount(m_chunks[i], from);
        if (line < s + qMax(c, 1)) {
            chunkIdx = i;
            break;
        }
    }

    for (DiffTextEdit *to : {m_baseEdit, m_mineEdit, m_theirsEdit,
                             m_resultEdit}) {
        if (to == from)
            continue;
        if (chunkIdx < 0) {
            to->scrollToLine(line);
            continue;
        }
        const Merge3Chunk &c = m_chunks[chunkIdx];
        const int fs = paneLine(c, from);
        const int fc = paneCount(c, from);
        const int ts = paneLine(c, to);
        const int tc = paneCount(c, to);
        const double frac = fc > 0 ? double(line - fs) / fc : 0.0;
        to->scrollToLine(ts + int(frac * tc));
    }
    m_syncing = false;
}

void MergeTab::gotoConflict(int listIndex)
{
    if (m_conflictChunks.isEmpty())
        return;
    m_currentConflict = qBound(0, listIndex, m_conflictChunks.size() - 1);
    const int chunkIdx = m_conflictChunks[m_currentConflict];
    const Merge3Chunk &c = m_chunks[chunkIdx];

    m_syncing = true;
    m_baseEdit->scrollToLine(qMax(0, c.baseStart - 3));
    m_mineEdit->scrollToLine(qMax(0, c.mineStart - 3));
    m_theirsEdit->scrollToLine(qMax(0, c.theirsStart - 3));
    m_resultEdit->scrollToLine(
        qMax(0, m_output.chunkStart.value(chunkIdx, 0) - 3));
    m_syncing = false;

    // Rahmen um den aktiven Konflikt in allen vier Panes.
    m_baseEdit->setSectionFrame(c.baseStart, c.baseCount);
    m_mineEdit->setSectionFrame(c.mineStart, c.mineCount);
    m_theirsEdit->setSectionFrame(c.theirsStart, c.theirsCount);
    m_resultEdit->setSectionFrame(m_output.chunkStart.value(chunkIdx, 0),
                                  resultChunkCount(chunkIdx));
    updateStatus();
}

void MergeTab::gotoNextConflict()
{
    gotoConflict(m_currentConflict + 1);
}

void MergeTab::gotoPrevConflict()
{
    gotoConflict(m_currentConflict - 1);
}

void MergeTab::chooseForCurrent(Merge3Choice choice)
{
    if (m_currentConflict < 0 || m_currentConflict >= m_conflictChunks.size())
        return;
    if (m_resultEdited) {
        const auto ret = QMessageBox::question(
            this, tr("3-Way Merge"),
            tr("Applying a choice regenerates the result and discards "
               "manual edits. Continue?"));
        if (ret != QMessageBox::Yes)
            return;
    }
    const int chunkIdx = m_conflictChunks[m_currentConflict];
    m_choices[chunkIdx] = choice;
    m_dirty = true;
    recomputeMerge(true);
    // Move frame and counter to the conflict, or to the next open one.
    gotoConflict(m_currentConflict);
    if (m_output.unresolvedConflicts > 0) {
        // Jump straight to the next unresolved conflict.
        for (int off = 1; off <= m_conflictChunks.size(); ++off) {
            const int li = (m_currentConflict + off) % m_conflictChunks.size();
            const int ci = m_conflictChunks[li];
            if (m_choices.value(ci, Merge3Choice::Unresolved)
                == Merge3Choice::Unresolved) {
                gotoConflict(li);
                break;
            }
        }
    }
}

void MergeTab::saveResult(bool saveAs)
{
    QString path = m_outputPath;
    if (saveAs || path.isEmpty()) {
        path = QFileDialog::getSaveFileName(
            this, tr("Save Merge Result"),
            m_outputPath.isEmpty() ? m_minePath : m_outputPath);
        if (path.isEmpty())
            return;
        m_outputPath = path;
        Q_EMIT titleChanged();
    }

    // Warn based on the actual text, so resolving by hand counts.
    const QString text = m_resultEdit->toPlainText();
    if (text.contains(QLatin1String("<<<<<<< "))) {
        const auto ret = QMessageBox::warning(
            this, tr("Save Merge Result"),
            tr("The result still contains conflict markers.\nSave anyway?"),
            QMessageBox::Yes | QMessageBox::Cancel);
        if (ret != QMessageBox::Yes)
            return;
    }

    QString out = text;
    if (m_mineCrLf)
        out.replace(QLatin1String("\n"), QLatin1String("\r\n"));
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)
        || file.write(out.toUtf8()) < 0 || !file.commit()) {
        QMessageBox::warning(this, tr("Save Merge Result"),
                             tr("Cannot save file:\n%1").arg(path));
        return;
    }
    m_dirty = false;
    Q_EMIT transientMessage(tr("Merge result saved: %1").arg(path), 4000);
    updateStatus();
}

void MergeTab::updateStatus()
{
    int unresolved = 0;
    for (const int ci : m_conflictChunks) {
        if (m_choices.value(ci, Merge3Choice::Unresolved)
            == Merge3Choice::Unresolved)
            ++unresolved;
    }
    const bool onConflict = m_currentConflict >= 0
                            && m_currentConflict < m_conflictChunks.size();
    for (QPushButton *b : std::as_const(m_choiceButtons))
        b->setEnabled(onConflict);

    if (m_conflictChunks.isEmpty()) {
        m_statusText = tr("No conflicts — merged automatically");
    } else {
        m_statusText = tr("Conflict %1 of %2 — %3 unresolved")
                           .arg(onConflict ? m_currentConflict + 1 : 0)
                           .arg(m_conflictChunks.size())
                           .arg(unresolved);
    }
    if (m_counterLabel)
        m_counterLabel->setText(m_statusText);
    Q_EMIT statusTextChanged(m_statusText);
}

void MergeTab::reloadContent()
{
    if (m_dirty) {
        const auto ret = QMessageBox::question(
            this, tr("3-Way Merge"),
            tr("Reload all files and discard current merge state?"));
        if (ret != QMessageBox::Yes)
            return;
    }
    m_dirty = false;
    loadFiles();
}

bool MergeTab::maybeClose()
{
    if (!m_dirty)
        return true;
    const auto ret = QMessageBox::warning(
        this, tr("3-Way Merge"),
        tr("The merge result has not been saved.\nClose anyway?"),
        QMessageBox::Yes | QMessageBox::Cancel);
    return ret == QMessageBox::Yes;
}
