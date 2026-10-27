/**
 * @file test_commandicons.cpp
 * @brief Offline resource, theme, interaction-mode and high-DPI checks for Data command icons.
 * @details Exercises actual embedded SVG rendering and action palette updates;
 * reviewable light/dark PNGs remain under the configured repository directory.
 * @author Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license GPL-3.0-or-later
 */
#include "CommandIcons.hpp"
#include <QAction>
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QPainter>
#include <QPalette>
#include <QSignalSpy>
#include <QTest>
#include <memory>

using namespace OpenSWMM::Data;

namespace
{
/** @brief Restore the user's application palette at the end of a case. */
class PaletteRestorer
{
public:
    /** @brief Copy the current application palette before the test changes it. */
    PaletteRestorer() : mOriginal(QApplication::palette()) {}
    /** @brief Return the application palette to its original state. */
    ~PaletteRestorer() { QApplication::setPalette(mOriginal); }

private:
    QPalette mOriginal; ///< Owning original palette.
};
/** @brief Find a visible glyph colour independently of antialiased transparent margins.
 * @param[in] pixmap Actual icon raster, including device-pixel metadata.
 * @return Most opaque pixel colour, or transparent when no glyph was rendered. */
QColor ink(const QPixmap &pixmap)
{
    QColor result(Qt::transparent);
    const auto image = pixmap.toImage().convertToFormat(QImage::Format_ARGB32);
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x) {
            const auto color = image.pixelColor(x, y);
            if (color.alpha() > result.alpha())
                result = color;
        }
    return result;
}
/** @brief Construct strong, deliberately distinct semantic palette roles.
 * @param[in] dark Whether to use light foreground on a dark application surface.
 * @return Owning light/dark test palette. */
QPalette testPalette(bool dark)
{
    auto palette = QApplication::palette();
    palette.setColor(QPalette::Active, QPalette::ButtonText,
                     dark ? QColor("#e2e8f0") : QColor("#16324f"));
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor("#87909c"));
    palette.setColor(QPalette::Active, QPalette::HighlightedText, QColor("#f0d269"));
    return palette;
}
}

/** @brief Verify the standalone plugin's real resources and palette observers. */
class CommandIconTest final : public QObject
{
    Q_OBJECT
private slots:
    /** @brief Render each independent icon in the sizes used by menus and ribbons. */
    void embeddedResourcesRenderAtMenuAndRibbonScales()
    {
        PaletteRestorer restore;
        const auto palette = testPalette(false);
        QApplication::setPalette(palette);
        QVERIFY(QFile::exists(":/openswmm/data/icons/usgs-gage.svg"));
        QVERIFY(QFile::exists(":/openswmm/data/icons/dem.svg"));
        for (const auto kind : {CommandIcon::MonitoringSites, CommandIcon::ElevationFootprints}) {
            const auto icon = commandIcon(kind);
            QVERIFY(!icon.isNull());
            for (const int edge : {16, 24, 32, 48, 64})
                for (const qreal scale : {1., 2.})
                    for (const auto mode :
                         {QIcon::Normal, QIcon::Active, QIcon::Selected, QIcon::Disabled}) {
                        const auto image = icon.pixmap(QSize(edge, edge), scale, mode);
                        QVERIFY(!image.isNull());
                        QCOMPARE(image.size(), QSize(edge, edge) * scale);
                        QCOMPARE(image.devicePixelRatio(), scale);
                        const auto color = ink(image);
                        QVERIFY(color.alpha() > 200);
                        const auto expected =
                            mode == QIcon::Disabled
                                ? palette.color(QPalette::Disabled, QPalette::ButtonText)
                            : mode == QIcon::Selected
                                ? palette.color(QPalette::Active, QPalette::HighlightedText)
                                : palette.color(QPalette::Active, QPalette::ButtonText);
                        QVERIFY(qAbs(color.red() - expected.red()) <= 2);
                        QVERIFY(qAbs(color.green() - expected.green()) <= 2);
                        QVERIFY(qAbs(color.blue() - expected.blue()) <= 2);
                    }
        }
        QVERIFY(commandIcon(CommandIcon::MonitoringSites).pixmap(48, 48).toImage() !=
                commandIcon(CommandIcon::ElevationFootprints).pixmap(48, 48).toImage());
    }
    /** @brief Existing QAction icons follow palette changes and remain enabled in menus. */
    void paletteChangesRefreshHostOwnedActions()
    {
        PaletteRestorer restore;
        QApplication::setPalette(testPalette(false));
        QAction action;
        setCommandIcon(&action, CommandIcon::MonitoringSites);
        QVERIFY(action.isIconVisibleInMenu());
        const auto before = action.icon().pixmap(32, 32).toImage();
        QSignalSpy changed(&action, &QAction::changed);
        const auto dark = testPalette(true);
        QApplication::setPalette(dark);
        QTRY_VERIFY(!changed.isEmpty());
        QVERIFY(action.icon().pixmap(32, 32).toImage() != before);
        QCOMPARE(ink(action.icon().pixmap(32, 32)).rgb(),
                 dark.color(QPalette::Active, QPalette::ButtonText).rgb());
        auto disposable = std::make_unique<QAction>();
        setCommandIcon(disposable.get(), CommandIcon::ElevationFootprints);
        disposable.reset();
        // Destroyed actions retire their observers before the next global event.
        QApplication::setPalette(testPalette(false));
        QCoreApplication::processEvents();
    }
    /** @brief Save actual light/dark renders for visual review in the repository. */
    void retainThemePreview()
    {
        PaletteRestorer restore;
        const QDir folder(QStringLiteral(SDK_DATA_ICON_VERIFICATION_DIRECTORY));
        QVERIFY(QDir().mkpath(folder.absolutePath()));
        for (const bool dark : {false, true}) {
            QApplication::setPalette(testPalette(dark));
            QImage image(224, 120, QImage::Format_ARGB32_Premultiplied);
            image.fill(dark ? QColor("#20252d") : QColor("#f5f7fa"));
            QPainter painter(&image);
            painter.drawPixmap(24, 12, commandIcon(CommandIcon::MonitoringSites).pixmap(64, 64));
            painter.drawPixmap(136, 12,
                               commandIcon(CommandIcon::ElevationFootprints).pixmap(64, 64));
            painter.setPen(QApplication::palette().color(QPalette::Active, QPalette::ButtonText));
            painter.drawText(QRect(0, 86, 112, 25), Qt::AlignCenter, "USGS sites");
            painter.drawText(QRect(112, 86, 112, 25), Qt::AlignCenter, "DEM footprints");
            painter.end();
            QVERIFY(image.save(folder.filePath(dark ? "icons-dark.png" : "icons-light.png")));
        }
    }
};
QTEST_MAIN(CommandIconTest)
#include "test_commandicons.moc"
