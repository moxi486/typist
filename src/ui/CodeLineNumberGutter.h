#pragma once

#include <QList>
#include <QPointer>
#include <QWidget>

class QTextBlock;
class QTextDocument;

namespace typist::ui {
class Editor;

class CodeLineNumberGutter : public QWidget {
 public:
  struct Row {
    int number;
    QRect bounds;
  };

  explicit CodeLineNumberGutter(Editor* editor);
  int numberWidth() const;
  void setDark(bool dark);
  QList<Row> visibleRows();

 protected:
  void paintEvent(QPaintEvent* event) override;

 private:
  int firstVisibleNumber(const QTextBlock& block, int group);

  Editor* m_editor;
  QPointer<QTextDocument> m_document;
  int m_revision = -1;
  int m_first = -1;
  int m_last = -1;
  bool m_dark = false;
};
}  // namespace typist::ui
