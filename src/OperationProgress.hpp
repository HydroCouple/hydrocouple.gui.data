/**
 * @file OperationProgress.hpp
 * @brief Scoped GUI-thread progress for plugin operations outside host worker jobs.
 * @author Caleb Buahin <caleb.buahin@gmail.com>
 * @date 2026
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license GPL-3.0-or-later
 */
#pragma once
#include <sdk/ExtensionHost.hpp>
#include <sdk/IProgressV1.hpp>
#include <QPointer>
#include <utility>

namespace OpenSWMM::Data
{
/** @brief One optional shared progress task, independently fenced by host and caller lifetimes.
 * @details Calls remain on the GUI thread. Host worker jobs retain their own progress; this
 * wrapper represents their enclosing network/review/export operation and never calls workers.
 * Finishing consumes the token before notifying the host, so repeated cancellation is inert. */
class OperationProgress final
{
public:
    /** @brief Borrow the host and context without starting work.
     * @param[in] host SDK facade used only while its lifetime survives.
     * @param[in] context QObject owning this operation and its cancellation lifetime. */
    OperationProgress(Sdk::ExtensionHost *host, QObject *context)
        : mHost(host), mLifetime(host ? host->lifetime() : nullptr), mContext(context)
    {
    }
    /** @brief Finish any still-active task as cancelled before releasing its owning state. */
    ~OperationProgress() { finish(Sdk::ProgressOutcome::Cancelled, QObject::tr("Operation closed.")); }
    /** @brief Begin a fresh task, cancelling any superseded task first.
     * @param[in] title User-facing operation caption. */
    void begin(const QString &title)
    {
        const auto context = mContext;
        const auto lifetime = mLifetime;
        const auto epoch = mEpoch + 1;
        finish(Sdk::ProgressOutcome::Cancelled, QObject::tr("Replaced by a new operation."));
        if (!context || !lifetime)
            return;
        if (mEpoch != epoch)
            return; // A newer reentrant operation already superseded this start.
        if (auto *service = qobject_cast<Sdk::IProgressV1 *>(lifetime.data())) {
            const auto id = service->beginProgress(title, context);
            // Native Logs/status publication can synchronously delete the owning
            // context and this embedded wrapper. Only local guards survive it.
            if (context && lifetime) {
                if (mEpoch == epoch)
                    mId = id;
                else if (!id.isNull())
                    service->finishProgress(id, Sdk::ProgressOutcome::Cancelled,
                                            QObject::tr("Cancelled while starting."));
            }
        }
    }
    /** @brief Publish current work without creating a log entry for every byte count.
     * @param[in] fraction Finite negative for indeterminate work, otherwise 0..1.
     * @param[in] stage Plain operation phase; repeated phases are deduplicated by the host. */
    void update(double fraction, const QString &stage)
    {
        if (auto *service = progress(); service && !mId.isNull())
            service->updateProgress(mId, fraction, stage);
    }
    /** @brief Close this task exactly once; host shutdown may already have closed it.
     * @param[in] outcome Success, failure, or explicit cancellation.
     * @param[in] detail Final user-facing result or diagnostic. */
    void finish(Sdk::ProgressOutcome outcome, const QString &detail = {})
    {
        ++mEpoch;
        const auto id = std::exchange(mId, QUuid{});
        if (auto *service = progress(); service && !id.isNull())
            service->finishProgress(id, outcome, detail);
    }
    /** @brief Record an operation event that does not require an active progress token.
     * @param[in] level Existing SDK console severity, such as info, warning, or error.
     * @param[in] message Plain context and result; never credentials or request headers. */
    void log(const QString &level, const QString &message) const
    {
        if (mLifetime && mHost)
            if (auto *console = mHost->console())
                console->append(level, message);
    }
private:
    /** @return Optional live shared progress service; null after host destruction. */
    Sdk::IProgressV1 *progress() const
    {
        return mLifetime ? qobject_cast<Sdk::IProgressV1 *>(mLifetime.data()) : nullptr;
    }
    Sdk::ExtensionHost *mHost; ///< Borrowed SDK facade, valid only while mLifetime survives.
    QPointer<QObject> mLifetime; ///< Host guard; workers never use this wrapper.
    QPointer<QObject> mContext; ///< Caller lifetime used by the host to cancel orphaned tasks.
    QUuid mId; ///< Current opaque token; null after terminal publication.
    quint64 mEpoch = 0; ///< Cancellation/replacement intent, including calls before begin returns a token.
};
}
