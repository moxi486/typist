#include "MermaidInternal.h"

#include <QFontMetricsF>
#include <QHash>
#include <QLineF>
#include <QPainter>
#include <QRegularExpression>
#include <QTextLayout>
#include <QtMath>

namespace typist::render::mermaid_detail {
namespace {
enum class EventType { Message, Note, Open, Divide, Close, Activate, Deactivate };
enum class NotePlacement { Over, Left, Right };
enum class MessageHead { None, Filled, Open, Cross };
constexpr qreal activationWidth = 12, activationStep = 6;
constexpr qreal sideNoteWidth = 170, sideNoteGap = 18;

struct Participant {
  QString name;
  bool actor = false;
  bool used = false, created = false, destroyed = false;
  qreal headTop = 18, lineTop = 0, lineBottom = 0;
};

struct Event {
  EventType type;
  QString text;
  int from = -1, to = -1, frame = -1, number = 0, activation = -1;
  int fromDepth = 0, toDepth = 0;
  int created = -1, destroyed = -1;
  bool dashed = false, bidirectional = false;
  MessageHead head = MessageHead::Filled;
  NotePlacement placement = NotePlacement::Over;
  QRectF rect{};
  QStringList headerLines;
};

struct Frame {
  QString kind;
  int depth;
  bool hasElse = false;
  QRectF rect;
  QColor highlight;
};

struct Activation {
  int participant, depth;
  qreal top = 0, bottom = 0;
};

struct Sequence {
  QList<Participant> participants;
  QList<Event> events;
  QList<Frame> frames;
  QList<Activation> activations;
};

QColor highlightColor(const QString& text) {
  static const QRegularExpression color(
      R"(^(rgb|rgba)\(\s*([0-9]{1,3})\s*,\s*([0-9]{1,3})\s*,\s*([0-9]{1,3})\s*(?:,\s*([0-9]+(?:\.[0-9]*)?|\.[0-9]+)\s*)?\)$)",
      QRegularExpression::CaseInsensitiveOption);
  const auto match = color.match(text);
  if (!match.hasMatch() || (match.captured(1).compare("rgba", Qt::CaseInsensitive) == 0) !=
                               (match.capturedStart(5) >= 0))
    return {};
  const int red = match.captured(2).toInt(), green = match.captured(3).toInt(),
            blue = match.captured(4).toInt();
  if (red > 255 || green > 255 || blue > 255) return {};
  bool ok = true;
  const qreal alpha = match.capturedStart(5) < 0 ? 1 : match.captured(5).toDouble(&ok);
  if (!ok || alpha < 0 || alpha > 1) return {};
  QColor result(red, green, blue);
  result.setAlphaF(alpha);
  return result;
}

bool parseSequence(const QStringList& lines, Sequence& sequence) {
  static const QString id = QStringLiteral(R"([\p{L}\p{N}_]+)");
  static const QRegularExpression declaration(
      QStringLiteral(R"(^(participant|actor)\s+(%1)(?:\s+as\s+(.+))?$)").arg(id));
  static const QRegularExpression message(
      QStringLiteral(
          R"(^(%1)\s*(<<-->>|<<->>|-->>|->>|-->|->|--x|-x|--\)|-\))\s*([+-])?\s*(%1)\s*:\s*(.*)$)")
          .arg(id));
  static const QRegularExpression creation(
      QStringLiteral(R"(^create\s+(participant|actor)\s+(%1)(?:\s+as\s+(.+))?$)").arg(id));
  static const QRegularExpression destruction(QStringLiteral(R"(^destroy\s+(%1)$)").arg(id));
  static const QRegularExpression activation(
      QStringLiteral(R"(^(activate|deactivate)\s+(%1)$)").arg(id));
  static const QRegularExpression note(
      QStringLiteral(R"(^Note\s+(over|left\s+of|right\s+of)\s+(%1)(?:\s*,\s*(%1))?\s*:\s*(.*)$)")
          .arg(id),
      QRegularExpression::CaseInsensitiveOption);
  static const QRegularExpression control(
      R"(^(alt|opt|loop|par|critical|break|rect|else|and|option)(?:\s+(.*))?$)");
  QHash<QString, int> ids;
  QList<int> stack;
  QList<QList<int>> active;
  int count = 0, number = 0;
  int pendingCreate = -1, pendingDestroy = -1;
  bool numbered = false;
  const auto participant = [&](const QString& token) {
    const auto found = ids.constFind(token);
    if (found != ids.cend()) return *found;
    const int index = int(sequence.participants.size());
    sequence.participants.append({token, false});
    active.append(QList<int>());
    ids.insert(token, index);
    return index;
  };
  const auto activate = [&](int index, bool start) {
    if (sequence.participants.at(index).destroyed) return false;
    sequence.participants[index].used = true;
    auto& levels = active[index];
    if ((start && (levels.size() >= 8 || sequence.activations.size() >= 64)) ||
        (!start && levels.isEmpty()))
      return false;
    Event event{start ? EventType::Activate : EventType::Deactivate, {}};
    if (start) {
      event.activation = int(sequence.activations.size());
      sequence.activations.append({index, int(levels.size())});
      if (!sequence.events.isEmpty() && sequence.events.last().type == EventType::Message &&
          sequence.events.last().to == index)
        sequence.events.last().toDepth = int(levels.size()) + 1;
      levels.append(event.activation);
    } else {
      event.activation = levels.takeLast();
    }
    sequence.events.append(event);
    return true;
  };
  for (qsizetype i = 1; i < lines.size(); ++i) {
    const auto line = lines.at(i).trimmed();
    if (line.startsWith("%%{")) return false;
    if (line.isEmpty() || line.startsWith("%%")) continue;
    const auto m = message.match(line);
    if ((pendingCreate >= 0 || pendingDestroy >= 0) && !m.hasMatch()) return false;
    if (line == "autonumber") {
      numbered = true;
    } else if (const auto created = creation.match(line); created.hasMatch()) {
      const int index = participant(created.captured(2));
      auto& entry = sequence.participants[index];
      if (entry.used || entry.created || entry.destroyed) return false;
      entry.created = true;
      entry.actor = created.captured(1) == "actor";
      if (created.capturedStart(3) >= 0) entry.name = label(created.captured(3));
      pendingCreate = index;
    } else if (const auto destroyed = destruction.match(line); destroyed.hasMatch()) {
      const auto found = ids.constFind(destroyed.captured(1));
      if (found == ids.cend() || sequence.participants.at(*found).destroyed) return false;
      pendingDestroy = *found;
    } else if (const auto declared = declaration.match(line); declared.hasMatch()) {
      const int index = participant(declared.captured(2));
      auto& entry = sequence.participants[index];
      if (entry.destroyed) return false;
      entry.actor = declared.captured(1) == "actor";
      if (declared.capturedStart(3) >= 0) entry.name = label(declared.captured(3));
    } else if (const auto bar = activation.match(line); bar.hasMatch()) {
      const int index = participant(bar.captured(2));
      if (!activate(index, bar.captured(1) == "activate")) return false;
    } else if (m.hasMatch()) {
      const int from = participant(m.captured(1)), to = participant(m.captured(4));
      if (sequence.participants.at(from).destroyed || sequence.participants.at(to).destroyed ||
          (pendingCreate >= 0 && (pendingCreate != to || from == to)) ||
          (pendingDestroy >= 0 && pendingDestroy != from && pendingDestroy != to))
        return false;
      Event event{EventType::Message, label(m.captured(5))};
      event.from = from;
      event.to = to;
      event.fromDepth = int(active.at(from).size());
      event.toDepth = int(active.at(to).size()) + (m.captured(3) == "+" ? 1 : 0);
      const auto arrowType = m.captured(2);
      event.dashed = arrowType.contains("--");
      event.bidirectional = arrowType.startsWith("<<");
      event.head = arrowType.endsWith('x')    ? MessageHead::Cross
                   : arrowType.endsWith(')')  ? MessageHead::Open
                   : arrowType.endsWith(">>") ? MessageHead::Filled
                                              : MessageHead::None;
      event.created = pendingCreate;
      event.destroyed = pendingDestroy;
      event.number = numbered ? ++number : 0;
      sequence.events.append(event);
      sequence.participants[from].used = sequence.participants[to].used = true;
      if ((m.captured(3) == "+" && !activate(to, true)) ||
          (m.captured(3) == "-" && !activate(from, false)))
        return false;
      if (pendingDestroy >= 0) {
        while (!active.at(pendingDestroy).isEmpty()) activate(pendingDestroy, false);
        sequence.participants[pendingDestroy].destroyed = true;
      }
      pendingCreate = pendingDestroy = -1;
      ++count;
    } else if (const auto n = note.match(line); n.hasMatch()) {
      Event event{EventType::Note, label(n.captured(4))};
      const auto position = n.captured(1).toLower();
      event.placement = position.startsWith("left")    ? NotePlacement::Left
                        : position.startsWith("right") ? NotePlacement::Right
                                                       : NotePlacement::Over;
      if (event.placement != NotePlacement::Over && n.capturedStart(3) >= 0) return false;
      event.from = participant(n.captured(2));
      event.to = n.capturedStart(3) < 0 ? event.from : participant(n.captured(3));
      event.fromDepth = int(active.at(event.from).size());
      if (sequence.participants.at(event.from).destroyed ||
          sequence.participants.at(event.to).destroyed)
        return false;
      sequence.participants[event.from].used = sequence.participants[event.to].used = true;
      sequence.events.append(event);
      ++count;
    } else if (const auto block = control.match(line); block.hasMatch()) {
      const auto kind = block.captured(1), text = label(block.captured(2));
      const bool divider = kind == "else" || kind == "and" || kind == "option";
      if (divider) {
        if (stack.isEmpty()) return false;
        auto& frame = sequence.frames[stack.last()];
        if ((kind == "else" && (frame.kind != "alt" || frame.hasElse)) ||
            (kind == "and" && frame.kind != "par") ||
            (kind == "option" && frame.kind != "critical"))
          return false;
        frame.hasElse = kind == "else";
        Event event{EventType::Divide, text};
        event.frame = stack.last();
        sequence.events.append(event);
      } else {
        if (stack.size() >= 6 || sequence.frames.size() >= 24) return false;
        const auto highlight =
            kind == "rect" ? highlightColor(block.captured(2).trimmed()) : QColor();
        if (kind == "rect" && !highlight.isValid()) return false;
        const int frame = int(sequence.frames.size());
        sequence.frames.append({kind, int(stack.size()), false, {}, highlight});
        stack.append(frame);
        Event event{EventType::Open, text};
        event.frame = frame;
        sequence.events.append(event);
      }
    } else if (line == "end") {
      if (stack.isEmpty()) return false;
      Event event{EventType::Close, {}};
      event.frame = stack.takeLast();
      sequence.events.append(event);
    } else {
      return false;
    }
    if (sequence.participants.size() > 8 || count > 60 || sequence.events.size() > 360)
      return false;
  }
  return stack.isEmpty() && pendingCreate < 0 && pendingDestroy < 0 &&
         !sequence.participants.isEmpty();
}

qreal textHeight(const QFontMetricsF& metrics, const QString& text, qreal width) {
  const auto rect =
      metrics.boundingRect(QRectF(0, 0, width, 0), Qt::TextWordWrap | Qt::TextWrapAnywhere, text);
  const qreal row = qMax(20., metrics.height());
  const qreal minimum = row * qMin(qsizetype(4), text.count('\n') + 1);
  return qBound(minimum, rect.height(), row * 4);
}

struct Layout {
  qreal width, height, header;
  QList<qreal> centers;
};

QRectF noteBounds(const Event& event, const Layout& layout) {
  const qreal a = layout.centers.at(event.from), b = layout.centers.at(event.to);
  if (event.placement == NotePlacement::Left)
    return {a - sideNoteGap - (event.fromDepth ? activationWidth / 2 : 0) - sideNoteWidth, 0,
            sideNoteWidth, 0};
  if (event.placement == NotePlacement::Right)
    return {
        a + sideNoteGap +
            (event.fromDepth ? (event.fromDepth - 1) * activationStep + activationWidth / 2 : 0),
        0, sideNoteWidth, 0};
  const qreal width = qMax(150., qAbs(a - b) + 140);
  return {(a + b - width) / 2, 0, width, 0};
}

QPair<qreal, qreal> messageEndpoints(const Event& event, const Layout& layout,
                                     const Sequence& sequence) {
  const qreal a = layout.centers.at(event.from), b = layout.centers.at(event.to);
  const bool self = event.from == event.to, forward = a < b;
  const auto port = [](qreal center, int depth, bool right) {
    if (!depth) return center;
    return center + (depth - 1) * activationStep + (right ? 1 : -1) * activationWidth / 2;
  };
  const qreal from = event.destroyed == event.from ? a : port(a, event.fromDepth, self || forward);
  const qreal to =
      event.created == event.to
          ? b + (forward ? -1 : 1) * (sequence.participants.at(event.to).actor ? 16 : 82)
      : event.destroyed == event.to ? b
                                    : port(b, event.toDepth, self || !forward);
  return {from, to};
}

qreal frameTextInset(const Frame& frame, const QFontMetricsF& metrics) {
  return qMax(65., metrics.horizontalAdvance(frame.kind) + 32);
}

QString frameLabel(const Frame& frame, const Event& event) {
  const bool option = event.type == EventType::Divide && frame.kind == "critical";
  const auto condition = event.text.isEmpty() ? QString() : QStringLiteral("[") + event.text + ']';
  return option ? QStringLiteral("option") + (condition.isEmpty() ? QString() : ' ' + condition)
                : condition;
}

QStringList frameLines(const QString& text, const QFont& font, qreal width) {
  QStringList lines;
  bool truncated = false;
  for (const auto& paragraph : text.split('\n')) {
    if (lines.size() == 4) {
      truncated = true;
      break;
    }
    QTextLayout layout(paragraph.isEmpty() ? QStringLiteral(" ") : paragraph, font);
    QTextOption option;
    option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    layout.setTextOption(option);
    layout.beginLayout();
    while (true) {
      auto line = layout.createLine();
      if (!line.isValid()) break;
      line.setLineWidth(width);
      if (lines.size() == 4) {
        truncated = true;
        break;
      }
      lines.append(paragraph.mid(line.textStart(), line.textLength()).trimmed());
    }
    layout.endLayout();
    if (truncated) break;
  }
  if (truncated)
    lines.last() =
        QFontMetricsF(font).elidedText(lines.last() + QChar(0x2026), Qt::ElideRight, width);
  return lines;
}

Layout arrange(Sequence& sequence, const QFontMetricsF& metrics, const QFont& font) {
  constexpr qreal column = 210, margin = 42;
  qreal extra = 0;
  for (const auto& bar : sequence.activations)
    extra = qMax(extra, bar.depth * activationStep + activationWidth / 2);
  Layout layout{0, 0, 78, {}};
  QList<qreal> gaps(sequence.participants.size(), column);
  for (const auto& event : sequence.events) {
    if (event.type != EventType::Note || event.placement == NotePlacement::Over) continue;
    const int gap = event.from - (event.placement == NotePlacement::Left ? 1 : 0);
    // Reserve space for the note, its padding and up to eight activation levels on either side.
    if (gap >= 0 && gap + 1 < gaps.size()) gaps[gap] = 260;
  }
  qreal center = margin + column / 2;
  for (int i = 0; i < sequence.participants.size(); ++i) {
    const auto& entry = sequence.participants.at(i);
    layout.centers.append(center);
    center += gaps.at(i);
    layout.header =
        qMax(layout.header, textHeight(metrics, entry.name, 158) + (entry.actor ? 75 : 36));
  }
  layout.width = qMax(360., layout.centers.last() + column / 2 + margin + 32 + extra);
  qreal edge = 24, left = 0, right = 0;
  for (const auto& frame : sequence.frames) edge = qMax(edge, 24. + frame.depth * 10);
  for (const auto& event : sequence.events) {
    if (event.type != EventType::Note) continue;
    const auto bounds = noteBounds(event, layout);
    left = qMax(left, edge - bounds.left());
    right = qMax(right, bounds.right() + edge - layout.width);
  }
  layout.width += left + right;
  for (auto& x : layout.centers) x += left;
  qreal y = layout.header + 18;
  qreal anchor = layout.header + 6;
  for (auto& entry : sequence.participants) entry.lineTop = layout.header;
  for (auto& event : sequence.events) {
    if (event.type == EventType::Activate || event.type == EventType::Deactivate) {
      auto& bar = sequence.activations[event.activation];
      (event.type == EventType::Activate ? bar.top : bar.bottom) =
          qMax(anchor, sequence.participants.at(bar.participant).lineTop);
    } else if (event.type == EventType::Open || event.type == EventType::Divide) {
      auto& frame = sequence.frames[event.frame];
      const qreal inset = 14 + frame.depth * 10;
      if (event.type == EventType::Open) {
        frame.rect = QRectF(inset, y, layout.width - inset * 2, 0);
      }
      if (frame.highlight.isValid()) {
        y += 12;
        anchor = y;
        continue;
      }
      event.headerLines = frameLines(frameLabel(frame, event), font,
                                     frame.rect.width() - frameTextInset(frame, metrics) - 10);
      const qreal height = qMax(20., metrics.height()) * event.headerLines.size() + 14;
      event.rect = QRectF(frame.rect.left(), y, frame.rect.width(), height);
      anchor = event.rect.bottom();
      y += height + 5;
    } else if (event.type == EventType::Close) {
      y += 12;
      sequence.frames[event.frame].rect.setBottom(y);
      anchor = y;
      y += 10;
    } else {
      const bool note = event.type == EventType::Note, self = event.from == event.to;
      const auto endpoints =
          note ? QPair<qreal, qreal>(layout.centers.at(event.from), layout.centers.at(event.to))
               : messageEndpoints(event, layout, sequence);
      const auto [a, b] = endpoints;
      const auto bounds = note ? noteBounds(event, layout) : QRectF();
      const qreal width = note ? bounds.width() : self ? 150 : qAbs(a - b) - 18;
      const QString text =
          event.number ? QString::number(event.number) + ". " + event.text : event.text;
      const qreal height = textHeight(metrics, text, width - (note ? 14 : 0)) + (note ? 16 : 5);
      if (event.created >= 0) {
        const auto& entry = sequence.participants.at(event.created);
        const qreal before = entry.actor ? 22 : (textHeight(metrics, entry.name, 158) + 14) / 2;
        y += qMax(0., before + 10 - height - 6);
      }
      event.rect = QRectF(note   ? bounds.left()
                          : self ? qMax(a, b) + 18
                                 : qMin(a, b) + 9,
                          y, width, height);
      anchor = event.rect.bottom() + (note ? 0 : self ? 30 : 6);
      y += height + (note ? 17 : self ? 44 : 28);
      if (event.created >= 0) {
        auto& entry = sequence.participants[event.created];
        const qreal nameHeight = textHeight(metrics, entry.name, 158);
        entry.headTop = anchor - (entry.actor ? 22 : (nameHeight + 14) / 2);
        entry.lineTop = entry.headTop + (entry.actor ? 53 : 14) + nameHeight;
        y = qMax(y, entry.lineTop + 18);
      }
      if (event.destroyed >= 0) sequence.participants[event.destroyed].lineBottom = anchor;
    }
  }
  layout.height = y + 16;
  for (auto& entry : sequence.participants)
    if (!entry.lineBottom) entry.lineBottom = layout.height - 20;
  for (auto& bar : sequence.activations)
    if (!bar.bottom) bar.bottom = sequence.participants.at(bar.participant).lineBottom;
  return layout;
}

void frameHeader(QPainter& painter, const Frame& frame, const Event& event, const QColor& ink,
                 const QColor& stroke, const QColor& panel) {
  const auto rect = event.rect;
  painter.fillRect(rect.adjusted(1, 1, -1, 0), panel);
  painter.setPen(QPen(stroke, 1.2, event.type == EventType::Divide ? Qt::DashLine : Qt::SolidLine));
  painter.drawLine(rect.topLeft(), rect.topRight());
  const qreal inset = frameTextInset(frame, QFontMetricsF(painter.font()));
  if (event.type == EventType::Open) {
    const qreal tabWidth = inset - 14;
    const QRectF tab(rect.topLeft(), QSizeF(tabWidth, rect.height()));
    painter.setPen(QPen(stroke, 1));
    painter.drawPolyline(
        QPolygonF({tab.topRight(), tab.bottomRight() - QPointF(7, 0), tab.bottomLeft()}));
    painter.setPen(ink);
    painter.drawText(tab.adjusted(3, 0, -8, 0), Qt::AlignCenter, frame.kind);
  }
  painter.setPen(ink);
  const qreal row = (rect.height() - 14) / event.headerLines.size();
  for (int i = 0; i < event.headerLines.size(); ++i)
    painter.drawText(
        QRectF(rect.left() + inset, rect.top() + 7 + i * row, rect.width() - inset - 10, row),
        Qt::AlignLeft | Qt::AlignVCenter, event.headerLines.at(i));
}

void messageHead(QPainter& painter, QPointF tail, QPointF tip, MessageHead head) {
  if (head == MessageHead::None || head == MessageHead::Cross) return;
  const qreal length = QLineF(tail, tip).length();
  if (qFuzzyIsNull(length)) return;
  const auto direction = (tip - tail) / length;
  const QPointF normal(-direction.y(), direction.x());
  const auto left = tip - direction * 8 + normal * 4;
  const auto right = tip - direction * 8 - normal * 4;
  const auto color = painter.pen().color();
  painter.setPen(QPen(color, 1.6));
  if (head == MessageHead::Open) {
    painter.setBrush(Qt::NoBrush);
    painter.drawPolyline(QPolygonF({left, tip, right}));
  } else {
    painter.setBrush(color);
    painter.drawPolygon(QPolygonF({tip, left, right}));
  }
}

QImage drawSequence(Sequence& sequence, bool dark) {
  const auto font = diagramFont();
  const QFontMetricsF metrics(font);
  const auto layout = arrange(sequence, metrics, font);
  const qreal scale = qMin(1., qMin(940. / layout.width, 1400. / layout.height));
  auto image = canvas(qCeil(layout.width * scale), qCeil(layout.height * scale), dark);
  if (image.isNull()) return image;
  QPainter painter(&image);
  painter.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);
  painter.scale(scale, scale);
  painter.setFont(font);
  const QColor ink(dark ? "#e4ebf5" : "#34455e"), stroke(dark ? "#8ba4c5" : "#7e9cc3"),
      panel(dark ? "#344359" : "#edf3fc"), note(dark ? "#574e32" : "#fff5d0");
  for (const auto& frame : sequence.frames) {
    if (frame.highlight.isValid()) {
      painter.fillRect(frame.rect, frame.highlight);
      continue;
    }
    auto tint = panel;
    tint.setAlpha(dark ? 30 : 55);
    painter.fillRect(frame.rect, tint);
  }
  for (int i = 0; i < sequence.participants.size(); ++i) {
    const auto& entry = sequence.participants.at(i);
    const qreal x = layout.centers.at(i);
    painter.setPen(QPen(stroke, 1, Qt::DashLine));
    painter.drawLine(QPointF(x, entry.lineTop), QPointF(x, entry.lineBottom));
    const qreal height = textHeight(metrics, entry.name, 158);
    painter.setPen(QPen(stroke, 1.4));
    painter.setBrush(panel);
    const qreal top = entry.headTop;
    if (entry.actor) {
      painter.drawEllipse(QPointF(x, top + 8), 8, 8);
      painter.drawLine(QPointF(x, top + 16), QPointF(x, top + 33));
      painter.drawLine(QPointF(x - 14, top + 22), QPointF(x + 14, top + 22));
      painter.drawLine(QPointF(x, top + 33), QPointF(x - 12, top + 47));
      painter.drawLine(QPointF(x, top + 33), QPointF(x + 12, top + 47));
    } else {
      painter.drawRoundedRect(QRectF(x - 82, top, 164, height + 14), 6, 6);
    }
    painter.setPen(ink);
    painter.drawText(QRectF(x - 79, top + (entry.actor ? 53 : 7), 158, height),
                     Qt::AlignCenter | Qt::TextWordWrap | Qt::TextWrapAnywhere, entry.name);
  }
  for (const auto& frame : sequence.frames) {
    if (frame.highlight.isValid()) continue;
    painter.setPen(QPen(stroke, 1.2));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(frame.rect);
  }
  for (const auto& bar : sequence.activations) {
    if (bar.top >= sequence.participants.at(bar.participant).lineBottom) continue;
    const qreal x = layout.centers.at(bar.participant) + bar.depth * activationStep;
    painter.setPen(QPen(stroke, 1.2));
    painter.setBrush(dark ? QColor("#56759d") : QColor("#d8e7ff"));
    painter.drawRect(QRectF(x - activationWidth / 2, bar.top, activationWidth,
                            qMin(qMax(10., bar.bottom - bar.top),
                                 sequence.participants.at(bar.participant).lineBottom - bar.top)));
  }
  const auto cross = [&](QPointF center) {
    painter.setPen(QPen(ink, 1.7));
    painter.drawLine(center - QPointF(7, 7), center + QPointF(7, 7));
    painter.drawLine(center - QPointF(7, -7), center + QPointF(7, -7));
  };
  for (const auto& event : sequence.events) {
    if (event.type == EventType::Close || event.type == EventType::Activate ||
        event.type == EventType::Deactivate)
      continue;
    if (event.type == EventType::Open || event.type == EventType::Divide) {
      const auto& frame = sequence.frames.at(event.frame);
      if (!frame.highlight.isValid()) frameHeader(painter, frame, event, ink, stroke, panel);
      continue;
    }
    painter.setPen(QPen(ink, 1.5));
    if (event.type == EventType::Note) {
      painter.setPen(QPen(stroke, 1));
      painter.setBrush(note);
      painter.drawRoundedRect(event.rect, 3, 3);
      painter.setPen(ink);
      painter.drawText(event.rect.adjusted(7, 5, -7, -5),
                       Qt::AlignCenter | Qt::TextWordWrap | Qt::TextWrapAnywhere, event.text);
    } else {
      const auto [a, b] = messageEndpoints(event, layout, sequence);
      const qreal y = event.rect.bottom() + 6;
      const QPointF end(b, y + (event.from == event.to ? 24 : 0));
      if (event.from == event.to) {
        const qreal turn = qMax(a, b) + 56;
        painter.setPen(QPen(ink, 1.5, event.dashed ? Qt::DashLine : Qt::SolidLine));
        painter.drawPolyline(QPolygonF({{a, y}, {turn, y}, {turn, y + 24}}));
        arrow(painter, {turn, y + 24}, end, event.dashed, false);
        messageHead(painter, {turn, y + 24}, end, event.head);
        if (event.bidirectional) messageHead(painter, {turn, y}, {a, y}, event.head);
      } else {
        arrow(painter, {a, y}, end, event.dashed, false);
        messageHead(painter, {a, y}, end, event.head);
        if (event.bidirectional) messageHead(painter, end, {a, y}, event.head);
      }
      if (event.head == MessageHead::Cross && event.destroyed != event.to) cross(end);
      const auto text =
          event.number ? QString::number(event.number) + ". " + event.text : event.text;
      painter.setPen(ink);
      painter.drawText(event.rect, Qt::AlignCenter | Qt::TextWordWrap | Qt::TextWrapAnywhere, text);
    }
  }
  for (int i = 0; i < sequence.participants.size(); ++i)
    if (sequence.participants.at(i).destroyed)
      cross({layout.centers.at(i), sequence.participants.at(i).lineBottom});
  return image;
}
}  // namespace

RenderResult sequenceDiagram(const QStringList& lines, bool dark) {
  Sequence sequence;
  if (!parseSequence(lines, sequence)) return {{}, unsupported};
  return {drawSequence(sequence, dark), {}};
}
}  // namespace typist::render::mermaid_detail
