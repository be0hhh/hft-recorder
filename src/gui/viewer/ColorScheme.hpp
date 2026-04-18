#pragma once

#include <QColor>

// Dark-theme palette for the event viewer. Inline to let QML-side helpers
// pick the same colours without duplicating hex literals.

namespace hftrec::gui::viewer {

inline QColor windowColor()         { return QColor(0x16, 0x16, 0x16); }
inline QColor panelColor()          { return QColor(0x2C, 0x2C, 0x2F); }
inline QColor panelAltColor()       { return QColor(0x3C, 0x3C, 0x3C); }
inline QColor borderColor()         { return QColor(0x3C, 0x3D, 0x3F); }
inline QColor bgColor()             { return windowColor(); }
inline QColor gridColor()           { return QColor(0x30, 0x30, 0x34); }
inline QColor axisTextColor()       { return QColor(0xF5, 0xF5, 0xF5); }
inline QColor mutedTextColor()      { return QColor(0xB6, 0xB6, 0xB6); }
// Orderbook (стакан) uses the green / pink-red palette — brighter, easier to read.
inline QColor bidColor()            { return QColor(0x22, 0xC5, 0x5E); }  // vivid emerald
inline QColor askColor()            { return QColor(0xFB, 0x71, 0x85); }  // light pink-red
// Trades (покупки/продажи) keep the original cyan / dark-red palette.
inline QColor tradeBuyColor()       { return QColor(0x24, 0xC2, 0xCB); }
inline QColor tradeSellColor()      { return QColor(0xDA, 0x25, 0x36); }
inline QColor tradeConnectorColor() { return QColor(0x70, 0x80, 0x90, 0x50); }
inline QColor tradeOutlineColor()   { return QColor(0x0A, 0x0A, 0x0A, 0xC0); }
inline QColor tooltipBackColor()    { return QColor(0x10, 0x10, 0x12, 0xEB); }
inline QColor tooltipBorderColor()  { return QColor(0x50, 0x50, 0x56, 0xFF); }
inline QColor haloBuyColor()        { return QColor(0x24, 0xC2, 0xCB, 0x60); }
inline QColor haloSellColor()       { return QColor(0xDA, 0x25, 0x36, 0x60); }

}  // namespace hftrec::gui::viewer
