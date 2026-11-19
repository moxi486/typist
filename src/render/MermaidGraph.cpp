#include "MermaidInternal.h"

#include <QFontMetricsF>
#include <QHash>
#include <QLineF>
#include <QPainter>
#include <QPainterPath>
#include <QPainterPathStroker>
#include <QRegularExpression>
#include <QSet>
#include <QtMath>
#include <algorithm>
#include <cmath>
#include <optional>
#include <utility>

namespace typist::render::mermaid_detail {
namespace {
enum class NodeShape {
  Box,
  Rounded,
  Ellipse,
  Diamond,
  Stadium,
  Subroutine,
  Cylinder,
  Asymmetric,
  Hexagon,
  Parallelogram,
  ParallelogramAlt,
  Trapezoid,
  TrapezoidAlt,
  DoubleCircle,
  Start,
  End,
  Choice
};
struct NodeStyle {
  QColor fill, stroke, color;
  std::optional<qreal> width;
  std::optional<QList<qreal>> dashes;
};

void mergeStyle(NodeStyle& target, const NodeStyle& source) {
  if (source.fill.isValid()) target.fill = source.fill;
  if (source.stroke.isValid()) target.stroke = source.stroke;
  if (source.color.isValid()) target.color = source.color;
  if (source.width) target.width = source.width;
  if (source.dashes) target.dashes = source.dashes;
}

QColor styleColor(QString value, bool allowNone) {
  if (allowNone && value.compare("none", Qt::CaseInsensitive) == 0) return QColor(0, 0, 0, 0);
  static const QRegularExpression hex(R"(^#(?:[\da-f]{3}|[\da-f]{4}|[\da-f]{6}|[\da-f]{8})$)",
                                      QRegularExpression::CaseInsensitiveOption);
  if (hex.match(value).hasMatch()) {
    value.remove(0, 1);
    if (value.size() <= 4) {
      QString expanded;
      for (const auto c : value) expanded += QString(2, c);
      value = expanded;
    }
    if (value.size() == 6) return QColor('#' + value);
    return QColor(value.mid(0, 2).toInt(nullptr, 16), value.mid(2, 2).toInt(nullptr, 16),
                  value.mid(4, 2).toInt(nullptr, 16), value.mid(6, 2).toInt(nullptr, 16));
  }
  static const QRegularExpression named("^[A-Za-z]+$");
  if (named.match(value).hasMatch()) return QColor(value);
  static const QRegularExpression rgb(
      R"(^(rgb|rgba)\(\s*(\d{1,3})\s*,\s*(\d{1,3})\s*,\s*(\d{1,3})\s*(?:,\s*(\d+(?:\.\d*)?|\.\d+)\s*)?\)$)",
      QRegularExpression::CaseInsensitiveOption);
  const auto match = rgb.match(value);
  if (!match.hasMatch() || (match.captured(1).size() == 4) != (match.capturedStart(5) >= 0))
    return {};
  const int red = match.captured(2).toInt(), green = match.captured(3).toInt(),
            blue = match.captured(4).toInt();
  const qreal alpha = match.capturedStart(5) < 0 ? 1 : match.captured(5).toDouble();
  if (red > 255 || green > 255 || blue > 255 || !std::isfinite(alpha) || alpha > 1) return {};
  return QColor(red, green, blue, qRound(alpha * 255));
}

bool parseStyle(const QString& text, NodeStyle& style) {
  // Mermaid uses commas between properties; escaped commas belong to values.
  QStringList properties;
  QString property;
  for (qsizetype i = 0; i < text.size(); ++i) {
    if (text.at(i) == '\\') {
      if (++i >= text.size() || text.at(i) != ',') return false;
      property += ',';
    } else if (text.at(i) == ',') {
      properties.append(property);
      property.clear();
    } else
      property += text.at(i);
  }
  properties.append(property);
  if (properties.size() > 32) return false;
  static const QString number = QStringLiteral(R"((?:\d+(?:\.\d*)?|\.\d+)(?:px)?)");
  static const QRegularExpression widthPattern('^' + number + '$',
                                               QRegularExpression::CaseInsensitiveOption);
  static const QRegularExpression dashPattern(
      '^' + number + "(?:(?:\\s*,\\s*|\\s+)" + number + ")*$",
      QRegularExpression::CaseInsensitiveOption);
  const auto pixels = [](QString value) {
    if (value.endsWith("px", Qt::CaseInsensitive)) value.chop(2);
    return value.toDouble();
  };
  for (const auto& entry : properties) {
    const auto colon = entry.indexOf(':');
    if (colon < 0) return false;
    const auto key = entry.left(colon).trimmed().toLower(), value = entry.mid(colon + 1).trimmed();
    if (key == "fill" || key == "stroke" || key == "color") {
      const auto color = styleColor(value, key != "color");
      if (!color.isValid()) return false;
      if (key == "fill") style.fill = color;
      if (key == "stroke") style.stroke = color;
      if (key == "color") style.color = color;
    } else if (key == "stroke-width") {
      if (!widthPattern.match(value).hasMatch()) return false;
      const auto width = pixels(value);
      if (!std::isfinite(width) || width > 12 || (width > 0 && width < .1)) return false;
      style.width = width;
    } else if (key == "stroke-dasharray") {
      QList<qreal> dashes;
      if (value.compare("none", Qt::CaseInsensitive) != 0) {
        if (!dashPattern.match(value).hasMatch()) return false;
        const auto parts = value.split(QRegularExpression("[\\s,]+"), Qt::SkipEmptyParts);
        if (parts.size() > 8) return false;
        for (const auto& part : parts) {
          const auto dash = pixels(part);
          if (!std::isfinite(dash) || dash < .1 || dash > 64) return false;
          dashes.append(dash);
        }
        if (dashes.size() % 2) {
          const int count = int(dashes.size());
          for (int i = 0; i < count; ++i) dashes.append(dashes.at(i));
        }
      }
      style.dashes = dashes;
    } else
      return false;
  }
  return true;
}

struct Node {
  QString text;
  NodeShape shape = NodeShape::Box;
  int rank = 0;
  QRectF rect;
  qreal diameter = 0;
  QSizeF textSize{};
  QSizeF boxSize{};
  NodeStyle style{};
  QStringList classes{};
};
enum class LinkStyle { Solid, Dotted, Thick, Invisible };
enum class LinkHead { None, Arrow, Circle, Cross };
struct Edge {
  int from, to;
  QString text;
  LinkStyle style = LinkStyle::Solid;
  LinkHead start = LinkHead::None, end = LinkHead::Arrow;
  int minLength = 1;
};

LinkHead linkHead(QChar marker) {
  if (marker == '<' || marker == '>') return LinkHead::Arrow;
  if (marker == 'o') return LinkHead::Circle;
  if (marker == 'x') return LinkHead::Cross;
  return LinkHead::None;
}

bool parseLink(QString& remaining, Edge& edge) {
  static const QRegularExpression compact(
      R"(^\s*([<ox]?(?:-{2,}[ox>]|-{3,}|={2,}[ox>]|={3,}|-\.+-[ox>]?)|~{3,})\s*(?:\|([^|]*)\|\s*)?)");
  static const QRegularExpression words(
      R"rx(^\s*([<ox]?)(--|==|-\.)\s+("[^"]*"|.+?)\s+(-{2,}[ox>]|-{3,}|={2,}[ox>]|={3,}|\.+-[ox>]?)\s*)rx");
  QString token;
  auto match = compact.match(remaining);
  if (match.hasMatch()) {
    token = match.captured(1);
    edge.text = label(match.captured(2));
  } else {
    match = words.match(remaining);
    if (!match.hasMatch()) return false;
    const auto opening = match.captured(2), ending = match.captured(4);
    if ((opening == "--" && !ending.startsWith("--")) ||
        (opening == "==" && !ending.startsWith("==")) ||
        (opening == "-." && !ending.startsWith('.')))
      return false;
    token = match.captured(1) + (opening == "-." ? "-" : "") + ending;
    edge.text = label(match.captured(3));
  }
  edge.style = token.startsWith('~') ? LinkStyle::Invisible
               : token.contains('=') ? LinkStyle::Thick
               : token.contains('.') ? LinkStyle::Dotted
                                     : LinkStyle::Solid;
  if (edge.style == LinkStyle::Invisible && !edge.text.isEmpty()) return false;
  edge.start = linkHead(token.front());
  edge.end = linkHead(token.back());
  edge.minLength = edge.style == LinkStyle::Dotted      ? int(token.count('.'))
                   : edge.style == LinkStyle::Invisible ? int(token.size()) - 2
                   : edge.style == LinkStyle::Thick     ? int(token.count('='))
                                                        : int(token.count('-'));
  if (edge.style == LinkStyle::Solid || edge.style == LinkStyle::Thick)
    edge.minLength -= edge.end == LinkHead::None ? 2 : 1;
  if (edge.minLength < 1 || edge.minLength > 8) return false;
  remaining.remove(0, match.capturedLength());
  return true;
}
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
      if (!quoted && (c == ']' || c == '}' || c == ')')) nesting = qMax(0, nesting - 1);
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
  QHash<QString, NodeStyle> classes;
  static const QString idPattern = QStringLiteral(R"([A-Za-z0-9_]+(?:-[A-Za-z0-9_]+)*)");
  static const QString idList = idPattern + "(?:\\s*,\\s*" + idPattern + ")*";
  static const QRegularExpression definition("^classDef\\s+(" + idList + ")\\s+(.+)$");
  static const QRegularExpression assignment("^class\\s+(" + idList + ")\\s+(" + idPattern + ")$");
  static const QRegularExpression styling("^style\\s+(" + idPattern + ")\\s+(.+)$");
  static const QRegularExpression shortClass("^:::(" + idPattern + ")");
  static const QRegularExpression nodePattern(R"(^\s*([A-Za-z0-9_]+(?:-[A-Za-z0-9_]+)*))");
  static const QRegularExpression unquotedDelimiter(R"rx([\[\]{}()"])rx");
  struct Form {
    QString open, close;
    NodeShape shape;
  };
  static const QList<Form> forms = {
      {"(((", ")))", NodeShape::DoubleCircle}, {"((", "))", NodeShape::Ellipse},
      {"([", "])", NodeShape::Stadium},        {"[[", "]]", NodeShape::Subroutine},
      {"[(", ")]", NodeShape::Cylinder},       {"{{", "}}", NodeShape::Hexagon},
      {"[/", "/]", NodeShape::Parallelogram},  {"[\\", "\\]", NodeShape::ParallelogramAlt},
      {"[/", "\\]", NodeShape::Trapezoid},     {"[\\", "/]", NodeShape::TrapezoidAlt},
      {">", "]", NodeShape::Asymmetric},       {"[", "]", NodeShape::Box},
      {"(", ")", NodeShape::Rounded},          {"{", "}", NodeShape::Diamond}};
  auto node = [&](QString& remaining) {
    const auto m = nodePattern.match(remaining);
    if (!m.hasMatch()) return -1;
    remaining.remove(0, m.capturedLength());
    remaining = remaining.trimmed();
    const int index = graphNode(nodes, ids, m.captured(1));
    bool framed = false;
    for (const auto& form : forms) {
      if (!remaining.startsWith(form.open)) continue;
      framed = true;
      const auto body = remaining.mid(form.open.size());
      QString text, tail;
      if (body.startsWith('"')) {
        const auto end = body.indexOf('"', 1);
        if (end < 0) continue;
        text = body.mid(1, end - 1);
        tail = body.mid(end + 1).trimmed();
        if (!tail.startsWith(form.close)) continue;
        tail.remove(0, form.close.size());
      } else {
        const auto end = body.indexOf(form.close);
        if (end < 0) continue;
        text = body.left(end);
        if (unquotedDelimiter.match(text).hasMatch()) continue;
        tail = body.mid(end + form.close.size());
      }
      nodes[index].text = label(text);
      nodes[index].shape = form.shape;
      remaining = tail.trimmed();
      return index;
    }
    return framed ? -1 : index;
  };
  const auto nodeGroup = [&](QString& remaining) {
    QList<int> group;
    do {
      const int index = node(remaining);
      if (index < 0 || nodes.size() > 64) return QList<int>{};
      if (remaining.startsWith(":::")) {
        const auto named = shortClass.match(remaining);
        if (!named.hasMatch() || nodes.at(index).classes.size() >= 16) return QList<int>{};
        nodes[index].classes.append(named.captured(1));
        remaining.remove(0, named.capturedLength());
        remaining = remaining.trimmed();
      }
      group.append(index);
      if (group.size() > 64) return QList<int>{};
      if (!remaining.startsWith('&')) break;
      remaining.remove(0, 1);
    } while (true);
    return group;
  };
  for (auto statement : graphStatements(lines)) {
    if (const auto defined = definition.match(statement); defined.hasMatch()) {
      NodeStyle style;
      if (!parseStyle(defined.captured(2), style)) return {{}, unsupported};
      for (auto name : defined.captured(1).split(',')) {
        mergeStyle(classes[name.trimmed()], style);
        if (classes.size() > 32) return {{}, QStringLiteral("Diagram exceeds 32 style classes")};
      }
      continue;
    }
    if (const auto assigned = assignment.match(statement); assigned.hasMatch()) {
      for (auto target : assigned.captured(1).split(',')) {
        const auto index = ids.constFind(target.trimmed());
        if (index == ids.cend() || nodes.at(*index).classes.size() >= 16) return {{}, unsupported};
        nodes[*index].classes.append(assigned.captured(2));
      }
      continue;
    }
    if (const auto styled = styling.match(statement); styled.hasMatch()) {
      NodeStyle style;
      if (!parseStyle(styled.captured(2), style)) return {{}, unsupported};
      const int index = graphNode(nodes, ids, styled.captured(1));
      if (nodes.size() > 64) return {{}, QStringLiteral("Diagram exceeds 64 nodes")};
      mergeStyle(nodes[index].style, style);
      continue;
    }
    auto previous = nodeGroup(statement);
    if (previous.isEmpty()) return {{}, unsupported};
    while (!statement.trimmed().isEmpty()) {
      Edge edge{-1, -1, {}};
      if (!parseLink(statement, edge)) return {{}, unsupported};
      const auto next = nodeGroup(statement);
      if (next.isEmpty()) return {{}, unsupported};
      if (edges.size() + previous.size() * next.size() > 128)
        return {{}, QStringLiteral("Diagram exceeds 128 edges")};
      for (const int from : previous)
        for (const int to : next) {
          edge.from = from;
          edge.to = to;
          edges.append(edge);
        }
      previous = next;
    }
    if (nodes.size() > 64) return {{}, QStringLiteral("Diagram exceeds 64 nodes")};
  }
  for (auto& entry : nodes) {
    auto resolved = classes.value("default");
    mergeStyle(resolved, classes.value("node"));
    for (const auto& name : entry.classes) {
      const auto defined = classes.constFind(name);
      if (defined == classes.cend()) return {{}, unsupported};
      mergeStyle(resolved, *defined);
    }
    mergeStyle(resolved, entry.style);
    entry.style = resolved;
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

QPainterPath nodeOutline(const Node& node) {
  const auto r = node.rect;
  const qreal l = r.left(), t = r.top(), right = r.right(), bottom = r.bottom(),
              cx = r.center().x(), cy = r.center().y(), inset = qMin(24., r.width() * .18);
  QPainterPath path;
  QPolygonF polygon;
  switch (node.shape) {
    case NodeShape::Ellipse:
    case NodeShape::DoubleCircle:
      path.addEllipse(r);
      return path;
    case NodeShape::Diamond:
      polygon = {{cx, t - 8}, {right + 8, cy}, {cx, bottom + 8}, {l - 8, cy}};
      break;
    case NodeShape::Hexagon:
      polygon = {{l + inset, t},          {right - inset, t},  {right, cy},
                 {right - inset, bottom}, {l + inset, bottom}, {l, cy}};
      break;
    case NodeShape::Asymmetric:
      polygon = {{l, t}, {right, t}, {right, bottom}, {l, bottom}, {l + inset, cy}};
      break;
    case NodeShape::Parallelogram:
      polygon = {{l + inset, t}, {right, t}, {right - inset, bottom}, {l, bottom}};
      break;
    case NodeShape::ParallelogramAlt:
      polygon = {{l, t}, {right - inset, t}, {right, bottom}, {l + inset, bottom}};
      break;
    case NodeShape::Trapezoid:
      polygon = {{l + inset, t}, {right - inset, t}, {right, bottom}, {l, bottom}};
      break;
    case NodeShape::TrapezoidAlt:
      polygon = {{l, t}, {right, t}, {right - inset, bottom}, {l + inset, bottom}};
      break;
    case NodeShape::Cylinder: {
      const qreal cap = qMin(12., r.height() / 4);
      path.moveTo(l, t + cap);
      path.arcTo(QRectF(l, t, r.width(), cap * 2), 180, -180);
      path.lineTo(right, bottom - cap);
      path.arcTo(QRectF(l, bottom - cap * 2, r.width(), cap * 2), 0, -180);
      path.closeSubpath();
      return path;
    }
    case NodeShape::Subroutine:
      path.addRect(r);
      return path;
    default: {
      const qreal radius = node.shape == NodeShape::Stadium   ? r.height() / 2
                           : node.shape == NodeShape::Rounded ? 22
                                                              : 5;
      path.addRoundedRect(r, radius, radius);
      return path;
    }
  }
  path.addPolygon(polygon);
  path.closeSubpath();
  return path;
}

QPointF graphPort(const Node& node, QPointF direction) {
  const auto path = nodeOutline(node);
  const auto bounds = path.boundingRect();
  auto inside = node.rect.center();
  auto outside = inside + direction * (bounds.width() + bounds.height() + 2);
  for (int i = 0; i < 28; ++i) {
    const auto middle = (inside + outside) / 2;
    if (path.contains(middle))
      inside = middle;
    else
      outside = middle;
  }
  return (inside + outside) / 2;
}

QPointF graphPort(const Node& node, bool horizontal, bool positive) {
  return graphPort(node,
                   horizontal ? QPointF(positive ? 1 : -1, 0) : QPointF(0, positive ? 1 : -1));
}

void drawLinkHead(QPainter& painter, QPointF near, QPointF tip, LinkHead head,
                  const QColor& background) {
  const qreal length = QLineF(near, tip).length();
  if (head == LinkHead::None || qFuzzyIsNull(length)) return;
  const auto direction = (tip - near) / length;
  const QPointF normal(-direction.y(), direction.x());
  painter.save();
  const auto color = painter.pen().color();
  painter.setPen(QPen(color, 1.6));
  if (head == LinkHead::Circle) {
    painter.setBrush(background);
    painter.drawEllipse(tip - direction * 4, 4, 4);
  } else if (head == LinkHead::Cross) {
    const auto center = tip - direction * 4;
    painter.drawLine(center - direction * 3.5 - normal * 3.5,
                     center + direction * 3.5 + normal * 3.5);
    painter.drawLine(center - direction * 3.5 + normal * 3.5,
                     center + direction * 3.5 - normal * 3.5);
  } else {
    painter.setBrush(color);
    painter.drawPolygon(
        QPolygonF({tip, tip - direction * 9 + normal * 4, tip - direction * 9 - normal * 4}));
  }
  painter.restore();
}

bool flowEdgeNeedsLane(const QList<Node>& nodes, const Edge& edge, bool horizontal) {
  if (edge.from == edge.to || edge.style == LinkStyle::Invisible) return false;
  const auto& a = nodes.at(edge.from);
  const auto& b = nodes.at(edge.to);
  const bool reverse = horizontal ? a.rect.center().x() > b.rect.center().x()
                                  : a.rect.center().y() > b.rect.center().y();
  QPainterPath path;
  path.moveTo(graphPort(a, horizontal, !reverse));
  path.lineTo(graphPort(b, horizontal, reverse));
  QPainterPathStroker stroke;
  stroke.setWidth(10);
  const auto corridor = stroke.createStroke(path);
  for (int i = 0; i < nodes.size(); ++i)
    if (i != edge.from && i != edge.to && corridor.intersects(nodeOutline(nodes.at(i))))
      return true;
  return false;
}

QPointF drawFlowEdge(QPainter& painter, const QList<Node>& nodes, const QList<Edge>& edges,
                     int index, bool horizontal, const QColor& background, qreal outerLane,
                     bool routed) {
  const auto& edge = edges.at(index);
  const auto& a = nodes.at(edge.from);
  const auto& b = nodes.at(edge.to);
  QPainterPath path;
  QPointF caption;
  if (edge.from == edge.to) {
    int before = 0;
    for (int i = 0; i < index; ++i)
      before += edges.at(i).from == edge.from && edges.at(i).to == edge.to &&
                edges.at(i).style != LinkStyle::Invisible;
    const qreal radius = 52 + qMin(before, 4) * 12;
    const auto start = graphPort(a, QPointF(1, -.35)), end = graphPort(a, QPointF(1, .35));
    path.moveTo(start);
    path.cubicTo(start + QPointF(radius, -24), end + QPointF(radius, 24), end);
    caption = {a.rect.right() + radius * .8 + 84, a.rect.center().y()};
  } else if (routed) {
    const bool forward = horizontal ? b.rect.center().x() > a.rect.center().x()
                                    : b.rect.center().y() > a.rect.center().y();
    const auto start = graphPort(a, horizontal, forward), end = graphPort(b, horizontal, !forward);
    const auto bend = [&](const Node& node, QPointF port, bool positive) {
      qreal boundary = horizontal ? port.x() : port.y();
      for (const auto& peer : nodes)
        if (peer.rank == node.rank) {
          const qreal side = horizontal ? (positive ? peer.rect.right() : peer.rect.left())
                                        : (positive ? peer.rect.bottom() : peer.rect.top());
          boundary = positive ? qMax(boundary, side) : qMin(boundary, side);
        }
      boundary += positive ? 30 : -30;
      return horizontal ? QPointF(boundary, port.y()) : QPointF(port.x(), boundary);
    };
    const auto leave = bend(a, start, forward), enter = bend(b, end, !forward);
    const auto outside = [horizontal, outerLane](QPointF port) {
      return horizontal ? QPointF(port.x(), outerLane) : QPointF(outerLane, port.y());
    };
    const auto first = outside(leave), last = outside(enter);
    path.moveTo(start);
    path.lineTo(leave);
    path.lineTo(first);
    path.lineTo(last);
    path.lineTo(enter);
    path.lineTo(end);
    caption = (first + last) / 2;
  } else {
    const bool reverse = horizontal ? a.rect.center().x() > b.rect.center().x()
                                    : a.rect.center().y() > b.rect.center().y();
    const auto start = graphPort(a, horizontal, !reverse), end = graphPort(b, horizontal, reverse);
    path.moveTo(start);
    path.lineTo(end);
    caption = (start + end) / 2;
  }
  auto pen = painter.pen();
  pen.setWidthF(edge.style == LinkStyle::Thick ? 3.4 : 1.6);
  pen.setStyle(edge.style == LinkStyle::Dotted ? Qt::DashLine : Qt::SolidLine);
  painter.setPen(pen);
  painter.setBrush(Qt::NoBrush);
  painter.drawPath(path);
  const auto start = path.pointAtPercent(0), end = path.pointAtPercent(1);
  drawLinkHead(painter, start + QLineF::fromPolar(12, path.angleAtPercent(0)).p2(), start,
               edge.start, background);
  drawLinkHead(painter, end - QLineF::fromPolar(12, path.angleAtPercent(1)).p2(), end, edge.end,
               background);
  return caption;
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
    auto ranks = graphRanks(int(nodes.size()), links);
    if (std::any_of(edges.cbegin(), edges.cend(),
                    [](const Edge& edge) { return edge.minLength > 1 && edge.from != edge.to; })) {
      // Preserve the existing cycle ordering. Only forward constraints can be
      // satisfied in a cycle; DAGs retain every requested minimum rank span.
      QList<int> order;
      for (int i = 0; i < nodes.size(); ++i) order.append(i);
      std::stable_sort(order.begin(), order.end(),
                       [&](int a, int b) { return ranks.at(a) < ranks.at(b); });
      QList<int> position(nodes.size());
      for (int i = 0; i < order.size(); ++i) position[order.at(i)] = i;
      for (const int from : order)
        for (const auto& edge : edges)
          if (edge.from == from && position.at(edge.to) > position.at(from))
            ranks[edge.to] = qMax(ranks.at(edge.to), ranks.at(from) + edge.minLength);
    }
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
  qreal largestCircle = 0;
  for (auto& n : nodes) {
    const int wrap = Qt::TextWordWrap | Qt::AlignCenter | (states ? 0 : Qt::TextWrapAnywhere);
    const auto bounds = metrics.boundingRect(QRectF(0, 0, 220, 180), wrap, n.text);
    const bool shaped = n.shape >= NodeShape::Stadium && n.shape <= NodeShape::DoubleCircle;
    n.boxSize = QSizeF(qMax(120., qMin(shaped ? 300. : 250., bounds.width() + (shaped ? 56 : 36))),
                       qMax(54., bounds.height() + (n.shape == NodeShape::Cylinder ? 48 : 30)));
    nodeWidth = qMax(nodeWidth, n.boxSize.width());
    nodeHeight = qMax(nodeHeight, n.boxSize.height());
    if (!states && (n.shape == NodeShape::Ellipse || n.shape == NodeShape::DoubleCircle)) {
      n.textSize = bounds.size() + QSizeF(4, 4);
      n.diameter = qMax(80., std::hypot(bounds.width(), bounds.height()) +
                                 (n.shape == NodeShape::DoubleCircle ? 52 : 36));
      largestCircle = qMax(largestCircle, n.diameter);
    }
  }
  for (const auto& rank : ranks) maxParallel = qMax(maxParallel, static_cast<int>(rank.size()));
  const qreal cellWidth = qMax(nodeWidth, largestCircle),
              cellHeight = qMax(nodeHeight, largestCircle);
  const bool loops = std::any_of(edges.cbegin(), edges.cend(), [](const Edge& edge) {
    return edge.from == edge.to && edge.style != LinkStyle::Invisible;
  });
  const qreal loopGap = loops && !states ? 250 : 0;
  const qreal majorStep = horizontal ? cellWidth + 110 + loopGap : cellHeight + 82;
  const qreal minorStep = horizontal ? cellHeight + 40 : cellWidth + 48 + loopGap;
  const qreal majorSize = (maxRank + 1) * majorStep + 28 - (horizontal ? loopGap : 0),
              minorSize = maxParallel * minorStep + 28 - (horizontal ? 0 : loopGap);
  const bool routes = states && std::any_of(edges.cbegin(), edges.cend(), [&](const Edge& edge) {
                        return qAbs(nodes.at(edge.from).rank - nodes.at(edge.to).rank) > 1;
                      });
  qreal naturalWidth = (horizontal ? majorSize : minorSize) + (loops && states ? 118 : 0) +
                       (!horizontal && routes ? 88 : 0);
  qreal naturalHeight = horizontal ? minorSize + (routes ? 34 : 0) : majorSize;
  for (int rank = 0; rank <= maxRank; ++rank) {
    const auto group = ranks.value(rank);
    for (int j = 0; j < group.size(); ++j) {
      const qreal groupSpan = group.size() * minorStep - (horizontal ? 0 : loopGap);
      const qreal major = 28 + rank * majorStep,
                  minor = 14 + (minorSize - groupSpan) / 2 + j * minorStep;
      auto& node = nodes[group[j]];
      const qreal width = states          ? nodeWidth
                          : node.diameter ? node.diameter
                                          : node.boxSize.width(),
                  height = states          ? nodeHeight
                           : node.diameter ? node.diameter
                                           : node.boxSize.height();
      QRectF rect((horizontal ? major : minor) + (cellWidth - width) / 2,
                  (horizontal ? minor : major) + (cellHeight - height) / 2, width, height);
      if (mode == "RL") rect.moveLeft(majorSize - rect.right() - 14);
      if (mode == "BT") rect.moveTop(naturalHeight - rect.bottom() - 14);
      node.rect = rect;
    }
  }
  if (!states)
    for (const auto& edge : edges)
      if (edge.from == edge.to && edge.style != LinkStyle::Invisible)
        naturalWidth = qMax(naturalWidth,
                            nodes.at(edge.from).rect.right() + (edge.text.isEmpty() ? 100 : 250));
  qreal outerLane = 0;
  for (const auto& node : nodes)
    outerLane = qMax(outerLane, horizontal ? node.rect.bottom() : node.rect.right());
  outerLane += loops && !horizontal ? 85 : 17;
  QList<bool> flowRoutes;
  if (!states) {
    for (const auto& edge : edges) flowRoutes.append(flowEdgeNeedsLane(nodes, edge, horizontal));
    if (flowRoutes.contains(true)) {
      outerLane += 24;
      if (horizontal)
        naturalHeight = qMax(naturalHeight, outerLane + 34);
      else
        naturalWidth = qMax(naturalWidth, outerLane + 90);
    }
  }
  const qreal scale = qMin(1., qMin(820. / naturalWidth, (states ? 720. : 1400.) / naturalHeight));
  QImage image = canvas(qMin(820, qCeil(naturalWidth * scale)),
                        qMin(states ? 720 : 1400, qCeil(naturalHeight * scale)), dark);
  QPainter painter(&image);
  painter.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);
  painter.scale(scale, scale);
  painter.setFont(font);
  const QColor foreground(dark ? "#dde3ec" : "#445269"), background(dark ? "#262b31" : "#ffffff");
  for (int i = 0; i < edges.size(); ++i) {
    const auto& edge = edges.at(i);
    if (edge.style == LinkStyle::Invisible) continue;
    painter.setPen(QPen(foreground, 1.6));
    QPointF midpoint;
    if (states)
      midpoint = drawStateEdge(painter, nodes, edges, i, horizontal, outerLane);
    else
      midpoint = drawFlowEdge(painter, nodes, edges, i, horizontal, background, outerLane,
                              flowRoutes.at(i));
    if (!edge.text.isEmpty()) {
      const bool self = !states && edge.from == edge.to;
      const bool bounded = states || self || (!states && flowRoutes.at(i));
      auto caption = edge.text;
      if (self || (!states && flowRoutes.at(i))) caption.replace('\n', ' ');
      if (bounded) caption = metrics.elidedText(caption, Qt::ElideRight, 150);
      QRectF text = metrics.boundingRect(caption);
      if (bounded)
        midpoint.setX(
            qBound(text.width() / 2 + 6, midpoint.x(), naturalWidth - text.width() / 2 - 6));
      text.moveCenter(midpoint);
      text.adjust(-5, -4, 5, 4);
      painter.fillRect(text, background);
      painter.drawText(text, Qt::AlignCenter, caption);
    }
  }
  for (const auto& node : nodes) {
    const auto& style = node.style;
    const auto border =
        style.stroke.isValid() ? style.stroke : QColor(dark ? "#7fa3d2" : "#96b3da");
    const qreal width = style.width.value_or(1.5);
    QPen pen(border, width);
    if (width == 0 || border.alpha() == 0)
      pen.setStyle(Qt::NoPen);
    else if (style.dashes && !style.dashes->isEmpty()) {
      auto pattern = *style.dashes;
      for (auto& dash : pattern) dash /= width;
      pen.setDashPattern(pattern);
    }
    painter.setPen(pen);
    painter.setBrush(style.fill.isValid() ? style.fill : QColor(dark ? "#344359" : "#edf3fc"));
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
    painter.drawPath(nodeOutline(node));
    painter.setBrush(Qt::NoBrush);
    if (node.shape == NodeShape::DoubleCircle)
      painter.drawEllipse(node.rect.adjusted(8, 8, -8, -8));
    if (node.shape == NodeShape::Subroutine) {
      painter.drawLine(node.rect.topLeft() + QPointF(10, 0),
                       node.rect.bottomLeft() + QPointF(10, 0));
      painter.drawLine(node.rect.topRight() - QPointF(10, 0),
                       node.rect.bottomRight() - QPointF(10, 0));
    }
    if (node.shape == NodeShape::Cylinder)
      painter.drawEllipse(QRectF(node.rect.left(), node.rect.top(), node.rect.width(),
                                 qMin(24., node.rect.height() / 2)));
    painter.setPen(style.color.isValid() ? style.color : foreground);
    const bool shaped = node.shape >= NodeShape::Stadium && node.shape <= NodeShape::DoubleCircle;
    const qreal inset = shaped ? 24 : 12, vertical = node.shape == NodeShape::Cylinder ? 16 : 4;
    auto textRect = node.rect.adjusted(inset, vertical, -inset, -vertical);
    if (node.diameter) {
      textRect.setSize(node.textSize);
      textRect.moveCenter(node.rect.center());
    }
    painter.drawText(textRect,
                     Qt::AlignCenter | Qt::TextWordWrap | (states ? 0 : Qt::TextWrapAnywhere),
                     node.text);
  }
  return {image, {}};
}
}  // namespace
}  // namespace typist::render::mermaid_detail
