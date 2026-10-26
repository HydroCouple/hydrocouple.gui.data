# Data source previews

Open a georeferenced model, then select **Data → USGS sites…** or
**Data → Elevation footprints…**.

1. Choose **Current viewport** or **Loaded boundary polygon**. Polygon mode
   lists loaded feature layers and their feature IDs/names. The checkbox limits
   the list to selected boundary features. Up to 5000 boundary choices are shown.
2. Choose the elevation product when previewing terrain.
3. Select **Preview** to request catalogue metadata. The request uses the area's
   envelope; results are then intersected with the actual polygon, preserving
   holes and multiple parts. No terrain payload is downloaded by a preview.
4. Select table rows to show those results on the map. Clearing the selection
   shows the full reviewed result set. Source IDs remain stable when sorting.
5. **Save selected features as a map layer** writes GeoJSON under the project’s
   `data/external/preview-layers/`, records the reviewed area, intended catalogue query and actual response origin,
   and adds it through the GUI SDK as a normal feature layer.

Changing the chosen area or catalogue source clears the review. Preview again before
saving. The feature export uses the frozen reviewed geometry and selected IDs.

## Typed review ownership and invalidation

This package requires the installed GUI SDK **1.5** and its typed lifecycle,
view and activity services. A review belongs to an exact Ready **open session**
and load generation. Two open models may share a saved project UUID; switching
between them never transfers a review. Focusing a utility document keeps the
existing model context. Closing, failed loading or replacing that context stops
pending work and clears its review.

| Native change | Current viewport review | Loaded polygon review |
| --- | --- | --- |
| View extent, canvas CRS or resize | Clear | Keep when the chosen polygon is unchanged |
| Chosen boundary geometry, source, CRS, filter, selection or field schema | Not a viewport input | Clear and refresh boundary choices |
| Chosen boundary title, visibility or other source metadata | Not a viewport input | Conservatively clear and refresh choices |
| Chosen attachment removal or detach/reattach, even with the same native ID | Keep the viewport review | Clear; the replacement has a new runtime identity |
| Unrelated layer addition/removal/change | Refresh choices; keep the area | Refresh choices; keep the unchanged chosen attachment |
| Layer style/order, result variable/playback, dock/command or utility focus | Keep | Keep |
| Model context/load replacement or closure | Clear and cancel | Clear and cancel |

The plugin subscribes before its initial snapshot. It keeps a separate adoption
cutoff for each explicit review, so a queued change that happened before the
review began cannot clear the new review. Exact geometry, feature eligibility,
attachment identity, available source/filter/CRS metadata and the complete selected
FID fingerprint are checked again before applying results, caching, previewing,
retrying or exporting. Selected IDs are read in bounded pages; holes and multipart
geometry remain exact.

Typed notifications arrive after native changes commit. An action that races an
undelivered model revision is therefore refused conservatively, even when the
current geometry appears unchanged: a source might have been replaced, or geometry
might have changed and changed back. This can require a fresh preview when an
unrelated change is still pending. Once irrelevant notifications have been delivered,
qualified detailed or unambiguous evidence can reconcile and preserve the review.
A posted callback alone does not prove dispatch is complete inside a nested event
loop; broad layer/model notices cannot establish that evidence. Preserved polygon
previews redraw after a safe viewport/CRS reconciliation. A late reply or job completion
cannot restore an invalidated review or write its cache/export.

These rules cover the current **catalogue metadata** package. Terrain payloads,
observation imports and design storms remain later DS2–DS4 packages with their own
invalidation and qualification gates.

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
