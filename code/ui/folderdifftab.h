#pragma once

#include "comparetab.h"

#include "diff/foldercompare.h"

#include <QFutureWatcher>
#include <QSet>

#include <atomic>

class QAction;
class QCheckBox;
class QDateEdit;
class QLabel;
class QLineEdit;
class QTreeWidget;
class QTreeWidgetItem;

// The folder comparison as a tab. The ignore options come from the global
// settings, shown in the Options menu of the window. Only the controls that
// belong to this tab stay local: filters, exclude patterns and quick mode.
class FolderDiffTab : public CompareTab
{
    Q_OBJECT
public:
    FolderDiffTab(const QString &leftDir, const QString &rightDir,
                  QWidget *parent = nullptr);
    ~FolderDiffTab() override;

    // CompareTab
    QString tabTitle() const override;
    QString currentStatusText() const override;
    void optionsChanged() override { startScan(); }
    void reloadContent() override  { startScan(); }
    bool maybeClose() override;

Q_SIGNALS:
    // Double click on a file row. An empty path means that side is missing.
    void openFileDiffRequested(const QString &leftPath, const QString &rightPath);

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    void setupUi();
    void startScan();
    void rememberTreeState();
    void restoreTreeState();
    void cancelScan();
    void onScanFinished();
    void populateTree();
    void applyFilters();
    bool applyFilterRecursive(QTreeWidgetItem *item);
    void onItemDoubleClicked(QTreeWidgetItem *item);
    void showContextMenu(const QPoint &pos);
    void copyEntry(const QString &relPath, bool toRight);
    void revealInFileManager(const QString &relPath, bool leftSide, bool isDir);
    void deleteEntry(const QString &relPath, bool leftSide);
    void exportReport();
    void setRoot(bool leftSide, const QString &dir);
    FolderCompareSettings currentSettings() const;
    static void addRecentFolderPair(const QString &l, const QString &r);

    QString m_leftDir;
    QString m_rightDir;

    QTreeWidget *m_tree      = nullptr;
    QLabel      *m_pathLabel = nullptr;
    QString      m_statusText;

    QAction   *m_quickCompareAction = nullptr;
    QLineEdit *m_excludeEdit        = nullptr;

    // Filter drop-down. These filter the view and need no new scan.
    QLineEdit *m_includeEdit        = nullptr;
    QCheckBox *m_modifiedSinceCheck = nullptr;
    QDateEdit *m_modifiedSinceEdit  = nullptr;

    QAction *m_showIdenticalAction = nullptr;
    QAction *m_showDifferentAction = nullptr;
    QAction *m_showOnlyLeftAction  = nullptr;
    QAction *m_showOnlyRightAction = nullptr;

    QAction *m_refreshAction = nullptr;
    QAction *m_cancelAction  = nullptr;

    // State of the tree across scans, so that deleting, copying or
    // refreshing does not collapse what the user had opened.
    QSet<QString> m_expandedPaths;
    QString m_currentPath;
    int m_treeScroll = -1;
    bool m_hasSavedTreeState = false;
    bool m_freshComparison = false; // root changed, do not restore the state

    FolderDiffEntry m_root;
    QFutureWatcher<FolderDiffEntry> m_watcher;
    std::atomic<bool> m_cancelRequested{false};
    bool m_scanning = false;
};
