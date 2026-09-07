#pragma once

#include "comparetab.h"

#include "diff/merge3engine.h"

#include <QHash>

class DiffTextEdit;
class QLabel;
class QPushButton;

// The three-way merge as a tab. Three read-only panes on top show base,
// mine and theirs, the editable result sits below. A change made by one
// side only is taken automatically. A conflict is resolved by one of the
// choice buttons, or by editing the result directly.
//
// The ignore options are deliberately NOT applied here. A merge always
// works on the raw text.
class MergeTab : public CompareTab
{
    Q_OBJECT
public:
    MergeTab(const QString &basePath, const QString &minePath,
             const QString &theirsPath, const QString &outputPath,
             QWidget *parent = nullptr);

    // CompareTab
    QString tabTitle() const override;
    QString currentStatusText() const override;
    void reloadContent() override;
    bool maybeClose() override;

    // For the exit code: are conflicts open or is the result unsaved?
    bool hasUnresolvedWork() const;

private:
    void setupUi();
    void loadFiles();
    void recomputeMerge(bool keepScroll);
    void applyHighlights();
    void applySyncScroll(DiffTextEdit *from);
    void gotoConflict(int listIndex);
    void gotoNextConflict();
    void gotoPrevConflict();
    void chooseForCurrent(Merge3Choice choice);
    void saveResult(bool saveAs);
    void updateStatus();
    int  resultChunkCount(int chunkIdx) const;
    int  paneLine(const Merge3Chunk &c, const DiffTextEdit *pane) const;
    int  paneCount(const Merge3Chunk &c, const DiffTextEdit *pane) const;

    QString m_basePath, m_minePath, m_theirsPath, m_outputPath;
    QStringList m_base, m_mine, m_theirs;
    bool m_mineCrLf = false;

    QList<Merge3Chunk> m_chunks;
    QHash<int, Merge3Choice> m_choices;
    Merge3Output m_output;
    QList<int> m_conflictChunks;   // Chunk-Indizes aller Konflikte
    int m_currentConflict = -1;    // Index in m_conflictChunks

    DiffTextEdit *m_baseEdit   = nullptr;
    DiffTextEdit *m_mineEdit   = nullptr;
    DiffTextEdit *m_theirsEdit = nullptr;
    DiffTextEdit *m_resultEdit = nullptr;
    QLabel       *m_counterLabel = nullptr;
    QList<QPushButton *> m_choiceButtons;

    bool m_applyingResult = false; // programmatisches Setzen des Ergebnisses
    bool m_resultEdited   = false; // edited by hand since the last assembly
    bool m_dirty          = false; // unsaved changes
    bool m_syncing        = false;
    QString m_statusText;
};
