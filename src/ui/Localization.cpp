#include "Localization.h"

#include <QCoreApplication>
#include <QDir>
#include <QLibraryInfo>

namespace typist::ui {

namespace {

QString languageOf(const QString& localeName) { return localeName.section(QChar(95), 0, 0).toLower(); }

}  // namespace

QStringList translationCandidates(const QString& localeName) {
  const QString language = languageOf(localeName);
  if (language.isEmpty() || language == QStringLiteral("en")) return QStringList();
  const QString fileName = QStringLiteral("typist_%1.qm").arg(language);
  QStringList candidates;
  candidates << QDir::currentPath() + QStringLiteral("/translations/") + fileName;
  candidates << QCoreApplication::applicationDirPath() + QStringLiteral("/translations/") + fileName;
  candidates << QLibraryInfo::path(QLibraryInfo::TranslationsPath) + QStringLiteral("/") + fileName;
  candidates << QDir::homePath() + QStringLiteral("/.typist/translations/") + fileName;
  return candidates;
}

bool installTranslatorForLocale(QTranslator* translator, const QString& localeName) {
  for (const QString& candidate : translationCandidates(localeName)) {
    if (translator->load(localeName, candidate)) {
      QCoreApplication::installTranslator(translator);
      return true;
    }
  }
  return false;
}

}  // namespace typist::ui
