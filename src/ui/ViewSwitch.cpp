#include "ViewSwitch.h"

#include "editor_document.h"
#include "markdown.h"

#include <QTextEdit>
#include <QTextDocument>

namespace typist::ui {

ViewSwitch::ViewSwitch(QObject* parent) : QObject(parent) {
  m_rendered = new QTextDocument(this);
  m_rendered->setUndoRedoEnabled(true);
  m_source = new QTextDocument(this);
  m_source->setUndoRedoEnabled(true);
  m_active = m_rendered;
}

QTextDocument* ViewSwitch::rendered() const { return m_rendered; }

QTextDocument* ViewSwitch::source() const { return m_source; }

QTextDocument* ViewSwitch::active() const { return m_active; }

bool ViewSwitch::isSourceMode() const { return m_sourceMode; }

QString ViewSwitch::currentMarkdown() const {
  // The plain document splits every source line into its own block, so serializing it
  // invents blank lines; its text content is the markdown.
  return m_sourceMode ? m_source->toPlainText() : typist::editor::serializeDocument(m_rendered);
}

void ViewSwitch::adopt(QTextDocument* document) { m_active = document; }

bool ViewSwitch::sameSemantics(const QString& left, const QString& right) {
  if (left == right) return true;
  // A plain-text source document serializes with different blank lines than the
  // rendered document, so the AST comparison is what decides a rebuild.
  const auto a = typist::core::parseMarkdown(left.toStdString());
  const auto b = typist::core::parseMarkdown(right.toStdString());
  return a.ok && b.ok && typist::core::astEqual(a.blocks, b.blocks);
}

void ViewSwitch::injectPlainWithoutUndo(const QString& markdown) {
  const bool undoWasEnabled = m_source->isUndoRedoEnabled();
  m_source->setUndoRedoEnabled(false);
  m_source->setPlainText(markdown);
  m_source->setUndoRedoEnabled(undoWasEnabled);
}

bool ViewSwitch::loadMarkdownIntoBoth(const QString& markdown, QString* error) {
  if (!typist::editor::loadMarkdown(markdown, m_rendered, error)) return false;
  injectPlainWithoutUndo(markdown);
  m_source->clearUndoRedoStacks(QTextDocument::UndoAndRedoStacks);
  m_sourceMode = false;
  adopt(m_rendered);
  return true;
}

bool ViewSwitch::switchView(QString* error) {
  const QString markdown = currentMarkdown();

  if (m_sourceMode) {
    // A failed parse must not discard what the author typed in source mode.
    if (!sameSemantics(typist::editor::serializeDocument(m_rendered), markdown)) {
      if (!typist::editor::loadMarkdown(markdown, m_rendered, error)) return false;
    }
    m_sourceMode = false;
    adopt(m_rendered);
    return true;
  }

  if (!sameSemantics(m_source->toPlainText(), markdown)) {
    injectPlainWithoutUndo(markdown);
    // Injected content is a new baseline; stale undo commands would replay positions
    // that no longer exist in it.
    m_source->clearUndoRedoStacks(QTextDocument::UndoAndRedoStacks);
  }
  m_sourceMode = true;
  adopt(m_source);
  return true;
}

void ViewSwitch::showActiveIn(QTextEdit* editor) const {
  editor->setDocument(m_active);
  editor->setAcceptRichText(!m_sourceMode);
}

}  // namespace typist::ui
