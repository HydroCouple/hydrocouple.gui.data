/**
 * @file test_catalog.cpp
 * @brief Offline coverage for exact-area catalogue filtering and cancellation.
 * @details Synthetic coordinates isolate polygon holes, multiple parts and intersecting product
 * envelopes. Tests make no network requests and do not read a user's project or credentials.
 * @author Caleb Buahin <caleb.buahin@gmail.com>
 * @date 2026
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license GPL-3.0-or-later
 */
#include "Catalog.hpp"
#include <QJsonArray>
#include <QJsonDocument>
#include <QTest>
#include <QUrlQuery>
using namespace OpenSWMM::Data;

namespace
{
/** @brief Create two polygon parts, the first containing a hole. */
QJsonObject boundary()
{
    return QJsonDocument::fromJson(R"({"type":"MultiPolygon","coordinates":[
        [[[0,0],[10,0],[10,10],[0,10],[0,0]],[[3,3],[3,7],[7,7],[7,3],[3,3]]],
        [[[20,20],[22,20],[22,22],[20,22],[20,20]]]]})")
        .object();
}
/** @brief Build an OGC monitoring-location response feature. */
QJsonObject site(const QString &id, double x, double y)
{
    return {{"type", "Feature"},
            {"id", id},
            {"properties", QJsonObject{{"monitoring_location_name", id}}},
            {"geometry", QJsonObject{{"type", "Point"}, {"coordinates", QJsonArray{x, y}}}}};
}
/** @brief Build a TNM product record with an advertised coverage envelope. */
QJsonObject product(const QString &id, double x0, double y0, double x1, double y1)
{
    return {{"sourceId", id},
            {"title", id},
            {"downloadURL", "https://example.invalid/tile.tif"},
            {"boundingBox", QJsonObject{{"minX", x0}, {"minY", y0}, {"maxX", x1}, {"maxY", y1}}}};
}
}

/** @brief Fixture-free tests for area, provider format and record-identity contracts. */
class CatalogTest final : public QObject
{
    Q_OBJECT
private slots:
    void preservesHolesAndParts()
    {
        const auto area = boundary();
        const auto before = QJsonDocument(area).toJson();
        const QJsonObject response{
            {"features",
             QJsonArray{site("inside", 1, 1), site("hole", 5, 5), site("outside", 15, 15),
                        site("second-part", 21, 21), site("edge", 0, 5), site("inside", 2, 2)}}};
        QString error;
        const auto records = parseCatalogue(Source::Sites, response, area, {}, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(records.size(), 3);
        QCOMPARE(records[0].id, "inside");
        QCOMPARE(records[1].id, "second-part");
        QCOMPARE(records[2].id, "edge");
        QCOMPARE(QJsonDocument(area).toJson(), before);
        const auto exported = featureCollection(records)["features"].toArray();
        QCOMPARE(exported.size(), 3);
        QCOMPARE(exported[1].toObject()["id"].toString(), "second-part");
    }
    void terrainUsesExactArea()
    {
        const QJsonObject response{
            {"items",
             QJsonArray{product("inside", 1, 1, 2, 2), product("hole", 4, 4, 6, 6),
                        product("intersects", 6, 6, 8, 8), product("outside", 30, 30, 31, 31)}}};
        QString error;
        const auto records = parseCatalogue(Source::Terrain, response, boundary(), {}, &error);
        QVERIFY(error.isEmpty());
        QCOMPARE(records.size(), 2);
        QCOMPARE(records[1].id, "intersects");
        QCOMPARE(records[0].geometry["type"].toString(), "Polygon");
    }
    void requestDoesNotReplaceTheBoundary()
    {
        QString error;
        const auto area = boundary();
        const auto url = catalogueUrl(Source::Sites, area, {}, 9000, &error);
        QVERIFY(error.isEmpty());
        QCOMPARE(url.scheme(), "https");
        const QUrlQuery query(url);
        QCOMPARE(query.queryItemValue("bbox"), "0,0,22,22");
        QCOMPARE(query.queryItemValue("limit"), "5000");
        QVERIFY(area["coordinates"].toArray().first().toArray().size() == 2);
    }
    void cancellationPublishesNoPartialResults()
    {
        int checked = 0;
        QString error;
        const QJsonObject response{{"features", QJsonArray{site("one", 1, 1), site("two", 2, 2)}}};
        const auto records = parseCatalogue(
            Source::Sites, response, boundary(), [&checked]() { return ++checked > 1; }, &error);
        QVERIFY(records.isEmpty());
        QCOMPARE(checked, 2);
    }
    void malformedAndWorldSpanningAreasAreRefused()
    {
        QString error;
        QVERIFY(areaEnvelope({}, &error).isEmpty());
        QVERIFY(!error.isEmpty());
        const auto world =
            QJsonDocument::fromJson(
                R"({"type":"Polygon","coordinates":[[[-179,0],[179,0],[179,1],[-179,1],[-179,0]]]})")
                .object();
        QVERIFY(areaEnvelope(world, &error).isEmpty());
        error.clear();
        QVERIFY(parseCatalogue(Source::Sites, {}, boundary(), {}, &error).isEmpty());
        QVERIFY(!error.isEmpty());
    }
    void serviceErrorsAreNotAnEmptySuccessfulCatalogue()
    {
        QString error;
        const QJsonObject response{{"items", QJsonArray{}},
                                   {"errors", QJsonArray{"Unknown dataset"}}};
        QVERIFY(parseCatalogue(Source::Terrain, response, boundary(), {}, &error).isEmpty());
        QVERIFY(error.contains("Unknown dataset"));
    }
};
QTEST_GUILESS_MAIN(CatalogTest)
#include "test_catalog.moc"
