#include "MermaidInternal.h"

#include <QFontMetricsF>
#include <QHash>
#include <QLineF>
#include <QPainter>
#include <QRegularExpression>
#include <QTextLayout>
#include <QtMath>
#include <algorithm>
#include <utility>

namespace typist::render::mermaid_detail {
namespace {
struct Member {
  QString text;
  bool underlined = false, italic = false;
};

struct ClassBox {
  QString name, annotation;
  QList<Member> fields, methods;
  QRectF rect;
};

enum class Marker { None, OpenArrow, Triangle, Diamond, FilledDiamond };

struct Relation {
  int from, to;
  Marker left, right;
  bool dashed;
  QString text, fromCount, toCount;
};

struct Note {
  QString text, target;
  int anchor = -1;
  QRectF rect{};
  QStringList lines{};
};

struct ClassDiagram {
  QList<ClassBox> boxes;
  QList<Relation> relations;
  QList<Note> notes;
  QString direction = QStringLiteral("TB");
};

QString classId(QString id) { return id.startsWith(QChar(0x60)) ? id.mid(1, id.size() - 2) : id; }

QString noteText(const QString& source) {
  QString text;
  for (qsizetype i = 0; i < source.size(); ++i) {
    auto c = source.at(i);
    if (c == '\\' && i + 1 < source.size()) {
      c = source.at(++i);
      if (c == 'n')
        c = '\n';
      else if (c != '\\' && c != '"')
        text += '\\';
    }
    text += c;
  }
  text.replace(QRegularExpression("<br\\s*/?>", QRegularExpression::CaseInsensitiveOption), "\n");
  return text;
}

Marker readMarker(const QString& symbol) {
  if (symbol == "<|" || symbol == "|>") return Marker::Triangle;
  if (symbol == "<" || symbol == ">") return Marker::OpenArrow;
  if (symbol == "o") return Marker::Diamond;
  if (symbol == "*") return Marker::FilledDiamond;
  return Marker::None;
}

bool addMember(ClassBox& box, QString text) {
  static const QRegularExpression annotation(R"(^<<([^<>]+)>>$)");
  if (const auto match = annotation.match(text); match.hasMatch()) {
    box.annotation = match.captured(1).trimmed();
    return !box.annotation.isEmpty();
  }
  if (text.startsWith("<<") || text.contains('{') || text.contains('}') ||
      text.indexOf('~', 1) >= 0 || text.contains(":::"))
    return false;
  const bool method = text.contains('(');
  if (method != text.contains(')')) return false;
  const bool underlined = text.endsWith('$'), italic = method && text.endsWith('*');
  if (underlined || italic) text.chop(1);
  text = text.trimmed();
  if (text.isEmpty()) return false;
  (method ? box.methods : box.fields).append({text, underlined, italic});
  return box.fields.size() + box.methods.size() <= 18;
}

bool parseClasses(ClassDiagram& diagram, const QStringList& lines) {
  static const QString id =
      QStringLiteral(R"((?:\x60[^\x60]+\x60|[\p{L}\p{N}_]+(?:-[\p{L}\p{N}_]+)*))");
  static const QRegularExpression declaration(
      QStringLiteral(R"rx(^class\s+(%1)(?:\s*\["([^"]+)"\])?(?:\s*<<([^<>]+)>>)?\s*(\{\}|\{)?$)rx")
          .arg(id));
  static const QRegularExpression annotation(QStringLiteral(R"(^<<([^<>]+)>>\s+(%1)$)").arg(id));
  static const QRegularExpression relation(
      QStringLiteral(
          R"rx(^(%1)\s*(?:"([^"]+)"\s*)?(<\||[<o*])?(--|\.\.)(\|>|[>o*])?\s*(?:"([^"]+)"\s*)?(%1)\s*(?::(?!:)\s*(.+))?$)rx")
          .arg(id));
  static const QRegularExpression member(QStringLiteral(R"(^(%1)\s*:(?!:)\s*(.+)$)").arg(id));
  static const QRegularExpression note(
      QStringLiteral(R"rx(^note(?:\s+for\s+(%1))?\s+"((?:[^"\\]|\\.)*)"$)rx").arg(id));
  static const QRegularExpression direction(R"(^direction\s+(TB|TD|BT|LR|RL)$)");
  static const QRegularExpression metadata(R"(^acc(?:Title|Descr)\s*[:{])",
                                           QRegularExpression::CaseInsensitiveOption);
  QHash<QString, int> ids;
  const auto box = [&](QString name) {
    name = classId(name);
    const auto found = ids.constFind(name);
    if (found != ids.cend()) return *found;
    const int index = int(diagram.boxes.size());
    ids.insert(name, index);
    diagram.boxes.append({name, {}, {}, {}, {}});
    return index;
  };

  int open = -1;
  for (qsizetype i = 1; i < lines.size(); ++i) {
    auto line = lines.at(i).trimmed();
    if (line.startsWith("%%{") || metadata.match(line).hasMatch()) return false;
    if (line.isEmpty() || line.startsWith("%%")) continue;
    if (line.endsWith(';')) {
      line.chop(1);
      line = line.trimmed();
    }
    if (open >= 0) {
      if (note.match(line).hasMatch()) return false;
      if (line == "}")
        open = -1;
      else if (!addMember(diagram.boxes[open], line))
        return false;
    } else if (const auto entry = note.match(line); entry.hasMatch()) {
      diagram.notes.append({noteText(entry.captured(2)), classId(entry.captured(1))});
    } else if (const auto orientation = direction.match(line); orientation.hasMatch()) {
      diagram.direction = orientation.captured(1);
    } else if (const auto declarationMatch = declaration.match(line); declarationMatch.hasMatch()) {
      const auto index = box(declarationMatch.captured(1));
      auto& entry = diagram.boxes[index];
      if (declarationMatch.capturedStart(2) >= 0)
        entry.name = label(declarationMatch.captured(2)).simplified();
      if (declarationMatch.capturedStart(3) >= 0)
        entry.annotation = declarationMatch.captured(3).trimmed();
      if (declarationMatch.captured(4) == "{") open = index;
    } else if (const auto stereotype = annotation.match(line); stereotype.hasMatch()) {
      const auto index = box(stereotype.captured(2));
      diagram.boxes[index].annotation = stereotype.captured(1).trimmed();
    } else if (const auto link = relation.match(line); link.hasMatch()) {
      const auto from = box(link.captured(1)), to = box(link.captured(7));
      diagram.relations.append({from, to, readMarker(link.captured(3)),
                                readMarker(link.captured(5)), link.captured(4) == "..",
                                label(link.captured(8)).simplified(), link.captured(2),
                                link.captured(6)});
    } else if (const auto definition = member.match(line); definition.hasMatch()) {
      const auto index = box(definition.captured(1));
      if (!addMember(diagram.boxes[index], definition.captured(2).trimmed())) return false;
    } else {
      return false;
    }
    if (diagram.boxes.size() > 24 || diagram.relations.size() > 48 || diagram.notes.size() > 16)
      return false;
  }
  if (open >= 0 || (diagram.boxes.isEmpty() && diagram.notes.isEmpty())) return false;
  for (auto& entry : diagram.notes)
    if (!entry.target.isEmpty()) {
      const auto anchor = ids.constFind(entry.target);
      if (anchor == ids.cend()) return false;
      entry.anchor = *anchor;
    }
  return true;
}

struct Theme {
  QColor ink, line, title, surface, paper;
};

struct Fonts {
  QFont body, heading, annotation;
  qreal rowHeight;
};

QStringList noteLines(const QString& text, const QFont& font, qreal width) {
  QStringList lines;
  const auto paragraphs = text.split('\n');
  for (qsizetype i = 0; i < paragraphs.size() && lines.size() < 6; ++i) {
    const auto& paragraph = paragraphs.at(i);
    QTextLayout layout(paragraph.isEmpty() ? QStringLiteral(" ") : paragraph, font);
    QTextOption option;
    option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    layout.setTextOption(option);
    layout.beginLayout();
    while (lines.size() < 6) {
      auto line = layout.createLine();
      if (!line.isValid()) break;
      line.setLineWidth(width);
      auto part = paragraph.mid(line.textStart(), line.textLength()).trimmed();
      if (lines.size() == 5 &&
          (i + 1 < paragraphs.size() || line.textStart() + line.textLength() < paragraph.size()))
        part = QFontMetricsF(font).elidedText(part + QChar(0x2026), Qt::ElideRight, width);
      lines.append(part);
    }
    layout.endLayout();
  }
  if (lines.isEmpty()) lines.append(QString());
  return lines;
}

void drawNote(QPainter& painter, const Note& note, const Fonts& fonts, const Theme& theme,
              bool dark) {
  const auto rect = note.rect;
  painter.setPen(QPen(QColor(dark ? "#b7a272" : "#c9ad58"), 1.2));
  painter.setBrush(QColor(dark ? "#514936" : "#fff6d6"));
  painter.drawPolygon(QPolygonF({rect.topLeft(),
                                 {rect.right() - 10, rect.top()},
                                 {rect.right(), rect.top() + 10},
                                 rect.bottomRight(),
                                 rect.bottomLeft()}));
  painter.setBrush(theme.paper);
  painter.drawPolygon(QPolygonF({{rect.right() - 10, rect.top()},
                                 {rect.right() - 10, rect.top() + 10},
                                 {rect.right(), rect.top() + 10}}));
  painter.setFont(fonts.body);
  painter.setPen(theme.ink);
  qreal y = rect.top() + 10;
  for (const auto& line : note.lines) {
    painter.drawText(QRectF(rect.left() + 12, y, rect.width() - 24, fonts.rowHeight),
                     Qt::AlignLeft | Qt::AlignVCenter, line);
    y += fonts.rowHeight;
  }
}

qreal headingHeight(const ClassBox& box, const Fonts& fonts) {
  return fonts.rowHeight + 10 + (box.annotation.isEmpty() ? 0 : fonts.rowHeight - 3);
}

void drawClass(QPainter& painter, const ClassBox& box, const Fonts& fonts, const Theme& theme) {
  const auto rect = box.rect;
  const qreal titleHeight = headingHeight(box, fonts);
  painter.fillRect(rect, theme.surface);
  painter.fillRect(QRectF(rect.topLeft(), QSizeF(rect.width(), titleHeight)), theme.title);
  painter.setPen(QPen(theme.line, 1.15));
  painter.setBrush(Qt::NoBrush);
  painter.drawRect(rect);
  painter.setPen(theme.ink);
  qreal y = rect.top() + 5;
  const qreal textWidth = rect.width() - 20;
  if (!box.annotation.isEmpty()) {
    painter.setFont(fonts.annotation);
    const auto text = QString(QChar(0xab)) + box.annotation + QChar(0xbb);
    painter.drawText(QRectF(rect.left() + 10, y, textWidth, fonts.rowHeight - 3), Qt::AlignCenter,
                     QFontMetricsF(fonts.annotation).elidedText(text, Qt::ElideRight, textWidth));
    y += fonts.rowHeight - 3;
  }
  painter.setFont(fonts.heading);
  painter.drawText(QRectF(rect.left() + 10, y, textWidth, fonts.rowHeight), Qt::AlignCenter,
                   QFontMetricsF(fonts.heading).elidedText(box.name, Qt::ElideRight, textWidth));
  y = rect.top() + titleHeight;
  for (const auto* members : {&box.fields, &box.methods}) {
    if (members->isEmpty()) continue;
    painter.setPen(QPen(theme.line, 1));
    painter.drawLine(QPointF(rect.left(), y), QPointF(rect.right(), y));
    y += 4;
    painter.setPen(theme.ink);
    for (const auto& member : *members) {
      auto font = fonts.body;
      font.setUnderline(member.underlined);
      font.setItalic(member.italic);
      painter.setFont(font);
      const auto text = QFontMetricsF(font).elidedText(member.text, Qt::ElideRight, textWidth);
      painter.drawText(QRectF(rect.left() + 10, y, textWidth, fonts.rowHeight),
                       Qt::AlignLeft | Qt::AlignVCenter, text);
      y += fonts.rowHeight;
    }
    y += 4;
  }
}

QPointF unitVector(QPointF vector) {
  const qreal length = qHypot(vector.x(), vector.y());
  return length ? vector / length : QPointF(1, 0);
}

void drawMarker(QPainter& painter, Marker marker, QPointF previous, QPointF tip,
                const Theme& theme) {
  if (marker == Marker::None) return;
  const auto back = unitVector(previous - tip);
  const QPointF side(-back.y(), back.x());
  const QPointF center = tip + back * 11;
  painter.setPen(QPen(theme.ink, 1.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
  if (marker == Marker::OpenArrow) {
    painter.drawPolyline(QPolygonF({center + side * 5, tip, center - side * 5}));
  } else if (marker == Marker::Triangle) {
    painter.setBrush(theme.paper);
    painter.drawPolygon(QPolygonF({tip, center + side * 6, center - side * 6}));
  } else {
    painter.setBrush(marker == Marker::FilledDiamond ? theme.ink : theme.paper);
    const auto middle = tip + back * 7;
    painter.drawPolygon(
        QPolygonF({tip, middle + side * 4.5, tip + back * 14, middle - side * 4.5}));
  }
}

void edgeLabel(QPainter& painter, const QString& text, QPointF center, const Theme& theme,
               QSizeF area, qreal textWidth) {
  if (text.isEmpty()) return;
  const QFontMetricsF metrics(painter.font());
  const auto caption = metrics.elidedText(text, Qt::ElideRight, textWidth);
  const qreal width = metrics.horizontalAdvance(caption) + 6, height = metrics.height() + 2;
  center.setX(qBound(width / 2 + 3, center.x(), area.width() - width / 2 - 3));
  center.setY(qBound(height / 2 + 3, center.y(), area.height() - height / 2 - 3));
  const QRectF rect(center - QPointF(width / 2, height / 2), QSizeF(width, height));
  painter.fillRect(rect, theme.paper);
  painter.setPen(theme.ink);
  painter.drawText(rect, Qt::AlignCenter, caption);
}

void drawRelation(QPainter& painter, const Relation& relation, const QPolygonF& path,
                  const Theme& theme, QSizeF area) {
  painter.setPen(QPen(theme.ink, 1.4, relation.dashed ? Qt::DashLine : Qt::SolidLine, Qt::RoundCap,
                      Qt::RoundJoin));
  painter.setBrush(Qt::NoBrush);
  painter.drawPolyline(path);
  drawMarker(painter, relation.left, path.at(1), path.first(), theme);
  drawMarker(painter, relation.right, path.at(path.size() - 2), path.last(), theme);
  if (relation.from == relation.to) {
    const qreal center = path.first().x() + (path.at(1).x() < path.first().x() ? -33 : 33);
    edgeLabel(painter, relation.text, {center, path.first().y() - 12}, theme, area, 54);
    edgeLabel(painter, relation.fromCount, {center, path.first().y() + 3}, theme, area, 48);
    edgeLabel(painter, relation.toCount, {center, path.last().y() + 12}, theme, area, 48);
    return;
  }
  QLineF middle;
  const qsizetype begin = path.size() == 2 ? 1 : 2;
  for (qsizetype i = begin; i < path.size() + 1 - begin; ++i) {
    const QLineF segment(path.at(i - 1), path.at(i));
    if (segment.length() > middle.length()) middle = segment;
  }
  const auto normal = [](QPointF line) { return unitVector(QPointF(-line.y(), line.x())); };
  const auto midpoint = (middle.p1() + middle.p2()) / 2;
  edgeLabel(painter, relation.text, midpoint + normal(middle.p2() - middle.p1()) * 10, theme, area,
            130);

  const auto nearEndpoint = [&](QPointF tip, QPointF next, qreal side) {
    return tip + unitVector(next - tip) * qMin(25., QLineF(tip, next).length() * .45) +
           normal(next - tip) * side;
  };
  edgeLabel(painter, relation.fromCount, nearEndpoint(path.first(), path.at(1), 14), theme, area,
            56);
  edgeLabel(painter, relation.toCount, nearEndpoint(path.last(), path.at(path.size() - 2), 14),
            theme, area, 56);
}

QImage drawClasses(ClassDiagram diagram, bool dark) {
  Theme theme{QColor(dark ? "#e8edf5" : "#243249"), QColor(dark ? "#8197b6" : "#8ca4ca"),
              QColor(dark ? "#394d67" : "#e2ecfb"), QColor(dark ? "#303945" : "#f7faff"),
              QColor(dark ? "#262b31" : "#ffffff")};
  const auto font = diagramFont();
  Fonts fonts{font, font, font, qMax(19., QFontMetricsF(font).height() + 2)};
  fonts.heading.setBold(true);
  fonts.annotation.setPointSizeF(9.5);
  fonts.annotation.setItalic(true);
  const QFontMetricsF body(font), heading(fonts.heading), annotation(fonts.annotation);
  const bool horizontal = diagram.direction == "LR" || diagram.direction == "RL";
  const bool reversed = diagram.direction == "RL" || diagram.direction == "BT";
  const bool loops = std::any_of(diagram.relations.cbegin(), diagram.relations.cend(),
                                 [](const Relation& edge) { return edge.from == edge.to; });
  const bool attachedNotes = std::any_of(diagram.notes.cbegin(), diagram.notes.cend(),
                                         [](const Note& note) { return note.anchor >= 0; });
  qreal nodeWidth = 132;
  for (auto& box : diagram.boxes) {
    nodeWidth = qMax(nodeWidth, heading.horizontalAdvance(box.name) + 22);
    nodeWidth = qMax(nodeWidth, annotation.horizontalAdvance(box.annotation) + 36);
    for (const auto* members : {&box.fields, &box.methods})
      for (const auto& member : *members)
        nodeWidth = qMax(nodeWidth, body.horizontalAdvance(member.text) + 22);
    box.rect.setHeight(headingHeight(box, fonts) +
                       (box.fields.size() + box.methods.size()) * fonts.rowHeight +
                       (int(!box.fields.isEmpty()) + int(!box.methods.isEmpty())) * 8);
  }
  nodeWidth = qMin(256., nodeWidth);
  if (!diagram.notes.isEmpty()) nodeWidth = qMax(220., nodeWidth);
  QList<qreal> noteSpan(diagram.boxes.size(), 0);
  for (auto& note : diagram.notes) {
    note.lines = noteLines(note.text, font, nodeWidth - 24);
    note.rect.setSize(QSizeF(nodeWidth, qMax(50., note.lines.size() * fonts.rowHeight + 20)));
    if (note.anchor >= 0) noteSpan[note.anchor] += note.rect.height() + 12;
  }
  QList<QSizeF> cells;
  for (int i = 0; i < diagram.boxes.size(); ++i) {
    const qreal height = diagram.boxes.at(i).rect.height(), notes = noteSpan.at(i);
    cells.append(horizontal ? QSizeF(nodeWidth, height + notes)
                            : QSizeF(nodeWidth + (notes > 0 ? nodeWidth + 18 : 0),
                                     qMax(height, notes - 12)));
  }

  QList<QPair<int, int>> links;
  QList<bool> connected(diagram.boxes.size(), false);
  for (const auto& edge : diagram.relations) {
    const auto anchor = [](Marker marker) {
      return marker == Marker::Triangle || marker == Marker::Diamond ||
             marker == Marker::FilledDiamond;
    };
    const bool reversed = (!anchor(edge.left) && anchor(edge.right)) ||
                          (edge.left == Marker::OpenArrow && edge.right == Marker::None);
    if (reversed)
      links.append({edge.to, edge.from});
    else
      links.append({edge.from, edge.to});
    connected[edge.from] = connected[edge.to] = true;
  }
  const auto ranks = graphRanks(int(diagram.boxes.size()), links);
  QList<QList<int>> layers;
  for (int i = 0; i < ranks.size(); ++i) {
    if (!connected.at(i)) continue;
    const int rank = ranks.at(i);
    if (rank >= layers.size()) layers.resize(rank + 1);
    layers[rank].append(i);
  }
  int isolated = 0;
  for (int i = 0; i < connected.size(); ++i) {
    if (connected.at(i)) continue;
    if (isolated++ % 3 == 0) layers.append(QList<int>());
    layers.last().append(i);
  }

  const qreal margin = attachedNotes && loops && !horizontal ? 80 : 25, gap = horizontal ? 140 : 96,
              crossGap = 68;
  qreal major = margin, minor = 0;
  QList<QPair<qreal, qreal>> span;
  QList<qreal> thickness;
  QList<int> layerIndex(diagram.boxes.size(), 0);
  for (int i = 0; i < layers.size(); ++i) {
    qreal band = 0, cross = 0;
    for (const int index : layers.at(i)) {
      layerIndex[index] = i;
      const auto cell = cells.at(index);
      band = qMax(band, horizontal ? cell.width() : cell.height());
      cross += (horizontal ? cell.height() : cell.width()) + crossGap;
    }
    thickness.append(qMax(0., cross - crossGap));
    minor = qMax(minor, thickness.last());
    span.append({major, major + band});
    major += band + gap;
  }
  major += margin - (layers.isEmpty() ? 0 : gap);
  const bool detours =
      std::any_of(diagram.relations.cbegin(), diagram.relations.cend(), [&](const Relation& edge) {
        return qAbs(layerIndex.at(edge.from) - layerIndex.at(edge.to)) > 1;
      });
  const qreal cross = minor + margin * 2;
  QSizeF area(horizontal ? major + (loops ? 72 : 0) : cross + (loops || detours ? 76 : 0),
              horizontal ? cross + (detours ? 76 : 0) : major);
  const auto point = [horizontal](qreal along, qreal across) {
    return horizontal ? QPointF(along, across) : QPointF(across, along);
  };
  QList<QPointF> origins(diagram.boxes.size());
  for (int i = 0; i < layers.size(); ++i) {
    auto& [begin, end] = span[i];
    if (reversed) {
      const qreal start = major - end;
      end = major - begin;
      begin = start;
    }
    qreal across = margin + (minor - thickness.at(i)) / 2;
    for (const int index : layers.at(i)) {
      auto& rect = diagram.boxes[index].rect;
      rect.setWidth(nodeWidth);
      const qreal height = rect.height();
      const auto cell = cells.at(index);
      const auto origin =
          point((begin + end - (horizontal ? cell.width() : cell.height())) / 2, across);
      origins[index] = origin;
      rect.moveTopLeft(origin + QPointF(0, horizontal ? 0 : (cell.height() - height) / 2));
      across += (horizontal ? cell.height() : cell.width()) + crossGap;
    }
  }
  for (int i = 0; i < diagram.boxes.size(); ++i) {
    const auto rect = diagram.boxes.at(i).rect;
    qreal y = horizontal ? rect.bottom() + 12
                         : origins.at(i).y() + (cells.at(i).height() - (noteSpan.at(i) - 12)) / 2;
    for (auto& note : diagram.notes)
      if (note.anchor == i) {
        note.rect.moveTopLeft({horizontal ? rect.left() : rect.right() + 18, y});
        y += note.rect.height() + 12;
      }
  }
  int generalCount = 0;
  for (const auto& note : diagram.notes) generalCount += note.anchor < 0;
  if (generalCount) {
    const int columns = qMin(3, generalCount);
    qreal y = diagram.boxes.isEmpty() ? margin : area.height() + 24, rowHeight = 0;
    int at = 0;
    for (auto& note : diagram.notes) {
      if (note.anchor >= 0) continue;
      const int column = at++ % columns;
      if (column == 0 && at > 1) {
        y += rowHeight + 16;
        rowHeight = 0;
      }
      note.rect.moveTopLeft({margin + column * (nodeWidth + 20), y});
      rowHeight = qMax(rowHeight, note.rect.height());
    }
    area.setWidth(qMax(area.width(), margin * 2 + columns * nodeWidth + (columns - 1) * 20));
    area.setHeight(y + rowHeight + margin);
  }

  const qreal scale = qMin(1., qMin(940. / area.width(), 1260. / area.height()));
  auto image = canvas(qCeil(area.width() * scale), qCeil(area.height() * scale), dark);
  if (image.isNull()) return image;
  QPainter painter(&image);
  painter.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);
  painter.scale(scale, scale);
  auto captions = fonts.annotation;
  captions.setItalic(false);
  painter.setFont(captions);
  for (int i = 0; i < diagram.relations.size(); ++i) {
    const auto& edge = diagram.relations.at(i);
    const auto a = diagram.boxes.at(edge.from).rect, b = diagram.boxes.at(edge.to).rect;
    const int ai = layerIndex.at(edge.from), bi = layerIndex.at(edge.to);
    int before = 0, total = 0;
    for (int j = 0; j < diagram.relations.size(); ++j) {
      const auto& other = diagram.relations.at(j);
      if ((other.from == edge.from && other.to == edge.to) ||
          (other.from == edge.to && other.to == edge.from)) {
        ++total;
        if (j < i) ++before;
      }
    }
    const qreal offset = (before - (total - 1) / 2.) * qMin(20., 24. / qMax(1, total - 1));
    QPolygonF path;
    if (edge.from == edge.to) {
      const bool left = attachedNotes && !horizontal;
      const qreal x = left ? a.left() : a.right();
      const qreal turn = x + (left ? -1 : 1) * (24 + qMin(3, before) * 8);
      const qreal y = a.center().y();
      path = QPolygonF({{x, y - 9}, {turn, y - 9}, {turn, y + 9}, {x, y + 9}});
    } else if (attachedNotes && ai == bi) {
      const qreal lane = span.at(ai).first - 12;
      const auto start =
          horizontal ? QPointF(a.left(), a.center().y()) : QPointF(a.center().x(), a.top());
      const auto end =
          horizontal ? QPointF(b.left(), b.center().y()) : QPointF(b.center().x(), b.top());
      path = QPolygonF({start, point(lane, horizontal ? start.y() : start.x()),
                        point(lane, horizontal ? end.y() : end.x()), end});
    } else {
      const bool sideBySide = ai == bi ? !horizontal : horizontal;
      const bool forward =
          sideBySide ? a.center().x() < b.center().x() : a.center().y() < b.center().y();
      const auto delta = b.center() - a.center();
      const qreal fan = qBound(-30., (sideBySide ? delta.y() : delta.x()) * .12, 30.);
      const auto port = [sideBySide](QRectF rect, bool positive, qreal shift) {
        const qreal room = qMax(0., (sideBySide ? rect.height() : rect.width()) / 2 - 8);
        const qreal inset = qBound(-room, shift, room);
        return sideBySide
                   ? QPointF(positive ? rect.right() : rect.left(), rect.center().y() + inset)
                   : QPointF(rect.center().x() + inset, positive ? rect.bottom() : rect.top());
      };
      const auto start = port(a, forward, offset + fan);
      const auto end = port(b, !forward, offset - fan);
      if (ai != bi && qAbs(ai - bi) > 1) {
        const auto as = span.at(ai), bs = span.at(bi);
        const qreal stubA = (forward ? as.second + 26 : as.first - 26) + offset;
        const qreal stubB = (forward ? bs.first - 26 : bs.second + 26) + offset;
        const qreal lane = cross + 17 + offset;
        const qreal from = horizontal ? start.y() : start.x();
        const qreal to = horizontal ? end.y() : end.x();
        path = QPolygonF({start, point(stubA, from), point(stubA, lane), point(stubB, lane),
                          point(stubB, to), end});
      } else if (attachedNotes && ai != bi) {
        const auto as = span.at(ai), bs = span.at(bi);
        const qreal middle = forward ? (as.second + bs.first) / 2 : (as.first + bs.second) / 2;
        path = QPolygonF({start, point(middle, horizontal ? start.y() : start.x()),
                          point(middle, horizontal ? end.y() : end.x()), end});
      } else {
        path = QPolygonF({start, end});
      }
    }
    if (path.size() == 4 && path.at(1) == path.at(2)) path = QPolygonF({path.first(), path.last()});
    drawRelation(painter, edge, path, theme, area);
  }
  painter.setPen(QPen(QColor(dark ? "#b7a272" : "#c9ad58"), 1, Qt::DashLine));
  painter.setBrush(Qt::NoBrush);
  for (const auto& note : diagram.notes)
    if (note.anchor >= 0) {
      const auto anchor = diagram.boxes.at(note.anchor).rect;
      const auto start = horizontal ? QPointF(anchor.left() + 8, anchor.bottom())
                                    : QPointF(anchor.right(), anchor.bottom() - 8);
      const qreal lane = horizontal ? anchor.left() - 14 : anchor.right() + 9;
      painter.drawPolyline(QPolygonF({start,
                                      {lane, start.y()},
                                      {lane, note.rect.center().y()},
                                      {note.rect.left(), note.rect.center().y()}}));
    }
  for (const auto& box : diagram.boxes) drawClass(painter, box, fonts, theme);
  for (const auto& note : diagram.notes) drawNote(painter, note, fonts, theme, dark);
  return image;
}
}  // namespace

RenderResult classDiagram(const QStringList& lines, bool dark) {
  ClassDiagram diagram;
  if (!parseClasses(diagram, lines)) return {{}, unsupported};
  return {drawClasses(std::move(diagram), dark), {}};
}
}  // namespace typist::render::mermaid_detail
