/**
 * @file Catalog.cpp
 * @brief Bounded USGS/TNM decoding and exact polygon intersection for previews.
 * @details OGR geometries have local ownership; source geometries are never modified. Remote
 * bounding boxes only narrow the query. Holes and multipart structure govern the final results.
 * @author Caleb Buahin <caleb.buahin@gmail.com>
 * @date 2026
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license GPL-3.0-or-later
 */
#include "Catalog.hpp"
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>
#include <QUrlQuery>
#include <ogr_geometry.h>
#include <cmath>
#include <memory>

namespace OpenSWMM::Data
{
namespace
{
using Geometry = std::unique_ptr<OGRGeometry, decltype(&OGRGeometryFactory::destroyGeometry)>;
/** @brief Decode an owning OGR geometry from copied GeoJSON. */
Geometry geometry(const QJsonObject &value)
{
    const auto bytes = QJsonDocument(value).toJson(QJsonDocument::Compact);
    return {OGRGeometryFactory::createFromGeoJson(bytes.constData()),
            OGRGeometryFactory::destroyGeometry};
}
/** @brief Form a coverage rectangle in longitude/latitude order. */
QJsonObject rectangle(double x0, double y0, double x1, double y1)
{
    return {{"type", "Polygon"},
            {"coordinates",
             QJsonArray{QJsonArray{QJsonArray{x0, y0}, QJsonArray{x1, y0}, QJsonArray{x1, y1},
                                   QJsonArray{x0, y1}, QJsonArray{x0, y0}}}}};
}
}

QRectF areaEnvelope(const QJsonObject &area, QString *error)
{
    auto shape = geometry(area);
    if (!shape || shape->IsEmpty() || !shape->IsValid() ||
        (wkbFlatten(shape->getGeometryType()) != wkbPolygon &&
         wkbFlatten(shape->getGeometryType()) != wkbMultiPolygon)) {
        if (error)
            *error = "Choose a valid polygon download area.";
        return {};
    }
    OGREnvelope extent;
    shape->getEnvelope(&extent);
    if (!std::isfinite(extent.MinX) || !std::isfinite(extent.MaxX) || !std::isfinite(extent.MinY) ||
        !std::isfinite(extent.MaxY) || extent.MinX < -180 || extent.MaxX > 180 ||
        extent.MinY < -90 || extent.MaxY > 90 || extent.MaxX <= extent.MinX ||
        extent.MaxY <= extent.MinY || extent.MaxX - extent.MinX > 180) {
        if (error)
            *error = "Use a smaller WGS84 area that does not cross the date line.";
        return {};
    }
    return {extent.MinX, extent.MinY, extent.MaxX - extent.MinX, extent.MaxY - extent.MinY};
}

QUrl catalogueUrl(Source source, const QJsonObject &area, const QString &dataset, int limit,
                  QString *error)
{
    const auto extent = areaEnvelope(area, error);
    if (extent.isEmpty())
        return {};
    QUrl url(source == Source::Sites
                 ? "https://api.waterdata.usgs.gov/ogcapi/v1/collections/monitoring-locations/items"
                 : "https://tnmaccess.nationalmap.gov/api/v1/products");
    QUrlQuery query;
    query.addQueryItem("bbox", QString("%1,%2,%3,%4")
                                   .arg(extent.left(), 0, 'g', 15)
                                   .arg(extent.top(), 0, 'g', 15)
                                   .arg(extent.right(), 0, 'g', 15)
                                   .arg(extent.bottom(), 0, 'g', 15));
    if (source == Source::Sites) {
        query.addQueryItem("limit", QString::number(qBound(1, limit, 5000)));
        query.addQueryItem("f", "json");
    } else {
        query.addQueryItem("datasets", dataset);
        query.addQueryItem("max", QString::number(qBound(1, limit, 5000)));
        query.addQueryItem("outputFormat", "JSON");
    }
    url.setQuery(query);
    return url;
}

QList<Item> parseCatalogue(Source source, const QJsonObject &document, const QJsonObject &area,
                           std::function<bool()> cancelled, QString *error)
{
    QList<Item> result;
    if (areaEnvelope(area, error).isEmpty())
        return result;
    auto boundary = geometry(area);
    if (source == Source::Terrain && !document.value("errors").toArray().isEmpty()) {
        if (error) {
            const auto detail = document.value("errors").toArray().first();
            *error = QStringLiteral("The elevation catalogue reported an error: %1")
                         .arg(detail.isString() ? detail.toString().left(500)
                                                : QStringLiteral("request refused"));
        }
        return result;
    }
    const QString key = source == Source::Sites ? "features" : "items";
    if (!document.value(key).isArray()) {
        if (error)
            *error = "The service response has no supported catalogue records.";
        return result;
    }
    const auto rows = document.value(key).toArray();
    if (rows.size() > 5000) {
        if (error)
            *error =
                "This catalogue is too large for one preview. Narrow the area or use a smaller request.";
        return result;
    }
    QSet<QString> seen;
    for (const auto &entry : rows) {
        if (cancelled && cancelled())
            return {};
        const auto row = entry.toObject();
        Item item;
        if (source == Source::Sites) {
            item.metadata = row["properties"].toObject();
            item.id = row["id"].toVariant().toString();
            if (item.id.isEmpty())
                item.id = item.metadata["id"].toVariant().toString();
            item.title = item.metadata["monitoring_location_name"].toString();
            item.geometry = row["geometry"].toObject();
            if (item.geometry["type"] != "Point")
                continue;
        } else {
            item.metadata = row;
            item.id = row["sourceId"].toString();
            item.title = row["title"].toString();
            item.download = QUrl(row["downloadURL"].toString());
            if (item.id.isEmpty())
                item.id = item.download.toString();
            const auto box = row["boundingBox"].toObject();
            if (!box["minX"].isDouble() || !box["minY"].isDouble() || !box["maxX"].isDouble() ||
                !box["maxY"].isDouble())
                continue;
            item.geometry = rectangle(box["minX"].toDouble(), box["minY"].toDouble(),
                                      box["maxX"].toDouble(), box["maxY"].toDouble());
        }
        if (item.id.isEmpty() || seen.contains(item.id))
            continue;
        auto shape = geometry(item.geometry);
        if (!shape || shape->IsEmpty() || !shape->IsValid() || !boundary->Intersects(shape.get()))
            continue;
        seen.insert(item.id);
        result.append(std::move(item));
    }
    return result;
}

QJsonObject featureCollection(const QList<Item> &items)
{
    QJsonArray features;
    for (const auto &item : items) {
        auto properties = item.metadata;
        properties["catalogue_id"] = item.id;
        properties["catalogue_title"] = item.title;
        features.append(QJsonObject{{"type", "Feature"},
                                    {"id", item.id},
                                    {"geometry", item.geometry},
                                    {"properties", properties}});
    }
    return {{"type", "FeatureCollection"}, {"features", features}};
}
}
