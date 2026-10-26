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

Changing the viewport in viewport mode, the chosen polygon, the boundary source,
its filters/selection, or catalogue source clears the review. Preview again before
saving. The feature export uses the reviewed geometry and selected IDs.

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
