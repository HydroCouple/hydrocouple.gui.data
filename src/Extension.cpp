/**
 * @file Extension.cpp
 * @brief Standalone SDK-only registration of the Data ribbon, preview dock and preferences.
 * @details The extension links Qt, GDAL and the installed SDK. No GUI private header, engine
 * handle or executable symbol is used. Host ownership and QObject guards govern all UI callbacks.
 * @author Caleb Buahin <caleb.buahin@gmail.com>
 * @date 2026
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license GPL-3.0-or-later
 */
#include "PreviewPanel.hpp"
#include <sdk/IGuiExtension.hpp>
#include <QFormLayout>
#include <QLabel>
#include <QSpinBox>

using namespace OpenSWMM::Sdk;
using namespace OpenSWMM::Data;

/** @brief Independently built catalogue-preview plugin with dynamically hosted contributions. */
class DataExtension final : public QObject,
                            public IGuiExtension,
                            public IDockPanelFactory,
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
                "Review monitoring sites and elevation footprints",
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
        if (!lifetime || !qobject_cast<IEventSourceV2 *>(lifetime) ||
            !qobject_cast<IEventSourceV3 *>(lifetime) ||
            !qobject_cast<IEventSourceV4 *>(lifetime) ||
            !qobject_cast<IViewControlsV1 *>(lifetime)) {
            if (error)
                *error = tr(
                    "Data sources requires SDK 1.5 lifecycle, view, activity and selected-feature services.");
            return false;
        }
        mHost = host;
        auto *sites = new QAction(tr("USGS sites…"), host->lifetime());
        auto *terrain = new QAction(tr("Elevation footprints…"), host->lifetime());
        connect(sites, &QAction::triggered, host->lifetime(), [this]() { show(Source::Sites); });
        connect(terrain, &QAction::triggered, host->lifetime(),
                [this]() { show(Source::Terrain); });
        if (!host->ui()->registerAction("sites", sites) ||
            !host->ui()->registerAction("terrain", terrain) ||
            !host->ui()->registerDockPanelFactory(this) ||
            !host->ui()->registerPreferencesPage(this) ||
            !host->ui()->registerRibbonTab(
                {"data",
                 tr("Data"),
                 false,
                 {{tr("Preview data sources"), {"sites", "terrain"}}}})) {
            if (error)
                *error = tr("Could not register the Data interface.");
            return false;
        }
        host->console()->append(
            "info", tr("Data sources loaded. Preview an area before saving selected features."));
        return true;
    }
    /** @brief Abort pending metadata work before host-owned widgets are destroyed. */
    void shutdown() override
    {
        if (mPanel)
            mPanel->cancel();
        mHost = nullptr;
        mSettings = nullptr;
    }
    /** @brief Enumerate contribution families. @return Owning capability captions. */
    QStringList contributions() const override { return {"ribbon", "dock", "preferences"}; }
    /** @brief Identify the dock/preferences factory in its distinct host registries. @return Stable local identity. */
    QString id() const override { return "data"; }
    /** @brief Supply the dock/preferences caption. @return Translated title. */
    QString title() const override { return tr("Data sources"); }
    /** @brief Create the host-owned catalogue dock.
     * @param[in] host Borrowed initialized extension host.
     * @return New widget transferring to host ownership. */
    QWidget *create(ExtensionHost *host) override
    {
        mPanel = new PreviewPanel(host);
        return mPanel;
    }
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
        auto *note = new QLabel(
            tr("Large areas may contain more records. The preview reports when more catalogue results are available."),
            page);
        note->setWordWrap(true);
        form->addRow(note);
        return page;
    }
    /** @brief Persist the edited request limit when the host applies preferences. */
    void apply() override
    {
        if (mSettings && mLimit)
            mSettings->setValue("previewLimit", mLimit->value());
    }
    /** @brief Restore the page's default request limit. */
    void reset() override
    {
        if (mLimit)
            mLimit->setValue(500);
    }

private:
    void show(Source source)
    {
        if (mPanel)
            mPanel->setSource(source);
        if (mHost)
            mHost->ui()->showDock("data");
    }
    ExtensionHost *mHost = nullptr;
    ISettings *mSettings = nullptr;
    QPointer<PreviewPanel> mPanel;
    QPointer<QSpinBox> mLimit;
};
#include "Extension.moc"
