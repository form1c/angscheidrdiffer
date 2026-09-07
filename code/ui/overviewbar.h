#pragma once

#include "diff/diffengine.h"

#include <QWidget>

// Narrow map beside the panes. It shows every block as a coloured stripe,
// proportional to the file length. A click jumps to the nearest difference.
class OverviewBar : public QWidget
{
    Q_OBJECT
public:
    explicit OverviewBar(QWidget *parent = nullptr);

    void setBlocks(const QList<DiffBlock> &blocks);
    void setCurrentDiff(int blockIndex); // -1 = keiner

Q_SIGNALS:
    // Index of the nearest non-equal block in the block list.
    void diffClicked(int blockIndex);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;

private:
    int virtualTotal() const;
    int blockVirtualStart(int index) const;

    QList<DiffBlock> m_blocks;
    int m_currentBlock = -1;
};
