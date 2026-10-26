# Contributing to openswmm.gui.data

Thank you for your interest in contributing to **openswmm.gui.data**! This document outlines the governance model, contribution workflow, and standards that keep the project healthy, reproducible, and scientifically rigorous. Please read it carefully before opening an issue, submitting a pull request, or proposing a major change.

The standalone Data Sources repository is hosted at
[HydroCouple/hydrocouple.gui.data](https://github.com/HydroCouple/hydrocouple.gui.data).
Its plugin identity remains `openswmm.gui.data`. Automated review, repository
protections and archival releases require separate configuration and verification.
Contacts and contributor agreements below remain applicable.

---

## Table of Contents

1. [Community Governance](#1-community-governance)
2. [Repository & Technical Management](#2-repository--technical-management)
3. [Succession & Delegation](#3-succession--delegation)
4. [Licensing, Intellectual Property & CLA](#4-licensing-intellectual-property--cla)
5. [Author Acknowledgment](#5-author-acknowledgment)
6. [How to Cite openswmm.gui.data](#6-how-to-cite-openswmmguidata)
7. [Versioning Strategy](#7-versioning-strategy)
8. [Deprecation & Backward Compatibility Policy](#8-deprecation--backward-compatibility-policy)
9. [Branching Model](#9-branching-model)
10. [Bug Fixes & Small Improvements](#10-bug-fixes--small-improvements)
11. [Pull Request Process](#11-pull-request-process)
12. [Continuous Integration & Automated Testing](#12-continuous-integration--automated-testing)
13. [Issue Reporting](#13-issue-reporting)
14. [Review Timeline Expectations](#14-review-timeline-expectations)
15. [Major Project Formulation Changes](#15-major-project-formulation-changes)
16. [Conflict of Interest in Reviews](#16-conflict-of-interest-in-reviews)
17. [Regression & Validation Testing](#17-regression--validation-testing)
18. [Documentation Standards](#18-documentation-standards)
19. [Dependency Evaluation Policy](#19-dependency-evaluation-policy)
20. [Security Vulnerability Reporting](#20-security-vulnerability-reporting)
21. [Roadmap & Prioritization](#21-roadmap--prioritization)
22. [Code of Conduct](#22-code-of-conduct)

---

## 1. Community Governance

openswmm.gui.data is a community-driven open source project. All contributors are welcome regardless of affiliation, background, or experience level. The community operates on a model of open discussion, merit-based evaluation, and transparent decision-making. Key decisions affecting the project direction are made openly on GitHub, and all community members are encouraged to participate.

---

## 2. Repository & Technical Management

**Repository and technical management is the responsibility of the Technical Manager (currently [@cbuahin](https://github.com/cbuahin)).**

The Technical Manager is responsible for:

- Maintaining the integrity and health of the `main`, `dev`, and `experimental` branches.
- Setting and enforcing coding standards, testing requirements, and documentation guidelines.
- Triaging issues and pull requests in a timely manner.
- Making final decisions on merges, releases, and branch management.
- Coordinating with the community on roadmap items and governance questions.

Community members who wish to take on elevated responsibilities (e.g., becoming a recurring reviewer or branch maintainer) may express interest by opening a discussion on GitHub.

---

## 3. Succession & Delegation

The Technical Manager role is a single point of authority, which must be protected against absence or unavailability.

- In the event the Technical Manager is unavailable for an extended period (e.g., travel, illness, or leave), they may designate a **Temporary Delegate** with equivalent merge and release authority. The delegation will be announced publicly in GitHub Discussions.
- If the Technical Manager steps down permanently, the outgoing manager will nominate a successor from the active contributor community. The nomination is subject to a community feedback period of at least two weeks before taking effect.
- In the absence of a named delegate and in urgent situations (e.g., a critical security fix), any two senior community reviewers may jointly approve and merge a patch to `main`, with a written rationale posted to GitHub Discussions immediately afterward.

---

## 4. Licensing, Intellectual Property & CLA

openswmm.gui.data is released under the **GNU General Public License version 3 or later (GPL-3.0-or-later)**.

All contributors must sign the project **Contributor License Agreement (CLA)** before their pull request can be merged. The CLA is detailed in [CLA.md](./CLA.md). Key points:

- **You retain your copyright.** The CLA grants a license; it does not transfer ownership.
- **Broad license grant.** You grant the Technical Manager a perpetual, irrevocable license to use, distribute, and **relicense** your contribution (e.g., for future dual open-source/commercial licensing).
- **Patent grant.** You grant a royalty-free patent license covering patents necessarily infringed by your contribution.
- **Representation of authority.** You confirm you have the right to submit the contribution and that it contains no unlicensed third-party material.
- **Corporate contributors** must additionally submit a Corporate CLA (CCLA) — see [CLA.md §6](./CLA.md#6-corporate-contributors).

### Signing the CLA

Until CLA Assistant automation is configured, sign by email using [CLA.md §9](CLA.md#9-how-to-sign), or post the following comment on your pull request:

> I have read the CLA Document and I hereby sign the CLA.

Once signed, the CLA covers future contributions to this Project as defined in the agreement. CCLA coverage for additional repositories must follow its Schedule C; do not copy signatures between repositories.

If your contribution includes third-party code or data, you are responsible for ensuring that the third-party license is compatible with GPLv3 and that proper attribution is included in your submission.

The full license text is available in [LICENSE](./LICENSE).

---

## 5. Author Acknowledgment

All contributors whose work is incorporated into openswmm.gui.data will be recognized in [AUTHORS.md](./AUTHORS.md).

- **Name, affiliation (if any), and scope of contribution** will be documented for each contributor.
- The Technical Manager maintains `AUTHORS.md` and will update it at each release.
- If you believe your contribution has been omitted or mis-described, please open an issue or contact the Technical Manager directly.

Contributors are encouraged to add themselves to `AUTHORS.md` as part of their pull request, subject to review and formatting consistency.

---

## 6. How to Cite openswmm.gui.data

Cite the version and commit or archived snapshot actually used, together with the
catalogue/service version and retrieval date where applicable. The development
package is named `openswmm.gui.data` (OpenSWMM Data Sources), maintained by Caleb
Buahin. Its current package version is recorded in `CMakeLists.txt`.

No public release citation or DOI has been registered for this initial package.
Once an archival release exists, this section will identify its citation and
version-specific archive. Contributions that rely on published scientific methods should
cite those method sources in their implementation and documentation.

---

## 7. Versioning Strategy

openswmm.gui.data follows **Semantic Versioning (SemVer)** as defined at [semver.org](https://semver.org), with formal pre-release trajectories during testing periods.

### Version Format

```
MAJOR.MINOR.PATCH[-PRERELEASE]
```

| Component | When to increment |
|-----------|-------------------|
| `MAJOR`   | Incompatible API or formulation changes; fundamental provider or model-editing contract changes |
| `MINOR`   | New data sources or capabilities added in a backward-compatible manner |
| `PATCH`   | Backward-compatible bug fixes, documentation updates, or performance improvements |

### Pre-Release Trajectories

During active release testing periods, the following pre-release labels are used in sequence:

| Stage              | Label example     | Purpose                                                                      |
|--------------------|-------------------|------------------------------------------------------------------------------|
| Alpha              | `1.2.0-alpha.1`   | Early feature-complete builds, internal or limited testing; may be unstable  |
| Beta               | `1.2.0-beta.1`    | Feature-frozen; broader community testing; known issues may exist            |
| Release Candidate  | `1.2.0-rc.1`      | Final candidate for release; only critical bug fixes accepted                |
| Stable             | `1.2.0`           | Fully validated, production-ready release                                    |

Pre-release builds are tagged in Git and published to the appropriate distribution channel with clear labels to prevent unintended use in production workflows.

---

## 8. Deprecation & Backward Compatibility Policy

openswmm.gui.data is committed to giving users adequate notice before breaking changes are introduced.

### Deprecation Process

1. **Announcement** — A feature, API, or behavior to be removed is first marked as deprecated in the release notes and in code (via comments or compiler warnings, as applicable). The deprecation announcement will state the planned removal version.
2. **Minimum notice period** — Deprecated items will not be removed for at least **one full minor version cycle** following the deprecation announcement. For widely-used public APIs, a minimum of **one major version cycle** is preferred.
3. **Removal** — Deprecated items are removed in a `MAJOR` version bump, never in a `MINOR` or `PATCH` release.

### Backward Compatibility Commitment

- `PATCH` releases are guaranteed to be fully backward-compatible.
- `MINOR` releases are backward-compatible for all stable public APIs.
- `MAJOR` releases may introduce breaking changes, which will be documented in a migration guide published alongside the release.

Experimental APIs (clearly marked as such) carry no backward compatibility guarantee and may change or be removed in any release.

---

## 9. Branching Model

The table defines the shared OpenSWMM branching convention as branches are
established. Target the existing integration branch designated by the Technical Manager.

| Branch                    | Purpose                                                                                    |
|---------------------------|--------------------------------------------------------------------------------------------|
| `main`                    | Stable, production-ready code. Only receives merges from release candidates.              |
| `dev`                     | Integration branch for ongoing development. All feature and bug-fix branches target here. |
| `experimental/<name>`     | Sandboxed branches for exploratory or major formulation changes. See Section 15.          |
| `bugfix/<issue-id>-desc`  | Short-lived branches forked from `dev` to address specific bug reports.                   |
| `feature/<name>`          | Short-lived branches for minor feature additions, forked from `dev`.                       |
| `release/<version>`       | Release preparation branches (alpha → beta → rc) cut from `dev`.                          |

---

## 10. Bug Fixes & Small Improvements

For small, well-scoped bug fixes and minor improvements, follow this workflow:

1. **Open or reference an issue.** Verify the bug is reproducible and link the relevant issue number in all subsequent commits and pull requests.
2. **Fork the `dev` branch.** Name your branch `bugfix/<issue-id>-short-description` (e.g., `bugfix/42-overflow-in-routing`).
3. **Write a failing unit test first.** The test must demonstrate the defect before the fix is applied.
4. **Implement the minimal fix.** Touch only the code necessary to resolve the issue. Do not refactor adjacent code.
5. **Ensure all regression tests pass.** If a regression test fails as a result of your change, you must either:
   - Fix the regression, **or**
   - Provide a written, technically justified explanation for why the regression failure is acceptable (included in the pull request description).
6. **Submit a pull request** against `dev` following the process described in Section 11.

---

## 11. Pull Request Process

All contributions enter the codebase through a pull request (PR). PRs must satisfy the following requirements before they can be merged:

### Required Approvals

Every PR requires **all three** of the following approvals:

| Reviewer               | Role                                                                                 |
|------------------------|--------------------------------------------------------------------------------------|
| **Technical Manager**  | [@cbuahin](https://github.com/cbuahin) (current Technical Manager) — final authority on code quality, architecture, and correctness       |
| **AI Copilot Review**  | AI-assisted review for style, logic and common error patterns; a maintainer arranges it if no automation is configured             |
| **Community Reviewer** | At least one community contributor **other than the PR author** must approve         |

Self-approvals are not permitted. The PR author may not count toward the community reviewer requirement.

### PR Checklist

Before requesting review, confirm that your PR:

- [ ] Targets the correct branch (`dev` for bug fixes and features; see Section 15 for experimental work)
- [ ] CLA signed — first-time contributors must sign the [CLA](./CLA.md) before the PR can be merged
- [ ] Includes a clear description of what was changed and why
- [ ] References any related issues (e.g., `Closes #42`)
- [ ] Includes new or updated unit tests covering the change
- [ ] All existing regression tests pass (or failures are documented and justified)
- [ ] CI pipeline passes (see Section 12)
- [ ] Documentation has been updated where applicable (Doxygen comments, README, etc.)
- [ ] `AUTHORS.md` entry has been added or updated if this is your first contribution

### Merge Policy

- Merges into `main` are performed exclusively by the Technical Manager at release time.
- Squash merging is preferred for `bugfix` and `feature` branches to keep the `dev` history clean.
- Merge commits are used when integrating `dev` into a `release` branch to preserve the full history.

---

## 12. Continuous Integration & Automated Testing

Repository workflows describe verification gates for the hosted repository.
Check Actions for their execution and results; repository protection rules and
required checks need separate configuration. Required checks must pass before
merge, or an existing failure must be clearly documented and reviewed.

The checks relevant to this plugin are:

| Gate | Purpose |
|---|---|
| Independent plugin build | Compile against the installed GUI SDK, matching Qt, and GDAL, without GUI private headers or executable symbols |
| Offline unit tests | Validate catalogue decoding, exact holes/multipart geometry, IDs, cancellation and malformed responses |
| GUI integration | Load the separately built module in the matching GUI and exercise area changes, overlays, export provenance and shutdown |
| Documentation | Generate Doxygen with missing/incomplete API documentation treated as errors |
| Dependency/static review | Review compiler diagnostics, external assets, dependency changes and applicable analysis results |

Local commands, using the matching GUI SDK kit, are:

```sh
cmake -S . -B build \
  -DCMAKE_PREFIX_PATH="/path/to/gui-sdk;/path/to/matching/Qt;/path/to/GDAL/prefix"
cmake --build build --config Release
ctest --test-dir build --output-on-failure
cd docs
doxygen Doxyfile
```

Generated documentation is written to `docs/html/index.html`. The CMake `doc`
target also runs the same build when Doxygen is available.

For vcpkg, use its toolchain and the GUI target triplet as explained in
[README.md](README.md). Tests use synthetic or explicitly recorded fixtures and
must not depend on a live public service. Live API probes are separate, manually
run checks with exact URLs, timestamps and bounded recorded responses. Keep
verification outputs in reviewable local project folders; do not commit private
models, credentials, caches or local workplans.

---

## 13. Issue Reporting

### Bug Reports

Use the **Bug Report** issue template on GitHub. A complete bug report must include:

- A clear, descriptive title.
- The version of openswmm.gui.data affected (e.g., `1.2.0-beta.1`).
- The operating system and compiler version.
- A minimal reproducible example — the smallest input file or code snippet that demonstrates the problem.
- Observed vs. expected behavior, including any relevant output or error messages.

Incomplete bug reports may be closed or deprioritized pending clarification.

### Feature Requests & Ideas

Use the **Feature Request** issue template for small, well-scoped additions. For proposals that would change the core physical or mathematical formulation, use the GitHub Discussions **Ideas** section instead (see Section 15).

### Issue Triage

The Technical Manager or a designated reviewer will triage new issues within **two weeks** of submission, assigning labels (e.g., `bug`, `enhancement`, `needs-clarification`, `wontfix`) and priority. Contributors are encouraged to comment on existing issues before opening duplicates.

---

## 14. Review Timeline Expectations

openswmm.gui.data is maintained by volunteers. The following timelines are targets, not guarantees, but the Technical Manager is committed to keeping them:

| Action                                    | Target Timeline          |
|-------------------------------------------|--------------------------|
| Initial issue triage                      | Within 2 weeks           |
| First review response on a PR             | Within 3 weeks           |
| Follow-up review after requested changes  | Within 2 weeks           |
| Major formulation discussion response     | Within 4 weeks           |

If you have not received a response within the stated window, you are welcome to post a polite follow-up comment on the issue or PR. PRs that become stale (no activity for 60 days) may be closed with a note that they can be reopened when the contributor is ready to continue.

---

## 15. Major Project Formulation Changes

Changes that affect the data-processing or model-editing behavior — including new precipitation methods, resampling or gap policies, unit/time-zone conversions, or significant algorithmic restructuring — follow a more rigorous governance process to protect the scientific integrity of the project.

### Step 1 — Open a Community Discussion

Start a new thread in the **Ideas** section of the GitHub Discussions tab. Your proposal should describe:

- The scientific or engineering motivation for the change.
- The anticipated impact on existing functionality and results.
- Any known limitations, tradeoffs, or open questions.
- References to relevant literature or prior implementations.

### Step 2 — Community Feedback

Allow a minimum of **four weeks** for community members to respond. The Technical Manager will facilitate the discussion and summarize the consensus or key points of disagreement. A proposal may be:

- **Accepted for prototyping** — community finds sufficient merit to proceed.
- **Revised** — further refinement is needed before prototyping.
- **Deferred** — not a current priority; may be revisited in a future release cycle.
- **Declined** — not aligned with the project scope or scientifically unsound.

### Step 3 — Experimental Branch Implementation

Accepted proposals are implemented in a dedicated `experimental/<name>` branch. Requirements for this phase:

- Rigorous **unit tests** covering the new formulation's behavior across the expected parameter space.
- **Regression tests** as described in Section 17.
- Code must compile cleanly and not break the existing test suite on the `dev` branch.
- Implementation notes and algorithm descriptions must be maintained in Doxygen-compatible documentation.

### Step 4 — Peer Review (Strongly Encouraged)

Where the improvement represents a meaningful scientific advance, contributors are strongly encouraged to submit a **peer-reviewed publication** describing the formulation, validation methodology, and results. Publication is not a hard requirement, but it significantly strengthens the case for incorporation into the plugin and signals readiness for production use. The Technical Manager can advise on suitable journals or conferences.

### Step 5 — Regression & Validation Testing

See Section 17 for full details. At minimum, the experimental implementation must demonstrate:

- Correctness against problems with known analytical solutions.
- Agreement within acceptable tolerance with the legacy engine and previous versions on benchmark problems.
- No unacceptable degradation in processing performance or data fidelity.

### Step 6 — Documentation & References

Before a major formulation change can be merged into `dev`, the following documentation must be complete:

- **Doxygen comments** on all new and modified public APIs, structs, and functions.
- **Inline citations** linking to peer-reviewed references where the formulation is derived from published work.
- Updated user-facing documentation (e.g., README sections, wiki pages) describing the new capability and how to invoke it.
- A changelog entry summarizing the change for the next release notes.

---

## 16. Conflict of Interest in Reviews

Scientific software reviews can intersect with contributors' professional interests. To protect the integrity of the review process:

- A reviewer who has a competing or closely related implementation, active publication, or financial interest in the outcome of a review **must disclose this conflict** before participating in the review.
- A conflicted reviewer may participate in discussion but **may not serve as one of the three required approvers** for that PR.
- The Technical Manager makes the final determination on whether a disclosed conflict disqualifies a reviewer on a case-by-case basis.
- Undisclosed conflicts of interest, if discovered after a merge, may trigger a re-review of the affected contribution.

---

## 17. Regression & Validation Testing

The plugin follows the OpenSWMM separation between offline correctness checks and
explicit integration/service verification.

### Catalogue and geometry correctness

Use deterministic fixtures for exact polygon/multipolygon intersection, holes,
boundary points, stable IDs under sorting, service errors, malformed records,
cancellation and stale results. An envelope query must not replace the chosen
polygon’s local filtering. A cancelled operation must not publish partial results.

### Model and host integration

Build independently against the installed SDK, then load into the matching GUI.
Verify model focus, reviewed-area invalidation, transient overlays, selected
feature export, provenance, project closure and clean extension shutdown. The
SDK and Qt compatibility metadata must reflect the build actually tested.

Future model-writing workflows must demonstrate a single undo step, rollback,
strict snapshot reopen and preservation of authored data. Observation and terrain
payload workflows are not part of the initial catalogue-preview package.

### Scientific methods and performance

New storm, interpolation, resampling, unit-conversion or time-zone methods need
reference values and documented tolerances. Preserve raw source data and record
all transformations. Measure response/parse limits and cancellation latency with
bounded fixtures. Record expected behavior and explain changes to prior results;
do not silently update a fixture to hide a regression.

Windows, Linux and macOS qualification are separate acceptance gates. Passing a
local build does not establish that all three platforms or live services pass.

---

## 18. Documentation Standards

openswmm.gui.data uses **Doxygen** for API documentation. All public-facing code elements (functions, classes, structs, enumerations, and their members) must include Doxygen-compatible documentation comments.

Minimum documentation per element:

- `@brief` — one-line summary.
- `@param` — description of each parameter including units where applicable.
- `@return` — description of the return value.
- `@note` / `@warning` — any important behavioral caveats.
- `@ref` or `@cite` — citation of the relevant literature where the implementation follows a published formulation.

Documentation is reviewed as part of the pull request process. PRs with undocumented public APIs will not be approved.

---

## 19. Dependency Evaluation Policy

Adding a third-party dependency increases the maintenance burden and risk surface of the project. All proposed new dependencies must be evaluated against the following criteria before inclusion:

| Criterion                | Requirement                                                                                      |
|--------------------------|--------------------------------------------------------------------------------------------------|
| **License compatibility**| The dependency's license must be compatible with GPLv3 (e.g., MIT, BSD, Apache 2.0, LGPL, GPL). Proprietary or non-GPL-compatible licenses are not permitted. |
| **Maintenance status**   | The dependency must be actively maintained with a responsive upstream community.                 |
| **Stability**            | The dependency must have a stable, versioned API. Unpinned or volatile dependencies are not acceptable. |
| **Binary footprint**     | Dependencies that substantially increase compiled binary size must be justified by proportional benefit. |
| **Platform support**     | The dependency must support all officially targeted platforms (Linux, macOS, Windows).           |

Proposals to add a new dependency should be raised in a GitHub Discussion or issue before implementation. Optional or experimental dependencies that are not required for the core build may be included under a feature flag, with lower scrutiny, provided they meet the license requirement.

---

## 20. Security Vulnerability Reporting

Follow [SECURITY.md](SECURITY.md). Do not publish vulnerability details in a
public issue. Use [security@hydrocouple.org](mailto:security@hydrocouple.org),
with [support@hydrocouple.org](mailto:support@hydrocouple.org) as the alternate
private project contact. Include the relevant plugin/GUI/SDK/dependency versions
and a minimal safe reproduction.

---

## 21. Roadmap & Prioritization

The Technical Manager coordinates roadmap decisions with contributors. The
current package provides catalogue previews, cache review and selected-feature
export. Precipitation frequency/design storms, observation imports and terrain
payload retrieval are subsequent product packages. Proposals must state which
implemented or planned capability they affect.

Local implementation workplans are kept in `workplans/` or `plans/` and are not
committed. Discuss a major scope or provider-contract change before implementing
it; record the accepted approach and validation criteria in a local workplan.
Use the hosted repository for public roadmap proposals through the issue or
discussion services enabled by the maintainer. The Technical Manager remains
available through the project address.

---

## 22. Code of Conduct

openswmm.gui.data is committed to providing a welcoming, respectful, and inclusive environment for all contributors. All participants are expected to:

- Engage constructively and professionally in all project spaces (issues, pull requests, discussions).
- Respect differing viewpoints and scientific perspectives.
- Accept constructive criticism of their contributions in good faith.
- Prioritize the long-term health of the project over individual preferences.

Harassment, personal attacks, or exclusionary behavior of any kind will not be tolerated. Violations may be reported to the Technical Manager (currently [@cbuahin](https://github.com/cbuahin)) by email to [support@hydrocouple.org](mailto:support@hydrocouple.org), following [CODE_OF_CONDUCT.md](CODE_OF_CONDUCT.md). Reported incidents will be reviewed and addressed promptly and confidentially.

---

*This document is maintained by the Technical Manager of openswmm.gui.data (currently [@cbuahin](https://github.com/cbuahin)). Last updated: October 26, 2026.*
