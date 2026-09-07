#include "connectorwidget.h"
#include "difftextedit.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

namespace {
constexpr int kArrowSize = 12;
}

ConnectorWidget::ConnectorWidget(DiffTextEdit *left, DiffTextEdit *right,
                                 QWidget *parent)
    : QWidget(parent), m_left(left), m_right(right)
{
    setFixedWidth(2 * kArrowSize + 12);
    setMouseTracking(true);
}

void ConnectorWidget::setBlocks(const QList<DiffBlock> &blocks)
{
    m_blocks = blocks;
    update();
}

QList<ConnectorWidget::ArrowHit> ConnectorWidget::visibleArrows() const
{
    QList<ArrowHit> arrows;
    for (int i = 0; i < m_blocks.size(); ++i) {
        const DiffBlock &b = m_blocks[i];
        if (b.type == BlockType::Equal)
            continue;

        const int yLeft  = m_left->lineTopY(b.leftStart);
        const int yRight = m_right->lineTopY(b.rightStart);

        // "→" sits on the left of the stripe, "←" on the right.
        const QRect toRight(4, yLeft + 2, kArrowSize, kArrowSize);
        const QRect toLeft(width() - kArrowSize - 4, yRight + 2,
                           kArrowSize, kArrowSize);

        if (toRight.bottom() >= 0 && toRight.top() <= height())
            arrows.append({toRight, i, true});
        if (toLeft.bottom() >= 0 && toLeft.top() <= height())
            arrows.append({toLeft, i, false});
    }
    return arrows;
}

void ConnectorWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), palette().color(QPalette::Window));
    p.setRenderHint(QPainter::Antialiasing);

    const QColor arrowColor = palette().color(QPalette::ButtonText);

    const QList<ArrowHit> arrows = visibleArrows();
    for (const ArrowHit &a : arrows) {
        QPainterPath path;
        const QRectF r = a.rect;
        if (a.leftToRight) {
            path.moveTo(r.left(), r.top());
            path.lineTo(r.right(), r.center().y());
            path.lineTo(r.left(), r.bottom());
        } else {
            path.moveTo(r.right(), r.top());
            path.lineTo(r.left(), r.center().y());
            path.lineTo(r.right(), r.bottom());
        }
        path.closeSubpath();
        p.fillPath(path, arrowColor);
    }
}

void ConnectorWidget::mousePressEvent(QMouseEvent *event)
{
    const QList<ArrowHit> arrows = visibleArrows();
    for (const ArrowHit &a : arrows) {
        if (a.rect.adjusted(-2, -2, 2, 2).contains(event->pos())) {
            Q_EMIT copyBlockRequested(a.blockIndex, a.leftToRight);
            return;
        }
    }
}
