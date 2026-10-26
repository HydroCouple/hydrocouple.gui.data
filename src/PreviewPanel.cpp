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
#include <QStandardItemModel>
#include <QTableView>
#include <QTimer>
#include <QUuid>
#include <QVBoxLayout>
#include <memory>

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
    host->observe(this, [this](const QString &event, const QVariantMap &data) {
        if (event == "instanceOpened" || event == "instanceActivated" ||
            event == "instanceClosed") {
            bindProject();
            return;
        }
        if (data.value("id").toUuid() != mInstanceId)
            return;
        if ((event == "viewportChanged" && mAreaMode->currentIndex() == 0) ||
            event == "layersChanged" ||
            (event == "boundaryChanged" && mAreaMode->currentIndex() == 1 &&
             data.value("layerId").toString() == mLayers->currentData().toString())) {
            invalidate(tr("The review area changed. Preview again before saving features."));
            if (event != "viewportChanged")
                refreshLayers();
        }
    });
    bindProject();
}

PreviewPanel::~PreviewPanel()
{
    mClosing = true;
    cancel();
    if (mProject)
        mProject->clearPreview();
}
void PreviewPanel::setSource(Source source)
{
    mSource->setCurrentIndex(source == Source::Sites ? 0 : 1);
}
void PreviewPanel::cancel()
{
    ++mGeneration;
    if (mReply) {
        disconnect(mReply, nullptr, this, nullptr);
        mReply->abort();
        mReply->deleteLater();
        mReply = nullptr;
    }
    if (mJob) {
        mJob->cancel();
        mJob = nullptr;
    }
    setBusy(false);
}
void PreviewPanel::setBusy(bool busy)
{
    mBusy = busy;
    const bool open = mInstance && mInstance->isOpen();
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
    cancel();
    mValid = false;
    mItems.clear();
    mArea = {};
    mProvenance = {};
    mQuery = QUrl{};
    mModel->removeRows(0, mModel->rowCount());
    if (mProject)
        mProject->clearPreview();
    mExport->setEnabled(false);
    if (!message.isEmpty())
        mStatus->setText(message);
}
void PreviewPanel::bindProject()
{
    auto *instance = mHost->activeInstance();
    // Utility/result documents have no active model facade. Keep the last explicitly
    // active, still-open model rather than switching the data review to another project.
    if (!instance && mInstance && mInstance->isOpen())
        instance = mInstance;
    if (instance && !instance->isOpen())
        instance = nullptr;
    const auto id = instance ? instance->id() : QUuid{};
    if (instance && id == mInstanceId && instance == mInstance)
        return;
    invalidate();
    mInstance = instance;
    mInstanceId = id;
    mProject = instance ? instance->project() : nullptr;
    refreshLayers();
    setBusy(false);
    mStatus->setText(instance ? tr("Choose an area and preview catalogue metadata.")
                              : tr("Open a model to preview data sources."));
}
void PreviewPanel::refreshLayers()
{
    const auto previous = mLayers->currentData();
    const QSignalBlocker guard(mLayers);
    mLayers->clear();
    if (mProject && mInstance->isOpen())
        for (const auto &layer : mProject->polygonLayers())
            mLayers->addItem(layer.value("title").toString(), layer.value("id"));
    const int index = mLayers->findData(previous);
    if (index >= 0)
        mLayers->setCurrentIndex(index);
    refreshFeatures();
}
void PreviewPanel::refreshFeatures()
{
    const auto previous = mFeatures->currentData();
    const QSignalBlocker guard(mFeatures);
    mFeatures->clear();
    if (mProject && mInstance->isOpen()) {
        const auto features = mProject->boundaryFeatures(mLayers->currentData().toString(),
                                                         mSelectedOnly->isChecked(), 0, 5000);
        for (const auto &feature : features)
            mFeatures->addItem(feature.value("title").toString(), feature.value("id"));
    }
    const int index = mFeatures->findData(previous);
    if (index >= 0)
        mFeatures->setCurrentIndex(index);
}
QJsonObject PreviewPanel::captureArea(QString *error) const
{
    if (!mProject || !mInstance || !mInstance->isOpen()) {
        if (error)
            *error = tr("Open a model first.");
        return {};
    }
    if (mAreaMode->currentIndex() == 0)
        return mProject->viewportBoundary(error);
    if (mLayers->currentIndex() < 0 || mFeatures->currentIndex() < 0) {
        if (error)
            *error = tr("Choose a loaded polygon layer and boundary feature.");
        return {};
    }
    return mProject->boundaryPolygon(mLayers->currentData().toString(),
                                     mFeatures->currentData().toLongLong(), error);
}
bool PreviewPanel::beginPreview()
{
    invalidate();
    QString error;
    mArea = captureArea(&error);
    mQuery = catalogueUrl(mSource->currentIndex() == 0 ? Source::Sites : Source::Terrain, mArea,
                          mDataset->currentData().toString(),
                          mHost->settings()->value("previewLimit", 500).toInt(), &error);
    if (mQuery.isEmpty()) {
        mStatus->setText(error);
        return false;
    }
    setBusy(true);
    mStatus->setText(tr("Previewing catalogue metadata…"));
    return true;
}
void PreviewPanel::request(const QUrl &url, int attempt, int redirects)
{
    const auto generation = mGeneration;
    QNetworkRequest networkRequest(url);
    networkRequest.setTransferTimeout(30000);
    networkRequest.setRawHeader("Accept", "application/json");
    networkRequest.setRawHeader("User-Agent", "OpenSWMM-Data/0.1");
    auto *reply = mHost->network()->shared()->get(networkRequest);
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
                if (generation != mGeneration || mClosing)
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
    return mProject && mInstance && !mInstance->inpPath().isEmpty() &&
                   !mProject->directory().isEmpty()
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
    if (!beginPreview())
        return;
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
    parse(file.readAll(), false);
}
void PreviewPanel::useCache()
{
    if (!beginPreview())
        return;
    if (cacheDirectory().isEmpty()) {
        setBusy(false);
        mStatus->setText(
            tr("Save the model in a project folder before using its catalogue cache."));
        return;
    }
    QFile index(QDir(cacheDirectory()).filePath("index.json"));
    if (!index.open(QIODevice::ReadOnly)) {
        setBusy(false);
        mStatus->setText(tr("No cached preview matches this area and source."));
        return;
    }
    const auto filename =
        QJsonDocument::fromJson(index.readAll()).object().value(cacheKey()).toString();
    QFile file(QDir(cacheDirectory()).filePath(filename));
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
    parse(file.readAll(), false);
}
void PreviewPanel::parse(const QByteArray &bytes, bool cache)
{
    struct Result {
        QList<Item> items;
        QString error;
        bool more = false;
    };
    auto result = std::make_shared<Result>();
    const auto generation = mGeneration;
    const auto area = mArea;
    const auto source = mSource->currentIndex() == 0 ? Source::Sites : Source::Terrain;
    mJob = mHost->runJob(
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
            if (!guard || generation != mGeneration || mClosing)
                return;
            mJob = nullptr;
            setBusy(false);
            if (!ok || !result->error.isEmpty()) {
                mStatus->setText(!result->error.isEmpty() ? result->error
                                 : !error.isEmpty() ? error : tr("Cancelled."));
                return;
            }
            if (cache && !cacheDirectory().isEmpty()) {
                const auto directory = cacheDirectory();
                QDir().mkpath(directory);
                const auto name = QUuid::createUuid().toString(QUuid::WithoutBraces) + ".json";
                const QJsonObject metadata{
                    {"url", mQuery.toString()},
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
            apply(result->items, result->more);
        });
    if (!mJob) {
        setBusy(false);
        mStatus->setText(tr("The preview could not be started."));
    }
}
void PreviewPanel::apply(const QList<Item> &items, bool more)
{
    mItems = items;
    mModel->removeRows(0, mModel->rowCount());
    for (int i = 0; i < items.size(); ++i) {
        const auto &item = items[i];
        auto *name = new QStandardItem(item.title);
        name->setData(i, Qt::UserRole);
        const auto details = item.metadata.value("site_type")
                                 .toString(item.metadata.value("publicationDate").toString());
        mModel->appendRow({name, new QStandardItem(item.id), new QStandardItem(details)});
    }
    mValid = true;
    mStatus->setText(
        tr("%1 results intersect the reviewed area. Select rows to preview or save them.%2")
            .arg(items.size())
            .arg(
                more
                    ? tr(" More catalogue results are available; narrow the area or raise the preview limit.")
                    : QString{}));
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
    if (!mValid || !mProject || !mInstance || !mInstance->isOpen()) {
        mExport->setEnabled(false);
        return;
    }
    const auto selected = selectedItems();
    QString error;
    if (!mProject->showPreview(featureCollection(selected.isEmpty() ? mItems : selected), &error))
        mStatus->setText(error);
    mExport->setEnabled(!mBusy && !selected.isEmpty());
}
void PreviewPanel::exportSelection()
{
    const auto selected = selectedItems();
    if (!mValid || selected.isEmpty() || !mProject || !mInstance->isOpen())
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
    const auto id = mProject->addFeatureLayer(
        path, mSource->currentIndex() == 0 ? tr("USGS sites") : tr("Elevation footprints"), &error);
    mStatus->setText(id.isEmpty() ? error
                                  : tr("Saved selected features as a map layer: %1").arg(path));
}
}
