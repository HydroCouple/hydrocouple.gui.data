/**
 * @file Precipitation.hpp
 * @brief In-memory precipitation-frequency values and validated storm generation.
 * @details All values are owning, source-neutral copies. Parsers never execute service
 * responses or write files. Canonical precipitation depths are millimetres; storm
 * times are elapsed minutes and intensities are millimetres per hour.
 * @author Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license GPL-3.0-or-later
 */
#pragma once
#include <QByteArray>
#include <QDateTime>
#include <QJsonObject>
#include <QList>
#include <QPointF>
#include <QString>
#include <QUrl>
#include <QVariantMap>
#include <QVector>

namespace OpenSWMM::Data
{
/** @brief Selected estimate of a frequency-depth table. */
enum class FrequencyBound { Estimate, Lower, Upper };
/** @brief Exact public options of one live NOAA point query. */
struct FrequencyQuery {
    double latitude = 38.9; ///< WGS84 latitude in decimal degrees.
    double longitude = -77.; ///< WGS84 longitude in decimal degrees.
    QString units = "metric"; ///< Service units: metric or english.
    QString series = "pds"; ///< Statistical series: pds or ams.
    QString data = "depth"; ///< Service values: depth or intensity.
};
/** @brief Accepted frequency table; all matrices are duration-major depths in mm. */
struct FrequencyTable {
    FrequencyQuery query; ///< Options accepted for this response.
    QVector<double> durationsMinutes; ///< Strictly increasing duration axis.
    QVector<double> returnPeriods; ///< Strictly increasing source recurrence labels, years.
    QVector<QVector<double>> depthsMm; ///< Central estimate: duration, frequency.
    QVector<QVector<double>> lowerMm; ///< Source lower confidence bound, same dimensions.
    QVector<QVector<double>> upperMm; ///< Source upper confidence bound, same dimensions.
    QVariantMap metadata; ///< Bounded source provenance strings and scalar values.
    QDateTime retrievedAt; ///< UTC acceptance timestamp.
    QUrl url; ///< Public query URL, without user credentials.
};
/** @brief A normalized cumulative temporal pattern with its original identity. */
struct CumulativeCurve {
    QString id; ///< Stable product/method identity within the accepted response.
    QString title; ///< Human-readable pattern label.
    QString source; ///< Exact publication or live product citation.
    double durationMinutes = 0.; ///< Native duration; zero denotes user-normalized data.
    QVector<QPointF> points; ///< Increasing normalized time and nondecreasing depth, 0..1.
};
/** @brief A generator offered only when its numerical definition is implemented. */
struct StormMethod {
    QString id; ///< Stable method key.
    QString label; ///< User-facing method caption.
    QString description; ///< Domain and numerical interpretation.
    QString source; ///< Primary publication or equation citation.
    bool requiresFrequencyTable = false; ///< Needs accepted multi-duration depth estimates.
    bool requiresCurve = false; ///< Needs an explicitly selected cumulative curve.
    bool requires24Hours = false; ///< Native published table is restricted to 24 hours.
};
/** @brief Immutable inputs for one regular-interval synthetic storm. */
struct StormRequest {
    QString method = "scs-ii"; ///< Key returned by stormMethods().
    double depthMm = 25.4; ///< Selected total precipitation in millimetres.
    int durationMinutes = 1440; ///< Positive duration, divisible by intervalMinutes.
    int intervalMinutes = 5; ///< Positive regular recording interval.
    double peakRatio = .5; ///< Relative peak time for triangular/alternating block.
    FrequencyTable frequency; ///< Accepted source table, only for a frequency method.
    int frequencyIndex = 0; ///< Column in frequency.returnPeriods.
    FrequencyBound bound = FrequencyBound::Estimate; ///< Estimate/bound for source sampling.
    CumulativeCurve curve; ///< Selected source/custom cumulative pattern.
};
/** @brief One candidate, entirely in memory until explicitly applied or exported. */
struct StormSeries {
    QString method; ///< Generator key.
    QString label; ///< Human-readable method name.
    int intervalMinutes = 0; ///< Fixed recording interval.
    QVector<double> timesMinutes; ///< Interval boundaries, n+1 values beginning at zero.
    QVector<double> intensityMmPerHour; ///< Constant intensity over each of n intervals.
    QVector<double> depthsMm; ///< Depth over each interval.
    QVector<double> cumulativeMm; ///< Cumulative depth at boundaries, n+1 values.
    double totalDepthMm = 0.; ///< Accepted total, equal to the final cumulative sample.
    QJsonObject parameters; ///< Compact generator/version parameters; no raw NOAA response.
};
/** @brief Form a NOAA-documented point request after validating its public options.
 * @param[in] query WGS84 location and source options.
 * @param[out] error Optional validation detail.
 * @return HTTPS URL, or empty for invalid options. */
QUrl frequencyUrl(const FrequencyQuery &query, QString *error = nullptr);
/** @brief Decode bounded PFDS assignments without evaluating script expressions.
 * @param[in] response Raw bytes retained only by the caller's session.
 * @param[in] query Exact request used to obtain the bytes.
 * @param[out] table Accepted canonical table; unchanged on failure.
 * @param[out] error Optional safe format/coverage detail.
 * @return True after complete dimension, unit and finite-value validation. */
bool parseFrequencyResponse(const QByteArray &response, const FrequencyQuery &query,
                            FrequencyTable *table, QString *error = nullptr);
/** @brief Decode a NOAA cumulative temporal CSV into source-labelled normalized curves.
 * @param[in] response Bounded comma-delimited temporal product bytes.
 * @param[in] area Explicit temporal-area identity supplied by the request.
 * @param[in] durationMinutes Native product duration.
 * @param[out] curves Accepted pattern list; unchanged on failure.
 * @param[out] error Optional format/monotonicity detail.
 * @return True for a supported complete temporal product. */
bool parseTemporalResponse(const QByteArray &response, const QString &area, int durationMinutes,
                           QList<CumulativeCurve> *curves, QString *error = nullptr);
/** @brief List numerically qualified generator methods. @return Owning descriptors. */
QList<StormMethod> stormMethods();
/** @brief Sample one source-depth column without extrapolating outside its duration axis.
 * @param[in] table Accepted canonical frequency table.
 * @param[in] durationMinutes Target duration in minutes.
 * @param[in] frequencyIndex Column index.
 * @param[in] bound Selected source estimate/bound.
 * @param[out] depthMm Canonical interpolated depth, unchanged on failure.
 * @param[out] error Optional unsupported-duration/table detail.
 * @return True for an exact knot or log-duration/log-depth interpolation. */
bool sampleFrequencyDepth(const FrequencyTable &table, double durationMinutes, int frequencyIndex,
                          FrequencyBound bound, double *depthMm, QString *error = nullptr);
/** @brief Generate finite regular intervals with nonnegative depth and conserved total.
 * @param[in] request Accepted immutable method and rainfall inputs.
 * @param[out] storm Complete candidate, unchanged on failure.
 * @param[out] error Optional invalid-input or unsupported-method detail.
 * @return True after numerical validation; no model changes or files are made. */
bool generateStorm(const StormRequest &request, StormSeries *storm, QString *error = nullptr);
}
