/**
 * @file NoaaClient.hpp
 * @brief Cancellable live NOAA Atlas 14 retrieval with session-only response data.
 * @details Uses the host network policy and GUI thread. Every request explicitly
 * bypasses disk caching. Parsed values are emitted as owning copies; raw bytes
 * are bounded and discarded after parsing. No files or settings are written.
 * @author Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license GPL-3.0-or-later
 */
#pragma once
#include "Precipitation.hpp"
#include <QObject>
#include <QPointer>
#include <QTimer>

class QNetworkReply;
namespace OpenSWMM::Sdk { class ExtensionHost; class INetworkPolicy; }

namespace OpenSWMM::Data
{
/** @brief Live in-memory point/temporal requests sharing the host's policy client. */
class NoaaClient final : public QObject
{
    Q_OBJECT
public:
    /** @brief Borrow an initialized host's policy and lifetime.
     * @param[in] host Extension host, guarded by its lifetime QObject.
     * @param[in] parent Optional QObject owner. */
    explicit NoaaClient(Sdk::ExtensionHost *host, QObject *parent = nullptr);
    /** @brief Inject the same policy dependency for independent consumer tests.
     * @param[in] network Borrowed policy-enforcing client.
     * @param[in] lifetime Required guard that outlives the policy.
     * @param[in] parent Optional QObject owner. */
    NoaaClient(Sdk::INetworkPolicy *network, QObject *lifetime, QObject *parent = nullptr);
    /** @brief Abort pending replies/retries without publishing another result. */
    ~NoaaClient() override;
    /** @brief Fetch a fresh point estimate, replacing a preceding active request.
     * @param[in] query Exact location and public PFDS options.
     * @param[out] error Optional immediate input/policy refusal detail.
     * @return True after acceptance; later completion uses signals. */
    bool fetchFrequency(const FrequencyQuery &query, QString *error = nullptr);
    /** @brief Fetch the selected official temporal product directly into memory.
     * @param[in] region NOAA region key, such as orb, sw or ne.
     * @param[in] area Explicit temporal area; never inferred from a state alone.
     * @param[in] durationMinutes Supported native duration: 360, 720, 1440 or 5760.
     * @param[out] error Optional immediate input/policy refusal detail.
     * @return True after acceptance; curves are emitted on completion. */
    bool fetchTemporal(const QString &region, const QString &area, int durationMinutes,
                       QString *error = nullptr);
    /** @brief Validate and form a temporal CSV URL from NOAA's published selector.
     * @param[in] region Official selector region key (California is sw).
     * @param[in] area Official selector area within that region.
     * @param[in] durationMinutes Native duration.
     * @param[out] error Optional unsupported-selector detail.
     * @return HTTPS NOAA URL, or empty when unsupported. */
    static QUrl temporalUrl(const QString &region, const QString &area, int durationMinutes,
                            QString *error = nullptr);
    /** @return True while a reply or bounded retry is pending. */
    bool isBusy() const { return mBusy; }
    /** @brief Abort current reply/retry; late callbacks cannot publish results. */
    void cancel();

signals:
    /** @brief Accepted canonical point values; request/retrieval time remain attached. */
    void frequencyReady(const OpenSWMM::Data::FrequencyTable &table);
    /** @brief Accepted patterns for the explicitly requested region/area/duration. */
    void temporalReady(const QList<OpenSWMM::Data::CumulativeCurve> &curves);
    /** @brief An accepted request failed; no earlier value is relabelled as current. */
    void failed(const QString &message);
    /** @brief Active request was cancelled or superseded, before another begins. */
    void cancelled();
    /** @brief Busy state changes on request acceptance and settlement. */
    void busyChanged(bool busy);
    /** @brief Short stage summary, without response payloads. */
    void progress(const QString &stage);

private:
    enum class Kind { Frequency, Temporal }; ///< Parser selected by accepted request type.
    /** @brief Validate policy and retire an older generation before acceptance. */
    bool start(const QUrl &url, Kind kind, QString *error);
    /** @brief Check NOAA origin, HTTPS and the live host's network policy. */
    bool permitted(const QUrl &url) const;
    /** @brief Start one bounded attempt, rechecking every redirect destination. */
    void request(const QUrl &url, quint64 generation, int attempt, int redirects);
    /** @brief Settle only the still-current accepted generation as failed. */
    void fail(quint64 generation, const QString &message);
    /** @brief Disconnect and retire the current reply before synchronous abort. */
    void abortReply();

    Sdk::INetworkPolicy *mNetwork = nullptr; ///< Borrowed only while mLifetime survives.
    QPointer<QObject> mLifetime; ///< Policy/host lifetime guard.
    QPointer<QNetworkReply> mReply; ///< Caller-owned current reply.
    QTimer mRetry; ///< Cancellable bounded backoff, owned by this client.
    quint64 mGeneration = 0; ///< Invalidates every superseded or cancelled callback.
    bool mBusy = false; ///< True for network work or waiting retry.
    Kind mKind = Kind::Frequency; ///< Immutable for one accepted request generation.
    FrequencyQuery mQuery; ///< Exact point options retained for canonical parsing.
    QString mArea; ///< Explicit temporal identity for source-labelled curves.
    int mDuration = 0; ///< Temporal native duration in minutes.
};
}
