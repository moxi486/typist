#include "MainWindow.h"

#include "FileWriter.h"
#include "editor_document.h"
#include "markdown.h"

#include <QAction>
#include <QFile>
#include <QFileDialog>
#include <QKeySequence>
#include <QMenu>
#include <QStatusBar>
#include <QString>
#include <QTextDocument>
#include <QTextEdit>

namespace typist {

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
  m_document = new QTextDocument(this);
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

  connect(m_document, &QTextDocument::contentsChanged, this, [this] {
    m_dirty = true;
    updateTitleAndStatus();
  });

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
  m_currentPath = path;
  m_dirty = false;
  updateTitleAndStatus();
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

  m_dirty = false;
  updateTitleAndStatus();
}

void MainWindow::saveAs() {
  const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Save Markdown"), m_currentPath,
                                                    QStringLiteral("Markdown (*.md)"));
  if (path.isEmpty()) return;
  m_currentPath = path;
  save();
}

void MainWindow::updateTitleAndStatus() {
  const QString base = m_currentPath.isEmpty() ? QStringLiteral("Typist") : m_currentPath;
  setWindowTitle(m_dirty ? base + QStringLiteral(" *") : base);
  statusBar()->showMessage(m_dirty ? QStringLiteral("Unsaved changes") : QStringLiteral("Saved"));
}

}  // namespace typist
