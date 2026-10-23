# Offline tests

Cases are grouped by subject: `Corpus`, `Capture`, `ParserClient`, `Json`, `Service`.

Add a case to the matching subject and register its source in this directory's
`CMakeLists.txt`. Keep existing suite targets, case IDs, labels and `EXPECTED`
counts stable when moving cases; update `EXPECTED` when adding coverage.
Use local fixture headers only for shared setup; keep fixture ownership and
lifetimes explicit. Case `.ipp` fragments are included by their suite driver
and must not be compiled separately. These suites stay offline.
