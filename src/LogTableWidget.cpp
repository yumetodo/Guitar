#include "LogTableWidget.h"
#include <QDebug>
#include <QEvent>
#include <QPainter>
#include <QStyledItemDelegate>
#include <math.h>
#include "MainWindow.h"

struct LogTableWidget::Private {
	MainWindow *mainwindow;
};

class MyItemDelegate : public QStyledItemDelegate {
private:
	MainWindow *mainwindow() const
	{
		LogTableWidget *w = dynamic_cast<LogTableWidget *>(parent());
		Q_ASSERT(w);
		MainWindow *mw = w->pv->mainwindow;
		Q_ASSERT(mw);
		return mw;
	}
public:
	explicit MyItemDelegate(QObject *parent = Q_NULLPTR)
		: QStyledItemDelegate(parent)
	{
	}

	void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const
	{
		QStyledItemDelegate::paint(painter, option, index);
	}
};

LogTableWidget::LogTableWidget(QWidget *parent)
	: QTableWidget(parent)
{
	pv = new Private;
	setItemDelegate(new MyItemDelegate(this));

}

LogTableWidget::~LogTableWidget()
{
	delete pv;
}

bool LogTableWidget::event(QEvent *e)
{
	if (e->type() == QEvent::Polish) {
		pv->mainwindow = qobject_cast<MainWindow *>(window());
		Q_ASSERT(pv->mainwindow);
	}
	return QTableWidget::event(e);
}


void drawBranch(QPainterPath *path, double x0, double y0, double x1, double y1, double r, bool bend_early)
{
	const double k = 0.55228475;
	if (x0 == x1) {
		path->moveTo(x0, y0);
		path->lineTo(x1, y1);
	} else {
		double ym = bend_early ? (y0 + r) : (y1 - r);
		double h = fabs(y1 - y0);
		double w = fabs(x1 - x0);
		if (r > h / 2) r = h / 2;
		if (r > w / 2) r = w / 2;
		double s = r;
		if (x0 > x1) r = -r;
		if (y0 > y1) s = -s;

		if (0) {
			path->moveTo(x0, y0);
			path->lineTo(x0, ym - s);
			path->cubicTo(x0, ym - s + s * k, x0 + r - r * k, ym, x0 + r, ym);
			path->lineTo(x1 - r, ym);
			path->cubicTo(x1 - r + r * k, ym, x1, ym + s - s * k, x1, ym + s);
			path->lineTo(x1, y1);
		} else {
			if (bend_early) {
				path->moveTo(x0, y0);
				path->cubicTo(x0, ym, x1, ym, x1, ym + ym - y0);
				path->lineTo(x1, y1);
			} else {
				path->moveTo(x0, y0);
				path->lineTo(x0, ym + ym - y1);
				path->cubicTo(x0, ym, x1, ym, x1, y1);
			}
		}
	}
}

void LogTableWidget::paintEvent(QPaintEvent *e)
{
	if (rowCount() < 1) return;

	QTableWidget::paintEvent(e);

	MainWindow *mw = pv->mainwindow;

	QPainter pr(viewport());
	pr.setRenderHint(QPainter::Antialiasing);
	pr.setBrush(QBrush(QColor(255, 255, 255)));

	Git::CommitItemList const *list = mw->logs();

	int indent_span = 16;

	auto ItemRect = [&](int row){
		QRect r;
		QTableWidgetItem *p = item(row, 0);
		if (p) {
			r = visualItemRect(p);
		}
		return r;
	};

	int line_width = 2;

	auto ItemPoint = [&](int depth, QRect const &rect){
		int h = rect.height();
		double n = h / 2.0;
		double x = floor(rect.x() + n + depth * indent_span);
		double y = floor(rect.y() + n);
		return QPointF(x, y);
	};

	auto SetPen = [&](QPainter *pr, int level, bool /*continued*/){
		QColor c = mw->color(level);
		Qt::PenStyle s = Qt::SolidLine;
		pr->setPen(QPen(c, line_width, s));
	};

	auto DrawLine = [&](size_t index, int itemrow){
		QRect rc1;
		if (index < list->size()) {
			Git::CommitItem const &item1 = list->at(index);
			rc1 = ItemRect(itemrow);
			QPointF pt1 = ItemPoint(item1.marker_depth, rc1);
			double halfheight = rc1.height() / 2.0;
			for (TreeLine const &line : item1.parent_lines) {
				if (line.depth >= 0) {
					QPainterPath *path = nullptr;
					Git::CommitItem const &item2 = list->at(line.index);
					QRect rc2 = ItemRect(line.index);
					if (index + 1 == line.index || line.depth == item1.marker_depth || line.depth == item2.marker_depth) {
						QPointF pt2 = ItemPoint(line.depth, rc2);
						if (pt2.y() > 0) {
							path = new QPainterPath();
							drawBranch(path, pt1.x(), pt1.y(), pt2.x(), pt2.y(), halfheight, line.bend_early);
						}
					} else {
						QPointF pt3 = ItemPoint(item2.marker_depth, rc2);
						if (pt3.y() > 0) {
							path = new QPainterPath();
							QRect rc3 = ItemRect(itemrow + 1);
							QPointF pt2 = ItemPoint(line.depth, rc3);
							drawBranch(path, pt1.x(), pt1.y(), pt2.x(), pt2.y(), halfheight, true);
							drawBranch(path, pt2.x(), pt2.y(), pt3.x(), pt3.y(), halfheight, false);
						}
					}
					if (path) {
						SetPen(&pr, line.color_number, false);
						pr.drawPath(*path);
						delete path;
					}
				}
			}
		}
		return rc1.y();
	};

	auto DrawMark = [&](size_t index, int row){
		double x, y;
		y = 0;
		if (index < list->size()) {
			Git::CommitItem const &item = list->at(index);
			QRect rc = ItemRect(row);
			QPointF pt = ItemPoint(item.marker_depth, rc);
			double r = 4;
			x = pt.x() - r;
			y = pt.y() - r;
			SetPen(&pr, item.marker_depth, false);
			if (item.resolved) {
				// ◯
				pr.drawEllipse(x, y, r * 2, r * 2);
			} else {
				// ▽
				QPainterPath path;
				path.moveTo(pt.x(), pt.y() + r);
				path.lineTo(pt.x() - r, pt.y() - r);
				path.lineTo(pt.x() + r, pt.y() - r);
				path.lineTo(pt.x(), pt.y() + r);
				pr.drawPath(path);
			}
		}
		return y;
	};

	// draw lines

	pr.setOpacity(0.5);
	pr.setBrush(Qt::NoBrush);

	for (size_t i = 0; i < list->size(); i++) {
		double y = DrawLine(i, i);
		if (y >= height()) break;
	}

	// draw marks

	pr.setOpacity(1);
	pr.setBrush(Qt::white);

	for (size_t i = 0; i < list->size(); i++) {
		double y = DrawMark(i, i);
		if (y >= height()) break;
	}
}

