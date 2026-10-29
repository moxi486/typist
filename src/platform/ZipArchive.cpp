#include "ZipArchive.h"

#include <array>

namespace typist::platform {
namespace {
constexpr quint32 kLocalHeader = 0x04034b50;
constexpr quint32 kCentralHeader = 0x02014b50;
constexpr quint32 kEndOfCentral = 0x06054b50;
constexpr quint16 kDosDate = static_cast<quint16>((44 << 9) | (1 << 5) | 1);
constexpr quint16 kDosTime = 0;

void appendU16(QByteArray& out, quint16 value) {
  out.append(static_cast<char>(value & 0xff));
  out.append(static_cast<char>((value >> 8) & 0xff));
}

void appendU32(QByteArray& out, quint32 value) {
  appendU16(out, static_cast<quint16>(value & 0xffff));
  appendU16(out, static_cast<quint16>((value >> 16) & 0xffff));
}

quint32 crc32(const QByteArray& data) {
  static const std::array<quint32, 256> table = [] {
    std::array<quint32, 256> t{};
    for (quint32 i = 0; i < 256; ++i) {
      quint32 v = i;
      for (int bit = 0; bit < 8; ++bit) v = (v & 1u) ? (0xedb88320u ^ (v >> 1)) : (v >> 1);
      t[i] = v;
    }
    return t;
  }();
  quint32 crc = 0xffffffffu;
  for (const unsigned char byte : data) crc = table[(crc ^ byte) & 0xff] ^ (crc >> 8);
  return crc ^ 0xffffffffu;
}

}  // namespace

QByteArray buildZip(const QVector<ZipEntry>& entries) {
  QByteArray out;
  QVector<quint32> offsets;
  offsets.reserve(entries.size());
  for (const ZipEntry& entry : entries) {
    offsets.append(static_cast<quint32>(out.size()));
    appendU32(out, kLocalHeader);
    appendU16(out, 20);
    appendU16(out, 0);
    appendU16(out, 0);
    appendU16(out, kDosTime);
    appendU16(out, kDosDate);
    appendU32(out, crc32(entry.data));
    appendU32(out, static_cast<quint32>(entry.data.size()));
    appendU32(out, static_cast<quint32>(entry.data.size()));
    appendU16(out, static_cast<quint16>(entry.name.size()));
    appendU16(out, 0);
    out.append(entry.name);
    out.append(entry.data);
  }

  const quint32 centralOffset = static_cast<quint32>(out.size());
  for (int i = 0; i < entries.size(); ++i) {
    const ZipEntry& entry = entries.at(i);
    appendU32(out, kCentralHeader);
    appendU16(out, 20);
    appendU16(out, 20);
    appendU16(out, 0);
    appendU16(out, 0);
    appendU16(out, kDosTime);
    appendU16(out, kDosDate);
    appendU32(out, crc32(entry.data));
    appendU32(out, static_cast<quint32>(entry.data.size()));
    appendU32(out, static_cast<quint32>(entry.data.size()));
    appendU16(out, static_cast<quint16>(entry.name.size()));
    appendU16(out, 0);
    appendU16(out, 0);
    appendU16(out, 0);
    appendU16(out, 0);
    appendU32(out, 0);
    appendU32(out, offsets.at(i));
    out.append(entry.name);
  }

  // Central directory size must be measured before the end-of-central-dir record is appended.
  const quint32 centralSize = static_cast<quint32>(out.size() - centralOffset);
  appendU32(out, kEndOfCentral);
  appendU16(out, 0);
  appendU16(out, 0);
  appendU16(out, static_cast<quint16>(entries.size()));
  appendU16(out, static_cast<quint16>(entries.size()));
  appendU32(out, centralSize);
  appendU32(out, centralOffset);
  appendU16(out, 0);
  return out;
}

}  // namespace typist::platform
