# OpenSWMM Data Sources

An independently built native plugin for `openswmm.gui`, consuming the installed
GUI SDK 1.5, matching Qt, and GDAL. The SDK and plugin host remain in the GUI repo.

The source repository is [HydroCouple/hydrocouple.gui.data](https://github.com/HydroCouple/hydrocouple.gui.data).
The plugin identity remains `openswmm.gui.data`; the hosted repository name does
not change its GUI registration or installed package identity.

This first consumer implements catalogue review: monitoring-site points and 3DEP
product footprints, a current-viewport or loaded-polygon area, exact intersection
filtering, selected-record map previews, a project-local metadata cache, and explicit
GIS export with the reviewed area and source provenance. It does not download the
advertised terrain tiles or import observation time series in this work package.
The saved program adds those payload workflows after catalogue review.

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
The **Data** ribbon opens the **Data sources** dock. See [the guide](docs/guide.md).
All new C++ interfaces and source files use Doxygen comments. The plugin requires
SDK 1.5 typed lifecycle, view and UI activity services. Review ownership uses the
per-open session and load generation, including when two models share a saved UUID.
The guide documents which changes clear a review and how pending changes are fenced.

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
