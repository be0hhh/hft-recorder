#pragma once

#include <algorithm>
#include <cstdint>

#include <QDateTime>
#include <QLatin1Char>
#include <QPainterPath>
#include <QPointF>
#include <QString>
#include <QStringLiteral>

namespace hftrec::gui::viewer::detail {

inline QString formatScaledE8(std::int64_t value) {
    const bool negative = value < 0;
    const std::uint64_t absValue = negative
        ? static_cast<std::uint64_t>(-(value + 1)) + 1u
        : static_cast<std::uint64_t>(value);
    const std::uint64_t integerPart = absValue / 100000000ull;
    const std::uint64_t fractionPart = absValue % 100000000ull;
    return QStringLiteral("%1%2.%3")
        .arg(negative ? QStringLiteral("-") : QString{})
        .arg(integerPart)
        .arg(fractionPart, 8, 10, QLatin1Char('0'));
}

inline QString formatTrimmedE8(std::int64_t value) {
    QString text = formatScaledE8(value);
    while (text.endsWith(QLatin1Char('0'))) text.chop(1);
    if (text.endsWith(QLatin1Char('.'))) text.chop(1);
    return text;
}

inline std::int64_t multiplyScaledE8(std::int64_t lhsE8, std::int64_t rhsE8) {
    constexpr std::int64_t kScale = 100000000ll;
    const bool negative = (lhsE8 < 0) != (rhsE8 < 0);

    std::uint64_t lhsAbs = lhsE8 < 0
        ? static_cast<std::uint64_t>(-(lhsE8 + 1)) + 1u
        : static_cast<std::uint64_t>(lhsE8);
    std::uint64_t rhsAbs = rhsE8 < 0
        ? static_cast<std::uint64_t>(-(rhsE8 + 1)) + 1u
        : static_cast<std::uint64_t>(rhsE8);

    const std::uint64_t lhsInt = lhsAbs / static_cast<std::uint64_t>(kScale);
    const std::uint64_t lhsFrac = lhsAbs % static_cast<std::uint64_t>(kScale);
    const std::uint64_t rhsInt = rhsAbs / static_cast<std::uint64_t>(kScale);
    const std::uint64_t rhsFrac = rhsAbs % static_cast<std::uint64_t>(kScale);

    const std::uint64_t resultAbs =
        lhsInt * rhsInt * static_cast<std::uint64_t>(kScale)
        + lhsInt * rhsFrac
        + rhsInt * lhsFrac
        + (lhsFrac * rhsFrac) / static_cast<std::uint64_t>(kScale);

    if (!negative) return static_cast<std::int64_t>(resultAbs);
    return -static_cast<std::int64_t>(resultAbs);
}

inline QString formatTimeNs(std::int64_t tsNs) {
    const qint64 ms = static_cast<qint64>(tsNs / 1000000ll);
    const auto dt = QDateTime::fromMSecsSinceEpoch(ms, Qt::UTC);
    if (!dt.isValid()) return QString::number(tsNs);
    return dt.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz 'UTC'"));
}

inline qreal clampReal(qreal value, qreal lo, qreal hi) {
    return std::clamp(value, lo, hi);
}

// Append one horizontal step to a bookTicker path. Breaks cleanly at xLeft if
// a previous segment ended elsewhere (bridges with a jump line), then draws
// the flat segment at y.
inline void appendStepSegment(QPainterPath& path, bool& started, qreal xLeft, qreal xRight, qreal y) {
    if (xRight <= xLeft) return;
    if (!started) {
        path.moveTo(xLeft, y);
        path.lineTo(xRight, y);
        started = true;
        return;
    }

    const QPointF current = path.currentPosition();
    if (!qFuzzyCompare(current.x() + 1.0, xLeft + 1.0)) {
        path.lineTo(xLeft, current.y());
    }
    if (!qFuzzyCompare(current.y() + 1.0, y + 1.0)) {
        path.lineTo(xLeft, y);
    }
    path.lineTo(xRight, y);
}

}  // namespace hftrec::gui::viewer::detail
