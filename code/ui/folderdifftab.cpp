#include "folderdifftab.h"
#include "iconloader.h"

#include <QAction>
#include <QCheckBox>
#include <QDateEdit>
#include <QDesktopServices>
#include <QDir>
#include <QDragEnterEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMenu>
#include <QUrl>
#include <QMessageBox>
#include <QMimeData>
#include <QRegularExpression>
#include <QSaveFile>
#include <QScrollBar>
#include <QSettings>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QWidgetAction>
#include <QtConcurrentRun>

#include <functional>

namespace {

const QChar kRecentSeparator = QChar(0x1F);

constexpr int kRoleRelPath = Qt::UserRole;      // QString
constexpr int kRoleStatus  = Qt::UserRole + 1;  // int(FolderEntryStatus)
constexpr int kRoleIsDir   = Qt::UserRole + 2;  // bool
constexpr int kRoleOnLeft  = Qt::UserRole + 3;  // bool
constexpr int kRoleOnRight = Qt::UserRole + 4;  // bool
constexpr int kRoleMTime   = Qt::UserRole + 5;  // QDateTime, later of both sides

struct RowColors {
    QColor different, onlyLeft, onlyRight;
};

RowColors themeRowColors(const QPalette &pal)
{
    const bool dark = pal.color(QPalette::Base).lightness() < 128;
    if (dark) {
        return {QColor(0x4a, 0x4a, 0x2a),
                QColor(0x4b, 0x2b, 0x2b),
                QColor(0x29, 0x44, 0x36)};
    }
    return {QColor(0xfd, 0xf6, 0xc3),
            QColor(0xf8, 0xd7, 0xd7),
            QColor(0xdd, 0xf5, 0xdd)};
}

QString statusText(FolderEntryStatus s, bool isBinary)
{
    switch (s) {
    case FolderEntryStatus::Identical: return QObject::tr("Identical");
    case FolderEntryStatus::Different:
        return isBinary ? QObject::tr("Different (binary)")
                        : QObject::tr("Different");
    case FolderEntryStatus::OnlyLeft:  return QObject::tr("Only left");
    case FolderEntryStatus::OnlyRight: return QObject::tr("Only right");
    }
    return {};
}

QString formatSize(qint64 size)
{
    return size < 0 ? QString() : QLocale().formattedDataSize(size);
}

QString formatTime(const QDateTime &dt)
{
    return dt.isValid()
        ? dt.toString(QStringLiteral("yyyy-MM-dd hh:mm")) : QString();
}

bool copyRecursively(const QString &src, const QString &dst)
{
    const QFileInfo fi(src);
    if (fi.isDir() && !fi.isSymLink()) {
        if (!QDir().mkpath(dst))
            return false;
        const QStringList entries = QDir(src).entryList(
            QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden);
        for (const QString &name : entries) {
            if (!copyRecursively(QDir(src).filePath(name),
                                 QDir(dst).filePath(name)))
                return false;
        }
        return true;
    }
    if (!QDir().mkpath(QFileInfo(dst).absolutePath()))
        return false;
    QFile::remove(dst);
    return QFile::copy(src, dst);
}

} // namespace

FolderDiffTab::FolderDiffTab(const QString &leftDir, const QString &rightDir,
                             QWidget *parent)
    : CompareTab(parent)
    , m_leftDir(QDir(leftDir).absolutePath())
    , m_rightDir(QDir(rightDir).absolutePath())
{
    setAcceptDrops(true);
    setupUi();
    addRecentFolderPair(m_leftDir, m_rightDir);

    connect(&m_watcher, &QFutureWatcher<FolderDiffEntry>::finished,
            this, &FolderDiffTab::onScanFinished);

    startScan();
}

FolderDiffTab::~FolderDiffTab()
{
    m_cancelRequested.store(true);
    m_watcher.waitForFinished();
}

QString FolderDiffTab::tabTitle() const
{
    return QStringLiteral("📁 %1 ↔ %2")
        .arg(QFileInfo(m_leftDir).fileName(), QFileInfo(m_rightDir).fileName());
}

QString FolderDiffTab::currentStatusText() const
{
    return m_statusText;
}

bool FolderDiffTab::maybeClose()
{
    m_cancelRequested.store(true);
    return true;
}

void FolderDiffTab::setupUi()
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // --- Row 1: the root paths ---
    m_pathLabel = new QLabel(this);
    m_pathLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_pathLabel->setContentsMargins(8, 4, 8, 4);
    m_pathLabel->setText(QStringLiteral("%1   ↔   %2")
                             .arg(m_leftDir, m_rightDir));
    layout->addWidget(m_pathLabel);

    // --- Row 2: the control bar of this tab ---
    auto *bar = new QToolBar(this);
    bar->setMovable(false);
    bar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);

    m_refreshAction = new QAction(
        actionIcon(QStringLiteral("Refresh")), tr("Refresh"), this);
    connect(m_refreshAction, &QAction::triggered,
            this, &FolderDiffTab::startScan);
    bar->addAction(m_refreshAction);

    m_cancelAction = new QAction(
        actionIcon(QStringLiteral("Cancel")), tr("Cancel"), this);
    m_cancelAction->setEnabled(false);
    connect(m_cancelAction, &QAction::triggered,
            this, &FolderDiffTab::cancelScan);
    bar->addAction(m_cancelAction);

    auto *exportAction = new QAction(
        actionIcon(QStringLiteral("Export-Folder-Report")),
        tr("Export Report..."), this);
    connect(exportAction, &QAction::triggered,
            this, &FolderDiffTab::exportReport);
    bar->addAction(exportAction);

    bar->addSeparator();

    auto *expandAllAction = new QAction(
        actionIcon(QStringLiteral("Expand-All")), tr("Expand All"), this);
    connect(expandAllAction, &QAction::triggered,
            this, [this]() { m_tree->expandAll(); });
    bar->addAction(expandAllAction);

    auto *collapseAllAction = new QAction(
        actionIcon(QStringLiteral("Collapse-All")), tr("Collapse All"), this);
    connect(collapseAllAction, &QAction::triggered,
            this, [this]() { m_tree->collapseAll(); });
    bar->addAction(collapseAllAction);

    bar->addSeparator();

    auto makeToggle = [this, bar](const QIcon &icon, const QString &text) {
        auto *a = new QAction(icon, text, this);
        a->setCheckable(true);
        a->setChecked(true);
        connect(a, &QAction::toggled, this, &FolderDiffTab::applyFilters);
        bar->addAction(a);
        return a;
    };
    bar->addWidget(new QLabel(tr(" Show: "), this));
    m_showIdenticalAction = makeToggle(
        actionIcon(QStringLiteral("Identical-Files")), tr("Identical"));
    m_showDifferentAction = makeToggle(
        actionIcon(QStringLiteral("Different-Files")), tr("Different"));
    m_showOnlyLeftAction  = makeToggle(
        actionIcon(QStringLiteral("Only-Left-Files")), tr("Only Left"));
    m_showOnlyRightAction = makeToggle(
        actionIcon(QStringLiteral("Only-Right-Files")), tr("Only Right"));

    bar->addSeparator();

    QSettings settings;

    // --- Filter drop-down: view filters by name and modification date ---
    auto *filterBtn = new QToolButton(this);
    filterBtn->setText(tr("Filter"));
    // While the delivered icon set carries no filter motif, the system theme
    // is used. As soon as Filter_<size>.png exists and the resource file has
    // been regenerated, it is picked up without a change here.
    QIcon filterIcon = actionIcon(QStringLiteral("Filter"));
    if (filterIcon.availableSizes().isEmpty())
        filterIcon = QIcon::fromTheme(QStringLiteral("view-filter"));
    filterBtn->setIcon(filterIcon);
    filterBtn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    filterBtn->setPopupMode(QToolButton::InstantPopup);
    auto *filterMenu = new QMenu(filterBtn);

    auto addFilterRow = [this, filterMenu](const QString &label,
                                           QWidget *field) {
        auto *row = new QWidget(filterMenu);
        auto *lay = new QHBoxLayout(row);
        lay->setContentsMargins(8, 2, 8, 2);
        lay->addWidget(new QLabel(label, row));
        lay->addWidget(field, 1);
        auto *wa = new QWidgetAction(filterMenu);
        wa->setDefaultWidget(row);
        filterMenu->addAction(wa);
    };

    m_includeEdit = new QLineEdit(this);
    m_includeEdit->setText(settings.value(
        QStringLiteral("folderIncludes")).toString());
    m_includeEdit->setPlaceholderText(tr("e.g. *.cpp;*.h — empty = all"));
    m_includeEdit->setToolTip(
        tr("Show only files matching these semicolon-separated patterns"));
    m_includeEdit->setMinimumWidth(180);
    connect(m_includeEdit, &QLineEdit::editingFinished, this, [this]() {
        QSettings().setValue(QStringLiteral("folderIncludes"),
                             m_includeEdit->text());
        applyFilters();
    });
    addFilterRow(tr("Name:"), m_includeEdit);

    m_modifiedSinceCheck = new QCheckBox(tr("Modified since"), this);
    m_modifiedSinceCheck->setChecked(settings.value(
        QStringLiteral("folderModifiedSinceOn"), false).toBool());
    m_modifiedSinceEdit = new QDateEdit(this);
    m_modifiedSinceEdit->setCalendarPopup(true);
    m_modifiedSinceEdit->setDate(settings.value(
        QStringLiteral("folderModifiedSince"),
        QDate::currentDate().addDays(-7)).toDate());
    m_modifiedSinceEdit->setToolTip(
        tr("Show only files whose newer side was modified on or after this "
           "date (folders stay visible while they contain matches)"));
    connect(m_modifiedSinceCheck, &QCheckBox::toggled, this, [this](bool on) {
        QSettings().setValue(QStringLiteral("folderModifiedSinceOn"), on);
        applyFilters();
    });
    connect(m_modifiedSinceEdit, &QDateEdit::dateChanged, this,
            [this](QDate d) {
                QSettings().setValue(QStringLiteral("folderModifiedSince"), d);
                applyFilters();
            });
    auto *sinceRow = new QWidget(filterMenu);
    auto *sinceLay = new QHBoxLayout(sinceRow);
    sinceLay->setContentsMargins(8, 2, 8, 2);
    sinceLay->addWidget(m_modifiedSinceCheck);
    sinceLay->addWidget(m_modifiedSinceEdit, 1);
    auto *sinceAction = new QWidgetAction(filterMenu);
    sinceAction->setDefaultWidget(sinceRow);
    filterMenu->addAction(sinceAction);

    filterBtn->setMenu(filterMenu);
    bar->addWidget(filterBtn);

    bar->addSeparator();

    m_quickCompareAction = new QAction(
        actionIcon(QStringLiteral("Quick-Compare-Folder")),
        tr("Quick (size only)"), this);
    m_quickCompareAction->setCheckable(true);
    m_quickCompareAction->setChecked(
        settings.value(QStringLiteral("quickCompare"), false).toBool());
    m_quickCompareAction->setToolTip(
        tr("Compare by file size only — fast, but does not detect all changes"));
    connect(m_quickCompareAction, &QAction::toggled, this, [this]() {
        QSettings().setValue(QStringLiteral("quickCompare"),
                             m_quickCompareAction->isChecked());
        startScan();
    });
    bar->addAction(m_quickCompareAction);

    bar->addWidget(new QLabel(tr(" Exclude: "), this));
    m_excludeEdit = new QLineEdit(this);
    m_excludeEdit->setText(settings.value(
        QStringLiteral("folderExcludes"),
        QStringLiteral(".svn;.git;build;*.o;*.so")).toString());
    m_excludeEdit->setToolTip(
        tr("Semicolon-separated name patterns to skip (wildcards allowed)"));
    m_excludeEdit->setMaximumWidth(260);
    connect(m_excludeEdit, &QLineEdit::editingFinished, this, [this]() {
        QSettings().setValue(QStringLiteral("folderExcludes"),
                             m_excludeEdit->text());
        startScan();
    });
    bar->addWidget(m_excludeEdit);

    layout->addWidget(bar);

    // --- Baum ---
    m_tree = new QTreeWidget(this);
    m_tree->setColumnCount(6);
    m_tree->setHeaderLabels({tr("Name"), tr("Status"),
                             tr("Size (L)"), tr("Modified (L)"),
                             tr("Size (R)"), tr("Modified (R)")});
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    for (int c = 1; c < 6; ++c)
        m_tree->header()->setSectionResizeMode(c, QHeaderView::ResizeToContents);
    m_tree->setAlternatingRowColors(true);
    m_tree->setRootIsDecorated(true);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    layout->addWidget(m_tree, 1);

    connect(m_tree, &QTreeWidget::itemDoubleClicked, this,
            [this](QTreeWidgetItem *item, int) { onItemDoubleClicked(item); });
    connect(m_tree, &QTreeWidget::customContextMenuRequested,
            this, &FolderDiffTab::showContextMenu);
}

// ---------------------------------------------------------------------------
// Drag and drop: a directory dropped on the left or right half sets that root
// ---------------------------------------------------------------------------

void FolderDiffTab::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasUrls())
        event->acceptProposedAction();
}

void FolderDiffTab::dropEvent(QDropEvent *event)
{
    QStringList dirs;
    const QList<QUrl> urls = event->mimeData()->urls();
    for (const QUrl &url : urls) {
        if (url.isLocalFile() && QFileInfo(url.toLocalFile()).isDir())
            dirs << url.toLocalFile();
    }
    if (dirs.isEmpty()) {
        Q_EMIT transientMessage(
            tr("Drop folders here to change the comparison roots"), 3000);
        return;
    }
    event->acceptProposedAction();

    if (dirs.size() >= 2) {
        setRoot(true, dirs.at(0));
        setRoot(false, dirs.at(1));
    } else {
        const bool leftHalf =
            event->position().toPoint().x() < width() / 2;
        setRoot(leftHalf, dirs.first());
    }
    startScan();
}

void FolderDiffTab::setRoot(bool leftSide, const QString &dir)
{
    // A new comparison must not restore the tree state of the old content.
    // The flag is needed because startScan would otherwise save the old tree.
    m_freshComparison = true;
    (leftSide ? m_leftDir : m_rightDir) = QDir(dir).absolutePath();
    m_pathLabel->setText(QStringLiteral("%1   ↔   %2")
                             .arg(m_leftDir, m_rightDir));
    addRecentFolderPair(m_leftDir, m_rightDir);
    Q_EMIT titleChanged();
}

// ---------------------------------------------------------------------------
// Scan
// ---------------------------------------------------------------------------

FolderCompareSettings FolderDiffTab::currentSettings() const
{
    QSettings settings;
    FolderCompareSettings s;
    s.diffOptions.ignoreWhitespace =
        settings.value(QStringLiteral("ignoreWhitespace"), false).toBool();
    s.diffOptions.ignoreComments =
        settings.value(QStringLiteral("ignoreComments"), false).toBool();
    s.diffOptions.ignoreCase =
        settings.value(QStringLiteral("ignoreCase"), false).toBool();
    s.diffOptions.ignoreFirstLines =
        settings.value(QStringLiteral("ignoreFirstLines"), 0).toInt();
    s.ignoreEol =
        settings.value(QStringLiteral("ignoreEol"), false).toBool();
    s.compareContent = !m_quickCompareAction->isChecked();
    s.excludePatterns =
        m_excludeEdit->text().split(QLatin1Char(';'), Qt::SkipEmptyParts);
    return s;
}

// Saves which nodes are open, the current item and the scroll position. A
// scan rebuilds the tree completely and should not destroy the navigation.
void FolderDiffTab::rememberTreeState()
{
    m_expandedPaths.clear();
    const std::function<void(QTreeWidgetItem *)> walk =
        [&](QTreeWidgetItem *item) {
            if (item->isExpanded())
                m_expandedPaths.insert(
                    item->data(0, kRoleRelPath).toString());
            for (int i = 0; i < item->childCount(); ++i)
                walk(item->child(i));
        };
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i)
        walk(m_tree->topLevelItem(i));

    m_currentPath = m_tree->currentItem()
        ? m_tree->currentItem()->data(0, kRoleRelPath).toString()
        : QString();
    m_treeScroll = m_tree->verticalScrollBar()->value();
    m_hasSavedTreeState = m_tree->topLevelItemCount() > 0;
}

void FolderDiffTab::restoreTreeState()
{
    const std::function<void(QTreeWidgetItem *)> walk =
        [&](QTreeWidgetItem *item) {
            const QString rel = item->data(0, kRoleRelPath).toString();
            item->setExpanded(m_expandedPaths.contains(rel));
            if (!m_currentPath.isEmpty() && rel == m_currentPath)
                m_tree->setCurrentItem(item);
            for (int i = 0; i < item->childCount(); ++i)
                walk(item->child(i));
        };
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i)
        walk(m_tree->topLevelItem(i));
}

void FolderDiffTab::startScan()
{
    if (m_scanning)
        return;
    if (m_freshComparison) {
        m_freshComparison = false;
        m_hasSavedTreeState = false;
    } else {
        rememberTreeState();
    }
    m_scanning = true;
    m_cancelRequested.store(false);
    m_refreshAction->setEnabled(false);
    m_cancelAction->setEnabled(true);
    m_statusText = tr("Scanning...");
    Q_EMIT statusTextChanged(m_statusText);

    const QString l = m_leftDir;
    const QString r = m_rightDir;
    const FolderCompareSettings s = currentSettings();
    const std::atomic<bool> *cancel = &m_cancelRequested;
    m_watcher.setFuture(QtConcurrent::run([l, r, s, cancel]() {
        return FolderCompare::compareFolders(l, r, s, cancel);
    }));
}

void FolderDiffTab::cancelScan()
{
    m_cancelRequested.store(true);
}

void FolderDiffTab::onScanFinished()
{
    m_scanning = false;
    m_refreshAction->setEnabled(true);
    m_cancelAction->setEnabled(false);
    m_root = m_watcher.result();
    populateTree();
    applyFilters();
}

// ---------------------------------------------------------------------------
// Tree
// ---------------------------------------------------------------------------

void FolderDiffTab::populateTree()
{
    m_tree->setUpdatesEnabled(false);
    m_tree->clear();

    const RowColors colors = themeRowColors(palette());
    int nDifferent = 0, nOnlyLeft = 0, nOnlyRight = 0, nIdentical = 0;

    const std::function<void(const FolderDiffEntry &, QTreeWidgetItem *)> addEntry =
        [&](const FolderDiffEntry &e, QTreeWidgetItem *parent) {
            auto *item = parent ? new QTreeWidgetItem(parent)
                                : new QTreeWidgetItem(m_tree);
            item->setText(0, e.name);
            item->setIcon(0, QIcon::fromTheme(
                e.isDir ? QStringLiteral("folder")
                        : QStringLiteral("text-x-generic")));
            item->setText(1, statusText(e.status, e.isBinary));
            item->setText(2, formatSize(e.sizeLeft));
            item->setText(3, formatTime(e.mtimeLeft));
            item->setText(4, formatSize(e.sizeRight));
            item->setText(5, formatTime(e.mtimeRight));
            item->setData(0, kRoleRelPath, e.relPath);
            item->setData(0, kRoleStatus, int(e.status));
            item->setData(0, kRoleIsDir, e.isDir);
            item->setData(0, kRoleOnLeft, e.sizeLeft >= 0);
            item->setData(0, kRoleOnRight, e.sizeRight >= 0);
            item->setData(0, kRoleMTime,
                          e.mtimeLeft > e.mtimeRight ? e.mtimeLeft
                                                     : e.mtimeRight);

            QColor bg;
            switch (e.status) {
            case FolderEntryStatus::Different: bg = colors.different; break;
            case FolderEntryStatus::OnlyLeft:  bg = colors.onlyLeft;  break;
            case FolderEntryStatus::OnlyRight: bg = colors.onlyRight; break;
            case FolderEntryStatus::Identical: break;
            }
            if (bg.isValid()) {
                for (int c = 0; c < m_tree->columnCount(); ++c)
                    item->setBackground(c, bg);
            }
            if (e.isDir && e.status != FolderEntryStatus::Identical) {
                QFont f = item->font(0);
                f.setBold(true);
                item->setFont(0, f);
            }

            if (!e.isDir) {
                switch (e.status) {
                case FolderEntryStatus::Different: ++nDifferent; break;
                case FolderEntryStatus::OnlyLeft:  ++nOnlyLeft;  break;
                case FolderEntryStatus::OnlyRight: ++nOnlyRight; break;
                case FolderEntryStatus::Identical: ++nIdentical; break;
                }
            } else if (e.children.isEmpty()
                       && e.status != FolderEntryStatus::Identical) {
                if (e.status == FolderEntryStatus::OnlyLeft) ++nOnlyLeft;
                else ++nOnlyRight;
            }

            for (const FolderDiffEntry &c : e.children)
                addEntry(c, item);
        };

    for (const FolderDiffEntry &c : m_root.children)
        addEntry(c, nullptr);

    // First scan: start collapsed. Changed subdirectories are recognisable
    // Status-Vererbung (gelb + fett) auch zugeklappt erkennbar.
    // A later scan after deleting, copying or refreshing restores the state.
    if (m_hasSavedTreeState)
        restoreTreeState();
    else
        m_tree->collapseAll();
    m_tree->setUpdatesEnabled(true);
    if (m_hasSavedTreeState && m_treeScroll >= 0)
        m_tree->verticalScrollBar()->setValue(m_treeScroll);

    const QString cancelNote = m_cancelRequested.load()
        ? tr(" (scan cancelled — partial result)") : QString();
    m_statusText = tr("%1 different, %2 only left, %3 only right, %4 identical%5")
                       .arg(nDifferent).arg(nOnlyLeft).arg(nOnlyRight)
                       .arg(nIdentical).arg(cancelNote);
    Q_EMIT statusTextChanged(m_statusText);
}

void FolderDiffTab::applyFilters()
{
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i)
        applyFilterRecursive(m_tree->topLevelItem(i));
}

bool FolderDiffTab::applyFilterRecursive(QTreeWidgetItem *item)
{
    const auto status =
        static_cast<FolderEntryStatus>(item->data(0, kRoleStatus).toInt());
    const bool isDir = item->data(0, kRoleIsDir).toBool();

    const auto allowed = [this](FolderEntryStatus s) {
        switch (s) {
        case FolderEntryStatus::Identical: return m_showIdenticalAction->isChecked();
        case FolderEntryStatus::Different: return m_showDifferentAction->isChecked();
        case FolderEntryStatus::OnlyLeft:  return m_showOnlyLeftAction->isChecked();
        case FolderEntryStatus::OnlyRight: return m_showOnlyRightAction->isChecked();
        }
        return true;
    };

    // View filters from the drop-down, applied to leaf entries. A directory
    // bleiben sichtbar, solange sie Treffer enthalten).
    const auto matchesLeafFilters = [this, item, isDir]() {
        if (!isDir && !m_includeEdit->text().trimmed().isEmpty()) {
            const QStringList patterns = m_includeEdit->text().split(
                QLatin1Char(';'), Qt::SkipEmptyParts);
            const QString name = item->text(0);
            bool matched = false;
            for (const QString &p : patterns) {
                const QRegularExpression re(
                    QRegularExpression::wildcardToRegularExpression(p.trimmed()),
                    QRegularExpression::CaseInsensitiveOption);
                if (re.match(name).hasMatch()) {
                    matched = true;
                    break;
                }
            }
            if (!matched)
                return false;
        }
        if (m_modifiedSinceCheck->isChecked()) {
            const QDateTime mtime = item->data(0, kRoleMTime).toDateTime();
            if (!mtime.isValid()
                || mtime.date() < m_modifiedSinceEdit->date())
                return false;
        }
        return true;
    };

    bool visible;
    if (isDir && item->childCount() > 0) {
        bool anyChildVisible = false;
        for (int i = 0; i < item->childCount(); ++i)
            anyChildVisible |= applyFilterRecursive(item->child(i));
        visible = anyChildVisible;
    } else {
        visible = allowed(status) && matchesLeafFilters();
    }
    item->setHidden(!visible);
    return visible;
}

// ---------------------------------------------------------------------------
// A double click opens the file comparison in its own tab
// ---------------------------------------------------------------------------

void FolderDiffTab::onItemDoubleClicked(QTreeWidgetItem *item)
{
    if (!item || item->data(0, kRoleIsDir).toBool())
        return;

    const QString rel = item->data(0, kRoleRelPath).toString();
    const bool onLeft  = item->data(0, kRoleOnLeft).toBool();
    const bool onRight = item->data(0, kRoleOnRight).toBool();

    Q_EMIT openFileDiffRequested(
        onLeft  ? QDir(m_leftDir).filePath(rel)  : QString(),
        onRight ? QDir(m_rightDir).filePath(rel) : QString());
}

// ---------------------------------------------------------------------------
// Context menu: copy / delete
// ---------------------------------------------------------------------------

void FolderDiffTab::showContextMenu(const QPoint &pos)
{
    QTreeWidgetItem *item = m_tree->itemAt(pos);
    if (!item)
        return;

    const QString rel  = item->data(0, kRoleRelPath).toString();
    const bool isDir   = item->data(0, kRoleIsDir).toBool();
    const bool onLeft  = item->data(0, kRoleOnLeft).toBool();
    const bool onRight = item->data(0, kRoleOnRight).toBool();

    QMenu menu(this);

    if (!isDir && (onLeft || onRight)) {
        connect(menu.addAction(actionIcon(QStringLiteral("Open-Diff-Text-Editor")),
                               tr("Open Diff")),
                &QAction::triggered, this,
                [this, item]() { onItemDoubleClicked(item); });
        menu.addSeparator();
    }

    if (onLeft)
        connect(menu.addAction(actionIcon(QStringLiteral("Copy-File-to-Right")),
                               tr("Copy to Right")),
                &QAction::triggered, this,
                [this, rel]() { copyEntry(rel, true); });
    if (onRight)
        connect(menu.addAction(actionIcon(QStringLiteral("Copy-File-to-Left")),
                               tr("Copy to Left")),
                &QAction::triggered, this,
                [this, rel]() { copyEntry(rel, false); });

    menu.addSeparator();
    if (onLeft)
        connect(menu.addAction(actionIcon(QStringLiteral("Delete-Left-File")),
                               tr("Delete Left")),
                &QAction::triggered, this,
                [this, rel]() { deleteEntry(rel, true); });
    if (onRight)
        connect(menu.addAction(actionIcon(QStringLiteral("Delete-Right-File")),
                               tr("Delete Right")),
                &QAction::triggered, this,
                [this, rel]() { deleteEntry(rel, false); });

    menu.addSeparator();
    // Open in the file manager: for a directory the directory itself, for a
    // file the directory containing it. Which file manager starts is decided
    // by the desktop environment, none is required here.
    if (onLeft)
        connect(menu.addAction(actionIcon(QStringLiteral("Open-Left-File")),
                               tr("Open Left in File Manager")),
                &QAction::triggered, this,
                [this, rel, isDir]() { revealInFileManager(rel, true, isDir); });
    if (onRight)
        connect(menu.addAction(actionIcon(QStringLiteral("Open-Right-File")),
                               tr("Open Right in File Manager")),
                &QAction::triggered, this,
                [this, rel, isDir]() { revealInFileManager(rel, false, isDir); });

    if (!menu.isEmpty())
        menu.exec(m_tree->viewport()->mapToGlobal(pos));
}

void FolderDiffTab::revealInFileManager(const QString &relPath, bool leftSide,
                                        bool isDir)
{
    const QString root = leftSide ? m_leftDir : m_rightDir;
    const QString path = relPath.isEmpty() ? root : QDir(root).filePath(relPath);
    const QString target = isDir ? path : QFileInfo(path).absolutePath();

    const QFileInfo info(target);
    if (!info.exists()) {
        Q_EMIT transientMessage(tr("Folder no longer exists: %1").arg(target),
                                5000);
        return;
    }
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(target))) {
        Q_EMIT transientMessage(
            tr("No file manager is registered for folders."), 5000);
    }
}

void FolderDiffTab::copyEntry(const QString &relPath, bool toRight)
{
    const QString src = QDir(toRight ? m_leftDir : m_rightDir).filePath(relPath);
    const QString dst = QDir(toRight ? m_rightDir : m_leftDir).filePath(relPath);

    const auto ret = QMessageBox::question(
        this, tr("Copy"),
        tr("Copy \"%1\" to the %2 side?\nExisting files will be overwritten.")
            .arg(relPath, toRight ? tr("right") : tr("left")));
    if (ret != QMessageBox::Yes)
        return;

    if (!copyRecursively(src, dst)) {
        QMessageBox::warning(this, tr("Copy"),
                             tr("Copying failed:\n%1").arg(relPath));
    }
    startScan();
}

void FolderDiffTab::deleteEntry(const QString &relPath, bool leftSide)
{
    const QString path =
        QDir(leftSide ? m_leftDir : m_rightDir).filePath(relPath);

    const auto ret = QMessageBox::warning(
        this, tr("Delete"),
        tr("Really delete \"%1\" on the %2 side?\nThis cannot be undone.")
            .arg(relPath, leftSide ? tr("left") : tr("right")),
        QMessageBox::Yes | QMessageBox::Cancel);
    if (ret != QMessageBox::Yes)
        return;

    const QFileInfo fi(path);
    bool ok;
    if (fi.isDir() && !fi.isSymLink())
        ok = QDir(path).removeRecursively();
    else
        ok = QFile::remove(path);

    if (!ok) {
        QMessageBox::warning(this, tr("Delete"),
                             tr("Deleting failed:\n%1").arg(path));
    }
    startScan();
}

// ---------------------------------------------------------------------------
// Export report
// ---------------------------------------------------------------------------

void FolderDiffTab::exportReport()
{
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export Report"), QString(),
        tr("Text files (*.txt);;All files (*)"));
    if (path.isEmpty())
        return;

    QString report;
    report += QStringLiteral("Folder comparison report\n");
    report += QStringLiteral("Left:  %1\n").arg(m_leftDir);
    report += QStringLiteral("Right: %1\n\n").arg(m_rightDir);

    const std::function<void(const FolderDiffEntry &)> walk =
        [&](const FolderDiffEntry &e) {
            if (!e.relPath.isEmpty()
                && e.status != FolderEntryStatus::Identical
                && (!e.isDir || e.children.isEmpty())) {
                report += statusText(e.status, e.isBinary)
                          + QStringLiteral("\t") + e.relPath
                          + QLatin1Char('\n');
            }
            for (const FolderDiffEntry &c : e.children)
                walk(c);
        };
    walk(m_root);

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)
        || file.write(report.toUtf8()) < 0 || !file.commit()) {
        QMessageBox::warning(this, tr("Export Report"),
                             tr("Cannot save file:\n%1").arg(path));
        return;
    }
    Q_EMIT transientMessage(tr("Report exported: %1").arg(path), 3000);
}

// ---------------------------------------------------------------------------
// Recent folder pairs
// ---------------------------------------------------------------------------

void FolderDiffTab::addRecentFolderPair(const QString &l, const QString &r)
{
    const QString entry = l + kRecentSeparator + r;
    QSettings settings;
    QStringList recent =
        settings.value(QStringLiteral("recentFolderPairs")).toStringList();
    recent.removeAll(entry);
    recent.prepend(entry);
    while (recent.size() > 10)
        recent.removeLast();
    settings.setValue(QStringLiteral("recentFolderPairs"), recent);
}
