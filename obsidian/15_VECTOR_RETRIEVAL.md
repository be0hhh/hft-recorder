# Vector Retrieval

Purpose:

- Fix the chosen retrieval policy for `hft-recorder`.
- Keep recorder context narrow and integration-aware.

Chosen stack:

- `Obsidian` = recorder truth map
- `Linear` = recorder workflow
- `Qdrant` = recorder semantic retrieval plus compact memory

Recommended collections:

- `hftrec_memory`
- `hftrec_docs`
- `hftrec_code_summaries`

What goes into `Qdrant`:

- runtime summaries
- decisions
- gotchas
- selected file-role summaries
- selected corpus/integration summaries

What does not go into `Qdrant`:

- whole notes
- raw recordings
- giant generated graph output
- blind full-source ingestion

Required metadata:

- `project = hft-recorder`
- `kind`
- `subsystem`
- `source_path` or `source_id`
- `updated_at`
- `status`

Integration rule:

- recorder retrieval can reference `CXETCPP` facts only through explicit integration summaries
- do not mix recorder and library collections into one undifferentiated pool
