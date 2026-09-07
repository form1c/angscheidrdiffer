#include "overviewbar.h"

#include <QMouseEvent>
#include <QPainter>

OverviewBar::OverviewBar(QWidget *parent)
    : QWidget(parent)
{
    setFixedWidth(16);
    setCursor(Qt::PointingHandCursor);
    setToolTip(tr("Overview — click to jump to a difference"));
}

void OverviewBar::setBlocks(const QList<DiffBlock> &blocks)
{
    m_blocks = blocks;
    update();
}

void OverviewBar::setCurrentDiff(int blockIndex)
{
    m_currentBlock = blockIndex;
    update();
}

int OverviewBar::virtualTotal() const
{
    int total = 0;
    for (const DiffBlock &b : m_blocks)
        total += qMax(b.leftCount, b.rightCount);
    return qMax(total, 1);
}

int OverviewBar::blockVirtualStart(int index) const
{
    int pos = 0;
    for (int i = 0; i < index; ++i)
        pos += qMax(m_blocks[i].leftCount, m_blocks[i].rightCount);
    return pos;
}

void OverviewBar::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), palette().color(QPalette::Window));

    if (m_blocks.isEmpty())
        return;

    const bool dark = palette().color(QPalette::Base).lightness() < 128;
    const QColor added   = dark ? QColor(0x3c, 0x6e, 0x3c) : QColor(0x66, 0xbb, 0x66);
    const QColor removed = dark ? QColor(0x7a, 0x3c, 0x3c) : QColor(0xdd, 0x66, 0x66);
    const QColor changed = dark ? QColor(0x7a, 0x7a, 0x30) : QColor(0xcc, 0xbb, 0x44);
    const QColor moved   = dark ? QColor(0x3c, 0x5a, 0x8a) : QColor(0x66, 0x99, 0xdd);
    const QColor trivial = dark ? QColor(0x50, 0x50, 0x50) : QColor(0xc8, 0xc8, 0xc8);

    const double scale = double(height()) / virtualTotal();
    int vpos = 0;

    for (int i = 0; i < m_blocks.size(); ++i) {
        const DiffBlock &b = m_blocks[i];
        const int span = qMax(b.leftCount, b.rightCount);
        if (b.type != BlockType::Equal) {
            QColor c;
            if (b.trivial) {
                c = trivial;
            } else if (b.movedPartner >= 0) {
                c = moved;
            } else switch (b.type) {
            case BlockType::OnlyRight: c = added;   break;
            case BlockType::OnlyLeft:  c = removed; break;
            default:                   c = changed; break;
            }
            const int y = int(vpos * scale);
            const int h = qMax(2, int(qMax(span, 1) * scale));
            p.fillRect(2, y, width() - 4, h, c);
            if (i == m_currentBlock) {
                p.setPen(palette().color(QPalette::Highlight));
                p.drawRect(0, y - 1, width() - 1, h + 1);
            }
        }
        vpos += span;
    }
}

void OverviewBar::mousePressEvent(QMouseEvent *event)
{
    if (m_blocks.isEmpty())
        return;

    const int clickedVirtual = int(double(event->pos().y()) / height() * virtualTotal());

    // Find the nearest non-equal block.
    int best = -1;
    int bestDist = INT_MAX;
    int vpos = 0;
    for (int i = 0; i < m_blocks.size(); ++i) {
        const int span = qMax(m_blocks[i].leftCount, m_blocks[i].rightCount);
        if (m_blocks[i].type != BlockType::Equal) {
            int dist = 0;
            if (clickedVirtual < vpos)
                dist = vpos - clickedVirtual;
            else if (clickedVirtual >= vpos + span)
                dist = clickedVirtual - (vpos + span) + 1;
            if (dist < bestDist) {
                bestDist = dist;
                best = i;
            }
        }
        vpos += span;
    }

    if (best >= 0)
        Q_EMIT diffClicked(best);
}
