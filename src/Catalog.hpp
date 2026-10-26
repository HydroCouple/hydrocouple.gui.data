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
#include <QJsonObject>
#include <QList>
#include <QRectF>
#include <QUrl>
#include <functional>

namespace OpenSWMM::Data
{
/** @brief Available catalogue adapters; payload processing is a subsequent work package. */
enum class Source { Sites, Terrain };
/** @brief Owning metadata for one selectable site or source product. */
struct Item {
    QString id;           ///< Stable source identity, retained with the reviewed area.
    QString title;        ///< User-facing station/product name.
    QJsonObject geometry; ///< WGS84 site point or catalogue coverage polygon.
    QJsonObject metadata; ///< Original properties, including provenance and source attributes.
    QUrl download;        ///< Advertised product URL; catalogue preview does not fetch it.
};
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
}
