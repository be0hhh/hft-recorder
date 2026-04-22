#include <QGuiApplication>
#include <QObject>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QMetaObject>
#include <QUrl>

#include "core/local_exchange/LocalExchangeServer.hpp"
#include "gui/models/SessionListModel.hpp"
#include "gui/models/ViewerSourceListModel.hpp"
#include "gui/viewer/ChartController.hpp"
#include "gui/viewer/ChartItem.hpp"
#include "gui/viewer/gpu/GpuChartItem.hpp"
#include "gui/viewmodels/AppViewModel.hpp"
#include "gui/viewmodels/CaptureViewModel.hpp"

namespace {

QString graphicsApiName(QSGRendererInterface::GraphicsApi api) {
    switch (api) {
        case QSGRendererInterface::Unknown: return QStringLiteral("unknown");
        case QSGRendererInterface::Software: return QStringLiteral("software");
        case QSGRendererInterface::OpenVG: return QStringLiteral("openvg");
        case QSGRendererInterface::OpenGL: return QStringLiteral("opengl");
        case QSGRendererInterface::Direct3D11: return QStringLiteral("d3d11");
        case QSGRendererInterface::Vulkan: return QStringLiteral("vulkan");
        case QSGRendererInterface::Metal: return QStringLiteral("metal");
        case QSGRendererInterface::Null: return QStringLiteral("null");
    }
    return QStringLiteral("unknown");
}

void wireRenderDiagnostics(QQmlApplicationEngine& engine) {
    if (engine.rootObjects().isEmpty()) return;
    auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().constFirst());
    if (window == nullptr) return;
    auto* appVm = window->findChild<hftrec::gui::AppViewModel*>(QStringLiteral("appVm"));
    if (appVm == nullptr) return;

    const QString requestedMode = qEnvironmentVariable("HFTREC_RENDER_MODE", "cpu").trimmed().toLower();
    QObject::connect(window, &QQuickWindow::sceneGraphInitialized, window, [window, appVm, requestedMode]() {
        appVm->setRenderDiagnostics(requestedMode, graphicsApiName(window->rendererInterface()->graphicsApi()));
    }, Qt::QueuedConnection);
}

}  // namespace

int main(int argc, char* argv[]) {
    QGuiApplication app(argc, argv);
    hftrec::local_exchange::LocalExchangeServer localExchangeServer;
    localExchangeServer.start();
    QCoreApplication::setOrganizationName(QStringLiteral("hftrec"));
    QCoreApplication::setApplicationName(QStringLiteral("hft-recorder"));
    const QString requestedMode = qEnvironmentVariable("HFTREC_RENDER_MODE", "cpu").trimmed().toLower();
    QQuickWindow::setGraphicsApi(requestedMode == QStringLiteral("gpu")
                                     ? QSGRendererInterface::OpenGL
                                     : QSGRendererInterface::Software);

    qmlRegisterType<hftrec::gui::SessionListModel>("HftRecorder", 1, 0, "SessionListModel");
    qmlRegisterType<hftrec::gui::ViewerSourceListModel>("HftRecorder", 1, 0, "ViewerSourceListModel");
    qmlRegisterType<hftrec::gui::AppViewModel>("HftRecorder", 1, 0, "AppViewModel");
    qmlRegisterType<hftrec::gui::CaptureViewModel>("HftRecorder", 1, 0, "CaptureViewModel");
    qmlRegisterType<hftrec::gui::viewer::ChartController>("HftRecorder", 1, 0, "ChartController");
    qmlRegisterType<hftrec::gui::viewer::ChartItem>("HftRecorder", 1, 0, "ChartItem");
    qmlRegisterType<hftrec::gui::viewer::gpu::GpuChartItem>("HftRecorder", 1, 0, "GpuChartItem");

    QQmlApplicationEngine engine;
    engine.addImportPath(QStringLiteral("qrc:/"));
    QObject::connect(&engine,
                     &QQmlApplicationEngine::objectCreationFailed,
                     &app,
                     []() { QCoreApplication::exit(-1); },
                     Qt::QueuedConnection);
    engine.load(QUrl(QStringLiteral("qrc:/HftRecorder/qml/Main.qml")));
    wireRenderDiagnostics(engine);

    const int rc = app.exec();
    localExchangeServer.stop();
    return rc;
}
