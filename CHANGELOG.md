# Changelog

User-visible changes are recorded here using the OpenSWMM repositories’
conventional-commit and release-note conventions. This initial working tree has
not been published as a release. Version `0.1.0` is the development package
version, not evidence of a release tag.

## [Unreleased]

- Share preview selection with the map: the select tool and native attribute table select Data rows, and Data rows select native preview features. DEM tile footprints now use the selectable native preview layer. Requires GUI SDK 1.8.
- Require SDK 1.7 and automatically add completed USGS observations as native time-series layers, with retained station geometry and provenance.
- Keep native import cancellable and bound to the originating model; report saved files separately from layer publication.
- Detect conflicting duplicate observations before CSV deduplication and preserve their raw responses.

### Changed

- Keep all filtered monitoring-location dots visible independently of row selection,
  with station labels and inspection-only retention after viewport changes.
- Require GUI SDK 1.7 lifecycle/view/activity, native time-series import and shared
  progress services, with exact per-open model/load ownership.
- Fence asynchronous preview publication and export against runtime boundary changes and pending native revisions.
- Keep unchanged reviews across delivered unrelated playback, layer presentation and utility-focus changes.

### Added

- **Clear preview** removes DEM footprints or temporary USGS site layers and stops
  pending preview work while retaining saved downloads and imported layers.
- Shared status-bar progress and timestamped native Logs for previews, downloads,
  processing, imports and exports, with stages and completion, failure or cancellation
  outcomes. Byte updates do not create a log entry for every packet.

- Actual USGS daily/continuous observation downloads with an inclusive date range,
  measurement selection, CSV/raw responses and provenance.
- Streamed DEM GeoTIFF downloads and cancellable mosaic/crop/mask processing for
  the exact reviewed viewport or loaded polygon, including holes and multiple parts.
- A simpler source-specific download flow with optional details and advanced controls.

- Embedded, theme-aware gage and DEM catalogue icons for menu and ribbon commands.

- Session-only USGS preview feature layers with native attribute tables, queries,
  selection, styling and labels; temporary layers stay out of project saves.

- Complete saved-property inspection for the current monitoring location and local
  text, exact-value, numeric and missing-value filters synchronized with the map.
- Canonical source repository, citation/homepage metadata and runtime project link at
  [HydroCouple/hydrocouple.gui.data](https://github.com/HydroCouple/hydrocouple.gui.data).

- Independently built Qt plugin using the installed GUI SDK (initially 1.1; now 1.7).
- Data ribbon, catalogue-preview dock and preview-limit preferences.
- Offline public-SDK preview lifecycle, stale-completion and invalidation acceptance tests.
- USGS monitoring-site and National Map elevation-footprint previews using the
  current viewport or an exact loaded boundary polygon, preserving holes and parts.
- Filtered catalogue overlays and explicit selected-record GeoJSON export with review provenance.
- Project-local catalogue cache, offline saved-response review and cancellation.
- Offline catalogue/geometry tests and Doxygen source/API documentation.
- GPL-3.0-or-later licensing matching the GUI.
- GUI-aligned documentation theme and repository contribution, security,
  privacy and contributor-agreement conventions.

### Scope

Automatic assignment of downloaded observations to model objects and design-storm
generation remain subsequent work packages. Downloaded native collections preserve
original units and time representations and can be plotted immediately. Preparing
and assigning observations as model inputs uses the GUI's explicit **Use in model**
workflow.
