#pragma once

#include "comparetab.h"

#include <QSet>

class DiffView;
class QFileSystemWatcher;
class QLabel;
class QLineEdit;

// A file comparison as a tab: the view plus headers, banners, search,
// loading and saving, the file watcher and the pane context menus.
class FileDiffTab : public CompareTab
{
    Q_OBJECT
public:
    explicit FileDiffTab(QWidget *parent = nullptr);

    // --- Interface used by the window ---
    void openFile(bool leftSide, const QString &path);
    bool saveFile(bool leftSide, bool saveAs);
    void saveModified();
    void swapSides();
    void exportPatch();
    void showFindBar();
    void undoLastChange();
    void redoLastChange();
    bool canUndo() const;
    bool canRedo() const;
    void setReadOnly(bool readOnly);
    bool hasDifferences() const;

    DiffView *view() const { return m_view; }

    // CompareTab
    QString tabTitle() const override;
    QString currentStatusText() const override;
    void optionsChanged() override;
    void reloadContent() override;
    bool maybeClose() override;

Q_SIGNALS:
    // Both sides received a directory by drag and drop.
    void openFolderDiffRequested(const QString &leftDir, const QString &rightDir);
    void undoStateChanged();

private:
    struct FileMeta {
        QString path;
        QString eol = QStringLiteral("\n"); // "\n", "\r\n" or "\r"
        bool utf8Bom = false;
        bool latin1  = false;
        bool loaded  = false;
    };

    void updateStatus();
    void updateEolBanner();
    void updateHeader(bool leftSide);
    void addRecentPair();
    void findNext(bool backwards = false);
    void updateFileWatcher();
    void onFileChangedOnDisk(const QString &path);
    void onFolderDropped(bool leftSide, const QString &dir);
    void applySyntaxHighlighting(bool leftSide);
    void extendPaneContextMenu(bool leftSide, QMenu *menu);

    DiffView *m_view = nullptr;

    FileMeta m_leftMeta;
    FileMeta m_rightMeta;

    // A dropped directory is remembered until both sides have one.
    QString m_pendingDirLeft;
    QString m_pendingDirRight;

    QLabel *m_leftHeader  = nullptr;
    QLabel *m_rightHeader = nullptr;
    QLabel *m_eolBanner   = nullptr;

    QFileSystemWatcher *m_watcher = nullptr;
    QWidget      *m_fileChangedBanner = nullptr;
    QLabel       *m_fileChangedLabel  = nullptr;
    QSet<QString> m_expectSelfChange;
    QSet<QString> m_pendingReloads;

    QWidget   *m_findBar  = nullptr;
    QLineEdit *m_findEdit = nullptr;

    bool m_readOnly = false;
};
