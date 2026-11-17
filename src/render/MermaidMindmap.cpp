#include "MermaidInternal.h"

#include <QFontMetricsF>
#include <QPainter>
#include <QPainterPath>
#include <QRegularExpression>
#include <QTextLayout>
#include <QtMath>
#include <array>

namespace typist::render::mermaid_detail {
namespace {
constexpr int maxNodes = 48, maxLevels = 8;
constexpr qreal columnGap = 42, rowGap = 14, margin = 22;

enum class Shape { Default, Square, Rounded, Circle, Bang, Cloud, Hexagon };
enum class Icon { None, Book, Idea, Check };

struct Node {
  QString text;
  Shape shape = Shape::Default;
  Icon icon = Icon::None;
  QList<int> children;
  int depth = 0, branch = 0, side = 1;
  QStringList lines;
  QSizeF textSize;
  QRectF rect;
  qreal subtreeHeight = 0;
};

bool readLabel(const QString& text, Node& node) {
  static const QRegularExpression delimiter(R"([\[\](){}])");
  struct Form {
    const char *opening, *closing;
    Shape shape;
  };
  static const std::array<Form, 6> forms{{{"((", "))", Shape::Circle},
                                          {"))", "((", Shape::Bang},
                                          {"{{", "}}", Shape::Hexagon},
                                          {"[", "]", Shape::Square},
                                          {"(", ")", Shape::Rounded},
                                          {")", "(", Shape::Cloud}}};
  const auto match = delimiter.match(text);
  if (!match.hasMatch()) {
    node.text = label(text);
  } else {
    const auto shape = text.mid(match.capturedStart());
    for (const auto& form : forms) {
      const QLatin1String start(form.opening), end(form.closing);
      if (!shape.startsWith(start)) continue;
      if (!shape.endsWith(end) || shape.size() <= start.size() + end.size()) return false;
      const auto content =
          shape.mid(start.size(), shape.size() - start.size() - end.size()).trimmed();
      const bool quoted = content.startsWith('"') && content.endsWith('"');
      if ((quoted ? content.mid(1, content.size() - 2).contains('"')
                  : content.contains('"') || delimiter.match(content).hasMatch()))
        return false;
      node.shape = form.shape;
      node.text = label(content);
      break;
    }
  }
  return !node.text.isEmpty() && !node.text.startsWith(QChar(0x60));
}

bool readNodes(const QStringList& lines, QList<Node>& nodes) {
  static const QRegularExpression icon(
      R"(^::icon\(\s*(?:fa|fas|fa-solid)\s+fa-(book|lightbulb|check)\s*\)$)");
  QList<QPair<qsizetype, int>> ancestry;
  for (qsizetype i = 1; i < lines.size(); ++i) {
    const auto& raw = lines.at(i);
    const auto line = raw.trimmed();
    if (line.startsWith("%%{")) return false;
    if (line.isEmpty() || line.startsWith("%%")) continue;
    if (const auto decoration = icon.match(line); decoration.hasMatch()) {
      if (nodes.isEmpty() || nodes.last().icon != Icon::None) return false;
      const auto name = decoration.captured(1);
      nodes.last().icon = name == "book"        ? Icon::Book
                          : name == "lightbulb" ? Icon::Idea
                                                : Icon::Check;
      continue;
    }
    if (line.contains(":::") || line.contains("::icon(") || nodes.size() >= maxNodes) return false;
    qsizetype indent = 0;
    while (indent < raw.size() && (raw.at(indent) == ' ' || raw.at(indent) == '\t')) ++indent;
    while (!ancestry.isEmpty() && ancestry.last().first >= indent) ancestry.removeLast();
    if ((!nodes.isEmpty() && ancestry.isEmpty()) || ancestry.size() >= maxLevels) return false;
    Node node;
    node.depth = int(ancestry.size());
    if (!readLabel(line, node)) return false;
    const int index = int(nodes.size());
    if (!ancestry.isEmpty()) nodes[ancestry.last().second].children.append(index);
    nodes.append(node);
    ancestry.append({indent, index});
  }
  return !nodes.isEmpty();
}

QFont nodeFont(const Node& node) {
  auto font = diagramFont();
  if (node.depth == 0) font.setPointSizeF(13);
  font.setBold(node.depth < 2);
  return font;
}

QStringList wrapText(const QString& text, const QFont& font, qreal width) {
  constexpr int maxLines = 4;
  QStringList lines;
  QTextOption option;
  option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
  const auto paragraphs = text.split('\n');
  const QFontMetricsF metrics(font);
  for (qsizetype i = 0; i < paragraphs.size() && lines.size() < maxLines; ++i) {
    const auto& paragraph = paragraphs.at(i);
    QTextLayout layout(paragraph, font);
    layout.setTextOption(option);
    layout.beginLayout();
    while (lines.size() < maxLines) {
      auto line = layout.createLine();
      if (!line.isValid()) break;
      line.setLineWidth(width);
      auto part = paragraph.mid(line.textStart(), line.textLength()).trimmed();
      if (lines.size() == maxLines - 1 &&
          (i < paragraphs.size() - 1 || line.textStart() + line.textLength() < paragraph.size()))
        part = metrics.elidedText(part + QChar(0x2026), Qt::ElideRight, width);
      lines.append(part);
    }
    layout.endLayout();
  }
  return lines;
}

void measureNodes(QList<Node>& nodes) {
  for (qsizetype i = nodes.size(); i-- > 0;) {
    auto& node = nodes[i];
    const auto font = nodeFont(node);
    const QFontMetricsF metrics(font);
    const qreal iconWidth = node.icon == Icon::None ? 0 : 22;
    node.lines = wrapText(node.text, font, 148 - iconWidth);
    qreal textWidth = 0;
    for (const auto& line : node.lines)
      textWidth = qMax(textWidth, metrics.horizontalAdvance(line));
    node.textSize = {textWidth, metrics.height() * node.lines.size()};
    qreal width = qMax(66., textWidth + iconWidth + 26), height = node.textSize.height() + 20;
    if (node.shape == Shape::Circle) width = height = qCeil(qHypot(width, height));
    if (node.shape == Shape::Hexagon) width += 20;
    if (node.shape == Shape::Bang || node.shape == Shape::Cloud) {
      width *= 1.34;
      height *= 1.4;
    }
    node.rect.setSize({width, height});
    qreal childrenHeight = 0;
    for (const int child : node.children) childrenHeight += nodes.at(child).subtreeHeight + rowGap;
    node.subtreeHeight = qMax(height, childrenHeight - rowGap);
  }
}

using Depths = std::array<qreal, maxLevels>;

void assignBranch(QList<Node>& nodes, int index, int side, int branch, Depths& widths) {
  auto& node = nodes[index];
  node.side = side;
  node.branch = branch;
  widths[node.depth] = qMax(widths[node.depth], node.rect.width());
  for (const int child : node.children) assignBranch(nodes, child, side, branch, widths);
}

void placeBranch(QList<Node>& nodes, int index, qreal top, const Depths& columns) {
  auto& node = nodes[index];
  node.rect.moveCenter({columns[node.depth], top + node.subtreeHeight / 2});
  qreal stack = 0;
  for (const int child : node.children) stack += nodes.at(child).subtreeHeight + rowGap;
  qreal next = top + (node.subtreeHeight - stack + rowGap) / 2;
  for (const int child : node.children) {
    placeBranch(nodes, child, next, columns);
    next += nodes.at(child).subtreeHeight + rowGap;
  }
}

QSizeF layoutNodes(QList<Node>& nodes) {
  auto& root = nodes.first();
  std::array<qreal, 2> height{}, span{};
  std::array<Depths, 2> widths{}, columns{};
  int branch = 0;
  for (const int child : root.children) {
    const int side = height[0] < height[1] ? 0 : 1;
    assignBranch(nodes, child, side, branch++, widths[side]);
    height[side] += nodes.at(child).subtreeHeight + rowGap;
  }
  for (int side = 0; side < 2; ++side)
    for (const qreal width : widths[side])
      if (width > 0) span[side] += width + columnGap;
  const qreal available = qMax(root.rect.height(), qMax(height[0], height[1]) - rowGap);
  root.rect.moveTopLeft({margin + span[0], margin + (available - root.rect.height()) / 2});
  for (int side = 0; side < 2; ++side) {
    const qreal sign = side == 0 ? -1 : 1;
    qreal edge = side == 0 ? root.rect.left() : root.rect.right();
    for (int depth = 1; depth < maxLevels && widths[side][depth] > 0; ++depth) {
      columns[side][depth] = edge + sign * (columnGap + widths[side][depth] / 2);
      edge += sign * (columnGap + widths[side][depth]);
    }
  }
  std::array<qreal, 2> top{margin + (available - height[0] + rowGap) / 2,
                           margin + (available - height[1] + rowGap) / 2};
  for (const int child : root.children) {
    const int side = nodes.at(child).side;
    placeBranch(nodes, child, top[side], columns[side]);
    top[side] += nodes.at(child).subtreeHeight + rowGap;
  }
  return {2 * margin + span[0] + root.rect.width() + span[1], 2 * margin + available};
}

void drawShape(QPainter& painter, const Node& node) {
  const auto& r = node.rect;
  switch (node.shape) {
    case Shape::Square:
      painter.drawRect(r);
      break;
    case Shape::Rounded:
      painter.drawRoundedRect(r, 12, 12);
      break;
    case Shape::Circle:
      painter.drawEllipse(r);
      break;
    case Shape::Hexagon: {
      const qreal y = r.center().y(), offset = 13;
      painter.drawPolygon(QPolygonF({{r.left(), y},
                                     r.topLeft() + QPointF(offset, 0),
                                     r.topRight() - QPointF(offset, 0),
                                     {r.right(), y},
                                     r.bottomRight() - QPointF(offset, 0),
                                     r.bottomLeft() + QPointF(offset, 0)}));
      break;
    }
    case Shape::Bang: {
      QPolygonF star;
      for (int i = 0; i < 24; ++i) {
        const qreal angle = i * qDegreesToRadians(15.);
        const qreal radius = i % 2 ? .78 : 1.;
        star.append(r.center() +
                    radius * QPointF(qCos(angle) * r.width() / 2, qSin(angle) * r.height() / 2));
      }
      painter.drawPolygon(star);
      break;
    }
    case Shape::Cloud: {
      QPainterPath cloud;
      cloud.moveTo(4, 45);
      cloud.cubicTo(3, 27, 10, 18, 23, 19);
      cloud.cubicTo(26, -3, 43, -2, 51, 13);
      cloud.cubicTo(63, -5, 82, 4, 83, 23);
      cloud.cubicTo(103, 20, 104, 57, 94, 66);
      cloud.cubicTo(102, 85, 74, 100, 65, 89);
      cloud.cubicTo(56, 104, 40, 99, 33, 89);
      cloud.cubicTo(19, 103, -1, 88, 5, 69);
      cloud.cubicTo(-2, 62, -2, 51, 4, 45);
      cloud.closeSubpath();
      QTransform transform;
      transform.translate(r.x(), r.y());
      transform.scale(r.width() / 100, r.height() / 100);
      painter.drawPath(transform.map(cloud));
      break;
    }
    case Shape::Default:
      painter.drawRoundedRect(r, 8, 8);
      break;
  }
}

void drawIcon(QPainter& painter, Icon icon, QPointF topLeft, const QColor& color) {
  if (icon == Icon::None) return;
  painter.save();
  painter.translate(topLeft);
  painter.setPen(QPen(color, 1.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
  painter.setBrush(Qt::NoBrush);
  if (icon == Icon::Book) {
    painter.drawRoundedRect(QRectF(1, 0, 12, 14), 1.4, 1.4);
    painter.drawLine(QPointF(4, 0), QPointF(4, 14));
    painter.drawLine(QPointF(7, 4), QPointF(10, 4));
  } else if (icon == Icon::Idea) {
    QPainterPath bulb;
    bulb.moveTo(5, 10);
    bulb.cubicTo(5, 8, 2, 8, 2, 4);
    bulb.cubicTo(2, -2, 12, -2, 12, 4);
    bulb.cubicTo(12, 8, 9, 8, 9, 10);
    painter.drawPath(bulb);
    painter.drawLine(QPointF(5, 11), QPointF(9, 11));
    painter.drawLine(QPointF(6, 14), QPointF(8, 14));
  } else {
    painter.drawPolyline(QPolygonF({{1, 7}, {5, 11}, {13, 3}}));
  }
  painter.restore();
}

struct Colors {
  QColor fill, stroke;
};

QImage drawMap(QList<Node>& nodes, bool dark) {
  measureNodes(nodes);
  const auto size = layoutNodes(nodes);
  const qreal scale = qMin(1., qMin(1020. / size.width(), 1320. / size.height()));
  auto image = canvas(qCeil(size.width() * scale), qCeil(size.height() * scale), dark);
  if (image.isNull()) return image;
  QPainter painter(&image);
  painter.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);
  painter.scale(scale, scale);
  const QColor ink(dark ? "#eef3fa" : "#1f3048");
  const Colors root{QColor(dark ? "#354b69" : "#dceafb"), QColor(dark ? "#9fbbeb" : "#4d73a8")};
  const std::array<Colors, 6> colors{
      Colors{QColor(dark ? "#2f404e" : "#e8f3fa"), QColor(dark ? "#88b7d6" : "#5285a9")},
      Colors{QColor(dark ? "#3c354e" : "#f0ebfa"), QColor(dark ? "#b6a2de" : "#8b72b0")},
      Colors{QColor(dark ? "#2f4640" : "#e6f4eb"), QColor(dark ? "#8bc4a7" : "#589575")},
      Colors{QColor(dark ? "#493a33" : "#fbede3"), QColor(dark ? "#dcaa87" : "#a76e4b")},
      Colors{QColor(dark ? "#483641" : "#f8eaf0"), QColor(dark ? "#dba2bd" : "#a65f81")},
      Colors{QColor(dark ? "#414331" : "#f0f3e4"), QColor(dark ? "#bac38d" : "#7d8c52")}};
  for (const auto& parent : nodes) {
    for (const int index : parent.children) {
      const auto& child = nodes.at(index);
      const bool left = child.side == 0;
      const qreal a = left ? parent.rect.left() + 3 : parent.rect.right() - 3;
      const qreal b = left ? child.rect.right() - 3 : child.rect.left() + 3;
      const qreal mid = (a + b) / 2;
      const qreal y1 = parent.rect.center().y(), y2 = child.rect.center().y();
      QPainterPath connector(QPointF(a, y1));
      connector.cubicTo({mid, y1}, {mid, y2}, {b, y2});
      painter.setPen(
          QPen(colors[child.branch % colors.size()].stroke, child.depth == 1 ? 2.2 : 1.4));
      painter.setBrush(Qt::NoBrush);
      painter.drawPath(connector);
    }
  }
  for (const auto& node : nodes) {
    const auto& style = node.depth == 0 ? root : colors[node.branch % colors.size()];
    painter.setPen(QPen(style.stroke, node.depth == 0 ? 1.8 : 1.15));
    painter.setBrush(style.fill);
    drawShape(painter, node);
    const auto font = nodeFont(node);
    painter.setFont(font);
    painter.setPen(ink);
    const qreal iconWidth = node.icon == Icon::None ? 0 : 22;
    const auto center = node.rect.center();
    drawIcon(painter, node.icon,
             {center.x() - (node.textSize.width() + iconWidth) / 2, center.y() - 7}, style.stroke);
    const auto left = center.x() - (node.textSize.width() - iconWidth) / 2;
    qreal top = center.y() - node.textSize.height() / 2;
    const qreal lineHeight = QFontMetricsF(font).height();
    for (const auto& line : node.lines) {
      painter.drawText(QRectF(left - 1, top, node.textSize.width() + 2, lineHeight),
                       Qt::AlignCenter, line);
      top += lineHeight;
    }
  }
  return image;
}
}  // namespace

RenderResult mindmap(const QStringList& lines, bool dark) {
  QList<Node> nodes;
  if (!readNodes(lines, nodes)) return {{}, unsupported};
  return {drawMap(nodes, dark), {}};
}
}  // namespace typist::render::mermaid_detail
