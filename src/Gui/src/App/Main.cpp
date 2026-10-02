#include <QGuiApplication>
#include <QObject>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QMetaObject>
#include <QUrl>

#include "../Api/ChartApiServer.hpp"
#include "../Models/RecordingCatalog.hpp"
#include "../Models/SessionListModel.hpp"
#include "../Models/ViewerSourceListModel.hpp"
#include "../Viewer/Compare/BookTickerCompareController.hpp"
#include "../Viewer/Compare/BookTickerCompareItem.hpp"
#include "../Viewer/Chart/Moex/MoexBasisController.hpp"
#include "../Viewer/Chart/Moex/MoexBasisItem.hpp"
#include "../Viewer/RateLimit/RateLimitStripItem.hpp"
#include "../Viewer/Strategy/StrategyIndicatorItem.hpp"
#include "../Viewer/Chart/Controller/ChartController.hpp"
#include "../Viewer/Chart/Item/ChartItem.hpp"
#include "../Viewer/Chart/Gpu/GpuChartItem.hpp"
#include "../Viewmodels/App/AppViewModel.hpp"
#include "../Backtests/Viewmodel/BacktestViewModel.hpp"
#include "../Viewmodels/Capture/CaptureViewModel.hpp"
#include "../Viewmodels/Compression/CompressionViewModel.hpp"
#include "../Viewmodels/App/WorkspaceViewModel.hpp"

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
#if HFTREC_WITH_CXET
    if (qEnvironmentVariableIsEmpty("CXET_WS_RUNTIME")) qputenv("CXET_WS_RUNTIME", "sync");
#endif
    QGuiApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("hftrec"));
    QCoreApplication::setApplicationName(QStringLiteral("hft-recorder"));
    const QString requestedMode = qEnvironmentVariable("HFTREC_RENDER_MODE", "cpu").trimmed().toLower();
    QQuickWindow::setGraphicsApi(requestedMode == QStringLiteral("gpu")
                                     ? QSGRendererInterface::OpenGL
                                     : QSGRendererInterface::Software);

    qmlRegisterType<hftrec::gui::SessionListModel>("HftRecorder", 1, 0, "SessionListModel");
    qmlRegisterType<hftrec::gui::RecordingCatalog>("HftRecorder", 1, 0, "RecordingCatalog");
    qmlRegisterType<hftrec::gui::ViewerSourceListModel>("HftRecorder", 1, 0, "ViewerSourceListModel");
    qmlRegisterType<hftrec::gui::AppViewModel>("HftRecorder", 1, 0, "AppViewModel");
    qmlRegisterType<hftrec::gui::BacktestViewModel>("HftRecorder", 1, 0, "BacktestViewModel");
    qmlRegisterType<hftrec::gui::CaptureViewModel>("HftRecorder", 1, 0, "CaptureViewModel");
    qmlRegisterType<hftrec::gui::CompressionViewModel>("HftRecorder", 1, 0, "CompressionViewModel");
    qmlRegisterType<hftrec::gui::WorkspaceViewModel>("HftRecorder", 1, 0, "WorkspaceViewModel");
    qmlRegisterType<hftrec::gui::viewer::BookTickerCompareController>("HftRecorder", 1, 0, "BookTickerCompareController");
    qmlRegisterType<hftrec::gui::viewer::BookTickerCompareItem>("HftRecorder", 1, 0, "BookTickerCompareItem");
    qmlRegisterType<hftrec::gui::viewer::MoexBasisController>("HftRecorder", 1, 0, "MoexBasisController");
    qmlRegisterType<hftrec::gui::viewer::MoexBasisItem>("HftRecorder", 1, 0, "MoexBasisItem");
    qmlRegisterType<hftrec::gui::viewer::ChartController>("HftRecorder", 1, 0, "ChartController");
    qmlRegisterType<hftrec::gui::viewer::ChartItem>("HftRecorder", 1, 0, "ChartItem");
    qmlRegisterType<hftrec::gui::viewer::RateLimitStripItem>("HftRecorder", 1, 0, "RateLimitStripItem");
    qmlRegisterType<hftrec::gui::viewer::StrategyIndicatorItem>("HftRecorder", 1, 0, "StrategyIndicatorItem");
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

    hftrec::gui::api::ChartApiServer chartApiServer;
    chartApiServer.setChartController(engine.rootObjects().isEmpty()
        ? nullptr
        : engine.rootObjects().constFirst()->findChild<hftrec::gui::viewer::ChartController*>(QStringLiteral("chartController")));
    chartApiServer.startFromEnvironment();

    const int rc = app.exec();
    return rc;
}
