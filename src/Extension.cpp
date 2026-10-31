/**
 * @file Extension.cpp
 * @brief SDK-only Data tools with optional private/shared ribbon placement and preferences.
 * @details The extension links Qt, GDAL and the installed SDK. No GUI private header, engine
 * handle or executable symbol is used. Host ownership and QObject guards govern all UI callbacks.
 * @author Caleb Buahin <caleb.buahin@gmail.com>
 * @date 2026
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license GPL-3.0-or-later
 */
#include "PreviewPanel.hpp"
#include "CommandIcons.hpp"
#include "StormPanel.hpp"
#include <sdk/IModelRainfall.hpp>
#include <sdk/IToolbox.hpp>
#include <sdk/IToolPreviews.hpp>
#include <sdk/IToolRuns.hpp>
#include <sdk/ISharedRibbon.hpp>
#include <sdk/IGuiExtension.hpp>
#include <QComboBox>
#include <QFormLayout>
#include <QLabel>
#include <QSpinBox>

using namespace OpenSWMM::Sdk;
using namespace OpenSWMM::Data;

/** @brief Independently built catalogue-preview plugin with dynamically hosted contributions. */
class DataExtension final : public QObject,
                            public IGuiExtension,
                            public IPreferencesPageFactory
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID OPENSWMM_GUI_EXTENSION_IID FILE "plugin.json")
    Q_INTERFACES(OpenSWMM::Sdk::IGuiExtension)
public:
    /** @brief Return the identity matching the generated embedded metadata. @return Owning plugin metadata. */
    ExtensionInfo info() const override
    {
        return {"openswmm.gui.data",
                "Data sources",
                "Live NOAA rainfall, design storms, observations and elevation",
                "0.1.0",
                "OpenSWMM",
                "https://github.com/HydroCouple/hydrocouple.gui.data",
                "GPL-3.0-or-later",
                {"data", "catalogue", "preview"}};
    }
    /** @brief Register contributions through the installed public SDK.
     * @param[in] host Borrowed service facade, valid until shutdown.
     * @param[out] error Optional registration diagnostic.
     * @return False if any required contribution could not be registered. */
    bool initialize(ExtensionHost *host, QString *error) override
    {
        auto *lifetime = host ? host->lifetime() : nullptr;
        if (!lifetime || !qobject_cast<ISessionEvents *>(lifetime) ||
            !qobject_cast<IViewEvents *>(lifetime) ||
            !qobject_cast<IActivityEvents *>(lifetime) ||
            !qobject_cast<IViewControls *>(lifetime) ||
            !qobject_cast<ITimeSeriesLayers *>(lifetime) ||
            !qobject_cast<IProgress *>(lifetime) ||
            !qobject_cast<IPreviewLayers *>(lifetime) ||
            !qobject_cast<IToolbox *>(lifetime) ||
            !qobject_cast<IToolPreviews *>(lifetime) ||
            !qobject_cast<IToolRuns *>(lifetime) ||
            !qobject_cast<ISharedRibbon *>(lifetime) ||
            !qobject_cast<IModelRainfall *>(lifetime)) {
            if (error)
                *error = tr(
                    "Data sources requires SDK 1.12 rainfall metadata, toolbox, shared ribbon, tool previews, analysis runs, lifecycle, native layers and progress services.");
            return false;
        }
        mHost = host;
        mSettings = host->settings();
        mRibbonPlacement = QStringLiteral("toolbox");
        mStormWorkspace = new StormWorkspace(host, lifetime);
        auto *sites = new QAction(tr("USGS time series…"), host->lifetime());
        auto *terrain = new QAction(tr("Elevation raster…"), host->lifetime());
        auto *precipitation = new QAction(tr("NOAA precipitation frequency…"), lifetime);
        auto *designStorm = new QAction(tr("Design storm builder…"), lifetime);
        setCommandIcon(sites, CommandIcon::MonitoringSites);
        setCommandIcon(terrain, CommandIcon::ElevationFootprints);
        setCommandIcon(precipitation, CommandIcon::PrecipitationFrequency);
        setCommandIcon(designStorm, CommandIcon::DesignStorm);
        auto *toolbox = qobject_cast<IToolbox *>(lifetime);
        const ToolSpec gages{"sites", "sites", tr("Preview and download USGS observations"),
                             {{"download", tr("Download data")}},
                             {"download", "usgs", "streamgages", "time-series"}, tr("Data sources")};
        const ToolSpec dem{"terrain", "terrain", tr("Preview and download elevation rasters"),
                           {{"download", tr("Download data")}},
                           {"download", "usgs", "elevation", "raster"}, tr("Data sources")};
        const ToolSpec frequency{"precipitation", "precipitation", tr("Evaluate live NOAA Atlas 14 frequency curves"),
                                 {{"precipitation", tr("Precipitation")}},
                                 {"precipitation", "noaa", "frequency", "retrieval"}, tr("Data sources")};
        const ToolSpec storm{"design-storm", "design-storm", tr("Compare design storms and apply rainfall to the model"),
                             {{"precipitation", tr("Precipitation")}},
                             {"precipitation", "design-storm", "time-series"}, tr("Data sources")};
        if (!host->ui()->registerAction("sites", sites) ||
            !host->ui()->registerAction("terrain", terrain) ||
            !host->ui()->registerAction("precipitation", precipitation) ||
            !host->ui()->registerAction("design-storm", designStorm) ||
            !toolbox->registerTool(gages, lifetime, [this](ExtensionHost *owner) {
                mSitesPanel = new PreviewPanel(owner, Source::Sites, "sites");
                return mSitesPanel.data();
            }, error) ||
            !toolbox->registerTool(dem, lifetime, [this](ExtensionHost *owner) {
                mTerrainPanel = new PreviewPanel(owner, Source::Terrain, "terrain");
                return mTerrainPanel.data();
            }, error) ||
            !toolbox->registerTool(frequency, lifetime, [this](ExtensionHost *owner) {
                return new StormPanel(owner, StormPanel::Role::Frequency, mStormWorkspace);
            }, error) ||
            !toolbox->registerTool(storm, lifetime, [this](ExtensionHost *owner) {
                return new StormPanel(owner, StormPanel::Role::Design, mStormWorkspace);
            }, error) ||
            !host->ui()->registerPreferencesPage(this)) {
            if (error)
                *error = tr("Could not register the Data interface.");
            return false;
        }
        QString ribbonError;
        const auto placement = mSettings
                                   ? mSettings->value("ribbonPlacement", QStringLiteral("toolbox")).toString()
                                   : QStringLiteral("toolbox");
        if (!setRibbonPlacement(placement, &ribbonError))
            host->console()->append("warning", ribbonError);
        host->console()->append(
            "info",
            tr("Data sources loaded. NOAA evaluation stays in memory; Apply creates the selected model rainfall. USGS and elevation previews remain independent."));
        return true;
    }
    /** @brief Abort pending previews and downloads before host-owned widgets are destroyed. */
    void shutdown() override
    {
        if (mSitesPanel)
            mSitesPanel->cancel();
        if (mTerrainPanel)
            mTerrainPanel->cancel();
        if (mStormWorkspace)
            mStormWorkspace->clear();
        mHost = nullptr;
        mSettings = nullptr;
        mRibbonPlacement = QStringLiteral("toolbox");
    }
    /** @brief Enumerate contribution families. @return Owning capability captions. */
    QStringList contributions() const override { return {"ribbon", "toolbox", "dock", "preferences"}; }
    /** @brief Identify the dock/preferences factory in its distinct host registries. @return Stable local identity. */
    QString id() const override { return "data"; }
    /** @brief Supply the dock/preferences caption. @return Translated title. */
    QString title() const override { return tr("Data sources"); }
    /** @brief Create this plugin's preferences page.
     * @param[in] settings Borrowed namespaced storage.
     * @return New widget transferred to the Preferences dialog. */
    QWidget *create(ISettings *settings) override
    {
        mSettings = settings;
        auto *page = new QWidget;
        auto *form = new QFormLayout(page);
        mLimit = new QSpinBox(page);
        mLimit->setRange(1, 5000);
        mLimit->setValue(settings->value("previewLimit", 500).toInt());
        form->addRow(tr("Maximum catalogue records per preview"), mLimit);
        mRibbonChoice = new QComboBox(page);
        mRibbonChoice->setObjectName("dataRibbonPlacement");
        mRibbonChoice->addItem(tr("Toolbox only"), "toolbox");
        mRibbonChoice->addItem(tr("Shared Data tab"), "shared");
        mRibbonChoice->addItem(tr("Separate Data tab"), "separate");
        mRibbonChoice->setCurrentIndex(mRibbonChoice->findData(mRibbonPlacement));
        form->addRow(tr("Ribbon placement"), mRibbonChoice);
        auto *ribbonNote = new QLabel(
            tr("Ribbon changes apply immediately. All Data tools are available in the Analysis Toolbox."),
            page);
        ribbonNote->setWordWrap(true);
        form->addRow(ribbonNote);
        auto *note = new QLabel(
            tr("Large areas may contain more records. The preview reports when more catalogue results are available."),
            page);
        note->setWordWrap(true);
        form->addRow(note);
        return page;
    }
    /** @brief Apply ribbon placement immediately and persist the accepted preferences. */
    void apply() override
    {
        if (mSettings && mLimit)
            mSettings->setValue("previewLimit", mLimit->value());
        if (!mHost || !mSettings || !mRibbonChoice)
            return;
        QString error;
        const QPointer<QObject> lifetime(mHost->lifetime());
        const bool changed = setRibbonPlacement(mRibbonChoice->currentData().toString(), &error);
        if (!lifetime || !mHost || !mSettings || !mRibbonChoice)
            return;
        if (changed)
            mSettings->setValue("ribbonPlacement", mRibbonPlacement);
        else {
            mRibbonChoice->setCurrentIndex(mRibbonChoice->findData(mRibbonPlacement));
            mHost->console()->append("warning", error);
            if (lifetime && mHost)
                mHost->ui()->statusMessage(error);
        }
    }
    /** @brief Restore the default preview limit and toolbox-only placement in the page. */
    void reset() override
    {
        if (mLimit)
            mLimit->setValue(500);
        if (mRibbonChoice)
            mRibbonChoice->setCurrentIndex(mRibbonChoice->findData("toolbox"));
    }

private:
    /** @brief Mount the chosen optional ribbon before retiring the preceding placement.
     * @param[in] requested toolbox, shared or separate; unknown saved values use toolbox.
     * @param[out] error Optional mounting refusal detail, cleared on entry.
     * @return True after applying the placement. A refused mount retains the previous tab. */
    bool setRibbonPlacement(const QString &requested, QString *error)
    {
        if (error)
            error->clear();
        if (!mHost)
            return false;
        const QString placement = requested == "shared" || requested == "separate"
                                      ? requested : QStringLiteral("toolbox");
        if (placement == mRibbonPlacement)
            return true;
        const QPointer<QObject> lifetime(mHost->lifetime());
        auto *shared = qobject_cast<ISharedRibbon *>(lifetime.data());
        bool mounted = true;
        if (placement == "shared")
            mounted = shared && shared->registerSharedRibbonTab(
                {"data", "data", tr("Data"), {{tr("Precipitation"), {"precipitation", "design-storm"}},
                                               {tr("Download data"), {"sites", "terrain"}}}}, error);
        else if (placement == "separate")
            mounted = mHost->ui()->registerRibbonTab(
                {"data", tr("Data"), false, {{tr("Precipitation"), {"precipitation", "design-storm"}},
                                            {tr("Download data"), {"sites", "terrain"}}}});
        if (!lifetime || !mHost)
            return false;
        if (!mounted) {
            if (error && error->isEmpty())
                *error = tr("Could not apply the selected Data ribbon placement.");
            return false;
        }
        const QString previous = mRibbonPlacement;
        mRibbonPlacement = placement;
        if (previous == "shared" && shared)
            shared->unregisterSharedRibbonTab("data");
        else if (previous == "separate")
            mHost->ui()->unregisterRibbonTab("data");
        return lifetime && mHost;
    }
    ExtensionHost *mHost = nullptr;
    ISettings *mSettings = nullptr;
    QPointer<PreviewPanel> mSitesPanel; ///< Independent gage preview/download dock.
    QPointer<PreviewPanel> mTerrainPanel; ///< Independent terrain preview/download dock.
    QPointer<StormWorkspace> mStormWorkspace; ///< Session-only NOAA data shared by the two precipitation tools.
    QPointer<QSpinBox> mLimit;
    QPointer<QComboBox> mRibbonChoice; ///< Dialog-owned staged ribbon placement.
    QString mRibbonPlacement = "toolbox"; ///< Currently mounted placement, independent of a live page.
};
#include "Extension.moc"
