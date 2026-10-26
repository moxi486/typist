#include "MainWindow.h"

#include "FileWriter.h"
#include "editor_document.h"
#include "markdown.h"

#include <QAction>
#include <QFile>
#include <QFileDialog>
#include <QKeySequence>
#include <QMenuBar>
#include <QMenu>
#include <QStatusBar>
#include <QString>
#include <QTextDocument>
#include <QTextEdit>

namespace typist {

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
  m_document = new QTextDocument(this);
  m_document->setUndoRedoEnabled(true);
  m_editor = new QTextEdit(this);
  m_editor->setDocument(m_document);
  m_editor->setAcceptRichText(true);
  setCentralWidget(m_editor);

  QMenu* fileMenu = menuBar()->addMenu(QStringLiteral("File"));

  QAction* openAction = new QAction(QStringLiteral("Open"), this);
  openAction->setShortcut(QKeySequence::Open);
  fileMenu->addAction(openAction);
  connect(openAction, &QAction::triggered, this, &MainWindow::openDialog);

  QAction* saveAction = new QAction(QStringLiteral("Save"), this);
  saveAction->setShortcut(QKeySequence::Save);
  fileMenu->addAction(saveAction);
  connect(saveAction, &QAction::triggered, this, &MainWindow::save);

  QAction* saveAsAction = new QAction(QStringLiteral("Save As"), this);
  saveAsAction->setShortcut(QKeySequence::SaveAs);
  fileMenu->addAction(saveAsAction);
  connect(saveAsAction, &QAction::triggered, this, &MainWindow::saveAs);

  QMenu* editMenu = menuBar()->addMenu(QStringLiteral("Edit"));
  m_undoAction = new QAction(QStringLiteral("Undo"), this);
  m_undoAction->setShortcut(QKeySequence::Undo);
  m_undoAction->setEnabled(false);
  editMenu->addAction(m_undoAction);
  connect(m_undoAction, &QAction::triggered, this, &MainWindow::undo);

  m_redoAction = new QAction(QStringLiteral("Redo"), this);
  m_redoAction->setShortcut(QKeySequence::Redo);
  m_redoAction->setEnabled(false);
  editMenu->addAction(m_redoAction);
  connect(m_redoAction, &QAction::triggered, this, &MainWindow::redo);

  connect(m_document, &QTextDocument::contentsChanged, this, [this] {
    m_dirty = true;
    updateTitleAndStatus();
  });
  connect(m_document, &QTextDocument::undoAvailable, m_undoAction, &QAction::setEnabled);
  connect(m_document, &QTextDocument::redoAvailable, m_redoAction, &QAction::setEnabled);

  updateTitleAndStatus();
}

void MainWindow::openDialog() {
  const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("Open Markdown"), QString(),
                                                    QStringLiteral("Markdown (*.md)"));
  if (!path.isEmpty()) openFile(path);
}

void MainWindow::openFile(const QString& path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    statusBar()->showMessage(QStringLiteral("Cannot open file"));
    return;
  }

  const QString source = QString::fromUtf8(file.readAll());
  const auto parsed = typist::core::parseMarkdown(source.toStdString());
  if (!parsed.ok) {
    statusBar()->showMessage(QString::fromStdString(parsed.error));
    return;
  }

  typist::editor::buildDocument(parsed.blocks, m_document);
  // Undo must not reach across a load: the loaded text is the baseline.
  m_document->clearUndoRedoStacks(QTextDocument::UndoAndRedoStacks);
  m_currentPath = path;
  m_savedMarkdown = source;
  markClean();
}

void MainWindow::save() {
  if (m_currentPath.isEmpty()) {
    saveAs();
    return;
  }

  QString error;
  const QString markdown = typist::editor::serializeDocument(m_document);
  if (!typist::platform::writeAtomic(m_currentPath, markdown, &error)) {
    statusBar()->showMessage(error);
    return;
  }

  m_savedMarkdown = markdown;
  markClean();
}

void MainWindow::saveAs() {
  const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Save Markdown"), m_currentPath,
                                                    QStringLiteral("Markdown (*.md)"));
  if (path.isEmpty()) return;
  m_currentPath = path;
  save();
}

void MainWindow::undo() {
  m_document->undo();
  // Undo can land back on the saved bytes, so dirty is re-derived from content.
  m_dirty = typist::editor::serializeDocument(m_document) != m_savedMarkdown;
  updateTitleAndStatus();
}

void MainWindow::redo() {
  m_document->redo();
  m_dirty = typist::editor::serializeDocument(m_document) != m_savedMarkdown;
  updateTitleAndStatus();
}

void MainWindow::markClean() {
  m_dirty = false;
  updateTitleAndStatus();
}

void MainWindow::updateTitleAndStatus() {
  const QString base = m_currentPath.isEmpty() ? QStringLiteral("Typist") : m_currentPath;
  setWindowTitle(m_dirty ? base + QStringLiteral(" *") : base);
  statusBar()->showMessage(m_dirty ? QStringLiteral("Unsaved changes") : QStringLiteral("Saved"));
}

}  // namespace typist
