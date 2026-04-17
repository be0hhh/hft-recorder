#pragma once

#include <QObject>
#include <QString>

namespace hftrec::gui {

class AppViewModel : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusTextChanged)

  public:
    explicit AppViewModel(QObject* parent = nullptr);

    QString statusText() const;

  signals:
    void statusTextChanged();

  private:
    QString statusText_{"Ready for GUI-first capture and compression lab work"};
};

}  // namespace hftrec::gui
