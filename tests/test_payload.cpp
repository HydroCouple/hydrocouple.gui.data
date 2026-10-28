/**
 * @file test_payload.cpp
 * @brief Offline payload download, paging, atomic publication and exact-area DEM regression tests.
 * @details A synthetic policy-controlled network supplies all responses. Small raster and output
 * fixtures remain in the configured repository evidence directory; no real service is contacted.
 * @author Caleb Buahin <caleb.buahin@gmail.com>
 * @date 2026
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license GPL-3.0-or-later
 */
#include "PayloadDownload.hpp"
#include "ProgressRecorder.hpp"
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>
#include <QSignalSpy>
#include <QTest>
#include <QTimer>
#include <QUrlQuery>
#include <QUuid>
#include <gdal_priv.h>
#include <ogr_spatialref.h>
#include <cmath>
#include <cstring>

using namespace OpenSWMM::Data;
using namespace OpenSWMM::Sdk;
namespace
{
/** @brief Copied synthetic HTTP response; held replies support deterministic cancellation. */
struct Response {
    QByteArray bytes;
    int status = 200;
    QUrl redirect;
    bool held = false;
    QByteArray retryAfter;
};
/** @brief Offline readable reply with explicit completion and abort outcomes. */
class Reply final : public QNetworkReply
{
public:
    Reply(const QNetworkRequest &request, Response response, QObject *parent)
        : QNetworkReply(parent), response(std::move(response))
    {
        setRequest(request);
        setUrl(request.url());
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, this->response.status);
        if (!this->response.redirect.isEmpty())
            setAttribute(QNetworkRequest::RedirectionTargetAttribute, this->response.redirect);
        if (!this->response.retryAfter.isEmpty())
            setRawHeader("Retry-After", this->response.retryAfter);
        open(QIODevice::ReadOnly | QIODevice::Unbuffered);
        if (!this->response.held)
            QTimer::singleShot(0, this, &Reply::complete);
    }
    /** @brief Deliver bytes and terminal HTTP status exactly once. */
    void complete()
    {
        if (isFinished())
            return;
        available = response.bytes.size();
        emit readyRead();
        if (isFinished())
            return;
        emit downloadProgress(available, available);
        setFinished(true);
        emit finished();
    }
    void abort() override
    {
        if (isFinished())
            return;
        aborted = true;
        setError(OperationCanceledError, "Cancelled");
        setFinished(true);
        emit finished();
    }
    qint64 bytesAvailable() const override
    {
        return available - offset + QIODevice::bytesAvailable();
    }
    bool aborted = false;

protected:
    qint64 readData(char *data, qint64 maximum) override
    {
        const auto count = qMin(maximum, available - offset);
        if (count <= 0)
            return -1;
        std::memcpy(data, response.bytes.constData() + offset, size_t(count));
        offset += count;
        return count;
    }

private:
    Response response;
    qint64 available = 0, offset = 0;
};
/** @brief Queued in-memory responses with observable outgoing requests and an explicit deny host. */
class Network final : public QNetworkAccessManager, public INetworkPolicy
{
public:
    QList<Response> responses;
    QList<QNetworkRequest> requests;
    QPointer<Reply> latest;
    QString deniedHost;
    bool live = false;
    bool allowed(const QUrl &url) const override
    {
        return url.host() != deniedHost && (!live || url.host() == "api.waterdata.usgs.gov");
    }
    QNetworkAccessManager *shared() override { return this; }

protected:
    QNetworkReply *createRequest(Operation operation, const QNetworkRequest &request,
                                 QIODevice *outgoing) override
    {
        requests.append(request);
        if (live)
            return QNetworkAccessManager::createRequest(operation, request, outgoing);
        auto response = responses.isEmpty() ? Response{{}, 599} : responses.takeFirst();
        latest = new Reply(request, response, this);
        return latest;
    }
};
/** @brief Cancellable queued job; tests may request cancellation at a GDAL progress callback. */
class Job final : public IJob, public IJobContext
{
public:
    bool cancelled = false, finished = false, cancelOnProgress = false;
    void cancel() override { cancelled = true; }
    bool isFinished() const override { return finished; }
    bool isCancelled() const override { return cancelled; }
    void setProgress(double, const QString &) override
    {
        if (cancelOnProgress)
            cancelled = true;
    }
    void postToMainThread(std::function<void()> callback) override { callback(); }
};
/** @brief Minimal public host; no model or UI service participates in downloading. */
class Host final : public QObject, public ExtensionHost, public ISettings, public ProgressRecorder
{
    Q_OBJECT
    Q_INTERFACES(OpenSWMM::Sdk::IProgress)
public:
    /** @brief Bind fake scoped progress to this host's QObject lifetime. */
    Host() : ProgressRecorder(this) {}
    Network networkClient;
    QList<std::shared_ptr<Job>> jobs;
    bool cancelCrop = false;
    QString apiKey;
    CompatibilityManifest manifest() const override { return {}; }
    QString pluginId() const override { return "test.payload"; }
    QObject *lifetime() const override { return const_cast<Host *>(this); }
    QList<IProject *> projects() const override { return {}; }
    IProject *activeProject() const override { return nullptr; }
    QList<IModelInstance *> instances() const override { return {}; }
    IModelInstance *activeInstance() const override { return nullptr; }
    IUiHost *ui() override { return nullptr; }
    ISettings *settings() override { return this; }
    IConsole *console() override { return nullptr; }
    INetworkPolicy *network() override { return &networkClient; }
    IRunRegistry *runRegistry() override { return nullptr; }
    IPlotService *plots() override { return nullptr; }
    IDialogs *dialogs() override { return nullptr; }
    bool isDark() const override { return false; }
    QColor themeColor(QPalette::ColorRole) const override { return {}; }
    QMetaObject::Connection
    observe(QObject *, std::function<void(const QString &, const QVariantMap &)>) override
    {
        return {};
    }
    IJob *runJob(const QString &title, std::function<void(IJobContext &)> work,
                 std::function<void(bool, const QString &)> complete) override
    {
        auto job = std::make_shared<Job>();
        job->cancelOnProgress = cancelCrop && title == QStringLiteral("Crop DEM to reviewed area");
        jobs.append(job);
        QTimer::singleShot(
            0, this, [job, work = std::move(work), complete = std::move(complete)]() {
                if (!job->cancelled)
                    work(*job);
                job->finished = true;
                complete(!job->cancelled, job->cancelled ? QStringLiteral("Cancelled") : QString{});
            });
        return job.get();
    }
    void prepareShutdown() override
    {
        for (const auto &job : jobs)
            job->cancel();
    }
    QVariant value(const QString &key, const QVariant &fallback = {}) const override
    {
        return key == QStringLiteral("usgsApiKey") ? QVariant(apiKey) : fallback;
    }
    void setValue(const QString &, const QVariant &) override {}
    void remove(const QString &) override {}
    QStringList keys() const override { return {}; }
};
/** @brief Unique reviewable directory per case/run; previous evidence is never removed. */
QString outputDirectory(const QString &name)
{
    const auto path =
        QDir(QStringLiteral(SDK_DATA_PAYLOAD_VERIFICATION_DIRECTORY))
            .filePath(name + '/' + QUuid::createUuid().toString(QUuid::WithoutBraces));
    QDir().mkpath(path);
    return path;
}
/** @brief Read a saved artifact as bytes. */
QByteArray contents(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}
/** @brief Generate one observation with source precision and complete flags. */
QJsonObject observation(const QString &time,
                        const QJsonValue &value = QStringLiteral("1.234567890123456789"),
                        const QString &site = QStringLiteral("USGS-01646500"),
                        const QString &statistic = QStringLiteral("00003"))
{
    return {{"type", "Feature"},
            {"id", time + statistic},
            {"geometry", QJsonObject{{"type", "Point"}, {"coordinates", QJsonArray{-77.1, 38.9}}}},
            {"properties", QJsonObject{{"monitoring_location_id", site},
                                       {"parameter_code", "00060"},
                                       {"time_series_id", "series-a"},
                                       {"statistic_id", statistic},
                                       {"time", time},
                                       {"value", value},
                                       {"unit_of_measure", "ft^3/s"},
                                       {"approval_status", "Provisional"},
                                       {"qualifier", QJsonArray{"e", "A"}},
                                       {"extra_metadata", "preserve me"}}}};
}
/** @brief Form a synthetic modern OGC page with an optional next link. */
QByteArray page(const QJsonArray &records, const QString &next = {})
{
    QJsonObject document{{"type", "FeatureCollection"}, {"features", records}};
    if (!next.isEmpty())
        document["links"] = QJsonArray{QJsonObject{{"rel", "next"}, {"href", next}}};
    return QJsonDocument(document).toJson();
}
/** @brief Default single-site observation request with an inclusive UTC period. */
PayloadRequest observations(const QString &directory)
{
    PayloadRequest request;
    request.items = {Item{"USGS-01646500", "Synthetic station", {}, {}, {}}};
    request.from = QDate(2025, 1, 1);
    request.to = QDate(2025, 1, 2);
    request.destinationDir = directory;
    return request;
}
/** @brief Exact WGS84 multipart boundary with an interior hole and disconnected second part. */
QJsonObject area()
{
    return QJsonDocument::fromJson(
               R"({"type":"MultiPolygon","coordinates":[[[[1,1],[5,1],[5,5],[1,5],[1,1]],[[2,2],[2,4],[4,4],[4,2],[2,2]]],[[[7,1],[9,1],[9,3],[7,3],[7,1]]]]})")
        .object();
}
/** @brief Create a valid local 10×10 metre-unit elevation GeoTIFF, with zero-valued valid cells. */
QByteArray raster(const QString &directory, int width = 10, double origin = 0, double elevation = 0)
{
    GDALAllRegister();
    const auto path = QDir(directory).filePath(
        QStringLiteral("synthetic-source-%1-%2.tif").arg(width).arg(origin));
    auto *driver = GetGDALDriverManager()->GetDriverByName("GTiff");
    auto *dataset =
        driver ? driver->Create(path.toUtf8().constData(), width, 10, 1, GDT_Float32, nullptr)
               : nullptr;
    if (!dataset)
        return {};
    double transform[]{origin, 1, 0, 10, 0, -1};
    OGRSpatialReference srs;
    srs.importFromEPSG(4326);
    dataset->SetSpatialRef(&srs);
    dataset->SetGeoTransform(transform);
    auto *band = dataset->GetRasterBand(1);
    band->SetUnitType("m");
    band->Fill(elevation);
    GDALClose(dataset);
    return contents(path);
}
/** @brief Single advertised synthetic GeoTIFF with a frozen exact area. */
PayloadRequest terrain(const QString &directory)
{
    PayloadRequest request;
    request.source = Source::Terrain;
    request.items = {Item{
        "tile-a", "Synthetic DEM tile", {}, {}, QUrl("https://prd-tnm.s3.amazonaws.com/test.tif")}};
    request.destinationDir = directory;
    request.provenance = {{"area", area()}};
    return request;
}
}

class TestPayload : public QObject
{
    Q_OBJECT
private slots:
    /** @brief Download byte updates report measured progress without one log per byte count. */
    void sharedDownloadProgress()
    {
        Host host;
        host.networkClient.responses = {{page({observation("2025-01-01")}), 200, {}, true}};
        PayloadDownload download(&host);
        QSignalSpy done(&download, &PayloadDownload::finished);
        QVERIFY(download.start(observations(outputDirectory("shared-progress"))));
        QCOMPARE(host.activeProgress(), 1);
        QTRY_VERIFY(host.networkClient.latest);
        auto reply = host.networkClient.latest;
        emit reply->downloadProgress(10, 100);
        emit reply->downloadProgress(20, 100);
        emit reply->downloadProgress(30, 100);
        QCOMPARE(host.events.filter("Receiving download data").size(), 1);
        reply->complete();
        QTRY_COMPARE(done.size(), 1);
        QCOMPARE(host.activeProgress(), 0);
        QCOMPARE(host.tasks.size(), 1);
        const auto task = host.tasks.cbegin().value();
        QCOMPARE(task.outcome, ProgressOutcome::Succeeded);
        QVERIFY(task.fractions.contains(0.1));
        QVERIFY(task.detail.contains("manifest.json"));
    }
    /** @brief Synchronous progress start may dispose the owner without dangling task state. */
    void progressPublicationMayCloseDownload()
    {
        Host host;
        auto download = std::make_unique<PayloadDownload>(&host);
        host.publishing = [&] { host.publishing = {}; download.reset(); };
        const auto request = observations(outputDirectory("reentrant-progress"));
        QVERIFY(download->start(request));
        QVERIFY(!download);
        QCOMPARE(host.activeProgress(), 0);
        QVERIFY(host.networkClient.requests.isEmpty());
    }
    /** @brief Cancelling during shared begin keeps one terminal result and no orphan progress or network work. */
    void cancellationDuringProgressBegin()
    {
        Host host;
        PayloadDownload download(&host);
        QSignalSpy done(&download, &PayloadDownload::finished);
        host.publishing = [&] { host.publishing = {}; download.cancel(); };
        QVERIFY(download.start(observations(outputDirectory("cancel-during-begin"))));
        QTRY_COMPARE(done.size(), 1);
        QVERIFY(qvariant_cast<PayloadResult>(done.first().first()).cancelled);
        QCOMPARE(host.activeProgress(), 0);
        QCOMPARE(host.tasks.cbegin()->outcome, ProgressOutcome::Cancelled);
        QVERIFY(host.networkClient.requests.isEmpty());
    }
    /** @brief Explicit opt-in live public controller probe; never enabled by normal CI. */
    void liveOfficialController()
    {
        if (!qEnvironmentVariableIsSet("OPENSWMM_DATA_LIVE_TEST"))
            QSKIP("Set OPENSWMM_DATA_LIVE_TEST=1 to contact the official public USGS service.");
        for (bool daily : {true, false}) {
            Host host;
            host.networkClient.live = true;
            auto request = observations(outputDirectory(daily ? "live-daily" : "live-continuous"));
            request.from = request.to = QDate(2024, 12, 1);
            request.daily = daily;
            PayloadDownload download(&host);
            QSignalSpy done(&download, &PayloadDownload::finished);
            QVERIFY(download.start(request));
            QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 120000);
            const auto result = qvariant_cast<PayloadResult>(done.first().first());
            QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
            QVERIFY(!result.paths.isEmpty());
            const auto csv = contents(result.paths.last());
            QVERIFY(csv.contains("USGS-01646500"));
            QVERIFY(csv.count("\r\n") > 1);
            qInfo().noquote() << "Live controller manifest:" << result.manifestPath;
        }
    }
    /** @brief Check current daily and continuous official response contracts captured without credentials. */
    void officialContract_data()
    {
        QTest::addColumn<bool>("daily");
        QTest::newRow("daily") << true;
        QTest::newRow("continuous") << false;
    }
    void officialContract()
    {
        QFETCH(bool, daily);
        const auto fixture = QDir(QStringLiteral(SDK_DATA_PAYLOAD_FIXTURE_DIRECTORY))
                                 .filePath(daily ? QStringLiteral("usgs-daily.json")
                                                 : QStringLiteral("usgs-continuous.json"));
        auto document = QJsonDocument::fromJson(contents(fixture)).object();
        QVERIFY(!document.value("features").toArray().isEmpty());
        // This case checks the actual first-page contract; dedicated synthetic cases test paging.
        document["links"] = QJsonArray{};
        Host host;
        host.networkClient.responses = {{QJsonDocument(document).toJson()}};
        auto request = observations(outputDirectory("official-contract"));
        request.from = request.to = QDate(2024, 12, 1);
        request.daily = daily;
        PayloadDownload download(&host);
        QSignalSpy done(&download, &PayloadDownload::finished);
        QVERIFY(download.start(request));
        QTRY_COMPARE(done.size(), 1);
        const auto result = qvariant_cast<PayloadResult>(done.first().first());
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
        QVERIFY(contents(result.paths.last()).contains("ft^3/s"));
    }
    /** @brief Refuse invalid periods/parameters/DEM areas before IO or job creation. */
    void invalidRequests()
    {
        Host host;
        PayloadDownload download(&host);
        auto request = observations(outputDirectory("invalid"));
        request.to = request.from.addDays(-1);
        QString error;
        QVERIFY(!download.start(request, &error));
        QVERIFY(!error.isEmpty());
        request.to = request.from;
        request.parameterCode = "60";
        QVERIFY(!download.start(request));
        request = terrain(request.destinationDir);
        request.provenance = {};
        QVERIFY(!download.start(request));
        QVERIFY(host.networkClient.requests.isEmpty());
    }
    /** @brief Page, deduplicate and retain distinct statistics, source precision, flags and raw metadata. */
    void pagedObservations()
    {
        Host host;
        host.apiKey = "test-secret";
        const auto next = QStringLiteral(
            "https://api.waterdata.usgs.gov/ogcapi/v1/collections/daily/items?cursor=second");
        const auto first = observation("2025-01-01");
        host.networkClient.responses = {
            {page({first}, next)},
            {page({first, observation("2025-01-02", QJsonValue()),
                   observation("2025-01-01", "8.5", "USGS-01646500", "00001")})}};
        PayloadDownload download(&host);
        QSignalSpy done(&download, &PayloadDownload::finished);
        QString error;
        QVERIFY2(download.start(observations(outputDirectory("paged")), &error), qPrintable(error));
        QTRY_COMPARE(done.size(), 1);
        const auto result = qvariant_cast<PayloadResult>(done.first().first());
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
        QCOMPARE(result.paths.size(), 3);
        QCOMPARE(result.observationCount, qint64(3));
        QCOMPARE(host.networkClient.requests.size(), 2);
        const auto query = QUrlQuery(host.networkClient.requests.first().url());
        QCOMPARE(query.queryItemValue("monitoring_location_id"), "USGS-01646500");
        QCOMPARE(query.queryItemValue("parameter_code"), "00060");
        QCOMPARE(query.queryItemValue("datetime"),
                 "2025-01-01T00:00:00Z/2025-01-02T23:59:59.999999Z");
        QCOMPARE(host.networkClient.requests.first().rawHeader("X-Api-Key"),
                 QByteArray("test-secret"));
        const auto csv = contents(result.paths.last());
        QVERIFY(csv.contains("1.234567890123456789"));
        QVERIFY(csv.contains("ft^3/s"));
        QVERIFY(csv.contains("Provisional"));
        QVERIFY(csv.contains("preserve me"));
        QCOMPARE(csv.count("\r\n"), 4);
        QVERIFY(!contents(result.manifestPath).contains("test-secret"));
        QCOMPARE(QJsonDocument::fromJson(contents(result.manifestPath))
                     .object()
                     .value("status")
                     .toString(),
                 "complete");
        QVERIFY(!download.isRunning());
    }
    /** @brief Native descriptors retain reviewed site geometry and source-neutral mappings. */
    void nativeDescriptor()
    {
        Host host;
        host.networkClient.responses = {{page({observation("2025-01-01")})}};
        PayloadDownload download(&host);
        QSignalSpy done(&download, &PayloadDownload::finished);
        auto request = observations(outputDirectory("native-descriptor"));
        request.items[0].geometry =
            QJsonObject{{"type", "Point"}, {"coordinates", QJsonArray{-77.2, 39.1}}};
        request.items[0].metadata = QJsonObject{{"site_attribute", "001"}};
        QVERIFY(download.start(request));
        QTRY_COMPARE(done.size(), 1);
        const auto result = qvariant_cast<PayloadResult>(done.first().first());
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
        QVERIFY(!result.importId.isNull());
        const auto descriptor = QJsonDocument::fromJson(contents(result.descriptorPath)).object();
        QCOMPARE(descriptor["schema"].toString(), "openswmm.timeseries-import/1");
        QCOMPARE(descriptor["datasetId"].toString(),
                 result.importId.toString(QUuid::WithoutBraces));
        const auto location = descriptor["locations"].toArray().first().toObject();
        QCOMPARE(location["geometry"].toObject(), request.items[0].geometry);
        QCOMPARE(location["attributes"].toObject(), request.items[0].metadata);
        const auto source = descriptor["sources"].toArray().first().toObject();
        QCOMPARE(source["columns"].toObject()["timeBasis"].toString(), "time_basis");
        QVERIFY(contents(result.paths.last()).contains("calendar-date"));
    }
    /** @brief Conflicting duplicate samples are detected before first-wins CSV reduction. */
    void conflictingDuplicateRetainsRawResponse()
    {
        Host host;
        host.networkClient.responses = {
            {page({observation("2025-01-01", "1"), observation("2025-01-01", "2")})}};
        PayloadDownload download(&host);
        QSignalSpy done(&download, &PayloadDownload::finished);
        QVERIFY(download.start(observations(outputDirectory("conflict"))));
        QTRY_COMPARE(done.size(), 1);
        const auto result = qvariant_cast<PayloadResult>(done.first().first());
        QVERIFY(result.error.contains("Conflicting"));
        QVERIFY(result.descriptorPath.isEmpty());
        QCOMPARE(result.paths.size(), 1);
        QVERIFY(contents(result.paths.first()).contains("2025-01-01"));
    }
    /** @brief Preserve an honest empty period with headers/raw provenance and no invented zero sample. */
    void emptyObservations()
    {
        Host host;
        host.networkClient.responses = {{page({})}};
        PayloadDownload download(&host);
        QSignalSpy done(&download, &PayloadDownload::finished);
        QVERIFY(download.start(observations(outputDirectory("empty-period"))));
        QTRY_COMPARE(done.size(), 1);
        const auto result = qvariant_cast<PayloadResult>(done.first().first());
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
        QCOMPARE(result.observationCount, qint64(0));
        QVERIFY(result.descriptorPath.isEmpty());
        QCOMPARE(contents(result.paths.last()).count("\r\n"), 1);
        QCOMPARE(QJsonDocument::fromJson(contents(result.manifestPath))
                     .object()
                     .value("observationCount")
                     .toInteger(),
                 qint64(0));
    }
    /** @brief Keep original UTC offsets while applying date limits to normalized UTC instants. */
    void continuousOffsets()
    {
        Host host;
        host.networkClient.responses = {
            {page({observation("2025-01-02T00:30:00+01:00", "2.3e-19")})}};
        auto request = observations(outputDirectory("offsets"));
        request.daily = false;
        request.to = request.from;
        PayloadDownload download(&host);
        QSignalSpy done(&download, &PayloadDownload::finished);
        QVERIFY(download.start(request));
        QTRY_COMPARE(done.size(), 1);
        const auto result = qvariant_cast<PayloadResult>(done.first().first());
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
        QVERIFY(contents(result.paths.last()).contains("2025-01-02T00:30:00+01:00"));
        QVERIFY(contents(result.paths.last()).contains("2.3e-19"));
        QVERIFY(host.networkClient.requests.first().url().path().contains("/continuous/"));
    }
    /** @brief Refuse malformed, cross-site, wrong-parameter and outside-period pages without publishing a CSV. */
    void invalidObservation_data()
    {
        QTest::addColumn<QByteArray>("body");
        QTest::newRow("html") << QByteArray("<html>not data</html>");
        QTest::newRow("wrong site") << page({observation("2025-01-01", "1", "USGS-other")});
        auto wrong = observation("2025-01-01");
        auto properties = wrong.value("properties").toObject();
        properties["parameter_code"] = "00065";
        wrong["properties"] = properties;
        QTest::newRow("wrong parameter") << page({wrong});
        QTest::newRow("outside period") << page({observation("2024-12-31")});
        QTest::newRow("invalid value") << page({observation("2025-01-01", "NaN")});
    }
    void invalidObservation()
    {
        QFETCH(QByteArray, body);
        Host host;
        host.networkClient.responses = {{body}};
        PayloadDownload download(&host);
        QSignalSpy done(&download, &PayloadDownload::finished);
        QVERIFY(download.start(observations(outputDirectory("invalid-response"))));
        QTRY_COMPARE(done.size(), 1);
        const auto result = qvariant_cast<PayloadResult>(done.first().first());
        QVERIFY(!result.error.isEmpty());
        QVERIFY(result.paths.isEmpty());
    }
    /** @brief Refuse repeated page links and preserve completed raw responses with a clearly partial manifest. */
    void repeatedPage()
    {
        Host host;
        const auto next = QStringLiteral(
            "https://api.waterdata.usgs.gov/ogcapi/v1/collections/daily/items?cursor=loop");
        host.networkClient.responses = {{page({observation("2025-01-01")}, next)},
                                        {page({}, next)}};
        PayloadDownload download(&host);
        QSignalSpy done(&download, &PayloadDownload::finished);
        QVERIFY(download.start(observations(outputDirectory("loop"))));
        QTRY_COMPARE(done.size(), 1);
        const auto result = qvariant_cast<PayloadResult>(done.first().first());
        QVERIFY(!result.error.isEmpty());
        QVERIFY(result.partial);
        QCOMPARE(host.networkClient.requests.size(), 2);
        QCOMPARE(QJsonDocument::fromJson(contents(result.manifestPath))
                     .object()
                     .value("status")
                     .toString(),
                 "failed");
    }
    /** @brief Apply network policy to a redirect before issuing another request. */
    void redirectPolicy()
    {
        Host host;
        host.networkClient.deniedHost = "refused.example";
        host.networkClient.responses = {{{}, 302, QUrl("https://refused.example/data")}};
        PayloadDownload download(&host);
        QSignalSpy done(&download, &PayloadDownload::finished);
        QVERIFY(download.start(observations(outputDirectory("policy"))));
        QTRY_COMPARE(done.size(), 1);
        const auto result = qvariant_cast<PayloadResult>(done.first().first());
        QVERIFY(!result.error.isEmpty());
        QCOMPARE(host.networkClient.requests.size(), 1);
    }
    /** @brief Retry a transient service response and avoid retaining its error body as payload. */
    void transientRetry()
    {
        Host host;
        host.networkClient.responses = {{"busy", 503, {}, false, "1"},
                                        {page({observation("2025-01-01")})}};
        PayloadDownload download(&host);
        QSignalSpy done(&download, &PayloadDownload::finished);
        QVERIFY(download.start(observations(outputDirectory("retry"))));
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 4000);
        const auto result = qvariant_cast<PayloadResult>(done.first().first());
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
        QCOMPARE(host.networkClient.requests.size(), 2);
        QVERIFY(!contents(result.paths.first()).contains("busy"));
    }
    /** @brief Abort held network IO once and remove every uncommitted output. */
    void cancelledTransfer()
    {
        Host host;
        host.networkClient.responses = {{{}, 200, {}, true}};
        PayloadDownload download(&host);
        QSignalSpy done(&download, &PayloadDownload::finished);
        QVERIFY(download.start(terrain(outputDirectory("cancel"))));
        QTRY_VERIFY(host.networkClient.latest);
        auto reply = host.networkClient.latest;
        download.cancel();
        download.cancel();
        QTRY_COMPARE(done.size(), 1);
        const auto result = qvariant_cast<PayloadResult>(done.first().first());
        QVERIFY(result.cancelled);
        QCOMPARE(host.activeProgress(), 0);
        QCOMPARE(host.tasks.cbegin()->outcome, ProgressOutcome::Cancelled);
        QCOMPARE(host.events.filter(": finished:").size(), 1);
        QVERIFY(result.rasterPaths.isEmpty());
        QVERIFY(!QFileInfo::exists(
            QDir(QFileInfo(result.manifestPath).absolutePath()).filePath("tile-1.tif")));
        if (reply)
            QVERIFY(reply->aborted);
    }
    /** @brief Download a real tiny TIFF and verify the exact hole/multipart pixel mask and unchanged zero elevations. */
    void terrainClip()
    {
        const auto directory = outputDirectory("raster");
        const auto bytes = raster(directory, 5);
        QVERIFY(!bytes.isEmpty());
        Host host;
        host.networkClient.responses = {{bytes}, {raster(directory, 5, 5, 12.5)}};
        PayloadDownload download(&host);
        QSignalSpy done(&download, &PayloadDownload::finished);
        auto request = terrain(directory);
        request.items.append(Item{"tile-b",
                                  "Synthetic adjacent tile",
                                  {},
                                  {},
                                  QUrl("https://prd-tnm.s3.amazonaws.com/second.tif")});
        QVERIFY(download.start(request));
        QTRY_COMPARE(done.size(), 1);
        const auto result = qvariant_cast<PayloadResult>(done.first().first());
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
        QCOMPARE(result.rasterPaths.size(), 1);
        auto *dataset = static_cast<GDALDataset *>(
            GDALOpen(result.rasterPaths.first().toUtf8().constData(), GA_ReadOnly));
        QVERIFY(dataset);
        QCOMPARE(dataset->GetRasterXSize(), 8);
        QCOMPARE(dataset->GetRasterYSize(), 4);
        double transform[6];
        QCOMPARE(dataset->GetGeoTransform(transform), CE_None);
        QCOMPARE(transform[0], 1.0);
        QCOMPARE(transform[3], 5.0);
        auto *band = dataset->GetRasterBand(1);
        int validNoData = 0;
        QVERIFY(std::isnan(band->GetNoDataValue(&validNoData)));
        QVERIFY(validNoData);
        float pixels[32];
        QCOMPARE(band->RasterIO(GF_Read, 0, 0, 8, 4, pixels, 8, 4, GDT_Float32, 0, 0), CE_None);
        QCOMPARE(pixels[0], 0.0f);              // Valid terrain elevation, never used as nodata.
        QVERIFY(std::isnan(pixels[1 * 8 + 1])); // Hole centred on (2.5, 3.5).
        QVERIFY(std::isnan(pixels[1 * 8 + 5])); // Gap between disconnected parts.
        QCOMPARE(pixels[2 * 8 + 6], 12.5f);     // Separate polygon uses the second downloaded tile.
        GByte mask[32];
        QCOMPARE(band->GetMaskBand()->RasterIO(GF_Read, 0, 0, 8, 4, mask, 8, 4, GDT_Byte, 0, 0),
                 CE_None);
        QCOMPARE(mask[1 * 8 + 1], GByte(0));
        QCOMPARE(mask[2 * 8 + 6], GByte(255));
        GDALClose(dataset);
    }
    /** @brief Reject HTML saved behind a TIFF URL and never offer it as an importable raster. */
    void invalidRaster()
    {
        Host host;
        host.networkClient.responses = {{"<html>not elevation</html>"}};
        PayloadDownload download(&host);
        QSignalSpy done(&download, &PayloadDownload::finished);
        QVERIFY(download.start(terrain(outputDirectory("invalid-raster"))));
        QTRY_COMPARE(done.size(), 1);
        const auto result = qvariant_cast<PayloadResult>(done.first().first());
        QVERIFY(!result.error.isEmpty());
        QCOMPARE(host.activeProgress(), 0);
        QCOMPARE(host.tasks.cbegin()->outcome, ProgressOutcome::Failed);
        QVERIFY(result.rasterPaths.isEmpty());
        for (const auto &path : result.paths)
            QVERIFY(!path.endsWith(".tif"));
    }
    /** @brief GDAL progress cancellation retains original tiles and removes the unpublished cropped DEM. */
    void cancelledCrop()
    {
        const auto directory = outputDirectory("crop-cancel");
        Host host;
        host.cancelCrop = true;
        host.networkClient.responses = {{raster(directory)}};
        PayloadDownload download(&host);
        QSignalSpy done(&download, &PayloadDownload::finished);
        QVERIFY(download.start(terrain(directory)));
        QTRY_COMPARE(done.size(), 1);
        const auto result = qvariant_cast<PayloadResult>(done.first().first());
        QVERIFY(!result.error.isEmpty());
        QVERIFY(result.partial);
        QVERIFY(result.rasterPaths.isEmpty());
        const QDir run(QFileInfo(result.manifestPath).absolutePath());
        QVERIFY(run.exists("tile-1.tif"));
        QVERIFY(!run.exists("area-dem.tif"));
        QVERIFY(!run.exists("area-dem.partial.tif"));
    }
};
QTEST_MAIN(TestPayload)
#include "test_payload.moc"
