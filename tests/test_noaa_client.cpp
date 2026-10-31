/**
 * @file test_noaa_client.cpp
 * @brief In-memory HTTP acceptance of live NOAA request policy and cancellation.
 * @details Synthetic replies exercise the production client without network or
 * response files. Tests check cache bypass, bounded bodies, policy redirects,
 * superseded callbacks and successful frequency/temporal dispatch.
 * @author Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license GPL-3.0-or-later
 */
#include "NoaaClient.hpp"
#include <sdk/INetworkPolicy.hpp>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTest>
#include <QUrlQuery>
#include <cstring>

using namespace OpenSWMM::Data;

namespace
{
/** @brief Generate a complete duration/frequency-shaped synthetic NOAA response. */
QByteArray pointResponse()
{
    const auto matrix = [](int delta) {
        QByteArray result("[");
        for (int row = 0; row < 19; ++row) {
            if (row)
                result += ',';
            result += '[';
            for (int column = 0; column < 10; ++column) {
                if (column)
                    result += ',';
                result += '\'' + QByteArray::number(10 + row * 5 + column + delta) + '\'';
            }
            result += ']';
        }
        return result + ']';
    };
    return "result='values';\nunit='metric';\ndatatype='depth';\nser='pds';\n"
           "lat='38.9000';\nlon='-77.0000';\nreg='orb';\nregion='Ohio River Basin';\n"
           "volume='2';\nversion='3';\nquantiles=" + matrix(0) + ";\nlower=" + matrix(-1) +
           ";\nupper=" + matrix(1) + ";\n";
}

/** @brief Small valid cumulative temporal product with a real header grammar. */
QByteArray temporalResponse()
{
    return "Tabulated Temporal Distribution Data,\nNOAA Atlas 14 Volume 2 Version 2.1,\n"
           "First Quartile,\n\npercent of duration,0,50,100\n50%,0,80,100\n"
           "All Cases,\n\npercent of duration,0,50,100\n50%,0,50,100\n";
}

/** @brief Controlled reply with bounded reads and synchronous abort settlement. */
class Reply final : public QNetworkReply
{
public:
    Reply(const QNetworkRequest &request, QByteArray body, QObject *parent)
        : QNetworkReply(parent), mBody(std::move(body))
    {
        setRequest(request);
        setUrl(request.url());
        setOperation(QNetworkAccessManager::GetOperation);
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, 200);
        open(QIODevice::ReadOnly | QIODevice::Unbuffered);
    }
    /** @brief Publish the synthetic bytes and the selected HTTP status. */
    void complete(int status = 200, const QUrl &redirect = {})
    {
        if (isFinished())
            return;
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, status);
        if (!redirect.isEmpty())
            setAttribute(QNetworkRequest::RedirectionTargetAttribute, redirect);
        emit downloadProgress(mBody.size(), mBody.size());
        if (isFinished())
            return;
        emit readyRead();
        if (isFinished())
            return;
        setFinished(true);
        emit finished();
    }
    /** @brief Expose retry metadata without a network server. */
    void retryAfter(const QByteArray &seconds) { setRawHeader("Retry-After", seconds); }
    /** @brief Simulate an already queued stale completion after cancellation. */
    void lateComplete() { emit finished(); }
    void abort() override
    {
        if (isFinished())
            return;
        aborted = true;
        setError(QNetworkReply::OperationCanceledError, "Cancelled");
        setFinished(true);
        emit finished();
    }
    qint64 bytesAvailable() const override
    {
        return mBody.size() - mOffset + QIODevice::bytesAvailable();
    }
    bool aborted = false; ///< True after client cancellation or response-size refusal.

protected:
    qint64 readData(char *destination, qint64 maximum) override
    {
        const auto count = qMin(maximum, qint64(mBody.size()) - mOffset);
        if (count <= 0)
            return -1;
        std::memcpy(destination, mBody.constData() + mOffset, size_t(count));
        mOffset += count;
        return count;
    }

private:
    QByteArray mBody; ///< Synthetic in-memory response, never persisted.
    qint64 mOffset = 0; ///< Bytes already supplied to the production client.
};

/** @brief Policy-controlled manager that creates only synthetic replies. */
class Network final : public QNetworkAccessManager, public OpenSWMM::Sdk::INetworkPolicy
{
public:
    bool enabled = true; ///< Simulates offline/refused host policy.
    bool allowed(const QUrl &) const override { return enabled; }
    QNetworkAccessManager *shared() override { return this; }
    QList<QNetworkRequest> requests; ///< Exact requests observed before any reply data.
    QList<QPointer<Reply>> replies; ///< Guarded replies for deliberate ordering.
    QByteArray nextBody; ///< Optional synthetic override for the next request.

protected:
    QNetworkReply *createRequest(Operation, const QNetworkRequest &request, QIODevice *) override
    {
        requests.append(request);
        const auto body = nextBody.isNull()
            ? (request.url().path().endsWith(".csv") ? temporalResponse() : pointResponse()) : nextBody;
        nextBody = {};
        auto *reply = new Reply(request, body, this);
        replies.append(reply);
        return reply;
    }
};

/** @brief Optional smoke-test policy using real Qt HTTPS and no disk cache. */
class LiveNetwork final : public QNetworkAccessManager, public OpenSWMM::Sdk::INetworkPolicy
{
public:
    bool allowed(const QUrl &url) const override
    {
        return url.scheme() == "https" && url.host() == "hdsc.nws.noaa.gov";
    }
    QNetworkAccessManager *shared() override { return this; }
};
}

/** @brief Network-only regression suite with no private GUI or filesystem dependency. */
class NoaaClientTest final : public QObject
{
    Q_OBJECT
private slots:
    /** @brief The official selector produces supported canonical URLs only. */
    void temporalSelectorsAreExplicit()
    {
        QString error;
        QCOMPARE(NoaaClient::temporalUrl("orb", "general", 1440, &error).toString(),
                 "https://hdsc.nws.noaa.gov/pub/hdsc/data/orb/orb_general_24h_temporal.csv");
        QVERIFY(error.isEmpty());
        QCOMPARE(NoaaClient::temporalUrl("sw", "10", 360).path(),
                 "/pub/hdsc/data/sw/ca_10_6h_temporal.csv");
        QVERIFY(NoaaClient::temporalUrl("sw", "15", 360, &error).isEmpty());
        QVERIFY(!error.isEmpty());
        QVERIFY(NoaaClient::temporalUrl("orb", "../tx_1", 1440).isEmpty());
        QVERIFY(NoaaClient::temporalUrl("orb", "general", 60).isEmpty());
        QVERIFY(NoaaClient::temporalUrl("unknown", "general", 1440).isEmpty());
    }
    /** @brief A fresh request disables both cache reading and cache saving. */
    void frequencyIsLiveAndMemoryOnly()
    {
        Network network;
        QObject lifetime;
        NoaaClient client(&network, &lifetime);
        FrequencyTable accepted;
        int deliveries = 0;
        connect(&client, &NoaaClient::frequencyReady, this, [&](const FrequencyTable &table) {
            accepted = table;
            ++deliveries;
        });
        QVERIFY(client.fetchFrequency({}));
        QVERIFY(client.isBusy());
        QTRY_COMPARE(network.requests.size(), 1);
        const auto request = network.requests.first();
        QCOMPARE(request.attribute(QNetworkRequest::CacheLoadControlAttribute).toInt(),
                 int(QNetworkRequest::AlwaysNetwork));
        QVERIFY(!request.attribute(QNetworkRequest::CacheSaveControlAttribute).toBool());
        QCOMPARE(request.attribute(QNetworkRequest::RedirectPolicyAttribute).toInt(),
                 int(QNetworkRequest::ManualRedirectPolicy));
        QCOMPARE(request.url().host(), "hdsc.nws.noaa.gov");
        QCOMPARE(QUrlQuery(request.url()).queryItemValue("lat").toDouble(), 38.9);
        network.replies.first()->complete();
        QCOMPARE(deliveries, 1);
        QVERIFY(!client.isBusy());
        QCOMPARE(accepted.depthsMm.size(), 19);
        QCOMPARE(accepted.depthsMm.first().size(), 10);
        QCOMPARE(accepted.url, request.url());
        QVERIFY(accepted.retrievedAt.isValid());
    }
    /** @brief Offline/policy refusal produces no network request or busy state. */
    void hostPolicyRefusalPrecedesNetwork()
    {
        Network network;
        network.enabled = false;
        QObject lifetime;
        NoaaClient client(&network, &lifetime);
        QString error;
        QVERIFY(!client.fetchFrequency({}, &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(!client.isBusy());
        QVERIFY(network.requests.isEmpty());
        QVERIFY(!client.fetchTemporal("orb", "general", 1440, &error));
        QVERIFY(network.requests.isEmpty());
    }
    /** @brief Cancelled callbacks cannot publish data, even if already queued. */
    void cancellationRetiresCallbacks()
    {
        Network network;
        QObject lifetime;
        NoaaClient client(&network, &lifetime);
        int deliveries = 0, cancellations = 0, failures = 0;
        connect(&client, &NoaaClient::frequencyReady, this, [&] { ++deliveries; });
        connect(&client, &NoaaClient::cancelled, this, [&] { ++cancellations; });
        connect(&client, &NoaaClient::failed, this, [&] { ++failures; });
        QVERIFY(client.fetchFrequency({}));
        QTRY_COMPARE(network.replies.size(), 1);
        const auto reply = network.replies.first();
        client.cancel();
        QVERIFY(reply && reply->aborted);
        reply->lateComplete();
        QCOMPARE(cancellations, 1);
        QCOMPARE(deliveries, 0);
        QCOMPARE(failures, 0);
        QVERIFY(!client.isBusy());
    }
    /** @brief Starting another product invalidates all older reply state. */
    void supersededReplyCannotReplaceTemporalCurves()
    {
        Network network;
        QObject lifetime;
        NoaaClient client(&network, &lifetime);
        int points = 0, temporal = 0;
        QList<CumulativeCurve> accepted;
        connect(&client, &NoaaClient::frequencyReady, this, [&] { ++points; });
        connect(&client, &NoaaClient::temporalReady, this, [&](const QList<CumulativeCurve> &curves) {
            accepted = curves;
            ++temporal;
        });
        QVERIFY(client.fetchFrequency({}));
        QTRY_COMPARE(network.replies.size(), 1);
        const auto old = network.replies.first();
        QVERIFY(client.fetchTemporal("orb", "general", 1440));
        old->lateComplete();
        QTRY_COMPARE(network.replies.size(), 2);
        network.replies.last()->complete();
        QCOMPARE(points, 0);
        QCOMPARE(temporal, 1);
        QCOMPARE(accepted.size(), 2);
        QVERIFY(accepted.first().source.contains("orb_general_24h_temporal.csv"));
    }
    /** @brief Redirects cannot escape NOAA even if the host allows other origins. */
    void foreignRedirectIsRefused()
    {
        Network network;
        QObject lifetime;
        NoaaClient client(&network, &lifetime);
        QString failure;
        connect(&client, &NoaaClient::failed, this, [&](const QString &text) { failure = text; });
        QVERIFY(client.fetchFrequency({}));
        QTRY_COMPARE(network.replies.size(), 1);
        network.replies.first()->complete(302, QUrl("https://example.com/payload"));
        QVERIFY(failure.contains("policy"));
        QCOMPARE(network.requests.size(), 1);
        QVERIFY(!client.isBusy());
    }
    /** @brief Relative same-origin redirects preserve cache bypass on the new request. */
    void permittedRedirectIsRechecked()
    {
        Network network;
        QObject lifetime;
        NoaaClient client(&network, &lifetime);
        int delivered = 0;
        connect(&client, &NoaaClient::frequencyReady, this, [&] { ++delivered; });
        QVERIFY(client.fetchFrequency({}));
        QTRY_COMPARE(network.replies.size(), 1);
        network.replies.first()->complete(302, QUrl("/redirected-point"));
        QCOMPARE(network.replies.size(), 2);
        QCOMPARE(network.requests.last().url().path(), "/redirected-point");
        QVERIFY(!network.requests.last().attribute(QNetworkRequest::CacheSaveControlAttribute).toBool());
        network.replies.last()->complete();
        QCOMPARE(delivered, 1);
    }
    /** @brief The advertised-size check rejects an oversized body before parsing it. */
    void responseMemoryIsBounded()
    {
        Network network;
        network.nextBody = QByteArray(2 * 1024 * 1024 + 1, 'x');
        QObject lifetime;
        NoaaClient client(&network, &lifetime);
        QString failure;
        connect(&client, &NoaaClient::failed, this, [&](const QString &text) { failure = text; });
        QVERIFY(client.fetchFrequency({}));
        QTRY_COMPARE(network.replies.size(), 1);
        const auto reply = network.replies.first();
        reply->complete();
        QVERIFY(reply && reply->aborted);
        QVERIFY(failure.contains("memory limit"));
        QVERIFY(!client.isBusy());
    }
    /** @brief Cancelling backoff prevents a later live retry. */
    void cancellationStopsRetry()
    {
        Network network;
        QObject lifetime;
        NoaaClient client(&network, &lifetime);
        QVERIFY(client.fetchFrequency({}));
        QTRY_COMPARE(network.replies.size(), 1);
        network.replies.first()->retryAfter("0");
        network.replies.first()->complete(503);
        QVERIFY(client.isBusy());
        client.cancel();
        QTest::qWait(1100);
        QCOMPARE(network.requests.size(), 1);
        QVERIFY(!client.isBusy());
    }
    /** @brief Destruction of the policy guard cancels all work before borrowed access. */
    void lifetimeDestructionCancelsRequest()
    {
        Network network;
        auto *lifetime = new QObject;
        NoaaClient client(&network, lifetime);
        QVERIFY(client.fetchFrequency({}));
        QTRY_COMPARE(network.replies.size(), 1);
        delete lifetime;
        QVERIFY(!client.isBusy());
        QString error;
        QVERIFY(!client.fetchFrequency({}, &error));
        QVERIFY(!error.isEmpty());
    }
    /** @brief A new request from a settlement observer suppresses the older result. */
    void reentrantSettlementPreservesNewerRequest()
    {
        Network network;
        QObject lifetime;
        NoaaClient client(&network, &lifetime);
        int point = 0, temporal = 0;
        bool once = false;
        connect(&client, &NoaaClient::frequencyReady, this, [&] { ++point; });
        connect(&client, &NoaaClient::temporalReady, this, [&] { ++temporal; });
        connect(&client, &NoaaClient::busyChanged, this, [&](bool busy) {
            if (!busy && !once) {
                once = true;
                QVERIFY(client.fetchTemporal("orb", "general", 1440));
            }
        });
        QVERIFY(client.fetchFrequency({}));
        QTRY_COMPARE(network.replies.size(), 1);
        network.replies.first()->complete();
        QTRY_COMPARE(network.replies.size(), 2);
        network.replies.last()->complete();
        QCOMPARE(point, 0);
        QCOMPARE(temporal, 1);
    }
    /** @brief Explicitly enabled live qualification retains only numeric summaries.
     * @note Set NOAA_LIVE_CHECK=1 to contact NOAA; normal deterministic runs skip this case. */
    void optionalLiveNoaaContract()
    {
        if (qEnvironmentVariable("NOAA_LIVE_CHECK") != "1")
            QSKIP("Set NOAA_LIVE_CHECK=1 for the optional live NOAA contract check.");
        LiveNetwork network;
        NoaaClient client(&network, &network);
        bool done = false;
        QString failure;
        FrequencyTable table;
        QList<CumulativeCurve> curves;
        connect(&client, &NoaaClient::failed, this, [&](const QString &message) {
            failure = message;
            done = true;
        });
        connect(&client, &NoaaClient::frequencyReady, this, [&](const FrequencyTable &value) {
            table = value;
            done = true;
        });
        connect(&client, &NoaaClient::temporalReady, this,
                [&](const QList<CumulativeCurve> &value) {
            curves = value;
            done = true;
        });
        FrequencyQuery query;
        query.latitude = 38.9;
        query.longitude = -77.04;
        QString immediate;
        QVERIFY2(client.fetchFrequency(query, &immediate), qPrintable(immediate));
        QTRY_VERIFY_WITH_TIMEOUT(done, 45000);
        QVERIFY2(failure.isEmpty(), qPrintable(failure));
        QVERIFY(!client.isBusy());
        QCOMPARE(table.durationsMinutes.size(), 19);
        QCOMPARE(table.returnPeriods.size(), 10);
        QCOMPARE(table.depthsMm.size(), table.durationsMinutes.size());
        QCOMPARE(table.lowerMm.size(), table.durationsMinutes.size());
        QCOMPARE(table.upperMm.size(), table.durationsMinutes.size());
        QVERIFY(table.retrievedAt.isValid());
        for (qsizetype row = 0; row < table.depthsMm.size(); ++row) {
            QCOMPARE(table.depthsMm[row].size(), table.returnPeriods.size());
            for (qsizetype column = 0; column < table.returnPeriods.size(); ++column) {
                QVERIFY(table.depthsMm[row][column] > 0.);
                QVERIFY(table.lowerMm[row][column] <= table.depthsMm[row][column]);
                QVERIFY(table.upperMm[row][column] >= table.depthsMm[row][column]);
            }
        }
        qInfo("Live NOAA frequency contract: %lld durations, %lld recurrence columns; in-memory only.",
              static_cast<long long>(table.durationsMinutes.size()),
              static_cast<long long>(table.returnPeriods.size()));
        done = false;
        QVERIFY2(client.fetchTemporal("orb", "general", 1440, &immediate), qPrintable(immediate));
        QTRY_VERIFY_WITH_TIMEOUT(done, 45000);
        QVERIFY2(failure.isEmpty(), qPrintable(failure));
        QVERIFY(!curves.isEmpty());
        for (const auto &curve : curves) {
            QVERIFY(curve.points.size() >= 3);
            QCOMPARE(curve.durationMinutes, 1440.);
            QCOMPARE(curve.points.first(), QPointF(0., 0.));
            QCOMPARE(curve.points.last(), QPointF(1., 1.));
            for (qsizetype i = 1; i < curve.points.size(); ++i) {
                QVERIFY(curve.points[i].x() > curve.points[i - 1].x());
                QVERIFY(curve.points[i].y() >= curve.points[i - 1].y());
            }
        }
        qInfo("Live NOAA temporal contract: %lld cumulative curves; in-memory only.",
              static_cast<long long>(curves.size()));
    }
};
QTEST_GUILESS_MAIN(NoaaClientTest)
#include "test_noaa_client.moc"
