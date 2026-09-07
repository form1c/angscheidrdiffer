#pragma once

#include "diff/diffengine.h"

#include <QWidget>

class DiffTextEdit;

// Schmaler Streifen zwischen den beiden Panes: zeichnet pro sichtbarem
// two arrow buttons per visible block. "→" copies the block to the right,
// "←" copies it to the left.
class ConnectorWidget : public QWidget
{
    Q_OBJECT
public:
    ConnectorWidget(DiffTextEdit *left, DiffTextEdit *right,
                    QWidget *parent = nullptr);

    void setBlocks(const QList<DiffBlock> &blocks);

Q_SIGNALS:
    void copyBlockRequested(int blockIndex, bool leftToRight);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;

private:
    struct ArrowHit {
        QRect rect;
        int   blockIndex;
        bool  leftToRight;
    };

    QList<ArrowHit> visibleArrows() const;

    DiffTextEdit *m_left;
    DiffTextEdit *m_right;
    QList<DiffBlock> m_blocks;
};
