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
#include <sdk/IProgress.hpp>
#include <sdk/IToolRuns.hpp>
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
        if (!mParentRun.isNull())
            return; // The panel owns settlement across download and native import.

        const auto context = mContext;
        const auto lifetime = mLifetime;
        const auto epoch = mEpoch + 1;
        finish(Sdk::ProgressOutcome::Cancelled, QObject::tr("Replaced by a new operation."));
        if (!context || !lifetime)
            return;
        if (mEpoch != epoch)
            return; // A newer reentrant operation already superseded this start.
        if (!mRequest.toolId.isEmpty()) {
            if (auto *service = runs()) {
                auto request = mRequest;
                request.title = title;
                const auto id = service->beginToolRun(request, context, mCancel);
                if (context && lifetime && mEpoch == epoch)
                    mId = id;
                else if (context && lifetime && !id.isNull())
                    service->finishToolRun(id, Sdk::ToolRunOutcome::Cancelled);
            }
            return;
        }
        if (auto *service = qobject_cast<Sdk::IProgress *>(lifetime.data())) {
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
        const auto run = toolRunId();
        if (auto *service = runs(); service && !run.isNull())
            service->updateToolRun(run, fraction, stage);
        else if (auto *service = progress(); service && !mId.isNull())
            service->updateProgress(mId, fraction, stage);
    }
    /** @brief Close this task exactly once; host shutdown may already have closed it.
     * @param[in] outcome Success, failure, or explicit cancellation.
     * @param[in] detail Final user-facing result or diagnostic. */
    void finish(Sdk::ProgressOutcome outcome, const QString &detail = {})
    {
        if (!mParentRun.isNull())
            return;
        ++mEpoch;
        const auto id = std::exchange(mId, QUuid{});
        if (!mRequest.toolId.isEmpty()) {
            if (auto *service = runs(); service && !id.isNull())
                service->finishToolRun(id, outcome == Sdk::ProgressOutcome::Succeeded
                                              ? Sdk::ToolRunOutcome::Succeeded
                                          : outcome == Sdk::ProgressOutcome::Cancelled
                                              ? Sdk::ToolRunOutcome::Cancelled
                                              : Sdk::ToolRunOutcome::Failed,
                                       detail);
        } else if (auto *service = progress(); service && !id.isNull())
            service->finishProgress(id, outcome, detail);
    }
    /** @brief Record an operation event that does not require an active progress token.
     * @param[in] level Existing SDK console severity, such as info, warning, or error.
     * @param[in] message Plain context and result; never credentials or request headers. */
    void log(const QString &level, const QString &message) const
    {
        const auto run = toolRunId();
        if (auto *service = runs(); service && !run.isNull())
            service->appendToolMessage(run, level, message);
        else if (mLifetime && mHost)
            if (auto *console = mHost->console())
                console->append(level, message);
    }
    /** @brief Configure future independent runs with reviewed context and cancellation.
     * @param[in] request Registered tool identity and exact reviewed session/generation.
     * @param[in] cancel Short GUI-thread controller for this panel's active work. */
    void configureToolRun(const Sdk::ToolRunRequest &request, std::function<void()> cancel)
    {
        mRequest = request;
        mCancel = std::move(cancel);
    }
    /** @brief Report a stage of a parent run; its controller owns final settlement.
     * @param[in] id Host-issued parent execution, or null for legacy progress. */
    void attachToolRun(const QUuid &id) { mParentRun = id; }
    /** @return Current parent/owned tool run, or null for legacy progress. */
    QUuid toolRunId() const { return mParentRun.isNull() && !mRequest.toolId.isEmpty() ? mId : mParentRun; }
    /** @brief Publish a small result before settling an owned preview run.
     * @param[in] result Copied value or completed asset reference. */
    void addResult(const Sdk::ToolResult &result)
    {
        if (auto *service = runs(); service && !toolRunId().isNull())
            service->addToolResult(toolRunId(), result);
    }
    /** @brief Submit a child worker under the parent before its first event.
     * @param[in] title User-facing stage caption.
     * @param[in] work Data-only cancellable worker callback.
     * @param[in] finished Optional GUI-thread completion receiver.
     * @return Borrowed cancellable host job, or null for an unavailable host/run. */
    Sdk::IJob *runJob(const QString &title, std::function<void(Sdk::IJobContext &)> work,
                     std::function<void(bool, const QString &)> finished = {})
    {
        if (!mLifetime || !mHost)
            return nullptr;
        if (auto *service = runs(); service && !toolRunId().isNull())
            return service->runToolJob(toolRunId(), title, std::move(work), std::move(finished));
        return mHost->runJob(title, std::move(work), std::move(finished));
    }
private:
    /** @return Optional live tool service, scoped by the borrowed host. */
    Sdk::IToolRuns *runs() const
    {
        return mLifetime ? qobject_cast<Sdk::IToolRuns *>(mLifetime.data()) : nullptr;
    }
    /** @return Optional live shared progress service; null after host destruction. */
    Sdk::IProgress *progress() const
    {
        return mLifetime ? qobject_cast<Sdk::IProgress *>(mLifetime.data()) : nullptr;
    }
    Sdk::ExtensionHost *mHost; ///< Borrowed SDK facade, valid only while mLifetime survives.
    QPointer<QObject> mLifetime; ///< Host guard; workers never use this wrapper.
    QPointer<QObject> mContext; ///< Caller lifetime used by the host to cancel orphaned tasks.
    QUuid mId; ///< Current opaque progress/owned run token; null after settlement.
    QUuid mParentRun; ///< Externally settled execution spanning payload and import.
    Sdk::ToolRunRequest mRequest; ///< Context copied at preview acceptance.
    std::function<void()> mCancel; ///< Guarded by the context supplied to the host.
    quint64 mEpoch = 0; ///< Cancellation/replacement intent, including calls before begin returns a token.
};
}
