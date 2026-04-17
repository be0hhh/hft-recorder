# hft-recorder - testing contract

## Testing stages

Testing is split into four practical layers.

### 1. Capture correctness

Must verify:
- session directory created correctly
- all required files exist
- manifest is valid JSON
- channel JSON lines follow the canonical schema
- snapshot cadence works

### 2. Corpus loading

Must verify:
- all channel files parse
- ordering is preserved
- integer values survive exactly
- malformed input reports deterministic errors

### 3. Validation correctness

Must verify:
- original normalized corpus and decoded pipeline output match exactly for
  lossless pipelines
- mismatches report channel and event index

### 4. Lab metrics

Must verify:
- every pipeline reports ratio and speed fields
- ranking is deterministic on the same inputs

## GUI acceptance

The GUI acceptance path is:
1. create a session
2. stop the session cleanly
3. open it in the Sessions page
4. open raw charts in Validation
5. run baselines in Lab
6. see dashboard results

## Current rule

The old large `.cxrec`-oriented unit/integration matrix is historical design
material, not the immediate test gate for the GUI-first product.

The immediate test gate is:
- capture files are correct
- corpus loads correctly
- lossless pipelines validate correctly
- GUI views can consume the results
