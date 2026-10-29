#pragma once
#include <QColor>
#include <QImage>
#include <QObject>
#include <QString>
#include <QTextFormat>
#include <QTextObjectInterface>
class QTextDocument;

namespace typist::render {
struct RenderResult {
  QImage image;
  QString error;
};
RenderResult math(const QString& source, bool display = false,
                  const QColor& color = QColor("#333333"));
RenderResult mermaid(const QString& source, bool dark = false);
QString html(const QString& markdown, bool dark = false);
constexpr int kTocObjectType = QTextFormat::UserObject + 44;
void installDocumentObjects(QTextDocument* document);
void invalidateToc(QTextDocument* document);
void expandTocForPrint(QTextDocument* document);
struct Heading {
  int level;
  QString text;
  int position;
};
QList<Heading> headings(const QTextDocument* document);

class TocObject : public QObject, public QTextObjectInterface {
  Q_OBJECT
  Q_INTERFACES(QTextObjectInterface)
 public:
  explicit TocObject(QTextDocument* document);
  QSizeF intrinsicSize(QTextDocument*, int, const QTextFormat&) override;
  void drawObject(QPainter*, const QRectF&, QTextDocument*, int, const QTextFormat&) override;
};
}  // namespace typist::render
