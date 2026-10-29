#pragma once

#include <QHash>
#include <QString>
#include <QStringList>
#include <QTranslator>

namespace typist::ui {

// The built-in catalog also works in portable builds without Qt Linguist installed.
class AppTranslator : public QTranslator {
 public:
  explicit AppTranslator(QObject* parent = nullptr);
  bool setLanguage(const QString& preference);
  bool isEmpty() const override;
  QString translate(const char* context, const char* sourceText,
                    const char* disambiguation = nullptr, int n = -1) const override;

 private:
  QHash<QString, QString> m_chinese;
  bool m_useChinese = false;
};

// Ordered .qm lookup paths for a locale; empty for English, which is the source language.
QStringList translationCandidates(const QString& localeName);

// Loads the first existing catalog for the locale and installs it on the application.
bool installTranslatorForLocale(QTranslator* translator, const QString& localeName);

}  // namespace typist::ui
