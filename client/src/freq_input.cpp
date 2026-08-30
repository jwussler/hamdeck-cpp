#include "freq_input.h"

#include <QStringList>

namespace FreqInput {

long long Parse(const QString& text) {
  QString s = text.trimmed();
  s.remove(',');
  s.remove(' ');
  if (s.isEmpty()) return 0;

  const QStringList parts = s.split('.');

  // The grouped display form: 7.185.000. Digits, not a decimal.
  if (parts.size() > 2) {
    QString digits = s;
    digits.remove('.');
    bool ok = false;
    const long long v = digits.toLongLong(&ok);
    return ok ? v : 0;
  }

  if (parts.size() == 2) {
    bool int_ok = false;
    const long long int_part = parts[0].toLongLong(&int_ok);
    bool f_ok = false;
    const double f = s.toDouble(&f_ok);
    if (!int_ok || !f_ok) return 0;
    // Below 100 the operator means MHz ("14.2"); above it, kHz ("14200.5").
    return int_part < 100 ? static_cast<long long>(f * 1000000)
                          : static_cast<long long>(f * 1000);
  }

  bool ok = false;
  const long long v = s.toLongLong(&ok);
  if (!ok) return 0;
  if (v < 100) return v * 1000000;        // 14      -> 14 MHz
  if (v < 100000) return v * 1000;        // 14200   -> 14.2 MHz
  return v;                               // 14200000 -> Hz
}

bool InRange(long long hz) { return hz >= kMinHz && hz <= kMaxHz; }

QString ToEditText(long long hz) {
  if (hz <= 0) return {};
  // Up to six decimals, trailing zeros trimmed - "7.185", not "7.185000".
  QString s = QString::number(hz / 1000000.0, 'f', 6);
  while (s.endsWith('0') && !s.endsWith(".000")) s.chop(1);
  return s;
}

}  // namespace FreqInput
