/**
 * @file PayloadDownload.hpp
 * @brief Cancellable observation and GeoTIFF downloads through the public host network policy.
 * @details Every accepted run owns a new reviewable directory. Observation timestamps, units,
 * statistics and qualifiers remain source values; downloading never edits model forcing.
 * @author Caleb Buahin <caleb.buahin@gmail.com>
 * @date 2026
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license GPL-3.0-or-later
 */
#pragma once
#include "Catalog.hpp"
#include <sdk/ExtensionHost.hpp>
#include <QDate>
#include <QObject>
#include <QStringList>
#include <QUuid>
#include <memory>

namespace OpenSWMM::Data
{
/** @brief Owning, immutable inputs copied when a download starts. */
struct PayloadRequest {
    Source source = Source::Sites; ///< Observations for sites, original GeoTIFFs for terrain.
    QList<Item> items;             ///< Reviewed selected source records, at most 100 per run.
    QDate from;        ///< Inclusive first UTC calendar date; required for observations.
    QDate to;          ///< Inclusive last UTC calendar date; required for observations.
    bool daily = true; ///< Daily values or continuous observations.
    QString parameterCode = "00060"; ///< Exactly five ASCII digits, preserving leading zeroes.
    QString destinationDir;          ///< User-selected parent of the new unique run directory.
    QJsonObject provenance; ///< Frozen reviewed area/query/session metadata, without secrets.
};

/** @brief Terminal owning result; completed files remain available after partial failure/cancellation. */
struct PayloadResult {
    QStringList paths; ///< Complete CSV/GeoTIFF/raw-page files; excludes uncommitted files.
    QStringList
        rasterPaths; ///< Only successfully validated DEMs cropped/masked to the reviewed area.
    QString
        descriptorPath; ///< Complete source-neutral import JSON; empty for empty/failed/cancelled runs.
    QUuid importId;     ///< Immutable idempotence/dataset UUID for this completed collection.
    QString manifestPath;   ///< JSON manifest recording complete, failed or cancelled outcome.
    QString error;          ///< Plain failure detail, empty on success or explicit cancellation.
    bool cancelled = false; ///< Explicit user/host cancellation stopped this run.
    bool partial = false;   ///< A failed/cancelled run retained one or more complete files.
    qint64 observationCount =
        0; ///< Deduplicated source rows in saved CSVs; zero includes an empty requested period.
};

/** @brief Serial GUI-thread network controller with data-only host jobs for payload validation. */
class PayloadDownload final : public QObject
{
    Q_OBJECT
public:
    /** @brief Borrow a host that outlives this controller; parent owns the QObject when supplied.
     * @param[in] host Initialized GUI-thread extension host; unavailable hosts cause start() to refuse.
     * @param[in] parent Optional QObject owner; otherwise the caller owns the controller. */
    explicit PayloadDownload(Sdk::ExtensionHost *host, QObject *parent = nullptr);
    /** @brief Abort pending IO/jobs, remove uncommitted output and finalize the manifest. */
    ~PayloadDownload() override;
    /** @brief Copy a valid request and schedule its first transfer without overwriting existing assets.
     * @param[in] request Reviewed selection and requested output/period.
     * @param[out] error Immediate validation/storage failure detail.
     * @return False without a completion signal when refused; otherwise exactly one asynchronous finished signal follows.
     * @note UTC end date is inclusive. Serial requests follow policy-checked HTTPS redirects and
     * bounded pagination. Terrain requests require provenance.area as a valid WGS84 polygon;
     * selected original tiles are retained and a cropped/masked Float32 GeoTIFF is derived from it. */
    bool start(const PayloadRequest &request, QString *error = nullptr);
    /** @brief Attribute the next transfer and its worker stages to an existing tool run.
     * @param[in] id Parent accepted by this host; the caller settles it after import.
     * @note Call while idle, before start(). A null UUID uses legacy progress. */
    void setToolRun(const QUuid &id);
    /** @brief Request immediate transfer abort and cooperative worker cancellation; completed files are retained. */
    void cancel();
    /** @return True during a transfer, validation job or pending terminal signal delivery. */
    bool isRunning() const;

signals:
    /** @brief Report bounded overall progress and a short plain-text stage description.
     * @param[in] fraction Overall completion estimate from zero to one; paging may make it nonmonotonic.
     * @param[in] message Current plain-text transfer/processing stage. */
    void progress(double fraction, const QString &message);
    /** @brief Publish copied completed-file paths and the final manifest exactly once per accepted run.
     * @param[in] result Owned terminal snapshot, including completed files retained by a partial run. */
    void finished(const OpenSWMM::Data::PayloadResult &result);

private:
    class Impl;
    std::unique_ptr<Impl> d; ///< Sole controller state; workers retain copied values only.
};
}
Q_DECLARE_METATYPE(OpenSWMM::Data::PayloadResult)
