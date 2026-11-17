#include "MermaidInternal.h"

#include <QFontMetricsF>
#include <QHash>
#include <QPainter>
#include <QPainterPath>
#include <QRegularExpression>
#include <QSet>
#include <QtMath>
#include <algorithm>
#include <utility>

namespace typist::render::mermaid_detail {
namespace {
enum class NodeShape { Box, Rounded, Ellipse, Diamond, Start, End, Choice };
struct Node {
  QString text;
  NodeShape shape = NodeShape::Box;
  int rank = 0;
  QRectF rect;
};
struct Edge {
  int from, to;
  QString text;
  bool dashed = false, head = true;
};
int graphNode(QList<Node>& nodes, QHash<QString, int>& ids, const QString& id,
              NodeShape shape = NodeShape::Box) {
  const auto previous = ids.constFind(id);
  if (previous != ids.cend()) return *previous;
  const int index = int(nodes.size());
  nodes.append({id, shape, 0, {}});
  ids.insert(id, index);
  return index;
}

RenderResult drawGraph(QList<Node> nodes, const QList<Edge>& edges, const QString& mode, bool dark,
                       bool states = false);

QStringList graphStatements(const QStringList& lines, bool inlineComments = false) {
  // Semicolons separate statements only outside labels and quoted strings.
  QStringList statements;
  for (const auto& line : lines) {
    if (line.trimmed().isEmpty() || line.trimmed().startsWith("%%")) continue;
    QString statement;
    int nesting = 0;
    bool quoted = false;
    for (qsizetype i = 0; i < line.size(); ++i) {
      const auto c = line.at(i);
      if (inlineComments && !quoted && c == '%' && i + 1 < line.size() && line.at(i + 1) == '%')
        break;
      if (c == '"') quoted = !quoted;
      if (!quoted && (c == '[' || c == '{' || c == '(')) ++nesting;
      if (!quoted && (c == ']' || c == '}' || c == ')')) --nesting;
      if (c == ';' && nesting == 0 && !quoted) {
        if (!statement.trimmed().isEmpty()) statements.append(statement.trimmed());
        statement.clear();
      } else
        statement += c;
    }
    if (!statement.trimmed().isEmpty()) statements.append(statement.trimmed());
  }
  return statements;
}

}  // namespace

RenderResult flowchart(QStringList lines, bool dark) {
  const auto header = lines.takeFirst().trimmed();
  static const QRegularExpression direction("^(?:graph|flowchart)\\s+(TD|TB|BT|LR|RL)\\s*$");
  const auto match = direction.match(header);
  if (!match.hasMatch()) return {{}, unsupported};
  const QString mode = match.captured(1);
  QList<Node> nodes;
  QList<Edge> edges;
  QHash<QString, int> ids;
  static const QRegularExpression nodePattern(
      R"rx(^\s*([A-Za-z0-9_]+(?:-[A-Za-z0-9_]+)*)(?:\[([^\]]*)\]|\(\(([^)]*)\)\)|\(([^)]*)\)|\{([^}]*)\})?)rx");
  static const QRegularExpression edgePattern(
      R"rx(^\s*(-->|---|==>|-\.->|--\s+(.+?)\s+-->)\s*(?:\|([^|]*)\|\s*)?)rx");
  auto node = [&](QString& remaining) {
    const auto m = nodePattern.match(remaining);
    if (!m.hasMatch()) return -1;
    remaining.remove(0, m.capturedLength());
    const int index = graphNode(nodes, ids, m.captured(1));
    if (!m.captured(2).isNull()) {
      nodes[index].text = label(m.captured(2));
      nodes[index].shape = NodeShape::Box;
    } else if (!m.captured(3).isNull()) {
      nodes[index].text = label(m.captured(3));
      nodes[index].shape = NodeShape::Ellipse;
    } else if (!m.captured(4).isNull()) {
      nodes[index].text = label(m.captured(4));
      nodes[index].shape = NodeShape::Rounded;
    } else if (!m.captured(5).isNull()) {
      nodes[index].text = label(m.captured(5));
      nodes[index].shape = NodeShape::Diamond;
    }
    return index;
  };
  for (auto statement : graphStatements(lines)) {
    int previous = node(statement);
    if (previous < 0) return {{}, unsupported};
    while (!statement.trimmed().isEmpty()) {
      const auto edge = edgePattern.match(statement);
      if (!edge.hasMatch()) return {{}, unsupported};
      statement.remove(0, edge.capturedLength());
      const int next = node(statement);
      if (next < 0) return {{}, unsupported};
      edges.append({previous, next,
                    label(edge.captured(3).isEmpty() ? edge.captured(2) : edge.captured(3)),
                    edge.captured(1) == "-.->", edge.captured(1) != "---"});
      previous = next;
      if (edges.size() > 128) return {{}, QStringLiteral("Diagram exceeds 128 edges")};
    }
    if (nodes.size() > 64) return {{}, QStringLiteral("Diagram exceeds 64 nodes")};
  }
  return drawGraph(std::move(nodes), edges, mode, dark);
}

RenderResult stateDiagram(QStringList lines, bool dark) {
  lines.removeFirst();
  static const QString id = QStringLiteral(R"([\p{L}\p{N}_][\p{L}\p{N}_.-]*)");
  static const QRegularExpression direction(R"(^direction\s+(TD|TB|BT|LR|RL)$)");
  static const QRegularExpression transition(
      QStringLiteral(R"(^((?:%1|\[\*\]))\s*-->\s*((?:%1|\[\*\]))(?:\s*:(?!:)\s*(.+))?$)").arg(id));
  static const QRegularExpression alias(
      QStringLiteral(R"rx(^state\s+"([^"]+)"\s+as\s+(%1)$)rx").arg(id));
  static const QRegularExpression description(QStringLiteral(R"(^(%1)\s*:(?!:)\s*(.+)$)").arg(id));
  static const QRegularExpression declaration(
      QStringLiteral(R"(^(?:state\s+)?(%1)(?:\s+(<<choice>>))?$)").arg(id));
  static const QRegularExpression metadata(R"(^acc(?:Title|Descr)\s*[:{])",
                                           QRegularExpression::CaseInsensitiveOption);
  QString mode = QStringLiteral("TB");
  QList<Node> nodes;
  QList<Edge> edges;
  QHash<QString, int> ids;
  const auto endpoint = [&](const QString& token, bool source) {
    if (token != "[*]") return graphNode(nodes, ids, token);
    return graphNode(nodes, ids, source ? QStringLiteral("[start]") : QStringLiteral("[end]"),
                     source ? NodeShape::Start : NodeShape::End);
  };
  for (const auto& statement : graphStatements(lines, true)) {
    if (metadata.match(statement).hasMatch()) return {{}, unsupported};
    const auto orientation = direction.match(statement);
    if (orientation.hasMatch()) {
      mode = orientation.captured(1);
      continue;
    }
    const auto link = transition.match(statement);
    if (link.hasMatch()) {
      const auto from = endpoint(link.captured(1), true);
      const auto to = endpoint(link.captured(2), false);
      edges.append({from, to, label(link.captured(3))});
    } else if (const auto named = alias.match(statement); named.hasMatch()) {
      const auto index = graphNode(nodes, ids, named.captured(2));
      nodes[index].text = label(named.captured(1));
    } else if (const auto named = description.match(statement); named.hasMatch()) {
      const auto index = graphNode(nodes, ids, named.captured(1));
      nodes[index].text = label(named.captured(2));
    } else if (const auto state = declaration.match(statement); state.hasMatch()) {
      const auto index = graphNode(nodes, ids, state.captured(1));
      if (state.capturedStart(2) >= 0) nodes[index].shape = NodeShape::Choice;
    } else {
      return {{}, unsupported};
    }
    if (nodes.size() > 64 || edges.size() > 128)
      return {{}, QStringLiteral("State diagram exceeds 64 nodes or 128 transitions")};
  }
  return drawGraph(std::move(nodes), edges, mode, dark, true);
}

namespace {
void rankStates(QList<Node>& nodes, const QList<Edge>& edges) {
  QList<int> indegree(nodes.size(), 0);
  for (const auto& edge : edges)
    if (edge.from != edge.to) ++indegree[edge.to];
  QSet<int> seen;
  int deepest = 0;
  const auto visit = [&](int root) {
    if (seen.contains(root) || nodes.at(root).shape == NodeShape::End) return;
    QList<int> next{root};
    seen.insert(root);
    for (qsizetype i = 0; i < next.size(); ++i) {
      const int from = next.at(i);
      for (const auto& edge : edges) {
        if (edge.from != from || seen.contains(edge.to) ||
            nodes.at(edge.to).shape == NodeShape::End)
          continue;
        nodes[edge.to].rank = nodes.at(from).rank + 1;
        deepest = qMax(deepest, nodes.at(edge.to).rank);
        seen.insert(edge.to);
        next.append(edge.to);
      }
    }
  };
  for (int i = 0; i < nodes.size(); ++i)
    if (nodes.at(i).shape == NodeShape::Start) visit(i);
  for (int i = 0; i < nodes.size(); ++i)
    if (indegree.at(i) == 0) visit(i);
  for (int i = 0; i < nodes.size(); ++i) visit(i);
  for (auto& node : nodes)
    if (node.shape == NodeShape::End) node.rank = deepest + 1;
}

}  // namespace

QList<int> graphRanks(int count, const QList<QPair<int, int>>& links) {
  // Kahn ordering gives branches their own row/column; cycles remain on bounded
  // fallback ranks rather than iterating rank relaxation indefinitely.
  QList<int> ranks(count, 0), indegree(count, 0), queue;
  int maxRank = 0;
  for (const auto& [from, to] : links)
    if (from != to) ++indegree[to];
  for (int i = 0; i < count; ++i)
    if (!indegree[i]) queue.append(i);
  QSet<int> visited;
  for (int at = 0; at < queue.size(); ++at) {
    const int n = queue[at];
    visited.insert(n);
    for (const auto& [from, to] : links)
      if (from == n && to != n) {
        ranks[to] = qMax(ranks[to], ranks[n] + 1);
        if (!--indegree[to]) queue.append(to);
      }
  }
  for (int i = 0; i < count; ++i)
    if (!visited.contains(i)) ranks[i] = maxRank++;
  return ranks;
}

namespace {
QRectF visibleStateBox(const Node& node) {
  if (node.shape != NodeShape::Start && node.shape != NodeShape::End &&
      node.shape != NodeShape::Choice)
    return node.rect;
  const qreal size = node.shape == NodeShape::Start ? 11 : 18;
  return QRectF(node.rect.center() - QPointF(size / 2, size / 2), QSizeF(size, size));
}

QPointF drawStateEdge(QPainter& painter, const QList<Node>& nodes, const QList<Edge>& edges,
                      int index, bool horizontal, qreal outerLane) {
  const auto& edge = edges.at(index);
  const auto a = visibleStateBox(nodes.at(edge.from));
  const auto b = visibleStateBox(nodes.at(edge.to));
  const auto delta = b.center() - a.center();
  const bool across = qAbs(delta.x()) >= qAbs(delta.y());
  int before = 0, total = 0;
  for (int i = 0; i < edges.size(); ++i) {
    const auto& other = edges.at(i);
    if ((other.from == edge.from && other.to == edge.to) ||
        (other.from == edge.to && other.to == edge.from)) {
      ++total;
      if (i < index) ++before;
    }
  }
  if (edge.from == edge.to) {
    const qreal radius = 42 + qMin(before, 5) * 12;
    const QPointF start(a.right(), a.center().y() - 10), end(a.right(), a.center().y() + 10);
    QPainterPath loop(start);
    loop.cubicTo({a.right() + radius, a.top() - 13}, {a.right() + radius, a.bottom() + 13}, end);
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(loop);
    arrow(painter, loop.pointAtPercent(.88), end);
    return {a.right() + radius * .76 + 22, a.center().y() - 20 - qMin(before, 5) * 10};
  }

  const qreal crossSpan = across ? qMin(a.height(), b.height()) : qMin(a.width(), b.width());
  const qreal step = qMin(22., crossSpan * .7 / qMax(1, total - 1));
  const qreal offset = (before - (total - 1) / 2.) * step;
  const bool sameLane = horizontal ? qAbs(delta.y()) < 1 : qAbs(delta.x()) < 1;
  if (sameLane && qAbs(nodes.at(edge.from).rank - nodes.at(edge.to).rank) > 1) {
    const qreal lane = outerLane + qMin(before, 4) * 5 + qBound(-6., offset, 6.);
    const auto outside = [horizontal, lane, offset](const QRectF& box) {
      return horizontal ? QPointF(box.center().x() + offset, lane)
                        : QPointF(lane, box.center().y() + offset);
    };
    const QPointF first = outside(a), last = outside(b);
    const auto leave = horizontal ? QPointF(first.x(), a.bottom()) : QPointF(a.right(), first.y());
    const auto enter = horizontal ? QPointF(last.x(), b.bottom()) : QPointF(b.right(), last.y());
    painter.drawPolyline(QPolygonF({leave, first, last}));
    arrow(painter, last, enter);
    return (first + last) / 2 + (horizontal ? QPointF(0, 13) : QPointF(16, 0));
  }
  const QPointF start =
      across ? QPointF(delta.x() > 0 ? a.right() : a.left(), a.center().y() + offset)
             : QPointF(a.center().x() + offset, delta.y() > 0 ? a.bottom() : a.top());
  const QPointF end = across
                          ? QPointF(delta.x() > 0 ? b.left() : b.right(), b.center().y() + offset)
                          : QPointF(b.center().x() + offset, delta.y() > 0 ? b.top() : b.bottom());
  arrow(painter, start, end);
  const bool forward = across ? delta.x() > 0 : delta.y() > 0;
  const qreal along = total == 1 ? .5 : .3 + .4 * before / (total - 1);
  auto caption = start + (end - start) * (forward ? along : 1 - along);
  if (!across && total > 1) caption.rx() += before * 2 < total ? -30 : 30;
  return caption;
}

RenderResult drawGraph(QList<Node> nodes, const QList<Edge>& edges, const QString& mode, bool dark,
                       bool states) {
  if (nodes.isEmpty()) return {{}, unsupported};
  if (states)
    rankStates(nodes, edges);
  else {
    QList<QPair<int, int>> links;
    for (const auto& edge : edges) links.append({edge.from, edge.to});
    const auto ranks = graphRanks(int(nodes.size()), links);
    for (int i = 0; i < nodes.size(); ++i) nodes[i].rank = ranks.at(i);
  }
  const bool horizontal = mode == "LR" || mode == "RL";
  int maxRank = 0;
  QHash<int, QList<int>> ranks;
  for (int i = 0; i < nodes.size(); ++i) {
    maxRank = qMax(maxRank, nodes[i].rank);
    ranks[nodes[i].rank].append(i);
  }
  const QFont font = diagramFont();
  const QFontMetricsF metrics(font);
  qreal nodeWidth = 120, nodeHeight = 54;
  int maxParallel = 1;
  for (const auto& n : nodes) {
    const auto bounds =
        metrics.boundingRect(QRectF(0, 0, 220, 180), Qt::TextWordWrap | Qt::AlignCenter, n.text);
    nodeWidth = qMax(nodeWidth, qMin(250., bounds.width() + 36));
    nodeHeight = qMax(nodeHeight, bounds.height() + 30);
  }
  for (const auto& rank : ranks) maxParallel = qMax(maxParallel, static_cast<int>(rank.size()));
  const qreal majorStep = horizontal ? nodeWidth + 110 : nodeHeight + 82;
  const qreal minorStep = horizontal ? nodeHeight + 40 : nodeWidth + 48;
  const qreal majorSize = (maxRank + 1) * majorStep + 28, minorSize = maxParallel * minorStep + 28;
  const bool loops = states && std::any_of(edges.cbegin(), edges.cend(),
                                           [](const Edge& edge) { return edge.from == edge.to; });
  const bool routes = states && std::any_of(edges.cbegin(), edges.cend(), [&](const Edge& edge) {
                        return qAbs(nodes.at(edge.from).rank - nodes.at(edge.to).rank) > 1;
                      });
  const qreal naturalWidth =
      (horizontal ? majorSize : minorSize) + (loops ? 118 : 0) + (!horizontal && routes ? 88 : 0);
  const qreal naturalHeight = horizontal ? minorSize + (routes ? 34 : 0) : majorSize;
  const qreal scale = qMin(1., qMin(820. / naturalWidth, (states ? 720. : 1400.) / naturalHeight));
  for (int rank = 0; rank <= maxRank; ++rank) {
    const auto group = ranks.value(rank);
    for (int j = 0; j < group.size(); ++j) {
      const qreal major = 28 + rank * majorStep,
                  minor = 14 + (minorSize - group.size() * minorStep) / 2 + j * minorStep;
      QRectF rect(horizontal ? major : minor, horizontal ? minor : major, nodeWidth, nodeHeight);
      if (mode == "RL") rect.moveLeft(majorSize - rect.right() - 14);
      if (mode == "BT") rect.moveTop(naturalHeight - rect.bottom() - 14);
      nodes[group[j]].rect = rect;
    }
  }
  qreal outerLane = 0;
  for (const auto& node : nodes)
    outerLane = qMax(outerLane, horizontal ? node.rect.bottom() : node.rect.right());
  outerLane += loops && !horizontal ? 85 : 17;
  QImage image = canvas(qCeil(naturalWidth * scale), qCeil(naturalHeight * scale), dark);
  QPainter painter(&image);
  painter.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);
  painter.scale(scale, scale);
  painter.setFont(font);
  const QColor foreground(dark ? "#dde3ec" : "#445269"), background(dark ? "#262b31" : "#ffffff");
  for (int i = 0; i < edges.size(); ++i) {
    const auto& edge = edges.at(i);
    const auto a = nodes[edge.from].rect, b = nodes[edge.to].rect;
    const bool reverse =
        horizontal ? a.center().x() > b.center().x() : a.center().y() > b.center().y();
    const QPointF start = horizontal ? QPointF(reverse ? a.left() : a.right(), a.center().y())
                                     : QPointF(a.center().x(), reverse ? a.top() : a.bottom());
    const QPointF finish = horizontal ? QPointF(reverse ? b.right() : b.left(), b.center().y())
                                      : QPointF(b.center().x(), reverse ? b.bottom() : b.top());
    painter.setPen(QPen(foreground, 1.6));
    QPointF midpoint;
    if (states)
      midpoint = drawStateEdge(painter, nodes, edges, i, horizontal, outerLane);
    else {
      arrow(painter, start, finish, edge.dashed, edge.head);
      midpoint = (start + finish) / 2;
    }
    if (!edge.text.isEmpty()) {
      const auto caption = states ? metrics.elidedText(edge.text, Qt::ElideRight, 150) : edge.text;
      QRectF text = metrics.boundingRect(caption);
      if (states)
        midpoint.setX(
            qBound(text.width() / 2 + 6, midpoint.x(), naturalWidth - text.width() / 2 - 6));
      text.moveCenter(midpoint);
      text.adjust(-5, -4, 5, 4);
      painter.fillRect(text, background);
      painter.drawText(text, Qt::AlignCenter, caption);
    }
  }
  for (const auto& node : nodes) {
    painter.setPen(QPen(dark ? QColor("#7fa3d2") : QColor("#96b3da"), 1.5));
    painter.setBrush(dark ? QColor("#344359") : QColor("#edf3fc"));
    if (node.shape == NodeShape::Start || node.shape == NodeShape::End) {
      const auto rect = visibleStateBox(node);
      if (node.shape == NodeShape::End) {
        painter.setPen(QPen(foreground, 1.6));
        painter.setBrush(background);
        painter.drawEllipse(rect);
      }
      painter.setPen(Qt::NoPen);
      painter.setBrush(foreground);
      painter.drawEllipse(rect.center(), 5.5, 5.5);
      continue;
    }
    if (node.shape == NodeShape::Choice) {
      const auto rect = visibleStateBox(node);
      painter.drawPolygon(QPolygonF({{rect.center().x(), rect.top()},
                                     {rect.right(), rect.center().y()},
                                     {rect.center().x(), rect.bottom()},
                                     {rect.left(), rect.center().y()}}));
      continue;
    }
    if (node.shape == NodeShape::Ellipse)
      painter.drawEllipse(node.rect);
    else if (node.shape == NodeShape::Diamond)
      painter.drawPolygon(QPolygonF({QPointF(node.rect.center().x(), node.rect.top() - 8),
                                     QPointF(node.rect.right() + 8, node.rect.center().y()),
                                     QPointF(node.rect.center().x(), node.rect.bottom() + 8),
                                     QPointF(node.rect.left() - 8, node.rect.center().y())}));
    else {
      const int radius = node.shape == NodeShape::Rounded ? 22 : 5;
      painter.drawRoundedRect(node.rect, radius, radius);
    }
    painter.setPen(foreground);
    painter.drawText(node.rect.adjusted(12, 4, -12, -4), Qt::AlignCenter | Qt::TextWordWrap,
                     node.text);
  }
  return {image, {}};
}
}  // namespace
}  // namespace typist::render::mermaid_detail
