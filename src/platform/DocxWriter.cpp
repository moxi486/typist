#include "DocxWriter.h"

#include "markdown.h"

#include <QByteArray>
#include <QFile>
#include <QFileInfo>
#include <QStringList>
#include <QVector>

#include <array>
#include <vector>
#include <vector>

namespace typist::platform {

namespace {

constexpr quint32 kLocalHeader = 0x04034b50;
constexpr quint32 kCentralHeader = 0x02014b50;
constexpr quint32 kEndOfCentral = 0x06054b50;
constexpr quint16 kDosDate = static_cast<quint16>((44 << 9) | (1 << 5) | 1);
constexpr quint16 kDosTime = 0;

void appendU16(QByteArray& out, quint16 value) {
  out.append(static_cast<char>(value & 0xff));
  out.append(static_cast<char>((value >> 8) & 0xff));
}

void appendU32(QByteArray& out, quint32 value) {
  appendU16(out, static_cast<quint16>(value & 0xffff));
  appendU16(out, static_cast<quint16>((value >> 16) & 0xffff));
}

quint32 crc32(const QByteArray& data) {
  static const std::array<quint32, 256> table = [] {
    std::array<quint32, 256> t{};
    for (quint32 i = 0; i < 256; ++i) {
      quint32 v = i;
      for (int bit = 0; bit < 8; ++bit) v = (v & 1u) ? (0xedb88320u ^ (v >> 1)) : (v >> 1);
      t[i] = v;
    }
    return t;
  }();
  quint32 crc = 0xffffffffu;
  for (const unsigned char byte : data) crc = table[(crc ^ byte) & 0xff] ^ (crc >> 8);
  return crc ^ 0xffffffffu;
}

struct ZipEntry {
  QByteArray name;
  QByteArray data;
};

QByteArray buildZip(const QVector<ZipEntry>& entries) {
  QByteArray out;
  QVector<quint32> offsets;
  offsets.reserve(entries.size());
  for (const ZipEntry& entry : entries) {
    offsets.append(static_cast<quint32>(out.size()));
    appendU32(out, kLocalHeader);
    appendU16(out, 20);
    appendU16(out, 0);
    appendU16(out, 0);
    appendU16(out, kDosTime);
    appendU16(out, kDosDate);
    appendU32(out, crc32(entry.data));
    appendU32(out, static_cast<quint32>(entry.data.size()));
    appendU32(out, static_cast<quint32>(entry.data.size()));
    appendU16(out, static_cast<quint16>(entry.name.size()));
    appendU16(out, 0);
    out.append(entry.name);
    out.append(entry.data);
  }

  const quint32 centralOffset = static_cast<quint32>(out.size());
  for (int i = 0; i < entries.size(); ++i) {
    const ZipEntry& entry = entries.at(i);
    appendU32(out, kCentralHeader);
    appendU16(out, 20);
    appendU16(out, 20);
    appendU16(out, 0);
    appendU16(out, 0);
    appendU16(out, kDosTime);
    appendU16(out, kDosDate);
    appendU32(out, crc32(entry.data));
    appendU32(out, static_cast<quint32>(entry.data.size()));
    appendU32(out, static_cast<quint32>(entry.data.size()));
    appendU16(out, static_cast<quint16>(entry.name.size()));
    appendU16(out, 0);
    appendU16(out, 0);
    appendU16(out, 0);
    appendU16(out, 0);
    appendU32(out, 0);
    appendU32(out, offsets.at(i));
    out.append(entry.name);
  }

  // Central directory size must be measured before the end-of-central-dir record is appended.
  const quint32 centralSize = static_cast<quint32>(out.size() - centralOffset);
  appendU32(out, kEndOfCentral);
  appendU16(out, 0);
  appendU16(out, 0);
  appendU16(out, static_cast<quint16>(entries.size()));
  appendU16(out, static_cast<quint16>(entries.size()));
  appendU32(out, centralSize);
  appendU32(out, centralOffset);
  appendU16(out, 0);
  return out;
}

}  // namespace


namespace {

QString escapeXml(const QString& text) {
  QString out;
  out.reserve(text.size());
  for (const QChar ch : text) {
    if (ch == QChar(38)) out += QStringLiteral("&amp;");
    else if (ch == QChar(60)) out += QStringLiteral("&lt;");
    else if (ch == QChar(62)) out += QStringLiteral("&gt;");
    else out += ch;
  }
  return out;
}

QString runXml(const QString& text, bool bold = false, bool italic = false, bool mono = false,
               bool strike = false) {
  QString props;
  if (bold || italic || mono || strike) {
    props += QStringLiteral("<w:rPr>");
    if (mono) props += QStringLiteral("<w:rFonts w:ascii=\"Consolas\" w:hAnsi=\"Consolas\"/>");
    if (bold) props += QStringLiteral("<w:b/>");
    if (italic) props += QStringLiteral("<w:i/>");
    if (strike) props += QStringLiteral("<w:strike/>");
    props += QStringLiteral("</w:rPr>");
  }
  return QStringLiteral("<w:r>%1<w:t xml:space=\"preserve\">%2</w:t></w:r>").arg(props, escapeXml(text));
}


QString runsXml(const std::vector<typist::core::InlineRun>& runs, const QString& fallback, bool forceBold = false) {
  if (runs.empty()) return runXml(fallback, forceBold);
  QString out;
  for (const typist::core::InlineRun& run : runs) {
    out += runXml(QString::fromStdString(run.text), run.bold || forceBold, run.italic, run.code, run.strike);
  }
  return out;
}

QString paragraphXml(const QString& body) {
  return QStringLiteral("<w:p>%1</w:p>").arg(body);
}

QString headingXml(int level, const QString& text) {
  const int clamped = qBound(1, level, 6);
  QString props = QStringLiteral("<w:pPr><w:pStyle w:val=\"Heading%1\"/></w:pPr>").arg(clamped);
  return paragraphXml(props + runXml(text, true));
}

QString codeXml(const QString& code) {
  const QStringList lines = code.split(QChar(10));
  QString body;
  for (int i = 0; i < lines.size(); ++i) {
    body += runXml(lines.at(i), false, false, true);
    if (i + 1 < lines.size()) body += QStringLiteral("<w:br/>");
  }
  QString props = QStringLiteral("<w:pPr><w:pBdr><w:left w:val=\"single\" w:sz=\"6\" w:space=\"4\" w:color=\"auto\"/></w:pBdr></w:pPr>");
  return paragraphXml(props + body);
}

QString quoteXml(const QString& text) {
  QString props = QStringLiteral("<w:pPr><w:ind w:left=\"480\"/>");
  props += QStringLiteral("<w:pBdr><w:left w:val=\"single\" w:sz=\"6\" w:space=\"4\" w:color=\"auto\"/></w:pBdr>");
  props += QStringLiteral("</w:pPr>");
  return paragraphXml(props + runXml(text, false, true));
}

QString breakXml() {
  QString props = QStringLiteral("<w:pPr><w:pBdr><w:bottom w:val=\"single\" w:sz=\"6\" w:space=\"1\" w:color=\"auto\"/></w:pBdr></w:pPr>");
  return paragraphXml(props);
}

QString listXml(const typist::core::Block& block, int depth, bool ordered) {
  QString out;
  const int numId = ordered ? 2 : 1;
  const int indent = 720 + depth * 720;
  for (const typist::core::Block& child : block.children) {
    if (child.type == typist::core::BlockType::ListItem) {
      // Real checkbox glyphs; whether they render depends on the viewer font.
      const QString prefix = child.task
          ? (child.checked ? QString(QChar(0x2611)) + QStringLiteral(" ") : QString(QChar(0x2610)) + QStringLiteral(" "))
          : QString();
      QString props = QStringLiteral("<w:pPr><w:pStyle w:val=\"ListParagraph\"/>");
      props += QStringLiteral("<w:numPr><w:ilvl w:val=\"%1\"/><w:numId w:val=\"%2\"/></w:numPr>").arg(depth).arg(numId);
      props += QStringLiteral("<w:ind w:left=\"%1\" w:hanging=\"360\"/></w:pPr>").arg(indent);
      out += paragraphXml(props + runXml(prefix + QString::fromStdString(child.text)));
    } else if (child.type == typist::core::BlockType::List) {
      out += listXml(child, depth + 1, child.ordered);
    } else if (!child.text.empty()) {
      const QString props = QStringLiteral("<w:pPr><w:ind w:left=\"%1\"/></w:pPr>").arg(indent);
      out += paragraphXml(props + runXml(QString::fromStdString(child.text)));
    }
  }
  return out;
}

constexpr int kTableWidthTwips = 9638;  // A4 minus two 20mm margins

QString tableXml(const typist::core::Block& table) {
  int columns = 0;
  for (const typist::core::Block& row : table.children) {
    if (row.type != typist::core::BlockType::TableRow) continue;
    int count = 0;
    for (const typist::core::Block& cell : row.children) {
      if (cell.type == typist::core::BlockType::TableCell) ++count;
    }
    columns = qMax(columns, count);
  }

  QString out = QStringLiteral("<w:tbl><w:tblPr><w:tblW w:w=\"0\" w:type=\"auto\"/>");
  // Column weights follow the longest content per column so the grid is readable.
  out += QStringLiteral("<w:tblBorders><w:top w:val=\"single\" w:sz=\"4\" w:space=\"0\" w:color=\"auto\"/>");
  out += QStringLiteral("<w:left w:val=\"single\" w:sz=\"4\" w:space=\"0\" w:color=\"auto\"/>");
  out += QStringLiteral("<w:bottom w:val=\"single\" w:sz=\"4\" w:space=\"0\" w:color=\"auto\"/>");
  out += QStringLiteral("<w:right w:val=\"single\" w:sz=\"4\" w:space=\"0\" w:color=\"auto\"/>");
  out += QStringLiteral("<w:insideH w:val=\"single\" w:sz=\"4\" w:space=\"0\" w:color=\"auto\"/>");
  out += QStringLiteral("<w:insideV w:val=\"single\" w:sz=\"4\" w:space=\"0\" w:color=\"auto\"/>");
  out += QStringLiteral("</w:tblBorders></w:tblPr>");
  QVector<int> weights(columns, 8);
  for (const typist::core::Block& row : table.children) {
    if (row.type != typist::core::BlockType::TableRow) continue;
    int column = 0;
    for (const typist::core::Block& cell : row.children) {
      if (cell.type != typist::core::BlockType::TableCell) continue;
      weights[column] = qMax(weights[column], static_cast<int>(QString::fromStdString(cell.text).length()) + 2);
      ++column;
    }
  }
  int totalWeight = 0;
  for (int weight : weights) totalWeight += weight;
  QVector<int> widths(columns, kTableWidthTwips / qMax(1, columns));
  int used = 0;
  for (int i = 0; i < columns - 1; ++i) {
    widths[i] = (kTableWidthTwips * weights[i]) / totalWeight;
    used += widths[i];
  }
  widths[columns - 1] = kTableWidthTwips - used;
  out += QStringLiteral("<w:tblGrid>");
  for (int i = 0; i < columns; ++i) out += QStringLiteral("<w:gridCol w:w=\"%1\"/>").arg(widths[i]);
  out += QStringLiteral("</w:tblGrid>");

  int rowIndex = 0;
  for (const typist::core::Block& row : table.children) {
    if (row.type != typist::core::BlockType::TableRow) continue;
    const bool header = rowIndex == 0;
    int columnIndex = 0;
    out += QStringLiteral("<w:tr>");
    for (const typist::core::Block& cell : row.children) {
      if (cell.type != typist::core::BlockType::TableCell) continue;
      const int width = widths[qMin(columnIndex, columns - 1)];
      out += QStringLiteral("<w:tc><w:tcPr><w:tcW w:w=\"%1\" w:type=\"dxa\"/></w:tcPr>").arg(width);
      const QStringList lines = QString::fromStdString(cell.text).split(QChar(10));
      for (const QString& line : lines) out += paragraphXml(runsXml(cell.runs, line, header));
      ++columnIndex;
      out += QStringLiteral("</w:tc>");
    }
    out += QStringLiteral("</w:tr>");
    ++rowIndex;
  }
  out += QStringLiteral("</w:tbl>");
  return out;
}

QString blocksXml(const std::vector<typist::core::Block>& blocks) {
  QString out;
  for (const typist::core::Block& block : blocks) {
    const QString text = QString::fromStdString(block.text);
    switch (block.type) {
      case typist::core::BlockType::Heading:
        out += headingXml(block.level, text);
        break;
      case typist::core::BlockType::Paragraph:
        out += paragraphXml(runsXml(block.runs, text));
        break;
      case typist::core::BlockType::CodeBlock:
        out += codeXml(text);
        break;
      case typist::core::BlockType::ThematicBreak:
        out += breakXml();
        break;
      case typist::core::BlockType::BlockQuote:
        if (!text.isEmpty()) out += quoteXml(text);
        if (!block.children.empty()) out += blocksXml(block.children);
        break;
      case typist::core::BlockType::List:
        out += listXml(block, 0, block.ordered);
        break;
      case typist::core::BlockType::Table:
        out += tableXml(block);
        break;
      default:
        if (!text.isEmpty()) out += paragraphXml(runsXml(block.runs, text));
        break;
    }
  }
  return out;
}

QString stylesXml() {
  QString out = QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>");
  out += QStringLiteral("<w:styles xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\">");
  out += QStringLiteral("<w:docDefaults><w:rPrDefault><w:rPr>");
  out += QStringLiteral("<w:rFonts w:ascii=\"Calibri\" w:hAnsi=\"Calibri\"/><w:sz w:val=\"22\"/></w:rPr></w:rPrDefault></w:docDefaults>");
  // Style names are child elements (w:name w:val=...), not attributes.
  out += QStringLiteral("<w:style w:type=\"paragraph\" w:styleId=\"Normal\"><w:name w:val=\"Normal\"/><w:qFormat/></w:style>");
  for (int level = 1; level <= 6; ++level) {
    out += QStringLiteral("<w:style w:type=\"paragraph\" w:styleId=\"Heading%1\">").arg(level);
    out += QStringLiteral("<w:name w:val=\"heading %1\"/><w:next w:val=\"Normal\"/><w:qFormat/>").arg(level);
    out += QStringLiteral("<w:pPr><w:keepNext/><w:outlineLvl w:val=\"%1\"/></w:pPr>").arg(level - 1);
    out += QStringLiteral("<w:rPr><w:b/><w:sz w:val=\"%1\"/></w:rPr>").arg(40 - level * 4);
    out += QStringLiteral("</w:style>");
  }
  out += QStringLiteral("<w:style w:type=\"paragraph\" w:styleId=\"ListParagraph\"><w:name w:val=\"List Paragraph\"/><w:qFormat/>");
  out += QStringLiteral("<w:pPr><w:ind w:left=\"720\" w:hanging=\"360\"/></w:pPr></w:style>");
  out += QStringLiteral("</w:styles>");
  return out;
}

QString numberingXml() {
  QString out = QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>");
  out += QStringLiteral("<w:numbering xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\">");
  const QStringList bullets = {QStringLiteral("\u2022"), QStringLiteral("\u25e6"), QStringLiteral("\u25aa")};
  for (int abstract = 0; abstract < 2; ++abstract) {
    out += QStringLiteral("<w:abstractNum w:abstractNumId=\"%1\">").arg(abstract);
    const bool bullet = abstract == 0;
    for (int level = 0; level < 3; ++level) {
      out += QStringLiteral("<w:lvl w:ilvl=\"%1\">").arg(level);
      out += bullet ? QStringLiteral("<w:numFmt w:val=\"bullet\"/>")
                    : QStringLiteral("<w:numFmt w:val=\"decimal\"/>");
      out += bullet ? QStringLiteral("<w:lvlText w:val=\"%1\"/>").arg(bullets.at(level))
                    : QStringLiteral("<w:lvlText w:val=\"%1\"/>").arg(QStringLiteral("%1.").arg(level + 1));
      out += QStringLiteral("<w:lvlJc w:val=\"left\"/>");
      out += QStringLiteral("<w:pPr><w:ind w:left=\"%1\" w:hanging=\"360\"/></w:pPr>").arg(720 + level * 720);
      if (bullet) out += QStringLiteral("<w:rPr><w:rFonts w:ascii=\"Symbol\" w:hAnsi=\"Symbol\" w:hint=\"default\"/></w:rPr>");
      out += QStringLiteral("</w:lvl>");
    }
    out += QStringLiteral("</w:abstractNum>");
  }
  out += QStringLiteral("<w:num w:numId=\"1\"><w:abstractNumId w:val=\"0\"/></w:num>");
  out += QStringLiteral("<w:num w:numId=\"2\"><w:abstractNumId w:val=\"1\"/></w:num>");
  out += QStringLiteral("</w:numbering>");
  return out;
}

QString contentTypesXml() {
  QString out = QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>");
  out += QStringLiteral("<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">");
  out += QStringLiteral("<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>");
  out += QStringLiteral("<Default Extension=\"xml\" ContentType=\"application/xml\"/>");
  out += QStringLiteral("<Override PartName=\"/word/document.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml\"/>");
  out += QStringLiteral("<Override PartName=\"/word/styles.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.styles+xml\"/>");
  out += QStringLiteral("<Override PartName=\"/word/numbering.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.numbering+xml\"/>");
  out += QStringLiteral("</Types>");
  return out;
}

QString rootRelsXml() {
  QString out = QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>");
  out += QStringLiteral("<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">");
  out += QStringLiteral("<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" Target=\"word/document.xml\"/>");
  out += QStringLiteral("</Relationships>");
  return out;
}

QString documentRelsXml() {
  QString out = QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>");
  out += QStringLiteral("<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">");
  out += QStringLiteral("<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles\" Target=\"styles.xml\"/>");
  out += QStringLiteral("<Relationship Id=\"rId2\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/numbering\" Target=\"numbering.xml\"/>");
  out += QStringLiteral("</Relationships>");
  return out;
}

}  // namespace


bool exportDocx(const QString& path, const QString& markdown, QString* error) {
  const typist::core::ParseResult parsed = typist::core::parseMarkdown(markdown.toStdString());
  if (!parsed.ok) {
    if (error) *error = QStringLiteral("Cannot parse markdown for DOCX export");
    return false;
  }

  QString document = QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>");
  document += QStringLiteral("<w:document xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\"><w:body>");
  document += blocksXml(parsed.blocks);
  document += QStringLiteral("<w:sectPr><w:pgSz w:w=\"11906\" w:h=\"16838\"/>");
  document += QStringLiteral("<w:pgMar w:top=\"1134\" w:right=\"1134\" w:bottom=\"1134\" w:left=\"1134\" w:header=\"720\" w:footer=\"720\" w:gutter=\"0\"/></w:sectPr>");
  document += QStringLiteral("</w:body></w:document>");

  QVector<ZipEntry> entries;
  entries.append(ZipEntry{QByteArrayLiteral("[Content_Types].xml"), contentTypesXml().toUtf8()});
  entries.append(ZipEntry{QByteArrayLiteral("_rels/.rels"), rootRelsXml().toUtf8()});
  entries.append(ZipEntry{QByteArrayLiteral("word/_rels/document.xml.rels"), documentRelsXml().toUtf8()});
  entries.append(ZipEntry{QByteArrayLiteral("word/styles.xml"), stylesXml().toUtf8()});
  entries.append(ZipEntry{QByteArrayLiteral("word/numbering.xml"), numberingXml().toUtf8()});
  entries.append(ZipEntry{QByteArrayLiteral("word/document.xml"), document.toUtf8()});

  const QByteArray zip = buildZip(entries);
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly)) {
    if (error) *error = QStringLiteral("Cannot write DOCX file");
    return false;
  }
  if (file.write(zip) != zip.size()) {
    file.close();
    file.remove();
    if (error) *error = QStringLiteral("DOCX write truncated");
    return false;
  }
  file.close();
  return true;
}

bool docxExportAvailable() { return true; }

qint64 docxSizeForBench(const QString& path, const QString& markdown) {
  QString error;
  if (!exportDocx(path, markdown, &error)) return -1;
  return QFileInfo(path).size();
}

}  // namespace typist::platform

