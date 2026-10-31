# OpenSWMM Data Sources

An independently built native plugin for `openswmm.gui`, consuming the installed
GUI SDK 1.11, matching Qt with Qt SVG, and GDAL. The SDK and plugin host remain in the GUI repo.

The source repository is [HydroCouple/hydrocouple.gui.data](https://github.com/HydroCouple/hydrocouple.gui.data).
The plugin identity remains `openswmm.gui.data`; the hosted repository name does
not change its GUI registration or installed package identity.

This plugin previews USGS monitoring locations and 3DEP elevation products in
the current viewport or an exact loaded polygon. It provides temporary labeled
gage features, catalogue search and optional attribute inspection, then downloads
selected observations for an inclusive date range or selected elevation tiles.
USGS output includes observation CSV, raw response pages and provenance; DEM
output includes original GeoTIFFs and a mosaic cropped/masked to the reviewed
area, including holes and multipart polygons. The derived DEM is added to
the map. Successful nonempty USGS downloads are automatically added as native
Time Series layers. Right-click a series to plot it or choose **Use in model…**
for an explicit prepared model copy; downloading never changes model forcing.
Published layers continue working when the Data plugin is disabled. Design storms
remain separate work.

**Clear preview** removes the temporary site layer or elevation footprints and
catalogue rows, including a preview still being fetched. Saved downloads and
imported layers remain available. Preview, download and import stages use the
GUI's shared status bar and native **Logs** panel. Each tool's **Results** and
**Messages** show its execution records, and the toolbox's **History** tab shows
runs across tools with their start/end timestamps and elapsed time. Saved files
and layer references remain available for inspection after a panel is hidden.
History lasts for the current GUI session. Completed download folders and their
manifests remain available after restart.

```sh
cmake -S . -B build \
  -DCMAKE_PREFIX_PATH="/path/to/gui-sdk;/path/to/matching/Qt;/path/to/GDAL/prefix"
cmake --build build --config Release
ctest --test-dir build --output-on-failure
```

For vcpkg, pass its toolchain and the same target triplet used by the GUI.
The included manifest requests GDAL; Qt and the GUI SDK come from the matching
application kit. An existing GUI dependency prefix can be reused with
`VCPKG_MANIFEST_MODE=OFF` and `VCPKG_INSTALLED_DIR` pointing at that prefix's parent.

Add the built plugin directory in **Tools → Plugins…**, then restart the GUI.
Use the **Analysis Toolbox** button on the **View** ribbon. Under **Data sources →
Download data**, **USGS time series…** and **Elevation raster…** each open their own
dockable tool panel. The command palette provides the same commands.

Ribbon placement is optional. In **Preferences → Extensions — Data sources → Ribbon placement**, choose
**Toolbox only** (the default), **Shared Data tab**, or **Separate Data tab** and apply
the preference. The shared tab can contain command groups from several plugins.
Changing placement applies immediately and keeps the tool panels and previews available.
The toolbox's graphical **Tags** checklist filters tools. Both have **Download** and
**USGS** tags; **Time series** narrows to gages, and **Raster** narrows to elevation.
All selected tags must match. Opening from the tree, ribbon or command palette
raises the same tool panel. Both previews can coexist. Clearing or hiding one
panel preserves the other panel's selection and download controls. The
monitoring-gage and elevation/raster icons follow the GUI's light/dark palette and
remain visible in command menus. See [the guide](docs/guide.md).
After **Preview**, **USGS gages (preview)** or **DEM tiles (preview)** provides the
usual map-layer attribute table, query, selection and styling controls. Select a
Data catalogue row to inspect every saved property, or use the Data attribute
filter to narrow the records supplied to the map. Selection is shared: the map
**Select** tool and the layer's attribute table select Data rows, and Data rows
select the map features. The advanced **Save selected sites/footprints as a map
layer** action saves the selected, visible Data rows. Sorting Data rows preserves
an unchanged native preview population; changing that population resets its
native query and styling.
The preview is omitted from project saves; an explicit feature export is persistent.
The area is frozen at Preview: panning, zooming or resizing the map afterwards
keeps the previewed rows, Download and feature export; Preview again to search the
new view. Attributes describe the recorded catalogue response. The download
controls choose the actual observation period and measurement; the dates are
limited to the selected sites' published record. An empty response does not imply
zero flow.
Use **Details and advanced options** for the full saved-property table and filters.
All new C++ interfaces and source files use Doxygen comments. The plugin requires
SDK 1.11 toolbox, shared ribbon, tool-run history, independent tool previews, typed lifecycle, view, UI activity, native time-series import, shared
progress and preview-layer services. Review ownership uses the
per-open session and load generation, including when two models share a saved UUID.
The guide distinguishes retained display from export authority and documents how
pending native changes are fenced.

Development plans and verification data remain local under `workplans/` and
`tests/verification/`; do not commit them.

OpenSWMM Data Sources is distributed under the **GNU General Public License,
version 3 or later** (`GPL-3.0-or-later`), matching `openswmm.gui`. See [LICENSE](LICENSE).

Generate the reference using the same convention as the GUI:

```sh
cd docs
doxygen Doxyfile
```

Open `docs/html/index.html` from the repository root. After configuring CMake,
`cmake --build build --target doc` runs the same documentation build. The bundled
Doxygen Awesome CSS, JavaScript, logo and manual styling match `openswmm.gui`;
upstream theme notices are preserved in the assets and [NOTICE](NOTICE).

Contribution and community guidance: [CONTRIBUTING](CONTRIBUTING.md),
[Code of Conduct](CODE_OF_CONDUCT.md), [AUTHORS](AUTHORS.md),
[SECURITY](SECURITY.md), and [PRIVACY](PRIVACY.md). Contributors sign the
[Individual CLA](CLA.md); organizations also complete the [Corporate CLA](CCLA.md).
The [signatory register](CLA-SIGNATORIES.md) records verified agreements, and
[RELEASING](RELEASING.md) describes release checks. See [CHANGELOG](CHANGELOG.md)
for the current unreleased scope. Publishing this source repository does not
designate development package `0.1.0` as a release. Check the repository’s Actions
results for the published revision; release qualification is recorded separately.
