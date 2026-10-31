/**
 * @file StormPanel.cpp
 * @brief Compact live frequency/storm docks and explicit undoable rainfall application.
 * @details Charts, response tables and comparison candidates exist only in memory.
 * No file dialog, response archive, cache or settings write is used by evaluation.
 * @author Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license GPL-3.0-or-later
 */
#include "StormPanel.hpp"
#include "NoaaClient.hpp"
#include <sdk/IModelRainfall.hpp>
#include <sdk/IModelInstance.hpp>
#include <sdk/IProject.hpp>
#include <sdk/IToolbox.hpp>
#include <sdk/IToolRuns.hpp>
#include <sdk/IToolPreviews.hpp>
#include <QComboBox>
#include <QCheckBox>
#include <QDateTimeEdit>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QHash>
#include <QPainter>
#include <QPainterPath>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QScrollArea>
#include <QSplitter>
#include <QStandardItemModel>
#include <QTableView>
#include <QTabWidget>
#include <QToolButton>
#include <QTimeZone>
#include <QTextDocument>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace OpenSWMM::Data
{
namespace
{
/** @brief Apply a semantic widget identity used by SDK-level GUI tests. */
template<class T> T *named(T *widget, const char *name)
{
    widget->setObjectName(QString::fromLatin1(name));
    return widget;
}
/** @brief Render small source values as plain text, without locale-dependent parsing. */
QString number(double value, int precision = 6)
{
    return QString::number(value, 'g', precision);
}
/** @brief Select a validated duration-major source matrix. */
const QVector<QVector<double>> &matrix(const FrequencyTable &table, FrequencyBound bound)
{
    return bound == FrequencyBound::Lower ? table.lowerMm
         : bound == FrequencyBound::Upper ? table.upperMm : table.depthsMm;
}
/** @brief Preserve wall-clock fields while making elapsed arithmetic immune to host DST. */
QDateTime modelClock(const QDateTime &value)
{
    return QDateTime(value.date(), value.time(), QTimeZone::UTC);
}
}

/** @brief Small palette-aware comparison plot without a separate chart-library dependency. */
class StormChart final : public QWidget
{
public:
    /** @brief Create a paint-only chart owned by the surrounding tool.
     * @param[in] parent Optional owning tool widget. */
    explicit StormChart(QWidget *parent = nullptr) : QWidget(parent)
    {
        setMinimumHeight(100);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        setAccessibleName(tr("Precipitation curve comparison"));
        setAccessibleDescription(tr("Click a legend entry to show or hide its curve."));
        setToolTip(tr("Click a legend entry to show or hide a curve."));
    }
    /** @brief Replace bounded owning curves and axis labels.
     * @param[in] labels Legend captions in curve order.
     * @param[in] curves Finite canonical axis/value pairs.
     * @param[in] xLabel Horizontal quantity and display units.
     * @param[in] yLabel Vertical quantity and display units.
     * @param[in] logX True to plot positive duration on a logarithmic axis. */
    void setCurves(QStringList labels, QVector<QVector<QPointF>> curves,
                   QString xLabel, QString yLabel, bool logX = false)
    {
        QHash<QString, bool> visibility;
        for (const auto &label : labels)
            visibility.insert(label, mVisibility.value(label, true));
        mVisibility = std::move(visibility);
        mLabels = std::move(labels);
        mCurves = std::move(curves);
        mXLabel = std::move(xLabel);
        mYLabel = std::move(yLabel);
        mLogX = logX;
        setToolTip(tr("Click a visible legend entry to show or hide its curve.\n%1 / %2\n%3")
            .arg(mXLabel, mYLabel, mLabels.join('\n')));
        update();
    }
protected:
    /** @brief Plot supplied finite curve values, never reading an SDK model while painting. */
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.fillRect(rect(), palette().base());
        const int legendRows = visibleLegendRows();
        const QRectF plot(58., 15. + legendRows * 18., qMax(10, width() - 78),
                          qMax(10, height() - 70 - legendRows * 18));
        p.setPen(palette().mid().color());
        p.drawRect(plot);
        if (mCurves.isEmpty()) {
            p.setPen(palette().text().color());
            p.drawText(plot.adjusted(10, 10, -10, -10), Qt::AlignCenter | Qt::TextWordWrap,
                       tr("Fetch NOAA data or generate a storm to evaluate its curves."));
            return;
        }
        double minX = std::numeric_limits<double>::infinity(), maxX = 0., maxY = 0.;
        for (const auto &curve : mCurves)
            for (const auto &point : curve)
                if (std::isfinite(point.x()) && std::isfinite(point.y()) &&
                    (!mLogX || point.x() > 0.)) {
                    const double x = mLogX ? std::log10(point.x()) : point.x();
                    minX = qMin(minX, x);
                    maxX = qMax(maxX, x);
                    maxY = qMax(maxY, point.y());
                }
        if (!std::isfinite(minX))
            return;
        if (maxX <= minX)
            maxX = minX + 1.;
        maxY = maxY > 0. ? maxY * 1.06 : 1.;
        const int tickCount = plot.height() < 45. ? 1 : plot.height() < 80. ? 2 : 4;
        for (int tick = 0; tick <= tickCount; ++tick) {
            const double fraction = double(tick) / tickCount;
            const double y = plot.bottom() - fraction * plot.height();
            p.setPen(palette().midlight().color());
            p.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
            p.setPen(palette().text().color());
            p.drawText(QRectF(0, y - 8., plot.left() - 7., 16.), Qt::AlignRight | Qt::AlignVCenter,
                       number(fraction * maxY, 3));
            const double x = minX + fraction * (maxX - minX);
            const qreal labelLeft = tick == 0 ? plot.left()
                                    : tick == tickCount ? plot.right() - 80.
                                                : plot.left() + fraction * plot.width() - 40.;
            const auto alignment = tick == 0 ? Qt::AlignLeft
                                 : tick == tickCount ? Qt::AlignRight : Qt::AlignHCenter;
            p.drawText(QRectF(labelLeft, plot.bottom() + 4, 80., 18.),
                       alignment, number(mLogX ? std::pow(10., x) : x, 4));
        }
        const auto color = [this](int index) {
            const int hue = (palette().highlight().color().hsvHue() + index * 67 + 360) % 360;
            return QColor::fromHsv(hue, 190, palette().base().color().lightness() < 128 ? 235 : 165);
        };
        p.save();
        p.setClipRect(plot.adjusted(-1, -1, 1, 1));
        for (qsizetype index = 0; index < mCurves.size(); ++index) {
            if (!mVisibility.value(mLabels.value(index), true))
                continue;
            QPainterPath path;
            bool first = true;
            for (const auto &point : mCurves[index]) {
                if (!std::isfinite(point.x()) || !std::isfinite(point.y()) ||
                    (mLogX && point.x() <= 0.))
                    continue;
                const double x = mLogX ? std::log10(point.x()) : point.x();
                const QPointF target(plot.left() + (x - minX) / (maxX - minX) * plot.width(),
                                     plot.bottom() - point.y() / maxY * plot.height());
                if (first)
                    path.moveTo(target);
                else
                    path.lineTo(target);
                first = false;
            }
            p.setPen(QPen(color(index), 2.));
            p.drawPath(path);
        }
        p.restore();
        for (qsizetype index = 0; index < qMin(qsizetype(legendRows * 2), mLabels.size()); ++index) {
            const double x = plot.left() + (index % 2) * plot.width() / 2.;
            const double y = 12. + (index / 2) * 18.;
            const bool visible = mVisibility.value(mLabels[index], true);
            p.setPen(QPen(visible ? color(index) : palette().mid().color(), 2.));
            p.drawLine(QPointF(x, y), QPointF(x + 18., y));
            p.setPen(visible ? palette().text().color() : palette().mid().color());
            p.drawText(QRectF(x + 24., y - 8., plot.width() / 2. - 25., 17.),
                       Qt::AlignLeft | Qt::AlignVCenter,
                       fontMetrics().elidedText(
                           (visible ? QStringLiteral("☑ ") : QStringLiteral("☐ ")) + mLabels[index],
                           Qt::ElideRight, int(plot.width() / 2. - 25.)));
        }
        p.setPen(palette().text().color());
        p.drawText(QRectF(plot.left(), height() - 22., plot.width(), 18.), Qt::AlignCenter, mXLabel);
        if (plot.height() >= 80.) {
            p.save();
            p.translate(13., plot.center().y());
            p.rotate(-90.);
            p.drawText(QRectF(-plot.height() / 2., -10., plot.height(), 20.), Qt::AlignCenter,
                       fontMetrics().elidedText(mYLabel, Qt::ElideRight, int(plot.height())));
            p.restore();
        }
    }
    /** @brief Toggle one curve from its legend without changing the selected model candidate.
     * @param[in] event Ordinary widget pointer event. */
    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton) {
            const double plotWidth = qMax(10, width() - 78);
            for (qsizetype index = 0; index < qMin(qsizetype(visibleLegendRows() * 2), mLabels.size()); ++index) {
                const QRectF legend(58. + (index % 2) * plotWidth / 2.,
                                     4. + (index / 2) * 18., plotWidth / 2., 17.);
                if (legend.contains(event->position())) {
                    const auto label = mLabels[index];
                    mVisibility[label] = !mVisibility.value(label, true);
                    update();
                    event->accept();
                    return;
                }
            }
        }
        QWidget::mousePressEvent(event);
    }
private:
    /** @brief Reserve useful plotting/axis space when the splitter makes this pane short.
     * @details Hidden legend rows retain their visibility state and full captions in
     * the tooltip; expanding the chart restores their ordinary pointer targets.
     * @return Number of complete two-column legend rows that fit above the plot. */
    int visibleLegendRows() const
    {
        return qMin(qMin(5, (int(mLabels.size()) + 1) / 2), qMax(0, (height() - 110) / 18));
    }
    QStringList mLabels; ///< At most eight visible series captions.
    QVector<QVector<QPointF>> mCurves; ///< Copied finite data only.
    QString mXLabel; ///< Horizontal units and quantity.
    QString mYLabel; ///< Vertical units and quantity.
    bool mLogX = false; ///< Frequency-duration plots use a log duration axis.
    QHash<QString, bool> mVisibility; ///< Bounded legend visibility of the current curves.
};

StormWorkspace::StormWorkspace(Sdk::ExtensionHost *host, QObject *parent)
    : QObject(parent), mHost(host), mLifetime(host ? host->lifetime() : nullptr),
      mClient(new NoaaClient(host, this)), mProgress(host, this)
{
    connect(mClient, &NoaaClient::frequencyReady, this, [this](const FrequencyTable &table) {
        const QPointer<StormWorkspace> guard(this);
        QString error;
        if (!acceptFrequency(table, &error)) {
            finish(Sdk::ProgressOutcome::Failed, error);
            if (guard)
                emit message(error);
            return;
        }
        if (!guard)
            return;
        const QString detail = tr("NOAA frequency data received in memory (%1 durations, %2 frequencies).")
                                   .arg(table.durationsMinutes.size()).arg(table.returnPeriods.size());
        finish(Sdk::ProgressOutcome::Succeeded, detail);
        if (guard)
            emit message(detail);
    });
    connect(mClient, &NoaaClient::temporalReady, this, [this](const QList<CumulativeCurve> &curves) {
        const QPointer<StormWorkspace> guard(this);
        mCurves = curves;
        ++mRevision;
        emit curvesChanged();
        if (!guard)
            return;
        const QString detail = tr("Received %1 temporal patterns in memory.").arg(curves.size());
        finish(Sdk::ProgressOutcome::Succeeded, detail);
        if (guard)
            emit message(detail);
    });
    connect(mClient, &NoaaClient::failed, this, [this](const QString &error) {
        const QPointer<StormWorkspace> guard(this);
        finish(Sdk::ProgressOutcome::Failed, error);
        if (guard)
            emit message(error);
    });
    connect(mClient, &NoaaClient::cancelled, this, [this] {
        const QPointer<StormWorkspace> guard(this);
        finish(Sdk::ProgressOutcome::Cancelled, tr("NOAA request cancelled."));
        if (guard)
            emit message(tr("NOAA request cancelled; any preceding result keeps its original identity."));
    });
    connect(mClient, &NoaaClient::busyChanged, this, [this] { emit busyChanged(); });
    connect(mClient, &NoaaClient::progress, this, [this](const QString &stage) {
        const QPointer<StormWorkspace> guard(this);
        mProgress.update(-1., stage);
        if (guard)
            emit message(stage);
    });
    if (mLifetime)
        connect(mLifetime, &QObject::destroyed, this, [this] {
            mHost = nullptr;
            clear();
        });
}
StormWorkspace::~StormWorkspace()
{
    cancel();
    clearPointPreview();
}
bool StormWorkspace::isBusy() const { return mClient && mClient->isBusy(); }
void StormWorkspace::begin(const QString &toolId, const QString &title)
{
    Sdk::ToolRunRequest request;
    request.toolId = toolId;
    request.title = title;
    request.inputs = tr("Live NOAA request; response retained in session memory only.");
    mProgress.configureToolRun(request, [guard = QPointer<StormWorkspace>(this)] {
        if (guard)
            guard->cancel();
    });
    mOperation = true;
    mProgress.begin(title);
}
void StormWorkspace::finish(Sdk::ProgressOutcome outcome, const QString &detail)
{
    if (!mOperation)
        return;
    mOperation = false;
    const QPointer<StormWorkspace> guard(this);
    mProgress.log(outcome == Sdk::ProgressOutcome::Failed ? "error" : "info", detail);
    if (guard)
        mProgress.finish(outcome, detail);
}
bool StormWorkspace::fetch(const FrequencyQuery &query, QString *error)
{
    const QPointer<StormWorkspace> guard(this);
    cancel();
    if (!guard)
        return false;
    if (!mClient->fetchFrequency(query, error))
        return false;
    if (!guard)
        return false;
    begin("precipitation", tr("Retrieve NOAA precipitation frequency"));
    return true;
}
bool StormWorkspace::fetchTemporal(const QString &region, const QString &area, int durationMinutes,
                                  QString *error)
{
    const QPointer<StormWorkspace> guard(this);
    cancel();
    if (!guard)
        return false;
    if (!mClient->fetchTemporal(region, area, durationMinutes, error))
        return false;
    if (!guard)
        return false;
    begin("design-storm", tr("Retrieve NOAA temporal curves"));
    return true;
}
bool StormWorkspace::acceptFrequency(const FrequencyTable &table, QString *error)
{
    if (table.returnPeriods.isEmpty() || table.returnPeriods.size() > 64 ||
        table.durationsMinutes.isEmpty() || table.durationsMinutes.size() > 256) {
        if (error)
            *error = tr("The NOAA response has no usable frequency axes.");
        return false;
    }
    for (const auto *axis : {&table.durationsMinutes, &table.returnPeriods}) {
        double previous = 0.;
        for (double value : *axis) {
            if (!std::isfinite(value) || value <= previous) {
                if (error)
                    *error = tr("The NOAA response axes must be finite, positive and increasing.");
                return false;
            }
            previous = value;
        }
    }
    for (const auto *depths : {&table.depthsMm, &table.lowerMm, &table.upperMm}) {
        if (depths->size() != table.durationsMinutes.size()) {
            if (error)
                *error = tr("The NOAA estimate and uncertainty matrices are incomplete.");
            return false;
        }
        for (const auto &row : *depths)
            if (row.size() != table.returnPeriods.size() ||
                std::any_of(row.cbegin(), row.cend(), [](double value) { return !std::isfinite(value) || value < 0.; })) {
                if (error)
                    *error = tr("The NOAA response contains invalid precipitation values.");
                return false;
            }
    }
    double depth = 0.;
    if (!sampleFrequencyDepth(table, table.durationsMinutes.first(), 0,
                              FrequencyBound::Estimate, &depth, error))
        return false;
    mFrequency = table;
    mCurves.clear();
    ++mRevision;
    const QPointer<StormWorkspace> guard(this);
    showPointPreview();
    if (!guard)
        return true;
    emit frequencyChanged();
    if (guard)
        emit curvesChanged();
    return true;
}
void StormWorkspace::cancel()
{
    const QPointer<StormWorkspace> guard(this);
    if (mClient)
        mClient->cancel();
    if (guard && mOperation)
        finish(Sdk::ProgressOutcome::Cancelled, tr("NOAA request cancelled."));
}
void StormWorkspace::clear()
{
    const QPointer<StormWorkspace> guard(this);
    cancel();
    if (!guard)
        return;
    clearPointPreview();
    if (!guard)
        return;
    mFrequency = {};
    mCurves.clear();
    ++mRevision;
    emit frequencyChanged();
    if (!guard)
        return;
    emit curvesChanged();
    if (guard)
        emit message(tr("NOAA responses and evaluation curves cleared from memory."));
}
void StormWorkspace::clearPointPreview()
{
    const auto session = std::exchange(mPreviewSession, QUuid{});
    if (mLifetime && !session.isNull())
        if (auto *previews = qobject_cast<Sdk::IToolPreviews *>(mLifetime.data()))
            previews->clearToolPreview(session, "precipitation");
}
void StormWorkspace::showPointPreview()
{
    const QPointer<StormWorkspace> guard(this);
    clearPointPreview();
    if (!guard || !mLifetime || !mHost || mFrequency.durationsMinutes.isEmpty())
        return;
    auto *sessions = qobject_cast<Sdk::ISessionEvents *>(mLifetime.data());
    auto *previews = qobject_cast<Sdk::IToolPreviews *>(mLifetime.data());
    auto *instance = mHost->activeInstance();
    if (!sessions || !previews || !instance)
        return;
    const auto snapshot = sessions->stateSnapshot();
    for (const auto &state : snapshot.sessions)
        if (state.status == Sdk::SessionStatus::Ready && sessions->resolveSession(state.sessionId) == instance) {
            const QJsonObject feature{{"type", "Feature"}, {"id", "noaa-query"},
                {"properties", QJsonObject{{"source", "NOAA Atlas 14"},
                    {"latitude", mFrequency.query.latitude}, {"longitude", mFrequency.query.longitude}}},
                {"geometry", QJsonObject{{"type", "Point"},
                    {"coordinates", QJsonArray{mFrequency.query.longitude, mFrequency.query.latitude}}}}};
            const QJsonObject collection{{"type", "FeatureCollection"}, {"features", QJsonArray{feature}},
                {"previewStyle", QJsonObject{{"asFeatureLayer", true}, {"title", tr("NOAA query point")}}}};
            // Reserve cleanup identity before native publication can reenter the plugin.
            mPreviewSession = state.sessionId;
            QString error;
            if (!previews->showToolPreview(state.sessionId, state.loadGeneration, "precipitation", collection, &error) && guard)
                mPreviewSession = {};
            return;
        }
}

StormPanel::StormPanel(Sdk::ExtensionHost *host, Role role, StormWorkspace *workspace, QWidget *parent)
    : QWidget(parent), mHost(host), mLifetime(host ? host->lifetime() : nullptr),
      mWorkspace(workspace), mRole(role)
{
    setObjectName(role == Role::Frequency ? "noaaFrequencyPanel" : "designStormPanel");
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    // The scroll area's bounded minimum belongs to the viewport, not to its form.
    // Only the inner form follows disclosure changes, so they change the scroll
    // range rather than forcing neighboring docks to surrender their space.
    auto *splitter = named(new QSplitter(Qt::Vertical, this), "stormLayoutSplitter");
    splitter->setChildrenCollapsible(false);
    splitter->setHandleWidth(7);
    splitter->setAccessibleName(tr("Inputs and precipitation chart divider"));
    auto *scroll = named(new QScrollArea(splitter), "stormControlsScroll");
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setSizeAdjustPolicy(QAbstractScrollArea::AdjustIgnored);
    scroll->setMinimumSize(0, 48);
    auto *inputPane = named(new QWidget, "stormControls");
    auto *inputs = new QVBoxLayout(inputPane);
    inputs->setContentsMargins(0, 0, 4, 0);
    inputs->setSizeConstraint(QLayout::SetMinAndMaxSize);
    scroll->setWidget(inputPane);
    auto *chartPane = named(new QWidget(splitter), "stormChartPane");
    auto *charts = new QVBoxLayout(chartPane);
    charts->setContentsMargins(0, 0, 0, 0);
    buildSource(inputs);
    if (role == Role::Frequency)
        buildFrequency(inputs, charts);
    else
        buildDesign(inputs, charts);
    mStatus = named(new QLabel(this), "stormStatus");
    mStatus->setTextFormat(Qt::PlainText);
    mStatus->setWordWrap(true);
    mStatus->setText(tr("Fetch NOAA data to evaluate curves. Responses remain in memory."));
    inputs->addWidget(mStatus);
    inputs->addStretch();
    // Long generated candidate names must not widen the dock. Their popups still
    // expose the complete choices; forms wrap label/field rows on narrow docks.
    for (auto *combo : findChildren<QComboBox *>()) {
        combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        combo->setMinimumContentsLength(8);
    }
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({260, 280});
    splitter->handle(1)->setToolTip(tr("Drag to resize the inputs and chart sections."));
    layout->addWidget(splitter);
    if (workspace) {
        connect(workspace, &StormWorkspace::frequencyChanged, this, &StormPanel::refreshFrequency);
        connect(workspace, &StormWorkspace::curvesChanged, this, &StormPanel::refreshCurves);
        connect(workspace, &StormWorkspace::busyChanged, this, &StormPanel::controls);
        connect(workspace, &StormWorkspace::message, mStatus, &QLabel::setText);
    }
    if (mLifetime && host) {
        host->observe(this, [this](const QString &event, const QVariantMap &) {
            if (event == "instanceActivated" || event == "instanceClosed" ||
                event == "instanceOpened" || event == "modelEdited" || event == "attributesChanged")
                refreshTarget();
        });
        if (auto *states = qobject_cast<Sdk::ISessionEvents *>(mLifetime.data()))
            states->observeState(this, {}, [this](const Sdk::StateEvent &event) {
                if (event.kind != Sdk::StateEventKind::StateChanged ||
                    event.changes.testFlag(Sdk::StateChange::Model))
                    refreshTarget();
            });
    }
    refreshFrequency();
    refreshCurves();
    refreshTarget();
    controls();
}
void StormPanel::buildSource(QVBoxLayout *layout)
{
    auto *sourceControls = new QWidget(this);
    auto *sourceLayout = new QVBoxLayout(sourceControls);
    sourceLayout->setContentsMargins(0, 0, 0, 0);
    auto *form = new QFormLayout;
    form->setRowWrapPolicy(QFormLayout::WrapLongRows);
    auto *location = new QWidget(this);
    auto *row = new QHBoxLayout(location);
    row->setContentsMargins(0, 0, 0, 0);
    mLatitude = named(new QDoubleSpinBox(location), "stormLatitude");
    mLongitude = named(new QDoubleSpinBox(location), "stormLongitude");
    mLatitude->setRange(-90., 90.);
    mLongitude->setRange(-180., 180.);
    mLatitude->setDecimals(6);
    mLongitude->setDecimals(6);
    mLatitude->setValue(38.9);
    mLongitude->setValue(-77.);
    mLatitude->setAccessibleName(tr("Latitude in decimal degrees"));
    mLongitude->setAccessibleName(tr("Longitude in decimal degrees"));
    row->addWidget(mLatitude);
    row->addWidget(mLongitude);
    form->addRow(tr("Latitude / longitude"), location);
    mLocationGage = named(new QComboBox(this), "stormLocationGage");
    mLocationGage->addItem(tr("Use a model gage location…"), QVariant{});
    form->addRow(tr("Model location"), mLocationGage);
    auto *options = new QWidget(this);
    auto *optionRow = new QHBoxLayout(options);
    optionRow->setContentsMargins(0, 0, 0, 0);
    mStatisticalSeries = named(new QComboBox(options), "stormStatisticalSeries");
    mStatisticalSeries->addItem(tr("Partial duration series (PDS)"), "pds");
    mStatisticalSeries->addItem(tr("Annual maximum series (AMS)"), "ams");
    mUnits = named(new QComboBox(options), "stormDisplayUnits");
    mUnits->addItem(tr("mm"), "metric");
    mUnits->addItem(tr("in"), "english");
    optionRow->addWidget(mStatisticalSeries, 1);
    optionRow->addWidget(mUnits);
    form->addRow(tr("Source / display units"), options);
    sourceLayout->addLayout(form);
    auto *buttons = new QHBoxLayout;
    mFetch = named(new QPushButton(tr("Fetch / Refresh NOAA"), this), "stormFetch");
    auto *clearButton = named(new QPushButton(tr("Clear"), this), "stormClear");
    mCancel = named(new QPushButton(tr("Cancel"), this), "stormCancel");
    buttons->addWidget(mFetch);
    buttons->addWidget(clearButton);
    buttons->addWidget(mCancel);
    sourceLayout->addLayout(buttons);
    if (mRole == Role::Design) {
        auto *sourceToggle = named(new QToolButton(this), "stormShowSource");
        sourceToggle->setText(tr("NOAA query options"));
        sourceToggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        sourceToggle->setArrowType(Qt::RightArrow);
        sourceToggle->setCheckable(true);
        sourceControls->hide();
        connect(sourceToggle, &QToolButton::toggled, sourceControls, &QWidget::setVisible);
        connect(sourceToggle, &QToolButton::toggled, sourceToggle, [sourceToggle](bool shown) {
            sourceToggle->setArrowType(shown ? Qt::DownArrow : Qt::RightArrow);
        });
        layout->addWidget(sourceToggle);
    }
    layout->addWidget(sourceControls);
    mSourceCaption = named(new QLabel(this), "stormSourceCaption");
    mSourceCaption->setWordWrap(true);
    mSourceCaption->setTextFormat(Qt::PlainText);
    layout->addWidget(mSourceCaption);
    connect(mFetch, &QPushButton::clicked, this, [this] {
        const QPointer<StormPanel> guard(this);
        if (!mWorkspace)
            return;
        inputsChanged();
        QString error;
        const bool accepted = mWorkspace->fetch(query(), &error);
        if (!guard)
            return;
        if (!accepted)
            mStatus->setText(error);
        controls();
    });
    connect(clearButton, &QPushButton::clicked, this, &StormPanel::clear);
    connect(mCancel, &QPushButton::clicked, this, &StormPanel::cancel);
    connect(mLatitude, qOverload<double>(&QDoubleSpinBox::valueChanged), this, &StormPanel::inputsChanged);
    connect(mLongitude, qOverload<double>(&QDoubleSpinBox::valueChanged), this, &StormPanel::inputsChanged);
    connect(mStatisticalSeries, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &StormPanel::inputsChanged);
    connect(mUnits, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] {
        const double scale = mUnits->currentData().toString() == "english" ? 25.4 : 1.;
        if (mDepth) {
            const QSignalBlocker blocker(mDepth);
            mDepth->setValue(mDepth->value() * mDisplayDepthToMm / scale);
        }
        mDisplayDepthToMm = scale;
        inputsChanged();
        refreshChart();
    });
    connect(mLocationGage, qOverload<int>(&QComboBox::activated), this, [this](int index) {
        const auto value = mLocationGage->itemData(index).toPointF();
        if (index > 0) {
            mLatitude->setValue(value.y());
            mLongitude->setValue(value.x());
        }
    });
}
void StormPanel::buildFrequency(QVBoxLayout *controls, QVBoxLayout *charts)
{
    mBasis = named(new QComboBox(this), "stormChartBasis");
    mBasis->addItem(tr("Depth-duration-frequency"), "depth");
    mBasis->addItem(tr("Intensity-duration-frequency"), "intensity");
    charts->addWidget(mBasis);
    mBound = named(new QComboBox(this), "stormFrequencyBound");
    mBound->addItem(tr("Estimate"), int(FrequencyBound::Estimate));
    mBound->addItem(tr("Lower bound"), int(FrequencyBound::Lower));
    mBound->addItem(tr("Upper bound"), int(FrequencyBound::Upper));
    auto *boundForm = new QFormLayout;
    boundForm->setRowWrapPolicy(QFormLayout::WrapLongRows);
    boundForm->addRow(tr("Depth bound"), mBound);
    controls->addLayout(boundForm);
    auto *design = new QPushButton(tr("Evaluate design storms…"), this);
    controls->addWidget(design);
    connect(design, &QPushButton::clicked, this, [this] {
        const QPointer<StormPanel> guard(this);
        if (mLifetime)
            if (auto *tools = qobject_cast<Sdk::IToolbox *>(mLifetime.data())) {
                QString error;
                const bool opened = tools->openTool("design-storm", &error);
                if (guard && !opened)
                    mStatus->setText(error);
            }
    });
    auto *tabs = named(new QTabWidget(this), "stormChartTabs");
    mChart = named(new StormChart(tabs), "stormChart");
    tabs->addTab(mChart, tr("Curves"));
    mTable = named(new QTableView(tabs), "stormFrequencyTable");
    mTableModel = new QStandardItemModel(mTable);
    mTable->setModel(mTableModel);
    mTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    mTable->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    tabs->addTab(mTable, tr("Values"));
    charts->addWidget(tabs, 1);
    connect(mBasis, qOverload<int>(&QComboBox::currentIndexChanged), this, &StormPanel::refreshChart);
    connect(mBound, qOverload<int>(&QComboBox::currentIndexChanged), this, &StormPanel::refreshChart);
}
void StormPanel::buildDesign(QVBoxLayout *layout, QVBoxLayout *charts)
{
    auto *form = new QFormLayout;
    form->setRowWrapPolicy(QFormLayout::WrapLongRows);
    mDepthSource = named(new QComboBox(this), "stormDepthSource");
    mDepthSource->addItem(tr("Live NOAA estimate"), "noaa");
    mDepthSource->addItem(tr("Manual total depth"), "manual");
    form->addRow(tr("Total-depth source"), mDepthSource);
    auto *estimate = new QWidget(this);
    auto *estimateRow = new QHBoxLayout(estimate);
    estimateRow->setContentsMargins(0, 0, 0, 0);
    mFrequency = named(new QComboBox(estimate), "stormFrequency");
    mBound = named(new QComboBox(estimate), "stormFrequencyBound");
    mBound->addItem(tr("Estimate"), int(FrequencyBound::Estimate));
    mBound->addItem(tr("Lower bound"), int(FrequencyBound::Lower));
    mBound->addItem(tr("Upper bound"), int(FrequencyBound::Upper));
    estimateRow->addWidget(mFrequency, 1);
    estimateRow->addWidget(mBound);
    form->addRow(tr("Frequency / depth bound"), estimate);
    mDepth = named(new QDoubleSpinBox(this), "stormTotalDepth");
    mDepth->setDecimals(4);
    mDepth->setRange(.0001, 100000.);
    mDepth->setValue(25.4);
    form->addRow(tr("Manual depth (display units)"), mDepth);
    mMethod = named(new QComboBox(this), "stormMethod");
    const auto methods = stormMethods();
    for (const auto &method : methods) {
        mMethod->addItem(method.label, method.id);
        mMethod->setItemData(mMethod->count() - 1, method.description + '\n' + method.source, Qt::ToolTipRole);
    }
    mMethod->setPlaceholderText(tr("Choose a distribution…"));
    mMethod->setCurrentIndex(-1);
    form->addRow(tr("Distribution"), mMethod);
    auto *duration = new QWidget(this);
    auto *durationRow = new QHBoxLayout(duration);
    durationRow->setContentsMargins(0, 0, 0, 0);
    mDuration = named(new QSpinBox(duration), "stormDurationMinutes");
    mDuration->setRange(1, 86400);
    mDuration->setValue(1440);
    mDuration->setSuffix(tr(" min"));
    mInterval = named(new QSpinBox(duration), "stormIntervalMinutes");
    mInterval->setRange(1, 1440);
    mInterval->setValue(5);
    mInterval->setSuffix(tr(" min"));
    durationRow->addWidget(mDuration);
    durationRow->addWidget(new QLabel(tr("step"), duration));
    durationRow->addWidget(mInterval);
    form->addRow(tr("Duration / interval"), duration);
    mPeak = named(new QDoubleSpinBox(this), "stormPeakRatio");
    mPeak->setRange(.01, .99);
    mPeak->setDecimals(2);
    mPeak->setValue(.5);
    form->addRow(tr("Relative peak time"), mPeak);
    mStart = named(new QDateTimeEdit(this), "stormStart");
    mStart->setTimeZone(QTimeZone::UTC);
    mStart->setDisplayFormat("yyyy-MM-dd HH:mm");
    mStart->setCalendarPopup(true);
    mStart->setDateTime(QDateTime(QDate::currentDate(), QTime(0, 0), QTimeZone::UTC));
    form->addRow(tr("Model start (wall clock)"), mStart);
    layout->addLayout(form);
    mTemporalControls = new QWidget(this);
    auto *temporalForm = new QFormLayout(mTemporalControls);
    temporalForm->setContentsMargins(0, 0, 0, 0);
    temporalForm->setRowWrapPolicy(QFormLayout::WrapLongRows);
    auto *temporalSelector = new QWidget(this);
    auto *temporalRow = new QHBoxLayout(temporalSelector);
    temporalRow->setContentsMargins(0, 0, 0, 0);
    mTemporalRegion = named(new QComboBox(temporalSelector), "stormTemporalRegion");
    for (const auto &region : QStringList{"orb", "ne", "mw", "se", "sw", "tx", "inw", "sa", "ak", "pr", "hi", "pi"})
        mTemporalRegion->addItem(region.toUpper(), region);
    mTemporalArea = named(new QLineEdit("general", temporalSelector), "stormTemporalArea");
    mTemporalArea->setMaximumWidth(90);
    auto *fetchTemporal = named(new QPushButton(tr("Fetch patterns"), temporalSelector), "stormFetchPatterns");
    temporalRow->addWidget(mTemporalRegion);
    temporalRow->addWidget(mTemporalArea);
    temporalRow->addWidget(fetchTemporal);
    temporalForm->addRow(tr("NOAA region / area"), temporalSelector);
    mCurve = named(new QComboBox(this), "stormTemporalCurve");
    temporalForm->addRow(tr("Temporal pattern"), mCurve);
    auto *temporalNote = new QLabel(tr("Choose the area from <a href=\"https://hdsc.nws.noaa.gov/pfds/pfds_temporal.html\">NOAA's temporal-area map</a>. A region alone does not identify its pattern area."), this);
    temporalNote->setWordWrap(true);
    temporalNote->setOpenExternalLinks(true);
    temporalForm->addRow(temporalNote);
    layout->addWidget(mTemporalControls);
    mCustomCurve = named(new QPlainTextEdit(this), "stormCustomCurve");
    mCustomCurve->setPlaceholderText(tr("Normalized time, cumulative depth — one pair per line\n0,0\n0.5,0.5\n1,1"));
    mCustomCurve->setMaximumHeight(100);
    layout->addWidget(mCustomCurve);
    auto *generation = new QHBoxLayout;
    mGenerate = named(new QPushButton(tr("Generate / compare"), this), "stormGenerate");
    mCandidate = named(new QComboBox(this), "stormCandidate");
    generation->addWidget(mGenerate);
    generation->addWidget(mCandidate, 1);
    layout->addLayout(generation);
    mBasis = named(new QComboBox(this), "stormChartBasis");
    mBasis->addItem(tr("Rainfall intensity"), "intensity");
    mBasis->addItem(tr("Cumulative depth"), "cumulative");
    mBasis->addItem(tr("Normalized cumulative pattern"), "normalized");
    charts->addWidget(mBasis);
    auto *previewTabs = named(new QTabWidget(this), "stormChartTabs");
    mChart = named(new StormChart(previewTabs), "stormChart");
    previewTabs->addTab(mChart, tr("Curves"));
    mTable = named(new QTableView(previewTabs), "stormSampleTable");
    mTableModel = new QStandardItemModel(mTable);
    mTable->setModel(mTableModel);
    mTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    mTable->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    previewTabs->addTab(mTable, tr("Selected storm values"));
    charts->addWidget(previewTabs, 1);
    auto *targetToggle = named(new QToolButton(this), "stormShowTarget");
    targetToggle->setText(tr("Apply selected storm to model"));
    targetToggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    targetToggle->setArrowType(Qt::RightArrow);
    targetToggle->setCheckable(true);
    layout->addWidget(targetToggle);
    auto *targetBox = new QGroupBox(this);
    targetBox->hide();
    auto *targetForm = new QFormLayout(targetBox);
    targetForm->setRowWrapPolicy(QFormLayout::WrapLongRows);
    mTargetCaption = named(new QLabel(targetBox), "stormTargetCaption");
    mTargetCaption->setWordWrap(true);
    targetForm->addRow(mTargetCaption);
    mSeriesName = named(new QLineEdit("DesignStorm", targetBox), "stormSeriesName");
    mSeriesName->setMaxLength(31);
    targetForm->addRow(tr("New series ID"), mSeriesName);
    mGage = named(new QComboBox(targetBox), "stormTargetGage");
    mGage->addItem(tr("Create new gage"), "");
    targetForm->addRow(tr("Gage"), mGage);
    mGageName = named(new QLineEdit("DesignGage", targetBox), "stormGageName");
    mGageName->setMaxLength(31);
    targetForm->addRow(tr("New gage ID"), mGageName);
    mPlaceGage = named(new QCheckBox(tr("Place new gage at the query location"), targetBox), "stormPlaceGage");
    mPlaceGage->setToolTip(tr("Requires the model coordinate system. When unchecked, create an unlocated gage at (0,0)."));
    targetForm->addRow(mPlaceGage);
    mResetFactor = named(new QCheckBox(tr("Set rain-gage multiplier to 1"), targetBox), "stormResetGageMultiplier");
    mResetFactor->setChecked(true);
    targetForm->addRow(mResetFactor);
    auto *targetNote = new QLabel(tr("Apply sets the gage's series, recording interval and rainfall format. Clear the multiplier choice only when its existing scaling is intended."), targetBox);
    targetNote->setWordWrap(true);
    targetForm->addRow(targetNote);
    mApply = named(new QPushButton(tr("Apply to model…"), targetBox), "stormApply");
    targetForm->addRow(mApply);
    layout->addWidget(targetBox);
    connect(targetToggle, &QToolButton::toggled, targetBox, &QWidget::setVisible);
    connect(targetToggle, &QToolButton::toggled, targetToggle, [targetToggle](bool shown) {
        targetToggle->setArrowType(shown ? Qt::DownArrow : Qt::RightArrow);
    });
    connect(mGenerate, &QPushButton::clicked, this, &StormPanel::generate);
    connect(mApply, &QPushButton::clicked, this, &StormPanel::applyToModel);
    connect(mBasis, qOverload<int>(&QComboBox::currentIndexChanged), this, &StormPanel::refreshChart);
    connect(mCandidate, qOverload<int>(&QComboBox::activated), this, [this](int) {
        const int selected = mCandidate->currentIndex();
        if (selected >= 0 && selected < mCandidates.size()) {
            const auto &storm = mCandidates[selected];
            const auto &source = mProvenance[selected];
            mUpdating = true;
            mMethod->setCurrentIndex(mMethod->findData(storm.method));
            mDuration->setValue(int(storm.timesMinutes.last()));
            mInterval->setValue(storm.intervalMinutes);
            mStart->setDateTime(mStarts[selected]);
            mLatitude->setValue(source.value("latitude").toDouble());
            mLongitude->setValue(source.value("longitude").toDouble());
            const bool manual = source.value("manualDepth").toBool();
            mDepthSource->setCurrentIndex(manual ? 1 : 0);
            mDepth->setValue(storm.totalDepthMm / mDisplayDepthToMm);
            if (!manual) {
                const int frequency = mFrequency->findData(source.value("returnPeriod").toDouble());
                if (frequency >= 0)
                    mFrequency->setCurrentIndex(frequency);
            }
            mPeak->setValue(storm.parameters.value("peakRatio").toDouble(.5));
            mUpdating = false;
            mCandidateValid = true;
            mStatus->setText(tr("Selected %1, starting %2. Review the current model target before applying.")
                .arg(storm.label, mStarts[selected].toString("yyyy-MM-dd HH:mm")));
        } else
            mCandidateValid = false;
        refreshChart();
        controls();
    });
    for (auto *combo : {mDepthSource, mFrequency, mBound, mMethod, mCurve})
        connect(combo, qOverload<int>(&QComboBox::currentIndexChanged), this, &StormPanel::inputsChanged);
    connect(mDuration, qOverload<int>(&QSpinBox::valueChanged), this, &StormPanel::inputsChanged);
    connect(mInterval, qOverload<int>(&QSpinBox::valueChanged), this, &StormPanel::inputsChanged);
    connect(mDepth, qOverload<double>(&QDoubleSpinBox::valueChanged), this, &StormPanel::inputsChanged);
    connect(mPeak, qOverload<double>(&QDoubleSpinBox::valueChanged), this, &StormPanel::inputsChanged);
    connect(mStart, &QDateTimeEdit::dateTimeChanged, this, &StormPanel::inputsChanged);
    connect(mCustomCurve, &QPlainTextEdit::textChanged, this, &StormPanel::inputsChanged);
    connect(mGage, qOverload<int>(&QComboBox::currentIndexChanged), this, &StormPanel::controls);
    connect(mSeriesName, &QLineEdit::textChanged, this, &StormPanel::controls);
    connect(mGageName, &QLineEdit::textChanged, this, &StormPanel::controls);
    connect(fetchTemporal, &QPushButton::clicked, this, [this] {
        const QPointer<StormPanel> guard(this);
        inputsChanged();
        QString error;
        const bool accepted = mWorkspace && mWorkspace->fetchTemporal(mTemporalRegion->currentData().toString(),
                mTemporalArea->text().trimmed(), mDuration->value(), &error);
        if (guard && !accepted)
            mStatus->setText(error);
    });
    connect(mTemporalRegion, qOverload<int>(&QComboBox::currentIndexChanged), this, &StormPanel::refreshCurves);
    connect(mTemporalArea, &QLineEdit::textChanged, this, &StormPanel::refreshCurves);
    connect(mDuration, qOverload<int>(&QSpinBox::valueChanged), this, &StormPanel::refreshCurves);
}
FrequencyQuery StormPanel::query() const
{
    FrequencyQuery result;
    result.latitude = mLatitude->value();
    result.longitude = mLongitude->value();
    result.units = mUnits->currentData().toString();
    result.series = mStatisticalSeries->currentData().toString();
    result.data = "depth";
    return result;
}
void StormPanel::refreshFrequency()
{
    if (!mWorkspace)
        return;
    mUpdating = true;
    const auto &table = mWorkspace->frequency();
    const bool available = !table.durationsMinutes.isEmpty();
    mCandidates.clear();
    mStarts.clear();
    mProvenance.clear();
    mCandidateValid = false;
    if (mCandidate)
        mCandidate->clear();
    if (available) {
        mLatitude->setValue(table.query.latitude);
        mLongitude->setValue(table.query.longitude);
        mStatisticalSeries->setCurrentIndex(mStatisticalSeries->findData(table.query.series));
        mSourceCaption->setText(tr("Accepted point %1, %2 · %3 · %4 · retrieved %5")
            .arg(number(table.query.latitude, 8), number(table.query.longitude, 8),
                 table.query.series.toUpper(),
                 tr("Atlas 14 volume %1, version %2").arg(table.metadata.value("volume").toString(),
                                                        table.metadata.value("version").toString()),
                 table.retrievedAt.toLocalTime().toString(Qt::ISODate)));
        if (mFrequency) {
            mFrequency->clear();
            for (double frequency : table.returnPeriods)
                mFrequency->addItem(tr("%1 year").arg(number(frequency)), frequency);
        }
        if (mTemporalRegion) {
            QString region = table.metadata.value("reg").toString();
            if (table.metadata.value("volume").toString() == "1")
                region = "sa";
            const int index = mTemporalRegion->findData(region);
            if (index >= 0)
                mTemporalRegion->setCurrentIndex(index);
            const bool numbered = QStringList{"sw", "ne", "mw", "se", "tx", "inw", "ak"}.contains(region);
            mTemporalArea->setText(numbered ? "1" : "general");
        }
    } else {
        mSourceCaption->setText(tr("No NOAA response retained. Fetch a live estimate or choose manual depth."));
        if (mFrequency)
            mFrequency->clear();
    }
    mUpdating = false;
    refreshChart();
    controls();
}
void StormPanel::refreshCurves()
{
    if (!mCurve || !mWorkspace)
        return;
    const QSignalBlocker blocker(mCurve);
    mCurve->clear();
    const QString prefix = mTemporalRegion->currentData().toString() + '/' + mTemporalArea->text().trimmed() + '/';
    for (const auto &curve : mWorkspace->curves())
        if (curve.id.startsWith(prefix) && std::abs(curve.durationMinutes - mDuration->value()) < 1e-9)
            mCurve->addItem(curve.title, curve.id);
    inputsChanged();
}
void StormPanel::refreshChart()
{
    if (!mChart)
        return;
    const bool english = mUnits->currentData().toString() == "english";
    const double scale = english ? 1. / 25.4 : 1.;
    const QString unit = english ? tr("in") : tr("mm");
    QVector<QVector<QPointF>> curves;
    QStringList labels;
    if (mRole == Role::Frequency) {
        if (!mWorkspace)
            return;
        const auto &table = mWorkspace->frequency();
        const bool intensity = mBasis->currentData().toString() == "intensity";
        mTableModel->clear();
        QStringList headers{tr("Duration (min)")};
        for (double period : table.returnPeriods)
            headers.append(tr("%1 yr (%2)").arg(number(period), intensity ? unit + "/h" : unit));
        mTableModel->setHorizontalHeaderLabels(headers);
        const auto &depths = matrix(table, static_cast<FrequencyBound>(mBound->currentData().toInt()));
        for (qsizetype row = 0; row < table.durationsMinutes.size(); ++row) {
            QList<QStandardItem *> cells{new QStandardItem(number(table.durationsMinutes[row]))};
            for (qsizetype column = 0; column < table.returnPeriods.size(); ++column) {
                const double value = depths.value(row).value(column) * scale *
                                     (intensity ? 60. / table.durationsMinutes[row] : 1.);
                cells.append(new QStandardItem(number(value)));
            }
            mTableModel->appendRow(cells);
        }
        for (qsizetype column = 0; column < qMin(qsizetype(10), table.returnPeriods.size()); ++column) {
            QVector<QPointF> curve;
            for (qsizetype row = 0; row < table.durationsMinutes.size(); ++row)
                curve.append({table.durationsMinutes[row], depths.value(row).value(column) * scale *
                              (intensity ? 60. / table.durationsMinutes[row] : 1.)});
            labels.append(tr("%1 year").arg(number(table.returnPeriods[column])));
            curves.append(curve);
        }
        mChart->setCurves(labels, curves, tr("Duration (minutes, logarithmic)"),
                          intensity ? tr("Intensity (%1/h)").arg(unit) : tr("Depth (%1)").arg(unit), true);
    } else {
        const bool normalized = mBasis->currentData().toString() == "normalized";
        const bool cumulative = normalized || mBasis->currentData().toString() == "cumulative";
        for (qsizetype index = 0; index < mCandidates.size(); ++index) {
            const auto &storm = mCandidates[index];
            QVector<QPointF> curve;
            if (cumulative) {
                for (qsizetype i = 0; i < storm.timesMinutes.size(); ++i)
                    curve.append({storm.timesMinutes[i] / 60., normalized
                        ? 100. * storm.cumulativeMm[i] / storm.totalDepthMm : storm.cumulativeMm[i] * scale});
            } else {
                curve.append({0., 0.});
                for (qsizetype i = 0; i < storm.intensityMmPerHour.size(); ++i) {
                    curve.append({storm.timesMinutes[i] / 60., storm.intensityMmPerHour[i] * scale});
                    curve.append({storm.timesMinutes[i + 1] / 60., storm.intensityMmPerHour[i] * scale});
                }
                curve.append({storm.timesMinutes.last() / 60., 0.});
            }
            labels.append(tr("%1: %2").arg(index + 1).arg(storm.label));
            curves.append(curve);
        }
        mChart->setCurves(labels, curves, tr("Elapsed time (hours)"),
                          normalized ? tr("Cumulative depth (%)")
                          : cumulative ? tr("Depth (%1)").arg(unit) : tr("Intensity (%1/h)").arg(unit));
        mTableModel->clear();
        mTableModel->setHorizontalHeaderLabels({tr("Start (min)"), tr("Intensity (%1/h)").arg(unit),
                                                tr("Interval depth (%1)").arg(unit), tr("Cumulative (%1)").arg(unit)});
        const int selected = mCandidate->currentIndex();
        if (selected >= 0 && selected < mCandidates.size()) {
            const auto &storm = mCandidates[selected];
            for (qsizetype i = 0; i < storm.depthsMm.size(); ++i)
                mTableModel->appendRow({new QStandardItem(number(storm.timesMinutes[i])),
                    new QStandardItem(number(storm.intensityMmPerHour[i] * scale)),
                    new QStandardItem(number(storm.depthsMm[i] * scale)),
                    new QStandardItem(number(storm.cumulativeMm[i + 1] * scale))});
        }
    }
}
void StormPanel::inputsChanged()
{
    if (mUpdating)
        return;
    mCandidateValid = false;
    controls();
}
Sdk::IModelInstance *StormPanel::target(Sdk::ModelSessionState *state) const
{
    if (!mLifetime || !mHost)
        return nullptr;
    auto *sessions = qobject_cast<Sdk::ISessionEvents *>(mLifetime.data());
    if (!sessions)
        return nullptr;
    auto *instance = mHost->activeInstance();
    const auto snapshot = sessions->stateSnapshot();
    if (!instance && !mTargetState.sessionId.isNull())
        instance = sessions->resolveSession(mTargetState.sessionId);
    for (const auto &candidate : snapshot.sessions)
        if (candidate.status == Sdk::SessionStatus::Ready && instance &&
            sessions->resolveSession(candidate.sessionId) == instance) {
            if (state)
                *state = candidate;
            return instance;
        }
    return nullptr;
}
void StormPanel::refreshTarget()
{
    if (!mLifetime || !mHost)
        return;
    Sdk::ModelSessionState state;
    auto *instance = target(&state);
    auto *metadata = qobject_cast<Sdk::IModelRainfall *>(mLifetime.data());
    Sdk::RainfallModelInfo info;
    QString error;
    const bool valid = instance && metadata &&
                       metadata->modelRainfall(state.sessionId, state.loadGeneration, &info, &error) &&
                       (info.depthUnit == "mm" || info.depthUnit == "in");
    const bool changedTarget = state.sessionId != mTargetState.sessionId ||
                               state.loadGeneration != mTargetState.loadGeneration;
    const bool changedRevision = state.modelRevision != mTargetState.modelRevision;
    const QString oldGage = mGage ? mGage->currentData().toString() : QString{};
    mUpdating = true;
    mLocationGage->clear();
    mLocationGage->addItem(tr("Use a model gage location…"), QVariant{});
    if (mGage) {
        mGage->clear();
        mGage->addItem(tr("Create new gage"), "");
    }
    if (valid) {
        for (const auto &gage : info.gages) {
            if (mGage)
                mGage->addItem(gage.id, gage.id);
            if (gage.hasLonLat)
                mLocationGage->addItem(gage.id, gage.lonLat);
        }
        if (mGage && !changedTarget) {
            const int index = mGage->findData(oldGage);
            if (index >= 0)
                mGage->setCurrentIndex(index);
        }
        mTargetState = state;
        mRainUnit = info.depthUnit;
        if (mTargetCaption)
            mTargetCaption->setText(tr("%1 · engine rainfall unit: %2").arg(state.title, info.depthUnit));
        if (mStart && changedTarget && info.start.isValid()) {
            mStart->setDateTime(info.start);
            mCandidateValid = false;
        }
        if (mPlaceGage && changedTarget) {
            QPointF point;
            auto *project = instance->project();
            mPlaceGage->setChecked(project && project->fromLonLat({mLongitude->value(), mLatitude->value()}, &point));
        }
    } else {
        mTargetState = {};
        mRainUnit.clear();
        if (mTargetCaption)
            mTargetCaption->setText(error.isEmpty() ? tr("Open a ready model to apply a storm.") : error);
    }
    mUpdating = false;
    if (changedRevision && mStatus && !changedTarget)
    {
        mCandidateValid = false;
        mStatus->setText(tr("The model changed. Review the series/gage choices, then generate or select a candidate again before applying."));
    }
    controls();
}
void StormPanel::controls()
{
    const bool busy = mWorkspace && mWorkspace->isBusy();
    mFetch->setEnabled(mWorkspace && !busy && mLifetime);
    mCancel->setEnabled(busy);
    mLocationGage->setEnabled(mLocationGage->count() > 1);
    if (mRole != Role::Design)
        return;
    if (busy)
        mCandidateValid = false;
    const bool manual = mDepthSource->currentData().toString() == "manual";
    mDepth->setEnabled(manual);
    mFrequency->setEnabled(!manual);
    mBound->setEnabled(!manual);
    const QString method = mMethod->currentData().toString();
    const bool temporal = method == "noaa-temporal";
    mTemporalControls->setVisible(temporal);
    mCustomCurve->setVisible(method == "custom");
    mPeak->setEnabled(method == "triangular" || method == "alternating-block" || method == "chicago");
    mGageName->setEnabled(mGage->currentData().toString().isEmpty());
    mPlaceGage->setEnabled(mGage->currentData().toString().isEmpty());
    bool matchingSource = false;
    if (mWorkspace && !mWorkspace->frequency().durationsMinutes.isEmpty()) {
        const auto entered = query();
        const auto &accepted = mWorkspace->frequency().query;
        matchingSource = std::abs(entered.latitude - accepted.latitude) < 1e-7 &&
                         std::abs(entered.longitude - accepted.longitude) < 1e-7 &&
                         entered.series == accepted.series;
    }
    bool hasSource = manual || matchingSource;
    const auto methods = stormMethods();
    const auto descriptor = std::find_if(methods.cbegin(), methods.cend(),
                                         [&method](const auto &value) { return value.id == method; });
    if (descriptor != methods.cend()) {
        if (descriptor->requiresFrequencyTable)
        {
            hasSource = matchingSource;
            mFrequency->setEnabled(true);
            mBound->setEnabled(true);
        }
        if (descriptor->requires24Hours && mDuration->value() != 1440) {
            const QSignalBlocker blocker(mDuration);
            mDuration->setValue(1440);
        }
        mDuration->setEnabled(!descriptor->requires24Hours);
    }
    mGenerate->setEnabled(!busy && hasSource && descriptor != methods.cend());
    mApply->setEnabled(mCandidateValid && !busy && !mTargetState.sessionId.isNull() &&
                       !mRainUnit.isEmpty() && !mSeriesName->text().trimmed().isEmpty() &&
                       (!mGage->currentData().toString().isEmpty() || !mGageName->text().trimmed().isEmpty()));
}
void StormPanel::cancel()
{
    if (mWorkspace)
        mWorkspace->cancel();
}
void StormPanel::clear()
{
    const QPointer<StormPanel> guard(this);
    mCandidates.clear();
    mStarts.clear();
    mProvenance.clear();
    mCandidateValid = false;
    if (mCandidate)
        mCandidate->clear();
    if (mWorkspace)
        mWorkspace->clear();
    if (!guard)
        return;
    refreshChart();
    controls();
}
void StormPanel::generate()
{
    if (mRole != Role::Design)
        return;
    StormRequest request;
    request.method = mMethod->currentData().toString();
    request.durationMinutes = mDuration->value();
    request.intervalMinutes = mInterval->value();
    request.peakRatio = mPeak->value();
    request.frequencyIndex = mFrequency->currentIndex();
    request.bound = static_cast<FrequencyBound>(mBound->currentData().toInt());
    const bool manual = mDepthSource->currentData().toString() == "manual";
    const auto methods = stormMethods();
    const auto method = std::find_if(methods.cbegin(), methods.cend(),
        [&request](const auto &value) { return value.id == request.method; });
    const bool frequencyShape = method != methods.cend() && method->requiresFrequencyTable;
    QString error;
    if (mWorkspace)
        request.frequency = mWorkspace->frequency();
    if (!manual || frequencyShape) {
        const auto expected = query();
        const auto &accepted = request.frequency.query;
        if (request.frequency.durationsMinutes.isEmpty() ||
            std::abs(expected.latitude - accepted.latitude) > 1e-7 ||
            std::abs(expected.longitude - accepted.longitude) > 1e-7 || expected.series != accepted.series) {
            mStatus->setText(tr("Fetch NOAA data for the currently entered location and series first."));
            return;
        }
    }
    if (manual)
        request.depthMm = mDepth->value() * (mUnits->currentData().toString() == "english" ? 25.4 : 1.);
    else {
        if (!sampleFrequencyDepth(request.frequency, request.durationMinutes, request.frequencyIndex,
                                  request.bound, &request.depthMm, &error)) {
            mStatus->setText(error);
            return;
        }
    }
    if (request.method == "noaa-temporal") {
        const auto &curves = mWorkspace ? mWorkspace->curves() : QList<CumulativeCurve>{};
        const auto selectedCurve = std::find_if(curves.cbegin(), curves.cend(), [this](const auto &curve) {
            return curve.id == mCurve->currentData().toString();
        });
        if (selectedCurve == curves.cend()) {
            mStatus->setText(tr("Fetch and select a NOAA temporal pattern first."));
            return;
        }
        const QString prefix = mTemporalRegion->currentData().toString() + '/' + mTemporalArea->text().trimmed() + '/';
        if (!selectedCurve->id.startsWith(prefix) ||
            std::abs(selectedCurve->durationMinutes - request.durationMinutes) > 1e-9) {
            mStatus->setText(tr("Fetch patterns matching the currently selected NOAA region, area and duration."));
            return;
        }
        request.curve = *selectedCurve;
    } else if (request.method == "custom") {
        request.curve.id = "custom";
        request.curve.title = tr("Custom cumulative curve");
        request.curve.source = tr("User-entered normalized cumulative table");
        if (mCustomCurve->document()->characterCount() > 1024 * 1024) {
            mStatus->setText(tr("A custom cumulative table may contain at most 1 MiB of text."));
            return;
        }
        const auto rows = mCustomCurve->toPlainText().split('\n', Qt::SkipEmptyParts);
        if (rows.size() > 5000) {
            mStatus->setText(tr("A custom curve may contain at most 5000 pairs."));
            return;
        }
        for (const auto &row : rows) {
            const auto values = row.trimmed().split(QRegularExpression("[,;\\s]+"), Qt::SkipEmptyParts);
            bool timeOk = false, depthOk = false;
            const double time = values.value(0).toDouble(&timeOk);
            const double depth = values.value(1).toDouble(&depthOk);
            if (values.size() != 2 || !timeOk || !depthOk) {
                mStatus->setText(tr("Enter each custom curve row as normalized time, cumulative depth."));
                return;
            }
            request.curve.points.append({time, depth});
        }
    }
    const QPointer<StormPanel> guard(this);
    Sdk::IToolRuns *runs = mLifetime ? qobject_cast<Sdk::IToolRuns *>(mLifetime.data()) : nullptr;
    QUuid run;
    if (runs)
        run = runs->beginToolRun({"design-storm", tr("Evaluate design storm"),
                                 tr("%1; duration %2 min, interval %3 min; in-memory evaluation")
                                     .arg(mMethod->currentText()).arg(request.durationMinutes).arg(request.intervalMinutes)}, this);
    if (!guard || (mHost && !mLifetime))
        return;
    StormSeries storm;
    if (!generateStorm(request, &storm, &error)) {
        mCandidateValid = false;
        mStatus->setText(error);
        if (runs && !run.isNull()) {
            runs->appendToolMessage(run, "error", error);
            if (guard && mLifetime)
                runs->finishToolRun(run, Sdk::ToolRunOutcome::Failed, error);
        }
        if (guard)
            controls();
        return;
    }
    if (mCandidates.size() == 8) {
        mCandidates.removeFirst();
        mStarts.removeFirst();
        mProvenance.removeFirst();
        mCandidate->removeItem(0);
    }
    QJsonObject provenance{{"kind", "synthetic-rainfall"}, {"method", storm.method},
                           {"parameters", storm.parameters}, {"manualDepth", manual},
                           {"latitude", mLatitude->value()}, {"longitude", mLongitude->value()}};
    if (!manual || frequencyShape) {
        const auto &source = request.frequency;
        provenance["source"] = "NOAA Atlas 14";
        provenance["volume"] = source.metadata.value("volume").toString();
        provenance["version"] = source.metadata.value("version").toString();
        provenance["retrievedAt"] = source.retrievedAt.toUTC().toString(Qt::ISODateWithMs);
        provenance["series"] = source.query.series;
        provenance["returnPeriod"] = source.returnPeriods.value(request.frequencyIndex);
        provenance["bound"] = mBound->currentText();
        provenance["frequencyUse"] = manual ? "shape-with-manual-total" : "depth-and-shape";
    }
    mCandidates.append(storm);
    mStarts.append(modelClock(mStart->dateTime()));
    mProvenance.append(provenance);
    mCandidate->addItem(tr("%1: %2 · %3 mm · %4 min")
                           .arg(mCandidates.size()).arg(storm.label).arg(number(storm.totalDepthMm))
                           .arg(request.durationMinutes));
    mCandidate->setCurrentIndex(mCandidates.size() - 1);
    mCandidateValid = true;
    QString detail = tr("%1 · total %2 mm · %3 intervals. Candidate stays in memory until applied.")
                               .arg(storm.label, number(storm.totalDepthMm)).arg(storm.depthsMm.size());
    if (storm.method == "chicago")
        detail += tr(" Fitted IDF a=%1, b=%2 min, c=%3; maximum relative fit error %4%.")
                      .arg(number(storm.parameters.value("idfA").toDouble()),
                           number(storm.parameters.value("idfBMinutes").toDouble()),
                           number(storm.parameters.value("idfC").toDouble()),
                           number(100. * storm.parameters.value("idfMaxRelativeError").toDouble(), 4));
    mStatus->setText(detail);
    refreshChart();
    controls();
    if (runs && !run.isNull()) {
        runs->addToolResult(run, {Sdk::ToolResultKind::Value, tr("Generated rainfall"),
                                  tr("%1 mm; %2 intervals").arg(number(storm.totalDepthMm)).arg(storm.depthsMm.size())});
        if (guard && mLifetime)
            runs->addToolResult(run, {Sdk::ToolResultKind::Value, tr("Distribution source"), storm.parameters.value("source").toString()});
        if (guard && mLifetime)
            runs->appendToolMessage(run, "info", detail);
        if (guard && mLifetime)
            runs->finishToolRun(run, Sdk::ToolRunOutcome::Succeeded, tr("Candidate evaluated in memory."));
    }
}
void StormPanel::applyToModel()
{
    if (mRole != Role::Design || !mCandidateValid || mCandidate->currentIndex() < 0 ||
        mCandidate->currentIndex() >= mCandidates.size() || !mLifetime || !mHost)
        return;
    const int selected = mCandidate->currentIndex();
    const StormSeries storm = mCandidates[selected];
    const QDateTime start = mStarts[selected];
    const QJsonObject provenance = mProvenance[selected];
    const QString series = mSeriesName->text().trimmed();
    const QString existingGage = mGage->currentData().toString();
    const QString gage = existingGage.isEmpty() ? mGageName->text().trimmed() : existingGage;
    const bool resetFactor = mResetFactor->isChecked();
    const bool placeGage = existingGage.isEmpty() && mPlaceGage->isChecked();
    const QRegularExpression identity(QStringLiteral("^[^\\s;\\\"\\[\\]]{1,31}$"));
    if (!identity.match(series).hasMatch() || !identity.match(gage).hasMatch()) {
        mStatus->setText(tr("Use nonempty model IDs of at most 31 characters without whitespace, quotes, brackets or semicolons."));
        return;
    }
    Sdk::ModelSessionState state;
    auto *instance = target(&state);
    if (!instance || state.sessionId != mTargetState.sessionId ||
        state.loadGeneration != mTargetState.loadGeneration || state.modelRevision != mTargetState.modelRevision) {
        refreshTarget();
        mStatus->setText(tr("The model changed. Review the refreshed target before applying again."));
        return;
    }
    auto *metadata = qobject_cast<Sdk::IModelRainfall *>(mLifetime.data());
    Sdk::RainfallModelInfo info;
    QString error;
    if (!metadata || !metadata->modelRainfall(state.sessionId, state.loadGeneration, &info, &error) ||
        info.depthUnit != mRainUnit) {
        mStatus->setText(error.isEmpty() ? tr("Verified model rainfall units are unavailable or changed.") : error);
        return;
    }
    if (existingGage.isEmpty()) {
        for (const auto &other : info.gages)
            if (other.id.compare(gage, Qt::CaseInsensitive) == 0) {
                mStatus->setText(tr("That gage ID already exists. Select it explicitly or choose a new ID."));
                return;
            }
    } else if (std::none_of(info.gages.cbegin(), info.gages.cend(),
                           [&gage](const auto &other) { return other.id == gage; })) {
        mStatus->setText(tr("The selected gage is no longer available."));
        return;
    }
    QPointF point;
    if (placeGage) {
        auto *project = instance->project();
        const QPointF lonLat(provenance.value("longitude").toDouble(), provenance.value("latitude").toDouble());
        if (!project || !project->fromLonLat(lonLat, &point)) {
            mStatus->setText(tr("Set the model coordinate system, clear 'Place new gage at the query location' to create an unlocated gage, or select an existing gage."));
            return;
        }
    }
    QVector<QDateTime> times;
    QVector<double> values;
    times.reserve(storm.intensityMmPerHour.size() + 1);
    values.reserve(storm.intensityMmPerHour.size() + 1);
    const double unitFactor = mRainUnit == "in" ? 1. / 25.4 : 1.;
    for (qsizetype i = 0; i < storm.intensityMmPerHour.size(); ++i) {
        times.append(start.addSecs(qint64(i) * storm.intervalMinutes * 60));
        values.append(storm.intensityMmPerHour[i] * unitFactor);
    }
    times.append(start.addSecs(qint64(storm.intensityMmPerHour.size()) * storm.intervalMinutes * 60));
    values.append(0.);
    const QPointer<StormPanel> guard(this);
    const QPointer<QObject> modelLifetime(instance->lifetime());
    auto *runs = qobject_cast<Sdk::IToolRuns *>(mLifetime.data());
    QUuid run;
    if (runs)
        run = runs->beginToolRun({"design-storm", tr("Apply design storm"),
            tr("%1 → %2; %3 mm").arg(series, gage, number(storm.totalDepthMm)), state.sessionId,
            state.loadGeneration}, this);
    if (!guard || !mLifetime || !modelLifetime)
        return;
    Sdk::ModelSessionState current;
    if (target(&current) != instance || current.sessionId != state.sessionId ||
        current.loadGeneration != state.loadGeneration || current.modelRevision != state.modelRevision) {
        if (runs && !run.isNull())
            runs->finishToolRun(run, Sdk::ToolRunOutcome::Failed, tr("The model changed before the edit was accepted."));
        return;
    }
    auto edit = instance->edit(tr("Apply design storm %1").arg(series), &error);
    if (!guard || !mLifetime || !modelLifetime) {
        edit.reset();
        return;
    }
    bool ok = edit && edit->addTimeSeries(series, times, values, false) &&
              (!existingGage.isEmpty() || edit->addGage(gage, point)) &&
              edit->setGageSeries(gage, series, storm.intervalMinutes * 60., 0) &&
              edit->setAttribute("gage", gage, "dataSource", 0, &error);
    if (ok && resetFactor)
        ok = edit->setAttribute("gage", gage, "scaleFactor", 1., &error);
    if (!guard || !mLifetime || !modelLifetime) {
        edit.reset();
        return;
    }
    if (ok) {
        auto *project = instance->project();
        if (!project)
            ok = false;
        else {
            auto extension = project->extensionData();
            auto applications = extension.value("designStorms").toObject();
            auto accepted = provenance;
            accepted["series"] = series;
            accepted["gage"] = gage;
            accepted["rainUnit"] = mRainUnit;
            accepted["resetGageMultiplier"] = resetFactor;
            accepted["gageLocation"] = existingGage.isEmpty()
                ? (placeGage ? "query-location" : "unlocated") : "existing-gage";
            accepted["startWallClock"] = start.toString("yyyy-MM-ddTHH:mm:ss");
            accepted["appliedAt"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
            applications[series] = accepted;
            extension["designStorms"] = applications;
            // SDK 1.12 snapshots extension data inside the same mediated edit.
            project->setExtensionData(extension);
            ok = edit->commit(&error);
        }
    }
    edit.reset(); // Complete rollback before reporting a failed transaction.
    if (!guard || !mLifetime)
        return;
    if (!ok) {
        const QString detail = error.isEmpty()
            ? tr("The storm could not be applied. Check for an existing time-series ID or a locked model; no model edit was committed.") : error;
        mStatus->setText(detail);
        if (runs && !run.isNull()) {
            runs->appendToolMessage(run, "error", detail);
            if (guard && mLifetime)
                runs->finishToolRun(run, Sdk::ToolRunOutcome::Failed, detail);
        }
        return;
    }
    const QString detail = tr("Applied %1 to gage %2 in %3/h as one model edit. %4 No NOAA response file was saved.")
                               .arg(series, gage, mRainUnit,
                                    resetFactor ? tr("Gage multiplier set to 1.") : tr("Existing gage multiplier preserved; model rainfall includes that scaling."));
    mStatus->setText(detail);
    refreshTarget();
    if (runs && !run.isNull()) {
        runs->addToolResult(run, {Sdk::ToolResultKind::Value, tr("Model rainfall"),
                                  tr("Series %1; gage %2").arg(series, gage)});
        if (guard && mLifetime)
            runs->appendToolMessage(run, "info", detail);
        if (guard && mLifetime)
            runs->finishToolRun(run, Sdk::ToolRunOutcome::Succeeded, detail);
    }
    if (guard && modelLifetime)
        emit applied(series, gage);
}
} // namespace OpenSWMM::Data
