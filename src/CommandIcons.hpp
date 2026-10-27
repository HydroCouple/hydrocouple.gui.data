/**
 * @file CommandIcons.hpp
 * @brief Self-contained, palette-aware icons for the Data catalogue commands.
 * @details SVG glyphs are rendered into ordinary Qt pixmap icons. No icon engine
 * implemented by the plugin escapes into host-owned actions or ribbon widgets.
 * @author Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license GPL-3.0-or-later
 */
#pragma once
#include <QIcon>

class QAction;

namespace OpenSWMM::Data
{
/** @brief Distinct, non-branded glyphs for the two metadata retrieval workflows. */
enum class CommandIcon {
    MonitoringSites,    ///< Water-level staff and river waves identify monitoring locations.
    ElevationFootprints ///< Mountain profile over raster cells identifies elevation products.
};
/** @brief Render an embedded SVG using current application palette roles.
 * @param[in] kind Monitoring locations or elevation products.
 * @return Ordinary owning Qt icon with normal, active, selected and disabled images.
 * @note Call on the GUI thread after QApplication construction. Images include
 * 16, 24, 32, 48 and 64 logical pixels at device scales one and two. */
QIcon commandIcon(CommandIcon kind);
/** @brief Assign a command icon and refresh it when the application palette changes.
 * @param[in,out] action Borrowed action receiving an icon and menu-icon visibility.
 * @param[in] kind Monitoring locations or elevation products.
 * @note Call once per newly constructed action. Its palette observer is owned
 * by the action and removed automatically during host-owned action teardown. */
void setCommandIcon(QAction *action, CommandIcon kind);
}
