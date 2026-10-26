# Releasing openswmm.gui.data

This repository is in initial development at version 0.1.0. No release, public
repository URL, DOI or signing automation is established by these files.

## Prepare a release

1. Review [CONTRIBUTING](CONTRIBUTING.md), the [Individual CLA](CLA.md), and any
   applicable [Corporate CLA](CCLA.md). Verify agreement records; never infer
   that a signature in another repository covers this one.
2. Confirm the supported GUI SDK, compiler, architecture and Qt kit. Use the
   installed SDK package and normal project dependency prefixes. Publish any
   required GUI SDK changes before expecting this repository's CI to pass.
3. Build the plugin and run `ctest --test-dir build --output-on-failure` with
   offline catalogue fixtures. Exercise it inside the matching GUI on every
   supported operating system, including preview invalidation, holes/parts,
   selection/export, cancellation, offline policy, and clean shutdown.
4. Run `cd docs` and `doxygen Doxyfile`; verify zero warnings, navigation,
   copied assets, code/reference links and light/dark styling. The generated
   site is `docs/html/`. Theme assets copied from the GUI retain upstream notices.
5. Keep the project version in `CMakeLists.txt`, runtime `ExtensionInfo` in
   `src/Extension.cpp`, `vcpkg.json`, `docs/Doxyfile`, and `CITATION.cff` aligned.
   Summarize the shipped behavior and qualification limits in [CHANGELOG](CHANGELOG.md).
   `cliff.toml` supports the same Conventional Commits/git-cliff grouping as the GUI.
6. Review [LICENSE](LICENSE), [NOTICE](NOTICE), [AUTHORS](AUTHORS.md),
   [SECURITY](SECURITY.md), and [PRIVACY](PRIVACY.md). Include license and third-party
   notices with binary distributions, together with the matching source and
   other material required by the applicable licenses.
7. Exclude build outputs, local plans, verification evidence, settings and cache
   data. No user model or private site/boundary data belongs in a release bundle.
8. Have the Technical Manager review release artifacts, sign/tag the approved
   version, and publish using the project's agreed release process. Record the
   public URL and archival identifier only after they exist.

## Repository workflows

The included workflows validate documentation, build the installed-SDK consumer
and offline catalogue tests on Linux, and provide CodeQL/Scorecard scaffolding.
They have not run on GitHub for this local repository. They do not configure
branch protection, a CLA bot, public hosting, a Pages deployment or a release.

The C++ workflow stages the SDK from the GUI reference `swmm6_gui` by default;
manual runs can choose another ref. That ref must already supply SDK 1.1. A
SDK-only package identifies the public Qt boundary but does not qualify a binary
against a complete GUI/engine kit. Full GUI and Windows/macOS acceptance remain
release gates.

Documentation is uploaded as a review artifact. Publishing it requires a separate,
explicitly configured deployment. Scorecard results are kept as workflow artifacts;
public Scorecard publication is disabled.
