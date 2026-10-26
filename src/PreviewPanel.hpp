/**
 * @file PreviewPanel.hpp
 * @brief Model/view/controller dock for reviewing sites and elevation footprints.
 * @details Area and result identities are frozen together. Relevant model/view/layer changes
 * invalidate the review before another export. Network requests use the host policy client;
 * copied geometry/response parsing runs in cancellable host jobs. The host owns this widget.
 * @author Caleb Buahin <caleb.buahin@gmail.com>
 * @date 2026
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license GPL-3.0-or-later
 */
#pragma once
#include "Catalog.hpp"
#include <sdk/ExtensionHost.hpp>
#include <sdk/IProject.hpp>
#include <QPointer>
#include <QWidget>

class QComboBox;
class QCheckBox;
class QLabel;
class QPushButton;
class QTableView;
class QStandardItemModel;
class QNetworkReply;

namespace OpenSWMM::Data
{
/** @brief Hosted catalogue review UI; persistent GIS export requires a current reviewed area. */
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
    /** @brief Cancel outstanding metadata work without publishing incomplete results. */
    void cancel();
public slots:
    /** @brief Preview an explicitly chosen saved response using the currently chosen area/source.
     * @param[in] path Existing JSON response file, bounded to 20 MiB; no network request is made.
     * @note Also supports deterministic offline GUI conformance without private host headers. */
    void loadCatalogue(const QString &path);

private:
    void bindProject();
    void refreshLayers();
    void refreshFeatures();
    void invalidate(const QString &message = {});
    QJsonObject captureArea(QString *error) const;
    bool beginPreview();
    void request(const QUrl &url, int attempt = 0, int redirects = 0);
    void parse(const QByteArray &bytes, bool cache);
    void apply(const QList<Item> &items, bool more);
    void updateOverlay();
    void exportSelection();
    void useCache();
    QList<Item> selectedItems() const;
    QString cacheDirectory() const;
    QString cacheKey() const;
    void setBusy(bool busy);
    Sdk::ExtensionHost *mHost;
    Sdk::IModelInstance *mInstance = nullptr;
    Sdk::IProject *mProject = nullptr;
    QUuid mInstanceId;
    QComboBox *mSource;
    QComboBox *mAreaMode;
    QComboBox *mLayers;
    QComboBox *mFeatures;
    QComboBox *mDataset;
    QCheckBox *mSelectedOnly;
    QLabel *mStatus;
    QPushButton *mPreview;
    QPushButton *mCancel;
    QPushButton *mCache;
    QPushButton *mRecorded;
    QPushButton *mExport;
    QTableView *mTable;
    QStandardItemModel *mModel;
    QPointer<QNetworkReply> mReply;
    Sdk::IJob *mJob = nullptr;
    QList<Item> mItems;
    QJsonObject mArea;
    QJsonObject mProvenance; ///< Actual response origin, separate from the intended review query.
    QUrl mQuery;
    quint64 mGeneration = 0;
    bool mValid = false;
    bool mBusy = false;
    bool mClosing = false;
};
}
