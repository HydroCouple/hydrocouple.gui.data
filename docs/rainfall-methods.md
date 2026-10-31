# Rainfall methods and numerical reference

The Design Storm Builder evaluates candidates in memory using the qualified
methods below. A selected storm becomes model forcing only through **Apply to
model…**. NOAA frequency and temporal responses are session values: the runtime
does not archive responses, write a NOAA disk cache, or save complete NOAA
matrices in the project. Explicitly applied model rainfall samples are durable
model data.

## Qualified method catalog

These are fourteen selectable methods. Published table resolution is retained
and identified; a smaller output interval interpolates that table rather than
recovering additional source observations.

| Method key | Method and domain | Source representation |
|---|---|---|
| `scs-i` | SCS Type I, native 24 hours | FHWA HDS-2 (2002), Table 6.5; 49 half-hour cumulative ordinates |
| `scs-ia` | SCS Type IA, native 24 hours | WSDOT Highway Runoff Manual (2014), Table 4C-3; 241 cumulative ordinates at 0.1-hour spacing |
| `scs-ii` | SCS Type II, native 24 hours | WSDOT Highway Runoff Manual (2014), Table 4C-4; 241 cumulative ordinates at 0.1-hour spacing |
| `scs-iii` | SCS Type III, native 24 hours | FHWA HDS-2 (2002), Table 6.5; 49 half-hour cumulative ordinates |
| `nrcs-mse3` | NRCS MSE3 regional pattern, native 24 hours | SEMCOG Southeast Michigan Rainfall (2020), Table 5; 241 cumulative ordinates at 0.1-hour spacing |
| `huff-first` | Huff first-quartile Midwest point median | Published cumulative percentages at 5% normalized-time increments |
| `huff-second` | Huff second-quartile Midwest point median | Same point-median source, explicit second quartile |
| `huff-third` | Huff third-quartile Midwest point median | Same point-median source, explicit third quartile |
| `huff-fourth` | Huff fourth-quartile Midwest point median | Same point-median source, explicit fourth quartile |
| `triangular` | Analytical triangular intensity pattern | Explicit total depth, duration and peak-time fraction |
| `alternating-block` | Depth-duration alternating-block storm | Selected live NOAA frequency/bound column, log-log duration interpolation and ranked interval depths |
| `chicago` | Chicago pattern using a fitted positive-shift IDF relationship | Least-squares fit of selected NOAA duration-depth data; analytical interval integration |
| `noaa-temporal` | Live NOAA Atlas 14 temporal pattern | Explicit temporal product, area, native duration, quartile/all-cases and percentile |
| `custom` | User-defined normalized cumulative curve | Finite, strictly increasing normalized times and nondecreasing cumulative fractions |

The source-specific SCS tables are not claimed to be numerically interchangeable
with every other SCS table, a finer WinTR-55 table, or Dstorm. For example, the
WSDOT Type II source explicitly gives cumulative fractions 0.431 at 11.8 hours,
0.568 at 11.9 hours and 0.663 at 12 hours. Other publications sometimes provide
different intermediate knots. Generation retains the cited source variant.

## Primary publications

- [FHWA, Introduction to Highway Hydraulics, HDS-2, second edition, October 2002](https://www.fhwa.dot.gov/engineering/hydraulics/pubs/hif02001.pdf):
  Table 6.5, printed page 6-35, provides the half-hour SCS ordinates used for
  Types I and III.
- [WSDOT Highway Runoff Manual M31-16.04, April 2014 revision](https://www.wsdot.wa.gov/publications/manuals/fulltext/M31-16/M31-16.04revision.pdf):
  Appendix 4C, Tables 4C-3 and 4C-4, provides the complete 0.1-hour Type IA and
  Type II cumulative tables used here.
- [SEMCOG, Southeast Michigan Rainfall: Current and Future Precipitation, June 2020](https://www.semcog.org/wp-content/uploads/2026/01/SEMICurrentFuturePrecipJune2020.pdf):
  Table 5, printed page 18, supplies the NRCS MSE3 ordinates. Its regional
  application is an explicit user choice.
- [City of Columbia, Missouri, Stormwater Manual, Appendix F](https://www.como.gov/wp-content/uploads/sites/20/2017/04/AppendixFTablesandFigures-1.pdf):
  Table 2.2.2.4 reproduces Huff and Angel's *Rainfall Frequency Atlas of the
  Midwest*, Bulletin 71 (1992), median heavy-storm time distributions at a point.
  These are the four Huff tables used here.
- [NRCS National Engineering Handbook Part 630, Chapter 4, Storm Rainfall Depth and Distribution, August 2019](https://directives.nrcs.usda.gov/sites/default/files2/1712930592/29495.pdf):
  methodological context for legacy SCS, updated regional and site-specific
  distributions. This reference does not make MSE3 applicable to every location.
- [USACE HEC-HMS Technical Reference Manual, Frequency Storm](https://www.hec.usace.army.mil/confluence/hmsdocs/hmstrm/meteorology/precipitation/frequency-storm):
  log-log depth-duration interpolation and alternating placement of ranked
  precipitation blocks.
- [Chen, C.-L., Synthetic Storms for Design of Urban Highway Drainage Facilities, FHWA/Utah Water Research Laboratory, May 1975](https://nmwrri.nmsu.edu/footer_pages/nm-wrri-library-database-files/wrri-library-pdfs/wrrilibrary7/007854.pdf):
  equations 9–13 and 21–22 provide the positive-shift IDF and fitted analytical
  hyetograph formulation used for Chicago.
- [NOAA PFDS FAQ](https://www.weather.gov/owp/hdsc_faqs) and
  [NOAA temporal distribution products](https://hdsc.nws.noaa.gov/pfds/pfds_temporal.html):
  live source/query options and temporal-area products.

## Cumulative interpolation and interval totals

Published and custom tables are represented as normalized cumulative coordinates
`(x, F(x))`, where elapsed time and accumulated rainfall both range from zero to
one. Tables must begin at `(0,0)`, end at `(1,1)`, contain finite values, have
strictly increasing times, and have nondecreasing cumulative fractions.
Interpolation is piecewise linear in cumulative time/depth. Every published
knot remains unchanged.

For total depth `D` and interval boundaries `t[i]`, the interval depth is:

`depth[i] = D × (F(t[i+1]/T) − F(t[i]/T))`

The displayed/model-ready interval intensity is:

`intensity[i] = depth[i] × 60 / intervalMinutes`

Depths are canonical millimetres and intensities are millimetres per hour.
Generation uses a regular interval and requires the total duration to divide
evenly by it. SCS and MSE3 retain their native 24-hour duration; shortening their
duration is refused. No implicit duration compression or extraction is applied.

The generator checks nonnegative finite interval depths and conservation of the
chosen total before a candidate can be applied. Intensity values are averages
over intervals, not instantaneous samples of the analytical curve.

## Triangular pattern

Let `r` be the chosen peak-time fraction, strictly between zero and one.
The normalized analytical cumulative pattern is:

- Before the peak, `F(x) = x² / r`.
- After the peak, `F(x) = 1 − (1−x)² / (1−r)`.

Interval depths are exact differences of this cumulative function. An interval
that straddles the peak therefore retains the correct rainfall volume even
when the peak does not lie on a sample boundary.

## Alternating-block pattern

For one accepted frequency estimate/bound column, the builder samples depths at
durations equal to successive multiples of the selected recording interval.
Exact source knots are retained. Between knots, interpolation is linear in the
logarithms of both duration and positive depth. Extrapolation outside the
returned duration range is refused; the interval cannot be shorter than the
shortest source duration.

Successive duration-depth differences form the precipitation blocks.
Decreasing duration-depth values are rejected. Blocks are ranked from largest
to smallest. The largest is placed in the interval beginning at the selected
peak fraction, rounded down to the recording grid. Subsequent blocks alternate
to the right and left, filling whichever side remains available.

The shape is scaled to the explicitly selected total depth. When that total is
the source's estimate at the chosen duration, the source storm total is
preserved. A manually selected total is an explicit depth scaling and does not
create a new NOAA frequency estimate.

## Chicago fit and diagnostics

The fitted relationship is:

`I(d) = a / (d + b)^c`

Here `d` and `b` are minutes and `I` is millimetres per hour. The fit requires
positive source depths, at least four returned duration knots within the storm,
a positive shift `b`, and `0 < c < 1`. The selected interval must be at least
the shortest returned duration. Storm duration must stay within the returned
NOAA duration range.

For each trial positive `b`, least-squares regression solves
`log I = log a − c log(d+b)`. A bounded, reproducible one-dimensional search
selects `b`. These are fitted coefficients; NOAA does not supply universal
Chicago/IDF equation coefficients.

The integrated depth-duration function is:

`P(d) = a × d / (d+b)^c / 60`

For peak position `rT`, cumulative depth before the peak is
`r × (P(T) − P((rT−t)/r))`; after the peak it is
`r × P(T) + (1−r) × P((t−rT)/(1−r))`.
The result is normalized by `P(T)` and scaled to the selected total depth.
Taking differences of this analytical cumulative function preserves interval
volume, including intervals containing the peak.

Candidate metadata records `a`, `b`, `c`, fitted duration limits, log-space
RMS error, maximum relative error at the fitted source knots, the fitted storm
total and its applied depth scale. The preview displays the coefficients and
maximum relative error. Evaluate these diagnostics when comparing Chicago to
the discrete NOAA-derived patterns; an accepted fit is not an exact
interpolant of every source estimate.

## Huff and regional applicability

The four Huff options use a **fixed point-median** curve set. They do not
provide a selectable probability family, an areal Huff adjustment, or a
location-based recommendation. A first-quartile curve describes timing of the
largest rainfall portion; it is distinct from a precipitation-frequency
confidence bound and from NOAA's independently derived temporal quartiles.

These source tables describe Midwest heavy-storm patterns. Selecting and
scaling their normalized duration is an explicit evaluation choice. The
software does not infer regulatory acceptability or automatically choose a
Huff quartile from project coordinates or storm length.

MSE3 is likewise an explicitly named regional NRCS pattern. The current
catalog does not include every NRCS regional distribution and does not
substitute MSE3 for Northeast N10 or Ohio Valley A/B/C/D curves.

## NOAA series, units and temporal percentiles

The live source's selected **AMS** or **PDS** basis remains attached to the
accepted table and applied storm provenance. The builder does not silently
convert annual maxima to partial-duration estimates, or convert recurrence
labels to annual exceedance probabilities. Choose the intended source basis
when fetching.

English source depths are converted from inches to millimetres. Source
intensities are converted to canonical depth using their source duration;
display and generation retain explicit rainfall units. Choosing a source
confidence bound is separate from choosing a temporal-distribution percentile.

Live NOAA temporal curves retain their returned cumulative time knots,
temporal-area identity and native duration. A curve is not stretched to another
duration. Neither a selected boundary polygon nor a point query automatically
applies an areal-reduction factor. Generated model rainfall does not establish
the recurrence probability of resulting flooding.

## Remaining method scope

[Dstorm](https://sites.google.com/view/dstorm) is a functional comparison
reference, not a claim of binary or numerical identity. Separate Dstorm
**Nested** and **Composite** variants are not offered until their exact
definitions and reference cases are qualified. The implemented alternating
block method is a nested-frequency construction in HEC-HMS terminology, but
that does not establish equivalence with another tool's method named Nested.

Additional regional NRCS catalogs, other Huff probability/area families, and
explicit shorter-duration extraction from published 24-hour curves remain
future qualification work. Unsupported methods are not presented as working
generators.
