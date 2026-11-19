#include "MermaidStyle.h"

#include <QRegularExpression>
#include <cmath>

namespace typist::render::mermaid_detail {
void mergeStyle(GraphStyle& target, const GraphStyle& source) {
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

bool parseStyle(const QString& text, GraphStyle& style) {
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

QPen graphPen(const GraphStyle& style, const QColor& color, qreal defaultWidth,
              Qt::PenStyle defaultPattern) {
  const qreal width = style.width.value_or(defaultWidth);
  const auto stroke = style.stroke.isValid() ? style.stroke : color;
  QPen pen(stroke, width, defaultPattern);
  if (width == 0 || stroke.alpha() == 0)
    pen.setStyle(Qt::NoPen);
  else if (style.dashes) {
    if (style.dashes->isEmpty())
      pen.setStyle(Qt::SolidLine);
    else {
      auto pattern = *style.dashes;
      for (auto& dash : pattern) dash /= width;
      pen.setDashPattern(pattern);
    }
  }
  return pen;
}

}  // namespace typist::render::mermaid_detail
