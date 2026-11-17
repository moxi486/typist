#include "MermaidInternal.h"

#include <QFontMetricsF>
#include <QPainter>
#include <QRegularExpression>
#include <QtMath>
#include <algorithm>
#include <array>

namespace typist::render::mermaid_detail {
namespace {
constexpr int maxPoints = 32;

struct Point {
  QString name;
  QPointF value;
};

struct Chart {
  QString title;
  std::array<QString, 2> xAxis, yAxis;
  std::array<QString, 4> quadrants;
  QList<Point> points;
};

QString chartLabel(const QString& token) {
  static const QRegularExpression text(R"rx(^(?:"[^"]+"|[^"]+)$)rx");
  if (!text.match(token.trimmed()).hasMatch()) return {};
  return label(token).simplified();
}

bool readAxis(const QString& text, std::array<QString, 2>& ends) {
  static const QString name = QStringLiteral(R"rx("[^"]+"|(?:(?!-->)[^"])+)rx");
  static const QRegularExpression axis(QStringLiteral(R"rx(^(%1)(?:\s*-->\s*(%1))?$)rx").arg(name));
  const auto match = axis.match(text);
  if (!match.hasMatch() || !ends[0].isEmpty()) return false;
  ends[0] = chartLabel(match.captured(1));
  ends[1] = chartLabel(match.captured(2));
  return !ends[0].isEmpty() && (match.capturedStart(2) < 0 || !ends[1].isEmpty());
}

bool readChart(Chart& chart, const QStringList& lines) {
  static const QRegularExpression title(R"(^title\s+(.+)$)");
  static const QRegularExpression axis(R"(^([xy])-axis\s+(.+)$)");
  static const QRegularExpression quadrant(R"(^quadrant-([1-4])\s+(.+)$)");
  static const QString number = QStringLiteral(R"((?:[0-9]+(?:\.[0-9]*)?|\.[0-9]+))");
  static const QRegularExpression point(
      QStringLiteral(R"rx(^("[^"]+"|[^":]+?)\s*:\s*\[\s*(%1)\s*,\s*(%1)\s*\]$)rx").arg(number));
  for (qsizetype i = 1; i < lines.size(); ++i) {
    const auto line = lines.at(i).trimmed();
    if (line.startsWith("%%{")) return false;
    if (line.isEmpty() || line.startsWith("%%")) continue;
    if (const auto heading = title.match(line); heading.hasMatch()) {
      if (!chart.title.isEmpty()) return false;
      chart.title = chartLabel(heading.captured(1));
      if (chart.title.isEmpty()) return false;
    } else if (const auto sides = axis.match(line); sides.hasMatch()) {
      auto& ends = sides.captured(1) == "x" ? chart.xAxis : chart.yAxis;
      if (!readAxis(sides.captured(2), ends)) return false;
    } else if (const auto section = quadrant.match(line); section.hasMatch()) {
      auto& name = chart.quadrants[section.captured(1).toInt() - 1];
      if (!name.isEmpty()) return false;
      name = chartLabel(section.captured(2));
      if (name.isEmpty()) return false;
    } else if (const auto position = point.match(line); position.hasMatch()) {
      if (chart.points.size() >= maxPoints) return false;
      bool xOk, yOk;
      const qreal x = position.captured(2).toDouble(&xOk), y = position.captured(3).toDouble(&yOk);
      const QString name = chartLabel(position.captured(1));
      if (name.isEmpty() || !xOk || !yOk || x < 0 || y < 0 || x > 1 || y > 1) return false;
      chart.points.append({name, QPointF(x, y)});
    } else {
      return false;
    }
  }
  return true;
}

struct Caption {
  QString text;
  QRectF rect;
};

QRectF positionLabel(QPointF point, QSizeF size, const QRectF& bounds,
                     const QList<QRectF>& occupied) {
  const qreal dx = size.width() / 2 + 10, dy = size.height() / 2 + 9;
  for (const auto offset :
       {QPointF(dx, 0), QPointF(-dx, 0), QPointF(0, dy), QPointF(0, -dy), QPointF(dx, dy),
        QPointF(-dx, dy), QPointF(dx, -dy), QPointF(-dx, -dy)}) {
    QRectF candidate(QPointF(), size);
    candidate.moveCenter(point + offset);
    if (bounds.contains(candidate) &&
        std::none_of(occupied.cbegin(), occupied.cend(),
                     [&candidate](const QRectF& other) { return candidate.intersects(other); }))
      return candidate;
  }
  return {};
}

QImage drawChart(const Chart& chart, bool dark) {
  constexpr int width = 550, side = 440, left = 78, margin = 22;
  const bool hasPoints = !chart.points.isEmpty();
  const bool hasX = !chart.xAxis[0].isEmpty();
  const int titleHeight = chart.title.isEmpty() ? 0 : 37;
  const int xHeight = hasX ? 31 : 0;
  const QRectF plot(left, margin + titleHeight + (hasPoints ? 0 : xHeight), side, side);
  const qreal half = side / 2.;
  const std::array<QRectF, 4> cells{QRectF(plot.center().x(), plot.top(), half, half),
                                    QRectF(plot.topLeft(), QSizeF(half, half)),
                                    QRectF(plot.left(), plot.center().y(), half, half),
                                    QRectF(plot.center(), QSizeF(half, half))};
  const std::array<QColor, 4> fills{
      QColor(dark ? "#29413d" : "#e7f3eb"), QColor(dark ? "#2c4055" : "#e8f1fc"),
      QColor(dark ? "#3a3748" : "#f0edf7"), QColor(dark ? "#473c36" : "#f7eee6")};
  const QColor ink(dark ? "#eaf1f8" : "#233451");
  const QColor axisInk(dark ? "#bac9db" : "#4e6079");
  const QColor border(dark ? "#6b7b93" : "#a1b0c6");
  const QColor marker(dark ? "#9ad5ff" : "#245c93");
  const QColor paper(dark ? "#262b31" : "#ffffff");
  auto pointBackground = paper;
  pointBackground.setAlpha(208);
  auto cellFont = diagramFont();
  cellFont.setBold(true);
  auto titleFont = cellFont;
  titleFont.setPointSizeF(15);
  auto smallFont = diagramFont();
  smallFont.setPointSizeF(9.5);
  const QFontMetricsF cellText(cellFont), smallText(smallFont), titleText(titleFont);

  QList<Caption> captions;
  QList<QPointF> centers;
  QList<QRectF> occupied;
  for (size_t i = 0; i < cells.size(); ++i) {
    const auto text = cellText.elidedText(chart.quadrants[i], Qt::ElideRight, half - 24);
    if (text.isEmpty()) continue;
    const auto& cell = cells[i];
    QRectF box(QPointF(), QSizeF(cellText.horizontalAdvance(text) + 12, cellText.height() + 8));
    box.moveCenter(QPointF(cell.center().x(), hasPoints ? cell.top() + 21 : cell.center().y()));
    captions.append({text, box});
    occupied.append(box);
  }
  for (const auto& point : chart.points) {
    const QPointF at(plot.left() + point.value.x() * side, plot.bottom() - point.value.y() * side);
    centers.append(at);
    occupied.append(QRectF(at - QPointF(7, 7), QSizeF(14, 14)));
  }
  QList<Caption> pointLabels;
  QList<Point> overflow;
  for (qsizetype i = 0; i < chart.points.size(); ++i) {
    const auto& point = chart.points.at(i);
    const auto text = smallText.elidedText(point.name, Qt::ElideRight, 142);
    const QSizeF size(smallText.horizontalAdvance(text) + 8, smallText.height() + 4);
    const auto rect = positionLabel(centers.at(i), size, plot.adjusted(3, 3, -3, -3), occupied);
    if (rect.isEmpty()) {
      overflow.append(point);
    } else {
      occupied.append(rect.adjusted(-2, -2, 2, 2));
      pointLabels.append({text, rect});
    }
  }

  const int plotEnd = qCeil(plot.bottom()) + (hasPoints ? xHeight : 0);
  const int rowHeight = qCeil(smallText.height()) + 9;
  const int overflowHeight =
      overflow.isEmpty() ? 0 : 20 + int((overflow.size() + 1) / 2) * rowHeight;
  auto image = canvas(width, plotEnd + margin + overflowHeight, dark);
  if (image.isNull()) return image;
  QPainter painter(&image);
  painter.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);
  for (size_t i = 0; i < cells.size(); ++i) painter.fillRect(cells[i], fills[i]);
  painter.setPen(QPen(border, 1.15));
  painter.drawRect(plot);
  painter.drawLine(QPointF(plot.left(), plot.center().y()),
                   QPointF(plot.right(), plot.center().y()));
  painter.drawLine(QPointF(plot.center().x(), plot.top()),
                   QPointF(plot.center().x(), plot.bottom()));
  painter.setPen(ink);
  painter.setFont(titleFont);
  painter.drawText(QRectF(left, margin - 3, side, 31), Qt::AlignCenter,
                   titleText.elidedText(chart.title, Qt::ElideRight, side - 8));
  painter.setFont(cellFont);
  for (const auto& caption : captions)
    painter.drawText(caption.rect, Qt::AlignCenter, caption.text);

  painter.setPen(axisInk);
  painter.setFont(smallFont);
  for (int i = 0; i < 2; ++i) {
    const qreal y = hasPoints ? plot.bottom() + 3 : plot.top() - xHeight;
    painter.drawText(QRectF(left + i * half, y, half, 28), Qt::AlignCenter,
                     smallText.elidedText(chart.xAxis[i], Qt::ElideRight, half - 12));
    if (chart.yAxis[i].isEmpty()) continue;
    painter.save();
    painter.translate(left - 22, plot.bottom() - (i + .5) * half);
    painter.rotate(-90);
    painter.drawText(QRectF(-half / 2, -14, half, 28), Qt::AlignCenter,
                     smallText.elidedText(chart.yAxis[i], Qt::ElideRight, half - 12));
    painter.restore();
  }
  painter.setPen(QPen(paper, 1.6));
  painter.setBrush(marker);
  for (const auto at : centers) painter.drawEllipse(at, 5, 5);
  painter.setPen(Qt::NoPen);
  painter.setBrush(pointBackground);
  for (const auto& caption : pointLabels) painter.drawRoundedRect(caption.rect, 3, 3);
  painter.setPen(ink);
  for (const auto& caption : pointLabels)
    painter.drawText(caption.rect, Qt::AlignCenter, caption.text);

  if (!overflow.isEmpty()) {
    painter.setPen(border);
    painter.drawLine(QPointF(margin, plotEnd + 15), QPointF(width - margin, plotEnd + 15));
    const qreal column = (width - margin * 2) / 2.;
    for (qsizetype i = 0; i < overflow.size(); ++i) {
      const auto& point = overflow.at(i);
      const qreal x = margin + (i % 2) * column;
      const qreal y = plotEnd + 22 + (i / 2) * rowHeight;
      const auto coordinates = QStringLiteral("[%1, %2]")
                                   .arg(QString::number(point.value.x(), 'g', 7),
                                        QString::number(point.value.y(), 'g', 7));
      const qreal remaining = column - smallText.horizontalAdvance(coordinates) - 20;
      const auto name = smallText.elidedText(point.name, Qt::ElideRight, remaining);
      painter.setPen(ink);
      painter.drawText(QRectF(x, y, column - 14, rowHeight), Qt::AlignVCenter,
                       name + QLatin1String("  ") + coordinates);
    }
  }
  return image;
}
}  // namespace

RenderResult quadrantChart(const QStringList& lines, bool dark) {
  Chart chart;
  if (!readChart(chart, lines)) return {{}, unsupported};
  return {drawChart(chart, dark), {}};
}
}  // namespace typist::render::mermaid_detail
