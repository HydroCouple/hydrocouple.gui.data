/**
 * @file CommandIcons.cpp
 * @brief Render plugin-owned SVG glyphs with current Qt palette contrast and scale.
 * @author Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license GPL-3.0-or-later
 */
#include "CommandIcons.hpp"
#include <QAction>
#include <QEvent>
#include <QFile>
#include <QGuiApplication>
#include <QPainter>
#include <QPalette>
#include <QPixmap>
#include <QPointer>
#include <QSvgRenderer>

namespace OpenSWMM::Data
{
namespace
{
/** @brief Refresh one action from the public application-palette event.
 * @details The action owns this observer. Qt removes destroyed event filters
 * automatically, so no plugin callback remains after contribution teardown. */
class CommandIconPaletteObserver final : public QObject
{
public:
    /** @brief Retain a guarded target and register for current application events.
     * @param[in] action Owning target action.
     * @param[in] kind Monitoring locations or elevation products. */
    CommandIconPaletteObserver(QAction *action, CommandIcon kind)
        : QObject(action), mAction(action), mKind(kind),
          mPaletteKey(QGuiApplication::palette().cacheKey())
    {
        qGuiApp->installEventFilter(this);
    }

protected:
    /** @brief Re-render when the application palette changes without consuming the event.
     * @param[in] watched Application or widget receiving the palette event.
     * @param[in] event Borrowed palette or unrelated application event.
     * @return False to retain the application's normal event handling. */
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        Q_UNUSED(watched);
        if (event->type() == QEvent::ApplicationPaletteChange && mAction) {
            const auto key = QGuiApplication::palette().cacheKey();
            if (key != mPaletteKey) {
                mPaletteKey = key;
                mAction->setIcon(commandIcon(mKind));
            }
        }
        return false;
    }

private:
    QPointer<QAction> mAction; ///< Guarded host-owned target.
    CommandIcon mKind;         ///< Glyph refreshed from the current palette.
    qint64 mPaletteKey;        ///< Re-render once when one palette change reaches several widgets.
};
}
QIcon commandIcon(CommandIcon kind)
{
    const auto path = kind == CommandIcon::MonitoringSites
                          ? QStringLiteral(":/openswmm/data/icons/usgs-gage.svg")
                          : QStringLiteral(":/openswmm/data/icons/dem.svg");
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    const auto source = file.readAll();
    const auto palette = QGuiApplication::palette();
    QIcon icon;
    for (const auto mode : {QIcon::Normal, QIcon::Active, QIcon::Selected, QIcon::Disabled}) {
        const auto color =
            mode == QIcon::Disabled   ? palette.color(QPalette::Disabled, QPalette::ButtonText)
            : mode == QIcon::Selected ? palette.color(QPalette::Active, QPalette::HighlightedText)
                                      : palette.color(QPalette::Active, QPalette::ButtonText);
        auto svg = source;
        svg.replace("#777777", color.name(QColor::HexRgb).toLatin1());
        QSvgRenderer renderer(svg);
        if (!renderer.isValid())
            return {};
        for (const int edge : {16, 24, 32, 48, 64})
            for (const qreal scale : {1., 2.}) {
                QPixmap image(QSize(edge, edge) * scale);
                image.setDevicePixelRatio(scale);
                image.fill(Qt::transparent);
                QPainter painter(&image);
                renderer.render(&painter, QRectF(0., 0., edge, edge));
                painter.end();
                icon.addPixmap(image, mode, QIcon::Off);
            }
    }
    return icon;
}
void setCommandIcon(QAction *action, CommandIcon kind)
{
    if (!action)
        return;
    action->setIcon(commandIcon(kind));
    action->setIconVisibleInMenu(true);
    if (qGuiApp)
        new CommandIconPaletteObserver(action, kind);
}
}
