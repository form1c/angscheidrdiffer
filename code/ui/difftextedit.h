#pragma once

#include <QHash>
#include <QPlainTextEdit>
#include <QSet>

// Character-level highlight within a line, used by the custom rendering.
struct CharHighlight {
    int line   = 0;
    int start  = 0;
    int length = 0;
    QColor color;
};

// One pane of a comparison: a plain text editor with a line number gutter,
// difference highlighting and drag and drop of files.
//
// Placeholder mode, a pure rendering layer: when a placeholder table is set
// and word wrap is off, the widget takes over the drawing itself. Real lines
// are drawn with a vertical offset, and between them sit hatched placeholder
// rows that cannot be edited. Document, undo and keyboard handling stay
// entirely with the base class.
class DiffTextEdit : public QPlainTextEdit
{
    Q_OBJECT
public:
    explicit DiffTextEdit(QWidget *parent = nullptr);

    // Difference highlighting for the fallback mode, via extra selections.
    void setDiffSelections(const QList<QTextEdit::ExtraSelection> &selections);

    // --- Ghost-Modus ---
    // ghostAfter maps a real line to the number of placeholders after it.
    // Leere Tabelle deaktiviert den Ghost-Modus.
    void setGhostLines(const QHash<int, int> &ghostAfter);
    // Line backgrounds and within-line colours for the custom rendering.
    void setDiffLineColors(const QHash<int, QColor> &lineBackgrounds);
    void setCharHighlights(const QList<CharHighlight> &highlights);
    bool ghostModeActive() const;

    // Frame around the current section. A startLine of -1 switches it off.
    void setSectionFrame(int startLine, int lineCount);

    // Lines AFTER which the other side has additional lines. A small wedge
    // is drawn in the gutter there. -1 means a gap before line 0.
    void setGapMarkers(const QSet<int> &linesWithGapAfter);

    // Selected line range. Without a selection this is the cursor line.
    void selectedLineRange(int *start, int *count) const;

    int  firstVisibleLine() const;
    void scrollToLine(int line);

    // Y position of the top edge of a line in viewport coordinates, or -1
    // when the line lies outside the visible area.
    int lineTopY(int line) const;

    int lineNumberAreaWidth() const;
    void lineNumberAreaPaintEvent(QPaintEvent *event);

Q_SIGNALS:
    void fileDropped(const QString &path);
    void folderDropped(const QString &path);
    // Emitted before the standard context menu is shown, so that receivers
    // can append their own actions.
    void contextMenuAboutToShow(QMenu *menu);

protected:
    void resizeEvent(QResizeEvent *event) override;
    void scrollContentsBy(int dx, int dy) override;
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    void updateLineNumberAreaWidth();
    void updateLineNumberArea(const QRect &rect, int dy);
    // Placeholder mode: widen the scroll range so that the last real lines
    // stay reachable even when placeholders follow them.
    void updateGhostScrollRange();
    // Scroll bar value for a given top line: without wrapping the line
    // number itself, with wrapping the visual row.
    int visualRowForLine(int line) const;

    // Placeholder mode: the visible rows, real ones and placeholders.
    struct VisualSlot {
        int  y    = 0;     // top edge within the viewport
        bool ghost = false;
        int  line = 0;     // real line; for a placeholder its anchor line
    };
    QList<VisualSlot> visibleSlots() const;
    QTextCursor cursorForGhostPosition(const QPoint &pos) const;
    void drawSectionFrame(QPainter &painter);

    QWidget *m_lineNumberArea = nullptr;
    QList<QTextEdit::ExtraSelection> m_diffSelections;
    QSet<int> m_gapMarkers;
    QHash<int, int> m_ghostAfter;
    QHash<int, QColor> m_lineBg;
    QList<CharHighlight> m_charHighlights;
    int m_frameStart = -1;
    int m_frameCount = 0;
    int m_baseScrollMax = 0;      // maximum the base class had set
    bool m_adjustingRange = false;
};
