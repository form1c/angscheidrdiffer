#include "filedifftab.h"
#include "diffview.h"
#include "difftextedit.h"
#include "iconloader.h"

#include "diff/commentstripper.h"

#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSaveFile>
#include <QScrollBar>
#include <QSettings>
#include <QShortcut>
#include <QStringDecoder>
#include <QTextDocument>
#include <QTextOption>
#include <QVBoxLayout>

#ifdef HAVE_KSYNTAX
#include <KSyntaxHighlighting/Definition>
#include <KSyntaxHighlighting/Repository>
#include <KSyntaxHighlighting/SyntaxHighlighter>
#include <KSyntaxHighlighting/Theme>
#endif

namespace {

const QChar kRecentSeparator = QChar(0x1F);

QString eolName(const QString &eol)
{
    if (eol == QLatin1String("\r\n")) return QStringLiteral("CRLF (Windows)");
    if (eol == QLatin1String("\r"))   return QStringLiteral("CR (Mac classic)");
    return QStringLiteral("LF (Unix)");
}

} // namespace

FileDiffTab::FileDiffTab(QWidget *parent)
    : CompareTab(parent)
{
    m_view = new DiffView(this);

    // --- Header row: file names above the two panes ---
    m_leftHeader  = new QLabel(tr("(no file)"), this);
    m_rightHeader = new QLabel(tr("(no file)"), this);
    m_leftHeader->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_rightHeader->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_leftHeader->setContentsMargins(6, 4, 6, 4);
    m_rightHeader->setContentsMargins(6, 4, 6, 4);

    auto *headerRow = new QWidget(this);
    auto *headerLayout = new QHBoxLayout(headerRow);
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->addWidget(m_leftHeader, 1);
    headerLayout->addWidget(m_rightHeader, 1);

    // --- EOL banner ---
    m_eolBanner = new QLabel(this);
    m_eolBanner->setVisible(false);
    m_eolBanner->setContentsMargins(8, 4, 8, 4);
    m_eolBanner->setAutoFillBackground(true);

    // --- "File changed on disk" banner ---
    m_fileChangedBanner = new QWidget(this);
    auto *fcLayout = new QHBoxLayout(m_fileChangedBanner);
    fcLayout->setContentsMargins(8, 4, 8, 4);
    m_fileChangedLabel = new QLabel(m_fileChangedBanner);
    fcLayout->addWidget(m_fileChangedLabel, 1);
    auto *fcReloadBtn = new QPushButton(tr("Reload"), m_fileChangedBanner);
    auto *fcIgnoreBtn = new QPushButton(tr("Ignore"), m_fileChangedBanner);
    fcLayout->addWidget(fcReloadBtn);
    fcLayout->addWidget(fcIgnoreBtn);
    m_fileChangedBanner->setVisible(false);

    // --- Find bar ---
    m_findBar = new QWidget(this);
    auto *findLayout = new QHBoxLayout(m_findBar);
    findLayout->setContentsMargins(4, 2, 4, 2);
    findLayout->addWidget(new QLabel(tr("Find:"), m_findBar));
    m_findEdit = new QLineEdit(m_findBar);
    m_findEdit->setClearButtonEnabled(true);
    findLayout->addWidget(m_findEdit, 1);
    auto *findPrevBtn = new QPushButton(
        QIcon::fromTheme(QStringLiteral("go-up")), QString(), m_findBar);
    auto *findNextBtn = new QPushButton(
        QIcon::fromTheme(QStringLiteral("go-down")), QString(), m_findBar);
    auto *findCloseBtn = new QPushButton(
        QIcon::fromTheme(QStringLiteral("window-close")), QString(), m_findBar);
    findLayout->addWidget(findPrevBtn);
    findLayout->addWidget(findNextBtn);
    findLayout->addWidget(findCloseBtn);
    m_findBar->setVisible(false);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_eolBanner);
    layout->addWidget(m_fileChangedBanner);
    layout->addWidget(headerRow);
    layout->addWidget(m_view, 1);
    layout->addWidget(m_findBar);

    // --- File watcher ---
    m_watcher = new QFileSystemWatcher(this);
    connect(m_watcher, &QFileSystemWatcher::fileChanged,
            this, &FileDiffTab::onFileChangedOnDisk);
    connect(fcReloadBtn, &QPushButton::clicked, this, [this]() {
        m_fileChangedBanner->setVisible(false);
        const QSet<QString> pending = m_pendingReloads;
        m_pendingReloads.clear();
        for (const QString &p : pending) {
            if (m_leftMeta.loaded && m_leftMeta.path == p)
                openFile(true, p);
            if (m_rightMeta.loaded && m_rightMeta.path == p)
                openFile(false, p);
        }
    });
    connect(fcIgnoreBtn, &QPushButton::clicked, this, [this]() {
        m_fileChangedBanner->setVisible(false);
        m_pendingReloads.clear();
    });

    // --- Verbindungen ---
    connect(m_view, &DiffView::diffStatusChanged, this, [this]() {
        updateStatus();
        Q_EMIT undoStateChanged();
    });
    connect(m_view, &DiffView::statusMessage,
            this, &CompareTab::transientMessage);
    connect(m_view->leftEdit()->document(), &QTextDocument::modificationChanged,
            this, [this]() { Q_EMIT titleChanged(); });
    connect(m_view->rightEdit()->document(), &QTextDocument::modificationChanged,
            this, [this]() { Q_EMIT titleChanged(); });
    for (QTextDocument *doc : {m_view->leftEdit()->document(),
                               m_view->rightEdit()->document()}) {
        connect(doc, &QTextDocument::undoAvailable,
                this, &FileDiffTab::undoStateChanged);
        connect(doc, &QTextDocument::redoAvailable,
                this, &FileDiffTab::undoStateChanged);
    }
    connect(m_view->leftEdit(), &DiffTextEdit::fileDropped,
            this, [this](const QString &p) { openFile(true, p); });
    connect(m_view->rightEdit(), &DiffTextEdit::fileDropped,
            this, [this](const QString &p) { openFile(false, p); });
    connect(m_view->leftEdit(), &DiffTextEdit::folderDropped,
            this, [this](const QString &d) { onFolderDropped(true, d); });
    connect(m_view->rightEdit(), &DiffTextEdit::folderDropped,
            this, [this](const QString &d) { onFolderDropped(false, d); });
    connect(m_view->leftEdit(), &DiffTextEdit::contextMenuAboutToShow,
            this, [this](QMenu *menu) { extendPaneContextMenu(true, menu); });
    connect(m_view->rightEdit(), &DiffTextEdit::contextMenuAboutToShow,
            this, [this](QMenu *menu) { extendPaneContextMenu(false, menu); });

    connect(m_findEdit, &QLineEdit::returnPressed, this, [this]() { findNext(); });
    connect(findNextBtn,  &QPushButton::clicked, this, [this]() { findNext(); });
    connect(findPrevBtn,  &QPushButton::clicked, this, [this]() { findNext(true); });
    connect(findCloseBtn, &QPushButton::clicked, this, [this]() {
        m_findBar->setVisible(false);
        m_view->leftEdit()->setFocus();
    });
    auto *escShortcut = new QShortcut(Qt::Key_Escape, m_findEdit);
    escShortcut->setContext(Qt::WidgetShortcut);
    connect(escShortcut, &QShortcut::activated, findCloseBtn, &QPushButton::click);

    optionsChanged(); // apply the options from the stored settings
}

// ---------------------------------------------------------------------------
// CompareTab
// ---------------------------------------------------------------------------

QString FileDiffTab::tabTitle() const
{
    if (!m_leftMeta.loaded && !m_rightMeta.loaded)
        return tr("New Comparison");
    const QString l = m_leftMeta.loaded
        ? QFileInfo(m_leftMeta.path).fileName() : tr("(none)");
    const QString r = m_rightMeta.loaded
        ? QFileInfo(m_rightMeta.path).fileName() : tr("(none)");
    return QStringLiteral("%1%2 ↔ %3%4")
        .arg(l,
             m_view->leftEdit()->document()->isModified()
                 ? QStringLiteral("*") : QString(),
             r,
             m_view->rightEdit()->document()->isModified()
                 ? QStringLiteral("*") : QString());
}

QString FileDiffTab::currentStatusText() const
{
    const int count = m_view->diffCount();
    if (count == 0)
        return tr("Files are identical");
    if (m_view->currentDiffNumber() > 0)
        return tr("Difference %1 of %2")
            .arg(m_view->currentDiffNumber()).arg(count);
    return tr("%n difference(s)", nullptr, count);
}

void FileDiffTab::updateStatus()
{
    Q_EMIT statusTextChanged(currentStatusText());
}

void FileDiffTab::optionsChanged()
{
    QSettings settings;

    DiffOptions options;
    options.ignoreWhitespace =
        settings.value(QStringLiteral("ignoreWhitespace"), false).toBool();
    options.ignoreComments =
        settings.value(QStringLiteral("ignoreComments"), false).toBool();
    options.ignoreCase =
        settings.value(QStringLiteral("ignoreCase"), false).toBool();
    options.ignoreFirstLines =
        settings.value(QStringLiteral("ignoreFirstLines"), 0).toInt();
    m_view->setOptions(options);

    const bool showWs =
        settings.value(QStringLiteral("showWhitespace"), false).toBool();
    const bool wrap =
        settings.value(QStringLiteral("wordWrap"), false).toBool();
    const bool aligned =
        settings.value(QStringLiteral("alignedView"), true).toBool();

    for (DiffTextEdit *edit : {m_view->leftEdit(), m_view->rightEdit()}) {
        QTextOption opt = edit->document()->defaultTextOption();
        opt.setFlags(showWs
                     ? opt.flags() | QTextOption::ShowTabsAndSpaces
                     : opt.flags() & ~QTextOption::ShowTabsAndSpaces);
        edit->document()->setDefaultTextOption(opt);
        edit->setLineWrapMode(wrap ? QPlainTextEdit::WidgetWidth
                                   : QPlainTextEdit::NoWrap);
    }

    // Placeholder rows are a rendering layer, the panes stay editable.
    m_view->setAlignedMode(aligned);

    updateEolBanner();
    Q_EMIT undoStateChanged();
}

void FileDiffTab::reloadContent()
{
    if (!maybeClose())
        return;
    if (m_leftMeta.loaded)
        openFile(true, m_leftMeta.path);
    if (m_rightMeta.loaded)
        openFile(false, m_rightMeta.path);
}

bool FileDiffTab::maybeClose()
{
    const bool leftModified  = m_view->leftEdit()->document()->isModified();
    const bool rightModified = m_view->rightEdit()->document()->isModified();
    if (!leftModified && !rightModified)
        return true;

    const auto ret = QMessageBox::warning(
        this, tr("Unsaved Changes"),
        tr("\"%1\" has unsaved changes. Save before closing?").arg(tabTitle()),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);

    if (ret == QMessageBox::Cancel)
        return false;
    if (ret == QMessageBox::Save) {
        if (leftModified && !saveFile(true, false))
            return false;
        if (rightModified && !saveFile(false, false))
            return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Laden / Speichern
// ---------------------------------------------------------------------------

void FileDiffTab::openFile(bool leftSide, const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, tr("Open File"),
                             tr("Cannot open file:\n%1").arg(path));
        return;
    }
    QByteArray raw = file.readAll();
    file.close();

    FileMeta meta;
    meta.path = path;
    meta.loaded = true;

    if (raw.startsWith("\xEF\xBB\xBF")) {
        meta.utf8Bom = true;
        raw.remove(0, 3);
    }
    QString text;
    QStringDecoder utf8(QStringDecoder::Utf8, QStringDecoder::Flag::Stateless);
    text = utf8.decode(raw);
    if (utf8.hasError() && !meta.utf8Bom) {
        meta.latin1 = true;
        text = QString::fromLatin1(raw);
    }

    if (text.contains(QLatin1String("\r\n")))
        meta.eol = QStringLiteral("\r\n");
    else if (text.contains(QLatin1Char('\r')))
        meta.eol = QStringLiteral("\r");
    else
        meta.eol = QStringLiteral("\n");
    text.replace(QLatin1String("\r\n"), QLatin1String("\n"));
    text.replace(QLatin1Char('\r'), QLatin1Char('\n'));

    (leftSide ? m_leftMeta : m_rightMeta) = meta;
    (leftSide ? m_pendingDirLeft : m_pendingDirRight).clear();

    m_view->setLanguages(CommentStripper::languageForFile(m_leftMeta.path),
                         CommentStripper::languageForFile(m_rightMeta.path));
    m_view->setText(leftSide, text);
    (leftSide ? m_view->leftEdit() : m_view->rightEdit())
        ->document()->setModified(false);

    updateHeader(leftSide);
    applySyntaxHighlighting(leftSide);
    updateFileWatcher();
    updateEolBanner();
    updateStatus();
    Q_EMIT titleChanged();

    if (m_leftMeta.loaded && m_rightMeta.loaded)
        addRecentPair();
}

bool FileDiffTab::saveFile(bool leftSide, bool saveAs)
{
    FileMeta &meta = leftSide ? m_leftMeta : m_rightMeta;
    QString path = meta.path;

    if (saveAs || path.isEmpty()) {
        path = QFileDialog::getSaveFileName(
            this, leftSide ? tr("Save Left File") : tr("Save Right File"), path);
        if (path.isEmpty())
            return false;
    }

    QString text = (leftSide ? m_view->leftEdit() : m_view->rightEdit())
                       ->toPlainText();
    if (meta.eol != QLatin1String("\n"))
        text.replace(QLatin1String("\n"), meta.eol);

    QByteArray data;
    if (meta.latin1) {
        data = text.toLatin1();
    } else {
        data = text.toUtf8();
        if (meta.utf8Bom)
            data.prepend("\xEF\xBB\xBF");
    }

    m_expectSelfChange.insert(path);

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) < 0 || !file.commit()) {
        m_expectSelfChange.remove(path);
        QMessageBox::warning(this, tr("Save File"),
                             tr("Cannot save file:\n%1").arg(path));
        return false;
    }

    meta.path = path;
    meta.loaded = true;
    updateHeader(leftSide);
    (leftSide ? m_view->leftEdit() : m_view->rightEdit())
        ->document()->setModified(false);
    updateFileWatcher();
    Q_EMIT titleChanged();
    Q_EMIT transientMessage(tr("Saved: %1").arg(path), 3000);
    return true;
}

void FileDiffTab::saveModified()
{
    if (m_view->leftEdit()->document()->isModified())
        saveFile(true, false);
    if (m_view->rightEdit()->document()->isModified())
        saveFile(false, false);
}

void FileDiffTab::swapSides()
{
    const QString leftText  = m_view->leftEdit()->toPlainText();
    const QString rightText = m_view->rightEdit()->toPlainText();
    const bool leftModified  = m_view->leftEdit()->document()->isModified();
    const bool rightModified = m_view->rightEdit()->document()->isModified();

    std::swap(m_leftMeta, m_rightMeta);

    m_view->setLanguages(CommentStripper::languageForFile(m_leftMeta.path),
                         CommentStripper::languageForFile(m_rightMeta.path));
    m_view->setText(true, rightText);
    m_view->setText(false, leftText);
    m_view->leftEdit()->document()->setModified(rightModified);
    m_view->rightEdit()->document()->setModified(leftModified);

    updateHeader(true);
    updateHeader(false);
    applySyntaxHighlighting(true);
    applySyntaxHighlighting(false);
    updateEolBanner();
    Q_EMIT titleChanged();
}

// ---------------------------------------------------------------------------
// Undo and redo on the side that was changed last
// ---------------------------------------------------------------------------

// doc->undo() and doc->redo() are used instead of the editor variants,
// which scroll to the undo position, often line 1 after a merge.
// The scroll position of both panes is kept.
void FileDiffTab::undoLastChange()
{
    DiffTextEdit *edit = m_view->lastChangedSideLeft()
        ? m_view->leftEdit() : m_view->rightEdit();
    const int left  = m_view->leftEdit()->verticalScrollBar()->value();
    const int right = m_view->rightEdit()->verticalScrollBar()->value();
    edit->document()->undo();
    m_view->leftEdit()->verticalScrollBar()->setValue(left);
    m_view->rightEdit()->verticalScrollBar()->setValue(right);
}

void FileDiffTab::redoLastChange()
{
    DiffTextEdit *edit = m_view->lastChangedSideLeft()
        ? m_view->leftEdit() : m_view->rightEdit();
    const int left  = m_view->leftEdit()->verticalScrollBar()->value();
    const int right = m_view->rightEdit()->verticalScrollBar()->value();
    edit->document()->redo();
    m_view->leftEdit()->verticalScrollBar()->setValue(left);
    m_view->rightEdit()->verticalScrollBar()->setValue(right);
}

bool FileDiffTab::canUndo() const
{
    if (m_readOnly)
        return false;
    return (m_view->lastChangedSideLeft()
                ? m_view->leftEdit() : m_view->rightEdit())
        ->document()->isUndoAvailable();
}

bool FileDiffTab::canRedo() const
{
    if (m_readOnly)
        return false;
    return (m_view->lastChangedSideLeft()
                ? m_view->leftEdit() : m_view->rightEdit())
        ->document()->isRedoAvailable();
}

void FileDiffTab::setReadOnly(bool readOnly)
{
    m_readOnly = readOnly;
    m_view->leftEdit()->setReadOnly(readOnly);
    m_view->rightEdit()->setReadOnly(readOnly);
    Q_EMIT undoStateChanged();
}

bool FileDiffTab::hasDifferences() const
{
    return m_view->diffCount() > 0;
}

// ---------------------------------------------------------------------------
// Header / Banner
// ---------------------------------------------------------------------------

void FileDiffTab::updateHeader(bool leftSide)
{
    const FileMeta &meta = leftSide ? m_leftMeta : m_rightMeta;
    const QString &pending = leftSide ? m_pendingDirLeft : m_pendingDirRight;
    QLabel *label = leftSide ? m_leftHeader : m_rightHeader;
    if (!pending.isEmpty()) {
        label->setText(tr("📁 %1 — drop a folder on the other side to compare")
                           .arg(pending));
    } else {
        label->setText(meta.loaded ? meta.path : tr("(no file)"));
    }
}

void FileDiffTab::updateEolBanner()
{
    const bool ignoreEol =
        QSettings().value(QStringLiteral("ignoreEol"), false).toBool();
    const bool differ = m_leftMeta.loaded && m_rightMeta.loaded
                        && m_leftMeta.eol != m_rightMeta.eol;
    const bool show = differ && !ignoreEol;
    if (show) {
        m_eolBanner->setText(
            tr("⚠ Line endings differ: left %1, right %2")
                .arg(eolName(m_leftMeta.eol), eolName(m_rightMeta.eol)));
    }
    m_eolBanner->setVisible(show);
}

// ---------------------------------------------------------------------------
// A dropped directory, remembered until both sides have one
// ---------------------------------------------------------------------------

void FileDiffTab::onFolderDropped(bool leftSide, const QString &dir)
{
    (leftSide ? m_pendingDirLeft : m_pendingDirRight) = dir;
    updateHeader(leftSide);

    if (!m_pendingDirLeft.isEmpty() && !m_pendingDirRight.isEmpty()) {
        const QString l = m_pendingDirLeft;
        const QString r = m_pendingDirRight;
        m_pendingDirLeft.clear();
        m_pendingDirRight.clear();
        updateHeader(true);
        updateHeader(false);
        Q_EMIT openFolderDiffRequested(l, r);
    } else {
        Q_EMIT transientMessage(
            tr("Folder selected — drop a folder on the other side to compare"),
            4000);
    }
}

// ---------------------------------------------------------------------------
// File watcher
// ---------------------------------------------------------------------------

void FileDiffTab::updateFileWatcher()
{
    const QStringList watched = m_watcher->files();
    if (!watched.isEmpty())
        m_watcher->removePaths(watched);
    QStringList paths;
    if (m_leftMeta.loaded && QFileInfo::exists(m_leftMeta.path))
        paths << m_leftMeta.path;
    if (m_rightMeta.loaded && QFileInfo::exists(m_rightMeta.path)
        && !paths.contains(m_rightMeta.path))
        paths << m_rightMeta.path;
    if (!paths.isEmpty())
        m_watcher->addPaths(paths);
}

void FileDiffTab::onFileChangedOnDisk(const QString &path)
{
    if (QFileInfo::exists(path) && !m_watcher->files().contains(path))
        m_watcher->addPath(path);

    if (m_expectSelfChange.remove(path))
        return;

    const bool affectsLeft  = m_leftMeta.loaded  && m_leftMeta.path == path;
    const bool affectsRight = m_rightMeta.loaded && m_rightMeta.path == path;
    if (!affectsLeft && !affectsRight)
        return;

    const bool modified =
        (affectsLeft  && m_view->leftEdit()->document()->isModified())
        || (affectsRight && m_view->rightEdit()->document()->isModified());

    if (!modified) {
        if (affectsLeft)
            openFile(true, path);
        if (affectsRight)
            openFile(false, path);
        Q_EMIT transientMessage(
            tr("Reloaded (changed on disk): %1").arg(path), 3000);
        return;
    }

    m_pendingReloads.insert(path);
    m_fileChangedLabel->setText(
        tr("⚠ File changed on disk: %1 — reload discards your unsaved changes.")
            .arg(QFileInfo(path).fileName()));
    m_fileChangedBanner->setVisible(true);
}

// ---------------------------------------------------------------------------
// Syntax-Highlighting (optional)
// ---------------------------------------------------------------------------

void FileDiffTab::applySyntaxHighlighting(bool leftSide)
{
#ifdef HAVE_KSYNTAX
    static KSyntaxHighlighting::Repository repository;

    QTextDocument *doc = (leftSide ? m_view->leftEdit()
                                   : m_view->rightEdit())->document();
    const FileMeta &meta = leftSide ? m_leftMeta : m_rightMeta;

    auto *highlighter =
        doc->findChild<KSyntaxHighlighting::SyntaxHighlighter *>();
    if (!highlighter)
        highlighter = new KSyntaxHighlighting::SyntaxHighlighter(doc);

    const bool dark = palette().color(QPalette::Base).lightness() < 128;
    highlighter->setTheme(repository.defaultTheme(
        dark ? KSyntaxHighlighting::Repository::DarkTheme
             : KSyntaxHighlighting::Repository::LightTheme));
    highlighter->setDefinition(repository.definitionForFileName(meta.path));
    highlighter->rehighlight();
#else
    Q_UNUSED(leftSide)
#endif
}

// ---------------------------------------------------------------------------
// Context menu of a pane: merging and navigation
// ---------------------------------------------------------------------------

void FileDiffTab::extendPaneContextMenu(bool leftSide, QMenu *menu)
{
    if (m_readOnly)
        return;

    menu->addSeparator();
    const auto add = [this, menu](const QString &iconName, const QString &text,
                                  auto slot) {
        QAction *a = menu->addAction(actionIcon(iconName), text);
        connect(a, &QAction::triggered, this, slot);
        return a;
    };

    // Reihenfolge: aufsteigender Wirkungsbereich — Selection, Section, All.
    // After a right click a selection is often present, so it comes first.
    if (leftSide) {
        QAction *sel = add(QStringLiteral("Selected-Text-Right"),
            tr("Selection →"),
            [this]() { m_view->copySelectedLines(true); });
        sel->setEnabled(m_view->leftEdit()->textCursor().hasSelection());
        add(QStringLiteral("Section-Text-Right"), tr("Section →"),
            [this]() { m_view->copyCurrentBlock(true); });
        add(QStringLiteral("All-Right"), tr("All →"),
            [this]() { m_view->copyAll(true); });
        add(QStringLiteral("Use-Both-Left-before-Right"),
            tr("Use Both (Left First)"),
            [this]() { m_view->copyCurrentBlock(true, MergeMode::InsertBefore); });
        add(QStringLiteral("Use-Both-Left-after-Right"),
            tr("Use Both (Left Last)"),
            [this]() { m_view->copyCurrentBlock(true, MergeMode::InsertAfter); });
    } else {
        QAction *sel = add(QStringLiteral("Selected-Text-Left"),
            tr("← Selection"),
            [this]() { m_view->copySelectedLines(false); });
        sel->setEnabled(m_view->rightEdit()->textCursor().hasSelection());
        add(QStringLiteral("Section-Text-Left"), tr("← Section"),
            [this]() { m_view->copyCurrentBlock(false); });
        add(QStringLiteral("All-Left"), tr("← All"),
            [this]() { m_view->copyAll(false); });
        add(QStringLiteral("Use-Both-Right-before-Left"),
            tr("Use Both (Right First)"),
            [this]() { m_view->copyCurrentBlock(false, MergeMode::InsertBefore); });
        add(QStringLiteral("Use-Both-Right-after-Left"),
            tr("Use Both (Right Last)"),
            [this]() { m_view->copyCurrentBlock(false, MergeMode::InsertAfter); });
    }
    menu->addSeparator();
    add(QStringLiteral("Previous-Change"), tr("Previous Difference"),
        [this]() { m_view->gotoPrevDiff(); });
    add(QStringLiteral("Next-Change"), tr("Next Difference"),
        [this]() { m_view->gotoNextDiff(); });
}

// ---------------------------------------------------------------------------
// Export, recently used pairs, search
// ---------------------------------------------------------------------------

void FileDiffTab::exportPatch()
{
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export Unified Diff"), QString(),
        tr("Patch files (*.patch *.diff);;All files (*)"));
    if (path.isEmpty())
        return;

    const QStringList leftLines =
        m_view->leftEdit()->toPlainText().split(QLatin1Char('\n'));
    const QStringList rightLines =
        m_view->rightEdit()->toPlainText().split(QLatin1Char('\n'));

    QString patch;
    patch += QStringLiteral("--- %1\n").arg(
        m_leftMeta.loaded ? m_leftMeta.path : QStringLiteral("left"));
    patch += QStringLiteral("+++ %1\n").arg(
        m_rightMeta.loaded ? m_rightMeta.path : QStringLiteral("right"));
    patch += QStringLiteral("@@ -1,%1 +1,%2 @@\n")
                 .arg(leftLines.size()).arg(rightLines.size());

    for (const DiffBlock &b : m_view->blocks()) {
        switch (b.type) {
        case BlockType::Equal:
            for (int i = 0; i < b.leftCount; ++i)
                patch += QLatin1Char(' ') + leftLines.value(b.leftStart + i)
                         + QLatin1Char('\n');
            break;
        case BlockType::OnlyLeft:
        case BlockType::Changed:
            for (int i = 0; i < b.leftCount; ++i)
                patch += QLatin1Char('-') + leftLines.value(b.leftStart + i)
                         + QLatin1Char('\n');
            if (b.type == BlockType::OnlyLeft)
                break;
            Q_FALLTHROUGH();
        case BlockType::OnlyRight:
            for (int i = 0; i < b.rightCount; ++i)
                patch += QLatin1Char('+') + rightLines.value(b.rightStart + i)
                         + QLatin1Char('\n');
            break;
        }
    }

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)
        || file.write(patch.toUtf8()) < 0 || !file.commit()) {
        QMessageBox::warning(this, tr("Export Unified Diff"),
                             tr("Cannot save file:\n%1").arg(path));
        return;
    }
    Q_EMIT transientMessage(tr("Patch exported: %1").arg(path), 3000);
}

void FileDiffTab::addRecentPair()
{
    const QString entry = m_leftMeta.path + kRecentSeparator + m_rightMeta.path;
    QSettings settings;
    QStringList recent =
        settings.value(QStringLiteral("recentPairs")).toStringList();
    recent.removeAll(entry);
    recent.prepend(entry);
    while (recent.size() > 10)
        recent.removeLast();
    settings.setValue(QStringLiteral("recentPairs"), recent);
}

void FileDiffTab::showFindBar()
{
    m_findBar->setVisible(true);
    m_findEdit->setFocus();
    m_findEdit->selectAll();
}

void FileDiffTab::findNext(bool backwards)
{
    const QString term = m_findEdit->text();
    if (term.isEmpty())
        return;

    DiffTextEdit *pane = m_view->rightEdit()->hasFocus()
        ? m_view->rightEdit() : m_view->leftEdit();

    QTextDocument::FindFlags flags;
    if (backwards)
        flags |= QTextDocument::FindBackward;

    if (!pane->find(term, flags)) {
        QTextCursor cur = pane->textCursor();
        cur.movePosition(backwards ? QTextCursor::End : QTextCursor::Start);
        pane->setTextCursor(cur);
        if (!pane->find(term, flags))
            Q_EMIT transientMessage(tr("\"%1\" not found").arg(term), 2000);
    }
}
