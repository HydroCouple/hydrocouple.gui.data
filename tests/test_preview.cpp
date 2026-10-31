/**
 * @file test_preview.cpp
 * @brief Offline public-SDK acceptance of review authority, retained gages and attribute filtering.
 * @details A copied-state host holds notifications and worker completions independently.
 * Tests exercise the actual panel through ordinary controls and saved catalogues,
 * including labeled map populations, full property inspection, filtered export identity,
 * and delayed native invalidation. No native GUI headers, engine handles, remote services
 * or temporary files are needed.
 * @author Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license GPL-3.0-or-later
 */
#include "PreviewPanel.hpp"
#include "ProgressRecorder.hpp"
#include "PayloadDownload.hpp"
#include <sdk/ISessionEvents.hpp>
#include <sdk/IViewEvents.hpp>
#include <sdk/IActivityEvents.hpp>
#include <sdk/IViewControls.hpp>
#include <sdk/IPreviewLayers.hpp>
#include <sdk/ITimeSeriesLayers.hpp>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDateEdit>
#include <QDockWidget>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPushButton>
#include <QScopeGuard>
#include <QSet>
#include <QSignalSpy>
#include <QTabBar>
#include <QTableView>
#include <QTest>
#include <QThread>
#include <QTimer>
#include <QToolButton>
#include <QTreeView>
#include <QUrlQuery>
#include <QVBoxLayout>
#include <algorithm>
#include <cstring>
#include <memory>
#include <limits>
#include <vector>
#include <map>

using namespace OpenSWMM::Sdk;
using OpenSWMM::Data::PreviewPanel;

namespace
{
/** @brief Produce a small exact WGS84 review polygon, optionally moved outside the catalogue. */
QJsonObject polygon(bool distant = false)
{
    const double x = distant ? -60. : -77.;
    return {{"type", "Polygon"},
            {"coordinates", QJsonArray{QJsonArray{QJsonArray{x, 40.}, QJsonArray{x + 1., 40.},
                                                  QJsonArray{x + 1., 41.}, QJsonArray{x, 41.},
                                                  QJsonArray{x, 40.}}}}};
}
/** @brief Produce one recorded monitoring location inside the ordinary review area. */
QByteArray catalogue()
{
    return QJsonDocument(
               QJsonObject{
                   {"type", "FeatureCollection"},
                   {"features",
                    QJsonArray{QJsonObject{
                        {"type", "Feature"},
                        {"id", "site-one"},
                        {"properties", QJsonObject{{"monitoring_location_name", "Site one"}}},
                        {"geometry", QJsonObject{{"type", "Point"},
                                                 {"coordinates", QJsonArray{-76.5, 40.5}}}}}}}})
        .toJson();
}

/** @brief Retain heterogeneous saved monitoring attributes, including numeric strings and JSON. */
/** @brief Build a USGS time-series-metadata page of streamflow records.
 * @param[in] rows Location ID, begin and end timestamps, as the service publishes them.
 * @param[in] interval `Daily` or `Points`. */
QByteArray recordPeriods(const QList<QStringList> &rows, const QString &interval = "Daily")
{
    QJsonArray features;
    for (const auto &row : rows)
        features.append(QJsonObject{{"type", "Feature"},
                                    {"properties",
                                     QJsonObject{{"monitoring_location_id", row[0]},
                                                 {"parameter_code", "00060"},
                                                 {"computation_period_identifier", interval},
                                                 {"begin", row[1]},
                                                 {"end", row[2]}}}});
    return QJsonDocument(QJsonObject{{"type", "FeatureCollection"}, {"features", features}})
        .toJson();
}
QByteArray richCatalogue()
{
    const auto site = [](const QString &id, const QString &title, QJsonObject properties, double x,
                         double y) {
        properties["monitoring_location_name"] = title;
        return QJsonObject{
            {"type", "Feature"},
            {"id", id},
            {"properties", properties},
            {"geometry", QJsonObject{{"type", "Point"}, {"coordinates", QJsonArray{x, y}}}}};
    };
    return QJsonDocument(
               QJsonObject{
                   {"type", "FeatureCollection"},
                   {"features",
                    QJsonArray{site("site-one", "Alpha River",
                                    {{"site_type", "ST"},
                                     {"drainage_area", 12.5},
                                     {"agency", "USGS"},
                                     {"only_alpha", "recorded"},
                                     {"nullable", QJsonValue(QJsonValue::Null)},
                                     {"quality", QJsonObject{{"approved", true}}}},
                                    -76.5, 40.5),
                               site("site-two", "Beta Creek",
                                    {{"site_type", "GW"},
                                     {"drainage_area", "25"},
                                     {"agency", "USGS"},
                                     {"measurements", QJsonArray{"flow", "stage"}}},
                                    -76.4, 40.6),
                               site("site-three", "Gamma Lake",
                                    {{"site_type", "LK"}, {"drainage_area", 0.}, {"agency", "EPA"}},
                                    -76.3, 40.7)}}})
        .toJson();
}

/** @brief A reply whose successful bytes are published only when the fixture explicitly completes it. */
class Reply final : public QNetworkReply
{
public:
    /** @brief Retain a request and synthetic bytes without performing network I/O. */
    Reply(const QNetworkRequest &request, QByteArray body, QObject *parent)
        : QNetworkReply(parent), mBody(std::move(body))
    {
        setRequest(request);
        setUrl(request.url());
        setOperation(QNetworkAccessManager::GetOperation);
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, 200);
        open(QIODevice::ReadOnly | QIODevice::Unbuffered);
    }
    /** @brief Announce the already copied successful response. */
    void complete()
    {
        if (isFinished())
            return;
        emit readyRead();
        setFinished(true);
        emit finished();
    }
    /** @brief Finish as a service failure without delivering the body. */
    void fail(const QString &message)
    {
        if (isFinished())
            return;
        setError(QNetworkReply::ServiceUnavailableError, message);
        setFinished(true);
        emit errorOccurred(QNetworkReply::ServiceUnavailableError);
        emit finished();
    }
    /** @brief Cooperatively settle an aborted response without further completion delivery. */
    void abort() override
    {
        setError(QNetworkReply::OperationCanceledError, "Cancelled");
        setFinished(true);
    }
    /** @brief Return the unread synthetic body length. */
    qint64 bytesAvailable() const override
    {
        return mBody.size() - mOffset + QIODevice::bytesAvailable();
    }

protected:
    /** @brief Copy only the requested unread body bytes. */
    qint64 readData(char *destination, qint64 maximum) override
    {
        const auto size = qMin(maximum, qint64(mBody.size()) - mOffset);
        if (size <= 0)
            return -1;
        std::memcpy(destination, mBody.constData() + mOffset, size_t(size));
        mOffset += size;
        return size;
    }

private:
    QByteArray mBody;   ///< Saved response copied into this synthetic reply.
    qint64 mOffset = 0; ///< Current read cursor.
};
/** @brief Restrict the panel's policy-client requests to controlled in-memory replies. */
class Network final : public QNetworkAccessManager, public INetworkPolicy
{
public:
    int requestCount = 0; ///< Actual network requests observed by the controlled policy client.
    QPointer<Reply>
        reply; ///< Latest synthetic reply, guarded through cancellation/deferred deletion.
    int periodRequests = 0;     ///< Record-period metadata requests, counted separately.
    QPointer<Reply> periodReply; ///< Latest record-period reply.
    QByteArray periodBody = R"({"type":"FeatureCollection","features":[]})"; ///< Its body.
    bool allowed(const QUrl &) const override { return true; }
    QNetworkAccessManager *shared() override { return this; }

protected:
    QNetworkReply *createRequest(Operation, const QNetworkRequest &request, QIODevice *) override
    {
        if (request.url().path().contains("time-series-metadata")) {
            ++periodRequests;
            periodReply = new Reply(request, periodBody, this);
            return periodReply;
        }
        ++requestCount;
        reply = new Reply(request, catalogue(), this);
        return reply;
    }
};

class Model;
/** @brief Value-backed project geometry and observable overlay/export side effects. */
class Project final : public IProject
{
public:
    Model *model = nullptr; ///< Borrowed associated test model.
    QString folder;         ///< Retained case directory containing every written test artifact.
    QString projectCrs = "EPSG:4326"; ///< Canvas/model coordinate-reference metadata.
    QJsonObject viewport = polygon(); ///< Current authoritative viewport boundary.
    QJsonObject boundary = polygon(); ///< Current authoritative chosen feature geometry.
    QList<QVariantMap> features{
        {{"id", qint64(1)}, {"title", "Boundary one"}, {"selected", true}},
        {{"id", qint64(2)}, {"title", "Boundary two"}, {"selected", false}}};
    bool boundaryPresent = true;        ///< Current source membership.
    QString boundaryTitle = "Boundary"; ///< Current source caption.
    QJsonObject overlay;                ///< Latest visible transient preview.
    QStringList rasters; ///< Paths of independently added native rasters, never preview-owned.
    QStringList exports;                ///< Paths added as ordinary persistent feature layers.
    int overlayWrites = 0;              ///< Count only successful nonempty preview publications.
    std::function<void()>
        previewChanged; ///< Optional native-layer mutation after changed preview content.
    QUuid previewAttachment;       ///< Native preview layer, replaced when its features change.
    QList<qint64> previewFids;     ///< Native FIDs in feature order; deliberately not 0..n-1.
    QSet<qint64> previewSelection; ///< Native preview-layer selection.
    QUuid id() const override;
    QString path() const override { return folder + "/model.oswp"; }
    QString directory() const override { return folder; }
    QList<IModelInstance *> instances() const override;
    QJsonObject extensionData() const override { return {}; }
    void setExtensionData(const QJsonObject &) override {}
    QString autosave(QString *) const override { return {}; }
    QString crsWkt() const override { return projectCrs; }
    QString crsAuthority() const override { return "EPSG:4326"; }
    bool toLonLat(QPointF value, QPointF *target) const override
    {
        *target = value;
        return true;
    }
    bool fromLonLat(QPointF value, QPointF *target) const override
    {
        *target = value;
        return true;
    }
    QList<QVariantMap> layers() const override
    {
        return boundaryPresent ? QList<QVariantMap>{{{"id", "boundary"},
                                                     {"title", boundaryTitle},
                                                     {"visible", true}}}
                               : QList<QVariantMap>{};
    }
    QString addRasterLayer(const QString &path, const QString &, QString *) override
    {
        rasters.append(path);
        return "raster-" + QString::number(rasters.size());
    }
    QString addFeatureLayer(const QString &file, const QString &, QString *) override
    {
        exports.append(file);
        return "export-" + QString::number(exports.size());
    }
    QJsonObject viewportBoundary(QString *) const override { return viewport; }
    QList<QVariantMap> polygonLayers() const override
    {
        return boundaryPresent ? QList<QVariantMap>{{{"id", "boundary"},
                                                     {"title", boundaryTitle},
                                                     {"count", 2}}}
                               : QList<QVariantMap>{};
    }
    QList<QVariantMap> boundaryFeatures(const QString &id, bool selectedOnly, int offset,
                                        int limit) const override
    {
        QList<QVariantMap> result;
        if (!boundaryPresent || id != "boundary")
            return result;
        for (const auto &feature : features)
            if (!selectedOnly || feature.value("selected").toBool())
                result.append(feature);
        return result.mid(offset, qMin(limit, 5000));
    }
    QJsonObject boundaryPolygon(const QString &id, qint64 fid, QString *) const override
    {
        if (boundaryPresent && id == "boundary")
            for (const auto &feature : features)
                if (feature.value("id").toLongLong() == fid)
                    return boundary;
        return {};
    }
    bool showPreview(const QJsonObject &value, QString *) override
    {
        const bool changed = overlay != value;
        overlay = value;
        ++overlayWrites;
        if (changed && value["previewStyle"].toObject()["asFeatureLayer"].toBool()) {
            // Like GDAL with string IDs, FIDs need not equal feature positions.
            previewAttachment = QUuid::createUuid();
            previewFids.clear();
            for (qsizetype i = 0; i < value["features"].toArray().size(); ++i)
                previewFids.append(500 - 7 * i);
            previewSelection.clear();
        }
        if (changed && previewChanged)
            previewChanged();
        return true;
    }
    void clearPreview() override
    {
        overlay = {};
        previewAttachment = {};
        previewFids.clear();
        previewSelection.clear();
    }
};

/** @brief Public model facade with a separate QObject lifetime and persistent identity. */
class Model final : public QObject, public IModelInstance
{
public:
    QUuid persistentId = QUuid::createUuid(); ///< May intentionally match another open model.
    QString input;                            ///< Distinct retained model path.
    bool open = true;     ///< Native availability, independent of copied load readiness.
    Project projectValue; ///< Project owned by this facade.
    QUuid id() const override { return persistentId; }
    QString title() const override { return QFileInfo(input).baseName(); }
    QString inpPath() const override { return input; }
    QString engineVersion() const override { return "6.0.0"; }
    QObject *lifetime() const override { return const_cast<Model *>(this); }
    IProject *project() const override { return const_cast<Project *>(&projectValue); }
    bool isOpen() const override { return open; }
    QList<QVariantMap> selection() const override { return {}; }
    void select(const QList<QVariantMap> &) override {}
    QString snapshot(QString *) const override { return {}; }
    QVariantMap attributes(const QString &, const QString &) const override { return {}; }
    IAttributeProvider *attributeProvider() const override { return nullptr; }
    QList<QVariantMap> results() const override { return {}; }
    std::unique_ptr<IEngineLease> acquireLease(LeaseKind, QString *) override { return {}; }
    std::unique_ptr<INativeEditScope> nativeEdit(const QString &, QString *) override { return {}; }
    std::unique_ptr<IEditScope> edit(const QString &, QString *) override { return {}; }
    bool registerAttributeProvider(const QString &, IComputedAttributeProvider *, QObject *,
                                   QString *) override
    {
        return false;
    }
    void unregisterAttributeProvider(const QString &) override {}
    void refreshAttributeProvider(const QString &) override {}
};
QUuid Project::id() const
{
    return model->id();
}
QList<IModelInstance *> Project::instances() const
{
    return model->isOpen() ? QList<IModelInstance *>{model} : QList<IModelInstance *>{};
}

/** @brief Completed data-only worker with deliberately delayed GUI completion. */
class Job final : public IJob, public IJobContext
{
public:
    bool cancelled = false; ///< A late cancel does not erase the already produced worker result.
    bool delivered = false; ///< Prevent duplicate fixture completion.
    std::function<void(bool, const QString &)> finished; ///< Held GUI continuation.
    void cancel() override { cancelled = true; }
    bool isFinished() const override { return true; }
    bool isCancelled() const override { return cancelled; }
    void setProgress(double, const QString &) override {}
    void postToMainThread(std::function<void()> callback) override { callback(); }
    /** @brief Deliver a successful latched result, including when cancellation raced after worker return. */
    void complete()
    {
        if (!delivered) {
            delivered = true;
            if (finished)
                finished(true, {});
        }
    }
};

/** @brief Installed-SDK-only host whose authoritative mutations precede explicit notification delivery. */
class Host : public QObject,
                   public ExtensionHost,
                   public ISessionEvents,
                   public IViewEvents,
                   public IActivityEvents,
                   public IViewControls,
                   public IPreviewLayers,
                   public ITimeSeriesLayers,
                   public ProgressRecorder,
                   public ISettings,
                   public IConsole,
                   public IUiHost
{
    Q_OBJECT
    Q_INTERFACES(
        OpenSWMM::Sdk::ISessionEvents OpenSWMM::Sdk::IViewEvents OpenSWMM::Sdk::IActivityEvents
            OpenSWMM::Sdk::IViewControls OpenSWMM::Sdk::IPreviewLayers
                OpenSWMM::Sdk::ITimeSeriesLayers OpenSWMM::Sdk::IProgress)
public:
    /** @brief Bind the fake scoped progress capability to this host lifetime. */
    Host() : ProgressRecorder(this) {}
    /** @brief One copied source session and its independently owned model facade. */
    struct Entry {
        std::unique_ptr<Model> model;           ///< Native facade substitute.
        ModelSessionState state;                ///< Authoritative current session state.
        ViewSnapshot view;                      ///< Authoritative copied attachment metadata.
        QUuid documentId = QUuid::createUuid(); ///< Canonical mounted document identity.
    };
    std::vector<std::unique_ptr<Entry>> entries; ///< Stable source lifetimes.
    Entry *active = nullptr;  ///< Cached active model, retained while a utility view has focus.
    UiActivityState activity; ///< Actual document focus, separate from active model context.
    QUuid utilityDocumentId = QUuid::createUuid(); ///< Independent model-free attachment.
    quint64 sequence = 10;                         ///< Shared session/view/activity watermark.
    bool stopping = false;       ///< End source queries and future observation delivery.
    int legacySubscriptions = 0; ///< Migration must not use the string notification service.
    std::vector<std::unique_ptr<Job>> jobs; ///< Retained borrowed worker handles.
    QList<StateEvent> pendingState;         ///< Committed but not yet delivered lifecycle changes.
    QList<ViewEvent> pendingViews;          ///< Committed but not yet delivered view changes.
    QList<ActivityEvent> pendingActivity;   ///< Committed but not yet delivered focus changes.
    Network policy;                         ///< Controlled successful policy client.
    std::function<void()>
        jobCreated;       ///< Optional synchronous lifetime mutation before runJob returns.
    QStringList messages; ///< Plain diagnostics for inspection.
    int selectionCommands = 0; ///< Accepted native preview-selection replacements.

    QList<TimeSeriesImportRequest> imports; ///< Complete descriptor publication attempts.
    std::function<void(const TimeSeriesImportResult &)> importFinished; ///< Held native completion.
    /** @brief Retain a cancellable fake native import for lifecycle assertions. */
    IJob *importTimeSeries(const TimeSeriesImportRequest &request, QObject *context,
                           std::function<void(const TimeSeriesImportResult &)> finished,
                           QString *error) override
    {
        if (error)
            error->clear();
        imports.append(request);
        const QPointer<QObject> guard(context);
        importFinished = [guard, finished](const TimeSeriesImportResult &result) {
            if (guard)
                finished(result);
        };
        return runJob("Import observations", [](IJobContext &) {}, {});
    }
    /** @brief Native collection listing is unused by the download panel. */
    QList<TimeSeriesLayerInfo> timeSeriesLayers(const QUuid &) const override { return {}; }

    /** @brief Add an independent Ready model with a chosen persistent identity. */
    Entry *addModel(const QString &directory, const QUuid &persistent = QUuid::createUuid())
    {
        auto item = std::make_unique<Entry>();
        item->model = std::make_unique<Model>();
        item->model->persistentId = persistent;
        item->model->input = directory + "/model.inp";
        item->model->projectValue.model = item->model.get();
        item->model->projectValue.folder = directory;
        QDir().mkpath(directory);
        item->state.sessionId = QUuid::createUuid();
        item->state.instanceId = persistent;
        item->state.loadGeneration = 1;
        item->state.stateRevision = 1;
        item->state.status = SessionStatus::Ready;
        item->state.title = item->model->title();
        item->state.inputPath = item->model->inpPath();
        LayerViewState layer;
        layer.layerSessionId = QUuid::createUuid();
        layer.layerId = "boundary";
        layer.title = "Boundary";
        layer.type = "Vector";
        layer.crsWkt = "EPSG:4326";
        layer.visible = true;
        layer.vector = true;
        layer.featureCount = 2;
        layer.selectionCount = 1;
        item->view.layers.append(layer);
        auto *result = item.get();
        entries.push_back(std::move(item));
        activity.documents.append({result->documentId,
                                   DocumentKind::Model,
                                   result->state.sessionId,
                                   {},
                                   result->state.title,
                                   false});
        if (!active)
            activate(result, false);
        return result;
    }
    /** @brief Change actual focus while optionally publishing its deferred typed invalidation. */
    void activate(Entry *entry, bool publish = true)
    {
        if (entry)
            active = entry;
        activity.activeDocumentId = entry ? entry->documentId : utilityDocumentId;
        if (!entry && std::none_of(activity.documents.cbegin(), activity.documents.cend(),
                                   [this](const auto &document) {
                                       return document.documentId == utilityDocumentId;
                                   }))
            activity.documents.append(
                {utilityDocumentId, DocumentKind::Utility, {}, {}, "Utility document", false});
        for (auto &document : activity.documents)
            document.active = document.documentId == activity.activeDocumentId;
        if (publish) {
            ActivityEvent event;
            event.kind = ActivityEventKind::ActiveDocumentChanged;
            event.sequence = ++sequence;
            event.windowId = activity.windowId;
            event.entityId = activity.activeDocumentId;
            pendingActivity.append(event);
        }
    }
    /** @brief Commit a lifecycle/revision mutation independently of its later observation. */
    void stateChange(Entry *entry, StateChanges changes,
                     StateEventKind kind = StateEventKind::StateChanged)
    {
        ++entry->state.stateRevision;
        pendingState.append({kind, ++sequence, entry->state, changes, {}});
    }
    /** @brief Commit one attachment/session view reason, retaining its exact runtime identity. */
    void viewChange(Entry *entry, ViewEventKind kind, const QUuid &layer = {})
    {
        ++entry->state.stateRevision;
        ViewEvent event;
        event.kind = kind;
        event.sequence = ++sequence;
        event.session = entry->state;
        event.layerSessionId = layer;
        for (const auto &value : entry->view.layers)
            if (value.layerSessionId == layer)
                event.layerId = value.layerId;
        pendingViews.append(event);
    }
    /** @brief Deliver one state observation, optionally including a nested posted-callback turn.
     * @param[in] processPosted True models a later observer entering a nested event loop. */
    void drainFirstState(bool processPosted = true)
    {
        if (!pendingState.isEmpty())
            emit stateCommitted(pendingState.takeFirst());
        if (processPosted)
            QCoreApplication::processEvents();
    }
    /** @brief Deliver held observations in their actual shared source order, then deferred rebases. */
    void drain()
    {
        while (!pendingState.isEmpty() || !pendingViews.isEmpty() || !pendingActivity.isEmpty()) {
            const auto a = pendingState.isEmpty() ? std::numeric_limits<quint64>::max()
                                                  : pendingState.first().sequence;
            const auto b = pendingViews.isEmpty() ? std::numeric_limits<quint64>::max()
                                                  : pendingViews.first().sequence;
            const auto c = pendingActivity.isEmpty() ? std::numeric_limits<quint64>::max()
                                                     : pendingActivity.first().sequence;
            if (a <= b && a <= c)
                emit stateCommitted(pendingState.takeFirst());
            else if (b <= c)
                emit viewCommitted(pendingViews.takeFirst());
            else
                emit activityCommitted(pendingActivity.takeFirst());
        }
        QCoreApplication::processEvents();
    }
    CompatibilityManifest manifest() const override { return {}; }
    QString pluginId() const override { return "openswmm.gui.data"; }
    QObject *lifetime() const override { return const_cast<Host *>(this); }
    QList<IProject *> projects() const override
    {
        QList<IProject *> result;
        for (const auto &entry : entries)
            if (entry->model->isOpen())
                result.append(entry->model->project());
        return result;
    }
    IProject *activeProject() const override { return active ? active->model->project() : nullptr; }
    QList<IModelInstance *> instances() const override
    {
        QList<IModelInstance *> result;
        for (const auto &entry : entries)
            if (entry->model->isOpen())
                result.append(entry->model.get());
        return result;
    }
    IModelInstance *activeInstance() const override
    {
        return active ? active->model.get() : nullptr;
    }
    IUiHost *ui() override { return this; }
    ISettings *settings() override { return this; }
    IConsole *console() override { return this; }
    INetworkPolicy *network() override { return &policy; }
    IRunRegistry *runRegistry() override { return nullptr; }
    IPlotService *plots() override { return nullptr; }
    IDialogs *dialogs() override { return nullptr; }
    bool isDark() const override { return false; }
    QColor themeColor(QPalette::ColorRole) const override { return {}; }
    QMetaObject::Connection
    observe(QObject *, std::function<void(const QString &, const QVariantMap &)>) override
    {
        ++legacySubscriptions;
        return {};
    }
    IJob *runJob(const QString &, std::function<void(IJobContext &)> work,
                 std::function<void(bool, const QString &)> finished) override
    {
        if (stopping)
            return nullptr;
        auto job = std::make_unique<Job>();
        job->finished = std::move(finished);
        work(*job);
        auto *result = job.get();
        jobs.push_back(std::move(job));
        if (jobCreated)
            jobCreated();
        return result;
    }
    void prepareShutdown() override
    {
        stopping = true;
        for (const auto &job : jobs)
            job->cancel();
    }
    StateSnapshot stateSnapshot() const override
    {
        StateSnapshot result;
        if (!stopping) {
            result.sequence = sequence;
            for (const auto &entry : entries)
                result.sessions.append(entry->state);
        }
        return result;
    }
    IModelInstance *resolveSession(const QUuid &id) const override
    {
        if (!stopping)
            for (const auto &entry : entries)
                if (entry->state.sessionId == id && entry->state.status == SessionStatus::Ready &&
                    entry->model->isOpen())
                    return entry->model.get();
        return nullptr;
    }
    QMetaObject::Connection observeState(QObject *context, const StateEventFilter &filter,
                                         std::function<void(const StateEvent &)> callback) override
    {
        const auto after = sequence;
        return connect(
            this, &Host::stateCommitted, context,
            [this, filter, after, callback = std::move(callback)](const StateEvent &event) {
                if (!stopping && event.sequence > after && filter.accepts(event))
                    callback(event);
            });
    }
    QMetaObject::Connection
    observeBeforeSave(QObject *context, std::function<void(const SaveRequest &)> callback) override
    {
        return connect(this, &Host::beforeSave, context, std::move(callback));
    }
    ViewSnapshot viewSnapshot(const QUuid &id) const override
    {
        if (!stopping)
            for (const auto &entry : entries)
                if (entry->state.sessionId == id) {
                    auto copy = entry->view;
                    copy.session = entry->state;
                    copy.sequence = sequence;
                    return copy;
                }
        return {};
    }
    QMetaObject::Connection observeViews(QObject *context, const ViewEventFilter &filter,
                                         std::function<void(const ViewEvent &)> callback) override
    {
        const auto after = sequence;
        return connect(
            this, &Host::viewCommitted, context,
            [this, filter, after, callback = std::move(callback)](const ViewEvent &event) {
                if (!stopping && event.sequence > after && filter.accepts(event))
                    callback(event);
            });
    }
    ActivitySnapshot activitySnapshot() const override
    {
        return stopping ? ActivitySnapshot{} : ActivitySnapshot{sequence, {}, activity};
    }
    QMetaObject::Connection
    observeActivity(QObject *context, const ActivityEventFilter &filter,
                    std::function<void(const ActivityEvent &)> callback) override
    {
        const auto after = sequence;
        return connect(
            this, &Host::activityCommitted, context,
            [this, filter, after, callback = std::move(callback)](const ActivityEvent &event) {
                if (!stopping && event.sequence > after && filter.accepts(event))
                    callback(event);
            });
    }
    /** @brief Model a native select-tool or attribute-table change of the preview layer. */
    void mapSelect(Entry *entry, const QList<qint64> &fids)
    {
        auto &project = entry->model->projectValue;
        project.previewSelection = QSet<qint64>(fids.cbegin(), fids.cend());
        viewChange(entry, ViewEventKind::LayerSelectionChanged, project.previewAttachment);
    }
    /** @brief Accept only preview-layer selection replacements for the exact Ready load. */
    bool executeViewCommand(const ViewCommand &command, QString *) override
    {
        for (const auto &entry : entries) {
            const auto &project = entry->model->projectValue;
            if (command.kind == ViewCommandKind::SelectLayerFeatures &&
                entry->state.sessionId == command.sessionId &&
                entry->state.loadGeneration == command.loadGeneration &&
                !project.previewAttachment.isNull() &&
                command.layerSessionId == project.previewAttachment) {
                ++selectionCommands;
                mapSelect(entry.get(), command.featureIds);
                return true;
            }
        }
        return false;
    }
    /** @brief Resolve the fake host-owned preview layer and its feature-order FIDs. */
    QUuid previewLayer(const QUuid &session, QList<qint64> *fids) const override
    {
        if (fids)
            fids->clear();
        for (const auto &entry : entries)
            if (!stopping && entry->state.sessionId == session &&
                !entry->model->projectValue.previewAttachment.isNull()) {
                if (fids)
                    *fids = entry->model->projectValue.previewFids;
                return entry->model->projectValue.previewAttachment;
            }
        return {};
    }
    QList<qint64> selectedLayerFeatures(const QUuid &session, const QUuid &attachment, int offset,
                                        int limit) const override
    {
        QList<qint64> selected;
        for (const auto &entry : entries)
            if (entry->state.sessionId == session && !attachment.isNull() &&
                attachment == entry->model->projectValue.previewAttachment)
                selected = entry->model->projectValue.previewSelection.values();
            else if (entry->state.sessionId == session)
                for (const auto &layer : entry->view.layers)
                    if (layer.layerSessionId == attachment)
                        for (const auto &feature : entry->model->projectValue.features)
                            if (feature.value("selected").toBool())
                                selected.append(feature.value("id").toLongLong());
        std::sort(selected.begin(), selected.end());
        return selected.mid(offset, qMin(limit, 5000));
    }
    QVariant value(const QString &, const QVariant &fallback) const override { return fallback; }
    void setValue(const QString &, const QVariant &) override {}
    void remove(const QString &) override {}
    QStringList keys() const override { return {}; }
    void append(const QString &, const QString &text) override { messages.append(text); }
    void clear() override { messages.clear(); }
    void appendLocation(const QString &, const QString &text, const QString &, int) override
    {
        messages.append(text);
    }
    void setLocationHandler(QObject *, std::function<void(const QString &, int)>) override {}
    bool registerAction(const QString &, QAction *) override { return true; }
    void unregisterAction(const QString &) override {}
    bool registerRibbonTab(const RibbonTabSpec &) override { return true; }
    void unregisterRibbonTab(const QString &) override {}
    bool registerDockPanelFactory(IDockPanelFactory *) override { return true; }
    void unregisterDockPanelFactory(const QString &) override {}
    bool registerDocumentViewFactory(IDocumentViewFactory *) override { return true; }
    void unregisterDocumentViewFactory(const QString &) override {}
    QWidget *openDocumentView(const QString &, IModelInstance *) override { return nullptr; }
    bool registerPreferencesPage(IPreferencesPageFactory *) override { return true; }
    void unregisterPreferencesPage(const QString &) override {}
    void showDock(const QString &) override {}
    void statusMessage(const QString &, int) override {}
    void message(const QString &, const QString &) override {}
    void postToMainThread(std::function<void()> callback) override
    {
        QTimer::singleShot(0, this, std::move(callback));
    }
    QWidget *mainWindow() const override { return nullptr; }

signals:
    void stateCommitted(const StateEvent &event);
    void viewCommitted(const ViewEvent &event);
    void activityCommitted(const ActivityEvent &event);
    void beforeSave(const SaveRequest &request);
};

/** @brief Independent production tool panels use separate native preview channels. */
class ToolHost final : public Host, public IToolPreviews
{
    Q_OBJECT
    Q_INTERFACES(OpenSWMM::Sdk::IToolPreviews)
public:
    struct Channel {
        QUuid session; ///< Exact owner session.
        Project project; ///< Independent preview population and selection.
    };
    std::map<QString, std::unique_ptr<Channel>> channels;
    /** @brief Publish one exact-generation tool population without replacing another. */
    bool showToolPreview(const QUuid &session, quint64 generation, const QString &tool,
                          const QJsonObject &value, QString *error) override
    {
        for (const auto &entry : entries) {
            if (entry->state.sessionId != session || entry->state.loadGeneration != generation)
                continue;
            const auto key = session.toString() + tool;
            auto &channel = channels[key];
            if (!channel) {
                channel = std::make_unique<Channel>();
                channel->session = session;
                channel->project.model = entry->model.get();
            }
            return channel->project.showPreview(value, error);
        }
        return false;
    }
    /** @brief Dispose only the selected tool population. */
    void clearToolPreview(const QUuid &session, const QString &tool) override
    {
        const auto found = channels.find(session.toString() + tool);
        if (found != channels.end())
            found->second->project.clearPreview();
    }
    /** @brief Resolve FIDs by both tool and session. */
    QUuid toolPreviewLayer(const QUuid &session, const QString &tool,
                           QList<qint64> *fids = nullptr) const override
    {
        if (fids)
            fids->clear();
        const auto found = channels.find(session.toString() + tool);
        if (found == channels.end())
            return {};
        if (fids)
            *fids = found->second->project.previewFids;
        return found->second->project.previewAttachment;
    }
    /** @brief Route selection by exact independent attachment. */
    bool executeViewCommand(const ViewCommand &command, QString *error) override
    {
        for (const auto &[key, channel] : channels) {
            auto &project = channel->project;
            if (channel->session == command.sessionId &&
                project.previewAttachment == command.layerSessionId) {
                project.previewSelection = QSet<qint64>(command.featureIds.cbegin(),
                                                        command.featureIds.cend());
                for (const auto &entry : entries)
                    if (entry->state.sessionId == command.sessionId)
                        viewChange(entry.get(), ViewEventKind::LayerSelectionChanged,
                                    project.previewAttachment);
                return true;
            }
        }
        return Host::executeViewCommand(command, error);
    }
    /** @brief Inspect only the exact tool attachment's selection. */
    QList<qint64> selectedLayerFeatures(const QUuid &session, const QUuid &attachment,
                                        int offset, int limit) const override
    {
        for (const auto &[key, channel] : channels)
            if (channel->session == session && channel->project.previewAttachment == attachment) {
                auto selected = channel->project.previewSelection.values();
                std::sort(selected.begin(), selected.end());
                return selected.mid(offset, qMin(limit, 5000));
            }
        return Host::selectedLayerFeatures(session, attachment, offset, limit);
    }
};

/** @brief Retain one independent case's real panel, source and recorded input. */
struct Fixture {
    QString
        directory; ///< All case artifacts remain reviewable below the configured verification root.
    Host host;     ///< Outlives every panel callback and retained job handle.
    Host::Entry *entry = nullptr;        ///< Initial independent Ready source lifetime.
    std::unique_ptr<PreviewPanel> panel; ///< Actual production UI under test.
    QString recorded;                    ///< Explicit saved response path.
    /** @brief Construct a unique case without reading or modifying user settings. */
    Fixture()
    {
        directory = QDir(QStringLiteral(SDK_DATA_PREVIEW_VERIFICATION_DIRECTORY))
                        .filePath(QString::fromLatin1(QTest::currentTestFunction()) + '/' +
                                  QUuid::createUuid().toString(QUuid::WithoutBraces));
        QDir().mkpath(directory);
        entry = host.addModel(directory + "/one");
        host.activity.windowId = QUuid::createUuid();
        recorded = directory + "/catalogue.json";
        QFile file(recorded);
        if (!file.open(QIODevice::WriteOnly) || file.write(catalogue()) != catalogue().size())
            qFatal("Cannot create retained preview fixture");
        file.close();
        panel = std::make_unique<PreviewPanel>(&host);
    }
    /** @brief Find an existing public UI control by its stable product objectName. */
    template <class T> T *control(const char *name) const
    {
        auto *value = panel->findChild<T *>(QString::fromLatin1(name));
        if (!value)
            qFatal("Missing public preview control: %s", name);
        return value;
    }
    /** @brief Choose polygon mode through the ordinary area selector. */
    void polygonMode() { control<QComboBox>("dataAreaMode")->setCurrentIndex(1); }
    /** @brief Replace the recorded response without creating a network request. */
    bool record(const QByteArray &bytes)
    {
        QFile file(recorded);
        return file.open(QIODevice::WriteOnly | QIODevice::Truncate) &&
               file.write(bytes) == bytes.size();
    }
    /** @brief Begin a saved response and optionally deliver its completed worker callback. */
    bool review(bool finish = true)
    {
        const auto before = host.jobs.size();
        panel->loadCatalogue(recorded);
        if (host.jobs.size() != before + 1)
            return false;
        if (finish)
            host.jobs.back()->complete();
        return true;
    }
    /** @brief Read the user-visible catalogue row count. */
    int rows() const { return control<QTableView>("dataCatalogue")->model()->rowCount(); }
    /** @brief Select the reviewed item and allow the UI to enable explicit GIS export.
     * @details The row also selects the native preview feature. Its evidence is
     * delivered next, as on the host's next turn, unless the case already holds
     * earlier observations; held cases keep their deliberate delivery order. */
    void select()
    {
        const bool settled = host.pendingState.isEmpty() && host.pendingViews.isEmpty() &&
                             host.pendingActivity.isEmpty();
        control<QTableView>("dataCatalogue")->selectRow(0);
        if (settled)
            host.drain();
    }
    /** @brief Read the catalogue IDs of the selected rows, sorted for comparison. */
    QStringList selectedIds() const
    {
        QStringList result;
        auto *table = control<QTableView>("dataCatalogue");
        for (const auto &row : table->selectionModel()->selectedRows(1))
            result.append(row.data().toString());
        result.sort();
        return result;
    }
    /** @brief Return the current chosen attachment, independently from its stable local key. */
    QUuid attachment() const { return entry->view.layers.first().layerSessionId; }
    /** @brief Copy the source's present overlay value. */
    bool hasOverlay() const { return !entry->model->projectValue.overlay.isEmpty(); }
    /** @brief Trigger the real explicit export command through its existing button. */
    void exportSelection() { control<QPushButton>("dataExport")->click(); }
    /** @brief Set one ordinary local attribute predicate using stable source property names. */
    bool filter(const QString &field, const QString &operation, const QString &text = {})
    {
        auto *fields = control<QComboBox>("dataFilterField");
        const int fieldIndex = field.isEmpty() ? 0 : fields->findData(field);
        auto *operators = control<QComboBox>("dataFilterOperator");
        const int operatorIndex = operators->findText(operation);
        if (fieldIndex < 0 || operatorIndex < 0)
            return false;
        fields->setCurrentIndex(fieldIndex);
        operators->setCurrentIndex(operatorIndex);
        control<QLineEdit>("dataFilterText")->setText(text);
        QCoreApplication::processEvents();
        return true;
    }
    /** @brief Read the saved-property value displayed for the table's current location. */
    QVariant attribute(const QString &name) const
    {
        const auto *attributes = control<QTableView>("dataAttributes")->model();
        for (int row = 0; row < attributes->rowCount(); ++row)
            if (attributes->index(row, 0).data().toString() == name)
                return attributes->index(row, 1).data();
        return {};
    }
    /** @brief Count actual geometry features in the current disposable map preview. */
    int overlayCount() const
    {
        return entry->model->projectValue.overlay["features"].toArray().size();
    }
};
}

/** @brief Verify lifecycle, exact review context, pending-state fences and bounded offline publication. */
class PreviewTest final : public QObject
{
    Q_OBJECT
private slots:
    /** @brief Open both real provider panels, select gages, then clear only DEM footprints. */
    void toolPanelsKeepPreviewsAndSelectionIndependent()
    {
        const auto directory = QDir(QStringLiteral(SDK_DATA_PREVIEW_VERIFICATION_DIRECTORY))
                                   .filePath("tool-panels/" + QUuid::createUuid().toString());
        QVERIFY(QDir().mkpath(directory));
        ToolHost host;
        const auto *entry = host.addModel(directory + "/model");
        const auto sitesFile = directory + "/sites.json";
        const auto demFile = directory + "/dem.json";
        {
            QFile file(sitesFile);
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write(catalogue());
        }
        {
            QFile file(demFile);
            QVERIFY(file.open(QIODevice::WriteOnly));
            const QByteArray tiles = R"({"total":1,"items":[{"sourceId":"dem-a","title":"Elevation tile","boundingBox":{"minX":-78,"minY":39,"maxX":-76,"maxY":42},"downloadURL":"https://example.test/tile.tif"}]})";
            file.write(tiles);
        }
        PreviewPanel sites(&host, OpenSWMM::Data::Source::Sites, "sites");
        PreviewPanel terrain(&host, OpenSWMM::Data::Source::Terrain, "terrain");
        sites.loadCatalogue(sitesFile);
        host.jobs.back()->complete();
        terrain.loadCatalogue(demFile);
        host.jobs.back()->complete();
        host.drain();
        const auto session = entry->state.sessionId;
        const auto sitesId = host.toolPreviewLayer(session, "sites");
        const auto demId = host.toolPreviewLayer(session, "terrain");
        QVERIFY(!sitesId.isNull());
        QVERIFY(!demId.isNull());
        QVERIFY(sitesId != demId);
        auto *table = sites.findChild<QTableView *>("dataCatalogue");
        QVERIFY(table);
        table->selectRow(0);
        host.drain();
        QVERIFY(sites.findChild<QPushButton *>("dataDownload")->isEnabled());
        terrain.clearPreview();
        host.drain();
        QVERIFY(host.toolPreviewLayer(session, "terrain").isNull());
        QCOMPARE(host.toolPreviewLayer(session, "sites"), sitesId);
        QCOMPARE(table->selectionModel()->selectedRows().size(), 1);
        QVERIFY(sites.findChild<QPushButton *>("dataDownload")->isEnabled());
        sites.hide();
        sites.show();
        host.drain();
        QCOMPARE(host.toolPreviewLayer(session, "sites"), sitesId);
        QVERIFY(sites.findChild<QPushButton *>("dataDownload")->isEnabled());
        QVERIFY(sites.findChild<QComboBox *>("dataSource")->isHidden());
        QVERIFY(terrain.findChild<QComboBox *>("dataSource")->isHidden());
        sites.setSource(OpenSWMM::Data::Source::Terrain);
        terrain.setSource(OpenSWMM::Data::Source::Sites);
        QCOMPARE(sites.findChild<QComboBox *>("dataSource")->currentIndex(), 0);
        QCOMPARE(terrain.findChild<QComboBox *>("dataSource")->currentIndex(), 1);
        QVERIFY(!sites.findChild<QComboBox *>("dataSource")->isEnabled());
        QVERIFY(!terrain.findChild<QComboBox *>("dataSource")->isEnabled());
    }

private slots:
    /** @brief Confirm every fixture file can be retained in the configured repository directory. */
    void initTestCase() { QVERIFY(QDir().mkpath(SDK_DATA_PREVIEW_VERIFICATION_DIRECTORY)); }
    /** @brief The default flow hides unrelated area/product inputs and advanced inspection. */
    void compactSourceSpecificWorkflow()
    {
        Fixture f;
        QVERIFY(f.control<QWidget>("dataBoundaryControls")->isHidden());
        QVERIFY(f.control<QWidget>("dataTerrainControls")->isHidden());
        QVERIFY(!f.control<QWidget>("dataPeriodControls")->isHidden());
        QVERIFY(f.control<QWidget>("dataDetails")->isHidden());
        QVERIFY(f.control<QPushButton>("dataCancel")->isHidden());
        f.control<QToolButton>("dataDetailsToggle")->click();
        QVERIFY(!f.control<QWidget>("dataDetails")->isHidden());
        f.polygonMode();
        QVERIFY(!f.control<QWidget>("dataBoundaryControls")->isHidden());
        f.panel->setSource(OpenSWMM::Data::Source::Terrain);
        QVERIFY(!f.control<QWidget>("dataTerrainControls")->isHidden());
        QVERIFY(f.control<QWidget>("dataPeriodControls")->isHidden());
        QCOMPARE(f.control<QPushButton>("dataDownload")->text(), "Download selected raster tiles…");
    }
    /** @brief Clearing an accepted DEM/site preview retains independent layers and files. */
    void clearPreviewRetainsSavedAssets_data()
    {
        QTest::addColumn<bool>("terrain");
        QTest::newRow("sites") << false;
        QTest::newRow("dem-footprints") << true;
    }
    /** @brief Only transient geometry and rows disappear; persistent raster/export paths survive. */
    void clearPreviewRetainsSavedAssets()
    {
        QFETCH(bool, terrain);
        Fixture f;
        if (terrain) {
            f.panel->setSource(OpenSWMM::Data::Source::Terrain);
            const QByteArray terrainCatalogue = R"({"total":1,"items":[{"sourceId":"dem-a","title":"Elevation tile","boundingBox":{"minX":-78,"minY":39,"maxX":-76,"maxY":42},"downloadURL":"https://example.test/tile.tif"}]})";
            QVERIFY(f.record(terrainCatalogue));
        }
        QVERIFY(f.review());
        QVERIFY(f.hasOverlay());
        f.entry->model->projectValue.rasters.append("existing-dem.tif");
        f.entry->model->projectValue.exports.append("saved-sites.geojson");
        const auto saved = f.directory + "/download-retained.csv";
        QFile file(saved); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("time,value\n"); file.close();
        f.control<QPushButton>("dataClearPreview")->click();
        QCOMPARE(f.rows(), 0);
        QVERIFY(!f.hasOverlay());
        QCOMPARE(f.entry->model->projectValue.rasters, QStringList{"existing-dem.tif"});
        QCOMPARE(f.entry->model->projectValue.exports, QStringList{"saved-sites.geojson"});
        QVERIFY(QFileInfo::exists(saved));
        QCOMPARE(f.host.activeProgress(), 0);
        QVERIFY(f.host.events.join('\n').contains("preview cleared"));
        QVERIFY(!f.control<QPushButton>("dataDownload")->isEnabled());
    }
    /** @brief First DEM preview enables Clear before native layer notifications arrive. */
    void firstDemPreviewCanClearWithoutSelection()
    {
        Fixture f;
        f.panel->setSource(OpenSWMM::Data::Source::Terrain);
        const QByteArray tiles = R"({"total":1,"items":[{"sourceId":"dem-a","title":"Elevation tile","boundingBox":{"minX":-78,"minY":39,"maxX":-76,"maxY":42},"downloadURL":"https://example.test/tile.tif"}]})";
        QVERIFY(f.record(tiles));
        auto &project = f.entry->model->projectValue;
        project.previewChanged = [&] {
            // A real native preview changes its session revision synchronously,
            // while detailed attachment observations reach consumers later.
            f.host.viewChange(f.entry, ViewEventKind::LayerAdded, project.previewAttachment);
        };
        QVERIFY(f.review());
        QCOMPARE(f.rows(), 1);
        QVERIFY(f.control<QTableView>("dataCatalogue")->selectionModel()->selectedRows().isEmpty());
        QVERIFY(!f.control<QPushButton>("dataDownload")->isEnabled());
        QVERIFY(f.control<QPushButton>("dataClearPreview")->isEnabled());
        f.control<QPushButton>("dataClearPreview")->click();
        QCOMPARE(f.rows(), 0);
        QVERIFY(!f.hasOverlay());
        f.host.drain();
        QCOMPARE(f.rows(), 0);
        QVERIFY(!f.hasOverlay());
    }
    /** @brief Pending network and latched parse completions cannot resurrect a cleared preview. */
    void clearPendingPreviewFencesLateWork()
    {
        Fixture f;
        f.control<QPushButton>("dataPreview")->click();
        QVERIFY(f.host.policy.reply);
        auto reply = f.host.policy.reply;
        QCOMPARE(f.host.activeProgress(), 1);
        QVERIFY(f.control<QPushButton>("dataClearPreview")->isEnabled());
        f.control<QPushButton>("dataClearPreview")->click();
        QCOMPARE(f.host.activeProgress(), 0);
        QCOMPARE(reply->error(), QNetworkReply::OperationCanceledError);
        reply->complete();
        QCOMPARE(f.host.jobs.size(), size_t(0));
        QVERIFY(!f.hasOverlay());
        QVERIFY(f.review(false));
        auto *job = f.host.jobs.back().get();
        f.control<QPushButton>("dataClearPreview")->click();
        job->complete();
        QCOMPARE(f.rows(), 0);
        QVERIFY(!f.hasOverlay());
        QCOMPARE(f.host.activeProgress(), 0);
        for (const auto &task : f.host.tasks)
            QCOMPARE(task.outcome, task.title.startsWith("Clear ") ? ProgressOutcome::Succeeded
                                                                   : ProgressOutcome::Cancelled);
        QVERIFY(f.review());
        QVERIFY(f.hasOverlay());
        QCOMPARE(f.host.activeProgress(), 0);
    }
    /** @brief Stable metadata stages reach shared progress/logs and conclude after preview/export. */
    void sharedPreviewProgressAndExport()
    {
        Fixture f;
        f.control<QPushButton>("dataPreview")->click();
        auto reply = f.host.policy.reply;
        QVERIFY(reply);
        emit reply->downloadProgress(10, 100);
        emit reply->downloadProgress(20, 100);
        emit reply->downloadProgress(30, 100);
        QCOMPARE(f.host.events.filter("Receiving catalogue metadata").size(), 1);
        reply->complete();
        f.host.jobs.back()->complete();
        QCOMPARE(f.host.activeProgress(), 0);
        QVERIFY(f.host.events.join('\n').contains("Saving catalogue cache"));
        f.select(); f.exportSelection();
        QCOMPARE(f.host.activeProgress(), 0);
        QVERIFY(f.host.events.join('\n').contains("Save selected catalogue features"));
        for (const auto &task : f.host.tasks) QCOMPARE(task.outcome, ProgressOutcome::Succeeded);
    }
    /** @brief Synchronous shared-log publication may destroy the panel at start or during byte updates. */
    void progressPublicationMayClosePanel()
    {
        Fixture starting;
        starting.host.publishing = [&] {
            starting.host.publishing = {};
            starting.panel.reset();
        };
        starting.control<QPushButton>("dataPreview")->click();
        QVERIFY(!starting.panel);
        QCOMPARE(starting.host.activeProgress(), 0);
        QCOMPARE(starting.host.policy.requestCount, 0);

        Fixture receiving;
        receiving.control<QPushButton>("dataPreview")->click();
        auto reply = receiving.host.policy.reply;
        QVERIFY(reply);
        receiving.host.publishing = [&] {
            receiving.host.publishing = {};
            receiving.panel.reset();
        };
        emit reply->downloadProgress(1, 10);
        QVERIFY(!receiving.panel);
        QCOMPARE(receiving.host.activeProgress(), 0);
    }
    /** @brief Cancellation before beginProgress returns closes that returned token and never starts IO. */
    void cancellationDuringProgressBegin()
    {
        Fixture f;
        f.host.publishing = [&] {
            f.host.publishing = {};
            f.panel->cancel();
        };
        f.control<QPushButton>("dataPreview")->click();
        QVERIFY(f.panel);
        QCOMPARE(f.host.activeProgress(), 0);
        QCOMPARE(f.host.policy.requestCount, 0);
        QCOMPARE(f.host.tasks.size(), 1);
        QCOMPARE(f.host.tasks.cbegin()->outcome, ProgressOutcome::Cancelled);
        QVERIFY(f.control<QPushButton>("dataPreview")->isEnabled());
        QVERIFY(f.review());
        QVERIFY(f.hasOverlay());
        QCOMPARE(f.host.activeProgress(), 0);
    }
    /** @brief Completed files hand off once to the exact Ready session and stay cancellable. */
    void downloadedObservationsBecomeNativeLayers()
    {
        Fixture f;
        QVERIFY(f.review());
        f.select();
        OpenSWMM::Data::PayloadResult result;
        result.importId = QUuid::createUuid();
        result.observationCount = 3;
        result.manifestPath = f.directory + "/manifest.json";
        result.descriptorPath = f.directory + "/timeseries-import.json";
        auto *payload = f.panel->findChild<OpenSWMM::Data::PayloadDownload *>();
        QVERIFY(payload);
        payload->finished(result);
        QCOMPARE(f.host.imports.size(), 1);
        QCOMPARE(f.host.imports.first().sessionId, f.entry->state.sessionId);
        QCOMPARE(f.host.imports.first().loadGeneration, f.entry->state.loadGeneration);
        QCOMPARE(f.host.imports.first().importId, result.importId);
        QCOMPARE(f.host.imports.first().descriptorPath, result.descriptorPath);
        QVERIFY(f.control<QPushButton>("dataCancel")->isEnabled());
        QVERIFY(!f.control<QPushButton>("dataClearPreview")->isEnabled());
        QCOMPARE(f.host.activeProgress(), 1);
        QVERIFY(!f.control<QPushButton>("dataDownload")->isEnabled());
        TimeSeriesImportResult imported;
        imported.importId = result.importId;
        imported.layerId = "native-observations";
        imported.seriesCount = 2;
        f.host.importFinished(imported);
        QVERIFY(f.control<QPushButton>("dataCancel")->isHidden());
        QVERIFY(f.control<QLabel>("dataStatus")->text().contains("Added 2 series"));
    }
    /** @brief A cancelled or invalidated native import cannot report a late success. */
    void nativeImportCancellationAndStaleCompletion()
    {
        Fixture f;
        QVERIFY(f.review());
        f.select();
        OpenSWMM::Data::PayloadResult result;
        result.importId = QUuid::createUuid();
        result.observationCount = 1;
        result.manifestPath = f.directory + "/manifest.json";
        result.descriptorPath = f.directory + "/timeseries-import.json";
        f.panel->findChild<OpenSWMM::Data::PayloadDownload *>()->finished(result);
        QCOMPARE(f.host.imports.size(), 1);
        auto *job = f.host.jobs.back().get();
        f.panel->cancel();
        QVERIFY(job->cancelled);
        QVERIFY(f.control<QLabel>("dataStatus")->text().contains("Layer import cancelled"));
        const auto before = f.control<QLabel>("dataStatus")->text();
        TimeSeriesImportResult imported;
        imported.layerId = "late";
        imported.seriesCount = 99;
        f.host.importFinished(imported);
        QCOMPARE(f.control<QLabel>("dataStatus")->text(), before);
        f.entry->model->open = false;
        f.panel->findChild<OpenSWMM::Data::PayloadDownload *>()->finished(result);
        QCOMPARE(f.host.imports.size(), 1);
        QVERIFY(f.control<QLabel>("dataStatus")->text().contains("saved, not added"));
    }
    /** @brief A reversed period or invalid measurement cannot start an observation transfer. */
    void observationPeriodAndParameterGateDownload()
    {
        Fixture f;
        QVERIFY(f.review());
        f.select();
        auto *button = f.control<QPushButton>("dataDownload");
        QVERIFY(button->isEnabled());
        auto *from = f.control<QDateEdit>("dataStartDate");
        auto *to = f.control<QDateEdit>("dataEndDate");
        from->setDate(to->date().addDays(1));
        QVERIFY(!button->isEnabled());
        const auto networkCount = f.host.policy.requestCount;
        f.panel->downloadTo(QString::fromLatin1(SDK_DATA_PREVIEW_VERIFICATION_DIRECTORY));
        QCOMPARE(f.host.policy.requestCount, networkCount);
        from->setDate(to->date());
        QVERIFY(button->isEnabled());
        auto *parameter = f.control<QComboBox>("dataParameter");
        parameter->setEditText("invalid");
        QVERIFY(!button->isEnabled());
        parameter->setEditText("00065");
        QVERIFY(button->isEnabled());
        f.entry->model->open = false;
        f.panel->downloadTo(QString::fromLatin1(SDK_DATA_PREVIEW_VERIFICATION_DIRECTORY));
        QCOMPARE(f.host.policy.requestCount, networkCount);
    }
    /** @brief Selected sites limit the period to the union of their published records. */
    void selectedSitesLimitObservationPeriod()
    {
        Fixture f;
        QVERIFY(f.record(richCatalogue()));
        QVERIFY(f.review());
        auto *from = f.control<QDateEdit>("dataStartDate");
        auto *to = f.control<QDateEdit>("dataEndDate");
        auto *note = f.control<QLabel>("dataRecordPeriod");
        const auto unlimited = from->minimumDate();
        from->setDate({1990, 1, 1});
        to->setDate({1990, 1, 31});
        auto &network = f.host.policy;
        network.periodBody =
            recordPeriods({{"site-one", "2001-05-01T05:00:00+00:00", "2010-02-01T05:00:00+00:00"},
                           {"site-two", "1995-03-01T05:00:00+00:00", "2005-12-31T05:00:00+00:00"}});
        auto *table = f.control<QTableView>("dataCatalogue");
        table->selectAll();
        f.host.drain();
        QTRY_VERIFY(network.periodReply);
        const QUrlQuery query(network.periodReply->url());
        QCOMPARE(query.queryItemValue("monitoring_location_id"), "site-one,site-three,site-two");
        QCOMPARE(query.queryItemValue("parameter_code"), "00060");
        QCOMPARE(query.queryItemValue("computation_period_identifier"), "Daily");
        network.periodReply->complete();
        QTRY_COMPARE(from->minimumDate(), QDate(1995, 3, 1));
        QCOMPARE(to->maximumDate(), QDate(2010, 2, 1));
        // The chosen period was outside the record: show its last 30 days.
        QCOMPARE(from->date(), QDate(2010, 1, 2));
        QCOMPARE(to->date(), QDate(2010, 2, 1));
        QCOMPARE(note->text(), "Published record: 1995-03-01 to 2010-02-01 (2 of 3 selected sites).");
        QTRY_VERIFY(f.control<QPushButton>("dataDownload")->isEnabled());
        // Continuous measurements query again; an overlapping span is clamped, not moved.
        from->setDate({2003, 12, 1});
        to->setDate({2004, 3, 1});
        network.periodBody = recordPeriods(
            {{"site-two", "2003-01-01T05:00:00+00:00", "2004-01-15T05:00:00+00:00"}}, "Points");
        const auto requests = network.periodRequests;
        f.control<QComboBox>("dataInterval")->setCurrentIndex(1);
        QCOMPARE(from->minimumDate(), unlimited); // Earlier limits do not constrain the new query.
        QTRY_COMPARE(network.periodRequests, requests + 1);
        QCOMPARE(QUrlQuery(network.periodReply->url()).queryItemValue("computation_period_identifier"),
                 "Points");
        network.periodReply->complete();
        QTRY_COMPARE(to->date(), QDate(2004, 1, 15));
        QCOMPARE(from->date(), QDate(2003, 12, 1));
        QCOMPARE(note->text(), "Published record: 2003-01-01 to 2004-01-15 (1 of 3 selected sites).");
        // Clearing the selection removes the limits and keeps the dates.
        table->clearSelection();
        f.host.drain();
        QCOMPARE(from->minimumDate(), unlimited);
        QCOMPARE(to->maximumDate(), QDateEdit().maximumDate());
        QCOMPARE(from->date(), QDate(2003, 12, 1));
        QVERIFY(note->text().isEmpty());
    }
    /** @brief An unavailable record listing leaves the dates unlimited and never gates Download. */
    void unavailableRecordPeriodKeepsDatesUnlimited()
    {
        Fixture f;
        QVERIFY(f.review());
        auto *from = f.control<QDateEdit>("dataStartDate");
        auto *note = f.control<QLabel>("dataRecordPeriod");
        const auto unlimited = from->minimumDate();
        const auto chosen = from->date();
        f.select();
        QVERIFY(f.control<QPushButton>("dataDownload")->isEnabled());
        QCOMPARE(note->text(), "Checking the published record…");
        QTRY_VERIFY(f.host.policy.periodReply);
        f.host.policy.periodReply->fail("Service unavailable");
        QTRY_COMPARE(note->text(), "Published record unknown: Service unavailable.");
        QCOMPARE(from->minimumDate(), unlimited);
        QCOMPARE(from->date(), chosen);
        QVERIFY(f.control<QPushButton>("dataDownload")->isEnabled());
        // DEM tiles have no observation period.
        f.control<QComboBox>("dataSource")->setCurrentIndex(1);
        QVERIFY(note->text().isEmpty());
    }
    /** @brief A current review exports its exact area/source IDs and uses no legacy subscription. */
    void currentReviewCanExport()
    {
        Fixture f;
        f.polygonMode();
        QVERIFY(f.review());
        QCOMPARE(f.host.legacySubscriptions, 0);
        QCOMPARE(f.rows(), 1);
        QVERIFY(f.hasOverlay());
        f.select();
        QVERIFY(f.control<QPushButton>("dataExport")->isEnabled());
        f.exportSelection();
        QCOMPARE(f.entry->model->projectValue.exports.size(), 1);
        QFile file(f.entry->model->projectValue.exports.first());
        QVERIFY(file.open(QIODevice::ReadOnly));
        const auto value = QJsonDocument::fromJson(file.readAll()).object();
        QCOMPARE(value["review"].toObject()["area"].toObject(), polygon());
        QCOMPARE(value["features"].toArray().first().toObject()["id"].toString(), "site-one");
    }
    /** @brief Selecting one gage highlights a record without hiding any other reviewed map points. */
    void selectionRetainsAllGageDotsAndLabels()
    {
        Fixture f;
        QVERIFY(f.record(richCatalogue()));
        QVERIFY(f.review());
        QCOMPARE(f.rows(), 3);
        QCOMPARE(f.overlayCount(), 3);
        QCOMPARE(f.entry->model->projectValue.overlay["previewStyle"]
                     .toObject()["pointLabelProperty"]
                     .toString(),
                 "catalogue_title");
        QVERIFY(f.entry->model->projectValue.overlay["previewStyle"]
                    .toObject()["asFeatureLayer"]
                    .toBool());
        QCOMPARE(
            f.entry->model->projectValue.overlay["previewStyle"].toObject()["title"].toString(),
            "USGS gages (preview)");
        const auto population = f.entry->model->projectValue.overlay;
        auto *table = f.control<QTableView>("dataCatalogue");
        table->sortByColumn(0, Qt::AscendingOrder);
        table->selectRow(0);
        QCOMPARE(f.entry->model->projectValue.overlay, population);
        table->sortByColumn(0, Qt::DescendingOrder);
        QCoreApplication::processEvents();
        QCOMPARE(table->model()->index(1, 1).data().toString(), "site-two");
        table->selectRow(1);
        QCOMPARE(f.overlayCount(), 3);
        QCOMPARE(f.entry->model->projectValue.overlay, population);
        // The rows also selected native features; deliver that evidence first.
        QTRY_VERIFY((f.host.drain(), f.control<QPushButton>("dataExport")->isEnabled()));
        f.exportSelection();
        QCOMPARE(f.entry->model->projectValue.exports.size(), 1);
        QFile file(f.entry->model->projectValue.exports.first());
        QVERIFY(file.open(QIODevice::ReadOnly));
        const auto saved = QJsonDocument::fromJson(file.readAll()).object();
        QVERIFY(!saved.contains("previewStyle"));
        const auto features = saved["features"].toArray();
        QCOMPARE(features.size(), 1);
        QCOMPARE(features.first().toObject()["id"].toString(), "site-two");
        QCOMPARE(features.first().toObject()["properties"].toObject()["drainage_area"].toString(),
                 "25");
    }
    /** @brief Select-tool changes on the preview layer select matching rows of a sorted table. */
    void mapSelectionSelectsCatalogueRows()
    {
        Fixture f;
        QVERIFY(f.record(richCatalogue()));
        QVERIFY(f.review());
        const auto &project = f.entry->model->projectValue;
        QCOMPARE(project.previewFids.size(), 3);
        auto *table = f.control<QTableView>("dataCatalogue");
        table->sortByColumn(0, Qt::DescendingOrder);
        f.host.mapSelect(f.entry, {project.previewFids[1], project.previewFids[2]});
        f.host.drain();
        QCOMPARE(f.selectedIds(), (QStringList{"site-three", "site-two"}));
        QCOMPARE(f.control<QLabel>("dataResultCount")->text(), "3 shown / 3 total / 2 selected");
        // The topmost selected row becomes current for attribute inspection.
        QCOMPARE(f.attribute("catalogue_id").toString(), "site-three");
        QCOMPARE(f.host.selectionCommands, 0);
        QTRY_VERIFY(f.control<QPushButton>("dataDownload")->isEnabled());
        QCOMPARE(f.host.selectionCommands, 0);
        // An empty-space select-tool click clears the rows and the download.
        f.host.mapSelect(f.entry, {});
        f.host.drain();
        QVERIFY(f.selectedIds().isEmpty());
        QVERIFY(!f.control<QPushButton>("dataDownload")->isEnabled());
        QCOMPARE(f.host.selectionCommands, 0);
    }
    /** @brief Row selection replaces the native selection once and waits for its evidence. */
    void catalogueRowsSelectNativePreviewFeatures()
    {
        Fixture f;
        QVERIFY(f.record(richCatalogue()));
        QVERIFY(f.review());
        const auto &project = f.entry->model->projectValue;
        auto *table = f.control<QTableView>("dataCatalogue");
        table->sortByColumn(0, Qt::DescendingOrder);
        QCOMPARE(table->model()->index(1, 1).data().toString(), "site-two");
        table->selectRow(1);
        QCOMPARE(project.previewSelection, (QSet<qint64>{project.previewFids[1]}));
        QCOMPARE(f.host.selectionCommands, 1);
        // The native write is a source revision, not review authority.
        QVERIFY(!f.control<QPushButton>("dataDownload")->isEnabled());
        f.host.drain();
        QTRY_VERIFY(f.control<QPushButton>("dataDownload")->isEnabled());
        QCOMPARE(f.host.selectionCommands, 1);
        QCOMPARE(f.selectedIds(), QStringList{"site-two"});
        table->clearSelection();
        QVERIFY(project.previewSelection.isEmpty());
        QCOMPARE(f.host.selectionCommands, 2);
    }
    /** @brief A filtered replacement layer receives the still-visible row selection. */
    void filteredReplacementCarriesRowSelection()
    {
        Fixture f;
        QVERIFY(f.record(richCatalogue()));
        QVERIFY(f.review());
        const auto &project = f.entry->model->projectValue;
        auto *table = f.control<QTableView>("dataCatalogue");
        table->selectRow(0);
        table->selectionModel()->select(table->model()->index(2, 0),
                                        QItemSelectionModel::Select | QItemSelectionModel::Rows);
        QCOMPARE(f.selectedIds(), (QStringList{"site-one", "site-three"}));
        QTRY_VERIFY((f.host.drain(), f.control<QPushButton>("dataDownload")->isEnabled()));
        const auto original = project.previewAttachment;
        // One predicate step hides only Gamma Lake; the selected Alpha row stays visible.
        QVERIFY(f.filter({}, "Contains", "USGS"));
        QTRY_VERIFY((f.host.drain(), f.control<QPushButton>("dataDownload")->isEnabled()));
        QCOMPARE(f.overlayCount(), 2);
        QVERIFY(project.previewAttachment != original);
        QCOMPARE(f.selectedIds(), QStringList{"site-one"});
        QCOMPARE(project.previewSelection, (QSet<qint64>{project.previewFids[0]}));
    }
    /** @brief DEM footprints use the selectable native preview layer like monitoring sites. */
    void demFootprintsUseSelectableNativeLayer()
    {
        Fixture f;
        f.panel->setSource(OpenSWMM::Data::Source::Terrain);
        const QByteArray tiles = R"({"total":2,"items":[{"sourceId":"dem-a","title":"Tile A","boundingBox":{"minX":-78,"minY":39,"maxX":-76,"maxY":42},"downloadURL":"https://example.test/a.tif"},{"sourceId":"dem-b","title":"Tile B","boundingBox":{"minX":-76.8,"minY":40.2,"maxX":-76.2,"maxY":40.8},"downloadURL":"https://example.test/b.tif"}]})";
        QVERIFY(f.record(tiles));
        QVERIFY(f.review());
        const auto &project = f.entry->model->projectValue;
        const auto style = project.overlay["previewStyle"].toObject();
        QVERIFY(style["asFeatureLayer"].toBool());
        QCOMPARE(style["title"].toString(), "DEM tiles (preview)");
        QVERIFY(!style.contains("pointLabelProperty"));
        QCOMPARE(project.previewFids.size(), 2);
        f.host.mapSelect(f.entry, {project.previewFids[1]});
        f.host.drain();
        QCOMPARE(f.selectedIds(), QStringList{"dem-b"});
        QTRY_VERIFY(f.control<QPushButton>("dataDownload")->isEnabled());
        QCOMPARE(f.control<QPushButton>("dataDownload")->text(), "Download selected raster tiles…");
        f.panel->clearPreview();
        QVERIFY(project.previewAttachment.isNull());
        QVERIFY(f.selectedIds().isEmpty());
    }
    /** @brief A real temporary-layer insertion retains results until detailed revision evidence arrives. */
    void nativePreviewInsertionWaitsForDetailedEvidence()
    {
        Fixture f;
        f.polygonMode();
        QVERIFY(f.record(richCatalogue()));
        const auto previewLayer = QUuid::createUuid();
        f.entry->model->projectValue.previewChanged = [&] {
            f.host.stateChange(f.entry, StateChange::Layers);
            f.host.viewChange(f.entry, ViewEventKind::LayerAdded, previewLayer);
        };
        QVERIFY(f.review());
        QCOMPARE(f.rows(), 3);
        QCOMPARE(f.overlayCount(), 3);
        f.select();
        QVERIFY(!f.control<QPushButton>("dataExport")->isEnabled());
        const auto writes = f.entry->model->projectValue.overlayWrites;
        f.host.drainFirstState();
        QCOMPARE(f.entry->model->projectValue.overlayWrites, writes);
        QVERIFY(!f.control<QPushButton>("dataExport")->isEnabled());
        f.exportSelection();
        QVERIFY(f.entry->model->projectValue.exports.isEmpty());
        f.host.drain();
        QTRY_VERIFY(f.control<QPushButton>("dataExport")->isEnabled());
        QCOMPARE(f.rows(), 3);
        QCOMPARE(f.overlayCount(), 3);
        f.exportSelection();
        QCOMPARE(f.entry->model->projectValue.exports.size(), 1);
    }
    /** @brief A local population change waits for replacement evidence; sorting preserves native content. */
    void nativePreviewFilteringWaitsWithoutResettingUnchangedPopulation()
    {
        Fixture f;
        QVERIFY(f.record(richCatalogue()));
        const auto previewLayer = QUuid::createUuid();
        int changes = 0;
        f.entry->model->projectValue.previewChanged = [&] {
            ++changes;
            f.host.viewChange(f.entry, ViewEventKind::LayerAdded, previewLayer);
        };
        QVERIFY(f.review());
        QCOMPARE(changes, 1);
        // Before insertion evidence arrives, only the desired population changes.
        QVERIFY(f.filter("agency", "Equals", "USGS"));
        QCOMPARE(f.rows(), 2);
        QCOMPARE(f.overlayCount(), 3);
        QCOMPARE(changes, 1);
        f.select();
        QVERIFY(!f.control<QPushButton>("dataExport")->isEnabled());
        // The first safe rebase publishes the filtered replacement, whose own
        // detailed revision must then be delivered before export is authorized.
        QTRY_VERIFY((f.host.drain(), f.control<QPushButton>("dataExport")->isEnabled()));
        QCOMPARE(f.overlayCount(), 2);
        QCOMPARE(changes, 2);
        const auto population = f.entry->model->projectValue.overlay;
        auto *table = f.control<QTableView>("dataCatalogue");
        table->sortByColumn(0, Qt::DescendingOrder);
        table->selectRow(0);
        QCOMPARE(f.entry->model->projectValue.overlay, population);
        QCOMPARE(changes, 2);
        QVERIFY(f.control<QPushButton>("dataExport")->isEnabled());
    }
    /** @brief A pending boundary mutation cannot hide behind an unrelated temporary-layer insertion. */
    void nativePreviewDoesNotAdoptPendingBoundaryRevision()
    {
        Fixture f;
        f.polygonMode();
        QVERIFY(f.record(richCatalogue()));
        f.entry->model->projectValue.previewChanged = [&] {
            f.host.viewChange(f.entry, ViewEventKind::LayerAdded, QUuid::createUuid());
        };
        QVERIFY(f.review());
        QCOMPARE(f.rows(), 3);
        f.entry->model->projectValue.boundary = polygon(true);
        f.host.viewChange(f.entry, ViewEventKind::LayerFeaturesChanged, f.attachment());
        f.entry->model->projectValue.boundary = polygon();
        f.host.viewChange(f.entry, ViewEventKind::LayerFeaturesChanged, f.attachment());
        f.select();
        QVERIFY(!f.control<QPushButton>("dataExport")->isEnabled());
        f.host.drain();
        QTRY_COMPARE(f.rows(), 0);
        QVERIFY(!f.hasOverlay());
        QVERIFY(!f.control<QPushButton>("dataExport")->isEnabled());
        QVERIFY(f.entry->model->projectValue.exports.isEmpty());
    }
    /** @brief A native source mutation nested in preview insertion never gains export authority. */
    void nativePreviewNestedSourceMutation_data()
    {
        QTest::addColumn<bool>("boundary");
        QTest::newRow("changed viewport retained for inspection") << false;
        QTest::newRow("changed chosen polygon cleared") << true;
    }
    void nativePreviewNestedSourceMutation()
    {
        QFETCH(bool, boundary);
        Fixture f;
        if (boundary)
            f.polygonMode();
        QVERIFY(f.record(richCatalogue()));
        f.entry->model->projectValue.previewChanged = [&] {
            f.host.viewChange(f.entry, ViewEventKind::LayerAdded, QUuid::createUuid());
            if (boundary) {
                f.entry->model->projectValue.boundary = polygon(true);
                f.host.viewChange(f.entry, ViewEventKind::LayerFeaturesChanged, f.attachment());
            } else {
                f.entry->model->projectValue.viewport = polygon(true);
                f.host.stateChange(f.entry, StateChange::Viewport);
            }
        };
        QVERIFY(f.review());
        QCOMPARE(f.rows(), boundary ? 0 : 3);
        if (boundary)
            QVERIFY(!f.hasOverlay());
        else
            QCOMPARE(f.overlayCount(), 3);
        QVERIFY(!f.control<QPushButton>("dataExport")->isEnabled());
        f.host.drain();
        QCOMPARE(f.rows(), boundary ? 0 : 3);
        QVERIFY(!f.control<QPushButton>("dataExport")->isEnabled());
        QVERIFY(f.entry->model->projectValue.exports.isEmpty());
    }
    /** @brief The current location exposes every saved property without flattening nested JSON or null. */
    void locationAttributesMatchSavedProperties()
    {
        Fixture f;
        QVERIFY(f.record(richCatalogue()));
        QVERIFY(f.review());
        QVERIFY(f.filter("catalogue_id", "Equals", "site-one"));
        f.select();
        QCOMPARE(f.attribute("catalogue_id").toString(), "site-one");
        QCOMPARE(f.attribute("catalogue_title").toString(), "Alpha River");
        QCOMPARE(f.attribute("site_type").toString(), "ST");
        QCOMPARE(f.attribute("drainage_area").toString().toDouble(), 12.5);
        QCOMPARE(f.attribute("agency").toString(), "USGS");
        QCOMPARE(f.attribute("only_alpha").toString(), "recorded");
        QVERIFY(f.attribute("nullable").isValid());
        QCOMPARE(f.attribute("nullable").toString(), "null");
        QCOMPARE(QJsonDocument::fromJson(f.attribute("quality").toString().toUtf8()).object(),
                 (QJsonObject{{"approved", true}}));
        QVERIFY(f.control<QComboBox>("dataFilterField")->findData("quality") >= 0);
        f.exportSelection();
        QCOMPARE(f.entry->model->projectValue.exports.size(), 1);
        QFile file(f.entry->model->projectValue.exports.first());
        QVERIFY(file.open(QIODevice::ReadOnly));
        const auto properties = QJsonDocument::fromJson(file.readAll())
                                    .object()["features"]
                                    .toArray()
                                    .first()
                                    .toObject()["properties"]
                                    .toObject();
        QVERIFY(properties["nullable"].isNull());
        QCOMPARE(properties["quality"].toObject(), (QJsonObject{{"approved", true}}));
        QVERIFY(f.filter("catalogue_id", "Equals", "site-two"));
        f.select();
        QCOMPARE(f.attribute("catalogue_id").toString(), "site-two");
        QVERIFY(!f.attribute("only_alpha").isValid());
        QCOMPARE(QJsonDocument::fromJson(f.attribute("measurements").toString().toUtf8()).array(),
                 (QJsonArray{"flow", "stage"}));
    }
    /** @brief Local search is literal and case-insensitive, updates map visibility, and makes no request. */
    void literalFilterKeepsTableAndMapTogether()
    {
        Fixture f;
        QVERIFY(f.record(richCatalogue()));
        QVERIFY(f.review());
        const auto jobs = f.host.jobs.size();
        QVERIFY(f.filter({}, "Contains", "alpha RIVER"));
        QCOMPARE(f.rows(), 1);
        QCOMPARE(f.overlayCount(), 1);
        QCOMPARE(f.control<QTableView>("dataCatalogue")->model()->index(0, 1).data().toString(),
                 "site-one");
        QVERIFY(f.filter({}, "Contains", ".*"));
        QCOMPARE(f.rows(), 0);
        QCOMPARE(f.overlayCount(), 0);
        QVERIFY(!f.control<QPushButton>("dataExport")->isEnabled());
        QCOMPARE(f.control<QTableView>("dataAttributes")->model()->rowCount(), 0);
        f.control<QPushButton>("dataClearFilter")->click();
        QCOMPARE(f.rows(), 3);
        QCOMPARE(f.overlayCount(), 3);
        QCOMPARE(f.host.jobs.size(), jobs);
    }
    /** @brief Numeric, exact-value and presence predicates work across heterogeneous source attributes. */
    void attributePredicatesUseSavedValues()
    {
        Fixture f;
        QVERIFY(f.record(richCatalogue()));
        QVERIFY(f.review());
        QVERIFY(f.filter("site_type", "Equals", "st"));
        QCOMPARE(f.rows(), 1);
        QCOMPARE(f.overlayCount(), 1);
        QVERIFY(f.filter("drainage_area", "At least", "20"));
        QCOMPARE(f.rows(), 1);
        QCOMPARE(f.control<QTableView>("dataCatalogue")->model()->index(0, 1).data().toString(),
                 "site-two");
        QVERIFY(f.filter("drainage_area", "At most", "12.5"));
        QCOMPARE(f.rows(), 2);
        QCOMPARE(f.overlayCount(), 2);
        QVERIFY(f.filter("drainage_area", "At least", "not a number"));
        QCOMPARE(f.rows(), 0);
        QVERIFY(f.filter("only_alpha", "Has value"));
        QCOMPARE(f.rows(), 1);
        QVERIFY(f.filter("only_alpha", "Is missing"));
        QCOMPARE(f.rows(), 2);
        QVERIFY(f.filter("nullable", "Is missing"));
        QCOMPARE(f.rows(), 3);
        QVERIFY(f.filter("nullable", "Has value"));
        QCOMPARE(f.rows(), 0);
    }
    /** @brief Filtered-out selected locations cannot become hidden export candidates after sorting. */
    void filteringCannotExportHiddenSelection()
    {
        Fixture f;
        QVERIFY(f.record(richCatalogue()));
        QVERIFY(f.review());
        QVERIFY(f.filter("catalogue_id", "Equals", "site-two"));
        f.select();
        QVERIFY(f.control<QPushButton>("dataExport")->isEnabled());
        QVERIFY(f.filter("catalogue_id", "Equals", "site-one"));
        QCOMPARE(f.rows(), 1);
        QVERIFY(!f.control<QPushButton>("dataExport")->isEnabled());
        f.exportSelection();
        QVERIFY(f.entry->model->projectValue.exports.isEmpty());
        f.select();
        f.exportSelection();
        QCOMPARE(f.entry->model->projectValue.exports.size(), 1);
        QFile file(f.entry->model->projectValue.exports.first());
        QVERIFY(file.open(QIODevice::ReadOnly));
        const auto features =
            QJsonDocument::fromJson(file.readAll()).object()["features"].toArray();
        QCOMPARE(features.size(), 1);
        QCOMPARE(features.first().toObject()["id"].toString(), "site-one");
    }
    /** @brief Panning retains inspectable/filterable gages but never permits export under stale authority. */
    void retainedViewportPreviewIsInspectionOnlyUntilReviewed()
    {
        Fixture f;
        QVERIFY(f.record(richCatalogue()));
        QVERIFY(f.review());
        f.select();
        // An undelivered model revision refuses the export and retains the display.
        f.host.stateChange(f.entry, StateChange::Model);
        f.exportSelection();
        f.host.drain();
        QVERIFY(f.entry->model->projectValue.exports.isEmpty());
        QCOMPARE(f.rows(), 3);
        QCOMPARE(f.overlayCount(), 3);
        QVERIFY(f.filter("agency", "Equals", "USGS"));
        QCOMPARE(f.rows(), 2);
        QCOMPARE(f.overlayCount(), 2);
        f.select();
        QVERIFY(f.attribute("catalogue_id").isValid());
        QVERIFY(!f.control<QPushButton>("dataExport")->isEnabled());
        f.exportSelection();
        QVERIFY(f.entry->model->projectValue.exports.isEmpty());
        f.entry->model->open = false;
        f.entry->state.status = SessionStatus::Closing;
        f.host.stateChange(f.entry, {}, StateEventKind::SessionClosing);
        f.host.drain();
        QCOMPARE(f.rows(), 0);
        QVERIFY(!f.hasOverlay());
        QCOMPARE(f.control<QTableView>("dataAttributes")->model()->rowCount(), 0);
    }
    /** @brief A pending model revision revokes export immediately while retaining the adopted viewport display. */
    void pendingModelChangeExportKeepsViewportDisplay()
    {
        Fixture f;
        QVERIFY(f.record(richCatalogue()));
        QVERIFY(f.review());
        f.select();
        QVERIFY(f.control<QPushButton>("dataExport")->isEnabled());
        f.host.stateChange(f.entry, StateChange::Model);
        // Deliberately withhold queued state delivery: export queries native
        // authority synchronously before the model observation reaches the UI.
        f.exportSelection();
        QVERIFY(f.entry->model->projectValue.exports.isEmpty());
        QVERIFY(!f.control<QPushButton>("dataExport")->isEnabled());
        QCOMPARE(f.rows(), 3);
        QCOMPARE(f.overlayCount(), 3);
        f.host.drain();
        QCOMPARE(f.rows(), 3);
        QCOMPARE(f.overlayCount(), 3);
        QVERIFY(f.filter("site_type", "Equals", "GW"));
        QCOMPARE(f.rows(), 1);
        QCOMPARE(f.overlayCount(), 1);
        f.select();
        QCOMPARE(f.attribute("catalogue_id").toString(), "site-two");
        QVERIFY(!f.control<QPushButton>("dataExport")->isEnabled());
    }
    /** @brief A retained viewport display cannot redraw onto an already committed new native load. */
    void queuedReloadClearsRetainedDisplayBeforeFilterPublication_data()
    {
        QTest::addColumn<bool>("closing");
        QTest::newRow("reload") << false;
        QTest::newRow("closing") << true;
    }
    /** @brief Queued reload and close changes cannot publish old display geometry. */
    void queuedReloadClearsRetainedDisplayBeforeFilterPublication()
    {
        QFETCH(bool, closing);
        Fixture f;
        QVERIFY(f.record(richCatalogue()));
        QVERIFY(f.review());
        f.entry->model->projectValue.viewport = polygon(true);
        f.host.stateChange(f.entry, StateChange::Viewport);
        f.host.drain();
        QCOMPARE(f.rows(), 3);
        QCOMPARE(f.overlayCount(), 3);
        const auto writes = f.entry->model->projectValue.overlayWrites;
        if (closing) {
            f.entry->model->open = false;
            f.entry->state.status = SessionStatus::Closing;
            f.host.stateChange(f.entry, {}, StateEventKind::SessionClosing);
        } else {
            ++f.entry->state.loadGeneration;
            f.entry->state.status = SessionStatus::Loading;
            f.host.stateChange(f.entry, {}, StateEventKind::LoadStarted);
        }
        // Keep the lifecycle notification queued while an ordinary local filter
        // tries to refresh the stale display against current native readiness.
        QVERIFY(f.filter("agency", "Equals", "USGS"));
        QCOMPARE(f.rows(), 0);
        QVERIFY(!f.hasOverlay());
        QCOMPARE(f.entry->model->projectValue.overlayWrites, writes);
        QVERIFY(!f.control<QPushButton>("dataExport")->isEnabled());
        f.exportSelection();
        QVERIFY(f.entry->model->projectValue.exports.isEmpty());
        f.host.drain();
        QCOMPARE(f.rows(), 0);
        QVERIFY(!f.hasOverlay());
    }
    /** @brief A nested current-location change cannot mix the previous location's remaining properties. */
    void nestedCurrentLocationKeepsOneCompleteAttributeRecord()
    {
        Fixture f;
        QVERIFY(f.record(richCatalogue()));
        QVERIFY(f.review());
        auto *table = f.control<QTableView>("dataCatalogue");
        table->sortByColumn(0, Qt::AscendingOrder);
        QCoreApplication::processEvents();
        QCOMPARE(table->model()->index(0, 1).data().toString(), "site-one");
        QCOMPARE(table->model()->index(1, 1).data().toString(), "site-two");
        auto *attributes = f.control<QTableView>("dataAttributes")->model();
        table->setCurrentIndex(QModelIndex{});
        bool moved = false;
        connect(attributes, &QAbstractItemModel::rowsInserted, f.panel.get(), [&] {
            if (moved)
                return;
            moved = true;
            table->setCurrentIndex(table->model()->index(1, 0));
            table->selectRow(1);
        });
        table->setCurrentIndex(table->model()->index(0, 0));
        QVERIFY(moved);
        QCOMPARE(f.attribute("catalogue_id").toString(), "site-two");
        QCOMPARE(f.attribute("catalogue_title").toString(), "Beta Creek");
        QVERIFY(!f.attribute("only_alpha").isValid());
        QVERIFY(!f.attribute("nullable").isValid());
        QVERIFY(!f.attribute("quality").isValid());
        const auto properties = QJsonDocument::fromJson(richCatalogue())
                                    .object()["features"]
                                    .toArray()
                                    .at(1)
                                    .toObject()["properties"]
                                    .toObject();
        QCOMPARE(attributes->rowCount(), properties.size() + 2);
        QCOMPARE(QJsonDocument::fromJson(f.attribute("measurements").toString().toUtf8()).array(),
                 (QJsonArray{"flow", "stage"}));
    }
    /** @brief Explicit source changes retire the displayed gage set and its location inspector. */
    void sourceChangeClearsRetainedLocationAttributes()
    {
        Fixture f;
        QVERIFY(f.record(richCatalogue()));
        QVERIFY(f.review());
        f.select();
        QVERIFY(f.attribute("catalogue_id").isValid());
        f.control<QComboBox>("dataSource")->setCurrentIndex(1);
        QCOMPARE(f.rows(), 0);
        QVERIFY(!f.hasOverlay());
        QCOMPARE(f.control<QTableView>("dataAttributes")->model()->rowCount(), 0);
    }
    /** @brief Loading/failed sessions cannot start a review; a Ready event enables the same facade. */
    void readinessIsAuthoritative()
    {
        Fixture f;
        f.entry->state.status = SessionStatus::Loading;
        f.host.stateChange(f.entry, {}, StateEventKind::LoadStarted);
        QVERIFY(!f.review());
        f.entry->state.status = SessionStatus::Ready;
        f.host.stateChange(f.entry, {}, StateEventKind::ModelReady);
        f.host.drain();
        QVERIFY(f.review());
        QCOMPARE(f.rows(), 1);
    }
    /** @brief Both area modes keep the area frozen at Preview; only Preview searches a moved viewport. */
    void viewportScopeMatchesAreaMode()
    {
        Fixture f;
        QVERIFY(f.review());
        f.entry->model->projectValue.viewport = polygon(true);
        f.host.stateChange(f.entry, StateChange::Viewport);
        f.host.drain();
        QCOMPARE(f.rows(), 1);
        QVERIFY(f.hasOverlay());
        f.select();
        QVERIFY(f.control<QPushButton>("dataExport")->isEnabled());
        f.exportSelection();
        QCOMPARE(f.entry->model->projectValue.exports.size(), 1);
        QFile file(f.entry->model->projectValue.exports.first());
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(QJsonDocument::fromJson(file.readAll()).object()["review"].toObject()["area"].toObject(),
                 polygon());
        QVERIFY(f.review()); // The moved viewport contains no recorded site.
        QCOMPARE(f.rows(), 0);
        f.polygonMode();
        QVERIFY(f.review());
        const auto overlayWrites = f.entry->model->projectValue.overlayWrites;
        f.entry->model->projectValue.viewport = polygon();
        f.host.stateChange(f.entry, StateChange::Viewport);
        f.host.drain();
        QCOMPARE(f.rows(), 1);
        QTRY_VERIFY(f.entry->model->projectValue.overlayWrites > overlayWrites);
        f.select();
        QVERIFY(f.control<QPushButton>("dataExport")->isEnabled());
    }
    /** @brief Exercise each chosen-boundary reason independently of title/persistent source IDs. */
    void selectedBoundaryInvalidations_data()
    {
        QTest::addColumn<int>("kind");
        for (const auto kind :
             {ViewEventKind::LayerMetadataChanged, ViewEventKind::LayerSchemaChanged,
              ViewEventKind::LayerFeaturesChanged, ViewEventKind::LayerSelectionChanged,
              ViewEventKind::LayerFilterChanged, ViewEventKind::LayerCrsChanged,
              ViewEventKind::LayerSourceChanged, ViewEventKind::LayerRemoved})
            QTest::newRow(qPrintable(QString::number(int(kind)))) << int(kind);
    }
    void selectedBoundaryInvalidations()
    {
        QFETCH(int, kind);
        Fixture f;
        f.polygonMode();
        QVERIFY(f.review());
        f.host.viewChange(f.entry, ViewEventKind(kind), f.attachment());
        f.host.drain();
        QCOMPARE(f.rows(), 0);
        QVERIFY(!f.hasOverlay());
        QVERIFY(!f.control<QPushButton>("dataExport")->isEnabled());
    }
    /** @brief Ignore other attachments and presentation/results changes after their safe deferred rebase. */
    void unrelatedChangesPreserveReview()
    {
        Fixture f;
        f.polygonMode();
        QVERIFY(f.review());
        f.host.viewChange(f.entry, ViewEventKind::LayerFeaturesChanged, QUuid::createUuid());
        f.host.viewChange(f.entry, ViewEventKind::LayerStyleChanged, f.attachment());
        f.host.viewChange(f.entry, ViewEventKind::ResultsTimeChanged, QUuid::createUuid());
        f.host.stateChange(f.entry, StateChange::Selection);
        f.host.activate(nullptr);
        f.host.drain();
        QCOMPARE(f.rows(), 1);
        f.select();
        QVERIFY(f.control<QPushButton>("dataExport")->isEnabled());
        QVERIFY(f.hasOverlay());
    }
    /** @brief Utility/model tab focus preserves the same reviewed source and its selected download rows. */
    void tabRoundTripKeepsDownloadSelection_data()
    {
        QTest::addColumn<bool>("boundary");
        QTest::newRow("viewport") << false;
        QTest::newRow("polygon") << true;
    }
    void tabRoundTripKeepsDownloadSelection()
    {
        QFETCH(bool, boundary);
        Fixture f;
        if (boundary)
            f.polygonMode();
        QVERIFY(f.review());
        f.select();
        QVERIFY(f.control<QPushButton>("dataDownload")->isEnabled());
        const auto writes = f.entry->model->projectValue.overlayWrites;
        const auto overlay = f.entry->model->projectValue.overlay;
        f.host.activate(nullptr);
        f.host.active = nullptr; // A utility document need not expose a cached model facade.
        f.host.drain();
        QCOMPARE(f.rows(), 1);
        QCOMPARE(f.entry->model->projectValue.overlayWrites, writes);
        QVERIFY(f.control<QPushButton>("dataDownload")->isEnabled());
        f.host.activate(f.entry);
        f.host.drain();
        QCOMPARE(f.entry->model->projectValue.overlay, overlay);
        QCOMPARE(f.entry->model->projectValue.overlayWrites, writes);
        QVERIFY(f.control<QPushButton>("dataDownload")->isEnabled());
    }
    /** @brief The real Data sources/Layers dock tabs preserve viewport space and reviewed downloads. */
    void realDockTabRoundTripKeepsDownload_data()
    {
        QTest::addColumn<bool>("boundary");
        QTest::addColumn<bool>("pendingPresentation");
        QTest::newRow("viewport clean") << false << false;
        QTest::newRow("viewport pending presentation") << false << true;
        QTest::newRow("polygon clean") << true << false;
        QTest::newRow("polygon pending presentation") << true << true;
    }
    void realDockTabRoundTripKeepsDownload()
    {
        QFETCH(bool, boundary);
        QFETCH(bool, pendingPresentation);
        QMainWindow window;
        window.resize(1400, 950);
        auto *canvas = new QWidget(&window);
        canvas->setMinimumSize(400, 240);
        window.setCentralWidget(canvas);
        Fixture f;
        if (boundary)
            f.polygonMode();
        auto *layers = new QDockWidget("Layers", &window);
        auto *layerContent = new QWidget(layers);
        auto *layerLayout = new QVBoxLayout(layerContent);
        layerLayout->addWidget(new QLineEdit(layerContent));
        layerLayout->addWidget(new QTreeView(layerContent));
        layers->setWidget(layerContent);
        auto *data = new QDockWidget("Data sources", &window);
        data->setWidget(f.panel.get());
        const auto detach = qScopeGuard([&] {
            data->setWidget(nullptr);
            f.panel->setParent(nullptr);
        });
        window.addDockWidget(Qt::LeftDockWidgetArea, layers);
        window.addDockWidget(Qt::LeftDockWidgetArea, data);
        window.tabifyDockWidget(layers, data);
        data->raise();
        window.show();
        QCoreApplication::processEvents();
        QTabBar *tabs = nullptr;
        int dataTab = -1, layersTab = -1;
        for (auto *bar : window.findChildren<QTabBar *>()) {
            for (int i = 0; i < bar->count(); ++i) {
                if (bar->tabText(i) == "Data sources") {
                    tabs = bar;
                    dataTab = i;
                } else if (bar->tabText(i) == "Layers")
                    layersTab = i;
            }
        }
        QVERIFY(tabs);
        QVERIFY(dataTab >= 0 && layersTab >= 0);
        QTest::mouseClick(tabs, Qt::LeftButton, Qt::NoModifier, tabs->tabRect(dataTab).center());
        QCoreApplication::processEvents();
        QCOMPARE(tabs->currentIndex(), dataTab);
        QVERIFY(f.review());
        f.select();
        QVERIFY(f.control<QPushButton>("dataDownload")->isEnabled());
        QCoreApplication::processEvents();
        window.layout()->activate();
        QCoreApplication::processEvents();
        const QRect canvasRect = canvas->geometry();
        const auto population = f.entry->model->projectValue.overlay;
        const auto jobs = f.host.jobs.size();
        int replacements = 0;
        f.entry->model->projectValue.previewChanged = [&] { ++replacements; };
        QTest::mouseClick(tabs, Qt::LeftButton, Qt::NoModifier, tabs->tabRect(layersTab).center());
        QCoreApplication::processEvents();
        QCOMPARE(tabs->currentIndex(), layersTab);
        QCOMPARE(canvas->geometry(), canvasRect);
        if (pendingPresentation) {
            f.host.stateChange(f.entry, StateChange::Layers);
            f.host.viewChange(f.entry, ViewEventKind::LayerMetadataChanged, QUuid::createUuid());
        }
        QTest::mouseClick(tabs, Qt::LeftButton, Qt::NoModifier, tabs->tabRect(dataTab).center());
        QCoreApplication::processEvents();
        QCOMPARE(tabs->currentIndex(), dataTab);
        QCOMPARE(canvas->geometry(), canvasRect);
        f.control<QTableView>("dataCatalogue")->clearSelection();
        f.select();
        if (pendingPresentation)
            QVERIFY(!f.control<QPushButton>("dataDownload")->isEnabled());
        f.host.drain();
        QTRY_VERIFY(f.control<QPushButton>("dataDownload")->isEnabled());
        QCOMPARE(f.entry->model->projectValue.overlay, population);
        QCOMPARE(replacements, 0);
        QCOMPARE(f.host.jobs.size(), jobs);
        f.entry->model->projectValue.previewChanged = {};
    }
    /** @brief A delivered unchanged viewport category does not revoke a completed viewport review. */
    void unchangedViewportTabNotificationKeepsDownload()
    {
        Fixture f;
        QVERIFY(f.review());
        f.select();
        const auto writes = f.entry->model->projectValue.overlayWrites;
        f.host.activate(nullptr);
        f.host.stateChange(f.entry, StateChange::Viewport);
        f.host.drain();
        QCOMPARE(f.rows(), 1);
        QCOMPARE(f.entry->model->projectValue.overlayWrites, writes);
        QVERIFY(f.control<QPushButton>("dataDownload")->isEnabled());
        f.host.activate(f.entry);
        f.host.drain();
        QVERIFY(f.control<QPushButton>("dataDownload")->isEnabled());
    }
    /** @brief Resizes, pans and canvas CRS changes keep a completed viewport review and its
     * frozen area; a map selection made meanwhile waits only for the queued revision. */
    void viewportChangesKeepCompletedDownload_data()
    {
        QTest::addColumn<int>("change");
        QTest::newRow("transient resize") << 0;
        QTest::newRow("pan or zoom") << 1;
        QTest::newRow("canvas CRS") << 2;
    }
    void viewportChangesKeepCompletedDownload()
    {
        QFETCH(int, change);
        Fixture f;
        QVERIFY(f.record(richCatalogue()));
        QVERIFY(f.review());
        f.select();
        QVERIFY(f.control<QPushButton>("dataDownload")->isEnabled());
        const auto jobs = f.host.jobs.size();
        auto &project = f.entry->model->projectValue;
        const auto area = project.viewport;
        const auto population = project.overlay;
        if (change == 2)
            project.projectCrs = "EPSG:3857";
        else
            project.viewport = polygon(true);
        f.host.stateChange(f.entry, StateChange::Viewport);
        if (change == 0) {
            project.viewport = area;
            f.host.stateChange(f.entry, StateChange::Viewport);
        }
        f.host.mapSelect(f.entry, {project.previewFids[1]});
        f.host.drain();
        QCOMPARE(f.selectedIds(), QStringList{"site-two"});
        QTRY_VERIFY(f.control<QPushButton>("dataDownload")->isEnabled());
        QVERIFY(f.control<QPushButton>("dataExport")->isEnabled());
        QCOMPARE(f.rows(), 3);
        QCOMPARE(project.overlay, population);
        QCOMPARE(f.host.jobs.size(), jobs);
        f.exportSelection();
        QCOMPARE(project.exports.size(), 1);
        QFile file(project.exports.first());
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(QJsonDocument::fromJson(file.readAll()).object()["review"].toObject()["area"].toObject(),
                 area);
    }
    /** @brief Viewport changes keep pending preview, payload transfer and native import work. */
    void viewportChangeKeepsActiveWork_data()
    {
        QTest::addColumn<int>("mode");
        QTest::newRow("preview parser") << 0;
        QTest::newRow("payload download") << 1;
        QTest::newRow("native import") << 2;
    }
    void viewportChangeKeepsActiveWork()
    {
        QFETCH(int, mode);
        Fixture f;
        QVERIFY(f.review(mode != 0));
        auto *payload = f.panel->findChild<OpenSWMM::Data::PayloadDownload *>();
        QVERIFY(payload);
        QSignalSpy finished(payload, &OpenSWMM::Data::PayloadDownload::finished);
        Job *job = mode == 0 ? f.host.jobs.back().get() : nullptr;
        QPointer<Reply> reply;
        if (mode == 1) {
            f.select();
            f.panel->downloadTo(f.directory + "/transfer");
            QVERIFY(payload->isRunning());
            QTRY_VERIFY(f.host.policy.reply);
            reply = f.host.policy.reply;
        } else if (mode == 2) {
            OpenSWMM::Data::PayloadResult saved;
            saved.importId = QUuid::createUuid();
            saved.observationCount = 1;
            saved.manifestPath = f.directory + "/manifest.json";
            saved.descriptorPath = f.directory + "/timeseries-import.json";
            payload->finished(saved);
            QCOMPARE(f.host.imports.size(), 1);
            job = f.host.jobs.back().get();
        }
        f.entry->model->projectValue.viewport = polygon(true);
        f.host.stateChange(f.entry, StateChange::Viewport);
        f.host.drain();
        QCoreApplication::processEvents(); // Deferred revision reconciliation.
        if (job)
            QVERIFY(!job->cancelled);
        if (mode == 0) {
            job->complete();
            QCOMPARE(f.rows(), 1);
            f.select();
            QTRY_VERIFY(f.control<QPushButton>("dataDownload")->isEnabled());
        } else if (mode == 1) {
            QVERIFY(payload->isRunning());
            QVERIFY(reply);
            QCOMPARE(reply->error(), QNetworkReply::NoError);
            QCOMPARE(finished.size(), 0);
        } else {
            TimeSeriesImportResult late;
            late.layerId = "late-layer";
            late.seriesCount = 1;
            f.host.importFinished(late);
            QVERIFY(f.control<QLabel>("dataStatus")->text().startsWith("Added 1 series"));
            QCOMPARE(f.host.activeProgress(), 0);
        }
    }
    /** @brief Selection waits for harmless queued tab/presentation revisions and recovers after delivery. */
    void queuedTabRevisionRestoresDownload_data()
    {
        QTest::addColumn<bool>("boundary");
        QTest::newRow("viewport") << false;
        QTest::newRow("polygon") << true;
    }
    void queuedTabRevisionRestoresDownload()
    {
        QFETCH(bool, boundary);
        Fixture f;
        if (boundary)
            f.polygonMode();
        QVERIFY(f.review());
        f.select();
        QVERIFY(f.control<QPushButton>("dataDownload")->isEnabled());
        f.host.activate(nullptr);
        f.host.stateChange(f.entry, StateChange::Layers);
        f.host.viewChange(f.entry, ViewEventKind::LayerStyleChanged, f.attachment());
        auto *table = f.control<QTableView>("dataCatalogue");
        table->clearSelection();
        f.select();
        QCOMPARE(f.rows(), 1);
        QVERIFY(f.hasOverlay());
        QVERIFY(!f.control<QPushButton>("dataDownload")->isEnabled());
        f.host.drain();
        QTRY_VERIFY(f.control<QPushButton>("dataDownload")->isEnabled());
        QVERIFY(f.control<QPushButton>("dataExport")->isEnabled());
        f.host.activate(f.entry);
        f.host.drain();
        QVERIFY(f.control<QPushButton>("dataDownload")->isEnabled());
    }
    /** @brief Read-only selection never authorizes an undelivered replacement of the reviewed boundary source. */
    void queuedSourceReplacementStillRevokesDownload()
    {
        Fixture f;
        f.polygonMode();
        QVERIFY(f.review());
        f.host.viewChange(f.entry, ViewEventKind::LayerSourceChanged, f.attachment());
        f.select();
        QCOMPARE(f.rows(), 1);
        QVERIFY(!f.control<QPushButton>("dataDownload")->isEnabled());
        f.host.drain();
        QCOMPARE(f.rows(), 0);
        QVERIFY(!f.hasOverlay());
        QVERIFY(!f.control<QPushButton>("dataDownload")->isEnabled());
    }
    /** @brief Equal selection counts do not hide replacement FIDs before deferred notification. */
    void sameCountSelectionCannotPublishStaleResult()
    {
        Fixture f;
        f.polygonMode();
        QVERIFY(f.review(false));
        auto &features = f.entry->model->projectValue.features;
        features[0]["selected"] = false;
        features[1]["selected"] = true;
        QCOMPARE(f.entry->view.layers.first().selectionCount, qint64(1));
        f.host.viewChange(f.entry, ViewEventKind::LayerSelectionChanged, f.attachment());
        f.host.jobs.back()->complete();
        QCOMPARE(f.rows(), 0);
        QVERIFY(!f.hasOverlay());
    }
    /** @brief Pending geometry and even change-then-revert revisions cannot publish a latched worker. */
    void pendingNativeChangesFenceCompletion_data()
    {
        QTest::addColumn<bool>("revert");
        QTest::newRow("geometry changed") << false;
        QTest::newRow("geometry changed and reverted") << true;
    }
    void pendingNativeChangesFenceCompletion()
    {
        QFETCH(bool, revert);
        Fixture f;
        f.polygonMode();
        QVERIFY(f.review(false));
        f.entry->model->projectValue.boundary = polygon(true);
        f.host.viewChange(f.entry, ViewEventKind::LayerFeaturesChanged, f.attachment());
        if (revert) {
            f.entry->model->projectValue.boundary = polygon();
            f.host.viewChange(f.entry, ViewEventKind::LayerFeaturesChanged, f.attachment());
        }
        f.host.jobs.back()->complete();
        QCOMPARE(f.rows(), 0);
        QVERIFY(!f.hasOverlay());
        QVERIFY(!f.control<QPushButton>("dataExport")->isEnabled());
    }
    /** @brief Explicit export revalidates even while the invalidation event remains undispatched. */
    void pendingChangesFenceExportAndOverlay()
    {
        Fixture f;
        f.polygonMode();
        QVERIFY(f.review());
        f.select();
        f.entry->view.layers.first().filter = "id=2";
        f.host.viewChange(f.entry, ViewEventKind::LayerFilterChanged, f.attachment());
        f.exportSelection();
        QVERIFY(f.entry->model->projectValue.exports.isEmpty());
        QVERIFY(!f.hasOverlay());
        QCOMPARE(QDir(f.entry->model->projectValue.folder + "/data/external/preview-layers")
                     .entryList({"*.geojson"}, QDir::Files)
                     .size(),
                 0);
    }
    /** @brief A successful network body cannot be cached or displayed after its source changed. */
    void pendingChangesFenceNetworkCachePublication()
    {
        Fixture f;
        f.polygonMode();
        f.control<QPushButton>("dataPreview")->click();
        QVERIFY(f.host.policy.reply);
        f.host.policy.reply->complete();
        QCOMPARE(f.host.jobs.size(), size_t(1));
        f.entry->view.layers.first().crsWkt = "Changed CRS";
        f.host.viewChange(f.entry, ViewEventKind::LayerCrsChanged, f.attachment());
        f.host.jobs.back()->complete();
        QCOMPARE(f.rows(), 0);
        QVERIFY(!f.hasOverlay());
        const QDir cache(f.entry->model->projectValue.folder + "/data/external/catalogues");
        QVERIFY(cache.entryList(QDir::Files).isEmpty());
    }
    /** @brief A late earlier completion cannot replace a new successful review. */
    void cancelledOldCompletionCannotReplaceNewReview()
    {
        Fixture f;
        QVERIFY(f.review(false));
        auto *old = f.host.jobs.back().get();
        f.panel->cancel();
        QVERIFY(f.review());
        const auto writes = f.entry->model->projectValue.overlayWrites;
        old->complete();
        QCOMPARE(f.rows(), 1);
        QCOMPARE(f.entry->model->projectValue.overlayWrites, writes);
        QVERIFY(f.hasOverlay());
    }
    /** @brief Same persistent identities do not merge distinct open sessions; utility focus keeps the chosen model. */
    void duplicatePersistentIdsUseExactSession()
    {
        Fixture f;
        QVERIFY(f.review(false));
        auto *old = f.host.jobs.back().get();
        auto *second = f.host.addModel(f.directory + "/two", f.entry->state.instanceId);
        QCOMPARE(second->model->id(), f.entry->model->id());
        QVERIFY(second->state.sessionId != f.entry->state.sessionId);
        f.host.activate(second);
        f.host.drain();
        old->complete();
        QCOMPARE(f.rows(), 0);
        QVERIFY(f.review());
        QVERIFY(!second->model->projectValue.overlay.isEmpty());
        QVERIFY(f.entry->model->projectValue.overlay.isEmpty());
        f.host.activate(nullptr);
        f.host.drain();
        QCOMPARE(f.rows(), 1);
    }
    /** @brief Reload generation and closed-session races refuse stale results before observations arrive. */
    void reloadAndClosureFenceCompletion_data()
    {
        QTest::addColumn<bool>("close");
        QTest::newRow("new load generation") << false;
        QTest::newRow("closed session") << true;
    }
    void reloadAndClosureFenceCompletion()
    {
        QFETCH(bool, close);
        Fixture f;
        QVERIFY(f.review(false));
        if (close) {
            f.entry->model->open = false;
            f.entry->state.status = SessionStatus::Closing;
            f.host.stateChange(f.entry, {}, StateEventKind::SessionClosing);
        } else {
            ++f.entry->state.loadGeneration;
            f.entry->state.status = SessionStatus::Loading;
            f.host.stateChange(f.entry, {}, StateEventKind::LoadStarted);
        }
        f.host.jobs.back()->complete();
        QCOMPARE(f.rows(), 0);
        QVERIFY(!f.hasOverlay());
        f.host.drain();
        if (close) {
            f.entry = f.host.addModel(f.directory + "/reopened", f.entry->state.instanceId);
            f.host.activate(f.entry);
        } else {
            f.entry->state.status = SessionStatus::Ready;
        }
        f.host.stateChange(f.entry, {}, StateEventKind::ModelReady);
        f.host.drain();
        QVERIFY(f.review());
        QCOMPARE(f.rows(), 1);
    }
    /** @brief Unregistering and recreating a stable layer key retires the prior attachment authority. */
    void reattachedLayerCannotInheritReview()
    {
        Fixture f;
        f.polygonMode();
        QVERIFY(f.review(false));
        const auto old = f.attachment();
        f.entry->view.layers.first().layerSessionId = QUuid::createUuid();
        f.host.viewChange(f.entry, ViewEventKind::LayerRemoved, old);
        f.host.viewChange(f.entry, ViewEventKind::LayerAdded, f.attachment());
        f.host.jobs.back()->complete();
        QCOMPARE(f.rows(), 0);
        QVERIFY(!f.hasOverlay());
        f.host.drain();
        QVERIFY(f.review());
    }
    /** @brief Fresh captures ignore older committed invalidations while destroyed receivers stay inert. */
    void adoptionCutoffAndReceiverDestruction()
    {
        Fixture f;
        f.panel.reset();
        f.host.viewChange(f.entry, ViewEventKind::LayerFeaturesChanged, f.attachment());
        f.panel = std::make_unique<PreviewPanel>(&f.host);
        f.polygonMode();
        QVERIFY(f.review());
        f.host.drain();
        QCOMPARE(f.rows(), 1);
        f.polygonMode();
        f.host.viewChange(f.entry, ViewEventKind::LayerFeaturesChanged, f.attachment());
        QVERIFY(f.review());
        f.host.drain();
        QCOMPARE(f.rows(), 1);
        QVERIFY(f.review(false));
        auto *pending = f.host.jobs.back().get();
        f.panel.reset();
        pending->complete();
        f.host.drain();
        QVERIFY(f.entry->model->projectValue.overlay.isEmpty());
    }
    /** @brief A stopped source cannot publish an already completed private worker. */
    void hostShutdownSuppressesPublication()
    {
        Fixture f;
        QVERIFY(f.review(false));
        f.host.prepareShutdown();
        f.host.jobs.back()->complete();
        QCOMPARE(f.rows(), 0);
        QVERIFY(!f.hasOverlay());
    }
    /** @brief A host callout deleting the panel cannot assign a returned job into dead storage. */
    void receiverCanDisappearDuringJobCreation()
    {
        Fixture f;
        auto *panel = f.panel.release();
        const QPointer<PreviewPanel> guard(panel);
        f.host.jobCreated = [panel] { delete panel; };
        panel->loadCatalogue(f.recorded);
        QVERIFY(guard.isNull());
        QCOMPARE(f.host.jobs.size(), size_t(1));
        f.host.jobs.back()->complete();
        QVERIFY(f.entry->model->projectValue.overlay.isEmpty());
        QVERIFY(f.entry->model->projectValue.exports.isEmpty());
    }
    /** @brief Native changes during Qt row publication cannot make an incomplete review actionable. */
    void sourceMutationDuringRowPublicationCannotPublishReview()
    {
        Fixture f;
        QVERIFY(f.review(false));
        bool changed = false;
        auto *model = f.control<QTableView>("dataCatalogue")->model();
        connect(model, &QAbstractItemModel::rowsInserted, f.panel.get(), [&] {
            if (changed)
                return;
            changed = true;
            f.entry->model->projectValue.viewport = polygon(true);
            f.host.stateChange(f.entry, StateChange::Viewport);
        });
        f.host.jobs.back()->complete();
        QVERIFY(changed);
        QCOMPARE(f.rows(), 0);
        QVERIFY(!f.hasOverlay());
        QVERIFY(!f.control<QPushButton>("dataExport")->isEnabled());
    }
    /** @brief Canvas CRS changes rebase both reviews; only polygon graphics are redrawn. */
    void projectCrsChangeRespectsAreaMode()
    {
        Fixture f;
        f.polygonMode();
        QVERIFY(f.review());
        const auto overlayWrites = f.entry->model->projectValue.overlayWrites;
        f.entry->model->projectValue.projectCrs = "EPSG:3857";
        f.host.stateChange(f.entry, StateChange::Viewport);
        f.host.drain();
        QCOMPARE(f.rows(), 1);
        QTRY_VERIFY(f.entry->model->projectValue.overlayWrites > overlayWrites);
        f.select();
        QVERIFY(f.control<QPushButton>("dataExport")->isEnabled());
        f.control<QComboBox>("dataAreaMode")->setCurrentIndex(0);
        QVERIFY(f.review());
        f.entry->model->projectValue.projectCrs = "EPSG:4326";
        f.host.stateChange(f.entry, StateChange::Viewport);
        f.host.drain();
        QCOMPARE(f.rows(), 1);
        QVERIFY(f.hasOverlay());
        f.select();
        QTRY_VERIFY(f.control<QPushButton>("dataExport")->isEnabled());
    }
    /** @brief A broad session publication cannot rebase over its still-pending
     * same-revision IViewEvents source change. */
    void broadStateBeforeDetailedSourceCannotRebaseReview()
    {
        const auto sourceChangedButDetailsHeld = [](Fixture &f) {
            f.host.stateChange(f.entry, StateChange::Layers);
            ViewEvent detailed;
            detailed.kind = ViewEventKind::LayerSourceChanged;
            detailed.sequence = ++f.host.sequence;
            detailed.session = f.entry->state;
            detailed.layerSessionId = f.attachment();
            detailed.layerId = "boundary";
            f.host.pendingViews.append(detailed);
            // An identical source replacement keeps all copied content fields
            // equal; only the undispatched detailed reason forbids rebasing.
            f.host.drainFirstState();
        };
        Fixture pending;
        pending.polygonMode();
        QVERIFY(pending.review(false));
        sourceChangedButDetailsHeld(pending);
        QCOMPARE(pending.host.pendingViews.size(), 1);
        pending.host.jobs.back()->complete();
        QCOMPARE(pending.rows(), 0);
        QVERIFY(!pending.hasOverlay());
        QVERIFY(!pending.control<QPushButton>("dataExport")->isEnabled());

        Fixture reviewed;
        reviewed.polygonMode();
        QVERIFY(reviewed.review());
        reviewed.select();
        QVERIFY(reviewed.control<QPushButton>("dataExport")->isEnabled());
        sourceChangedButDetailsHeld(reviewed);
        QCOMPARE(reviewed.host.pendingViews.size(), 1);
        reviewed.exportSelection();
        QVERIFY(reviewed.entry->model->projectValue.exports.isEmpty());
        QCOMPARE(reviewed.rows(), 0);
        QVERIFY(!reviewed.hasOverlay());
    }
    /** @brief Reentrant catalogue startup cannot replace the admitted request's data or provenance. */
    void nestedCatalogueStartCannotStealOuterReview()
    {
        Fixture f;
        QVERIFY(f.review());
        auto alternate = QJsonDocument::fromJson(catalogue()).object();
        auto features = alternate["features"].toArray();
        auto feature = features.first().toObject();
        feature["id"] = "nested-site";
        feature["properties"] = QJsonObject{{"monitoring_location_name", "Nested site"}};
        features[0] = feature;
        alternate["features"] = features;
        const auto alternatePath = f.directory + "/alternate-catalogue.json";
        QFile alternateFile(alternatePath);
        QVERIFY(alternateFile.open(QIODevice::WriteOnly));
        const auto bytes = QJsonDocument(alternate).toJson();
        QCOMPARE(alternateFile.write(bytes), qint64(bytes.size()));
        alternateFile.close();
        bool nestedAttempted = false;
        auto *model = f.control<QTableView>("dataCatalogue")->model();
        connect(model, &QAbstractItemModel::rowsRemoved, f.panel.get(), [&] {
            if (nestedAttempted)
                return;
            nestedAttempted = true;
            f.panel->loadCatalogue(alternatePath);
        });
        const auto before = f.host.jobs.size();
        f.panel->loadCatalogue(f.recorded);
        QVERIFY(nestedAttempted);
        QCOMPARE(f.host.jobs.size(), before + 1);
        f.host.jobs.back()->complete();
        QCOMPARE(f.rows(), 1);
        QCOMPARE(model->index(0, 1).data().toString(), "site-one");
        f.select();
        QVERIFY(f.control<QPushButton>("dataExport")->isEnabled());
        f.exportSelection();
        QCOMPARE(f.entry->model->projectValue.exports.size(), 1);
        QFile saved(f.entry->model->projectValue.exports.first());
        QVERIFY(saved.open(QIODevice::ReadOnly));
        const auto exported = QJsonDocument::fromJson(saved.readAll()).object();
        QCOMPARE(exported["features"].toArray().first().toObject()["id"].toString(), "site-one");
        const auto response = exported["review"].toObject()["response"].toObject();
        QCOMPARE(response["kind"].toString(), "saved");
        QCOMPARE(response["path"].toString(), QFileInfo(f.recorded).absoluteFilePath());
    }
};
QTEST_MAIN(PreviewTest)
#include "test_preview.moc"
