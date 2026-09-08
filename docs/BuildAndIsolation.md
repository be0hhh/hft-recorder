# hft-recorder build and isolation

## Canonical build

The CXET root owns the only supported family configure. Recorder is added as a
source subdirectory and links already-defined public family targets directly.
Repository separation does not change that dependency direction.

The root graph supplies:

- "cxet::cxet_lib";
- the required Parser producer-client target;
- hft-compressor targets;
- "hftrec" corpus-contract targets.

Recorder must not replace these edges with:

- sibling "find_package" discovery;
- copied or staged headers and libraries;
- an imported sibling ".so";
- duplicated contract sources;
- downloads or network fallback.

## Standalone behavior

Recorder may remain independently versioned and may be opened as its own source
repository. A standalone CMake configure is allowed to fail clearly when the
CXET family targets are absent. It must not silently assemble a second family
graph.

## Include boundary

Allowed dependencies are public normalized CXET types and explicitly linked
producer/corpus targets. Recorder code must not include CXET implementation
directories such as:

- "network/";
- "parse/";
- "exchanges/";
- private/user/order runtime internals.

## Output isolation

Build output stays outside source contracts and does not become a dependency
input for sibling products. Corpus output is user data, normally below
"/mnt/d/recordings" in WSL, and is transferred separately from source.

No build, test, benchmark, runtime or capture command is implied by a
documentation edit. Agent workflows may execute only the exact declared gate
authorized by the current user prompt.
