#pragma once

#include <QMainWindow>

class CompareTab;
class FileDiffTab;
class FolderDiffTab;
class MergeTab;
class QAction;
class QLabel;
class QMenu;
class QSpinBox;
class QTabWidget;

// The single main window. Every comparison is a tab inside it.
// One toolbar, with Use Both and Options as drop-down buttons.
class AppWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit AppWindow(QWidget *parent = nullptr);

    FileDiffTab   *addFileTab(const QString &leftPath = QString(),
                              const QString &rightPath = QString(),
                              int insertAfterIndex = -1);
    FolderDiffTab *addFolderTab(const QString &leftDir, const QString &rightDir);
    MergeTab      *addMergeTab(const QString &basePath, const QString &minePath,
                               const QString &theirsPath,
                               const QString &outputPath);

    void setReadOnly(bool readOnly);
    bool hasAnyDifferences() const;

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void setupActions();
    void connectTab(CompareTab *tab);
    FileDiffTab   *currentFileTab() const;
    FolderDiffTab *currentFolderTab() const;
    CompareTab    *currentTab() const;
    void onCurrentTabChanged();
    void updateActionStates();
    void updateTabTitle(CompareTab *tab);
    void broadcastOptionsChanged();
    void compareFoldersDialog();
    void mergeFilesDialog();
    void showAboutDialog();
    void rebuildRecentMenus();

    QTabWidget *m_tabs        = nullptr;
    QLabel     *m_statusLabel = nullptr;

    // Actions for a file comparison, disabled while a folder tab is in front
    QAction *m_openLeftAction   = nullptr;
    QAction *m_openRightAction  = nullptr;
    QAction *m_saveLeftAction   = nullptr;
    QAction *m_saveRightAction  = nullptr;
    QAction *m_saveBothAction   = nullptr;
    QAction *m_saveLeftAsAction  = nullptr;
    QAction *m_saveRightAsAction = nullptr;
    QAction *m_swapAction       = nullptr;
    QAction *m_exportAction     = nullptr;
    QAction *m_findAction       = nullptr;
    QAction *m_undoAction       = nullptr;
    QAction *m_redoAction       = nullptr;
    QAction *m_prevAction       = nullptr;
    QAction *m_nextAction       = nullptr;
    QAction *m_sectionRightAction = nullptr;
    QAction *m_sectionLeftAction  = nullptr;
    QAction *m_allRightAction   = nullptr;
    QAction *m_allLeftAction    = nullptr;
    QAction *m_selRightAction   = nullptr;
    QAction *m_selLeftAction    = nullptr;
    QAction *m_bothLeftFirstAction  = nullptr;
    QAction *m_bothRightFirstAction = nullptr;
    QAction *m_bothLeftLastAction   = nullptr;
    QAction *m_bothRightLastAction  = nullptr;
    QAction *m_reloadAction     = nullptr;

    // Globale Options-Toggles (QSettings)
    QAction  *m_ignoreWsAction       = nullptr;
    QAction  *m_ignoreEolAction      = nullptr;
    QAction  *m_ignoreCommentsAction = nullptr;
    QAction  *m_ignoreCaseAction     = nullptr;
    QAction  *m_showWsAction         = nullptr;
    QAction  *m_wrapAction           = nullptr;
    QAction  *m_alignedAction        = nullptr;
    QSpinBox *m_ignoreFirstSpin      = nullptr;

    QMenu *m_recentMenu        = nullptr;
    QMenu *m_recentFoldersMenu = nullptr;

    bool m_readOnly = false;
    bool m_restoringOptions = false;
};
