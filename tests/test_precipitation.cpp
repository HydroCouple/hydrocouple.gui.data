/**
 * @file test_precipitation.cpp
 * @brief Deterministic safe NOAA parsing and conservative rainfall checks.
 * @details Synthetic literals require no network, disk output or user models.
 * @author Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license GPL-3.0-or-later
 */
#include "Precipitation.hpp"
#include <QTest>
#include <QRegularExpression>
#include <QUrlQuery>
#include <algorithm>
#include <cmath>
#include <numeric>
using namespace OpenSWMM::Data;
namespace
{
/** @brief Build one duration-major literal matrix with quoted numeric cells. */
QByteArray matrixResponse(double offset = 0.)
{
    QByteArray result = "[";
    for (int row = 0; row < 19; ++row) {
        if (row) result += ',';
        result += '[';
        for (int column = 0; column < 10; ++column) {
            if (column) result += ',';
            result += '\'';
            result += QByteArray::number((row + 1) * 10. + column + offset);
            result += '\'';
        }
        result += ']';
    }
    return result + ']';
}
/** @brief Build consistent estimate/bound arrays and literal provenance. */
QByteArray response(const QByteArray &unit = "metric", const QByteArray &data = "depth")
{
    return "result='values';unit='" + unit + "';ser='pds';datatype='" + data +
           "';volume='2';version='3';region='Ohio River Basin';reg='orb';quantiles=" +
           matrixResponse() + ";lower=" + matrixResponse(-1.) + ";upper=" + matrixResponse(1.) + ';';
}
/** @brief Build a compact internally consistent multi-duration test table. */
FrequencyTable frequency()
{
    FrequencyTable result;
    result.durationsMinutes = {5, 10, 15, 30, 60};
    result.returnPeriods = {2, 10};
    result.depthsMm = {{5, 10}, {8, 16}, {10, 20}, {15, 30}, {20, 40}};
    result.lowerMm = result.depthsMm;
    result.upperMm = result.depthsMm;
    return result;
}
}
/** @brief Numerics and literal-envelope regression suite. */
class PrecipitationTest final : public QObject
{
    Q_OBJECT
private slots:
    void pointUrlUsesExplicitSourceOptions()
    {
        FrequencyQuery query;
        QString error;
        const auto url = frequencyUrl(query, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(url.scheme(), "https");
        QCOMPARE(url.host(), "hdsc.nws.noaa.gov");
        const QUrlQuery parameters(url);
        QCOMPARE(parameters.queryItemValue("units"), "metric");
        QCOMPARE(parameters.queryItemValue("series"), "pds");
        QCOMPARE(parameters.queryItemValue("data"), "depth");
        QCOMPARE(parameters.queryItemValue("lat").toDouble(), 38.9);
        query.latitude = 91.;
        QVERIFY(frequencyUrl(query, &error).isEmpty());
        QVERIFY(!error.isEmpty());
    }
    void parsesLiteralStringsAndRetainsSourceBounds()
    {
        FrequencyTable table;
        QString error;
        QVERIFY2(parseFrequencyResponse(response(), {}, &table, &error), qPrintable(error));
        QCOMPARE(table.durationsMinutes.size(), 19);
        QCOMPARE(table.returnPeriods.size(), 10);
        QCOMPARE(table.durationsMinutes.first(), 5.);
        QCOMPARE(table.durationsMinutes.last(), 86400.);
        QCOMPARE(table.depthsMm[0][0], 10.);
        QCOMPARE(table.lowerMm[0][0], 9.);
        QCOMPARE(table.upperMm[0][0], 11.);
        QCOMPARE(table.metadata["volume"].toString(), "2");
        QVERIFY(table.retrievedAt.isValid());
        QVERIFY(!table.metadata.contains("quantiles"));
        QVERIFY(!table.metadata.contains("lower"));
    }
    void convertsEnglishIntensityToCanonicalDepth()
    {
        FrequencyQuery query;
        query.units = "english";
        query.data = "intensity";
        FrequencyTable table;
        QString error;
        QVERIFY2(parseFrequencyResponse(response("english", "intensity"), query, &table, &error), qPrintable(error));
        QVERIFY(std::abs(table.depthsMm[0][0] - 10. * 25.4 * 5. / 60.) < 1e-10);
        QVERIFY(std::abs(table.depthsMm[4][2] - 52. * 25.4) < 1e-10);
    }
    void refusesExecutableAndMalformedEnvelopesAtomically()
    {
        FrequencyTable table;
        table.metadata["sentinel"] = "unchanged";
        QString error;
        const QList<QByteArray> invalid{
            "result=__import__('os').system('bad')", "result='values';quantiles=NaN;",
            "result='values';result='values';", response() + "launch();", QByteArray(512 * 1024 + 1, 'x'),
            response().replace("unit='metric'", "unit='english'"),
            response().replace("quantiles=[[", "quantiles=[[[")};
        for (const auto &bytes : invalid) {
            QVERIFY(!parseFrequencyResponse(bytes, {}, &table, &error));
            QVERIFY(!error.isEmpty());
            QCOMPARE(table.metadata["sentinel"].toString(), "unchanged");
            QVERIFY(table.depthsMm.isEmpty());
        }
    }
    void temporalCsvRetainsQuartileAndPercentile()
    {
        const QByteArray bytes = "Tabulated Temporal Distribution Data,\r\n"
                                 "NOAA Atlas 14 synthetic parser test,\r\n"
                                 "This file contains all four quartiles and all cases,\r\n"
                                 "The first row of each table is the percent of duration (x-axis),\r\n"
                                 "The percent of total precipitation is in the labeled rows,\r\n"
                                 "First Quartile,\r\n"
                                 "percent of duration,0,25,50,75,100\r\n"
                                 "10%,0,65,80,95,100\r\n50%,0,45,70,90,100\r\n"
                                 "All Cases\r\npercent of duration,0,25,50,75,100\r\n"
                                 "50%,0,20,50,85,100\r\n";
        QList<CumulativeCurve> curves;
        QString error;
        QVERIFY2(parseTemporalResponse(bytes, "orb/general", 1440, &curves, &error), qPrintable(error));
        QCOMPARE(curves.size(), 3);
        QCOMPARE(curves.first().durationMinutes, 1440.);
        QVERIFY(curves.first().title.contains("First Quartile"));
        QVERIFY(curves.first().title.contains("10%"));
        QCOMPARE(curves.first().points[1], QPointF(.25, .65));
        auto invalid = bytes;
        invalid.replace("50%,0,45,70,90,100", "50%,0,45,40,90,100");
        const auto previous = curves.first().points;
        QVERIFY(!parseTemporalResponse(invalid, "orb/general", 1440, &curves, &error));
        QCOMPARE(curves.first().points, previous);
    }
    void durationSamplingUsesExactKnotsAndBoundedInterpolation()
    {
        const auto table = frequency();
        QString error;
        double value = -1.;
        QVERIFY(sampleFrequencyDepth(table, 10, 1, FrequencyBound::Estimate, &value, &error));
        QCOMPARE(value, 16.);
        QVERIFY(sampleFrequencyDepth(table, std::sqrt(5. * 10.), 0, FrequencyBound::Estimate, &value, &error));
        QVERIFY(std::abs(value - std::sqrt(5. * 8.)) < 1e-10);
        QVERIFY(!sampleFrequencyDepth(table, 1, 0, FrequencyBound::Estimate, &value, &error));
        QVERIFY(!sampleFrequencyDepth(table, 100, 0, FrequencyBound::Estimate, &value, &error));
    }
    void triangularIntegratesAnalyticalMassAndPeak()
    {
        StormRequest request;
        request.method = "triangular";
        request.durationMinutes = 60;
        request.intervalMinutes = 5;
        request.depthMm = 25.;
        request.peakRatio = .5;
        StormSeries series;
        QString error;
        QVERIFY2(generateStorm(request, &series, &error), qPrintable(error));
        QCOMPARE(series.depthsMm.size(), 12);
        QCOMPARE(series.timesMinutes.size(), 13);
        QVERIFY(std::abs(series.totalDepthMm - 25.) < 1e-10);
        QVERIFY(std::abs(series.cumulativeMm[6] - 12.5) < 1e-10);
        QVERIFY(std::abs(series.depthsMm[0] - series.depthsMm[11]) < 1e-10);
        QVERIFY(std::abs(std::accumulate(series.intensityMmPerHour.cbegin(), series.intensityMmPerHour.cend(), 0.) * 5. / 60. - 25.) < 1e-10);
    }
    void customCurvePreservesKnotsAndRejectsImplicitDurationScaling()
    {
        StormRequest request;
        request.method = "custom";
        request.depthMm = 40.;
        request.durationMinutes = 60;
        request.intervalMinutes = 15;
        request.curve = {"custom-a", "Test", "Synthetic table", 0., {{0,0},{.25,.1},{.5,.8},{1,1}}};
        StormSeries series;
        QString error;
        QVERIFY2(generateStorm(request, &series, &error), qPrintable(error));
        QCOMPARE(series.cumulativeMm[1], 4.);
        QCOMPARE(series.cumulativeMm[2], 32.);
        request.curve.durationMinutes = 1440;
        QVERIFY(!generateStorm(request, &series, &error));
        request.curve.durationMinutes = 0;
        request.curve.points[2].setY(.05);
        QVERIFY(!generateStorm(request, &series, &error));
    }
    void alternatingBlockUsesAndConservesSourceIncrements()
    {
        StormRequest request;
        request.method = "alternating-block";
        request.frequency = frequency();
        request.durationMinutes = 60;
        request.intervalMinutes = 5;
        request.depthMm = 20.;
        StormSeries series;
        QString error;
        QVERIFY2(generateStorm(request, &series, &error), qPrintable(error));
        const auto peak = std::max_element(series.depthsMm.cbegin(), series.depthsMm.cend());
        QCOMPARE(int(peak - series.depthsMm.cbegin()), 6);
        QCOMPARE(*peak, 5.);
        QVERIFY(std::abs(series.totalDepthMm - 20.) < 1e-10);
        request.intervalMinutes = 1;
        QVERIFY(!generateStorm(request, &series, &error));
        QVERIFY(error.contains("duration range"));
    }
    void generationRefusesInvalidOrUnqualifiedInputsAtomically()
    {
        StormSeries series;
        series.label = "untouched";
        StormRequest request;
        request.method = "triangular";
        QString error;
        request.intervalMinutes = 7;
        QVERIFY(!generateStorm(request, &series, &error));
        QCOMPARE(series.label, "untouched");
        request.intervalMinutes = 5;
        request.depthMm = -1.;
        QVERIFY(!generateStorm(request, &series, &error));
        request.depthMm = 10.;
        request.method = "unqualified-placeholder";
        QVERIFY(!generateStorm(request, &series, &error));
        QCOMPARE(series.label, "untouched");
    }
    void chicagoRecoversKnownIdfAndIntegratesWithoutPeakAliasing()
    {
        StormRequest request;
        request.method = "chicago";
        request.durationMinutes = 120;
        request.intervalMinutes = 5;
        request.peakRatio = .35;
        request.frequency.durationsMinutes = {5,10,15,30,60,120};
        request.frequency.returnPeriods = {10};
        for (double t : request.frequency.durationsMinutes)
            request.frequency.depthsMm.append(QVector<double>{900. * t / std::pow(t + 8., .7) / 60.});
        request.depthMm = request.frequency.depthsMm.last().first();
        StormSeries series;
        QString error;
        QVERIFY2(generateStorm(request, &series, &error), qPrintable(error));
        QVERIFY(std::abs(series.parameters["idfA"].toDouble() - 900.) < .001);
        QVERIFY(std::abs(series.parameters["idfBMinutes"].toDouble() - 8.) < .0001);
        QVERIFY(std::abs(series.parameters["idfC"].toDouble() - .7) < .000001);
        QVERIFY(series.parameters["idfMaxRelativeError"].toDouble() < .000001);
        QVERIFY(std::abs(series.totalDepthMm - request.depthMm) < 1e-10);
        QVERIFY(std::all_of(series.depthsMm.cbegin(), series.depthsMm.cend(), [](double value) { return value >= 0.; }));
        request.durationMinutes = 240;
        QVERIFY(!generateStorm(request, &series, &error));
        QVERIFY(error.contains("duration range"));
    }
    void publishedScsTypeIiAndIaMatchPrimaryTableKnots()
    {
        StormRequest request;
        request.method = "scs-ii";
        request.durationMinutes = 1440;
        request.intervalMinutes = 6;
        request.depthMm = 100.;
        StormSeries series;
        QString error;
        QVERIFY2(generateStorm(request, &series, &error), qPrintable(error));
        // WSDOT 2014 Table4C-4: cumulative fractions at0.1h source spacing.
        const QList<QPair<int,double>> typeIi{{20,.022},{60,.080},{115,.283},{117,.354},
                                             {118,.431},{119,.568},{120,.663},{180,.921},{240,1.}};
        for (const auto &knot : typeIi)
            QVERIFY(std::abs(series.cumulativeMm[knot.first] / 100. - knot.second) < 1e-4);
        QCOMPARE(series.cumulativeMm.size(), 241);
        request.method = "scs-ia";
        QVERIFY2(generateStorm(request, &series, &error), qPrintable(error));
        // WSDOT 2014 Table4C-3: early maritime peak, not a TypeII rescaling.
        const QList<QPair<int,double>> typeIa{{20,.050},{60,.206},{75,.310},{80,.425},
                                             {100,.577},{120,.664},{180,.860},{240,1.}};
        for (const auto &knot : typeIa)
            QVERIFY(std::abs(series.cumulativeMm[knot.first] / 100. - knot.second) < 1e-4);
        request.durationMinutes = 720;
        QVERIFY(!generateStorm(request, &series, &error));
        QVERIFY(error.contains("24-hour"));
    }
    void publishedScsTypeIAndIiiKeepSourceResolution()
    {
        StormRequest request;
        request.method = "scs-i";
        request.depthMm = 100.;
        request.durationMinutes = 1440;
        request.intervalMinutes = 30;
        StormSeries series;
        QString error;
        QVERIFY2(generateStorm(request, &series, &error), qPrintable(error));
        // FHWA HDS2 Table6.5 half-hour ordinates, not inferred 0.1h data.
        QVERIFY(std::abs(series.cumulativeMm[4] - 3.5) < 1e-8);
        QVERIFY(std::abs(series.cumulativeMm[20] - 51.5) < 1e-8);
        QVERIFY(std::abs(series.cumulativeMm[24] - 68.2) < 1e-8);
        request.method = "scs-iii";
        QVERIFY2(generateStorm(request, &series, &error), qPrintable(error));
        QVERIFY(std::abs(series.cumulativeMm[20] - 18.9) < 1e-8);
        QVERIFY(std::abs(series.cumulativeMm[24] - 50.) < 1e-8);
        QVERIFY(std::abs(series.cumulativeMm[26] - 75.) < 1e-8);
    }
    void huffPointMedianKeepsExplicitQuartilePattern()
    {
        StormRequest request;
        request.method = "huff-first";
        request.depthMm = 100.;
        request.durationMinutes = 100;
        request.intervalMinutes = 5;
        StormSeries series;
        QString error;
        QVERIFY2(generateStorm(request, &series, &error), qPrintable(error));
        QCOMPARE(series.cumulativeMm[5], 60.);
        QCOMPARE(series.cumulativeMm[10], 82.);
        request.method = "huff-fourth";
        QVERIFY2(generateStorm(request, &series, &error), qPrintable(error));
        QVERIFY(std::abs(series.cumulativeMm[5] - 13.) < 1e-8);
        QVERIFY(std::abs(series.cumulativeMm[10] - 28.) < 1e-8);
        QVERIFY(series.label.contains("Midwest point median"));
    }
    void nrcsMse3KeepsQualifiedRegionalTableAndPeak()
    {
        StormRequest request;
        request.method = "nrcs-mse3";
        request.depthMm = 100.;
        request.intervalMinutes = 6;
        request.durationMinutes = 1440;
        StormSeries series;
        QString error;
        QVERIFY2(generateStorm(request, &series, &error), qPrintable(error));
        // SEMCOG 2020 Table5, native0.1-hour spacing and explicit regional family.
        QVERIFY(std::abs(series.cumulativeMm[120] / 100. - .462890) < 1e-6);
        QVERIFY(std::abs(series.cumulativeMm[121] / 100. - .627550) < 1e-6);
        QVERIFY(std::abs(series.totalDepthMm - 100.) < 1e-8);
        QVERIFY(series.label.contains("Midwest/Southeast"));
        QVERIFY(series.parameters["source"].toString().contains("Table 5"));
    }
    void canonicalUnitsAndGeneratedIntensitiesRejectOverflowAtomically()
    {
        FrequencyQuery query;
        query.units = "english";
        auto bytes = QString::fromLatin1(response("english"));
        bytes.replace(QRegularExpression("'[0-9]+'"), "'1e308'");
        FrequencyTable table;
        table.metadata["sentinel"] = "unchanged";
        QString error;
        QVERIFY(!parseFrequencyResponse(bytes.toLatin1(), query, &table, &error));
        QCOMPARE(table.metadata["sentinel"].toString(), "unchanged");
        QVERIFY(table.depthsMm.isEmpty());

        StormRequest request;
        request.method = "triangular";
        request.depthMm = 1e308;
        request.durationMinutes = 60;
        request.intervalMinutes = 1;
        StormSeries series;
        series.label = "unchanged";
        QVERIFY(!generateStorm(request, &series, &error));
        QCOMPARE(series.label, "unchanged");

        // Avoid an unnecessary depth*60 intermediate overflow when the actual
        // interval-average intensity is representable.
        request.durationMinutes = 120;
        request.intervalMinutes = 60;
        QVERIFY2(generateStorm(request, &series, &error), qPrintable(error));
        QVERIFY(std::all_of(series.intensityMmPerHour.cbegin(), series.intensityMmPerHour.cend(),
                            [](double value) { return std::isfinite(value); }));
    }
};
QTEST_GUILESS_MAIN(PrecipitationTest)
#include "test_precipitation.moc"
