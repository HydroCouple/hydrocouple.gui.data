/**
 * @file PreviewPanel.hpp
 * @brief Compact area, catalogue review and observation/raster download dock.
 * @details Area and result identities are frozen together. Relevant model/view/layer changes
 * invalidate the review before another export or download. Network requests use the host policy client;
 * copied geometry/response parsing runs in cancellable host jobs. The host owns this widget.
 * @author Caleb Buahin <caleb.buahin@gmail.com>
 * @date 2026
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license GPL-3.0-or-later
 */
#pragma once
#include "Catalog.hpp"
#include "OperationProgress.hpp"
#include <sdk/ExtensionHost.hpp>
#include <sdk/IProject.hpp>
#include <sdk/IEventSourceV2.hpp>
#include <sdk/IEventSourceV3.hpp>
#include <sdk/IEventSourceV4.hpp>
#include <sdk/IViewControlsV1.hpp>
#include <sdk/IPreviewLayersV1.hpp>
#include <sdk/ITimeSeriesLayersV1.hpp>
#include <QPointer>
#include <QWidget>

class QComboBox;
class QCheckBox;
class QLabel;
class QPushButton;
class QTableView;
class QStandardItemModel;
class QNetworkReply;
class QLineEdit;
class QDateEdit;

namespace OpenSWMM::Data
{
class CatalogueFilter;
class PayloadDownload;
struct PayloadResult;
/** @brief Hosted review/download UI; payload transfers and GIS export require a current reviewed area. */
class PreviewPanel final : public QWidget
{
    Q_OBJECT
public:
    /** @brief Construct the dock contents and subscribe to guarded SDK notifications.
     * @param[in] host Borrowed initialized extension host, outliving this widget.
     * @param[in] parent Optional owning QWidget. */
    explicit PreviewPanel(Sdk::ExtensionHost *host, QWidget *parent = nullptr);
    /** @brief Cancel pending requests/jobs and remove this plugin's transient overlay. */
    ~PreviewPanel() override;
    /** @brief Select a catalogue adapter from a ribbon command.
     * @param[in] source Monitoring locations or elevation footprints. */
    void setSource(Source source);
    /** @brief Cancel outstanding previews, payload transfers and jobs without publishing partial data files. */
    void cancel();
public slots:
    /** @brief Clear only disposable catalogue graphics and rows, cancelling any pending preview.
     * @note Saved downloads, exported feature layers, native time-series layers and rasters remain.
     * Refused during an accepted payload transfer or native import; Cancel controls those operations. */
    void clearPreview();
    /** @brief Preview an explicitly chosen saved response using the currently chosen area/source.
     * @param[in] path Existing JSON response file, bounded to 20 MiB; no network request is made.
     * @note Also supports deterministic offline GUI conformance without private host headers. */
    void loadCatalogue(const QString &path);
    /** @brief Download the selected reviewed records into a new child of the chosen folder.
     * @param[in] directory User-chosen destination; existing files are never overwritten.
     * @note Uses the visible USGS period/parameter controls. Raster results are added only
     * while the original model and review remain current. Observation files preserve source
     * units and timestamps; downloading does not alter model time series. */
    void downloadTo(const QString &directory);

private:
    /** @brief Owning source stamp; sequence and revision are separate from semantic content. */
    struct ReviewContext {
        QUuid sessionId;            ///< Exact Ready per-open model lifetime.
        quint64 loadGeneration = 0; ///< Exact accepted native load.
        quint64 stateRevision = 0;  ///< Source-authoritative fence against pending changes.
        quint64 sequence = 0;       ///< Immutable review-adoption cutoff in the shared stream.
        QUuid attachmentId;         ///< Exact chosen boundary attachment; null in viewport mode.
        qint64 featureId = -1;      ///< Chosen source FID; never inferred from a row index.
        bool polygon = false;       ///< True for a chosen loaded feature rather than the viewport.
        QJsonObject area;         ///< Exact WGS84 polygon/multipolygon, including holes and parts.
        QJsonObject metadata;     ///< Available source/filter/CRS/eligibility values.
        QByteArray selectionHash; ///< Hash of every sorted selected FID, with bounded page storage.
    };
    /** @brief Bind the actual Ready model context, retaining it while utility documents have focus.
     * Same-session tab focus refreshes controls without replacing the preview layer. */
    void bindProject();
    /** @brief Refresh unambiguous polygon attachments, preserving their runtime identities. */
    void refreshLayers();
    /** @brief Refresh up to 5000 eligible boundary choices, preserving the chosen FID. */
    void refreshFeatures();
    /** @brief Cancel pending work and discard all review authority.
     * @param[in] message Optional user-facing explanation.
     * @param[in] retainDisplay Keep an adopted catalogue for inspection, without export authority. */
    void invalidate(const QString &message = {}, bool retainDisplay = false);
    /** @brief Capture a coherent current source stamp without granting historical authority.
     * @param[out] context Exact owning current source values.
     * @param[out] error Optional unavailable/ambiguous/stale-context detail.
     * @return True only for a coherent Ready source and eligible chosen boundary. */
    bool captureContext(ReviewContext *context, QString *error = nullptr) const;
    /** @brief Compare semantic source values while excluding publication counters.
     * @param[in] left Earlier owning context.
     * @param[in] right Current owning context.
     * @return True for the same session/load, attachment/FID and exact reviewed inputs. */
    static bool sameContent(const ReviewContext &left, const ReviewContext &right);
    /** @brief Refuse publication after a source or pending revision change.
     * @return True only while the adopted review remains authoritative. */
    bool validateReview();
    /** @brief Post unrelated revision adoption after specific or unambiguous typed evidence.
     * @param[in] state Last delivered copied state for this exact session/generation. */
    void observeRevision(const Sdk::ModelSessionState &state);
    /** @brief Apply typed model lifecycle and viewport invalidations.
     * Completed viewport reviews suspend writes until the publication turn's final
     * coherent area is reconciled, so a transient tab resize cannot revoke them.
     * Pending preview/download/import work still loses authority on an area mutation.
     * @param[in] event Deferred source-authoritative state transition. */
    void stateChanged(const Sdk::StateEvent &event);
    /** @brief Apply typed chosen-boundary invalidations and refresh choices.
     * @param[in] event Deferred current/historical attachment observation. */
    void viewChanged(const Sdk::ViewEvent &event);
    /** @brief Adopt an exact source/query stamp for a new explicit preview.
     * @return False when no coherent area/query can be captured. */
    bool beginPreview();
    /** @brief Request metadata only while the frozen review still matches its native source.
     * @param[in] url Policy-controlled HTTPS catalogue endpoint.
     * @param[in] attempt Bounded retry index.
     * @param[in] redirects Bounded redirect count. */
    void request(const QUrl &url, int attempt = 0, int redirects = 0);
    /** @brief Parse owning bytes in a cancellable data-only job.
     * @param[in] bytes Bounded recorded/cache/network response.
     * @param[in] cache Publish successful raw bytes into the project cache. */
    void parse(const QByteArray &bytes, bool cache);
    /** @brief Publish complete results only after revalidating the native source.
     * @param[in] items Complete exact-area filtered records.
     * @param[in] more True when the server advertises additional catalogue records. */
    void apply(const QList<Item> &items, bool more);
    /** @brief Refresh transient geometry and write eligibility, awaiting qualified native revisions.
     * Read-only selection waits when the unchanged source has an undelivered revision;
     * matching detailed evidence restores controls, while relevant source changes still
     * discard authority. This wait never authorizes pending-source downloads or exports. */
    void updateOverlay();
    /** @brief Apply the local saved-property predicate without changing the service query. */
    void filterResults();
    /** @brief Show every saved property of the current visible catalogue location. */
    void updateAttributes();
    /** @brief Refresh visible, total and visible-selected counts. */
    void updateCounts();
    /** @brief Select the catalogue rows of the native preview layer's selected features.
     * @note Map select tool and native attribute-table changes arrive as LayerSelectionChanged. */
    void pullNativeSelection();
    /** @brief Replace the native preview layer's selection with the selected visible rows.
     * @note The write advances the source revision; downloads wait for its detailed evidence. */
    void pushNativeSelection();
    /** @brief Copy visible records in stable source order, independent of table sorting.
     * @param[out] indices Optional mItems index of each returned record.
     * @return Owning records matching the current local predicate. */
    QList<Item> visibleItems(QList<int> *indices = nullptr) const;
    /** @brief Check display ownership against current native readiness, without granting export.
     * @return True only for the same Ready session and accepted load as the bound model. */
    bool displayIsCurrent() const;
    /** @brief Persist the frozen area/selected records only after an authoritative source check. */
    void exportSelection();
    /** @brief Choose an output folder, then download selected observations or raster tiles. */
    void downloadSelection();
    /** @brief Begin guarded native import after a complete observation download.
     * @param[in] result Completed descriptor and immutable import identity.
     * @return True when the method owns the resulting progress/refusal message. */
    bool importObservations(const PayloadResult &result);
    /** @brief Show only controls relevant to the selected source and area mode. */
    void updateControls();
    /** @brief Open cached raw metadata matching the exact current area/query. */
    void useCache();
    /** @brief Copy selected catalogue records independently of table sorting.
     * @return Owning selected records. */
    QList<Item> selectedItems() const;
    /** @brief Resolve reviewable project-local raw response storage.
     * @return Absolute catalogue directory, or empty before the model is saved. */
    QString cacheDirectory() const;
    /** @brief Identify an exact query/geometry cache entry.
     * @return Stable SHA-256 hexadecimal key. */
    QString cacheKey() const;
    /** @brief Refresh control availability without granting new source authority.
     * @param[in] busy Preview retrieval/parsing is pending; active payload work also keeps controls busy. */
    void setBusy(bool busy);
    OperationProgress mPreviewProgress; ///< Enclosing metadata request/cache/parse progress.
    OperationProgress mImportProgress; ///< Native publication progress independent of downloaded files.
    Sdk::ExtensionHost *mHost;       ///< Borrowed host; use only while mHostLifetime survives.
    QPointer<QObject> mHostLifetime; ///< Guard against host destruction during queued/native calls.
    QPointer<QObject> mModelLifetime;               ///< Exact bound native model lifetime.
    Sdk::IEventSourceV2 *mStateSource = nullptr;    ///< Borrowed Ready/session/revision source.
    Sdk::IEventSourceV3 *mViewSource = nullptr;     ///< Borrowed runtime attachment source.
    Sdk::IEventSourceV4 *mActivitySource = nullptr; ///< Borrowed actual document-focus source.
    Sdk::IViewControlsV1 *mViewControls = nullptr;  ///< Borrowed paged selected-FID query.
    Sdk::IPreviewLayersV1 *mPreviewLayers = nullptr; ///< Borrowed native preview attachment lookup.
    Sdk::ITimeSeriesLayersV1 *mTimeSeriesLayers =
        nullptr;                     ///< Borrowed native dataset import service.
    Sdk::IJob *mImportJob = nullptr; ///< Borrowed host-owned native preparation job.
    bool mImportPending =
        false; ///< Keep controls/cancellation active until native publication finishes.
    Sdk::IModelInstance *mInstance = nullptr; ///< Borrowed exact Ready facade.
    Sdk::IProject *mProject = nullptr;        ///< Borrowed project belonging to mInstance.
    QUuid mSessionId; ///< Per-open identity; persistent instance UUIDs are not unique sessions.
    quint64 mLoadGeneration = 0;   ///< Exact bound accepted load.
    quint64 mInitialWatermark = 0; ///< Immutable subscribe-before-snapshot initialization cutoff.
    quint64 mObservedRevision =
        0; ///< Maximum revision supported by specific/unambiguous delivered evidence.
    bool mStartingPreview =
        false; ///< Refuse nested explicit starts until the entire startup path returns.
    bool mRebaseScheduled = false; ///< At most one posted unrelated-revision reconciliation.
    bool mOverlayRefreshPending =
        false; ///< Redraw frozen polygon graphics only after safe rebasing.
    bool mAwaitingDisplayRevision =
        false; ///< A native preview write awaits detailed source evidence before export.
    ReviewContext mReview;    ///< Frozen review authority; never synthesized from callback row IDs.
    QComboBox *mSource;       ///< Catalogue provider selector.
    QComboBox *mAreaMode;     ///< Viewport or loaded-boundary mode.
    QComboBox *mLayers;       ///< Native IDs plus unambiguous runtime attachment item data.
    QComboBox *mFeatures;     ///< Source FIDs for the first 5000 eligible boundary choices.
    QComboBox *mDataset;      ///< Terrain product query selector.
    QCheckBox *mSelectedOnly; ///< Restrict boundary choices to selected native features.
    QLabel *mStatus;          ///< Current plain-text progress/refusal explanation.
    QPushButton *mPreview;    ///< Explicit metadata request action.
    QPushButton *mClear; ///< Explicit disposal of preview-only graphics and catalogue selection.
    QPushButton *mCancel;     ///< Pending request/job cancellation action.
    QPushButton *mCache;      ///< Exact matching cached-response action.
    QPushButton *mRecorded;   ///< Explicit saved-catalogue action.
    QPushButton *mExport;     ///< Frozen selected-record GIS export action.
    QPushButton *mDownload;   ///< Primary source-specific payload download action.
    PayloadDownload *mPayload = nullptr; ///< Owned cancellable payload transfer controller.
    QWidget *mBoundaryControls;          ///< Hidden unless a loaded boundary polygon is chosen.
    QWidget *mTerrainControls;           ///< Terrain resolution, hidden for USGS observations.
    QWidget *mPeriodControls;            ///< USGS period, sampling interval and parameter.
    QDateEdit *mStartDate;      ///< Inclusive first calendar date of the observation request.
    QDateEdit *mEndDate;        ///< Inclusive last calendar date of the observation request.
    QComboBox *mInterval;       ///< Continuous measurements or daily observations.
    QComboBox *mParameter;      ///< USGS five-digit parameter code with familiar names.
    QTableView *mTable;         ///< Catalogue selection/sorting view.
    QStandardItemModel *mModel; ///< Complete currently reviewed catalogue records.
    CatalogueFilter *mFilter;   ///< Local saved-property predicate over stable source rows.
    QComboBox *mFilterField;    ///< All values or one exact saved property name.
    QComboBox *mFilterOperator; ///< Literal, numeric or presence comparison.
    QLineEdit *mFilterText;     ///< Plain comparison text; never a regular expression.
    QLabel *mCounts;            ///< Visible/total/visible-selected catalogue counts.
    QLabel *mAttributeTitle;    ///< Current location identity, or selection guidance.
    QTableView *mAttributes;    ///< Read-only complete saved-property inspection.
    QStandardItemModel *mAttributeModel; ///< Plain values, including nested JSON and explicit null.
    QPointer<QNetworkReply> mReply;      ///< Active policy-controlled reply.
    Sdk::IJob *mJob = nullptr;           ///< Borrowed host-retained current parsing job.
    QList<Item> mItems;                  ///< Owning complete exact-area records.
    QList<int> mPublished; ///< mItems indices in the feature order of the shown preview.
    QJsonObject mArea;                   ///< Frozen reviewed geometry used for parsing/provenance.
    QJsonObject mProvenance;             ///< Actual response origin, separate from intended query.
    QUrl mQuery;                         ///< Frozen intended metadata query.
    quint64 mGeneration = 0;  ///< Plugin work generation; increments on cancellation/invalidation.
    bool mValid = false;      ///< Complete results match the retained review authority.
    bool mHasDisplay = false; ///< An adopted catalogue may outlive current export authority.
    bool mFiltering = false; ///< Suppress intermediate selection publications during proxy changes.
    bool mSyncingSelection = false; ///< Applying native preview selection to rows; do not echo it.
    bool mBusy = false;      ///< Preview request/parsing or a payload transfer is pending.
    bool mClosing = false;   ///< Widget destruction suppresses callbacks and publications.
};
}
