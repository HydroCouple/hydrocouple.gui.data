/**
 * @file PreviewPanel.cpp
 * @brief Compact area/preview/download workflow with optional catalogue inspection.
 * @details Generation counters discard late replies/jobs after area changes. Metadata requests
 * are separate from explicit observation/raster transfers. Cancellation aborts network IO and
 * cooperatively stops parsing. Output data and provenance use user-chosen reviewable folders.
 * @author Caleb Buahin <caleb.buahin@gmail.com>
 * @date 2026
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license GPL-3.0-or-later
 */
#include "PreviewPanel.hpp"
#include "CatalogueFilter.hpp"
#include "PayloadDownload.hpp"
#include <QCheckBox>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDateEdit>
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
#include <QLineEdit>
#include <QNetworkReply>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QPushButton>
#include <QPersistentModelIndex>
#include <QSaveFile>
#include <QSignalBlocker>
#include <QScopeGuard>
#include <QStandardItemModel>
#include <QSet>
#include <QTableView>
#include <QToolButton>
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

PreviewPanel::PreviewPanel(Sdk::ExtensionHost *host, QWidget *parent)
    : QWidget(parent), mPreviewProgress(host, this), mImportProgress(host, this), mHost(host)
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
    layout->addLayout(form);
    mBoundaryControls = new QWidget(this);
    mBoundaryControls->setObjectName("dataBoundaryControls");
    auto *boundaryForm = new QFormLayout(mBoundaryControls);
    boundaryForm->setContentsMargins(0, 0, 0, 0);
    boundaryForm->addRow(tr("Layer"), mLayers);
    boundaryForm->addRow(tr("Polygon"), mFeatures);
    boundaryForm->addRow(mSelectedOnly);
    layout->addWidget(mBoundaryControls);
    mTerrainControls = new QWidget(this);
    mTerrainControls->setObjectName("dataTerrainControls");
    auto *terrainForm = new QFormLayout(mTerrainControls);
    terrainForm->setContentsMargins(0, 0, 0, 0);
    terrainForm->addRow(tr("Resolution"), mDataset);
    layout->addWidget(mTerrainControls);
    auto *buttons = new QHBoxLayout;
    mPreview = new QPushButton(tr("Preview"), this);
    mPreview->setObjectName("dataPreview");
    mClear = new QPushButton(tr("Clear preview"), this);
    mClear->setObjectName("dataClearPreview");
    mClear->setToolTip(tr("Remove preview graphics and rows. Downloaded data and layers are retained."));
    mCancel = new QPushButton(tr("Cancel"), this);
    mCancel->setObjectName("dataCancel");
    mCache = new QPushButton(tr("Use cached preview"), this);
    mRecorded = new QPushButton(tr("Open saved catalogue…"), this);
    buttons->addWidget(mPreview);
    buttons->addWidget(mClear);
    buttons->addWidget(mCancel);
    layout->addLayout(buttons);
    mStatus = new QLabel(this);
    mStatus->setObjectName("dataStatus");
    mStatus->setTextFormat(Qt::PlainText);
    mStatus->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    mStatus->setWordWrap(true);
    layout->addWidget(mStatus);
    auto *filterRow = new QHBoxLayout;
    mFilterField = new QComboBox(this);
    mFilterField->setObjectName("dataFilterField");
    mFilterField->addItem(tr("All attributes"), QString{});
    mFilterOperator = new QComboBox(this);
    mFilterOperator->setObjectName("dataFilterOperator");
    mFilterOperator->addItems({tr("Contains"), tr("Equals"), tr("At least"), tr("At most"),
                               tr("Has value"), tr("Is missing")});
    filterRow->addWidget(mFilterField, 1);
    filterRow->addWidget(mFilterOperator);
    auto *detailsToggle = new QToolButton(this);
    detailsToggle->setObjectName("dataDetailsToggle");
    detailsToggle->setText(tr("Details and advanced options"));
    detailsToggle->setCheckable(true);
    detailsToggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    detailsToggle->setArrowType(Qt::RightArrow);
    auto *details = new QWidget(this);
    details->setObjectName("dataDetails");
    auto *detailsLayout = new QVBoxLayout(details);
    detailsLayout->setContentsMargins(0, 0, 0, 0);
    detailsLayout->addLayout(filterRow);
    auto *searchRow = new QHBoxLayout;
    mFilterText = new QLineEdit(this);
    mFilterText->setObjectName("dataFilterText");
    mFilterText->setPlaceholderText(tr("Find sites or tiles…"));
    auto *clearFilter = new QPushButton(tr("Clear filter"), this);
    clearFilter->setObjectName("dataClearFilter");
    searchRow->addWidget(mFilterText, 1);
    detailsLayout->addWidget(clearFilter);
    layout->addLayout(searchRow);
    mCounts = new QLabel(this);
    mCounts->setObjectName("dataResultCount");
    layout->addWidget(mCounts);
    mTable = new QTableView(this);
    mTable->setObjectName("dataCatalogue");
    mModel = new QStandardItemModel(this);
    mModel->setHorizontalHeaderLabels({tr("Name"), tr("ID"), tr("Type / date")});
    mFilter = new CatalogueFilter(this);
    mFilter->setSourceModel(mModel);
    mTable->setModel(mFilter);
    mTable->setSortingEnabled(true);
    mTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    mTable->setSelectionMode(QAbstractItemView::ExtendedSelection);
    mTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    mTable->horizontalHeader()->setStretchLastSection(true);
    mTable->setColumnWidth(0, 230);
    mTable->setColumnWidth(1, 160);
    mTable->setMinimumHeight(120);
    layout->addWidget(mTable, 1);
    mPeriodControls = new QWidget(this);
    mPeriodControls->setObjectName("dataPeriodControls");
    auto *periodForm = new QFormLayout(mPeriodControls);
    periodForm->setContentsMargins(0, 0, 0, 0);
    mParameter = new QComboBox(mPeriodControls);
    mParameter->setObjectName("dataParameter");
    mParameter->addItem(tr("Streamflow (00060)"), "00060");
    mParameter->addItem(tr("Gage height (00065)"), "00065");
    mParameter->addItem(tr("Water temperature (00010)"), "00010");
    mParameter->setEditable(true);
    mParameter->setToolTip(tr("Choose a measurement or enter its five-digit USGS parameter code."));
    mInterval = new QComboBox(mPeriodControls);
    mInterval->setObjectName("dataInterval");
    mInterval->addItems({tr("Daily values"), tr("Continuous measurements")});
    mStartDate = new QDateEdit(QDate::currentDate().addDays(-30), mPeriodControls);
    mStartDate->setObjectName("dataStartDate");
    mEndDate = new QDateEdit(QDate::currentDate(), mPeriodControls);
    mEndDate->setObjectName("dataEndDate");
    for (auto *date : {mStartDate, mEndDate}) {
        date->setCalendarPopup(true);
        date->setDisplayFormat("yyyy-MM-dd");
    }
    auto *dates = new QHBoxLayout;
    dates->addWidget(mStartDate);
    dates->addWidget(new QLabel(tr("to"), mPeriodControls));
    dates->addWidget(mEndDate);
    periodForm->addRow(tr("Measurement"), mParameter);
    periodForm->addRow(tr("Values"), mInterval);
    periodForm->addRow(tr("Period (inclusive)"), dates);
    layout->addWidget(mPeriodControls);
    mDownload = new QPushButton(tr("Download selected observations…"), this);
    mDownload->setObjectName("dataDownload");
    mDownload->setToolTip(tr("Choose a folder and download actual data for the selected rows."));
    layout->addWidget(mDownload);
    layout->addWidget(detailsToggle);
    layout->addWidget(details);
    details->hide();
    connect(detailsToggle, &QToolButton::toggled, this, [details, detailsToggle](bool open) {
        details->setVisible(open);
        detailsToggle->setArrowType(open ? Qt::DownArrow : Qt::RightArrow);
    });
    mAttributeTitle = new QLabel(tr("Select a location to inspect its saved attributes."), this);
    mAttributeTitle->setTextFormat(Qt::PlainText);
    mAttributeTitle->setWordWrap(true);
    detailsLayout->addWidget(mAttributeTitle);
    mAttributes = new QTableView(this);
    mAttributes->setObjectName("dataAttributes");
    mAttributeModel = new QStandardItemModel(this);
    mAttributeModel->setHorizontalHeaderLabels({tr("Attribute"), tr("Saved value")});
    mAttributes->setModel(mAttributeModel);
    mAttributes->setEditTriggers(QAbstractItemView::NoEditTriggers);
    mAttributes->horizontalHeader()->setStretchLastSection(true);
    mAttributes->setColumnWidth(0, 190);
    mAttributes->setMinimumHeight(80);
    mAttributes->setMaximumHeight(180);
    detailsLayout->addWidget(mAttributes);
    mExport = new QPushButton(tr("Save selected rows as a map layer"), this);
    mExport->setObjectName("dataExport");
    mExport->setToolTip(tr("Save the selected, visible Data catalogue rows. "
                           "The map layer's own selection is independent."));
    detailsLayout->addWidget(mExport);
    auto *cacheRow = new QHBoxLayout;
    cacheRow->addWidget(mCache);
    cacheRow->addWidget(mRecorded);
    detailsLayout->addLayout(cacheRow);
    const auto changed = [this]() {
        invalidate(tr("Area or source changed. Preview again before downloading."));
        updateControls();
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
    connect(mClear, &QPushButton::clicked, this, &PreviewPanel::clearPreview);
    connect(mCancel, &QPushButton::clicked, this, [this]() {
        const QPointer<PreviewPanel> self(this);
        cancel();
        if (self)
            mStatus->setText(tr("Cancelled."));
    });
    connect(mCache, &QPushButton::clicked, this, &PreviewPanel::useCache);
    connect(mRecorded, &QPushButton::clicked, this, [this]() {
        const QPointer<PreviewPanel> self(this);
        const auto path =
            QFileDialog::getOpenFileName(this, tr("Open saved catalogue"), cacheDirectory(),
                                         tr("JSON catalogues (*.json *.geojson *.body)"));
        if (self && !path.isEmpty())
            loadCatalogue(path);
    });
    connect(mExport, &QPushButton::clicked, this, &PreviewPanel::exportSelection);
    connect(mDownload, &QPushButton::clicked, this, &PreviewPanel::downloadSelection);
    for (auto *date : {mStartDate, mEndDate})
        connect(date, &QDateEdit::dateChanged, this, [this]() { setBusy(mBusy); });
    connect(mParameter, &QComboBox::currentTextChanged, this, [this]() { setBusy(mBusy); });
    connect(mInterval, &QComboBox::currentIndexChanged, this, [this]() { setBusy(mBusy); });
    connect(mTable->selectionModel(), &QItemSelectionModel::selectionChanged, this, [this]() {
        if (!mFiltering) {
            updateCounts();
            updateOverlay();
        }
    });
    connect(mTable->selectionModel(), &QItemSelectionModel::currentChanged, this, [this]() {
        if (!mFiltering)
            updateAttributes();
    });
    connect(mFilterField, &QComboBox::currentIndexChanged, this, &PreviewPanel::filterResults);
    connect(mFilterOperator, &QComboBox::currentIndexChanged, this, &PreviewPanel::filterResults);
    connect(mFilterText, &QLineEdit::textChanged, this, &PreviewPanel::filterResults);
    connect(clearFilter, &QPushButton::clicked, this, [this]() {
        const QSignalBlocker field(mFilterField), operation(mFilterOperator), text(mFilterText);
        mFilterField->setCurrentIndex(0);
        mFilterOperator->setCurrentIndex(0);
        mFilterText->clear();
        filterResults();
    });
    updateCounts();
    mHostLifetime = host ? host->lifetime() : nullptr;
    if (mHostLifetime)
        connect(mHostLifetime, &QObject::destroyed, this, [this] {
            const QPointer<PreviewPanel> self(this);
            cancel();
            if (self)
                mStatus->setText(tr("The Data source host closed."));
        });
    mPayload = new PayloadDownload(host, this);
    connect(mPayload, &PayloadDownload::progress, this,
            [this](double, const QString &message) { mStatus->setText(message); });
    connect(mPayload, &PayloadDownload::finished, this, [this](const PayloadResult &result) {
        setBusy(false);
        if (mClosing)
            return;
        QString message;
        if (result.cancelled)
            message = tr("Download cancelled.");
        else if (!result.error.isEmpty())
            message = result.error;
        else if (mSource->currentIndex() == 0 && result.observationCount == 0)
            message = tr("No observations were available for this measurement and period. "
                         "Request details were saved in %1")
                          .arg(QFileInfo(result.manifestPath).absolutePath());
        else if (mSource->currentIndex() == 0)
            message = tr("Saved %1 observations in %2")
                          .arg(result.observationCount)
                          .arg(QFileInfo(result.manifestPath).absolutePath());
        else
            message = tr("Download complete. Saved files in %1")
                          .arg(QFileInfo(result.manifestPath).absolutePath());
        if (!result.cancelled && result.error.isEmpty() && !result.descriptorPath.isEmpty() &&
            importObservations(result))
            return;
        const QPointer<PreviewPanel> self(this);
        if (!result.cancelled && result.error.isEmpty() && !result.rasterPaths.isEmpty() &&
            mValid && mHostLifetime && mModelLifetime && mProject && displayIsCurrent() &&
            validateReview()) {
            auto *project = mProject;
            const auto session = mSessionId;
            const auto load = mLoadGeneration;
            OperationProgress import(mHost, this);
            import.begin(tr("Add downloaded DEM layer"));
            if (!self || !mHostLifetime) return;
            int added = 0;
            QStringList failures;
            for (const auto &path : result.rasterPaths) {
                if (!mHostLifetime || !mModelLifetime || mProject != project ||
                    mSessionId != session || mLoadGeneration != load || !displayIsCurrent())
                    break;
                import.update(double(added) / result.rasterPaths.size(), tr("Adding saved raster to the map"));
                if (!self || !mHostLifetime || !mModelLifetime || mProject != project ||
                    mSessionId != session || mLoadGeneration != load || !displayIsCurrent())
                    return;
                QString error;
                const auto id =
                    project->addRasterLayer(path, QFileInfo(path).completeBaseName(), &error);
                if (!self)
                    return;
                if (id.isEmpty()) {
                    message += tr("\nSaved raster could not be added to the map: %1").arg(error);
                    failures.append(error);
                } else {
                    ++added;
                }
                // The accepted batch contains immutable validated files. Each import still
                // targets the same Ready model; no new review revision is granted here.
            }
            import.finish(added == result.rasterPaths.size() ? Sdk::ProgressOutcome::Succeeded
                           : failures.isEmpty() ? Sdk::ProgressOutcome::Cancelled
                                                : Sdk::ProgressOutcome::Failed,
                          tr("Added %1 DEM layers. Downloaded files retained. %2")
                              .arg(added).arg(failures.join("; ")));
        }
        if (!self)
            return;
        if (!result.manifestPath.isEmpty() && !result.error.isEmpty())
            message += tr("\nCompleted files and transfer details: %1").arg(result.manifestPath);
        mStatus->setText(message);
    });
    updateControls();
    mStateSource = qobject_cast<Sdk::IEventSourceV2 *>(mHostLifetime.data());
    mViewSource = qobject_cast<Sdk::IEventSourceV3 *>(mHostLifetime.data());
    mActivitySource = qobject_cast<Sdk::IEventSourceV4 *>(mHostLifetime.data());
    mViewControls = qobject_cast<Sdk::IViewControlsV1 *>(mHostLifetime.data());
    mTimeSeriesLayers = qobject_cast<Sdk::ITimeSeriesLayersV1 *>(mHostLifetime.data());
    if (!mStateSource || !mViewSource || !mActivitySource || !mViewControls || !mTimeSeriesLayers ||
        !qobject_cast<Sdk::IProgressV1 *>(mHostLifetime.data())) {
        mStatus->setText(tr(
            "This Data panel requires the SDK 1.7 lifecycle, view, activity, time-series layer and progress services."));
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
void PreviewPanel::clearPreview()
{
    if (mClosing || mImportPending || (mPayload && mPayload->isRunning()))
        return;
    const QPointer<PreviewPanel> self(this);
    const QString source = mSource->currentIndex() == 0 ? tr("USGS site") : tr("DEM footprint");
    OperationProgress progress(mHost, this);
    progress.begin(tr("Clear %1 preview").arg(source));
    if (!self || !mHostLifetime) return;
    progress.update(-1, tr("Removing temporary preview graphics and rows"));
    if (!self || !mHostLifetime) return;
    invalidate(tr("Preview cleared. Downloaded files and added layers are retained."));
    if (!self)
        return;
    setBusy(false);
    progress.finish(Sdk::ProgressOutcome::Succeeded,
                    tr("%1 preview cleared; downloaded files and added layers retained.").arg(source));
}
void PreviewPanel::cancel()
{
    const QPointer<PreviewPanel> self(this);
    ++mGeneration;
    mPreviewProgress.finish(Sdk::ProgressOutcome::Cancelled, tr("Preview cancelled."));
    if (!self) return;
    mImportProgress.finish(Sdk::ProgressOutcome::Cancelled,
                           tr("Layer import cancelled; downloaded files are retained."));
    if (!self) return;
    const bool cancellingImport = mImportPending;
    auto *importJob = mImportJob;
    mImportJob = nullptr;
    mImportPending = false;
    if (importJob && mHostLifetime)
        importJob->cancel();
    if (!self)
        return;
    if (mPayload)
        mPayload->cancel();
    if (!self)
        return;
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
    if (self) {
        setBusy(false);
        if (cancellingImport && !mClosing)
            mStatus->setText(
                tr("Layer import cancelled. Downloaded observation files were retained."));
    }
}
void PreviewPanel::setBusy(bool busy)
{
    busy = busy || mImportPending || (mPayload && mPayload->isRunning());
    mBusy = busy;
    const bool open = mHostLifetime && mModelLifetime && mInstance && mInstance->isOpen();
    mPreview->setEnabled(open && !busy);
    mCache->setEnabled(open && !busy);
    mRecorded->setEnabled(open && !busy);
    mClear->setEnabled(open && (mHasDisplay || mBusy) && !mImportPending &&
                       !(mPayload && mPayload->isRunning()));
    mCancel->setEnabled(busy);
    mCancel->setVisible(busy);
    mExport->setEnabled(open && !busy && mValid && !mAwaitingDisplayRevision &&
                        !selectedItems().isEmpty());
    const bool polygon = mAreaMode->currentIndex() == 1;
    mLayers->setEnabled(open && polygon && !busy);
    mFeatures->setEnabled(open && polygon && !busy);
    mSelectedOnly->setEnabled(open && polygon && !busy);
    mDataset->setEnabled(!busy && mSource->currentIndex() == 1);
    mSource->setEnabled(!busy);
    mAreaMode->setEnabled(!busy);
    mPeriodControls->setEnabled(!busy);
    const QString code =
        mParameter->currentText() == mParameter->itemText(mParameter->currentIndex())
            ? mParameter->currentData().toString()
            : mParameter->currentText().trimmed();
    bool validCode = code.size() == 5;
    for (const auto character : code)
        validCode = validCode && character >= QLatin1Char('0') && character <= QLatin1Char('9');
    const bool periodValid = mStartDate->date().isValid() && mEndDate->date().isValid() &&
                             mStartDate->date() <= mEndDate->date();
    mDownload->setEnabled(open && !busy && mValid && !mAwaitingDisplayRevision &&
                          !selectedItems().isEmpty() &&
                          (mSource->currentIndex() == 1 || (periodValid && validCode)));
}
void PreviewPanel::updateControls()
{
    const bool sites = mSource->currentIndex() == 0;
    mBoundaryControls->setVisible(mAreaMode->currentIndex() == 1);
    mTerrainControls->setVisible(!sites);
    mPeriodControls->setVisible(sites);
    mDownload->setText(sites ? tr("Download selected observations…")
                             : tr("Download selected raster tiles…"));
    mExport->setText(sites ? tr("Save selected sites as a map layer")
                           : tr("Save selected footprints as a map layer"));
}
void PreviewPanel::invalidate(const QString &message, bool retainDisplay)
{
    const QPointer<PreviewPanel> self(this);
    mValid = false;
    mOverlayRefreshPending = false;
    mAwaitingDisplayRevision = false;
    mReview = {};
    retainDisplay = retainDisplay && mHasDisplay;
    if (!retainDisplay) {
        mHasDisplay = false;
        mItems.clear();
    }
    mArea = {};
    mProvenance = {};
    mQuery = QUrl{};
    cancel();
    if (!self)
        return;
    if (!retainDisplay)
        mModel->removeRows(0, mModel->rowCount());
    if (!self)
        return;
    if (!retainDisplay && mHostLifetime && mModelLifetime && mProject)
        mProject->clearPreview();
    if (!self)
        return;
    updateAttributes();
    if (!self)
        return;
    updateCounts();
    if (retainDisplay)
        updateOverlay();
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
    mStatus->setText(
        instance
            ? tr("Choose an area, preview available sites or tiles, then select rows to download.")
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
        !mStateSource || !mViewSource || !mActivitySource || !mViewControls || !mTimeSeriesLayers)
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
    const bool captured = !mReview.sessionId.isNull() && captureContext(&current);
    if (captured && self && generation == mGeneration && sameContent(mReview, current) &&
        mReview.stateRevision == current.stateRevision)
        return true;
    if (self && generation == mGeneration) {
        const bool retain = captured && !mReview.polygon &&
                            current.sessionId == mReview.sessionId &&
                            current.loadGeneration == mReview.loadGeneration;
        invalidate(tr("The review source changed or has pending changes. "
                      "Preview again before saving features."),
                   retain);
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
            invalidate(tr("The review source changed. Preview again before saving features."),
                       !mReview.polygon);
            return;
        }
        if (current.stateRevision == mObservedRevision) {
            mReview.stateRevision = current.stateRevision;
            mAwaitingDisplayRevision = false;
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
            invalidate(tr("The viewport changed. Showing the previous preview; "
                          "preview again before saving features."),
                       true);
            return;
        }
        // The frozen polygon stays authoritative, but its transient graphics
        // need reprojection after a canvas CRS change. The public viewport
        // category also includes panning, so refresh once after safe rebasing.
        mOverlayRefreshPending = mOverlayRefreshPending || mValid;
        if (!mValid && mHasDisplay)
            updateOverlay();
        if (!self)
            return;
    }
    // A broad V2 record can precede its same-revision V3 source invalidation.
    // Another observer may enter a nested loop before that detailed event is
    // delivered, so broad model/layer evidence cannot authorize a rebase.
    if (event.kind == StateEventKind::StateChanged &&
        (event.changes.testFlag(StateChange::Layers) || event.changes.testFlag(StateChange::Model)))
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
        if (self && generation == mGeneration) {
            mStatus->setText(error);
            mPreviewProgress.log("warning", tr("Preview refused: %1").arg(error));
        }
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
        mPreviewProgress.log("warning", tr("Preview refused: %1").arg(error));
        return false;
    }
    setBusy(true);
    if (!self || generation != mGeneration)
        return false;
    mPreviewProgress.begin(source == Source::Sites ? tr("USGS site preview") : tr("DEM footprint preview"));
    if (!self || !mHostLifetime || generation != mGeneration) return false;
    mPreviewProgress.update(-1, tr("Finding available sites or tiles"));
    if (!self || !mHostLifetime || generation != mGeneration) return false;
    mStatus->setText(tr("Finding available sites or tiles…"));
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
            [this, self, reply, generation, exceeded](qint64 received, qint64 total) {
                if (generation != mGeneration)
                    return;
                mPreviewProgress.update(total > 0 ? qBound(0., double(received) / total, 1.) : -1.,
                                        tr("Receiving catalogue metadata"));
                if (!self || !mHostLifetime || generation != mGeneration) return;
                if (received > maxResponse || total > maxResponse) {
                    *exceeded = true;
                    reply->abort();
                }
            });
    connect(
        reply, &QNetworkReply::finished, this,
        [this, self, reply, generation, url, attempt, redirects, exceeded]() {
            reply->deleteLater();
            if (generation != mGeneration || mClosing || !validateReview() ||
                generation != mGeneration)
                return;
            mReply = nullptr;
            if (*exceeded) {
                setBusy(false);
                mStatus->setText(
                    tr("Catalogue exceeds the preview size limit. Use a smaller area."));
                mPreviewProgress.finish(Sdk::ProgressOutcome::Failed, mStatus->text());
                return;
            }
            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
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
                mPreviewProgress.update(-1, tr("Service busy; waiting to retry"));
                if (!self || !mHostLifetime || generation != mGeneration) return;
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
                mPreviewProgress.finish(Sdk::ProgressOutcome::Failed, mStatus->text());
                return;
            }
            const auto bytes = reply->readAll();
            if (bytes.size() > maxResponse) {
                setBusy(false);
                mStatus->setText(tr("Catalogue exceeds the preview size limit."));
                mPreviewProgress.finish(Sdk::ProgressOutcome::Failed, mStatus->text());
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
    mPreviewProgress.update(-1, tr("Reading saved catalogue"));
    if (!self || !mHostLifetime || generation != mGeneration) return;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > maxResponse) {
        setBusy(false);
        mStatus->setText(tr("Cannot read a catalogue within the 20 MiB preview limit."));
        mPreviewProgress.finish(Sdk::ProgressOutcome::Failed, mStatus->text());
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
    mPreviewProgress.update(-1, tr("Reading cached catalogue"));
    if (!self || !mHostLifetime || generation != mGeneration) return;
    const auto directory = cacheDirectory();
    if (!self || generation != mGeneration)
        return;
    if (directory.isEmpty()) {
        setBusy(false);
        mStatus->setText(
            tr("Save the model in a project folder before using its catalogue cache."));
        mPreviewProgress.finish(Sdk::ProgressOutcome::Failed, mStatus->text());
        return;
    }
    QFile index(QDir(directory).filePath("index.json"));
    if (!index.open(QIODevice::ReadOnly)) {
        setBusy(false);
        mStatus->setText(tr("No cached preview matches this area and source."));
        mPreviewProgress.finish(Sdk::ProgressOutcome::Failed, mStatus->text());
        return;
    }
    const auto filename =
        QJsonDocument::fromJson(index.readAll()).object().value(cacheKey()).toString();
    QFile file(QDir(directory).filePath(filename));
    if (filename.isEmpty() || QFileInfo(filename).fileName() != filename ||
        !file.open(QIODevice::ReadOnly) || file.size() > maxResponse) {
        setBusy(false);
        mStatus->setText(tr("No cached preview matches this area and source."));
        mPreviewProgress.finish(Sdk::ProgressOutcome::Failed, mStatus->text());
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
    mPreviewProgress.update(-1, tr("Filtering catalogue to the reviewed area"));
    if (!self || !hostLifetime || generation != mGeneration) return;
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
                mPreviewProgress.finish(!ok && error.isEmpty() ? Sdk::ProgressOutcome::Cancelled
                                                               : Sdk::ProgressOutcome::Failed,
                                        mStatus->text());
                return;
            }
            if (cache && !cacheDirectory().isEmpty()) {
                mPreviewProgress.update(-1, tr("Saving catalogue cache"));
                if (!guard || !mHostLifetime || generation != mGeneration) return;
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
        mPreviewProgress.finish(Sdk::ProgressOutcome::Failed, mStatus->text());
    }
}
void PreviewPanel::apply(const QList<Item> &items, bool more)
{
    const QPointer<PreviewPanel> self(this);
    const auto generation = mGeneration;
    if (!validateReview() || !self || generation != mGeneration)
        return;
    mHasDisplay = false;
    mItems = items;
    mModel->removeRows(0, mModel->rowCount());
    if (!self || generation != mGeneration)
        return;
    for (int i = 0; i < items.size(); ++i) {
        const auto &item = items[i];
        auto *name = new QStandardItem(item.title);
        name->setData(i, Qt::UserRole);
        name->setData(catalogueProperties(item), CatalogueFilter::PropertiesRole);
        const auto details = item.metadata.value("site_type")
                                 .toString(item.metadata.value("publicationDate").toString());
        mModel->appendRow({name, new QStandardItem(item.id), new QStandardItem(details)});
        if (!self || generation != mGeneration)
            return;
    }
    if (!validateReview() || !self || generation != mGeneration)
        return;
    mValid = true;
    mHasDisplay = true;
    {
        const QSignalBlocker blocker(mFilterField);
        const auto field = mFilterField->currentData().toString();
        QSet<QString> names;
        for (const auto &item : items) {
            const auto properties = catalogueProperties(item);
            for (auto it = properties.begin(); it != properties.end(); ++it)
                names.insert(it.key());
        }
        auto sorted = names.values();
        sorted.sort(Qt::CaseInsensitive);
        mFilterField->clear();
        mFilterField->addItem(tr("All attributes"), QString{});
        for (const auto &name : sorted)
            mFilterField->addItem(name, name);
        mFilterField->setCurrentIndex(qMax(0, mFilterField->findData(field)));
    }
    mStatus->setText(
        tr("%1 results intersect the reviewed area. All filtered results remain on the map. "
           "Select rows to download.%2")
            .arg(items.size())
            .arg(
                more
                    ? tr(" More catalogue results are available; narrow the area or raise the preview limit.")
                    : QString{}));
    if (self && generation == mGeneration)
        filterResults();
    if (self && generation == mGeneration)
        mPreviewProgress.finish(Sdk::ProgressOutcome::Succeeded,
                                tr("%1 results in the reviewed area.").arg(items.size()));
}
QList<Item> PreviewPanel::selectedItems() const
{
    QList<Item> result;
    for (const auto &row : mTable->selectionModel()->selectedRows()) {
        const auto source = mFilter->mapToSource(row);
        bool ok = false;
        const int i = source.data(Qt::UserRole).toInt(&ok);
        if (source.isValid() && mFilter->mapFromSource(source).isValid() && ok && i >= 0 &&
            i < mItems.size())
            result.append(mItems[i]);
    }
    return result;
}
QList<Item> PreviewPanel::visibleItems() const
{
    QList<Item> result;
    // Source order makes a table sort or row selection an identical map update,
    // preserving the native preview layer's query, style and selected FIDs.
    for (int row = 0; row < mModel->rowCount(); ++row) {
        const auto source = mModel->index(row, 0);
        if (!mFilter->mapFromSource(source).isValid())
            continue;
        bool ok = false;
        const int i = source.data(Qt::UserRole).toInt(&ok);
        if (ok && i >= 0 && i < mItems.size())
            result.append(mItems[i]);
    }
    return result;
}
void PreviewPanel::filterResults()
{
    const QPointer<PreviewPanel> self(this);
    if (mFiltering)
        return;
    const auto match = static_cast<CatalogueFilter::Match>(mFilterOperator->currentIndex());
    mFilterText->setEnabled(match != CatalogueFilter::Match::HasValue &&
                            match != CatalogueFilter::Match::IsMissing);
    mFiltering = true;
    const auto release = qScopeGuard([self] {
        if (self)
            self->mFiltering = false;
    });
    mFilter->setCriteria(mFilterField->currentData().toString(), match, mFilterText->text());
    if (!self)
        return;
    mFiltering = false;
    updateAttributes();
    if (!self)
        return;
    updateCounts();
    updateOverlay();
}
void PreviewPanel::updateCounts()
{
    mCounts->setText(tr("%1 shown / %2 total / %3 selected")
                         .arg(mFilter->rowCount())
                         .arg(mItems.size())
                         .arg(selectedItems().size()));
}
void PreviewPanel::updateAttributes()
{
    const QPointer<PreviewPanel> self(this);
    const auto generation = mGeneration;
    const QPersistentModelIndex current(mTable->currentIndex());
    mAttributeModel->removeRows(0, mAttributeModel->rowCount());
    if (!self || generation != mGeneration || current != mTable->currentIndex())
        return;
    bool ok = false;
    const int i = mFilter->index(current.row(), 0).data(Qt::UserRole).toInt(&ok);
    if (!current.isValid() || !ok || i < 0 || i >= mItems.size()) {
        mAttributeTitle->setText(tr("Select a location to inspect its saved attributes."));
        return;
    }
    const auto item = mItems[i];
    mAttributeTitle->setText(tr("Saved attributes: %1 (%2)").arg(item.title, item.id));
    const auto properties = catalogueProperties(item);
    for (auto it = properties.begin(); it != properties.end(); ++it) {
        const auto value = CatalogueFilter::attributeText(it.value());
        auto *name = new QStandardItem(it.key());
        auto *text = new QStandardItem(value);
        text->setToolTip(value);
        mAttributeModel->appendRow({name, text});
        if (!self || generation != mGeneration || current != mTable->currentIndex())
            return;
    }
}
bool PreviewPanel::displayIsCurrent() const
{
    const QPointer<const PreviewPanel> self(this);
    if (!mHostLifetime || !mModelLifetime || !mStateSource || !mInstance || !mProject)
        return false;
    const auto snapshot = mStateSource->stateSnapshot();
    if (!self || !mHostLifetime || !mModelLifetime)
        return false;
    for (const auto &session : snapshot.sessions)
        if (session.sessionId == mSessionId && session.loadGeneration == mLoadGeneration &&
            session.status == Sdk::SessionStatus::Ready)
            return mInstance->isOpen();
    return false;
}
void PreviewPanel::updateOverlay()
{
    const QPointer<PreviewPanel> self(this);
    const auto generation = mGeneration;
    if (!mHasDisplay) {
        mExport->setEnabled(false);
        mDownload->setEnabled(false);
        return;
    }
    const bool current = displayIsCurrent();
    if (!self || generation != mGeneration)
        return;
    if (!current) {
        invalidate(tr("The model changed or closed. Choose a ready model and preview again."));
        if (self)
            bindProject();
        return;
    }
    if (mAwaitingDisplayRevision) {
        mOverlayRefreshPending = true;
        mExport->setEnabled(false);
        mDownload->setEnabled(false);
        return;
    }
    if (mValid && !validateReview())
        return;
    const auto visible = visibleItems();
    auto collection = featureCollection(visible);
    if (mSource->currentIndex() == 0) {
        // The style hint belongs only to this transient display. Saved source
        // properties and provenance are serialized independently below.
        collection["previewStyle"] = QJsonObject{{"pointLabelProperty", "catalogue_title"},
                                                 {"asFeatureLayer", true},
                                                 {"title", tr("USGS gages (preview)")}};
        auto features = collection.value("features").toArray();
        for (qsizetype i = 0; i < visible.size(); ++i) {
            if (!visible[i].title.isEmpty())
                continue;
            auto feature = features[i].toObject();
            auto properties = feature.value("properties").toObject();
            properties["catalogue_title"] = visible[i].id;
            feature["properties"] = properties;
            features[i] = feature;
        }
        collection["features"] = features;
    }
    QString error;
    const bool shown = mProject->showPreview(collection, &error);
    if (!shown)
        if (self)
            mStatus->setText(error);
    if (!self || generation != mGeneration)
        return;
    const bool stillCurrent = displayIsCurrent();
    if (!self || generation != mGeneration)
        return;
    if (!stillCurrent) {
        invalidate(tr("The model changed or closed. Choose a ready model and preview again."));
        return;
    }
    if (shown && mValid) {
        ReviewContext currentReview;
        const bool captured = captureContext(&currentReview);
        if (!self || generation != mGeneration)
            return;
        if (captured && sameContent(mReview, currentReview) &&
            currentReview.stateRevision != mReview.stateRevision) {
            // Native temporary-layer insertion/replacement can advance the
            // source revision. Retain the display, but only delivered detailed
            // evidence may rebase the review; the write itself grants nothing.
            mAwaitingDisplayRevision = true;
            mOverlayRefreshPending = true;
            mExport->setEnabled(false);
            return;
        }
    }
    if (mValid && !validateReview())
        return;
    if (self)
        setBusy(mBusy);
}
bool PreviewPanel::importObservations(const PayloadResult &result)
{
    const auto folder = QFileInfo(result.manifestPath).absolutePath();
    if (!mValid || !mHostLifetime || !mModelLifetime || !mProject || !mTimeSeriesLayers ||
        !displayIsCurrent() || !validateReview()) {
        mStatus->setText(
            tr("Observations saved, not added. Reopen the saved import descriptor in a ready project: %1")
                .arg(result.descriptorPath));
        mImportProgress.log("warning", mStatus->text());
        return true;
    }
    Sdk::TimeSeriesImportRequest request;
    request.sessionId = mSessionId;
    request.loadGeneration = mLoadGeneration;
    request.importId = result.importId;
    request.descriptorPath = result.descriptorPath;
    request.packagePath = QDir(folder).filePath("series.gpkg");
    const auto generation = mGeneration;
    const QPointer<PreviewPanel> self(this);
    mImportPending = true;
    mImportProgress.begin(tr("Add downloaded time-series layer"));
    if (!self || !mHostLifetime || generation != mGeneration) return true;
    // The host import job owns preparation progress after this handoff.
    mImportProgress.update(1., tr("Files handed to native dataset import"));
    if (!self || !mHostLifetime || generation != mGeneration) return true;
    setBusy(true);
    mStatus->setText(tr("Observations saved. Adding time-series layer…"));
    QString error;
    auto *job = mTimeSeriesLayers->importTimeSeries(
        request, this,
        [self, generation, folder](const Sdk::TimeSeriesImportResult &imported) {
            if (!self || self->mClosing || self->mGeneration != generation)
                return;
            self->mImportJob = nullptr;
            self->mImportPending = false;
            self->setBusy(false);
            if (!imported.layerId.isEmpty())
                self->mStatus->setText(
                    tr("Added %1 series to Layers. Right-click the layer to plot or use in model. Files: %2")
                        .arg(imported.seriesCount)
                        .arg(folder));
            else
                self->mStatus->setText(
                    tr("Observations saved, not added: %1\nFiles: %2")
                        .arg(imported.cancelled ? tr("Import cancelled.") : imported.error,
                             folder));
            self->mImportProgress.finish(imported.cancelled ? Sdk::ProgressOutcome::Cancelled
                                         : imported.layerId.isEmpty() ? Sdk::ProgressOutcome::Failed
                                                                       : Sdk::ProgressOutcome::Succeeded,
                                         self->mStatus->text());
        },
        &error);
    if (!self)
        return true;
    if (generation != mGeneration) {
        if (job && mHostLifetime)
            job->cancel();
        return true;
    }
    if (mImportPending) {
        mImportJob = job;
        if (!job) {
            mImportPending = false;
            setBusy(false);
            mStatus->setText(tr("Observations saved, not added: %1\nFiles: %2").arg(error, folder));
            mImportProgress.finish(Sdk::ProgressOutcome::Failed, mStatus->text());
        }
    }
    return true;
}
void PreviewPanel::downloadSelection()
{
    if (!mDownload->isEnabled())
        return;
    const QPointer<PreviewPanel> self(this);
    const auto directory = QFileDialog::getExistingDirectory(
        this, tr("Choose download folder"), mProject ? mProject->directory() : QString{});
    if (self && !directory.isEmpty())
        downloadTo(directory);
}
void PreviewPanel::downloadTo(const QString &directory)
{
    const auto selected = selectedItems();
    if (mClosing || mBusy || !mPayload || mPayload->isRunning() || !mValid ||
        mAwaitingDisplayRevision || selected.isEmpty() || !mHostLifetime || !mModelLifetime ||
        !mProject || !displayIsCurrent() || !validateReview())
        return;
    PayloadRequest request;
    request.source = mSource->currentIndex() == 0 ? Source::Sites : Source::Terrain;
    request.items = selected;
    request.from = mStartDate->date();
    request.to = mEndDate->date();
    request.daily = mInterval->currentIndex() == 0;
    request.parameterCode =
        mParameter->currentText() == mParameter->itemText(mParameter->currentIndex())
            ? mParameter->currentData().toString()
            : mParameter->currentText().trimmed();
    request.destinationDir = directory;
    request.provenance = QJsonObject{{"area", mArea},
                                     {"intendedQuery", mQuery.toString()},
                                     {"response", mProvenance},
                                     {"sessionId", mSessionId.toString(QUuid::WithoutBraces)}};
    QString error;
    const QPointer<PreviewPanel> self(this);
    const bool started = mPayload->start(request, &error);
    if (!self) return;
    if (!started) {
        mStatus->setText(error);
        return;
    }
    setBusy(true);
}
void PreviewPanel::exportSelection()
{
    const auto selected = selectedItems();
    if (!mValid || mAwaitingDisplayRevision || selected.isEmpty() || !mHostLifetime ||
        !mModelLifetime || !mProject || !mInstance->isOpen() || !validateReview())
        return;
    if (cacheDirectory().isEmpty()) {
        mStatus->setText(tr("Save the model in a project folder before saving features."));
        return;
    }
    const QPointer<PreviewPanel> self(this);
    const auto generation = mGeneration;
    OperationProgress progress(mHost, this);
    progress.begin(tr("Save selected catalogue features"));
    if (!self || !mHostLifetime || generation != mGeneration) return;
    progress.update(-1, tr("Writing reviewed features"));
    if (!self || !mHostLifetime || generation != mGeneration) return;
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
        progress.finish(Sdk::ProgressOutcome::Failed, mStatus->text());
        return;
    }
    QString error;
    progress.update(-1, tr("Adding saved feature layer"));
    if (!self || !mHostLifetime || generation != mGeneration) return;
    const auto id = mProject->addFeatureLayer(
        path, mSource->currentIndex() == 0 ? tr("USGS sites") : tr("Elevation footprints"), &error);
    if (!self)
        return;
    mStatus->setText(id.isEmpty() ? error
                                  : tr("Saved selected features as a map layer: %1").arg(path));
    progress.finish(id.isEmpty() ? Sdk::ProgressOutcome::Failed : Sdk::ProgressOutcome::Succeeded,
                    mStatus->text());
}
}
