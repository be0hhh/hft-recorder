#pragma once

#include <QString>
#include <QJsonObject>

#include "network/local/hftrecorder/Protocol.hpp"

namespace hftrec::gui::api::detail {

bool buildOrderRequest(const QJsonObject& root,
                       cxet::network::local::hftrecorder::OrderRequestFrame& frame,
                       QString& error) noexcept;

}  // namespace hftrec::gui::api::detail
