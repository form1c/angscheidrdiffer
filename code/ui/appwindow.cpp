#include "appwindow.h"
#include "difftextedit.h"
#include "diffview.h"
#include "filedifftab.h"
#include "folderdifftab.h"
#include "mergetab.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QSettings>
#include <QSpinBox>
#include <QStatusBar>
#include <QStyle>
#include <QTabWidget>
#include <QToolBar>
#include <QToolButton>
#include <QWidgetAction>

#include "iconloader.h"

namespace {

const QChar kRecentSeparator = QChar(0x1F);

} // namespace

AppWindow::AppWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("AnGscheidrDiffer"));
    // Fenster-Icon kommt von QApplication::setWindowIcon() (main.cpp:
    // Ressourcen-PNGs, installiertes Theme-Icon hat Vorrang).
    resize(1280, 800);

    m_tabs = new QTabWidget(this);
    m_tabs->setTabsClosable(true);
    m_tabs->setMovable(true);
    m_tabs->setDocumentMode(true);
    setCentralWidget(m_tabs);

    m_statusLabel = new QLabel(this);
    statusBar()->addPermanentWidget(m_statusLabel);

    setupActions();

    connect(m_tabs, &QTabWidget::currentChanged,
            this, [this](int) { onCurrentTabChanged(); });
    connect(m_tabs, &QTabWidget::tabCloseRequested, this, [this](int index) {
        auto *tab = qobject_cast<CompareTab *>(m_tabs->widget(index));
        if (tab && !tab->maybeClose())
            return;
        m_tabs->removeTab(index);
        delete tab;
        if (m_tabs->count() == 0)
            addFileTab(); // nie ganz leer
    });

    // Load the stored options into the toggles, without a broadcast loop.
    QSettings settings;
    m_restoringOptions = true;
    m_ignoreWsAction->setChecked(
        settings.value(QStringLiteral("ignoreWhitespace"), false).toBool());
    m_ignoreEolAction->setChecked(
        settings.value(QStringLiteral("ignoreEol"), false).toBool());
    m_ignoreCommentsAction->setChecked(
        settings.value(QStringLiteral("ignoreComments"), false).toBool());
    m_ignoreCaseAction->setChecked(
        settings.value(QStringLiteral("ignoreCase"), false).toBool());
    m_showWsAction->setChecked(
        settings.value(QStringLiteral("showWhitespace"), false).toBool());
    m_wrapAction->setChecked(
        settings.value(QStringLiteral("wordWrap"), false).toBool());
    m_alignedAction->setChecked(
        settings.value(QStringLiteral("alignedView"), true).toBool());
    m_ignoreFirstSpin->setValue(
        settings.value(QStringLiteral("ignoreFirstLines"), 0).toInt());
    m_restoringOptions = false;

    restoreGeometry(settings.value(QStringLiteral("geometry")).toByteArray());
    rebuildRecentMenus();
}

// ---------------------------------------------------------------------------
// Tabs
// ---------------------------------------------------------------------------

FileDiffTab *AppWindow::addFileTab(const QString &leftPath,
                                   const QString &rightPath,
                                   int insertAfterIndex)
{
    auto *tab = new FileDiffTab;
    connectTab(tab);
    connect(tab, &FileDiffTab::openFolderDiffRequested,
            this, &AppWindow::addFolderTab);
    connect(tab, &FileDiffTab::undoStateChanged,
            this, &AppWindow::updateActionStates);
    // A change of selection enables or disables the selection merge buttons.
    connect(tab->view()->leftEdit(), &QPlainTextEdit::copyAvailable,
            this, [this](bool) { updateActionStates(); });
    connect(tab->view()->rightEdit(), &QPlainTextEdit::copyAvailable,
            this, [this](bool) { updateActionStates(); });

    const int index = insertAfterIndex >= 0
        ? m_tabs->insertTab(insertAfterIndex + 1, tab, tab->tabTitle())
        : m_tabs->addTab(tab, tab->tabTitle());
    m_tabs->setCurrentIndex(index);

    if (m_readOnly)
        tab->setReadOnly(true);
    if (!leftPath.isEmpty())
        tab->openFile(true, leftPath);
    if (!rightPath.isEmpty())
        tab->openFile(false, rightPath);
    updateTabTitle(tab);
    return tab;
}

FolderDiffTab *AppWindow::addFolderTab(const QString &leftDir,
                                       const QString &rightDir)
{
    auto *tab = new FolderDiffTab(leftDir, rightDir);
    connectTab(tab);
    connect(tab, &FolderDiffTab::openFileDiffRequested, this,
            [this, tab](const QString &l, const QString &r) {
                addFileTab(l, r, m_tabs->indexOf(tab));
            });

    const int index = m_tabs->addTab(tab, tab->tabTitle());
    m_tabs->setCurrentIndex(index);
    rebuildRecentMenus();
    return tab;
}

MergeTab *AppWindow::addMergeTab(const QString &basePath,
                                 const QString &minePath,
                                 const QString &theirsPath,
                                 const QString &outputPath)
{
    auto *tab = new MergeTab(basePath, minePath, theirsPath, outputPath);
    connectTab(tab);
    const int index = m_tabs->addTab(tab, tab->tabTitle());
    m_tabs->setCurrentIndex(index);
    return tab;
}

void AppWindow::connectTab(CompareTab *tab)
{
    connect(tab, &CompareTab::titleChanged, this, [this, tab]() {
        updateTabTitle(tab);
    });
    connect(tab, &CompareTab::statusTextChanged, this,
            [this, tab](const QString &text) {
                if (currentTab() == tab)
                    m_statusLabel->setText(text);
            });
    connect(tab, &CompareTab::transientMessage, this,
            [this](const QString &text, int timeoutMs) {
                statusBar()->showMessage(text, timeoutMs);
            });
}

CompareTab *AppWindow::currentTab() const
{
    return qobject_cast<CompareTab *>(m_tabs->currentWidget());
}

FileDiffTab *AppWindow::currentFileTab() const
{
    return qobject_cast<FileDiffTab *>(m_tabs->currentWidget());
}

FolderDiffTab *AppWindow::currentFolderTab() const
{
    return qobject_cast<FolderDiffTab *>(m_tabs->currentWidget());
}

void AppWindow::onCurrentTabChanged()
{
    updateActionStates();
    CompareTab *tab = currentTab();
    m_statusLabel->setText(tab ? tab->currentStatusText() : QString());
    setWindowTitle(tab
        ? QStringLiteral("%1 – AnGscheidrDiffer").arg(tab->tabTitle())
        : QStringLiteral("AnGscheidrDiffer"));
    rebuildRecentMenus();
}

void AppWindow::updateTabTitle(CompareTab *tab)
{
    const int index = m_tabs->indexOf(tab);
    if (index >= 0)
        m_tabs->setTabText(index, tab->tabTitle());
    if (currentTab() == tab)
        setWindowTitle(QStringLiteral("%1 – AnGscheidrDiffer")
                           .arg(tab->tabTitle()));
}

void AppWindow::updateActionStates()
{
    FileDiffTab *fileTab = currentFileTab();
    const bool isFile = fileTab != nullptr;
    const bool canEdit = isFile && !m_readOnly;

    for (QAction *a : {m_openLeftAction, m_openRightAction, m_swapAction,
                       m_exportAction, m_findAction,
                       m_prevAction, m_nextAction})
        a->setEnabled(isFile);
    for (QAction *a : {m_saveLeftAction, m_saveRightAction, m_saveBothAction,
                       m_saveLeftAsAction, m_saveRightAsAction,
                       m_sectionRightAction, m_sectionLeftAction,
                       m_allRightAction, m_allLeftAction,
                       m_bothLeftFirstAction, m_bothRightFirstAction,
                       m_bothLeftLastAction, m_bothRightLastAction})
        a->setEnabled(canEdit);

    // Offer a selection merge only when the source pane has a selection.
    const bool leftSelection = isFile
        && fileTab->view()->leftEdit()->textCursor().hasSelection();
    const bool rightSelection = isFile
        && fileTab->view()->rightEdit()->textCursor().hasSelection();
    m_selRightAction->setEnabled(canEdit && leftSelection);
    m_selLeftAction->setEnabled(canEdit && rightSelection);

    m_undoAction->setEnabled(isFile && fileTab->canUndo());
    m_redoAction->setEnabled(isFile && fileTab->canRedo());
    m_reloadAction->setEnabled(currentTab() != nullptr);
}

void AppWindow::setReadOnly(bool readOnly)
{
    m_readOnly = readOnly;
    for (int i = 0; i < m_tabs->count(); ++i) {
        if (auto *fileTab = qobject_cast<FileDiffTab *>(m_tabs->widget(i)))
            fileTab->setReadOnly(readOnly);
    }
    updateActionStates();
    if (readOnly)
        statusBar()->showMessage(tr("Read-only mode"), 5000);
}

bool AppWindow::hasAnyDifferences() const
{
    for (int i = 0; i < m_tabs->count(); ++i) {
        if (auto *fileTab = qobject_cast<FileDiffTab *>(m_tabs->widget(i))) {
            if (fileTab->hasDifferences())
                return true;
        }
        if (auto *mergeTab = qobject_cast<MergeTab *>(m_tabs->widget(i))) {
            if (mergeTab->hasUnresolvedWork())
                return true;
        }
    }
    return false;
}

void AppWindow::closeEvent(QCloseEvent *event)
{
    for (int i = 0; i < m_tabs->count(); ++i) {
        auto *tab = qobject_cast<CompareTab *>(m_tabs->widget(i));
        if (tab && !tab->maybeClose()) {
            event->ignore();
            return;
        }
    }
    QSettings settings;
    settings.setValue(QStringLiteral("geometry"), saveGeometry());
    event->accept();
}

// ---------------------------------------------------------------------------
// Actions, menus and the single toolbar
// ---------------------------------------------------------------------------

void AppWindow::setupActions()
{
    // --- File actions ---
    m_openLeftAction = new QAction(
        actionIcon(QStringLiteral("Open-Left-File")), tr("Open &Left..."), this);
    m_openLeftAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_O));
    connect(m_openLeftAction, &QAction::triggered, this, [this]() {
        FileDiffTab *tab = currentFileTab();
        if (!tab) tab = addFileTab();
        const QString path = QFileDialog::getOpenFileName(this, tr("Open Left File"));
        if (!path.isEmpty()) tab->openFile(true, path);
    });

    m_openRightAction = new QAction(
        actionIcon(QStringLiteral("Open-Right-File")), tr("Open &Right..."), this);
    m_openRightAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_O));
    connect(m_openRightAction, &QAction::triggered, this, [this]() {
        FileDiffTab *tab = currentFileTab();
        if (!tab) tab = addFileTab();
        const QString path = QFileDialog::getOpenFileName(this, tr("Open Right File"));
        if (!path.isEmpty()) tab->openFile(false, path);
    });

    auto *newTabAction = new QAction(
        actionIcon(QStringLiteral("New-Tab")), tr("&New Comparison Tab"), this);
    newTabAction->setShortcut(QKeySequence::AddTab);
    connect(newTabAction, &QAction::triggered, this, [this]() { addFileTab(); });

    auto *mergeFilesAction = new QAction(
        actionIcon(QStringLiteral("3-Way-Merge")),
        tr("3-Way Merge (Base/Mine/Theirs)..."), this);
    connect(mergeFilesAction, &QAction::triggered,
            this, &AppWindow::mergeFilesDialog);

    auto *compareFoldersAction = new QAction(
        actionIcon(QStringLiteral("Compare-Folder")),
        tr("Compare &Folders..."), this);
    connect(compareFoldersAction, &QAction::triggered,
            this, &AppWindow::compareFoldersDialog);

    m_saveLeftAction = new QAction(
        actionIcon(QStringLiteral("Save-Left-File")), tr("Save Left"), this);
    connect(m_saveLeftAction, &QAction::triggered, this, [this]() {
        if (auto *t = currentFileTab()) t->saveFile(true, false);
    });
    m_saveRightAction = new QAction(
        actionIcon(QStringLiteral("Save-Right-File")), tr("Save Right"), this);
    connect(m_saveRightAction, &QAction::triggered, this, [this]() {
        if (auto *t = currentFileTab()) t->saveFile(false, false);
    });
    m_saveBothAction = new QAction(tr("&Save Modified"), this);
    m_saveBothAction->setShortcut(QKeySequence::Save);
    connect(m_saveBothAction, &QAction::triggered, this, [this]() {
        if (auto *t = currentFileTab()) t->saveModified();
    });
    m_saveLeftAsAction = new QAction(
        actionIcon(QStringLiteral("Save-Left-File-As")),
        tr("Save Left As..."), this);
    connect(m_saveLeftAsAction, &QAction::triggered, this, [this]() {
        if (auto *t = currentFileTab()) t->saveFile(true, true);
    });
    m_saveRightAsAction = new QAction(
        actionIcon(QStringLiteral("Save-Right-File-As")),
        tr("Save Right As..."), this);
    connect(m_saveRightAsAction, &QAction::triggered, this, [this]() {
        if (auto *t = currentFileTab()) t->saveFile(false, true);
    });

    m_reloadAction = new QAction(
        actionIcon(QStringLiteral("Refresh")), tr("Reload"), this);
    m_reloadAction->setShortcut(QKeySequence(Qt::Key_F5));
    connect(m_reloadAction, &QAction::triggered, this, [this]() {
        if (auto *t = currentTab()) t->reloadContent();
    });

    m_swapAction = new QAction(
        actionIcon(QStringLiteral("Swap-Left-and-Right")),
        tr("Swap Sides"), this);
    connect(m_swapAction, &QAction::triggered, this, [this]() {
        if (auto *t = currentFileTab()) t->swapSides();
    });

    m_exportAction = new QAction(
        actionIcon(QStringLiteral("Export-Diff")),
        tr("Export Unified Diff..."), this);
    connect(m_exportAction, &QAction::triggered, this, [this]() {
        if (auto *t = currentFileTab()) t->exportPatch();
    });

    auto *quitAction = new QAction(tr("&Quit"), this);
    quitAction->setShortcut(QKeySequence::Quit);
    connect(quitAction, &QAction::triggered, this, &QWidget::close);

    // --- Undo/Redo/Find ---
    m_undoAction = new QAction(
        actionIcon(QStringLiteral("Undo")), tr("Undo Merge/Edit"), this);
    m_undoAction->setShortcut(QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_Z));
    m_undoAction->setToolTip(
        tr("Undo the last change on the side that received it (Ctrl+Alt+Z).\n"
           "Plain Ctrl+Z undoes in the focused pane."));
    connect(m_undoAction, &QAction::triggered, this, [this]() {
        if (auto *t = currentFileTab()) t->undoLastChange();
    });
    m_redoAction = new QAction(
        actionIcon(QStringLiteral("Redo")), tr("Redo Merge/Edit"), this);
    m_redoAction->setShortcut(QKeySequence(Qt::CTRL | Qt::ALT | Qt::SHIFT | Qt::Key_Z));
    connect(m_redoAction, &QAction::triggered, this, [this]() {
        if (auto *t = currentFileTab()) t->redoLastChange();
    });

    m_findAction = new QAction(
        actionIcon(QStringLiteral("Find-Search")), tr("&Find..."), this);
    m_findAction->setShortcut(QKeySequence::Find);
    connect(m_findAction, &QAction::triggered, this, [this]() {
        if (auto *t = currentFileTab()) t->showFindBar();
    });

    // --- Navigation ---
    m_prevAction = new QAction(
        actionIcon(QStringLiteral("Previous-Change")), tr("Previous Difference"), this);
    m_prevAction->setShortcut(QKeySequence(Qt::ALT | Qt::Key_Up));
    connect(m_prevAction, &QAction::triggered, this, [this]() {
        if (auto *t = currentFileTab()) t->view()->gotoPrevDiff();
    });
    m_nextAction = new QAction(
        actionIcon(QStringLiteral("Next-Change")), tr("Next Difference"), this);
    m_nextAction->setShortcut(QKeySequence(Qt::ALT | Qt::Key_Down));
    connect(m_nextAction, &QAction::triggered, this, [this]() {
        if (auto *t = currentFileTab()) t->view()->gotoNextDiff();
    });

    // --- Merge-Aktionen ---
    auto makeMerge = [this](const QString &text, const QString &tooltip,
                            auto slot, const QKeySequence &shortcut = {}) {
        auto *a = new QAction(text, this);
        a->setToolTip(tooltip);
        if (!shortcut.isEmpty())
            a->setShortcut(shortcut);
        connect(a, &QAction::triggered, this, slot);
        return a;
    };
    m_sectionRightAction = makeMerge(
        tr("Section →"), tr("Copy current change section left → right (Alt+Right)"),
        [this]() { if (auto *t = currentFileTab())
                       t->view()->copyCurrentBlock(true); },
        QKeySequence(Qt::ALT | Qt::Key_Right));
    m_sectionLeftAction = makeMerge(
        tr("← Section"), tr("Copy current change section right → left (Alt+Left)"),
        [this]() { if (auto *t = currentFileTab())
                       t->view()->copyCurrentBlock(false); },
        QKeySequence(Qt::ALT | Qt::Key_Left));
    m_allRightAction = makeMerge(
        tr("All →"), tr("Copy all lines from left to right"),
        [this]() { if (auto *t = currentFileTab()) t->view()->copyAll(true); });
    m_allLeftAction = makeMerge(
        tr("← All"), tr("Copy all lines from right to left"),
        [this]() { if (auto *t = currentFileTab()) t->view()->copyAll(false); });
    m_selRightAction = makeMerge(
        tr("Selection →"), tr("Copy selected lines from left to right"),
        [this]() { if (auto *t = currentFileTab())
                       t->view()->copySelectedLines(true); });
    m_selLeftAction = makeMerge(
        tr("← Selection"), tr("Copy selected lines from right to left"),
        [this]() { if (auto *t = currentFileTab())
                       t->view()->copySelectedLines(false); });
    m_bothLeftFirstAction = makeMerge(
        tr("Use Both (Left First)"),
        tr("Insert the left section before the right section — keeps both"),
        [this]() { if (auto *t = currentFileTab())
                       t->view()->copyCurrentBlock(true, MergeMode::InsertBefore); });
    m_bothRightFirstAction = makeMerge(
        tr("Use Both (Right First)"),
        tr("Insert the right section before the left section — keeps both"),
        [this]() { if (auto *t = currentFileTab())
                       t->view()->copyCurrentBlock(false, MergeMode::InsertBefore); });
    m_bothLeftLastAction = makeMerge(
        tr("Use Both (Left Last)"),
        tr("Insert the left section after the right section — keeps both"),
        [this]() { if (auto *t = currentFileTab())
                       t->view()->copyCurrentBlock(true, MergeMode::InsertAfter); });
    m_bothRightLastAction = makeMerge(
        tr("Use Both (Right Last)"),
        tr("Insert the right section after the left section — keeps both"),
        [this]() { if (auto *t = currentFileTab())
                       t->view()->copyCurrentBlock(false, MergeMode::InsertAfter); });

    // Icons for the merge actions, showing direction and side in the motif.
    m_selRightAction->setIcon(actionIcon(QStringLiteral("Selected-Text-Right")));
    m_selLeftAction->setIcon(actionIcon(QStringLiteral("Selected-Text-Left")));
    m_sectionRightAction->setIcon(actionIcon(QStringLiteral("Section-Text-Right")));
    m_sectionLeftAction->setIcon(actionIcon(QStringLiteral("Section-Text-Left")));
    m_allRightAction->setIcon(actionIcon(QStringLiteral("All-Right")));
    m_allLeftAction->setIcon(actionIcon(QStringLiteral("All-Left")));
    m_bothLeftFirstAction->setIcon(
        actionIcon(QStringLiteral("Use-Both-Left-before-Right")));
    m_bothRightFirstAction->setIcon(
        actionIcon(QStringLiteral("Use-Both-Right-before-Left")));
    m_bothLeftLastAction->setIcon(
        actionIcon(QStringLiteral("Use-Both-Left-after-Right")));
    m_bothRightLastAction->setIcon(
        actionIcon(QStringLiteral("Use-Both-Right-after-Left")));

    // --- Globale Options-Toggles (schreiben QSettings + Broadcast) ---
    auto makeOption = [this](const QString &text, const QString &key,
                             const QString &tooltip = {}) {
        auto *a = new QAction(text, this);
        a->setCheckable(true);
        if (!tooltip.isEmpty())
            a->setToolTip(tooltip);
        connect(a, &QAction::toggled, this, [this, key](bool on) {
            if (m_restoringOptions)
                return;
            QSettings().setValue(key, on);
            broadcastOptionsChanged();
        });
        return a;
    };
    m_ignoreWsAction = makeOption(tr("Ignore Whitespace"),
                                  QStringLiteral("ignoreWhitespace"));
    m_ignoreWsAction->setIcon(
        actionIcon(QStringLiteral("Ignore-Whitespaces-Tabs")));
    m_ignoreEolAction = makeOption(
        tr("Ignore Line Endings"), QStringLiteral("ignoreEol"),
        tr("Hide the warning when the files use different line endings"));
    m_ignoreEolAction->setIcon(
        actionIcon(QStringLiteral("Ignore-Line-Endings")));
    m_ignoreCommentsAction = makeOption(
        tr("Ignore Comments"), QStringLiteral("ignoreComments"),
        tr("Ignore comment differences (C, C++, Java, JavaScript, TypeScript, HTML)"));
    m_ignoreCommentsAction->setIcon(
        actionIcon(QStringLiteral("Ignore-Comment-Lines")));
    m_ignoreCaseAction = makeOption(tr("Ignore Case"),
                                    QStringLiteral("ignoreCase"));
    m_ignoreCaseAction->setIcon(actionIcon(QStringLiteral("Ignore-Case")));
    m_showWsAction = makeOption(tr("Show Whitespace"),
                                QStringLiteral("showWhitespace"));
    m_showWsAction->setIcon(actionIcon(QStringLiteral("Show-Whitespaces")));
    m_wrapAction = makeOption(tr("Word Wrap"), QStringLiteral("wordWrap"));
    m_wrapAction->setIcon(actionIcon(QStringLiteral("Word-Wrap")));
    m_alignedAction = makeOption(
        tr("Aligned View"), QStringLiteral("alignedView"),
        tr("Align changes side by side with virtual placeholder rows.\n"
           "View-only rows — editing and undo are not affected.\n"
           "Requires Word Wrap to be off."));
    m_alignedAction->setIcon(actionIcon(QStringLiteral("Aligned-View")));

    // The aligned view needs word wrap off, because the placeholder
    // rendering assumes uniform line heights. The two exclude each other
    // rather than one being ignored silently, and a short message says
    // when the other option was switched off.
    connect(m_wrapAction, &QAction::toggled, this, [this](bool on) {
        if (on && !m_restoringOptions && m_alignedAction->isChecked()) {
            m_alignedAction->setChecked(false);
            statusBar()->showMessage(
                tr("Word Wrap and Aligned View cannot be active at the same "
                   "time — Aligned View has been turned off."), 5000);
        }
    });
    connect(m_alignedAction, &QAction::toggled, this, [this](bool on) {
        if (on && !m_restoringOptions && m_wrapAction->isChecked()) {
            m_wrapAction->setChecked(false);
            statusBar()->showMessage(
                tr("Word Wrap and Aligned View cannot be active at the same "
                   "time — Word Wrap has been turned off."), 5000);
        }
    });

    m_ignoreFirstSpin = new QSpinBox(this);
    m_ignoreFirstSpin->setRange(0, 100);
    m_ignoreFirstSpin->setPrefix(tr("Ignore first "));
    m_ignoreFirstSpin->setSuffix(tr(" lines"));
    m_ignoreFirstSpin->setToolTip(
        tr("Treat the first N lines of every file as equal "
           "(e.g. SVN keyword headers). 0 = off."));
    connect(m_ignoreFirstSpin, &QSpinBox::valueChanged, this, [this](int v) {
        if (m_restoringOptions)
            return;
        QSettings().setValue(QStringLiteral("ignoreFirstLines"), v);
        broadcastOptionsChanged();
    });

    // --- Menus ---
    QMenu *fileMenu = menuBar()->addMenu(tr("&File"));
    fileMenu->addAction(newTabAction);
    fileMenu->addAction(m_openLeftAction);
    fileMenu->addAction(m_openRightAction);
    m_recentMenu = fileMenu->addMenu(tr("Recent Comparisons"));
    fileMenu->addSeparator();
    fileMenu->addAction(compareFoldersAction);
    fileMenu->addAction(mergeFilesAction);
    m_recentFoldersMenu = fileMenu->addMenu(tr("Recent Folder Comparisons"));
    fileMenu->addSeparator();
    fileMenu->addAction(m_saveBothAction);
    fileMenu->addAction(m_saveLeftAction);
    fileMenu->addAction(m_saveRightAction);
    fileMenu->addAction(m_saveLeftAsAction);
    fileMenu->addAction(m_saveRightAsAction);
    fileMenu->addSeparator();
    fileMenu->addAction(m_reloadAction);
    fileMenu->addAction(m_swapAction);
    fileMenu->addSeparator();
    fileMenu->addAction(m_exportAction);
    fileMenu->addSeparator();
    fileMenu->addAction(quitAction);

    QMenu *editMenu = menuBar()->addMenu(tr("&Edit"));
    editMenu->addAction(m_undoAction);
    editMenu->addAction(m_redoAction);
    editMenu->addSeparator();
    editMenu->addAction(m_findAction);

    // Reihenfolge: aufsteigender Wirkungsbereich — Selection, Section, All.
    QMenu *mergeMenu = menuBar()->addMenu(tr("&Merge"));
    mergeMenu->addAction(m_selRightAction);
    mergeMenu->addAction(m_selLeftAction);
    mergeMenu->addSeparator();
    mergeMenu->addAction(m_sectionRightAction);
    mergeMenu->addAction(m_sectionLeftAction);
    mergeMenu->addSeparator();
    mergeMenu->addAction(m_allRightAction);
    mergeMenu->addAction(m_allLeftAction);
    mergeMenu->addSeparator();
    mergeMenu->addAction(m_bothLeftFirstAction);
    mergeMenu->addAction(m_bothRightFirstAction);
    mergeMenu->addAction(m_bothLeftLastAction);
    mergeMenu->addAction(m_bothRightLastAction);

    QMenu *optionsMenu = menuBar()->addMenu(tr("&Options"));
    optionsMenu->addAction(m_ignoreWsAction);
    optionsMenu->addAction(m_ignoreEolAction);
    optionsMenu->addAction(m_ignoreCommentsAction);
    optionsMenu->addAction(m_ignoreCaseAction);
    // A spin box cannot show an icon itself. A small icon label in front of
    // it lines the row up with the other entries of the menu.
    auto *spinRow = new QWidget(this);
    auto *spinLayout = new QHBoxLayout(spinRow);
    spinLayout->setContentsMargins(6, 2, 6, 2);
    spinLayout->setSpacing(6);
    const int spinIconSize = style()->pixelMetric(QStyle::PM_SmallIconSize);
    auto *spinIcon = new QLabel(spinRow);
    spinIcon->setPixmap(actionIcon(QStringLiteral("Ignore-First-X-Lines"))
                            .pixmap(spinIconSize, spinIconSize));
    spinLayout->addWidget(spinIcon);
    spinLayout->addWidget(m_ignoreFirstSpin, 1);
    auto *spinAction = new QWidgetAction(this);
    spinAction->setDefaultWidget(spinRow);
    optionsMenu->addAction(spinAction);
    optionsMenu->addSeparator();
    optionsMenu->addAction(m_showWsAction);
    optionsMenu->addAction(m_wrapAction);
    optionsMenu->addAction(m_alignedAction);

    QMenu *helpMenu = menuBar()->addMenu(tr("&Help"));
    auto *aboutAction = new QAction(
        actionIcon(QStringLiteral("Settings")),
        tr("&About AnGscheidrDiffer..."), this);
    connect(aboutAction, &QAction::triggered, this, &AppWindow::showAboutDialog);
    helpMenu->addAction(aboutAction);
    auto *aboutQtAction = new QAction(tr("About &Qt..."), this);
    connect(aboutQtAction, &QAction::triggered,
            this, []() { QApplication::aboutQt(); });
    helpMenu->addAction(aboutQtAction);

    // --- The single toolbar ---
    auto *toolbar = addToolBar(tr("Main"));
    toolbar->setMovable(false);
    toolbar->setToolButtonStyle(Qt::ToolButtonIconOnly);

    toolbar->addAction(m_openLeftAction);
    toolbar->addAction(m_openRightAction);
    toolbar->addAction(compareFoldersAction);
    toolbar->addSeparator();
    toolbar->addAction(m_saveLeftAction);
    toolbar->addAction(m_saveRightAction);
    toolbar->addSeparator();
    toolbar->addAction(m_undoAction);
    toolbar->addAction(m_redoAction);
    toolbar->addSeparator();
    toolbar->addAction(m_reloadAction);
    toolbar->addAction(m_swapAction);
    toolbar->addSeparator();
    toolbar->addAction(m_prevAction);
    toolbar->addAction(m_nextAction);
    toolbar->addSeparator();

    // The main merge actions carry icon and text, so the direction is legible.
    // Reihenfolge: aufsteigender Wirkungsbereich — Selection, Section, All.
    const auto addTextAction = [toolbar](QAction *a) {
        toolbar->addAction(a);
        if (auto *btn = qobject_cast<QToolButton *>(toolbar->widgetForAction(a)))
            btn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    };
    addTextAction(m_selRightAction);
    addTextAction(m_selLeftAction);
    addTextAction(m_sectionRightAction);
    addTextAction(m_sectionLeftAction);
    addTextAction(m_allRightAction);
    addTextAction(m_allLeftAction);

    // "Use Both"-Dropdown.
    auto *useBothBtn = new QToolButton(this);
    useBothBtn->setText(tr("Use Both"));
    useBothBtn->setPopupMode(QToolButton::InstantPopup);
    useBothBtn->setToolButtonStyle(Qt::ToolButtonTextOnly);
    auto *useBothMenu = new QMenu(useBothBtn);
    useBothMenu->addAction(m_bothLeftFirstAction);
    useBothMenu->addAction(m_bothRightFirstAction);
    useBothMenu->addAction(m_bothLeftLastAction);
    useBothMenu->addAction(m_bothRightLastAction);
    useBothBtn->setMenu(useBothMenu);
    toolbar->addWidget(useBothBtn);

    toolbar->addSeparator();

    // The "Options" drop-down: every toggle plus the spin box.
    auto *optionsBtn = new QToolButton(this);
    optionsBtn->setText(tr("Options"));
    optionsBtn->setIcon(actionIcon(QStringLiteral("Settings")));
    optionsBtn->setPopupMode(QToolButton::InstantPopup);
    optionsBtn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    optionsBtn->setMenu(optionsMenu);
    toolbar->addWidget(optionsBtn);
}

void AppWindow::broadcastOptionsChanged()
{
    for (int i = 0; i < m_tabs->count(); ++i) {
        if (auto *tab = qobject_cast<CompareTab *>(m_tabs->widget(i)))
            tab->optionsChanged();
    }
    updateActionStates();
}

// ---------------------------------------------------------------------------
// Folder comparison and recently used pairs
// ---------------------------------------------------------------------------

void AppWindow::compareFoldersDialog()
{
    const QString left = QFileDialog::getExistingDirectory(
        this, tr("Select Left Folder"));
    if (left.isEmpty())
        return;
    const QString right = QFileDialog::getExistingDirectory(
        this, tr("Select Right Folder"), QFileInfo(left).absolutePath());
    if (right.isEmpty())
        return;
    addFolderTab(left, right);
}

void AppWindow::mergeFilesDialog()
{
    const QString base = QFileDialog::getOpenFileName(
        this, tr("Select Base File (common ancestor)"));
    if (base.isEmpty())
        return;
    const QString startDir = QFileInfo(base).absolutePath();
    const QString mine = QFileDialog::getOpenFileName(
        this, tr("Select Mine File (your version)"), startDir);
    if (mine.isEmpty())
        return;
    const QString theirs = QFileDialog::getOpenFileName(
        this, tr("Select Theirs File (other version)"), startDir);
    if (theirs.isEmpty())
        return;
    // The output file is asked for when saving, through Save Result As.
    addMergeTab(base, mine, theirs, QString());
}

// ---------------------------------------------------------------------------
// About dialog
// ---------------------------------------------------------------------------

void AppWindow::showAboutDialog()
{
    // The version comes from QApplication, set in main.cpp from APP_VERSION,
    // which CMakeLists.txt provides. No second number is kept here.
    const QString version = QApplication::applicationVersion();

    QMessageBox box(this);
    box.setWindowTitle(tr("About AnGscheidrDiffer"));
    box.setIconPixmap(QApplication::windowIcon().pixmap(64, 64));
    box.setTextFormat(Qt::RichText);
    box.setText(tr("<h3>AnGscheidrDiffer %1</h3>"
                   "<p>Side-by-side diff and merge tool.</p>").arg(version));
    box.setInformativeText(
        tr("<p>Built with Qt %1.</p>"
           "<p>Copyright &copy; 2026 formic<br>"
           "Licensed under the MIT License.</p>")
            .arg(QLatin1String(qVersion())));
    box.addButton(QMessageBox::Close);
    box.exec();
}

void AppWindow::rebuildRecentMenus()
{
    QSettings settings;

    m_recentMenu->clear();
    const QStringList recent =
        settings.value(QStringLiteral("recentPairs")).toStringList();
    m_recentMenu->setEnabled(!recent.isEmpty());
    for (const QString &entry : recent) {
        const QStringList parts = entry.split(kRecentSeparator);
        if (parts.size() != 2)
            continue;
        QAction *a = m_recentMenu->addAction(
            QStringLiteral("%1  ↔  %2")
                .arg(QFileInfo(parts[0]).fileName(),
                     QFileInfo(parts[1]).fileName()));
        a->setToolTip(entry);
        connect(a, &QAction::triggered, this, [this, parts]() {
            addFileTab(parts[0], parts[1]);
        });
    }

    m_recentFoldersMenu->clear();
    const QStringList recentFolders =
        settings.value(QStringLiteral("recentFolderPairs")).toStringList();
    m_recentFoldersMenu->setEnabled(!recentFolders.isEmpty());
    for (const QString &entry : recentFolders) {
        const QStringList parts = entry.split(kRecentSeparator);
        if (parts.size() != 2)
            continue;
        QAction *a = m_recentFoldersMenu->addAction(
            QStringLiteral("📁 %1  ↔  %2")
                .arg(QFileInfo(parts[0]).fileName(),
                     QFileInfo(parts[1]).fileName()));
        a->setToolTip(entry);
        connect(a, &QAction::triggered, this, [this, parts]() {
            addFolderTab(parts[0], parts[1]);
        });
    }
}
