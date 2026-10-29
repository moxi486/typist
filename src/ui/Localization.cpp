#include "Localization.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QLibraryInfo>
#include <QLocale>
#include <QXmlStreamReader>

namespace typist::ui {

AppTranslator::AppTranslator(QObject* parent) : QTranslator(parent) {
  QFile catalog(QStringLiteral(":/translations/typist_zh.ts"));
  if (!catalog.open(QIODevice::ReadOnly)) return;
  QXmlStreamReader xml(&catalog);
  QString context, source;
  while (!xml.atEnd()) {
    xml.readNext();
    if (!xml.isStartElement()) continue;
    if (xml.name() == QStringLiteral("name"))
      context = xml.readElementText().section(QStringLiteral("::"), -1);
    else if (xml.name() == QStringLiteral("source"))
      source = xml.readElementText();
    else if (xml.name() == QStringLiteral("translation"))
      m_chinese.insert(context + QChar('\x1f') + source, xml.readElementText());
  }
}

bool AppTranslator::setLanguage(const QString& preference) {
  m_useChinese =
      preference == QStringLiteral("zh") ||
      (preference == QStringLiteral("system") && QLocale::system().language() == QLocale::Chinese);
  return m_useChinese;
}

bool AppTranslator::isEmpty() const { return !m_useChinese || m_chinese.isEmpty(); }

QString AppTranslator::translate(const char* context, const char* sourceText, const char*,
                                 int) const {
  if (!m_useChinese) return QString();
  // Older external catalogs used the unqualified Qt context.
  const QString name = QString::fromUtf8(context).section(QStringLiteral("::"), -1);
  return m_chinese.value(name + QChar('\x1f') + QString::fromUtf8(sourceText));
}

namespace {

QString languageOf(const QString& localeName) {
  return localeName.section(QChar(95), 0, 0).toLower();
}

}  // namespace

QStringList translationCandidates(const QString& localeName) {
  const QString language = languageOf(localeName);
  if (language.isEmpty() || language == QStringLiteral("en")) return QStringList();
  const QString fileName = QStringLiteral("typist_%1.qm").arg(language);
  QStringList candidates;
  candidates << QDir::currentPath() + QStringLiteral("/translations/") + fileName;
  candidates << QCoreApplication::applicationDirPath() + QStringLiteral("/translations/") +
                    fileName;
  candidates << QLibraryInfo::path(QLibraryInfo::TranslationsPath) + QStringLiteral("/") + fileName;
  candidates << QDir::homePath() + QStringLiteral("/.typist/translations/") + fileName;
  return candidates;
}

bool installTranslatorForLocale(QTranslator* translator, const QString& localeName) {
  for (const QString& candidate : translationCandidates(localeName)) {
    if (translator->load(candidate)) {
      QCoreApplication::installTranslator(translator);
      return true;
    }
  }
  return false;
}

}  // namespace typist::ui
