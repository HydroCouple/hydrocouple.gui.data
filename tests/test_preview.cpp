/**
 * @file test_preview.cpp
 * @brief Offline public-SDK acceptance of typed review invalidation and delayed publication.
 * @details A copied-state host holds notifications and worker completions independently.
 * Tests exercise the actual panel through ordinary controls and saved catalogues,
 * without native GUI headers, engine handles, remote services or temporary files.
 * @author Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license GPL-3.0-or-later
 */
#include "PreviewPanel.hpp"
#include <sdk/IEventSourceV2.hpp>
#include <sdk/IEventSourceV3.hpp>
#include <sdk/IEventSourceV4.hpp>
#include <sdk/IViewControlsV1.hpp>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPushButton>
#include <QTableView>
#include <QTest>
#include <QThread>
#include <QTimer>
#include <algorithm>
#include <cstring>
#include <memory>
#include <limits>
#include <vector>

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
    QPointer<Reply>
        reply; ///< Latest synthetic reply, guarded through cancellation/deferred deletion.
    bool allowed(const QUrl &) const override { return true; }
    QNetworkAccessManager *shared() override { return this; }

protected:
    QNetworkReply *createRequest(Operation, const QNetworkRequest &request, QIODevice *) override
    {
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
    QStringList exports;                ///< Paths added as ordinary persistent feature layers.
    int overlayWrites = 0;              ///< Count only successful nonempty preview publications.
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
    QString addRasterLayer(const QString &, const QString &, QString *) override { return {}; }
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
        overlay = value;
        ++overlayWrites;
        return true;
    }
    void clearPreview() override { overlay = {}; }
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
class Host final : public QObject,
                   public ExtensionHost,
                   public IEventSourceV2,
                   public IEventSourceV3,
                   public IEventSourceV4,
                   public IViewControlsV1,
                   public ISettings,
                   public IConsole,
                   public IUiHost
{
    Q_OBJECT
    Q_INTERFACES(OpenSWMM::Sdk::IEventSourceV2 OpenSWMM::Sdk::IEventSourceV3
                     OpenSWMM::Sdk::IEventSourceV4 OpenSWMM::Sdk::IViewControlsV1)
public:
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
    quint64 sequence = 10;                         ///< Shared V2/V3/V4 commit watermark.
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
    /** @brief Simulate a later observer entering a nested loop before the detailed V3 event. */
    void drainFirstState()
    {
        if (!pendingState.isEmpty())
            emit stateCommitted(pendingState.takeFirst());
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
    bool executeViewCommand(const ViewCommand &, QString *) override { return false; }
    QList<qint64> selectedLayerFeatures(const QUuid &session, const QUuid &attachment, int offset,
                                        int limit) const override
    {
        QList<qint64> selected;
        for (const auto &entry : entries)
            if (entry->state.sessionId == session)
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
    /** @brief Select the reviewed item and allow the UI to enable explicit GIS export. */
    void select() { control<QTableView>("dataCatalogue")->selectRow(0); }
    /** @brief Return the current chosen attachment, independently from its stable local key. */
    QUuid attachment() const { return entry->view.layers.first().layerSessionId; }
    /** @brief Copy the source's present overlay value. */
    bool hasOverlay() const { return !entry->model->projectValue.overlay.isEmpty(); }
    /** @brief Trigger the real explicit export command through its existing button. */
    void exportSelection() { control<QPushButton>("dataExport")->click(); }
};
}

/** @brief Verify lifecycle, exact review context, pending-state fences and bounded offline publication. */
class PreviewTest final : public QObject
{
    Q_OBJECT
private slots:
    /** @brief Confirm every fixture file can be retained in the configured repository directory. */
    void initTestCase() { QVERIFY(QDir().mkpath(SDK_DATA_PREVIEW_VERIFICATION_DIRECTORY)); }
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
    /** @brief Viewport mutations invalidate viewport review while polygon mode keeps its explicit area. */
    void viewportScopeMatchesAreaMode()
    {
        Fixture f;
        QVERIFY(f.review());
        f.entry->model->projectValue.viewport = polygon(true);
        f.host.stateChange(f.entry, StateChange::Viewport);
        f.host.drain();
        QCOMPARE(f.rows(), 0);
        QVERIFY(!f.hasOverlay());
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
    /** @brief Canvas CRS changes rebase explicit polygon reviews but invalidate viewport reviews. */
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
        QCOMPARE(f.rows(), 0);
        QVERIFY(!f.hasOverlay());
        QVERIFY(!f.control<QPushButton>("dataExport")->isEnabled());
    }
    /** @brief A broad V2 publication cannot rebase over its still-pending same-revision V3 source change. */
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
