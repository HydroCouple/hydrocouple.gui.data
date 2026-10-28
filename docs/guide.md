# Data previews and downloads

Open a georeferenced model, then open the **Data** ribbon and choose
**USGS time series…** or **Elevation raster…**. Each extension can register its own
ribbon tabs; Data uses one named tab. A
monitoring-gage icon identifies USGS sites; an elevation/raster icon identifies
DEM product footprints. Both icons adapt to the application palette and menu or
ribbon scale. They are generic workflow symbols, rather than agency logos.

1. Choose **Current viewport** or **Loaded boundary polygon**. Polygon mode
   lists loaded feature layers and their feature IDs/names. The checkbox limits
   the list to selected boundary features. Up to 5000 boundary choices are shown.
2. Choose the elevation product when previewing terrain.
3. Select **Preview** to request catalogue metadata. The request uses the area's
   envelope; results are then intersected with the actual polygon, preserving
   holes and multiple parts. No terrain payload is downloaded by a preview.
4. Monitoring locations appear in a temporary **USGS gages (preview)** feature
   layer, as dots with their catalogue names beside them.
   All results matching the local filter stay visible when table rows are selected.
   Source IDs remain stable when sorting.
5. Select the catalogue rows to download. The small search box filters the returned
   sites or tiles. Open **Details and advanced options** when you need property-specific
   comparisons, the complete saved-attribute table, cached responses or metadata export.
6. For USGS, choose the measurement, daily or continuous values, and the inclusive
   start/end dates. Choose **Download selected observations…** and an output folder.
7. For terrain, choose **Download selected raster tiles…** and an output folder.
   The plugin downloads the selected source GeoTIFFs, then creates a mosaic cropped
   and masked to the reviewed area, preserving polygon holes and multiple parts.
   The completed derived DEM is added to the original ready model when it remains current.

Choose **Clear preview**, beside **Preview**, to remove elevation footprints or
the temporary USGS site layer and clear the catalogue rows and details. It can
also stop a preview that is still fetching or parsing: a late response cannot
restore the cleared graphics. It does not delete saved downloads, imported Time
Series collections or loaded rasters. The control is disabled while a payload
download or native collection import is active; use **Cancel** to stop that work
before clearing the preview.

**Save selected sites/footprints as a map layer** under the advanced options is a
separate metadata export. It writes GeoJSON under the project's
`data/external/preview-layers/`, retaining the reviewed area, intended catalogue
query, actual response origin and original properties. It does not download observations
or elevation pixels. Only selected rows still visible in the filter are used by
metadata exports and payload downloads.

Changing the chosen area controls or catalogue source clears the review. Moving
the viewport after a completed viewport preview keeps the previous dots and
attributes available for inspection, while disabling feature export. Preview the
new area before saving. The feature export uses the frozen reviewed geometry and
selected IDs.

## Temporary USGS feature layer

The USGS preview is a normal feature layer for map inspection: use the layer's
ordinary controls to open its attribute table, query or select monitoring
locations, and change its point or label styling. It is temporary and is omitted
from project saves. Closing the model or clearing the Data preview removes it. Explicitly
save features when a persistent project layer is needed; previewing still fetches
catalogue metadata only.

Native layer queries and selections are independent of the Data dock's local
filter and selected rows. The advanced **Save selected sites as a map layer** action saves selected,
visible Data catalogue rows, rather than the native map layer's selection. A
native query may further restrict what the map shows without narrowing the Data
table. Sorting or selecting Data rows preserves the native preview's query,
selection and styling when the displayed population is unchanged. Changing the
Data-filtered population replaces the preview and resets those native settings.

USGS preview-layer updates remain observable through the GUI's usual layer events.
The Data dock briefly disables its save action while the update's detailed source
notifications are reconciled. Publishing a temporary layer does not itself grant
authority to save under a changed area or boundary. Compatible older hosts may
show the catalogue geometry as an overlay instead of exposing the native feature
layer. Elevation footprints continue to use the geometry preview in this package.

## Inspecting and filtering saved attributes

The attribute selector lists the exact saved property names present in the returned
records, plus `catalogue_id` and `catalogue_title`. Choose **All attributes** to
search any saved value, or one property to restrict the comparison. The inspector
follows the current table location and clears when that location is filtered out.

| Comparison | Meaning |
| --- | --- |
| Contains | Literal, case-insensitive text search; an empty value shows all records |
| Equals | Case-insensitive equality of the displayed saved value |
| At least / At most | Inclusive numeric comparison; numeric source strings are accepted, invalid or non-finite numbers do not match |
| Has value | The chosen property exists and is not JSON null |
| Is missing | The chosen property is absent or JSON null |

Nested arrays and objects are shown as JSON and can be searched as plain text.
Text is not interpreted as a regular expression or executable expression. Filters
operate only on the already returned, exact-area catalogue records: they neither
fetch additional locations nor change the reviewed query. Filtering is also
available on a retained, stale viewport preview, but cannot restore its permission
to export. A fresh preview must establish that permission again.

## Observation files and download period

The USGS period includes both chosen dates. Requests use the modern OGC daily or
continuous collections, a selected monitoring-location identity and the selected
five-digit parameter code. Daily dates and continuous timestamps, including their
original UTC offsets, are preserved in output. Daily values may include several
statistic IDs; their IDs and time-series IDs distinguish those observations.

A fresh subfolder is created for each transfer; existing files are never overwritten.
Observation CSV files retain values, units, qualifiers, approval status, location,
parameter and time-series identities, with the full source properties available
alongside the tabular values. Raw response pages and a provenance manifest retain
the query, requested period and reviewed area. Missing observations are not replaced
with zeros. A location may have no observations for a chosen measurement or period.
Successful nonempty downloads also write `timeseries-import.json`. Its versioned,
source-neutral column mappings retain each source series/statistic, the reviewed
station geometry and full station attributes. The GUI imports the complete
collection into `series.gpkg` and automatically adds a persistent **Time Series**
layer. The import is cancellable; raw CSV/JSON files remain available if cancelled.
A failed or partial transfer creates no automatic collection. Empty periods create
no empty layer; all-null series remain visible with their missing samples.

Right-click a layer or series to plot or inspect observations. Located stations
also support map picking; unlocated series remain usable from the layer tree.
**Use in model…** explicitly prepares a model-owned copy with reviewed units,
time conventions and missing-value policy. Downloading and plotting do not change
model forcing. Published native layers survive plugin removal and project reopening.
Conflicting duplicate samples stop import rather than silently retaining the first.
The output does not apply implicit unit/timezone conversion.

Transfers allow up to 100 selected records per run, 256 pages and 200,000
observations per site. Each response page is bounded to 20 MiB and raw data for
one site to 256 MiB. A source raster tile is bounded to 2 GiB. A limit failure
asks for a smaller selection or shorter period; it never reports truncated data
as a complete download.

Transfers follow pagination, host HTTPS/offline policy and cancellation. The
progress message reports the current stage; **Cancel** appears only during pending
work. A failure or cancellation does not publish a partially written data file;
the transfer result identifies any already completed files.

## Shared progress and activity messages

Preview requests, cached or saved catalogue review, observation/raster downloads,
and native collection import use the GUI's shared status bar. An unknown response
size appears as a busy indicator; measured work uses a percentage and stage
message. A running simulation or project-opening operation has priority in that
bar. Data work remains tracked while another operation is displayed, and resumes
its place when that operation finishes. Concurrent extension operations share a
summary based on the slowest known progress, or busy while any total is unknown.

Open the application's **Logs** panel for stage changes, clear/export actions and
terminal success, failure or cancellation. Messages identify the Data extension
and are also retained in its **Output** tab. A saved download is distinct from
successful map publication: **Saved, not added** identifies retained assets when
the original model is no longer available for import. Cancelling or clearing one
operation does not erase another operation's status. Use the Data dock's
**Cancel** control for pending Data work.

## Elevation raster files

The selected National Map products are downloaded as original GeoTIFF tiles. A
GDAL processing job mosaics them and uses the exact reviewed WGS84 polygon as
a cutline. The derived `area-dem.tif` uses Float32 values and NaN NoData outside
the area and inside holes; a legitimate zero elevation remains valid. Original
tiles, the cutline and the manifest remain beside the result for review. No
vertical-unit conversion is inferred. Check source units before using the DEM
for model elevations. Downloads can be large because complete selected source
tiles are retrieved before cropping.

A server response must pass HTTP and raster validation before being accepted as
a tile. Only complete, validated derived rasters are offered to the original
model's map; saved files remain available if that model has closed.

## Typed review ownership and invalidation

This package requires the installed GUI SDK **1.7**, its typed lifecycle,
view/activity and native observation-import services, and its separately
discoverable `IProgressV1` shared progress service. A review belongs to an exact Ready **open session**
and load generation. Two open models may share a saved project UUID; switching
between them never transfers a review. Focusing a utility document keeps the
existing model context. Closing, failed loading or replacing that context stops
pending work and clears its review.

Returning from a utility tab preserves the same model's catalogue selection and
download controls. A viewport notification with the same reviewed area and CRS
does not replace the preview layer. A completed review also survives a transient
tab-layout resize that restores its original area before revision reconciliation;
write actions stay disabled during that transition. A settled change in area or
CRS revokes the review and cannot later revive it without a fresh Preview.
Selecting catalogue rows while an unchanged
source has a queued revision temporarily disables write actions; qualified
notification delivery restores them. A relevant source change still clears the
review, even if its current geometry happens to match the earlier snapshot.

| Native change | Current viewport review | Loaded polygon review |
| --- | --- | --- |
| View extent, canvas CRS or resize | Keep the completed display for inspection; revoke export authority | Keep when the chosen polygon is unchanged |
| Chosen boundary geometry, source, CRS, filter, selection or field schema | Not a viewport input | Clear and refresh boundary choices |
| Chosen boundary title, visibility or other source metadata | Not a viewport input | Conservatively clear and refresh choices |
| Chosen attachment removal or detach/reattach, even with the same native ID | Keep the viewport review | Clear; the replacement has a new runtime identity |
| Unrelated layer addition/removal/change | Refresh choices; keep the area | Refresh choices; keep the unchanged chosen attachment |
| Layer style/order, result variable/playback, dock/command or utility focus | Keep | Keep |
| Local attribute filter or table sorting/selection | Keep; supply filtered records to the map | Keep; supply filtered records to the map |
| Model context/load replacement or closure | Clear and cancel | Clear and cancel |

The plugin subscribes before its initial snapshot. It keeps a separate adoption
cutoff for each explicit review, so a queued change that happened before the
review began cannot clear the new review. Exact geometry, feature eligibility,
attachment identity, available source/filter/CRS metadata and the complete selected
FID fingerprint are checked again before adopting new results, caching,
retrying or exporting. An older completed display may remain visible after viewport
changes, but never grants authority to those actions. Selected boundary IDs are read in bounded pages; holes and multipart
geometry remain exact.

Typed notifications arrive after native changes commit. An action that races an
undelivered model revision is therefore refused conservatively, even when the
current geometry appears unchanged: a source might have been replaced, or geometry
might have changed and changed back. This can require a fresh preview when an
unrelated change is still pending. Once irrelevant notifications have been delivered,
qualified detailed or unambiguous evidence can reconcile and preserve the review.
A posted callback alone does not prove dispatch is complete inside a nested event
loop; broad layer/model notices cannot establish that evidence. Preserved polygon
previews redraw after a safe viewport/CRS reconciliation. Retained viewport graphics
are redrawn in the current map CRS without making their old area current. A late reply or job completion
cannot restore an invalidated review or write its cache/export.

Payload transfers start only from a current review. Changing the source, chosen
area or model cancels pending transfers and native imports. Completed assets stay
on disk. Publication targets the original Ready session and load generation, never
a newly active model. If publication is unavailable, the panel reports **Saved,
not added** and the saved descriptor can be imported later. Assigning observations
to model gages or inflows remains an explicit **Use in model…** operation. Design
storms are a later package.

Network access follows the GUI's HTTPS/offline policy. **Cancel** stops requests,
scheduled retries and parsing jobs. Catalogue responses are bounded to 20 MiB;
preferences set a preview limit of 1–5000 records. A notice identifies further
server results instead of treating the first page as a complete national inventory.

Successful raw responses and request provenance remain under
`data/external/catalogues/`. **Use cached preview** requires the same source,
request and exact area, and works while offline. **Open saved catalogue…** reviews
an explicitly selected recorded JSON response against the current area without
network access. Exports identify saved/cache inputs by path and preserve recorded request
metadata when available; an intended query is not represented as a fetched response.
Save the model in a project folder before caching or exporting.

TNM coverage rectangles describe catalogue extents; they do not promise valid
elevation pixels everywhere within the rectangle. Areas crossing the date line
are refused in this revision so a preview cannot silently widen into a global query.

The live contract is based on the official
[USGS OGC API](https://api.waterdata.usgs.gov/docs/ogcapi/) and
[TNM catalogue](https://tnmaccess.nationalmap.gov/api/v1/datasets).

@par License
OpenSWMM Data Sources uses GNU GPL version 3 or later (`GPL-3.0-or-later`),
matching the GUI. The full terms are in the repository’s LICENSE file.
