#pragma once

#include "diff/diffengine.h"
#include "diff/linenormalizer.h"
#include "diff/mergeengine.h" // MergeMode + LineEdit

#include <QWidget>

class ConnectorWidget;
class DiffTextEdit;
class OverviewBar;
class QTimer;

// Zentrales Widget: linkes Pane | Connector | rechtes Pane | Overview-Bar.
// Holds the block list, the highlighting, the synchronised scrolling, the
// Merge-Operationen.
class DiffView : public QWidget
{
    Q_OBJECT
public:
    explicit DiffView(QWidget *parent = nullptr);

    DiffTextEdit *leftEdit() const  { return m_left; }
    DiffTextEdit *rightEdit() const { return m_right; }

    // Sets the text programmatically. This does not start the delayed
    // recomputation but calls recompute(). Clearing the undo history is
    void setText(bool leftSide, const QString &text);

    void setLanguages(Language left, Language right);
    void setOptions(const DiffOptions &options);
    const DiffOptions &options() const { return m_options; }

    // Recompute the comparison from the current content of the documents.
    void recompute();

    const QList<DiffBlock> &blocks() const { return m_blocks; }
    int diffCount() const;
    int currentDiffNumber() const; // 1-basiert; 0 = keiner aktiv

    void gotoNextDiff();
    void gotoPrevDiff();
    void gotoBlock(int blockIndex);

    // Merge operations. leftToRight: source on the left, target on the right.
    void copyAll(bool leftToRight);
    void copySelectedLines(bool leftToRight);
    void copyCurrentBlock(bool leftToRight,
                          MergeMode mode = MergeMode::Replace);

    // The side changed last, either by a merge or by typing. Used by the
    // Undo/Redo-Toolbar-Buttons.
    bool lastChangedSideLeft() const { return m_lastChangedLeft; }

    // Aligned view: draw the gaps of the other side as placeholder rows.
    // This is a rendering layer only. Document, undo and editing are untouched.
    void setAlignedMode(bool aligned);
    bool alignedMode() const { return m_aligned; }

Q_SIGNALS:
    void diffStatusChanged();
    // A note for the status bar, for example "select lines first".
    void statusMessage(const QString &text, int timeoutMs);

private:
    void applyHighlights();
    void applyGapMarkers();
    void applyGhostLines();
    void onScrolled(bool fromLeft);
    void onHScrolled(int value, bool fromLeft);
    int navigationCursorLine(bool *leftSide) const;
    int  mapLine(int line, bool leftToRight) const;
    int  realLineAtVisualRow(int visualRow, bool leftSide) const;
    // Merge support: run a pending recomputation at once, then apply the
    // edits from the bottom up as a SINGLE undo step.
    void flushPendingRediff();
    void applyEdits(QList<MergeEngine::LineEdit> edits, bool targetLeft);
    void updateCurrentBlockFromCursor(bool leftSide);
    void updateSectionFrames();
    QStringList documentLines(bool leftSide) const;
    int  blockIndexAtCursor(bool leftSide) const;

    DiffTextEdit    *m_left      = nullptr;
    DiffTextEdit    *m_right     = nullptr;
    ConnectorWidget *m_connector = nullptr;
    OverviewBar     *m_overview  = nullptr;
    QTimer          *m_rediffTimer = nullptr;

    QList<DiffBlock> m_blocks;
    QList<int>       m_diffBlockIndices; // indices of the non-equal blocks
    int              m_currentBlock = -1;

    // Placeholder prefix sums: prefix[i] = placeholders before real line i.
    // An empty list means the aligned view is off.
    QList<int> m_ghostPrefixLeft;
    QList<int> m_ghostPrefixRight;

    DiffOptions m_options;
    Language    m_leftLang  = Language::None;
    Language    m_rightLang = Language::None;

    bool m_syncing         = false;
    bool m_applyingEdit    = false;
    bool m_lastChangedLeft = true;
    bool m_aligned         = false;
};
