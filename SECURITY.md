# Security Policy

This policy follows the OpenSWMM repositories’ private-reporting convention.
It applies to `openswmm.gui.data`, the native Data Sources extension for the GUI.

## Supported Versions

This repository is in initial development. Security fixes target the current
working development version. No published release is recorded in
[CHANGELOG.md](CHANGELOG.md). Support for a release line will be stated when it
is published; experimental builds are not a long-term support commitment.

## Reporting a Vulnerability

**Do not open a public issue containing vulnerability details.** Email
[security@hydrocouple.org](mailto:security@hydrocouple.org), the private reporting
address used by the OpenSWMM Gymnasium project. If that channel is unavailable,
contact [support@hydrocouple.org](mailto:support@hydrocouple.org).

If private GitHub security advisories are enabled for a future hosted repository,
its Security tab may also be used. No hosted Data repository or enabled advisory
workflow is assumed by this document.

Please include, where possible:

- The affected version or commit and the GUI/SDK, Qt, GDAL and operating-system versions.
- The affected code path: catalogue parsing, network handling, cache/export,
  plugin lifecycle, documentation or build tooling.
- A minimal recorded catalogue or boundary fixture and reproduction steps.
- Observed and expected behavior, impact and any proof of concept.
- A safe way to contact you for clarification.

Remove unrelated private model data, credentials and personal information from
reports. Send sensitive reproductions through the private channel.

## What to Expect

Following the other OpenSWMM repositories, maintainers target:

- Acknowledgement within **3 business days**.
- Initial assessment within **10 business days**.
- A fix or mitigation plan within **30 days** of confirmation for high/critical
  issues, with lower severities scheduled for maintenance work.
- Coordinated disclosure with a fix or mitigation and reporter credit, unless
  the reporter requests otherwise.

These are response targets. If there is no acknowledgement, resend privately and
contact the maintainer listed in [AUTHORS.md](AUTHORS.md).

## Scope

In scope are the native plugin under `src/`, bounded catalogue decoding,
network/cache/export paths, configuration and repository tooling. Native plugins
run inside the GUI process; a failure can affect the host application.

Report GUI host or SDK defects through the GUI maintainer’s private contact and
engine defects through the
[engine security channel](https://github.com/HydroCouple/openswmm.engine/security/advisories/new).
Third-party Qt, GDAL or theme defects should be coordinated with their upstream
maintainers; identify the relevant dependency version in the report.

## Handling Untrusted Inputs

Catalogue content and local response files are treated as data, not executable
code. Network previews use the host’s HTTPS/offline policy, response bounds and
cancellation. These controls do not isolate native extensions from the host
process. Install native plugins only from sources you choose to trust.
