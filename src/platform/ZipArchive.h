#pragma once
#include <QByteArray>
#include <QVector>

namespace typist::platform {
struct ZipEntry {
  QByteArray name, data;
};
// Creates a stored ZIP in entry order, with no extra fields or data descriptors.
QByteArray buildZip(const QVector<ZipEntry>& entries);
}  // namespace typist::platform
