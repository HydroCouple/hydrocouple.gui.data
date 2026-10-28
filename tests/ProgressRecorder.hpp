/**
 * @file ProgressRecorder.hpp
 * @brief Deterministic public progress capability for offline plugin acceptance.
 * @author Caleb Buahin <caleb.buahin@gmail.com>
 * @date 2026
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license GPL-3.0-or-later
 */
#pragma once
#include <sdk/IProgress.hpp>
#include <QHash>
#include <QPointer>
#include <QStringList>
#include <cmath>
#include <functional>

/** @brief Capture shared progress and its stage-deduplicated lifecycle logs without native GUI internals. */
class ProgressRecorder : public OpenSWMM::Sdk::IProgress
{
public:
    /** @brief Copied task evidence retained after completion. */
    struct Task {
        QString title; ///< Operation caption.
        QString stage; ///< Last stable stage.
        QPointer<QObject> context; ///< Receiver lifetime.
        QList<double> fractions; ///< Accepted measurable/indeterminate updates.
        bool finished = false; ///< Terminal outcome already delivered.
        OpenSWMM::Sdk::ProgressOutcome outcome = OpenSWMM::Sdk::ProgressOutcome::Cancelled; ///< Terminal classification.
        QString detail; ///< Final result text.
    };
    /** @brief Borrow the QObject receiving context-destruction cleanup.
     * @param[in] owner Host fake that outlives its contexts or disconnects on destruction. */
    explicit ProgressRecorder(QObject *owner) : mOwner(owner) {}
    /** @brief Record one initial indeterminate operation.
     * @param[in] title Operation caption.
     * @param[in] context Required receiving lifetime.
     * @return New token, or null without a live context/owner. */
    QUuid beginProgress(const QString &title, QObject *context) override
    {
        if (!mOwner || !context || title.isEmpty()) return {};
        const auto id = QUuid::createUuid();
        tasks.insert(id, Task{title, {}, context, {-1.}, false, {}, {}});
        events.append(title + ": started");
        QObject::connect(context, &QObject::destroyed, mOwner, [this, id] {
            finishProgress(id, OpenSWMM::Sdk::ProgressOutcome::Cancelled, "Context closed");
        });
        publish();
        return id;
    }
    /** @brief Record numeric updates while logging only distinct stage labels.
     * @param[in] id Existing active token.
     * @param[in] fraction Finite progress, normalized like the SDK host.
     * @param[in] stage Stable plain phase.
     * @return False for unknown/finished/nonfinite updates. */
    bool updateProgress(const QUuid &id, double fraction, const QString &stage) override
    {
        auto it = tasks.find(id);
        if (it == tasks.end() || it->finished || !std::isfinite(fraction)) return false;
        it->fractions.append(fraction < 0 ? -1. : qBound(0., fraction, 1.));
        if (!stage.isEmpty() && stage != it->stage) events.append(it->title + ": " + stage);
        it->stage = stage;
        publish();
        return true;
    }
    /** @brief Finish once while retaining all evidence.
     * @param[in] id Existing active token.
     * @param[in] outcome Terminal classification.
     * @param[in] detail Result or diagnostic.
     * @return False for unknown/already finished tokens. */
    bool finishProgress(const QUuid &id, OpenSWMM::Sdk::ProgressOutcome outcome,
                        const QString &detail = {}) override
    {
        auto it = tasks.find(id);
        if (it == tasks.end() || it->finished) return false;
        it->finished = true; it->outcome = outcome; it->detail = detail;
        events.append(it->title + ": finished: " + detail);
        publish();
        return true;
    }
    /** @return Number of outstanding scoped operations. */
    int activeProgress() const
    {
        int count = 0;
        for (const auto &task : tasks) if (!task.finished) ++count;
        return count;
    }
    QHash<QUuid, Task> tasks; ///< All accepted operation records for assertions.
    QStringList events; ///< Simulated native lifecycle/stage log entries.
    std::function<void()> publishing; ///< Optional synchronous host-publication reentry hook.
private:
    /** @brief Invoke a copied reentry hook so deleting its receiver cannot invalidate the call. */
    void publish() { const auto callback = publishing; if (callback) callback(); }
    QPointer<QObject> mOwner; ///< Borrowed fake host lifetime, never owns test widgets.
};
