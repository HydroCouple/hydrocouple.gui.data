/**
 * @file StormPanel.hpp
 * @brief Live NOAA tables and synthetic-storm evaluation without response files.
 * @details A host-owned workspace shares bounded session values between the frequency
 * and storm docks. Evaluation never modifies a model. Applying a selected candidate
 * uses one mediated model edit and needs no downloaded file or native-layer import.
 * @author Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license GPL-3.0-or-later
 */
#pragma once
#include "Precipitation.hpp"
#include "OperationProgress.hpp"
#include <sdk/ISessionEvents.hpp>
#include <QPointer>
#include <QWidget>

class QComboBox;
class QCheckBox;
class QDateTimeEdit;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QStandardItemModel;
class QTableView;
class QPlainTextEdit;
class QTabWidget;
class QVBoxLayout;

namespace OpenSWMM::Data
{
class NoaaClient;
class StormChart;

/** @brief Shared session-only NOAA responses; never serialized into project/settings. */
class StormWorkspace final : public QObject
{
    Q_OBJECT
public:
    /** @brief Create a guarded network/evaluation workspace.
     * @param[in] host Initialized borrowed SDK host.
     * @param[in] parent Optional lifetime owner, normally the extension host. */
    explicit StormWorkspace(Sdk::ExtensionHost *host, QObject *parent = nullptr);
    /** @brief Stop pending requests before owned data/client destruction. */
    ~StormWorkspace() override;
    /** @return Borrowed immutable accepted table, empty before a successful request. */
    const FrequencyTable &frequency() const { return mFrequency; }
    /** @return Borrowed immutable live temporal patterns. */
    const QList<CumulativeCurve> &curves() const { return mCurves; }
    /** @return True while a live network operation is accepted. */
    bool isBusy() const;
    /** @return Monotonic memory revision for candidate/source identity. */
    quint64 revision() const { return mRevision; }
    /** @brief Request an exact live table and retire pending work.
     * @param[in] query Accepted point/source options.
     * @param[out] error Optional immediate refusal detail.
     * @return True after the client accepts the request. */
    bool fetch(const FrequencyQuery &query, QString *error = nullptr);
    /** @brief Retrieve a temporal product for explicit region/area/duration choices.
     * @param[in] region NOAA temporal product region key.
     * @param[in] area Explicit product area key.
     * @param[in] durationMinutes Native temporal duration.
     * @param[out] error Optional immediate refusal detail.
     * @return True after request acceptance. */
    bool fetchTemporal(const QString &region, const QString &area, int durationMinutes,
                       QString *error = nullptr);
    /** @brief Accept structured values from the checked client or deterministic SDK tests.
     * @param[in] table Complete canonical response value.
     * @param[out] error Optional malformed-axis detail.
     * @return True when a nonempty valid table was copied into session memory. */
    bool acceptFrequency(const FrequencyTable &table, QString *error = nullptr);
public slots:
    /** @brief Abort pending network work; keep the preceding explicitly identified response. */
    void cancel();
    /** @brief Cancel and release all accepted NOAA responses/patterns. */
    void clear();
signals:
    /** @brief Accepted table or Clear changed the session-only source values. */
    void frequencyChanged();
    /** @brief Accepted temporal patterns or Clear changed the pattern choices. */
    void curvesChanged();
    /** @brief Pending network work changed UI control eligibility. */
    void busyChanged();
    /** @brief A plain summary suitable for a dock status, without response payloads.
     * @param text Request stage or terminal detail. */
    void message(const QString &text);
private:
    /** @brief Bind stage progress to one registered tool.
     * @param toolId Owner-local registered tool identity.
     * @param title User-facing operation title. */
    void begin(const QString &toolId, const QString &title);
    /** @brief Settle the current parent operation exactly once.
     * @param outcome Actual terminal status.
     * @param detail Literal summary. */
    void finish(Sdk::ProgressOutcome outcome, const QString &detail);
    /** @brief Publish only the accepted point to this tool's disposable native preview. */
    void showPointPreview();
    /** @brief Remove this tool's preview while preserving USGS and terrain previews. */
    void clearPointPreview();
    Sdk::ExtensionHost *mHost = nullptr; ///< Borrowed while mLifetime survives.
    QPointer<QObject> mLifetime; ///< Guard against host shutdown callbacks.
    NoaaClient *mClient = nullptr; ///< Workspace-owned live request controller.
    OperationProgress mProgress; ///< Parent progress/logs for accepted live requests.
    FrequencyTable mFrequency; ///< Most recently accepted bounded in-memory table.
    QList<CumulativeCurve> mCurves; ///< Most recently accepted bounded temporal response.
    quint64 mRevision = 0; ///< Changes when accepted response authority is replaced/cleared.
    bool mOperation = false; ///< An accepted parent run still needs settlement.
    QUuid mPreviewSession; ///< Original model lifetime of the displayed point.
};

/** @brief One compact tool dock, sharing response values but owning its editable inputs.
 * @details Inputs scroll independently of the chart/value pane. A vertical splitter
 * gives the user control of their relative heights without allowing an expanded
 * source or model-target form to increase the enclosing dock's minimum size. */
class StormPanel final : public QWidget
{
    Q_OBJECT
public:
    /** @brief Distinguish the frequency table tool from the storm-generation tool. */
    enum class Role { Frequency, Design };
    /** @brief Construct tool contents without fetching or writing data.
     * @param[in] host Borrowed public SDK host.
     * @param[in] role Tool content to show.
     * @param[in] workspace Shared session-only accepted responses.
     * @param[in] parent Optional QWidget owner. */
    StormPanel(Sdk::ExtensionHost *host, Role role, StormWorkspace *workspace,
               QWidget *parent = nullptr);
    /** @return Copied current generated candidates, at most eight overlay curves. */
    QList<StormSeries> candidates() const { return mCandidates; }
public slots:
    /** @brief Abort live NOAA work without erasing accepted values. */
    void cancel();
    /** @brief Release the shared live response and this dock's candidate curves. */
    void clear();
    /** @brief Generate/overlay a candidate only in memory using visible inputs. */
    void generate();
    /** @brief Apply the explicitly selected candidate through one undoable model edit. */
    void applyToModel();
signals:
    /** @brief A complete model transaction created/rebound forcing.
     * @param series Time-series identity.
     * @param gage Rain-gage identity. */
    void applied(const QString &series, const QString &gage);
private:
    /** @brief Build common live-source controls.
     * @param layout Target vertical layout, borrowed during construction. */
    void buildSource(QVBoxLayout *layout);
    /** @brief Build frequency inputs and independently resizable table/charts.
     * @param controls Scrollable input layout.
     * @param charts Chart-pane layout, outside the input scroll area. */
    void buildFrequency(QVBoxLayout *controls, QVBoxLayout *charts);
    /** @brief Build candidate/pattern/target inputs and their resizable previews.
     * @param layout Scrollable input layout, including expandable model targets.
     * @param charts Chart-pane layout, outside the input scroll area. */
    void buildDesign(QVBoxLayout *layout, QVBoxLayout *charts);
    /** @brief Reflect accepted values in table, frequency columns and source caption. */
    void refreshFrequency();
    /** @brief Refresh live temporal-pattern choices. */
    void refreshCurves();
    /** @brief Repaint the selected frequency or storm basis. */
    void refreshChart();
    /** @brief Refresh appropriate method-only options and invalidate pending application. */
    void inputsChanged();
    /** @brief Query the current Ready target and verified engine rain units. */
    void refreshTarget();
    /** @brief Synchronize enabled state without changing model/source content. */
    void controls();
    /** @brief Copy the visible public query.
     * @return Owning request, not network acceptance. */
    FrequencyQuery query() const;
    /** @brief Resolve one exact current Ready target.
     * @param[out] state Target copied lifecycle state.
     * @return Borrowed model or null; caller must revalidate before edits. */
    Sdk::IModelInstance *target(Sdk::ModelSessionState *state) const;
    Sdk::ExtensionHost *mHost = nullptr; ///< Borrowed public host.
    QPointer<QObject> mLifetime; ///< Host validity guard.
    QPointer<StormWorkspace> mWorkspace; ///< Shared response lifetime, not transferred.
    Role mRole; ///< Fixed tool role.
    QDoubleSpinBox *mLatitude = nullptr; ///< Decimal WGS84 query latitude.
    QDoubleSpinBox *mLongitude = nullptr; ///< Decimal WGS84 query longitude.
    QComboBox *mUnits = nullptr; ///< Presentation/request units; canonical storage is mm.
    QComboBox *mStatisticalSeries = nullptr; ///< AMS/PDS source request option.
    QComboBox *mLocationGage = nullptr; ///< Verified existing gage location as a query input.
    QPushButton *mFetch = nullptr; ///< Explicit live query.
    QPushButton *mCancel = nullptr; ///< Explicit cancellation.
    QLabel *mSourceCaption = nullptr; ///< Exact accepted source identity/time.
    QLabel *mStatus = nullptr; ///< Plain non-payload operation feedback.
    QComboBox *mBasis = nullptr; ///< DDF/IDF or hyetograph/cumulative chart basis.
    StormChart *mChart = nullptr; ///< Lightweight host-theme-aware chart.
    QTableView *mTable = nullptr; ///< Accepted values or generated interval values.
    QStandardItemModel *mTableModel = nullptr; ///< Widget-owned read-only tabular values.
    QComboBox *mDepthSource = nullptr; ///< Live NOAA or manual total depth.
    QComboBox *mFrequency = nullptr; ///< Return-period source column.
    QComboBox *mBound = nullptr; ///< Source estimate/uncertainty bound.
    QDoubleSpinBox *mDepth = nullptr; ///< Manual depth in selected display units.
    QSpinBox *mDuration = nullptr; ///< Duration, minutes.
    QSpinBox *mInterval = nullptr; ///< Regular recording interval, minutes.
    QDoubleSpinBox *mPeak = nullptr; ///< Relative peak for applicable methods.
    QComboBox *mMethod = nullptr; ///< Qualified generator family.
    QComboBox *mTemporalRegion = nullptr; ///< Explicit NOAA product region.
    QLineEdit *mTemporalArea = nullptr; ///< Explicit NOAA product area.
    QComboBox *mCurve = nullptr; ///< Selected live temporal pattern.
    QWidget *mTemporalControls = nullptr; ///< Shown only for the NOAA temporal method.
    QPlainTextEdit *mCustomCurve = nullptr; ///< Normalized custom time/depth pairs.
    QDateTimeEdit *mStart = nullptr; ///< Explicit model wall-clock storm start.
    QComboBox *mCandidate = nullptr; ///< Explicit overlaid candidate selected for application.
    QPushButton *mGenerate = nullptr; ///< Pure in-memory generation.
    QPushButton *mApply = nullptr; ///< Explicit model forcing operation.
    QLineEdit *mSeriesName = nullptr; ///< New unique model time-series ID.
    QComboBox *mGage = nullptr; ///< New gage or explicitly selected existing gage.
    QLineEdit *mGageName = nullptr; ///< New unique model rain-gage ID.
    QCheckBox *mResetFactor = nullptr; ///< Explicit choice to avoid an existing gage scaling the storm.
    QCheckBox *mPlaceGage = nullptr; ///< Optional georeferenced placement; otherwise a gage is unlocated.
    QLabel *mTargetCaption = nullptr; ///< Exact current model and engine rainfall units.
    QList<StormSeries> mCandidates; ///< Up to eight generated overlays, no response serialization.
    QList<QDateTime> mStarts; ///< Explicit wall-clock start of each candidate.
    QList<QJsonObject> mProvenance; ///< Compact accepted source/method values per candidate.
    Sdk::ModelSessionState mTargetState; ///< Reviewed Ready target/revision for application.
    QString mRainUnit; ///< Verified engine depth unit: mm or in.
    double mDisplayDepthToMm = 1.; ///< Previous display scale for preserving manual physical depth.
    bool mCandidateValid = false; ///< Inputs still match an explicitly selected candidate.
    bool mUpdating = false; ///< Suppress invalidation while reflecting source/target values.
};
}
