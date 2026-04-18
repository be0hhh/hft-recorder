#pragma once

#include <QColor>

// Dark-theme palette for the event viewer. Inline to let QML-side helpers
// pick the same colours without duplicating hex literals.

namespace hftrec::gui::viewer {

inline QColor bgColor()             { return QColor(0x0E, 0x0E, 0x12); }
inline QColor gridColor()           { return QColor(0x1F, 0x1F, 0x28); }
inline QColor axisTextColor()       { return QColor(0xD4, 0xD4, 0xD4); }
inline QColor bidColor()            { return QColor(0x12, 0xC7, 0x7A); }
inline QColor askColor()            { return QColor(0xEF, 0x44, 0x44); }
inline QColor tradeBuyColor()       { return QColor(0x00, 0xFF, 0x66); }
inline QColor tradeSellColor()      { return QColor(0xFF, 0x40, 0x20); }
inline QColor tradeConnectorColor() { return QColor(0xAA, 0xAA, 0xAA, 0x70); }

}  // namespace hftrec::gui::viewer
