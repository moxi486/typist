#pragma once

#include <QTextCursor>
#include <QTimer>
#include <QVector>
#include <QWidget>

class QAction;
class QLabel;
class QLineEdit;
class QTextEdit;
class QToolButton;
class QTreeWidget;
class QTreeWidgetItem;

namespace typist::ui {

class OutlinePanel : public QWidget {
  Q_OBJECT

 public:
  explicit OutlinePanel(QTextEdit* editor, QWidget* parent = nullptr);
  void setDocument(QTextDocument* document, bool reset = false);
  bool collapsible() const { return m_collapsible; }

 protected:
  void changeEvent(QEvent* event) override;
  void showEvent(QShowEvent* event) override;
  bool eventFilter(QObject* watched, QEvent* event) override;

 private:
  struct Heading {
    QTextCursor cursor;
    QString text;
    int level;
    int parent;
    bool collapsed = false;
    QTreeWidgetItem* item = nullptr;
  };

  void refresh(bool rebuild = false);
  void renderTree();
  void followEditor(bool scrolling);
  void markActive(bool scroll);
  void revealCurrent();
  void activate(QTreeWidgetItem* item);
  void setCollapsible(bool enabled);
  void retranslate();

  QTextEdit* m_editor;
  QTextDocument* m_document = nullptr;
  QLineEdit* m_filter;
  QTreeWidget* m_tree;
  QLabel* m_empty;
  QToolButton* m_options;
  QAction* m_flat;
  QAction* m_hierarchy;
  QAction* m_reveal;
  QTimer m_refresh;
  QVector<Heading> m_headings;
  QTreeWidgetItem* m_marked = nullptr;
  int m_active = -1;
  int m_firstMatch = -1;
  bool m_collapsible;
  bool m_followScroll = false;
};

}  // namespace typist::ui
