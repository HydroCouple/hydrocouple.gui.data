/**
 * @file NoaaClient.cpp
 * @brief Bounded, uncached NOAA HTTP requests and guarded completion dispatch.
 * @details Point requests follow NOAA HDSC FAQ 2.5. Temporal paths follow the
 * public PFDS temporal selector; downloaded values remain in memory only.
 * @author Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license GPL-3.0-or-later
 */
#include "NoaaClient.hpp"
#include <sdk/ExtensionHost.hpp>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QMap>
#include <memory>

namespace OpenSWMM::Data
{
namespace
{
constexpr qint64 maximumResponse = 2 * 1024 * 1024; ///< In-memory body ceiling.
constexpr int maximumRedirects = 4; ///< Every new destination is policy checked.
constexpr int maximumRetries = 2; ///< Additional attempts after transient failure.

/** @brief Assign an optional literal error and produce an empty URL. */
QUrl invalid(QString *error, const QString &message)
{
    if (error)
        *error = message;
    return {};
}
}

NoaaClient::NoaaClient(Sdk::ExtensionHost *host, QObject *parent)
    : NoaaClient(host ? host->network() : nullptr, host ? host->lifetime() : nullptr, parent)
{
}

NoaaClient::NoaaClient(Sdk::INetworkPolicy *network, QObject *lifetime, QObject *parent)
    : QObject(parent), mNetwork(network), mLifetime(lifetime)
{
    mRetry.setSingleShot(true);
    if (lifetime)
        connect(lifetime, &QObject::destroyed, this, [this] {
            mNetwork = nullptr;
            cancel();
        });
}

NoaaClient::~NoaaClient()
{
    ++mGeneration;
    mRetry.stop();
    abortReply();
}

QUrl NoaaClient::temporalUrl(const QString &region, const QString &area, int durationMinutes,
                            QString *error)
{
    if (error)
        error->clear();
    if (durationMinutes != 360 && durationMinutes != 720 && durationMinutes != 1440 &&
        durationMinutes != 5760)
        return invalid(error, tr("NOAA temporal products support 6, 12, 24 or 96 hours."));
    const QMap<QString, int> numbered{{"sw", 14}, {"ak", 2}, {"mw", 4}, {"se", 2},
                                     {"ne", 2}, {"tx", 3}, {"inw", 2}};
    bool valid = false;
    if (numbered.contains(region)) {
        bool number = false;
        const int value = area.toInt(&number);
        valid = number && QString::number(value) == area && value >= 1 && value <= numbered[region];
    } else if (region == "sa") {
        valid = area == "general" || area == "convective";
    } else if (region == "orb" || region == "pr" || region == "hi" || region == "pi") {
        valid = area == "general";
    }
    if (!valid)
        return invalid(error, tr("Choose an available NOAA temporal region and area."));
    const QString fileRegion = region == "sw" ? QStringLiteral("ca") : region;
    return QUrl(QStringLiteral("https://hdsc.nws.noaa.gov/pub/hdsc/data/%1/%2_%3_%4h_temporal.csv")
                    .arg(region, fileRegion, area).arg(durationMinutes / 60));
}

bool NoaaClient::fetchFrequency(const FrequencyQuery &query, QString *error)
{
    const auto url = frequencyUrl(query, error);
    if (url.isEmpty())
        return false;
    if (!start(url, Kind::Frequency, error))
        return false;
    mQuery = query;
    const auto generation = mGeneration;
    // Start on the next turn so even synthetic immediate replies see the copied query.
    QTimer::singleShot(0, this, [this, url, generation] { request(url, generation, 0, 0); });
    return true;
}

bool NoaaClient::fetchTemporal(const QString &region, const QString &area, int durationMinutes,
                               QString *error)
{
    const auto url = temporalUrl(region, area, durationMinutes, error);
    if (url.isEmpty())
        return false;
    if (!start(url, Kind::Temporal, error))
        return false;
    mArea = region + '/' + area;
    mDuration = durationMinutes;
    const auto generation = mGeneration;
    QTimer::singleShot(0, this, [this, url, generation] { request(url, generation, 0, 0); });
    return true;
}

bool NoaaClient::permitted(const QUrl &url) const
{
    return mLifetime && mNetwork && url.isValid() && url.scheme() == "https" &&
           url.host().compare(QStringLiteral("hdsc.nws.noaa.gov"), Qt::CaseInsensitive) == 0 &&
           (url.port(-1) == -1 || url.port() == 443) && url.userName().isEmpty() &&
           url.password().isEmpty() && url.fragment().isEmpty() && mNetwork->allowed(url);
}

bool NoaaClient::start(const QUrl &url, Kind kind, QString *error)
{
    if (error)
        error->clear();
    if (!permitted(url)) {
        if (error)
            *error = tr("The host network policy does not permit live NOAA retrieval.");
        return false;
    }
    const auto before = mGeneration;
    const QPointer<NoaaClient> self(this);
    cancel();
    if (!self || mGeneration != before + 1) {
        if (error)
            *error = tr("A newer NOAA request superseded this request.");
        return false;
    }
    mKind = kind;
    mBusy = true;
    const auto generation = mGeneration;
    emit busyChanged(true);
    return self && generation == mGeneration && mBusy;
}

void NoaaClient::abortReply()
{
    const QPointer<QNetworkReply> reply = mReply;
    mReply = nullptr;
    if (!reply)
        return;
    disconnect(reply, nullptr, this, nullptr);
    reply->abort();
    if (reply)
        reply->deleteLater();
}

void NoaaClient::cancel()
{
    const auto generation = ++mGeneration;
    mRetry.stop();
    abortReply();
    if (!mBusy)
        return;
    mBusy = false;
    const QPointer<NoaaClient> self(this);
    emit busyChanged(false);
    if (self && generation == mGeneration)
        emit cancelled();
}

void NoaaClient::fail(quint64 generation, const QString &message)
{
    if (generation != mGeneration || !mBusy)
        return;
    mRetry.stop();
    mBusy = false;
    const QPointer<NoaaClient> self(this);
    emit busyChanged(false);
    if (self && generation == mGeneration)
        emit failed(message);
}

void NoaaClient::request(const QUrl &url, quint64 generation, int attempt, int redirects)
{
    if (generation != mGeneration || !mBusy)
        return;
    if (!permitted(url)) {
        fail(generation, tr("The host network policy refused the NOAA destination."));
        return;
    }
    QNetworkRequest networkRequest(url);
    networkRequest.setTransferTimeout(30000);
    networkRequest.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    networkRequest.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
    networkRequest.setAttribute(QNetworkRequest::CacheSaveControlAttribute, false);
    networkRequest.setRawHeader("User-Agent", "OpenSWMM-Data/0.1");
    networkRequest.setRawHeader("Accept", "text/plain,text/csv;q=0.9,*/*;q=0.1");
    const QPointer<NoaaClient> self(this);
    emit progress(mKind == Kind::Frequency ? tr("Retrieving live NOAA frequency estimates")
                                          : tr("Retrieving live NOAA temporal patterns"));
    if (!self || !mLifetime || generation != mGeneration)
        return;
    auto *manager = mNetwork->shared();
    if (!manager) {
        fail(generation, tr("The NOAA network client is unavailable."));
        return;
    }
    QPointer<QNetworkReply> reply = manager->get(networkRequest);
    if (!self || !mLifetime || generation != mGeneration) {
        if (reply) {
            reply->abort();
            if (reply)
                reply->deleteLater();
        }
        return;
    }
    if (!reply) {
        fail(generation, tr("The NOAA network request was refused."));
        return;
    }
    mReply = reply;
    reply->setReadBufferSize(maximumResponse + 1);
    auto bytes = std::make_shared<QByteArray>();
    auto exceeded = std::make_shared<bool>(false);
    const auto read = [reply, bytes, exceeded] {
        if (!reply || *exceeded)
            return;
        while (reply->bytesAvailable() > 0) {
            const auto part = reply->read(qMin(qint64(65536), maximumResponse + 1 - bytes->size()));
            if (part.isEmpty())
                break;
            bytes->append(part);
            if (bytes->size() > maximumResponse) {
                *exceeded = true;
                reply->abort();
                return;
            }
        }
    };
    connect(reply, &QNetworkReply::readyRead, this, read);
    connect(reply, &QNetworkReply::downloadProgress, this,
            [reply, exceeded](qint64 received, qint64 total) {
        if (received > maximumResponse || total > maximumResponse) {
            *exceeded = true;
            if (reply)
                reply->abort();
        }
    });
    connect(reply, &QNetworkReply::finished, this,
            [this, self, reply, generation, url, attempt, redirects, bytes, exceeded, read] {
        if (!reply || !self || generation != mGeneration || !mBusy)
            return;
        read();
        if (!self || !reply || generation != mGeneration || !mBusy)
            return;
        mReply = nullptr;
        reply->deleteLater();
        if (*exceeded) {
            fail(generation, tr("The NOAA response exceeds the 2 MiB memory limit."));
            return;
        }
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const auto redirect = reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl();
        if (status >= 300 && status < 400 && !redirect.isEmpty()) {
            if (redirects >= maximumRedirects) {
                fail(generation, tr("The NOAA response exceeded the redirect limit."));
                return;
            }
            request(url.resolved(redirect), generation, attempt, redirects + 1);
            return;
        }
        const bool transient = status == 429 || status == 500 || status == 502 || status == 503 ||
                               status == 504 || reply->error() == QNetworkReply::TimeoutError ||
                               reply->error() == QNetworkReply::TemporaryNetworkFailureError;
        if (transient && attempt < maximumRetries) {
            bool numeric = false;
            const int header = reply->rawHeader("Retry-After").toInt(&numeric);
            const int seconds = numeric ? qBound(1, header, 5) : (1 << attempt);
            emit progress(tr("NOAA service busy; retrying in %1 seconds").arg(seconds));
            if (!self || generation != mGeneration)
                return;
            disconnect(&mRetry, nullptr, this, nullptr);
            connect(&mRetry, &QTimer::timeout, this, [this, url, generation, attempt, redirects] {
                request(url, generation, attempt + 1, redirects);
            });
            mRetry.start(seconds * 1000);
            return;
        }
        if (reply->error() != QNetworkReply::NoError || status < 200 || status >= 300) {
            fail(generation, tr("Live NOAA retrieval failed (HTTP %1): %2")
                                 .arg(status).arg(reply->errorString()));
            return;
        }
        QString error;
        FrequencyTable table;
        QList<CumulativeCurve> curves;
        const bool accepted = mKind == Kind::Frequency
            ? parseFrequencyResponse(*bytes, mQuery, &table, &error)
            : parseTemporalResponse(*bytes, mArea, mDuration, &curves, &error);
        bytes->clear();
        if (!accepted) {
            fail(generation, error);
            return;
        }
        const auto kind = mKind;
        if (kind == Kind::Frequency) {
            table.url = url;
            table.retrievedAt = QDateTime::currentDateTimeUtc();
        } else {
            for (auto &curve : curves)
                curve.source = url.toString();
        }
        mBusy = false;
        emit busyChanged(false);
        if (!self || generation != mGeneration)
            return;
        if (kind == Kind::Frequency)
            emit frequencyReady(table);
        else
            emit temporalReady(curves);
    });
}
}
