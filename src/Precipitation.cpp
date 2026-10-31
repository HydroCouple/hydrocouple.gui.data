/**
 * @file Precipitation.cpp
 * @brief Safe live NOAA response decoding and conservative synthetic rainfall numerics.
 * @details The PFDS 19-by-10 point-product schema is qualified against NOAA's
 * public point-query client. Source numbers are converted to millimetre depths.
 * All parsing and generation is in memory; no file or network operations occur here.
 * @author Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license GPL-3.0-or-later
 */
#include "Precipitation.hpp"
#include "StandardStormCurves.hpp"
#include <QRegularExpression>
#include <QSet>
#include <QUrlQuery>
#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>

namespace OpenSWMM::Data
{
namespace
{
/** @brief Bound service parsing and chart/generator allocations. */
constexpr qsizetype MaxResponseBytes = 512 * 1024;
constexpr int MaxIntervals = 20000;
/** @brief Fixed public PFDS point-product duration axis, in minutes. */
const QVector<double> PointDurations{5, 10, 15, 30, 60, 120, 180, 360, 720, 1440,
                                     2880, 4320, 5760, 10080, 14400, 28800, 43200, 64800, 86400};
/** @brief Source recurrence labels, not an inferred annual probability conversion. */
const QVector<double> PointPeriods{1, 2, 5, 10, 25, 50, 100, 200, 500, 1000};
/** @brief Return one literal failure without modifying output values. */
bool fail(QString *error, const QString &message)
{
    if (error)
        *error = message;
    return false;
}
/** @brief Decode only literals from the PFDS assignment envelope, never code. */
class LiteralReader
{
public:
    explicit LiteralReader(QString text) : mText(std::move(text)) {}
    bool assignments(QVariantMap *out)
    {
        while (skip(), mAt < mText.size()) {
            if (out->size() >= 64)
                return false;
            const auto start = mAt;
            if (!mText[mAt].isLetter() && mText[mAt] != '_')
                return false;
            while (mAt < mText.size() && (mText[mAt].isLetterOrNumber() || mText[mAt] == '_'))
                ++mAt;
            const auto key = mText.mid(start, mAt - start);
            skip();
            if (key.size() > 64 || mAt >= mText.size() || mText[mAt++] != '=' || out->contains(key))
                return false;
            QVariant value;
            if (!literal(&value, 0))
                return false;
            out->insert(key, value);
            skip();
            if (mAt < mText.size() && mText[mAt] == ';')
                ++mAt;
            else if (mAt < mText.size() && !mText[mAt].isLetter() && mText[mAt] != '_')
                return false;
        }
        return !out->isEmpty();
    }
private:
    void skip()
    {
        while (mAt < mText.size() && mText[mAt].isSpace())
            ++mAt;
    }
    bool literal(QVariant *out, int depth)
    {
        skip();
        if (++mTokens > 10000 || depth > 2 || mAt >= mText.size())
            return false;
        const auto ch = mText[mAt];
        if (ch == '\'' || ch == '"') {
            ++mAt;
            QString value;
            bool closed = false;
            while (mAt < mText.size() && value.size() <= 8192) {
                auto c = mText[mAt++];
                if (c == ch) { closed = true; break; }
                if (c == '\\') {
                    if (mAt >= mText.size()) return false;
                    c = mText[mAt++];
                    if (c == 'n') c = '\n';
                    else if (c == 'r') c = '\r';
                    else if (c == 't') c = '\t';
                    else if (c != '\\' && c != '\'' && c != '"') return false;
                }
                if (c.isNull()) return false;
                value += c;
            }
            if (!closed || value.size() > 8192) return false;
            *out = value;
            return true;
        }
        if (ch == '[') {
            ++mAt;
            QVariantList values;
            skip();
            if (mAt < mText.size() && mText[mAt] == ']') { ++mAt; *out = values; return true; }
            while (mAt < mText.size() && values.size() < 1024) {
                QVariant value;
                if (!literal(&value, depth + 1)) return false;
                values.append(value);
                skip();
                if (mAt >= mText.size()) return false;
                if (mText[mAt] == ']') { ++mAt; *out = values; return true; }
                if (mText[mAt++] != ',') return false;
            }
            return false;
        }
        const auto start = mAt;
        while (mAt < mText.size() && (mText[mAt].isDigit() || QStringView(u"+-.eE").contains(mText[mAt])))
            ++mAt;
        const auto token = mText.mid(start, mAt - start);
        static const QRegularExpression number(QStringLiteral("^[+-]?(?:[0-9]+(?:\\.[0-9]*)?|\\.[0-9]+)(?:[eE][+-]?[0-9]+)?$"));
        bool ok = false;
        const auto value = token.toDouble(&ok);
        if (!ok || !std::isfinite(value) || !number.match(token).hasMatch())
            return false;
        *out = value;
        return true;
    }
    QString mText;
    qsizetype mAt = 0;
    int mTokens = 0;
};
/** @brief Validate a literal NOAA matrix and canonicalize its physical units. */
bool matrix(const QVariant &value, const FrequencyQuery &query,
             QVector<QVector<double>> *out)
{
    if (value.metaType().id() != QMetaType::QVariantList)
        return false;
    const auto rows = value.toList();
    if (rows.size() != PointDurations.size())
        return false;
    QVector<QVector<double>> result;
    for (qsizetype r = 0; r < rows.size(); ++r) {
        if (rows[r].metaType().id() != QMetaType::QVariantList)
            return false;
        const auto columns = rows[r].toList();
        if (columns.size() != PointPeriods.size())
            return false;
        QVector<double> row;
        for (const auto &column : columns) {
            if (column.metaType().id() != QMetaType::QString && column.metaType().id() != QMetaType::Double)
                return false;
            bool ok = false;
            double number = column.toDouble(&ok);
            if (!ok || !std::isfinite(number) || number < 0.)
                return false;
            const auto unitFactor = query.units == "english" ? 25.4 : 1.;
            const auto durationFactor = query.data == "intensity" ? PointDurations[r] / 60. : 1.;
            number *= unitFactor * durationFactor;
            if (!std::isfinite(number))
                return false;
            row.append(number);
        }
        result.append(row);
    }
    *out = std::move(result);
    return true;
}
/** @brief Ensure normalized cumulative coordinates form a complete mass curve. */
bool validCurve(const CumulativeCurve &curve)
{
    if (curve.points.size() < 2 || curve.points.size() > 20001 ||
        std::abs(curve.points.first().x()) > 1e-9 || std::abs(curve.points.first().y()) > 1e-9 ||
        std::abs(curve.points.last().x() - 1.) > 1e-9 ||
        std::abs(curve.points.last().y() - 1.) > 1e-9)
        return false;
    double previousX = -1., previousY = -1.;
    for (const auto &point : curve.points) {
        if (!std::isfinite(point.x()) || !std::isfinite(point.y()) ||
            point.x() < 0. || point.x() > 1. || point.y() < 0. || point.y() > 1. ||
            point.x() <= previousX || point.y() < previousY)
            return false;
        previousX = point.x(); previousY = point.y();
    }
    return true;
}
/** @brief Piecewise-linear interpolation preserving every tabulated cumulative knot. */
double cumulativeAt(const CumulativeCurve &curve, double time)
{
    if (time <= 0.) return 0.;
    if (time >= 1.) return 1.;
    const auto next = std::upper_bound(curve.points.cbegin(), curve.points.cend(), time,
                                      [](double x, const QPointF &point) { return x < point.x(); });
    const auto previous = next - 1;
    return previous->y() + (next->y() - previous->y()) *
           (time - previous->x()) / (next->x() - previous->x());
}
/** @brief Construct a source-qualified standard cumulative curve at its published knots. */
CumulativeCurve standardCurve(const QString &method)
{
    CumulativeCurve curve;
    curve.id = method;
    if (method == "nrcs-mse3") {
        curve.title = QStringLiteral("NRCS MSE3 (Midwest/Southeast)");
        curve.source = QStringLiteral("SEMCOG Southeast Michigan Rainfall (2020), Table 5, NRCS MSE3");
        curve.durationMinutes = 1440.;
        for (int i = 0; i < int(StormCurves::NrcsMse3.size()); ++i)
            curve.points.append({double(i) / 240., StormCurves::NrcsMse3[i] / 1000000.});
        return curve;
    }
    if (method.startsWith("huff-")) {
        const auto index = method == "huff-first" ? 0 : method == "huff-second" ? 1
                           : method == "huff-third" ? 2 : 3;
        curve.title = QStringLiteral("Huff point median, quartile %1 (Midwest)").arg(index + 1);
        curve.source = QStringLiteral("Huff and Angel (1992) Bulletin 71, point median; Columbia Appendix F Table 2.2.2.4");
        const auto &values = StormCurves::HuffPointMedian[index];
        for (int i = 0; i < int(values.size()); ++i)
            curve.points.append({double(i) / 20., values[i] / 100.});
        return curve;
    }
    if (method == "scs-i" || method == "scs-iii") {
        curve.title = method == "scs-i" ? QStringLiteral("SCS Type I") : QStringLiteral("SCS Type III");
        curve.source = QStringLiteral("FHWA HDS-2 (2002), Table 6.5, published half-hour ordinates");
        curve.durationMinutes = 1440.;
        const auto &values = method == "scs-i" ? StormCurves::ScsI : StormCurves::ScsIii;
        for (int i = 0; i < int(values.size()); ++i)
            curve.points.append({double(i) / 48., values[i] / 1000.});
        return curve;
    }
    curve.title = method == "scs-ia" ? QStringLiteral("SCS Type IA") : QStringLiteral("SCS Type II");
    curve.source = QStringLiteral("WSDOT Highway Runoff Manual M31-16.04 (2014), Appendix 4C, Table ") +
                   (method == "scs-ia" ? QStringLiteral("4C-3") : QStringLiteral("4C-4"));
    curve.durationMinutes = 1440.;
    const auto &values = method == "scs-ia" ? StormCurves::ScsIa : StormCurves::ScsIi;
    for (int i = 0; i < int(values.size()); ++i)
        curve.points.append({double(i) / 240., values[i] / 1000.});
    return curve;
}
/** @brief One positive-shift IDF fit, following Chen/FHWA 1975 equations 9, 21 and 22. */
struct IdfFit {
    double a = 0.;
    double b = 0.;
    double c = 0.;
    double logRms = 0.;
    double maxRelativeError = 0.;
    double objective = std::numeric_limits<double>::infinity();
};
/** @brief Fit log intensity against log(duration+b), optimizing the positive shift. */
bool chicagoFit(const StormRequest &request, IdfFit *fit, QString *error)
{
    const auto &table = request.frequency;
    if (table.durationsMinutes.isEmpty() || request.intervalMinutes < table.durationsMinutes.first())
        return fail(error, QStringLiteral("Chicago requires an interval at least as long as the shortest returned duration."));
    if (request.durationMinutes < table.durationsMinutes.first() ||
        request.durationMinutes > table.durationsMinutes.last())
        return fail(error, QStringLiteral("The requested Chicago storm is outside NOAA's returned duration range."));
    QVector<double> times, intensities;
    for (double time : table.durationsMinutes) {
        if (time > request.durationMinutes) break;
        double depth = 0.;
        if (!sampleFrequencyDepth(table, time, request.frequencyIndex, request.bound, &depth, error))
            return false;
        if (depth <= 0.)
            return fail(error, QStringLiteral("Chicago fitting requires positive source depths."));
        times.append(time);
        intensities.append(depth * 60. / time);
    }
    if (times.size() < 4)
        return fail(error, QStringLiteral("Chicago fitting requires at least four returned durations within the storm."));
    const auto evaluate = [&](double logB) {
        IdfFit result;
        result.b = std::exp(logB);
        double meanX = 0., meanY = 0.;
        for (int i = 0; i < times.size(); ++i) {
            meanX += std::log(times[i] + result.b);
            meanY += std::log(intensities[i]);
        }
        meanX /= times.size(); meanY /= times.size();
        double variance = 0., covariance = 0.;
        for (int i = 0; i < times.size(); ++i) {
            const auto x = std::log(times[i] + result.b) - meanX;
            variance += x * x;
            covariance += x * (std::log(intensities[i]) - meanY);
        }
        if (variance <= 0.) return result;
        result.c = -covariance / variance;
        result.a = std::exp(meanY + result.c * meanX);
        if (!std::isfinite(result.a) || result.a <= 0. || result.c <= 0. || result.c >= 1.)
            return result;
        result.objective = 0.;
        for (int i = 0; i < times.size(); ++i) {
            const auto prediction = result.a / std::pow(times[i] + result.b, result.c);
            const auto residual = std::log(prediction / intensities[i]);
            result.objective += residual * residual;
            result.maxRelativeError = std::max(result.maxRelativeError, std::abs(prediction / intensities[i] - 1.));
        }
        result.logRms = std::sqrt(result.objective / times.size());
        return result;
    };
    // A reproducible bounded one-dimensional search is sufficient after the
    // analytical log-linear regression of a and c for each positive b.
    const auto lo = std::log(times.first() * 1e-5);
    const auto hi = std::log(times.last() * 10.);
    constexpr int GridCount = 160;
    int bestIndex = -1;
    IdfFit best;
    for (int i = 0; i <= GridCount; ++i) {
        const auto result = evaluate(lo + (hi - lo) * i / GridCount);
        if (result.objective < best.objective) { best = result; bestIndex = i; }
    }
    if (bestIndex < 0)
        return fail(error, QStringLiteral("The source depths do not support a positive, nonnegative-rainfall Chicago IDF fit."));
    double left = lo + (hi - lo) * std::max(0, bestIndex - 1) / GridCount;
    double right = lo + (hi - lo) * std::min(GridCount, bestIndex + 1) / GridCount;
    constexpr double Golden = .6180339887498948482;
    double x1 = right - Golden * (right - left), x2 = left + Golden * (right - left);
    auto f1 = evaluate(x1), f2 = evaluate(x2);
    for (int i = 0; i < 100; ++i) {
        if (f1.objective < best.objective) best = f1;
        if (f2.objective < best.objective) best = f2;
        if (f1.objective <= f2.objective) {
            right = x2; x2 = x1; f2 = f1;
            x1 = right - Golden * (right - left); f1 = evaluate(x1);
        } else {
            left = x1; x1 = x2; f1 = f2;
            x2 = left + Golden * (right - left); f2 = evaluate(x2);
        }
    }
    *fit = best;
    return true;
}
}

QUrl frequencyUrl(const FrequencyQuery &query, QString *error)
{
    if (error) error->clear();
    if (!std::isfinite(query.latitude) || !std::isfinite(query.longitude) ||
        query.latitude < -90. || query.latitude > 90. || query.longitude < -180. || query.longitude > 180. ||
        (query.units != "metric" && query.units != "english") ||
        (query.series != "pds" && query.series != "ams") ||
        (query.data != "depth" && query.data != "intensity")) {
        fail(error, QStringLiteral("Choose a valid latitude/longitude, units, series and data type."));
        return {};
    }
    QUrl url(QStringLiteral("https://hdsc.nws.noaa.gov/cgi-bin/hdsc/new/cgi_readH5.py"));
    QUrlQuery parameters;
    parameters.addQueryItem("lat", QString::number(query.latitude, 'f', 7));
    parameters.addQueryItem("lon", QString::number(query.longitude, 'f', 7));
    parameters.addQueryItem("type", "pf");
    parameters.addQueryItem("data", query.data);
    parameters.addQueryItem("units", query.units);
    parameters.addQueryItem("series", query.series);
    url.setQuery(parameters);
    return url;
}

bool parseFrequencyResponse(const QByteArray &response, const FrequencyQuery &query,
                            FrequencyTable *table, QString *error)
{
    if (error) error->clear();
    const auto url = frequencyUrl(query, error);
    if (!table || url.isEmpty())
        return fail(error, QStringLiteral("A valid NOAA request and output table are required."));
    if (response.isEmpty() || response.size() > MaxResponseBytes)
        return fail(error, QStringLiteral("The NOAA response is empty or exceeds the 512 KiB limit."));
    QVariantMap values;
    LiteralReader reader(QString::fromUtf8(response));
    if (!reader.assignments(&values))
        return fail(error, QStringLiteral("The NOAA response is not a supported literal data envelope."));
    if (values.value("result").toString() != "values")
        return fail(error, QStringLiteral("NOAA has no precipitation-frequency estimates for this location."));
    for (const auto &pair : {qMakePair(QStringLiteral("unit"), query.units),
                             qMakePair(QStringLiteral("ser"), query.series),
                             qMakePair(QStringLiteral("datatype"), query.data)})
        if (values.contains(pair.first) && values.value(pair.first).toString().toLower() != pair.second)
            return fail(error, QStringLiteral("The NOAA response does not match the requested source options."));
    FrequencyTable result;
    result.query = query;
    result.durationsMinutes = PointDurations;
    result.returnPeriods = PointPeriods;
    if (!matrix(values.value("quantiles"), query, &result.depthsMm) ||
        !matrix(values.value("lower"), query, &result.lowerMm) ||
        !matrix(values.value("upper"), query, &result.upperMm))
        return fail(error, QStringLiteral("The NOAA estimate/bound matrices do not match the qualified 19-by-10 schema."));
    for (qsizetype r = 0; r < PointDurations.size(); ++r)
        for (qsizetype c = 0; c < PointPeriods.size(); ++c) {
            const auto depth = result.depthsMm[r][c];
            if (result.lowerMm[r][c] > depth || depth > result.upperMm[r][c])
                return fail(error, QStringLiteral("The NOAA confidence bounds are inconsistent with the estimate."));
        }
    for (auto it = values.cbegin(); it != values.cend(); ++it)
        if (it.value().metaType().id() != QMetaType::QVariantList)
            result.metadata.insert(it.key(), it.value());
    result.metadata.insert("axisSchema", QStringLiteral("pfds-point-19x10"));
    result.url = url;
    result.retrievedAt = QDateTime::currentDateTimeUtc();
    *table = std::move(result);
    return true;
}

bool parseTemporalResponse(const QByteArray &response, const QString &area, int durationMinutes,
                           QList<CumulativeCurve> *curves, QString *error)
{
    if (error) error->clear();
    if (!curves || area.isEmpty() || area.size() > 128 || durationMinutes <= 0 ||
        response.isEmpty() || response.size() > MaxResponseBytes)
        return fail(error, QStringLiteral("Choose a bounded temporal product with an explicit area and duration."));
    QList<CumulativeCurve> result;
    QSet<QString> identities;
    QString section;
    QVector<double> axis;
    const auto lines = QString::fromUtf8(response).split('\n');
    for (auto line : lines) {
        line = line.trimmed();
        if (line.isEmpty()) continue;
        auto fields = line.split(',');
        for (auto &field : fields) {
            field = field.trimmed();
            if (field.startsWith('"') && field.endsWith('"') && field.size() >= 2)
                field = field.mid(1, field.size() - 2).trimmed();
        }
        const auto first = fields.first().trimmed();
        static const QRegularExpression quartileHeader(
            QStringLiteral("^(?:First|Second|Third|Fourth) Quartile$"), QRegularExpression::CaseInsensitiveOption);
        if (quartileHeader.match(first).hasMatch() || first.compare("All cases", Qt::CaseInsensitive) == 0) {
            section = first;
            axis.clear();
            continue;
        }
        if (first.compare("percent of duration", Qt::CaseInsensitive) == 0) {
            QVector<double> parsed;
            for (int i = 1; i < fields.size(); ++i) {
                if (fields[i].isEmpty() && i == fields.size() - 1) continue;
                bool ok = false;
                const auto value = fields[i].toDouble(&ok);
                if (!ok || !std::isfinite(value))
                    return fail(error, QStringLiteral("The NOAA temporal time axis contains an invalid number."));
                parsed.append(value / 100.);
            }
            if (parsed.size() < 2 || parsed.size() > 1000)
                return fail(error, QStringLiteral("The NOAA temporal time axis has an unsupported size."));
            axis = std::move(parsed);
            continue;
        }
        if (axis.isEmpty() || !first.endsWith('%')) continue;
        bool percentileOk = false;
        const auto percentile = first.first(first.size() - 1).toDouble(&percentileOk);
        if (!percentileOk || !std::isfinite(percentile) || percentile < 0. || percentile > 100. ||
            section.isEmpty())
            return fail(error, QStringLiteral("The NOAA temporal percentile/section is invalid."));
        if (fields.last().isEmpty()) fields.removeLast();
        if (fields.size() != axis.size() + 1 || result.size() >= 100)
            return fail(error, QStringLiteral("The NOAA temporal row dimensions are inconsistent."));
        CumulativeCurve curve;
        curve.id = area + '/' + section + '/' + first;
        curve.title = section + QStringLiteral(" — ") + first;
        curve.source = QStringLiteral("NOAA Atlas 14 temporal distribution: ") + area;
        curve.durationMinutes = durationMinutes;
        if (identities.contains(curve.id))
            return fail(error, QStringLiteral("The NOAA temporal product repeats a curve identity."));
        for (int i = 0; i < axis.size(); ++i) {
            bool ok = false;
            const auto value = fields[i + 1].toDouble(&ok);
            if (!ok || !std::isfinite(value))
                return fail(error, QStringLiteral("The NOAA temporal cumulative row contains an invalid number."));
            curve.points.append({axis[i], value / 100.});
        }
        if (!validCurve(curve))
            return fail(error, QStringLiteral("The NOAA temporal curve is incomplete or nonmonotonic."));
        identities.insert(curve.id);
        result.append(std::move(curve));
    }
    if (result.isEmpty())
        return fail(error, QStringLiteral("The NOAA response contains no supported temporal curves."));
    *curves = std::move(result);
    return true;
}

QList<StormMethod> stormMethods()
{
    return {
        {"scs-i", "SCS Type I (24 hours)", "Published half-hour cumulative ordinates, with linear interpolation; native 24-hour duration.", "FHWA HDS-2 (2002), Table 6.5", false, false, true},
        {"scs-ia", "SCS Type IA (24 hours)", "Published 0.1-hour cumulative ordinates, with linear interpolation; native 24-hour duration.", "WSDOT Highway Runoff Manual M31-16.04 (2014), Table 4C-3", false, false, true},
        {"scs-ii", "SCS Type II (24 hours)", "Published 0.1-hour cumulative ordinates, with linear interpolation; native 24-hour duration.", "WSDOT Highway Runoff Manual M31-16.04 (2014), Table 4C-4", false, false, true},
        {"scs-iii", "SCS Type III (24 hours)", "Published half-hour cumulative ordinates, with linear interpolation; native 24-hour duration.", "FHWA HDS-2 (2002), Table 6.5", false, false, true},
        {"nrcs-mse3", "NRCS MSE3 (24 hours, Midwest/Southeast)", "Explicit regional MSE3 0.1-hour cumulative table, with linear interpolation; no automatic geographic selection.", "SEMCOG Southeast Michigan Rainfall (2020), Table 5, NRCS MSE3", false, false, true},
        {"huff-first", "Huff first quartile (Midwest point median)", "Published point-median pattern; explicit regional/quartile selection, linear cumulative interpolation.", "Huff and Angel (1992) Bulletin 71; Columbia Appendix F Table 2.2.2.4", false, false, false},
        {"huff-second", "Huff second quartile (Midwest point median)", "Published point-median pattern; explicit regional/quartile selection, linear cumulative interpolation.", "Huff and Angel (1992) Bulletin 71; Columbia Appendix F Table 2.2.2.4", false, false, false},
        {"huff-third", "Huff third quartile (Midwest point median)", "Published point-median pattern; explicit regional/quartile selection, linear cumulative interpolation.", "Huff and Angel (1992) Bulletin 71; Columbia Appendix F Table 2.2.2.4", false, false, false},
        {"huff-fourth", "Huff fourth quartile (Midwest point median)", "Published point-median pattern; explicit regional/quartile selection, linear cumulative interpolation.", "Huff and Angel (1992) Bulletin 71; Columbia Appendix F Table 2.2.2.4", false, false, false},
        {"triangular", "Triangular", "Analytical triangular intensity with explicit peak position.", "Analytical triangular distribution", false, false, false},
        {"alternating-block", "Alternating block", "Rank depth-duration increments around the selected peak interval.", "USACE HEC-HMS Frequency Storm", true, false, false},
        {"chicago", "Chicago (fitted IDF)", "Fit a positive-shift IDF relationship; evaluate its analytical cumulative curve with explicit peak position.", "Chen/FHWA 1975, PRWG106-4, equations 9–13 and 21–22", true, false, false},
        {"noaa-temporal", "NOAA Atlas 14 temporal", "Use the selected live area/quartile/percentile cumulative pattern.", "NOAA Atlas 14 PFDS temporal distributions", false, true, false},
        {"custom", "Custom cumulative curve", "Piecewise-linear normalized cumulative coordinates supplied by the user.", "User-supplied cumulative curve", false, true, false}
    };
}

bool sampleFrequencyDepth(const FrequencyTable &table, double durationMinutes, int frequencyIndex,
                          FrequencyBound bound, double *depthMm, QString *error)
{
    if (error) error->clear();
    const auto &values = bound == FrequencyBound::Lower ? table.lowerMm
                         : bound == FrequencyBound::Upper ? table.upperMm : table.depthsMm;
    if (!depthMm || !std::isfinite(durationMinutes) || table.durationsMinutes.size() < 2 ||
        frequencyIndex < 0 || frequencyIndex >= table.returnPeriods.size() ||
        values.size() != table.durationsMinutes.size())
        return fail(error, QStringLiteral("The selected precipitation-frequency table or column is unavailable."));
    double previous = 0.;
    for (int i = 0; i < table.durationsMinutes.size(); ++i) {
        const auto time = table.durationsMinutes[i];
        if (!std::isfinite(time) || time <= previous || values[i].size() != table.returnPeriods.size())
            return fail(error, QStringLiteral("The precipitation-frequency dimensions or duration axis are invalid."));
        previous = time;
    }
    if (durationMinutes < table.durationsMinutes.first() || durationMinutes > table.durationsMinutes.last())
        return fail(error, QStringLiteral("The requested duration is outside NOAA's returned duration range."));
    auto next = std::lower_bound(table.durationsMinutes.cbegin(), table.durationsMinutes.cend(), durationMinutes);
    const int hi = int(next - table.durationsMinutes.cbegin());
    double value = values[hi][frequencyIndex];
    if (*next != durationMinutes) {
        const int lo = hi - 1;
        const auto lower = values[lo][frequencyIndex];
        if (!std::isfinite(lower) || !std::isfinite(value) || lower <= 0. || value <= 0.)
            return fail(error, QStringLiteral("Log-log duration interpolation requires positive finite source depths."));
        const auto ratio = std::log(durationMinutes / table.durationsMinutes[lo]) /
                           std::log(table.durationsMinutes[hi] / table.durationsMinutes[lo]);
        value = std::exp(std::log(lower) + (std::log(value) - std::log(lower)) * ratio);
    }
    if (!std::isfinite(value) || value < 0.)
        return fail(error, QStringLiteral("The selected precipitation-frequency depth is invalid."));
    *depthMm = value;
    return true;
}

bool generateStorm(const StormRequest &request, StormSeries *storm, QString *error)
{
    if (error) error->clear();
    if (!storm || !std::isfinite(request.depthMm) || request.depthMm <= 0. ||
        request.durationMinutes <= 0 || request.intervalMinutes <= 0 ||
        request.durationMinutes % request.intervalMinutes != 0 ||
        request.durationMinutes / request.intervalMinutes > MaxIntervals)
        return fail(error, QStringLiteral("Choose positive depth and a duration divisible by the interval (at most 20,000 intervals)."));
    const auto methods = stormMethods();
    const auto method = std::find_if(methods.cbegin(), methods.cend(), [&](const auto &item) { return item.id == request.method; });
    if (method == methods.cend())
        return fail(error, QStringLiteral("This storm method has not been qualified for generation."));
    if (method->requires24Hours && request.durationMinutes != 1440)
        return fail(error, QStringLiteral("This published pattern has a native 24-hour duration; reduction is not implicit."));
    const int count = request.durationMinutes / request.intervalMinutes;
    QVector<double> depths(count);
    QJsonObject extraParameters;
    if (request.method == "triangular") {
        if (!std::isfinite(request.peakRatio) || request.peakRatio <= 0. || request.peakRatio >= 1.)
            return fail(error, QStringLiteral("The triangular peak position must lie strictly between 0 and 1."));
        const auto cumulative = [&](double x) {
            if (x <= request.peakRatio) return x * x / request.peakRatio;
            return 1. - (1. - x) * (1. - x) / (1. - request.peakRatio);
        };
        for (int i = 0; i < count; ++i)
            depths[i] = request.depthMm * (cumulative(double(i + 1) / count) - cumulative(double(i) / count));
    } else if (request.method == "chicago") {
        if (!std::isfinite(request.peakRatio) || request.peakRatio <= 0. || request.peakRatio >= 1.)
            return fail(error, QStringLiteral("The Chicago peak position must lie strictly between 0 and 1."));
        IdfFit fit;
        if (!chicagoFit(request, &fit, error)) return false;
        const auto duration = double(request.durationMinutes);
        const auto sourceDepth = [&](double t) { return fit.a * t / std::pow(t + fit.b, fit.c) / 60.; };
        const auto full = sourceDepth(duration);
        const auto peak = request.peakRatio * duration;
        const auto cumulative = [&](double t) {
            if (t <= peak)
                return request.peakRatio * (full - sourceDepth((peak - t) / request.peakRatio)) / full;
            return request.peakRatio + (1. - request.peakRatio) *
                   sourceDepth((t - peak) / (1. - request.peakRatio)) / full;
        };
        for (int i = 0; i < count; ++i)
            depths[i] = request.depthMm * (cumulative(double(i + 1) * request.intervalMinutes) - cumulative(double(i) * request.intervalMinutes));
        extraParameters = {{"idfEquation", "intensityMmPerHour = a / (durationMinutes + b)^c"},
                           {"idfA", fit.a}, {"idfBMinutes", fit.b}, {"idfC", fit.c},
                           {"idfLogRms", fit.logRms}, {"idfMaxRelativeError", fit.maxRelativeError},
                           {"idfDurationMinMinutes", request.frequency.durationsMinutes.first()},
                           {"idfDurationMaxMinutes", duration}, {"fittedTotalDepthMm", full},
                           {"depthScale", request.depthMm / full}};
    } else if (request.method == "alternating-block") {
        if (!std::isfinite(request.peakRatio) || request.peakRatio <= 0. || request.peakRatio >= 1.)
            return fail(error, QStringLiteral("The alternating-block peak position must lie strictly between 0 and 1."));
        QVector<double> increments;
        double previous = 0.;
        for (int i = 1; i <= count; ++i) {
            double depth = 0.;
            if (!sampleFrequencyDepth(request.frequency, double(i) * request.intervalMinutes,
                                       request.frequencyIndex, request.bound, &depth, error))
                return false;
            if (depth + 1e-9 < previous)
                return fail(error, QStringLiteral("The frequency-depth sequence decreases with duration."));
            increments.append(std::max(0., depth - previous));
            previous = depth;
        }
        if (previous <= 0.)
            return fail(error, QStringLiteral("The selected source storm has no positive depth."));
        std::sort(increments.begin(), increments.end(), std::greater<double>());
        const int peak = std::clamp(int(std::floor(request.peakRatio * count)), 0, count - 1);
        QVector<int> positions{peak};
        for (int distance = 1; positions.size() < count; ++distance) {
            if (peak + distance < count) positions.append(peak + distance);
            if (peak - distance >= 0) positions.append(peak - distance);
        }
        for (int i = 0; i < count; ++i)
            depths[positions[i]] = increments[i] * request.depthMm / previous;
    } else {
        const auto curve = method->requires24Hours || request.method.startsWith("huff-")
                               ? standardCurve(request.method) : request.curve;
        if (!validCurve(curve))
            return fail(error, QStringLiteral("Choose a complete, finite and monotonic cumulative curve from (0,0) to (1,1)."));
        if (curve.durationMinutes > 0. && std::abs(curve.durationMinutes - request.durationMinutes) > 1e-9)
            return fail(error, QStringLiteral("The selected source pattern has a different native duration; duration reduction is not implicit."));
        for (int i = 0; i < count; ++i)
            depths[i] = request.depthMm * (cumulativeAt(curve, double(i + 1) / count) - cumulativeAt(curve, double(i) / count));
    }
    StormSeries result;
    result.method = request.method;
    result.label = method->label;
    result.intervalMinutes = request.intervalMinutes;
    result.timesMinutes.append(0.);
    result.cumulativeMm.append(0.);
    double total = 0.;
    for (int i = 0; i < count; ++i) {
        if (!std::isfinite(depths[i]) || depths[i] < 0.)
            return fail(error, QStringLiteral("The generated storm has invalid or negative interval depths."));
        const auto intensity = depths[i] * (60. / request.intervalMinutes);
        if (!std::isfinite(intensity))
            return fail(error, QStringLiteral("The generated interval intensity exceeds the finite numeric range."));
        total += depths[i];
        result.depthsMm.append(depths[i]);
        result.intensityMmPerHour.append(intensity);
        result.timesMinutes.append(double(i + 1) * request.intervalMinutes);
        result.cumulativeMm.append(total);
    }
    if (!std::isfinite(total) || std::abs(total - request.depthMm) > 1e-8 * std::max(1., request.depthMm))
        return fail(error, QStringLiteral("The generated intervals do not conserve the selected rainfall depth."));
    result.totalDepthMm = total;
    result.parameters = {{"generatorVersion", 1}, {"method", request.method}, {"source", method->source},
                         {"depthMm", request.depthMm}, {"durationMinutes", request.durationMinutes},
                         {"intervalMinutes", request.intervalMinutes}, {"peakRatio", request.peakRatio}};
    for (auto it = extraParameters.constBegin(); it != extraParameters.constEnd(); ++it)
        result.parameters.insert(it.key(), it.value());
    if (method->requiresCurve) {
        result.parameters.insert("curveId", request.curve.id);
        result.parameters.insert("curveSource", request.curve.source);
    }
    *storm = std::move(result);
    return true;
}
}
