#pragma once

#include <QColor>
#include <QString>
#include <QStringList>

namespace hftrec::gui::viewer {

inline QString normalizedExchange(QString value) {
    value = value.trimmed().toLower();
    value.replace('_', QString{});
    value.replace('-', QString{});
    value.replace(' ', QString{});
    return value;
}

inline QColor venueColor(const QString& exchange) {
    const QString key = normalizedExchange(exchange);
    if (key == QStringLiteral("kucoin")) return QColor{22, 132, 73};
    if (key == QStringLiteral("gate") || key == QStringLiteral("gateio")) return QColor{48, 96, 175};
    if (key == QStringLiteral("bitget")) return QColor{24, 154, 170};
    if (key == QStringLiteral("binance")) return QColor{196, 154, 36};
    return QColor{128, 128, 136};
}

inline QString displayExchangeName(QString value) {
    value = value.trimmed();
    if (value.isEmpty()) return QStringLiteral("unknown");
    value.replace('_', ' ');
    value.replace('-', ' ');
    QStringList parts = value.split(' ', Qt::SkipEmptyParts);
    for (QString& part : parts) {
        if (part.size() <= 3) part = part.toUpper();
        else part = part.left(1).toUpper() + part.mid(1).toLower();
    }
    return parts.join(' ');
}

}  // namespace hftrec::gui::viewer
