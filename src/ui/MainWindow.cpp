#include "MainWindow.h"

#include "editor_document.h"
#include "markdown.h"

#include <QFile>
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
  setWindowTitle(QStringLiteral("Typist"));
  statusBar()->showMessage(QStringLiteral("Ready"));
}

// M0 shell: openFile is the wiring point; no dialog or file picker yet.
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
  setWindowTitle(path);
}

}  // namespace typist
