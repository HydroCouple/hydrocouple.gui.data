/**
 * @file Catalog.hpp
 * @brief Provider-neutral catalogue records and exact preview-area filtering.
 * @details Geometry uses WGS84 longitude/latitude. These value-only operations can run in a host
 * job without model or widget access. Coverage envelopes supplied by TNM are labelled catalogue
 * footprints; they do not assert that every pixel in the envelope contains elevation data.
 * @author Caleb Buahin <caleb.buahin@gmail.com>
 * @date 2026
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license GPL-3.0-or-later
 */
#pragma once
#include <QDate>
#include <QJsonObject>
#include <QList>
#include <QRectF>
#include <QStringList>
#include <QUrl>
#include <functional>

namespace OpenSWMM::Data
{
/** @brief Available catalogue adapters for preview and explicit payload retrieval. */
enum class Source { Sites, Terrain };
/** @brief Owning metadata for one selectable site or source product. */
struct Item {
    QString id;           ///< Stable source identity, retained with the reviewed area.
    QString title;        ///< User-facing station/product name.
    QJsonObject geometry; ///< WGS84 site point or catalogue coverage polygon.
    QJsonObject metadata; ///< Original properties, including provenance and source attributes.
    QUrl download;        ///< Advertised product URL, used only by an explicit payload download.
};
/** @brief Copy exactly the properties written by featureCollection().
 * @param[in] item Original source record.
 * @return Source properties plus stable catalogue_id and catalogue_title. */
QJsonObject catalogueProperties(const Item &item);
/** @brief Read an exact, valid polygon's query envelope.
 * @param[in] area WGS84 Polygon/MultiPolygon geometry preserving holes and parts.
 * @param[out] error Optional geometry/range failure detail.
 * @return Envelope rectangle, or empty on invalid geometry; dateline-spanning areas are refused. */
QRectF areaEnvelope(const QJsonObject &area, QString *error = nullptr);
/** @brief Build an official service request for catalogue metadata only.
 * @param[in] source Sites or elevation products.
 * @param[in] area Frozen exact search polygon.
 * @param[in] dataset TNM dataset name for terrain; ignored for monitoring locations.
 * @param[in] limit Maximum records requested, clamped to 1..5000.
 * @param[out] error Optional invalid-area detail.
 * @return HTTPS request URL, or empty when area validation fails. */
QUrl catalogueUrl(Source source, const QJsonObject &area, const QString &dataset, int limit,
                  QString *error = nullptr);
/** @brief Decode catalogue metadata and retain only geometry intersecting the exact area.
 * @param[in] source Expected service adapter.
 * @param[in] document Parsed raw response; no script or expression is executed.
 * @param[in] area Frozen WGS84 polygon, not merely its envelope.
 * @param[in] cancelled Optional worker cancellation query, checked for every record.
 * @param[out] error Optional malformed-response/geometry detail.
 * @return Owning filtered records in source order, with duplicate identities removed. */
QList<Item> parseCatalogue(Source source, const QJsonObject &document, const QJsonObject &area,
                           std::function<bool()> cancelled = {}, QString *error = nullptr);
/** @brief Serialize records for a disposable preview or explicit GIS export.
 * @param[in] items Owning site/product metadata in desired display order.
 * @return WGS84 GeoJSON FeatureCollection retaining stable IDs and original properties. */
QJsonObject featureCollection(const QList<Item> &items);
/** @brief Union of the published USGS records of several locations for one measurement. */
struct RecordPeriod {
    QDate begin;       ///< Earliest first UTC date among matching records; invalid with none.
    QDate end;         ///< Latest last UTC date among matching records.
    QStringList sites; ///< Sorted location IDs that have a matching record.
};
/** @brief Build an official request for the published record periods of USGS locations.
 * @param[in] siteIds Monitoring-location IDs; callers send at most 50 per request.
 * @param[in] parameterCode Five-digit USGS parameter code.
 * @param[in] daily Daily values (`Daily`) rather than continuous measurements (`Points`).
 * @return HTTPS time-series-metadata query; it requests metadata only. */
QUrl recordPeriodUrl(const QStringList &siteIds, const QString &parameterCode, bool daily);
/** @brief Merge one time-series-metadata page into a union of record periods.
 * @param[in] document Parsed response page.
 * @param[in] parameterCode Only rows for this parameter count.
 * @param[in] daily Only `Daily` rows count when true, only `Points` rows otherwise.
 * @param[in,out] period Accumulated union; rows without a valid begin and end are skipped.
 * @param[out] error Optional malformed-response detail.
 * @return False when the page is not a feature collection; @p period is then unchanged. */
bool mergeRecordPeriod(const QJsonObject &document, const QString &parameterCode, bool daily,
                       RecordPeriod *period, QString *error = nullptr);
}
