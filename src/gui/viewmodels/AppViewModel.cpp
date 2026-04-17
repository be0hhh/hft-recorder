#include "gui/viewmodels/AppViewModel.hpp"

namespace hftrec::gui {

AppViewModel::AppViewModel(QObject* parent)
    : QObject(parent) {
}

QString AppViewModel::statusText() const {
    return statusText_;
}

}  // namespace hftrec::gui
