/**
 * @file PayloadDownload.cpp
 * @brief Bounded modern USGS observation paging and streamed TNM GeoTIFF retrieval.
 * @details HTTPS destinations and every redirect/page pass the host policy. QSaveFile publication
 * keeps partial transfers private. Original JSON pages and a terminal manifest document retained
 * outputs, including partial runs. CSV values are never converted to model units or local time.
 * @author Caleb Buahin <caleb.buahin@gmail.com>
 * @date 2026
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license GPL-3.0-or-later
 */
#include "PayloadDownload.hpp"
#include "OperationProgress.hpp"
#include <QDateTime>
#include <QCryptographicHash>
#include <QHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QTimer>
#include <QUrlQuery>
#include <QUuid>
#include <QTimeZone>
#include <gdal_priv.h>
#include <gdal_utils.h>
#include <algorithm>
#include <cmath>

namespace OpenSWMM::Data
{
namespace
{
constexpr qint64 maxPageBytes = 20 * 1024 * 1024;
constexpr qint64 maxSiteBytes = 256 * 1024 * 1024;
constexpr qint64 maxRasterBytes = 2LL * 1024 * 1024 * 1024;
constexpr int maxPages = 256;
constexpr int maxRecords = 200000;

/** @brief Quote one CSV cell, preserving all literal source content and scientific precision. */
QByteArray csvCell(const QString &value)
{
    auto bytes = value.toUtf8();
    bytes.replace("\"", "\"\"");
    return '"' + bytes + '"';
}
/** @brief Keep original strings; serialize other scalar/structured JSON without decimal rounding. */
QString text(const QJsonValue &value)
{
    if (value.isNull() || value.isUndefined())
        return {};
    if (value.isString())
        return value.toString();
    if (value.isDouble())
        return QString::number(value.toDouble(), 'g', 17);
    if (value.isBool())
        return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    return QString::fromUtf8(value.isObject()
                                 ? QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact)
                                 : QJsonDocument(value.toArray()).toJson(QJsonDocument::Compact));
}
/** @brief Publish an entire new file atomically, refusing existing output paths. */
bool writeNew(const QString &path, const QByteArray &bytes)
{
    if (QFileInfo::exists(path))
        return false;
    QSaveFile output(path);
    output.setDirectWriteFallback(false);
    return output.open(QIODevice::WriteOnly) && output.write(bytes) == bytes.size() &&
           output.commit();
}
/** @brief Remove credentials from retained URLs, even when a server next link repeats them. */
QString publicUrl(QUrl url)
{
    url.setUserInfo({});
    QUrlQuery query(url);
    query.removeAllQueryItems(QStringLiteral("api_key"));
    url.setQuery(query);
    return url.toString();
}
/** @brief Immutable worker result for a validated observation page. */
struct Page {
    QList<QJsonObject> records;
    QUrl next;
    QString error;
};
/** @brief Validate every observation against the selected location, parameter and requested period. */
Page decodePage(const QByteArray &bytes, const PayloadRequest &request, const QString &site,
                Sdk::IJobContext &job)
{
    Page page;
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject() ||
        document.object().value("type").toString() != QStringLiteral("FeatureCollection") ||
        !document.object().value("features").isArray()) {
        page.error =
            QStringLiteral("The observation service did not return a GeoJSON feature collection.");
        return page;
    }
    const auto rows = document.object().value("features").toArray();
    if (rows.size() > maxRecords) {
        page.error = QStringLiteral(
            "The observation response exceeds the record limit. Choose a shorter period.");
        return page;
    }
    for (const auto &row : rows) {
        if (job.isCancelled())
            return {};
        const auto feature = row.toObject();
        const auto properties = feature.value("properties").toObject();
        if (properties.value("monitoring_location_id").toString() != site ||
            properties.value("parameter_code").toString() != request.parameterCode) {
            page.error = QStringLiteral(
                "An observation response contains a different site or parameter; nothing from that page was saved.");
            return page;
        }
        const auto timestamp = properties.value("time").toString();
        const QDate date = QDate::fromString(timestamp, Qt::ISODate);
        const auto instant = QDateTime::fromString(timestamp, Qt::ISODateWithMs);
        const bool dateOnly = request.daily && timestamp.size() == 10 && date.isValid();
        const bool hasOffset =
            timestamp.endsWith('Z') || QRegularExpression(QStringLiteral("[+-][0-9]{2}:[0-9]{2}$"))
                                           .match(timestamp)
                                           .hasMatch();
        const auto utcDate = dateOnly ? date : instant.toUTC().date();
        if ((!dateOnly && (!instant.isValid() || !hasOffset)) || utcDate < request.from ||
            utcDate > request.to) {
            page.error = QStringLiteral(
                "An observation timestamp is invalid or outside the requested period.");
            return page;
        }
        const auto value = properties.value("value");
        bool numeric = value.isNull();
        double number = 0;
        if (!value.isNull())
            number = text(value).toDouble(&numeric);
        if (value.isUndefined() || !numeric || !std::isfinite(number) ||
            properties.value("time_series_id").toString().isEmpty()) {
            page.error = QStringLiteral(
                "An observation has an unsupported value or missing series identity.");
            return page;
        }
        page.records.append(feature);
    }
    for (const auto &entry : document.object().value("links").toArray()) {
        const auto link = entry.toObject();
        if (link.value("rel").toString() == QStringLiteral("next")) {
            if (!page.next.isEmpty() || link.value("href").toString().isEmpty()) {
                page.error =
                    QStringLiteral("The observation service returned ambiguous pagination links.");
                return page;
            }
            page.next = QUrl(link.value("href").toString());
        }
    }
    return page;
}
/** @brief Serialize separate statistic/series rows without discarding source flags or null samples. */
QByteArray observationsCsv(QList<QJsonObject> records, Sdk::IJobContext &job)
{
    const QStringList fields{"monitoring_location_id",
                             "parameter_code",
                             "time_series_id",
                             "statistic_id",
                             "time",
                             "value",
                             "unit_of_measure",
                             "approval_status",
                             "qualifier"};
    std::stable_sort(records.begin(), records.end(), [](const auto &left, const auto &right) {
        const auto a = left.value("properties").toObject(),
                   b = right.value("properties").toObject();
        for (const auto &key : {"time_series_id", "statistic_id"}) {
            const auto av = a.value(key).toString(), bv = b.value(key).toString();
            if (av != bv)
                return av < bv;
        }
        const auto instant = [](const QJsonObject &properties) {
            const auto timestamp = properties.value("time").toString();
            return timestamp.size() == 10
                       ? QDateTime(QDate::fromString(timestamp, Qt::ISODate), QTime(0, 0),
                                   QTimeZone::UTC)
                       : QDateTime::fromString(timestamp, Qt::ISODateWithMs).toUTC();
        };
        return instant(a) < instant(b);
    });
    QByteArray output =
        fields.join(',').toUtf8() + ",observation_id,properties_json,geometry_json,time_basis\r\n";
    for (const auto &feature : records) {
        if (job.isCancelled())
            return {};
        const auto properties = feature.value("properties").toObject();
        QList<QByteArray> cells;
        for (const auto &field : fields)
            cells.append(csvCell(text(properties.value(field))));
        cells.append(csvCell(text(feature.value("id"))));
        cells.append(
            csvCell(QString::fromUtf8(QJsonDocument(properties).toJson(QJsonDocument::Compact))));
        cells.append(csvCell(QString::fromUtf8(
            QJsonDocument(feature.value("geometry").toObject()).toJson(QJsonDocument::Compact))));
        cells.append(csvCell(properties.value("time").toString().size() == 10
                                 ? QStringLiteral("calendar-date")
                                 : QStringLiteral("instant")));
        output += cells.join(',') + "\r\n";
    }
    return output;
}
/** @brief Inspect only local GTiff data; never allow a downloaded document to open a network-backed GDAL datasource. */
QString validateRaster(const QString &path, Sdk::IJobContext &job, QJsonObject *metadata = nullptr)
{
    if (job.isCancelled())
        return {};
    GDALAllRegister();
    const char *drivers[]{"GTiff", nullptr};
    auto *dataset = static_cast<GDALDataset *>(GDALOpenEx(
        path.toUtf8().constData(), GDAL_OF_RASTER | GDAL_OF_READONLY, drivers, nullptr, nullptr));
    if (!dataset)
        return QStringLiteral("The downloaded payload is not a readable GeoTIFF raster.");
    double transform[6];
    QString error;
    if (dataset->GetRasterXSize() <= 0 || dataset->GetRasterYSize() <= 0 ||
        dataset->GetRasterCount() < 1 || !dataset->GetSpatialRef() ||
        dataset->GetGeoTransform(transform) != CE_None ||
        !std::all_of(std::begin(transform), std::end(transform),
                     [](double value) { return std::isfinite(value); }))
        error =
            QStringLiteral("The GeoTIFF has no usable raster dimensions, CRS or georeferencing.");
    if (error.isEmpty()) {
        auto *band = dataset->GetRasterBand(1);
        double pixel = 0;
        if (band->RasterIO(GF_Read, 0, 0, 1, 1, &pixel, 1, 1, GDT_Float64, 0, 0) != CE_None)
            error = QStringLiteral("The GeoTIFF pixel data is unreadable.");
    }
    if (error.isEmpty() && metadata) {
        QJsonArray geotransform, bands;
        for (double coefficient : transform)
            geotransform.append(coefficient);
        for (int i = 1; i <= dataset->GetRasterCount(); ++i) {
            auto *band = dataset->GetRasterBand(i);
            int hasNoData = 0;
            const double noData = band->GetNoDataValue(&hasNoData);
            bands.append(QJsonObject{
                {"type", GDALGetDataTypeName(band->GetRasterDataType())},
                {"unit", QString::fromUtf8(band->GetUnitType())},
                {"noData", hasNoData ? std::isnan(noData) ? QJsonValue("NaN") : QJsonValue(noData)
                                     : QJsonValue()}});
        }
        *metadata = {{"width", dataset->GetRasterXSize()},
                     {"height", dataset->GetRasterYSize()},
                     {"crsWkt", QString::fromUtf8(dataset->GetProjectionRef())},
                     {"geotransform", geotransform},
                     {"bands", bands}};
    }
    GDALClose(dataset);
    return error;
}

/** @brief Adapt host cancellation/progress to a GDAL application operation. */
int warpProgress(double fraction, const char *, void *user)
{
    auto &job = *static_cast<Sdk::IJobContext *>(user);
    job.setProgress(fraction, QStringLiteral("Crop DEM to reviewed boundary"));
    return !job.isCancelled();
}
/** @brief Mosaic and mask local tiles to the exact cutline, preserving zero as a valid elevation. */
QString cropRaster(const QStringList &paths, const QString &cutline, const QString &output,
                   Sdk::IJobContext &job, QJsonObject *metadata)
{
    GDALAllRegister();
    QList<GDALDatasetH> sources;
    const char *drivers[]{"GTiff", nullptr};
    for (const auto &path : paths) {
        auto *dataset = GDALOpenEx(path.toUtf8().constData(), GDAL_OF_RASTER | GDAL_OF_READONLY,
                                   drivers, nullptr, nullptr);
        if (!dataset) {
            for (auto *source : sources)
                GDALClose(source);
            return QStringLiteral("A downloaded DEM tile could not be reopened for clipping.");
        }
        sources.append(dataset);
    }
    QStringList arguments{"-of",
                          "GTiff",
                          "-cutline",
                          cutline,
                          "-crop_to_cutline",
                          "-dstnodata",
                          "nan",
                          "-ot",
                          "Float32",
                          "-co",
                          "TILED=YES",
                          "-co",
                          "COMPRESS=DEFLATE",
                          "-co",
                          "PREDICTOR=3"};
    QList<QByteArray> encoded;
    for (const auto &argument : arguments)
        encoded.append(argument.toUtf8());
    QList<char *> argv;
    for (auto &argument : encoded)
        argv.append(argument.data());
    argv.append(nullptr);
    auto *options = GDALWarpAppOptionsNew(argv.data(), nullptr);
    if (options)
        GDALWarpAppOptionsSetProgress(options, warpProgress, &job);
    int usageError = 0;
    GDALDatasetH derived = nullptr;
    if (options && !job.isCancelled())
        derived = GDALWarp(output.toUtf8().constData(), nullptr, sources.size(), sources.data(),
                           options, &usageError);
    if (derived)
        GDALClose(derived);
    GDALWarpAppOptionsFree(options);
    for (auto *source : sources)
        GDALClose(source);
    if (!derived || usageError || job.isCancelled()) {
        QFile::remove(output);
        return QStringLiteral(
            "DEM clipping was cancelled or failed; original tiles were retained.");
    }
    const auto error = validateRaster(output, job, metadata);
    if (!error.isEmpty() || job.isCancelled())
        QFile::remove(output);
    return error;
}
}

/**
 * @brief Sole GUI-thread transfer state, owned by PayloadDownload.
 * @details Serial requests bound memory to one 20 MiB observation page or a 512 KiB raster
 * read buffer. One site's observations are limited to 200000 records/256 MiB/256 pages;
 * original raster payloads are limited to 2 GiB each. Worker callbacks own copied data and
 * use a guarded controller plus generation check before publication. A run finishes once,
 * retaining committed files and recording partial outcomes in its manifest.
 */
class PayloadDownload::Impl
{
public:
    /** @brief Initialize an idle state without making requests or taking SDK ownership.
     * @param[in] owner Borrowed enclosing controller, owning this state until destruction.
     * @param[in] host Borrowed GUI-thread host; its QObject lifetime fences SDK calls. */
    Impl(PayloadDownload *owner, Sdk::ExtensionHost *host)
        : q(owner), host(host), lifetime(host ? host->lifetime() : nullptr), progress(host, owner)
    {
    }
    PayloadDownload *q;       ///< Borrowed owner and GUI signal context; never retained by workers.
    Sdk::ExtensionHost *host; ///< Borrowed SDK facade, used only while lifetime survives.
    QPointer<QObject> lifetime; ///< Host destruction guard, independent of project/model lifetime.
    OperationProgress progress; ///< Enclosing transfer task; nested parsing/crop jobs report their own host progress.
    PayloadRequest request;     ///< Immutable owning inputs for the accepted run.
    PayloadResult result;       ///< Growing committed-output list, copied for terminal delivery.
    QPointer<QNetworkReply> reply; ///< Active caller-owned reply, aborted/deleted on termination.
    Sdk::IJob *job = nullptr; ///< Borrowed host-retained worker handle; cancelled without deletion.
    std::unique_ptr<QSaveFile>
        output;        ///< Owned uncommitted original-tile writer; reset discards partial bytes.
    QString directory; ///< New unique run directory; no earlier run is reused or overwritten.
    QString
        outputPath; ///< Original tile candidate, removed when validation fails before retention.
    QString cutlinePath;   ///< Retained exact WGS84 GeoJSON boundary for the derived DEM.
    QString pendingPath;   ///< Worker-owned unpublished CSV/DEM path, cleaned on cancellation.
    QStringList tilePaths; ///< Validated original tiles awaiting crop; absent from rasterPaths.
    QJsonArray
        fileRecords; ///< Manifest entries for committed paths and raster inspection metadata.
    QList<QJsonObject> records; ///< One site's bounded deduplicated observation features.
    QSet<QString> seenPages;    ///< Public next-page URLs preventing loops within the current site.
    QHash<QString, QByteArray>
        seenRecords; ///< Normalized series/time identity to original record hash; conflicts fail.
    QJsonArray
        observationSources; ///< Complete CSV mappings for the source-neutral import descriptor.
    int itemIndex = 0; ///< Zero-based selected site/tile index; advances after complete validation.
    int pageIndex = 0; ///< One-based observation page count for the current site.
    qint64 siteBytes = 0;   ///< Accumulated raw JSON bytes enforcing the per-site bound.
    qint64 received = 0;    ///< Current transfer bytes, reset for redirects/retries.
    quint64 generation = 0; ///< Work token invalidated before aborting IO or cancelling workers.
    bool running = false;   ///< Accepted run still owns transfer/processing authority.
    bool finishing = false; ///< Terminal signal is queued; new starts wait until delivery.

    /** @brief Atomically update this run's own manifest, including incomplete outcomes.
     * @param[in] status Exact in-progress/terminal caption retained as plain JSON.
     * @return True after atomic commit; false on open/write/commit failure. */
    bool manifest(const QString &status)
    {
        QJsonObject document{{"schema", "openswmm-data-download/v1"},
                             {"status", status},
                             {"source", request.source == Source::Sites
                                            ? "USGS observations"
                                            : "3DEP original GeoTIFF tiles"},
                             {"updatedAt", QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
                             {"fromInclusiveUtc", request.from.toString(Qt::ISODate)},
                             {"toInclusiveUtc", request.to.toString(Qt::ISODate)},
                             {"collection", request.daily ? "daily" : "continuous"},
                             {"parameterCode", request.parameterCode},
                             {"provenance", request.provenance},
                             {"files", fileRecords},
                             {"observationCount", result.observationCount},
                             {"timeSeriesDescriptor", QFileInfo(result.descriptorPath).fileName()},
                             {"cancelled", result.cancelled},
                             {"partial", result.partial},
                             {"error", result.error}};
        QJsonArray selected;
        for (const auto &item : request.items)
            selected.append(QJsonObject{{"id", item.id},
                                        {"title", item.title},
                                        {"metadata", item.metadata},
                                        {"geometry", item.geometry},
                                        {"download", publicUrl(item.download)}});
        document["selectedItems"] = selected;
        QSaveFile file(result.manifestPath);
        file.setDirectWriteFallback(false);
        const auto bytes = QJsonDocument(document).toJson();
        return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() &&
               file.commit();
    }
    /** @brief Record only a fully committed output and its source URL.
     * @param[in] path Existing local output owned by this run; not modified here.
     * @param[in] kind Human-readable artifact category.
     * @param[in] url Source URL; credentials/API query keys are stripped from the manifest.
     * @param[in] metadata Optional owning raster inspection/processing description. */
    void retained(const QString &path, const QString &kind, const QUrl &url = {},
                  const QJsonObject &metadata = {})
    {
        result.paths.append(path);
        fileRecords.append(QJsonObject{{"path", QFileInfo(path).fileName()},
                                       {"kind", kind},
                                       {"source", publicUrl(url)},
                                       {"bytes", QString::number(QFileInfo(path).size())},
                                       {"raster", metadata}});
    }
    /** @brief Fence callbacks before stopping IO and emit one queued terminal result.
     * @param[in] error Plain failure detail, or empty for success/explicit cancellation.
     * @param[in] cancelled Explicit cancellation outcome rather than a service failure.
     * @note Discards active writers, cooperatively cancels workers and retains committed files.
     * Repeated terminal calls are inert. Queued GUI completion requires owner survival. */
    void finish(const QString &error = {}, bool cancelled = false)
    {
        if (!running)
            return;
        running = false;
        ++generation;
        if (reply) {
            QObject::disconnect(reply, nullptr, q, nullptr);
            reply->abort();
            reply->deleteLater();
            reply = nullptr;
        }
        if (job && lifetime)
            job->cancel();
        job = nullptr;
        output.reset();
        if (!outputPath.isEmpty() && !result.paths.contains(outputPath))
            QFile::remove(outputPath);
        if (!pendingPath.isEmpty())
            QFile::remove(pendingPath);
        result.error = error;
        result.cancelled = cancelled;
        result.partial = (cancelled || !error.isEmpty()) && !result.paths.isEmpty();
        if (!manifest(cancelled         ? QStringLiteral("cancelled")
                      : error.isEmpty() ? QStringLiteral("complete")
                                        : QStringLiteral("failed")))
            result.error += QStringLiteral(" Could not update the download manifest.");
        finishing = true;
        const auto terminal = result;
        QTimer::singleShot(0, q, [this, terminal]() {
            finishing = false;
            emit q->finished(terminal);
        });
        progress.finish(cancelled ? Sdk::ProgressOutcome::Cancelled
                                  : result.error.isEmpty() ? Sdk::ProgressOutcome::Succeeded
                                                           : Sdk::ProgressOutcome::Failed,
                        result.error.isEmpty()
                            ? QStringLiteral("%1 complete files retained. Transfer details: %2")
                                  .arg(result.paths.size()).arg(result.manifestPath)
                            : result.error + QStringLiteral(" Transfer details: ") + result.manifestPath);
    }
    /** @brief Publish the native import contract only after all requested observation CSVs completed.
     * @return True after writing new source-neutral metadata; no model/GUI objects are accessed. */
    bool writeDescriptor()
    {
        QJsonArray locations;
        for (const auto &item : request.items)
            locations.append(QJsonObject{{"id", item.id},
                                         {"title", item.title},
                                         {"geometry", item.geometry},
                                         {"attributes", item.metadata}});
        const QJsonObject descriptor{
            {"schema", "openswmm.timeseries-import/1"},
            {"datasetId", result.importId.toString(QUuid::WithoutBraces)},
            {"title", QStringLiteral("USGS %1 — %2 to %3")
                          .arg(request.parameterCode, request.from.toString(Qt::ISODate),
                               request.to.toString(Qt::ISODate))},
            {"provenance", QJsonObject{{"downloadManifest", "manifest.json"},
                                       {"review", request.provenance},
                                       {"provider", "USGS"},
                                       {"collection", request.daily ? "daily" : "continuous"}}},
            {"sources", observationSources},
            {"locations", locations}};
        const auto path = QDir(directory).filePath("timeseries-import.json");
        if (!writeNew(path, QJsonDocument(descriptor).toJson()))
            return false;
        result.descriptorPath = path;
        return true;
    }
    /** @brief Start the next independent selection, or finalize the complete run. */
    void nextItem()
    {
        if (!running)
            return;
        if (!lifetime) {
            finish(QStringLiteral("The plugin host closed."));
            return;
        }
        if (itemIndex == request.items.size()) {
            if (request.source == Source::Terrain)
                cropDem();
            else {
                if (result.observationCount > 0 && !writeDescriptor()) {
                    finish(QStringLiteral(
                        "Observations were saved, but their import descriptor could not be written."));
                    return;
                }
                finish();
            }
            return;
        }
        pageIndex = 0;
        siteBytes = 0;
        records.clear();
        seenPages.clear();
        seenRecords.clear();
        const auto &item = request.items[itemIndex];
        const QPointer<PayloadDownload> guard(q);
        const auto token = generation;
        emit q->progress(double(itemIndex) / request.items.size(),
                         QStringLiteral("Downloading %1…").arg(item.title));
        if (!guard || !running || token != generation) return;
        if (request.source == Source::Terrain) {
            outputPath = QDir(directory).filePath(QStringLiteral("tile-%1.tif").arg(itemIndex + 1));
            output = std::make_unique<QSaveFile>(outputPath);
            output->setDirectWriteFallback(false);
            if (QFileInfo::exists(outputPath) || !output->open(QIODevice::WriteOnly)) {
                finish(QStringLiteral("Cannot create the selected GeoTIFF output."));
                return;
            }
            fetch(item.download);
        } else {
            outputPath.clear();
            QUrl url(QStringLiteral("https://api.waterdata.usgs.gov/ogcapi/v1/collections/%1/items")
                         .arg(request.daily ? "daily" : "continuous"));
            QUrlQuery query;
            query.addQueryItem("monitoring_location_id", item.id);
            query.addQueryItem("parameter_code", request.parameterCode);
            query.addQueryItem("datetime", request.from.toString(Qt::ISODate) + "T00:00:00Z/" +
                                               request.to.toString(Qt::ISODate) +
                                               "T23:59:59.999999Z");
            query.addQueryItem("limit", "10000");
            query.addQueryItem("f", "json");
            url.setQuery(query);
            seenPages.insert(publicUrl(url));
            fetch(url);
        }
    }
    /** @brief Perform a policy-checked transfer with bounded manual redirect/retry handling.
     * @param[in] url HTTPS destination without embedded credentials; every page/redirect is rechecked.
     * @param[in] attempt Retry index, limited to three retries for HTTP 429/503.
     * @param[in] redirects Followed redirect count, capped at five.
     * @note Uses the host manager with a 60-second transfer timeout. API key headers are
     * restricted to the exact USGS host. GDAL never opens a remote datasource. */
    void fetch(const QUrl &url, int attempt = 0, int redirects = 0)
    {
        if (!running || !lifetime)
            return;
        if (url.scheme() != QStringLiteral("https") || url.host().isEmpty() ||
            !url.userInfo().isEmpty() || !host->network() || !host->network()->allowed(url)) {
            finish(
                QStringLiteral("The download destination is not permitted by the network policy."));
            return;
        }
        const auto token = generation;
        QNetworkRequest networkRequest(url);
        networkRequest.setTransferTimeout(60000);
        networkRequest.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                                    QNetworkRequest::ManualRedirectPolicy);
        networkRequest.setRawHeader("User-Agent", "OpenSWMM-Data/0.1");
        networkRequest.setRawHeader("Accept", request.source == Source::Sites
                                                  ? "application/geo+json,application/json"
                                                  : "image/tiff,application/octet-stream");
        const auto key = host->settings()
                             ? host->settings()->value("usgsApiKey").toString().trimmed()
                             : QString{};
        if (request.source == Source::Sites &&
            url.host() == QStringLiteral("api.waterdata.usgs.gov") && !key.isEmpty())
            networkRequest.setRawHeader("X-Api-Key", key.toUtf8());
        const QPointer<PayloadDownload> guard(q);
        const auto hostGuard = lifetime;
        auto *manager = host->network()->shared();
        if (!manager) {
            finish(QStringLiteral("The network service is unavailable."));
            return;
        }
        auto *response = manager->get(networkRequest);
        if (!guard || !hostGuard || !guard->d->running || token != guard->d->generation) {
            if (response) {
                response->abort();
                response->deleteLater();
            }
            return;
        }
        if (!response) {
            finish(QStringLiteral("The network request could not be started."));
            return;
        }
        reply = response;
        received = 0;
        auto bytes = std::make_shared<QByteArray>();
        auto failure = std::make_shared<QString>();
        const auto consume = [this, token, response, bytes, failure]() {
            if (!running || token != generation || !failure->isEmpty())
                return;
            while (response->bytesAvailable() > 0) {
                const auto chunk = response->read(256 * 1024);
                if (chunk.isEmpty())
                    break;
                received += chunk.size();
                if (received > (request.source == Source::Sites ? maxPageBytes : maxRasterBytes)) {
                    *failure = QStringLiteral(
                        "The download exceeds the size limit. Choose a smaller period or product.");
                    response->abort();
                    return;
                }
                if (request.source == Source::Sites)
                    bytes->append(chunk);
                else if (!output || output->write(chunk) != chunk.size()) {
                    *failure =
                        QStringLiteral("The GeoTIFF could not be written; check free disk space.");
                    response->abort();
                    return;
                }
            }
        };
        response->setReadBufferSize(512 * 1024);
        QObject::connect(response, &QIODevice::readyRead, q, consume);
        QObject::connect(
            response, &QNetworkReply::downloadProgress, q,
            [this, token](qint64 bytes, qint64 total) {
                if (running && token == generation) {
                    const auto limit =
                        request.source == Source::Sites ? maxPageBytes : maxRasterBytes;
                    if (total > limit) {
                        finish(QStringLiteral(
                            "The advertised payload exceeds the download size limit."));
                        return;
                    }
                    const double part = total > 0 ? qMin(0.95, double(bytes) / total) : 0;
                    emit q->progress(total > 0 ? (itemIndex + part) / request.items.size() : -1.,
                                     QStringLiteral("Received %1 bytes…").arg(bytes));
                }
            });
        QObject::connect(
            response, &QNetworkReply::finished, q,
            [this, token, response, bytes, failure, consume, url, attempt, redirects]() {
                response->deleteLater();
                if (!running || token != generation)
                    return;
                consume();
                if (!running || token != generation)
                    return;
                reply = nullptr;
                const int status =
                    response->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                const auto location =
                    response->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl();
                if (status >= 300 && status < 400 && !location.isEmpty() && redirects < 5) {
                    if (request.source == Source::Terrain) {
                        output.reset();
                        output = std::make_unique<QSaveFile>(outputPath);
                        output->setDirectWriteFallback(false);
                        if (!output->open(QIODevice::WriteOnly)) {
                            finish(QStringLiteral("Cannot restart the GeoTIFF output."));
                            return;
                        }
                    }
                    fetch(url.resolved(location), attempt, redirects + 1);
                    return;
                }
                if ((status == 429 || status == 503) && attempt < 3) {
                    bool ok = false;
                    const int retry = response->rawHeader("Retry-After").toInt(&ok);
                    const int seconds = ok ? qBound(1, retry, 60) : (1 << attempt);
                    if (request.source == Source::Terrain) {
                        output.reset();
                        output = std::make_unique<QSaveFile>(outputPath);
                        output->setDirectWriteFallback(false);
                        if (!output->open(QIODevice::WriteOnly)) {
                            finish(QStringLiteral("Cannot retry the GeoTIFF output."));
                            return;
                        }
                    }
                    const QPointer<PayloadDownload> guard(q);
                    emit q->progress(
                        double(itemIndex) / request.items.size(),
                        QStringLiteral("Service busy; retrying in %1 seconds…").arg(seconds));
                    if (!guard || !running || token != generation) return;
                    QTimer::singleShot(seconds * 1000, q, [this, token, url, attempt, redirects]() {
                        if (running && token == generation)
                            fetch(url, attempt + 1, redirects);
                    });
                    return;
                }
                if (!failure->isEmpty() || response->error() != QNetworkReply::NoError ||
                    status < 200 || status >= 300) {
                    finish(!failure->isEmpty() ? *failure
                                               : QStringLiteral("Download failed (HTTP %1): %2")
                                                     .arg(status)
                                                     .arg(response->errorString()));
                    return;
                }
                if (request.source == Source::Sites)
                    parsePage(*bytes, url);
                else if (!output->commit())
                    finish(QStringLiteral("The downloaded GeoTIFF could not be committed."));
                else {
                    output.reset();
                    checkRaster(url);
                }
            });
    }
    /** @brief Validate one owning JSON page in the host worker pool, then publish its raw bytes.
     * @param[in] bytes Complete bounded response copied into the worker.
     * @param[in] url Actual response URL used to resolve next links and record provenance. */
    void parsePage(const QByteArray &bytes, const QUrl &url)
    {
        if (++pageIndex > maxPages || (siteBytes += bytes.size()) > maxSiteBytes) {
            finish(QStringLiteral(
                "The observation download exceeds the paging limit. Choose a shorter period."));
            return;
        }
        const auto token = generation;
        const auto capturedRequest = request;
        const auto site = request.items[itemIndex].id;
        auto parsed = std::make_shared<Page>();
        QPointer<PayloadDownload> guard(q);
        const auto hostGuard = lifetime;
        progress.update(-1, QStringLiteral("Validate USGS observations"));
        if (!guard || !hostGuard || !running || token != generation) return;
        auto *submittedJob = host->runJob(
            QStringLiteral("Validate USGS observations"),
            [bytes, capturedRequest, site, parsed](Sdk::IJobContext &context) {
                *parsed = decodePage(bytes, capturedRequest, site, context);
            },
            [guard, token, parsed, bytes, url](bool ok, const QString &error) {
                if (!guard || !guard->d->running || token != guard->d->generation)
                    return;
                auto &d = *guard->d;
                d.job = nullptr;
                if (!ok || !parsed->error.isEmpty()) {
                    d.finish(!parsed->error.isEmpty() ? parsed->error
                             : error.isEmpty()
                                 ? QStringLiteral("Observation parsing was cancelled.")
                                 : error);
                    return;
                }
                const auto path = QDir(d.directory)
                                      .filePath(QStringLiteral("site-%1-page-%2.geojson")
                                                    .arg(d.itemIndex + 1)
                                                    .arg(d.pageIndex));
                if (!writeNew(path, bytes)) {
                    d.finish(QStringLiteral("The observation response could not be saved."));
                    return;
                }
                d.retained(path, QStringLiteral("raw observations"), url);
                for (const auto &feature : parsed->records) {
                    const auto properties = feature.value("properties").toObject();
                    const auto time = properties.value("time").toString();
                    const auto normalized = time.size() == 10
                                                ? time
                                                : QDateTime::fromString(time, Qt::ISODateWithMs)
                                                      .toUTC()
                                                      .toString(Qt::ISODateWithMs);
                    const auto identity = QString::fromUtf8(
                        QJsonDocument(QJsonArray{properties.value("time_series_id"),
                                                 properties.value("statistic_id"), normalized})
                            .toJson(QJsonDocument::Compact));
                    const auto hash = QCryptographicHash::hash(
                        QJsonDocument(feature).toJson(QJsonDocument::Compact),
                        QCryptographicHash::Sha256);
                    if (d.seenRecords.contains(identity)) {
                        if (d.seenRecords.value(identity) != hash) {
                            d.finish(QStringLiteral(
                                "Conflicting observations share a series, statistic and timestamp. Raw responses were retained; no layer was added."));
                            return;
                        }
                    } else {
                        d.seenRecords.insert(identity, hash);
                        d.records.append(feature);
                    }
                }
                if (d.records.size() > maxRecords) {
                    d.finish(QStringLiteral(
                        "Too many observations were returned. Choose a shorter period."));
                    return;
                }
                if (!parsed->next.isEmpty()) {
                    const auto next = url.resolved(parsed->next);
                    if (next.host() != QStringLiteral("api.waterdata.usgs.gov") ||
                        d.seenPages.contains(publicUrl(next))) {
                        d.finish(QStringLiteral(
                            "The service returned a repeated or unsupported observation page link."));
                        return;
                    }
                    d.seenPages.insert(publicUrl(next));
                    d.fetch(next);
                    return;
                }
                d.saveCsv();
            });
        if (!guard || !hostGuard || !guard->d->running || token != guard->d->generation) {
            if (hostGuard && submittedJob) submittedJob->cancel();
            return;
        }
        job = submittedJob;
        if (!job)
            finish(QStringLiteral("The observation validation job could not be started."));
    }
    /** @brief Prepare CSV in a cancellable data-only job before atomic GUI-thread publication. */
    void saveCsv()
    {
        const auto token = generation;
        const auto rows = records;
        const auto csv = QDir(directory).filePath(QStringLiteral("site-%1-%2-%3.csv")
                                                      .arg(itemIndex + 1)
                                                      .arg(request.parameterCode)
                                                      .arg(request.daily ? "daily" : "continuous"));
        const auto partial = csv + QStringLiteral(".partial");
        pendingPath = partial;
        auto failure = std::make_shared<QString>();
        QPointer<PayloadDownload> guard(q);
        const auto hostGuard = lifetime;
        progress.update(-1, QStringLiteral("Prepare observation CSV"));
        if (!guard || !hostGuard || !running || token != generation) return;
        auto *submittedJob = host->runJob(
            QStringLiteral("Prepare observation CSV"),
            [rows, partial, failure](Sdk::IJobContext &context) {
                const auto bytes = observationsCsv(rows, context);
                if (context.isCancelled())
                    return;
                QSaveFile file(partial);
                file.setDirectWriteFallback(false);
                if (QFileInfo::exists(partial) || !file.open(QIODevice::WriteOnly)) {
                    *failure = QStringLiteral("The observation CSV could not be opened.");
                    return;
                }
                for (qsizetype offset = 0; offset < bytes.size(); offset += 256 * 1024) {
                    if (context.isCancelled())
                        return;
                    const auto count = qMin(qsizetype(256 * 1024), bytes.size() - offset);
                    if (file.write(bytes.constData() + offset, count) != count) {
                        *failure = QStringLiteral("The observation CSV could not be written.");
                        return;
                    }
                }
                if (!context.isCancelled() && !file.commit())
                    *failure = QStringLiteral("The observation CSV could not be committed.");
                if (context.isCancelled())
                    QFile::remove(partial);
            },
            [guard, token, partial, csv, failure](bool ok, const QString &error) {
                if (!guard || !guard->d->running || token != guard->d->generation) {
                    QFile::remove(partial);
                    return;
                }
                auto &d = *guard->d;
                d.job = nullptr;
                if (!ok || !failure->isEmpty() || !QFileInfo::exists(partial)) {
                    QFile::remove(partial);
                    d.finish(!failure->isEmpty() ? *failure
                             : error.isEmpty()
                                 ? QStringLiteral("Observation CSV preparation was cancelled.")
                                 : error);
                    return;
                }
                if (QFileInfo::exists(csv) || !QFile::rename(partial, csv)) {
                    QFile::remove(partial);
                    d.finish(QStringLiteral("The observation CSV could not be saved."));
                    return;
                }
                d.retained(csv, QStringLiteral("observation CSV"));
                d.result.observationCount += d.records.size();
                if (!d.records.isEmpty()) {
                    const auto &item = d.request.items[d.itemIndex];
                    const QJsonObject columns{{"siteId", "monitoring_location_id"},
                                              {"parameter", "parameter_code"},
                                              {"sourceSeriesId", "time_series_id"},
                                              {"statistic", "statistic_id"},
                                              {"time", "time"},
                                              {"value", "value"},
                                              {"unit", "unit_of_measure"},
                                              {"approval", "approval_status"},
                                              {"qualifier", "qualifier"},
                                              {"observationId", "observation_id"},
                                              {"properties", "properties_json"},
                                              {"geometry", "geometry_json"},
                                              {"timeBasis", "time_basis"}};
                    d.observationSources.append(QJsonObject{
                        {"path", QFileInfo(csv).fileName()},
                        {"columns", columns},
                        {"defaults",
                         QJsonObject{{"provider", "USGS"},
                                     {"collection", d.request.daily ? "daily" : "continuous"},
                                     {"locationId", item.id}}},
                        {"metadata", item.metadata}});
                }
                d.pendingPath.clear();
                d.manifest(QStringLiteral("in_progress"));
                ++d.itemIndex;
                d.nextItem();
            });
        if (!guard || !hostGuard || !guard->d->running || token != guard->d->generation) {
            if (hostGuard && submittedJob) submittedJob->cancel();
            return;
        }
        job = submittedJob;
        if (!job)
            finish(QStringLiteral("The CSV preparation job could not be started."));
    }
    /** @brief Derive one area DEM; a worker owns its unpublished partial file and cleans it on cancellation. */
    void cropDem()
    {
        const auto token = generation;
        const auto tiles = tilePaths;
        const auto cutline = cutlinePath;
        const auto partial = QDir(directory).filePath(QStringLiteral("area-dem.partial.tif"));
        pendingPath = partial;
        const auto complete = QDir(directory).filePath(QStringLiteral("area-dem.tif"));
        auto error = std::make_shared<QString>();
        auto metadata = std::make_shared<QJsonObject>();
        QPointer<PayloadDownload> guard(q);
        const auto hostGuard = lifetime;
        // Transfer work is complete; the nested host job now owns measurable GDAL progress.
        progress.update(1., QStringLiteral("Downloads complete; cropping DEM to reviewed area"));
        if (!guard || !hostGuard || !running || token != generation) return;
        auto *submittedJob = host->runJob(
            QStringLiteral("Crop DEM to reviewed area"),
            [tiles, cutline, partial, error, metadata](Sdk::IJobContext &context) {
                *error = cropRaster(tiles, cutline, partial, context, metadata.get());
            },
            [guard, token, partial, complete, error, metadata](bool ok, const QString &detail) {
                if (!guard || !guard->d->running || token != guard->d->generation) {
                    QFile::remove(partial);
                    return;
                }
                auto &d = *guard->d;
                d.job = nullptr;
                if (!ok || !error->isEmpty() || !QFileInfo::exists(partial)) {
                    QFile::remove(partial);
                    d.finish(!error->isEmpty()  ? *error
                             : detail.isEmpty() ? QStringLiteral("DEM clipping was cancelled.")
                                                : detail);
                    return;
                }
                if (QFileInfo::exists(complete) || !QFile::rename(partial, complete)) {
                    QFile::remove(partial);
                    d.finish(QStringLiteral("The cropped DEM could not be published."));
                    return;
                }
                auto description = *metadata;
                description["processing"] =
                    "GDALWarp mosaic/crop to exact WGS84 cutline, Float32 NaN NoData, nearest resampling; source horizontal CRS and elevation units retained";
                d.retained(complete, QStringLiteral("area DEM"), {}, description);
                d.pendingPath.clear();
                d.result.rasterPaths.append(complete);
                d.finish();
            });
        if (!guard || !hostGuard || !guard->d->running || token != guard->d->generation) {
            if (hostGuard && submittedJob) submittedJob->cancel();
            return;
        }
        job = submittedJob;
        if (!job)
            finish(QStringLiteral("The DEM clipping job could not be started."));
    }
    /** @brief Validate a committed local tile in a worker before using it for an area DEM.
     * @param[in] url Actual response URL retained after successful tile validation.
     * @note Originals are retained; only cropDem() publishes an importable raster. */
    void checkRaster(const QUrl &url)
    {
        const auto token = generation;
        const auto path = outputPath;
        auto error = std::make_shared<QString>();
        auto metadata = std::make_shared<QJsonObject>();
        QPointer<PayloadDownload> guard(q);
        const auto hostGuard = lifetime;
        progress.update(-1, QStringLiteral("Validate downloaded GeoTIFF"));
        if (!guard || !hostGuard || !running || token != generation) return;
        auto *submittedJob = host->runJob(
            QStringLiteral("Validate downloaded GeoTIFF"),
            [path, error, metadata](Sdk::IJobContext &context) {
                *error = validateRaster(path, context, metadata.get());
            },
            [guard, token, path, error, metadata, url](bool ok, const QString &detail) {
                if (!guard || !guard->d->running || token != guard->d->generation) {
                    QFile::remove(path);
                    return;
                }
                auto &d = *guard->d;
                d.job = nullptr;
                if (!ok || !error->isEmpty()) {
                    d.finish(!error->isEmpty()  ? *error
                             : detail.isEmpty() ? QStringLiteral("Raster validation was cancelled.")
                                                : detail);
                    return;
                }
                d.retained(path, QStringLiteral("original GeoTIFF"), url, *metadata);
                d.tilePaths.append(path);
                d.outputPath.clear();
                d.manifest(QStringLiteral("in_progress"));
                ++d.itemIndex;
                d.nextItem();
            });
        if (!guard || !hostGuard || !guard->d->running || token != guard->d->generation) {
            if (hostGuard && submittedJob) submittedJob->cancel();
            return;
        }
        job = submittedJob;
        if (!job)
            finish(QStringLiteral("The raster validation job could not be started."));
    }
};

PayloadDownload::PayloadDownload(Sdk::ExtensionHost *host, QObject *parent)
    : QObject(parent), d(std::make_unique<Impl>(this, host))
{
    qRegisterMetaType<PayloadResult>();
    connect(this, &PayloadDownload::progress, this, [this](double fraction, const QString &stage) {
        // Byte counters are useful locally but would create one log entry per packet.
        d->progress.update(fraction, stage.startsWith(QStringLiteral("Received "))
                                         ? tr("Receiving download data") : stage);
    });
    if (d->lifetime)
        connect(d->lifetime, &QObject::destroyed, this,
                [this]() { d->finish(QStringLiteral("The plugin host closed."), true); });
}
PayloadDownload::~PayloadDownload()
{
    cancel();
}
bool PayloadDownload::start(const PayloadRequest &request, QString *error)
{
    const auto refuse = [&](const QString &message) {
        if (error)
            *error = message;
        d->progress.log("warning", QStringLiteral("Download refused: ") + message);
        return false;
    };
    if (isRunning() || !d->lifetime)
        return refuse(
            QStringLiteral("Another download is active or the plugin host is unavailable."));
    if (request.source != Source::Sites && request.source != Source::Terrain)
        return refuse(QStringLiteral("Choose a supported download source."));
    if (request.items.isEmpty() || request.items.size() > 100 ||
        request.destinationDir.trimmed().isEmpty())
        return refuse(QStringLiteral("Select 1–100 reviewed items and an output directory."));
    if (request.source == Source::Sites &&
        (!request.from.isValid() || !request.to.isValid() || request.from > request.to ||
         !QRegularExpression(QStringLiteral("^[0-9]{5}$")).match(request.parameterCode).hasMatch()))
        return refuse(QStringLiteral(
            "Choose a valid start/end period and a five-digit USGS parameter code."));
    if (request.source == Source::Terrain &&
        areaEnvelope(request.provenance.value("area").toObject()).isEmpty())
        return refuse(QStringLiteral(
            "Preview a valid viewport or boundary polygon before downloading a DEM."));
    QSet<QString> identities;
    for (const auto &item : request.items) {
        if (item.id.isEmpty() || identities.contains(item.id))
            return refuse(
                QStringLiteral("Selected source identities must be nonempty and unique."));
        identities.insert(item.id);
        if (request.source == Source::Terrain &&
            (!item.download.isValid() || item.download.isEmpty()))
            return refuse(QStringLiteral("A selected elevation product has no download URL."));
    }
    const auto directory =
        QDir(request.destinationDir)
            .absoluteFilePath(QStringLiteral("download-%1")
                                  .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
    if (!QDir().mkpath(directory))
        return refuse(QStringLiteral("Cannot create the download directory."));
    d->request = request;
    d->result = {};
    d->result.importId = QUuid::createUuid();
    d->observationSources = {};
    d->directory = directory;
    d->result.manifestPath = QDir(directory).filePath(QStringLiteral("manifest.json"));
    d->fileRecords = {};
    d->outputPath.clear();
    d->pendingPath.clear();
    d->tilePaths.clear();
    d->cutlinePath.clear();
    d->itemIndex = 0;
    if (request.source == Source::Terrain) {
        d->cutlinePath = QDir(directory).filePath(QStringLiteral("reviewed-area.geojson"));
        const QJsonObject cutline{
            {"type", "FeatureCollection"},
            {"features", QJsonArray{QJsonObject{{"type", "Feature"},
                                                {"properties", QJsonObject{}},
                                                {"geometry", request.provenance.value("area")}}}}};
        if (!writeNew(d->cutlinePath, QJsonDocument(cutline).toJson()))
            return refuse(QStringLiteral("Cannot save the reviewed DEM boundary."));
        d->retained(d->cutlinePath, QStringLiteral("exact WGS84 cutline"));
    }
    if (!d->manifest(QStringLiteral("in_progress")))
        return refuse(QStringLiteral("Cannot create the download manifest."));
    d->running = true;
    const auto token = ++d->generation;
    const QPointer<PayloadDownload> guard(this);
    d->progress.begin(request.source == Source::Sites ? tr("USGS observation download")
                                                     : tr("DEM download"));
    if (!guard || !d->running || token != d->generation) return true;
    QTimer::singleShot(0, this, [this, token]() {
        if (d->running && token == d->generation)
            d->nextItem();
    });
    return true;
}
void PayloadDownload::cancel()
{
    d->finish({}, true);
}
bool PayloadDownload::isRunning() const
{
    return d->running || d->finishing;
}
}
