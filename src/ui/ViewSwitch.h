#pragma once

#include <QObject>
#include <QString>

class QTextDocument;
class QTextEdit;
class QUrl;

namespace typist::ui {

inline constexpr int kActualZoom = 100;
inline constexpr int kMinZoom = 50;
inline constexpr int kMaxZoom = 200;
inline constexpr int kZoomStep = 10;

// One Markdown buffer shown through two documents: rendered and plain source. Each
// document keeps its own undo history, so switching back and forth over content that
// parses to the same AST does not discard the edits made in either mode.
class ViewSwitch : public QObject {
  Q_OBJECT

 public:
  explicit ViewSwitch(QObject* parent = nullptr);

  QTextDocument* rendered() const;
  QTextDocument* source() const;
  QTextDocument* active() const;
  bool isSourceMode() const;
  void setBaseUrl(const QUrl& base);
  int zoomPercent() const { return m_zoomPercent; }
  void setZoomPercent(int percent);

  bool loadMarkdownIntoBoth(const QString& markdown, QString* error);
  bool switchView(QString* error);
  QString currentMarkdown() const;
  void showActiveIn(QTextEdit* editor) const;

 private:
  void adopt(QTextDocument* document);
  void injectPlainWithoutUndo(const QString& markdown);
  static bool sameSemantics(const QString& left, const QString& right);

  QTextDocument* m_rendered = nullptr;
  QTextDocument* m_source = nullptr;
  QTextDocument* m_active = nullptr;
  bool m_sourceMode = false;
  int m_zoomPercent = kActualZoom;
  qreal m_renderedPointSize = 0;
  qreal m_sourcePointSize = 0;
};

}  // namespace typist::ui
