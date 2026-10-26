/**
 * @file PreviewPanel.cpp
 * @brief Review-first catalogue workflow, exact area filtering, cache and explicit GIS export.
 * @details Generation counters discard late replies/jobs after area changes. Metadata requests
 * never fetch advertised terrain payload URLs. Cancellation aborts network IO and cooperatively
 * stops parsing. Raw responses and provenance are written only to reviewable project folders.
 * @author Caleb Buahin <caleb.buahin@gmail.com>
 * @date 2026
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license GPL-3.0-or-later
 */
#include "PreviewPanel.hpp"
#include <QCheckBox>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileDialog>
#include <QFormLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QItemSelectionModel>
#include <QLabel>
#include <QNetworkReply>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QPushButton>
#include <QSaveFile>
#include <QSignalBlocker>
#include <QScopeGuard>
#include <QStandardItemModel>
#include <QTableView>
#include <QTimer>
#include <QUuid>
#include <QVBoxLayout>
#include <memory>
#include <limits>

namespace OpenSWMM::Data
{
namespace
{
constexpr qint64 maxResponse = 20 * 1024 * 1024;
/** @brief Atomically write reviewable bytes without leaving a partially published file. */
bool save(const QString &path, const QByteArray &bytes)
{
    QSaveFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
}
}

PreviewPanel::PreviewPanel(Sdk::ExtensionHost *host, QWidget *parent) : QWidget(parent), mHost(host)
{
    setObjectName("dataPreviewPanel");
    auto *layout = new QVBoxLayout(this);
    auto *form = new QFormLayout;
    mSource = new QComboBox(this);
    mSource->setObjectName("dataSource");
    mSource->addItems({tr("USGS monitoring sites"), tr("3DEP elevation footprints")});
    mAreaMode = new QComboBox(this);
    mAreaMode->setObjectName("dataAreaMode");
    mAreaMode->addItems({tr("Current viewport"), tr("Loaded boundary polygon")});
    mLayers = new QComboBox(this);
    mLayers->setObjectName("dataBoundaryLayer");
    mFeatures = new QComboBox(this);
    mFeatures->setObjectName("dataBoundaryFeature");
    mSelectedOnly = new QCheckBox(tr("Only selected boundary features"), this);
    mDataset = new QComboBox(this);
    mDataset->addItem(tr("1/3 arc-second DEM"), "National Elevation Dataset (NED) 1/3 arc-second");
    mDataset->addItem(tr("1 arc-second DEM"), "National Elevation Dataset (NED) 1 arc-second");
    mDataset->addItem(tr("1-meter DEM"), "Digital Elevation Model (DEM) 1 meter");
    form->addRow(tr("Source"), mSource);
    form->addRow(tr("Area"), mAreaMode);
    form->addRow(tr("Boundary layer"), mLayers);
    form->addRow(tr("Boundary feature"), mFeatures);
    form->addRow(mSelectedOnly);
    form->addRow(tr("Elevation product"), mDataset);
    layout->addLayout(form);
    auto *buttons = new QHBoxLayout;
    mPreview = new QPushButton(tr("Preview"), this);
    mPreview->setObjectName("dataPreview");
    mCancel = new QPushButton(tr("Cancel"), this);
    mCancel->setObjectName("dataCancel");
    mCache = new QPushButton(tr("Use cached preview"), this);
    mRecorded = new QPushButton(tr("Open saved catalogue…"), this);
    buttons->addWidget(mPreview);
    buttons->addWidget(mCancel);
    buttons->addWidget(mCache);
    layout->addLayout(buttons);
    layout->addWidget(mRecorded);
    mStatus = new QLabel(this);
    mStatus->setObjectName("dataStatus");
    mStatus->setTextFormat(Qt::PlainText);
    mStatus->setWordWrap(true);
    layout->addWidget(mStatus);
    mTable = new QTableView(this);
    mTable->setObjectName("dataCatalogue");
    mModel = new QStandardItemModel(this);
    mModel->setHorizontalHeaderLabels({tr("Name"), tr("ID"), tr("Type / date")});
    mTable->setModel(mModel);
    mTable->setSortingEnabled(true);
    mTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    mTable->setSelectionMode(QAbstractItemView::ExtendedSelection);
    mTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    mTable->horizontalHeader()->setStretchLastSection(true);
    mTable->setColumnWidth(0, 230);
    mTable->setColumnWidth(1, 160);
    layout->addWidget(mTable);
    mExport = new QPushButton(tr("Save selected features as a map layer"), this);
    mExport->setObjectName("dataExport");
    layout->addWidget(mExport);
    const auto changed = [this]() {
        invalidate(tr("Area or source changed. Preview again before saving features."));
        setBusy(false);
    };
    connect(mSource, &QComboBox::currentIndexChanged, this, changed);
    connect(mDataset, &QComboBox::currentIndexChanged, this, changed);
    connect(mAreaMode, &QComboBox::currentIndexChanged, this, changed);
    connect(mLayers, &QComboBox::currentIndexChanged, this, [this, changed]() {
        refreshFeatures();
        changed();
    });
    connect(mFeatures, &QComboBox::currentIndexChanged, this, changed);
    connect(mSelectedOnly, &QCheckBox::toggled, this, [this, changed]() {
        refreshFeatures();
        changed();
    });
    connect(mPreview, &QPushButton::clicked, this, [this]() {
        if (mStartingPreview || mClosing)
            return;
        const QPointer<PreviewPanel> self(this);
        mStartingPreview = true;
        const auto release = qScopeGuard([self] {
            if (self)
                self->mStartingPreview = false;
        });
        if (beginPreview())
            request(mQuery);
    });
    connect(mCancel, &QPushButton::clicked, this, [this]() {
        cancel();
        mStatus->setText(tr("Cancelled."));
    });
    connect(mCache, &QPushButton::clicked, this, &PreviewPanel::useCache);
    connect(mRecorded, &QPushButton::clicked, this, [this]() {
        const auto path =
            QFileDialog::getOpenFileName(this, tr("Open saved catalogue"), cacheDirectory(),
                                         tr("JSON catalogues (*.json *.geojson *.body)"));
        if (!path.isEmpty())
            loadCatalogue(path);
    });
    connect(mExport, &QPushButton::clicked, this, &PreviewPanel::exportSelection);
    connect(mTable->selectionModel(), &QItemSelectionModel::selectionChanged, this,
            [this]() { updateOverlay(); });
    mHostLifetime = host ? host->lifetime() : nullptr;
    mStateSource = qobject_cast<Sdk::IEventSourceV2 *>(mHostLifetime.data());
    mViewSource = qobject_cast<Sdk::IEventSourceV3 *>(mHostLifetime.data());
    mActivitySource = qobject_cast<Sdk::IEventSourceV4 *>(mHostLifetime.data());
    mViewControls = qobject_cast<Sdk::IViewControlsV1 *>(mHostLifetime.data());
    if (!mStateSource || !mViewSource || !mActivitySource || !mViewControls) {
        mStatus->setText(
            tr("This Data panel requires the SDK 1.5 lifecycle, view and activity services."));
        setBusy(false);
        return;
    }
    mStateSource->observeState(this, {}, [this](const Sdk::StateEvent &event) {
        if (event.sequence > mInitialWatermark)
            stateChanged(event);
    });
    mViewSource->observeViews(this, {}, [this](const Sdk::ViewEvent &event) {
        if (event.sequence > mInitialWatermark)
            viewChanged(event);
    });
    mActivitySource->observeActivity(this, {{Sdk::ActivityEventKind::ActiveDocumentChanged}},
                                     [this](const Sdk::ActivityEvent &event) {
                                         if (event.sequence > mInitialWatermark)
                                             bindProject();
                                     });
    mInitialWatermark = mStateSource->stateSnapshot().sequence;
    bindProject();
}

PreviewPanel::~PreviewPanel()
{
    mClosing = true;
    cancel();
    if (mHostLifetime && mProject)
        mProject->clearPreview();
}
void PreviewPanel::setSource(Source source)
{
    mSource->setCurrentIndex(source == Source::Sites ? 0 : 1);
}
void PreviewPanel::cancel()
{
    const QPointer<PreviewPanel> self(this);
    ++mGeneration;
    const auto reply = mReply;
    mReply = nullptr;
    if (reply) {
        disconnect(reply, nullptr, this, nullptr);
        reply->abort();
        if (reply)
            reply->deleteLater();
    }
    if (!self)
        return;
    auto *job = mJob;
    mJob = nullptr;
    if (job && mHostLifetime)
        job->cancel();
    if (self)
        setBusy(false);
}
void PreviewPanel::setBusy(bool busy)
{
    mBusy = busy;
    const bool open = mHostLifetime && mModelLifetime && mInstance && mInstance->isOpen();
    mPreview->setEnabled(open && !busy);
    mCache->setEnabled(open && !busy);
    mRecorded->setEnabled(open && !busy);
    mCancel->setEnabled(busy);
    mExport->setEnabled(open && !busy && mValid && !selectedItems().isEmpty());
    const bool polygon = mAreaMode->currentIndex() == 1;
    mLayers->setEnabled(open && polygon && !busy);
    mFeatures->setEnabled(open && polygon && !busy);
    mSelectedOnly->setEnabled(open && polygon && !busy);
    mDataset->setEnabled(!busy && mSource->currentIndex() == 1);
}
void PreviewPanel::invalidate(const QString &message)
{
    const QPointer<PreviewPanel> self(this);
    mValid = false;
    mOverlayRefreshPending = false;
    mReview = {};
    mItems.clear();
    mArea = {};
    mProvenance = {};
    mQuery = QUrl{};
    cancel();
    if (!self)
        return;
    mModel->removeRows(0, mModel->rowCount());
    if (!self)
        return;
    if (mHostLifetime && mProject)
        mProject->clearPreview();
    if (!self)
        return;
    mExport->setEnabled(false);
    if (!message.isEmpty())
        mStatus->setText(message);
}
void PreviewPanel::bindProject()
{
    if (!mHostLifetime || !mStateSource || !mViewSource || !mActivitySource)
        return;
    const QPointer<PreviewPanel> self(this);
    auto *instance = mHost->activeInstance();
    if (!self || !mHostLifetime)
        return;
    const auto activity = mActivitySource->activitySnapshot();
    if (!self || !mHostLifetime)
        return;
    bool nativeDocument = false;
    for (const auto &document : activity.ui.documents)
        if (document.documentId == activity.ui.activeDocumentId &&
            document.kind == Sdk::DocumentKind::Model) {
            nativeDocument = true;
            instance = mStateSource->resolveSession(document.sessionId);
            break;
        }
    if (!self || !mHostLifetime)
        return;
    if (!nativeDocument && !instance && mModelLifetime && mInstance && mInstance->isOpen())
        instance = mInstance;
    Sdk::ModelSessionState ready;
    const auto snapshot = mStateSource->stateSnapshot();
    if (!self || !mHostLifetime)
        return;
    for (const auto &state : snapshot.sessions) {
        if (state.status != Sdk::SessionStatus::Ready || !instance)
            continue;
        auto *candidate = mStateSource->resolveSession(state.sessionId);
        if (!self || !mHostLifetime)
            return;
        if (candidate == instance) {
            ready = state;
            break;
        }
    }
    if (!self || !mHostLifetime)
        return;
    if (ready.sessionId.isNull())
        instance = nullptr;
    if (instance == mInstance && ready.sessionId == mSessionId &&
        ready.loadGeneration == mLoadGeneration)
        return;
    invalidate();
    if (!self || !mHostLifetime)
        return;
    mInstance = instance;
    mSessionId = ready.sessionId;
    mLoadGeneration = ready.loadGeneration;
    mObservedRevision = ready.stateRevision;
    mModelLifetime = instance ? instance->lifetime() : nullptr;
    if (!self || !mHostLifetime)
        return;
    mProject = instance ? instance->project() : nullptr;
    if (!self || !mHostLifetime)
        return;
    refreshLayers();
    if (!self)
        return;
    setBusy(false);
    if (!self)
        return;
    mStatus->setText(instance ? tr("Choose an area and preview catalogue metadata.")
                              : tr("Open a ready model to preview data sources."));
}
void PreviewPanel::refreshLayers()
{
    const QPointer<PreviewPanel> self(this);
    const auto previous = mLayers->currentData(Qt::UserRole + 1);
    QSignalBlocker guard(mLayers);
    const auto dismiss = qScopeGuard([combo = QPointer<QComboBox>(mLayers), &guard] {
        if (!combo)
            guard.dismiss();
    });
    mLayers->clear();
    if (!self)
        return;
    if (mHostLifetime && mModelLifetime && mProject && mInstance->isOpen()) {
        const auto view = mViewSource->viewSnapshot(mSessionId);
        if (!self || !mHostLifetime || !mModelLifetime)
            return;
        const auto layers = mProject->polygonLayers();
        if (!self || !mHostLifetime || !mModelLifetime)
            return;
        for (const auto &layer : layers) {
            const auto id = layer.value("id").toString();
            QUuid attachment;
            int matches = 0;
            for (const auto &item : view.layers)
                if (item.layerId == id && item.vector) {
                    attachment = item.layerSessionId;
                    ++matches;
                }
            if (matches != 1 || attachment.isNull())
                continue;
            mLayers->addItem(layer.value("title").toString(), id);
            if (!self)
                return;
            mLayers->setItemData(mLayers->count() - 1, attachment, Qt::UserRole + 1);
            if (!self)
                return;
        }
    }
    const int index = mLayers->findData(previous, Qt::UserRole + 1);
    if (index >= 0)
        mLayers->setCurrentIndex(index);
    refreshFeatures();
}
void PreviewPanel::refreshFeatures()
{
    const QPointer<PreviewPanel> self(this);
    const auto previous = mFeatures->currentData();
    QSignalBlocker guard(mFeatures);
    const auto dismiss = qScopeGuard([combo = QPointer<QComboBox>(mFeatures), &guard] {
        if (!combo)
            guard.dismiss();
    });
    mFeatures->clear();
    if (!self)
        return;
    if (mHostLifetime && mModelLifetime && mProject && mInstance->isOpen()) {
        const auto features = mProject->boundaryFeatures(mLayers->currentData().toString(),
                                                         mSelectedOnly->isChecked(), 0, 5000);
        if (!self || !mHostLifetime || !mModelLifetime)
            return;
        for (const auto &feature : features) {
            mFeatures->addItem(feature.value("title").toString(), feature.value("id"));
            if (!self)
                return;
        }
    }
    const int index = mFeatures->findData(previous);
    if (index >= 0)
        mFeatures->setCurrentIndex(index);
}
bool PreviewPanel::captureContext(ReviewContext *result, QString *error) const
{
    const auto refuse = [error](const QString &message) {
        if (error)
            *error = message;
        return false;
    };
    if (!result || mClosing || !mHostLifetime || !mModelLifetime || !mInstance || !mProject ||
        !mStateSource || !mViewSource || !mActivitySource || !mViewControls)
        return refuse(tr("Open a ready model first."));
    const QPointer<const PreviewPanel> self(this);
    const auto hostLifetime = mHostLifetime;
    const auto modelLifetime = mModelLifetime;
    auto *host = mHost;
    auto *instance = mInstance;
    auto *project = mProject;
    auto *states = mStateSource;
    auto *views = mViewSource;
    auto *activity = mActivitySource;
    auto *controls = mViewControls;
    const auto sessionId = mSessionId;
    const auto generation = mLoadGeneration;
    const bool polygon = mAreaMode->currentIndex() == 1;
    const auto layerId = mLayers->currentData().toString();
    const auto attachment = mLayers->currentData(Qt::UserRole + 1).toUuid();
    const auto featureId = mFeatures->currentData().toLongLong();
    const bool selectedOnly = mSelectedOnly->isChecked();
    const bool chosen = mLayers->currentIndex() >= 0 && mFeatures->currentIndex() >= 0;
    const auto alive = [&] { return self && hostLifetime && modelLifetime; };
    const auto before = views->viewSnapshot(sessionId);
    if (!alive() || before.session.status != Sdk::SessionStatus::Ready ||
        before.session.sessionId != sessionId || before.session.loadGeneration != generation ||
        states->resolveSession(sessionId) != instance)
        return refuse(tr("The model generation is no longer ready."));
    if (!alive() || !instance->isOpen())
        return refuse(tr("The model is no longer available."));
    auto *active = host->activeInstance();
    if (!alive() || (active && active != instance))
        return refuse(tr("The active model changed. Preview again."));
    const auto ui = activity->activitySnapshot().ui;
    if (!alive())
        return false;
    for (const auto &document : ui.documents)
        if (document.documentId == ui.activeDocumentId &&
            document.kind == Sdk::DocumentKind::Model && document.sessionId != sessionId)
            return refuse(tr("The active model document changed. Preview again."));
    ReviewContext context;
    context.sessionId = sessionId;
    context.loadGeneration = generation;
    context.stateRevision = before.session.stateRevision;
    context.sequence = before.sequence;
    context.polygon = polygon;
    context.metadata = {{"inputPath", before.session.inputPath},
                        {"projectPath", before.session.projectPath}};
    if (!polygon)
        context.metadata["projectCrs"] = project->crsWkt();
    if (!alive())
        return false;
    if (polygon) {
        if (!chosen || attachment.isNull())
            return refuse(tr("Choose a loaded polygon layer and boundary feature."));
        Sdk::LayerViewState layer;
        int matches = 0;
        for (const auto &item : before.layers)
            if (item.layerId == layerId) {
                ++matches;
                if (item.layerSessionId == attachment)
                    layer = item;
            }
        if (matches != 1 || layer.layerSessionId.isNull() || !layer.vector ||
            layer.selectionCount < 0 || layer.selectionCount > std::numeric_limits<int>::max())
            return refuse(tr("The boundary attachment is unavailable or ambiguous."));
        context.attachmentId = attachment;
        context.featureId = featureId;
        context.metadata["layerId"] = layerId;
        context.metadata["title"] = layer.title;
        context.metadata["type"] = layer.type;
        context.metadata["visible"] = layer.visible;
        context.metadata["opacity"] = layer.opacity;
        context.metadata["crs"] = layer.crsWkt;
        context.metadata["filter"] = layer.filter;
        context.metadata["featureCount"] = QString::number(layer.featureCount);
        context.metadata["selectionCount"] = QString::number(layer.selectionCount);
        context.metadata["selectedOnly"] = selectedOnly;
        bool eligible = false;
        const auto choices = project->boundaryFeatures(layerId, selectedOnly, 0, 5000);
        if (!alive())
            return false;
        for (const auto &choice : choices)
            if (choice.value("id").toLongLong() == featureId) {
                eligible = true;
                context.metadata["featureTitle"] = choice.value("title").toString();
                context.metadata["featureSelected"] = choice.value("selected").toBool();
                break;
            }
        if (!eligible)
            return refuse(tr("The chosen feature is no longer an eligible boundary."));
        QCryptographicHash hash(QCryptographicHash::Sha256);
        int offset = 0;
        for (;;) {
            const auto page = controls->selectedLayerFeatures(sessionId, attachment, offset, 5000);
            if (!alive())
                return false;
            if (page.size() > 5000 || offset > std::numeric_limits<int>::max() - page.size())
                return refuse(tr("The boundary selection cannot be captured safely."));
            if (qint64(offset) + page.size() > layer.selectionCount)
                return refuse(tr("The boundary selection changed during capture."));
            for (const auto id : page)
                hash.addData(QByteArrayView(reinterpret_cast<const char *>(&id), sizeof(id)));
            offset += int(page.size());
            if (page.size() < 5000 || offset == layer.selectionCount)
                break;
        }
        if (offset != layer.selectionCount)
            return refuse(tr("The boundary selection changed during capture."));
        context.selectionHash = hash.result();
        context.area = project->boundaryPolygon(layerId, featureId, error);
    } else
        context.area = project->viewportBoundary(error);
    if (!alive())
        return false;
    if (context.area.isEmpty())
        return false;
    const auto after = views->viewSnapshot(sessionId);
    if (!alive() || after.sequence != before.sequence || after.session.sessionId != sessionId ||
        after.session.loadGeneration != generation ||
        after.session.status != Sdk::SessionStatus::Ready ||
        after.session.stateRevision != before.session.stateRevision)
        return refuse(tr("The model changed while capturing the review area. Preview again."));
    *result = std::move(context);
    return true;
}
bool PreviewPanel::sameContent(const ReviewContext &left, const ReviewContext &right)
{
    return left.sessionId == right.sessionId && left.loadGeneration == right.loadGeneration &&
           left.attachmentId == right.attachmentId && left.featureId == right.featureId &&
           left.polygon == right.polygon && left.area == right.area &&
           left.metadata == right.metadata && left.selectionHash == right.selectionHash;
}
bool PreviewPanel::validateReview()
{
    const QPointer<PreviewPanel> self(this);
    const auto generation = mGeneration;
    ReviewContext current;
    if (!mReview.sessionId.isNull() && captureContext(&current) && self &&
        generation == mGeneration && sameContent(mReview, current) &&
        mReview.stateRevision == current.stateRevision)
        return true;
    if (self && generation == mGeneration) {
        invalidate(tr(
            "The review source changed or has pending changes. Preview again before saving features."));
        if (self && mHostLifetime)
            bindProject();
    }
    return false;
}
void PreviewPanel::observeRevision(const Sdk::ModelSessionState &state)
{
    if (state.sessionId != mSessionId || state.loadGeneration != mLoadGeneration)
        return;
    mObservedRevision = qMax(mObservedRevision, state.stateRevision);
    if (mRebaseScheduled || mReview.sessionId.isNull() || mClosing)
        return;
    mRebaseScheduled = true;
    const auto generation = mGeneration;
    QTimer::singleShot(0, this, [this, generation] {
        mRebaseScheduled = false;
        if (mClosing || generation != mGeneration || mReview.sessionId.isNull())
            return;
        const QPointer<PreviewPanel> self(this);
        ReviewContext current;
        if (!captureContext(&current) || !self || generation != mGeneration)
            return;
        if (!sameContent(mReview, current)) {
            invalidate(tr("The review source changed. Preview again before saving features."));
            return;
        }
        if (current.stateRevision == mObservedRevision) {
            mReview.stateRevision = current.stateRevision;
            if (mOverlayRefreshPending) {
                mOverlayRefreshPending = false;
                updateOverlay();
            }
        }
    });
}
void PreviewPanel::stateChanged(const Sdk::StateEvent &event)
{
    const QPointer<PreviewPanel> self(this);
    using namespace Sdk;
    if (event.kind == StateEventKind::ModelReady || event.kind == StateEventKind::SessionOpened)
        bindProject();
    if (!self || event.session.sessionId != mSessionId)
        return;
    if (event.session.loadGeneration != mLoadGeneration ||
        event.kind == StateEventKind::LoadStarted || event.kind == StateEventKind::LoadFailed ||
        event.kind == StateEventKind::SessionClosing ||
        event.kind == StateEventKind::SessionClosed) {
        invalidate(tr("The model changed or closed. Choose a ready model and preview again."));
        if (self)
            bindProject();
        return;
    }
    if (event.sequence > mReview.sequence && event.kind == StateEventKind::StateChanged &&
        event.changes.testFlag(StateChange::Viewport)) {
        if (mAreaMode->currentIndex() == 0) {
            invalidate(tr("The viewport changed. Preview again before saving features."));
            return;
        }
        // The frozen polygon stays authoritative, but its transient graphics
        // need reprojection after a canvas CRS change. The public viewport
        // category also includes panning, so refresh once after safe rebasing.
        mOverlayRefreshPending = mOverlayRefreshPending || mValid;
    }
    // A broad V2 record can precede its same-revision V3 source invalidation.
    // Another observer may enter a nested loop before that detailed event is
    // delivered, so broad model/layer evidence cannot authorize a rebase.
    if (event.kind == StateEventKind::StateChanged &&
        (event.changes.testFlag(StateChange::Layers) ||
         event.changes.testFlag(StateChange::Model)))
        return;
    observeRevision(event.session);
}
void PreviewPanel::viewChanged(const Sdk::ViewEvent &event)
{
    const QPointer<PreviewPanel> self(this);
    using namespace Sdk;
    if (event.session.sessionId != mSessionId || event.session.loadGeneration != mLoadGeneration)
        return;
    const auto target = mReview.sessionId.isNull() ? mLayers->currentData(Qt::UserRole + 1).toUuid()
                                                   : mReview.attachmentId;
    const bool chosen = !target.isNull() && event.layerSessionId == target;
    bool relevant = false;
    switch (event.kind) {
    case ViewEventKind::LayerRemoved:
    case ViewEventKind::LayerMetadataChanged:
    case ViewEventKind::LayerSchemaChanged:
    case ViewEventKind::LayerFeaturesChanged:
    case ViewEventKind::LayerSelectionChanged:
    case ViewEventKind::LayerFilterChanged:
    case ViewEventKind::LayerCrsChanged:
    case ViewEventKind::LayerSourceChanged:
        relevant = chosen && mAreaMode->currentIndex() == 1;
        break;
    default:
        break;
    }
    if (relevant && event.sequence > mReview.sequence)
        invalidate(tr("The boundary source changed. Preview again before saving features."));
    if (!self)
        return;
    if (event.kind == ViewEventKind::LayerAdded || event.kind == ViewEventKind::LayerRemoved ||
        event.kind == ViewEventKind::LayerMetadataChanged || relevant)
        refreshLayers();
    if (self)
        observeRevision(event.session);
}
bool PreviewPanel::beginPreview()
{
    const QPointer<PreviewPanel> self(this);
    const auto beforeBinding = mGeneration;
    const auto previousSession = mSessionId;
    const auto previousLoad = mLoadGeneration;
    auto *previousInstance = mInstance;
    bindProject();
    if (!self)
        return false;
    const bool rebound = previousSession != mSessionId || previousLoad != mLoadGeneration ||
                         previousInstance != mInstance;
    if (mGeneration != beforeBinding + (rebound ? 1 : 0))
        return false; // A native query or binding callback explicitly cancelled this startup.
    const auto generation = mGeneration + 1;
    invalidate();
    if (!self || !mHostLifetime || generation != mGeneration)
        return false;
    QString error;
    ReviewContext context;
    if (!captureContext(&context, &error)) {
        if (self && generation == mGeneration)
            mStatus->setText(error);
        return false;
    }
    if (!self || !mHostLifetime || generation != mGeneration)
        return false;
    const auto source = mSource->currentIndex() == 0 ? Source::Sites : Source::Terrain;
    const auto dataset = mDataset->currentData().toString();
    const int limit = mHost->settings()->value("previewLimit", 500).toInt();
    if (!self || !mHostLifetime || generation != mGeneration)
        return false;
    mReview = context;
    mObservedRevision = context.stateRevision;
    mArea = context.area;
    mQuery = catalogueUrl(source, mArea, dataset, limit, &error);
    if (mQuery.isEmpty()) {
        mStatus->setText(error);
        return false;
    }
    setBusy(true);
    if (!self || generation != mGeneration)
        return false;
    mStatus->setText(tr("Previewing catalogue metadata…"));
    return self && generation == mGeneration;
}
void PreviewPanel::request(const QUrl &url, int attempt, int redirects)
{
    const auto generation = mGeneration;
    if (!validateReview() || generation != mGeneration)
        return;
    QNetworkRequest networkRequest(url);
    networkRequest.setTransferTimeout(30000);
    networkRequest.setRawHeader("Accept", "application/json");
    networkRequest.setRawHeader("User-Agent", "OpenSWMM-Data/0.1");
    const QPointer<PreviewPanel> self(this);
    const auto hostLifetime = mHostLifetime;
    auto *reply = mHost->network()->shared()->get(networkRequest);
    if (!self || !hostLifetime || generation != mGeneration) {
        if (hostLifetime && reply) {
            const QPointer<QNetworkReply> response(reply);
            reply->abort();
            if (response)
                response->deleteLater();
        }
        return;
    }
    mReply = reply;
    reply->setReadBufferSize(maxResponse + 1);
    auto exceeded = std::make_shared<bool>(false);
    connect(reply, &QNetworkReply::readyRead, this, [reply, exceeded]() {
        if (reply->bytesAvailable() > maxResponse) {
            *exceeded = true;
            reply->abort();
        }
    });
    connect(reply, &QNetworkReply::downloadProgress, this,
            [this, reply, generation, exceeded](qint64 received, qint64 total) {
                if (generation != mGeneration)
                    return;
                if (received > maxResponse || total > maxResponse) {
                    *exceeded = true;
                    reply->abort();
                }
            });
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, generation, url, attempt, redirects, exceeded]() {
                reply->deleteLater();
                if (generation != mGeneration || mClosing || !validateReview() ||
                    generation != mGeneration)
                    return;
                mReply = nullptr;
                if (*exceeded) {
                    setBusy(false);
                    mStatus->setText(
                        tr("Catalogue exceeds the preview size limit. Use a smaller area."));
                    return;
                }
                const int status =
                    reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                const auto redirected =
                    reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl();
                if (status >= 300 && status < 400 && !redirected.isEmpty() && redirects < 5) {
                    request(url.resolved(redirected), attempt, redirects + 1);
                    return;
                }
                if ((status == 429 || status == 503) && attempt < 3) {
                    bool ok = false;
                    int seconds = reply->rawHeader("Retry-After").toInt(&ok);
                    seconds = ok ? qBound(1, seconds, 60) : (1 << attempt);
                    mStatus->setText(tr("Service busy; retrying in %1 seconds…").arg(seconds));
                    QTimer::singleShot(seconds * 1000, this,
                                       [this, url, generation, attempt, redirects]() {
                                           if (generation == mGeneration)
                                               request(url, attempt + 1, redirects);
                                       });
                    return;
                }
                if (reply->error() != QNetworkReply::NoError) {
                    setBusy(false);
                    mStatus->setText(reply->errorString());
                    return;
                }
                const auto bytes = reply->readAll();
                if (bytes.size() > maxResponse) {
                    setBusy(false);
                    mStatus->setText(tr("Catalogue exceeds the preview size limit."));
                    return;
                }
                mProvenance = {{"kind", "network"},
                               {"url", url.toString()},
                               {"retrievedAt", QDateTime::currentDateTimeUtc().toString(Qt::ISODate)}};
                parse(bytes, true);
            });
}
QString PreviewPanel::cacheDirectory() const
{
    return mHostLifetime && mModelLifetime && mProject && mInstance &&
                   !mInstance->inpPath().isEmpty() && !mProject->directory().isEmpty()
               ? QDir(mProject->directory()).filePath("data/external/catalogues")
               : QString{};
}
QString PreviewPanel::cacheKey() const
{
    return QString::fromLatin1(
        QCryptographicHash::hash(mQuery.toEncoded() +
                                     QJsonDocument(mArea).toJson(QJsonDocument::Compact),
                                 QCryptographicHash::Sha256)
            .toHex());
}
void PreviewPanel::loadCatalogue(const QString &path)
{
    if (mStartingPreview || mClosing)
        return;
    const QPointer<PreviewPanel> self(this);
    mStartingPreview = true;
    const auto release = qScopeGuard([self] {
        if (self)
            self->mStartingPreview = false;
    });
    if (!beginPreview())
        return;
    const auto generation = mGeneration;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > maxResponse) {
        setBusy(false);
        mStatus->setText(tr("Cannot read a catalogue within the 20 MiB preview limit."));
        return;
    }
    mProvenance = {{"kind", "saved"}, {"path", QFileInfo(file).absoluteFilePath()}};
    QFile sidecar(path + ".request.json");
    if (sidecar.open(QIODevice::ReadOnly) && sidecar.size() <= 1024 * 1024)
        mProvenance["recordedRequest"] = QJsonDocument::fromJson(sidecar.readAll()).object();
    if (self && generation == mGeneration)
        parse(file.readAll(), false);
}
void PreviewPanel::useCache()
{
    if (mStartingPreview || mClosing)
        return;
    const QPointer<PreviewPanel> self(this);
    mStartingPreview = true;
    const auto release = qScopeGuard([self] {
        if (self)
            self->mStartingPreview = false;
    });
    if (!beginPreview())
        return;
    const auto generation = mGeneration;
    const auto directory = cacheDirectory();
    if (!self || generation != mGeneration)
        return;
    if (directory.isEmpty()) {
        setBusy(false);
        mStatus->setText(
            tr("Save the model in a project folder before using its catalogue cache."));
        return;
    }
    QFile index(QDir(directory).filePath("index.json"));
    if (!index.open(QIODevice::ReadOnly)) {
        setBusy(false);
        mStatus->setText(tr("No cached preview matches this area and source."));
        return;
    }
    const auto filename =
        QJsonDocument::fromJson(index.readAll()).object().value(cacheKey()).toString();
    QFile file(QDir(directory).filePath(filename));
    if (filename.isEmpty() || QFileInfo(filename).fileName() != filename ||
        !file.open(QIODevice::ReadOnly) || file.size() > maxResponse) {
        setBusy(false);
        mStatus->setText(tr("No cached preview matches this area and source."));
        return;
    }
    mProvenance = {{"kind", "cache"}, {"path", QFileInfo(file).absoluteFilePath()}};
    QFile sidecar(file.fileName() + ".request.json");
    if (sidecar.open(QIODevice::ReadOnly) && sidecar.size() <= 1024 * 1024)
        mProvenance["recordedRequest"] = QJsonDocument::fromJson(sidecar.readAll()).object();
    if (self && generation == mGeneration)
        parse(file.readAll(), false);
}
void PreviewPanel::parse(const QByteArray &bytes, bool cache)
{
    const auto generation = mGeneration;
    if (!validateReview() || generation != mGeneration)
        return;
    struct Result {
        QList<Item> items;
        QString error;
        bool more = false;
    };
    auto result = std::make_shared<Result>();
    const auto area = mArea;
    const auto source = mSource->currentIndex() == 0 ? Source::Sites : Source::Terrain;
    const QPointer<PreviewPanel> self(this);
    const auto hostLifetime = mHostLifetime;
    auto *job = mHost->runJob(
        tr("Filter catalogue preview"),
        [bytes, area, source, result](Sdk::IJobContext &job) {
            QJsonParseError error;
            const auto document = QJsonDocument::fromJson(bytes, &error);
            if (error.error != QJsonParseError::NoError || !document.isObject()) {
                result->error = error.error == QJsonParseError::NoError
                                    ? QStringLiteral("A JSON catalogue object is required.")
                                    : error.errorString();
                return;
            }
            result->items = parseCatalogue(
                source, document.object(), area, [&job]() { return job.isCancelled(); },
                &result->error);
            for (const auto &link : document.object()["links"].toArray())
                if (link.toObject()["rel"] == "next")
                    result->more = true;
            if (source == Source::Terrain)
                result->more = document.object()["total"].toInt() >
                               document.object()["items"].toArray().size();
            job.setProgress(1.);
        },
        [this, guard = QPointer<PreviewPanel>(this), generation, result, cache,
         bytes](bool ok, const QString &error) {
            if (!guard || generation != mGeneration || mClosing || !validateReview() ||
                generation != mGeneration)
                return;
            mJob = nullptr;
            setBusy(false);
            if (!guard || generation != mGeneration || !validateReview() ||
                generation != mGeneration)
                return;
            if (!ok || !result->error.isEmpty()) {
                mStatus->setText(!result->error.isEmpty() ? result->error
                                 : !error.isEmpty()       ? error
                                                          : tr("Cancelled."));
                return;
            }
            if (cache && !cacheDirectory().isEmpty()) {
                const auto directory = cacheDirectory();
                QDir().mkpath(directory);
                const auto name = QUuid::createUuid().toString(QUuid::WithoutBraces) + ".json";
                const QJsonObject metadata{{"url", mQuery.toString()},
                                           {"area", mArea},
                                           {"response", mProvenance},
                                           {"retrievedAt", mProvenance.value("retrievedAt")}};
                if (save(QDir(directory).filePath(name), bytes) &&
                    save(QDir(directory).filePath(name + ".request.json"),
                         QJsonDocument(metadata).toJson())) {
                    QFile old(QDir(directory).filePath("index.json"));
                    QJsonObject index;
                    if (old.open(QIODevice::ReadOnly))
                        index = QJsonDocument::fromJson(old.readAll()).object();
                    old.close();
                    index[cacheKey()] = name;
                    save(QDir(directory).filePath("index.json"), QJsonDocument(index).toJson());
                    mProvenance["path"] = QDir(directory).filePath(name);
                } else
                    mHost->console()->append(
                        "warning", tr("Could not save the catalogue cache in %1").arg(directory));
            }
            if (guard && mHostLifetime && generation == mGeneration)
                apply(result->items, result->more);
        });
    if (!self || !hostLifetime || generation != mGeneration) {
        if (hostLifetime && job)
            job->cancel();
        return;
    }
    mJob = job;
    if (!mJob) {
        setBusy(false);
        mStatus->setText(tr("The preview could not be started."));
    }
}
void PreviewPanel::apply(const QList<Item> &items, bool more)
{
    const QPointer<PreviewPanel> self(this);
    const auto generation = mGeneration;
    if (!validateReview() || !self || generation != mGeneration)
        return;
    mItems = items;
    mModel->removeRows(0, mModel->rowCount());
    if (!self || generation != mGeneration)
        return;
    for (int i = 0; i < items.size(); ++i) {
        const auto &item = items[i];
        auto *name = new QStandardItem(item.title);
        name->setData(i, Qt::UserRole);
        const auto details = item.metadata.value("site_type")
                                 .toString(item.metadata.value("publicationDate").toString());
        mModel->appendRow({name, new QStandardItem(item.id), new QStandardItem(details)});
        if (!self || generation != mGeneration)
            return;
    }
    if (!validateReview() || !self || generation != mGeneration)
        return;
    mValid = true;
    mStatus->setText(
        tr("%1 results intersect the reviewed area. Select rows to preview or save them.%2")
            .arg(items.size())
            .arg(
                more
                    ? tr(" More catalogue results are available; narrow the area or raise the preview limit.")
                    : QString{}));
    if (self && generation == mGeneration)
        updateOverlay();
}
QList<Item> PreviewPanel::selectedItems() const
{
    QList<Item> result;
    for (const auto &row : mTable->selectionModel()->selectedRows()) {
        const int i = row.data(Qt::UserRole).toInt();
        if (i >= 0 && i < mItems.size())
            result.append(mItems[i]);
    }
    return result;
}
void PreviewPanel::updateOverlay()
{
    if (!mValid || !mHostLifetime || !mModelLifetime || !mProject || !mInstance ||
        !mInstance->isOpen()) {
        mExport->setEnabled(false);
        return;
    }
    if (!validateReview())
        return;
    const QPointer<PreviewPanel> self(this);
    const auto selected = selectedItems();
    QString error;
    if (!mProject->showPreview(featureCollection(selected.isEmpty() ? mItems : selected), &error))
        if (self)
            mStatus->setText(error);
    if (self && validateReview())
        mExport->setEnabled(!mBusy && !selected.isEmpty());
}
void PreviewPanel::exportSelection()
{
    const auto selected = selectedItems();
    if (!mValid || selected.isEmpty() || !mHostLifetime || !mModelLifetime || !mProject ||
        !mInstance->isOpen() || !validateReview())
        return;
    if (cacheDirectory().isEmpty()) {
        mStatus->setText(tr("Save the model in a project folder before saving features."));
        return;
    }
    const auto directory = QDir(mProject->directory()).filePath("data/external/preview-layers");
    QDir().mkpath(directory);
    const auto path =
        QDir(directory).filePath(QUuid::createUuid().toString(QUuid::WithoutBraces) + ".geojson");
    auto collection = featureCollection(selected);
    collection["review"] =
        QJsonObject{{"area", mArea},
                    {"intendedQuery", mQuery.toString()},
                    {"response", mProvenance},
                    {"savedAt", QDateTime::currentDateTimeUtc().toString(Qt::ISODate)}};
    if (!save(path, QJsonDocument(collection).toJson())) {
        mStatus->setText(tr("Could not save the selected features."));
        return;
    }
    QString error;
    const QPointer<PreviewPanel> self(this);
    const auto id = mProject->addFeatureLayer(
        path, mSource->currentIndex() == 0 ? tr("USGS sites") : tr("Elevation footprints"), &error);
    if (!self)
        return;
    mStatus->setText(id.isEmpty() ? error
                                  : tr("Saved selected features as a map layer: %1").arg(path));
}
}
