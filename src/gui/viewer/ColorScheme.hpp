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
inline QColor bidColor()            { return QColor(0x15, 0x63, 0xBD); }  
inline QColor askColor()            { return QColor(0xAD, 0x18, 0x20); }  
inline QColor tradeBuyColor()       { return QColor(0x22, 0xC5, 0x5E); }  // зелёный #22C55E
inline QColor tradeSellColor()      { return QColor(0xA8, 0x55, 0xF0); }  // фиолетовый #ac4dff
inline QColor tradeConnectorColor() { return QColor(0x70, 0x80, 0x90, 0x50); }
inline QColor tradeOutlineColor()   { return QColor(0x0A, 0x0A, 0x0A, 0xC0); }
inline QColor tooltipBackColor()    { return QColor(0x10, 0x10, 0x12, 0xEB); }
inline QColor tooltipBorderColor()  { return QColor(0x50, 0x50, 0x56, 0xFF); }
inline QColor haloBuyColor()        { return QColor(0x24, 0xC8, 0xD3, 0x60); }
inline QColor haloSellColor()       { return QColor(0xFF, 0x4B, 0x57, 0x60); }

}  // namespace hftrec::gui::viewer
