#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QUrl>

#include "gui/models/SessionListModel.hpp"
#include "gui/viewer/ChartController.hpp"
#include "gui/viewer/ChartItem.hpp"
#include "gui/viewmodels/AppViewModel.hpp"
#include "gui/viewmodels/CaptureViewModel.hpp"

int main(int argc, char* argv[]) {
    QGuiApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("hftrec"));
    QCoreApplication::setApplicationName(QStringLiteral("hft-recorder"));
    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);

    qmlRegisterType<hftrec::gui::SessionListModel>("HftRecorder", 1, 0, "SessionListModel");
    qmlRegisterType<hftrec::gui::AppViewModel>("HftRecorder", 1, 0, "AppViewModel");
    qmlRegisterType<hftrec::gui::CaptureViewModel>("HftRecorder", 1, 0, "CaptureViewModel");
    qmlRegisterType<hftrec::gui::viewer::ChartController>("HftRecorder", 1, 0, "ChartController");
    qmlRegisterType<hftrec::gui::viewer::ChartItem>("HftRecorder", 1, 0, "ChartItem");

    QQmlApplicationEngine engine;
    engine.addImportPath(QStringLiteral("qrc:/"));
    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        []() { QCoreApplication::exit(-1); },
        Qt::QueuedConnection);
    engine.load(QUrl(QStringLiteral("qrc:/HftRecorder/qml/Main.qml")));

    return app.exec();
}
