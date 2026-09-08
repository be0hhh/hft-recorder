# hft-recorder role router

Read `apps/hft-recorder/AGENTS.md` first. Use this router together with the root `docs/Agents/RoleRouter.md`; the local recorder rules are stricter and take precedence.

## Skill routing

| Skill | Primary role | Supporting roles | Scope |
|---|---|---|---|
| `hft-recorder-corpus-pipeline` | `recorder_corpus_engineer` | `architect` for CXET/app, schema, or cross-app boundaries | capture, canonical corpus, replay, validation, manifests |
| `hft-recorder-gui-development` | `recorder_ui_engineer` | `recorder_corpus_engineer`; `architect` for public or cross-module interfaces | Qt 6/QML product workflows, models, async UI integration |
| `hft-recorder-compression-lab` | `compression_engineer` | `recorder_corpus_engineer`; `architect` for artifact or app boundaries | baselines, custom stream pipelines, lossless verification, results |

## Request routing

```text
capture, corpus, session, manifest, replay, validation
  -> recorder_corpus_engineer

Qt, QML, GUI, model, view model, session browser, dashboard
  -> recorder_ui_engineer + recorder_corpus_engineer when corpus state is exposed

compression, codec, zstd, lz4, brotli, xz, ratio, encode, decode
  -> compression_engineer + recorder_corpus_engineer

CXET public API, shared-library boundary, cross-app artifact, schema change
  -> architect + affected recorder specialist
```

## Worker routing

- Use read-only explorers/reviewers automatically only for non-trivial work.
- Start editing workers only after explicit implementation authorization in the current user message.
- Use no more than three workers and assign non-overlapping files or modules.
- Keep shared integration, registry, and manifest files with the primary agent.
- Use GPT-5.6-sol high without fast mode; GPT-5.6-terra high without fast mode is allowed for easy tasks. Use xhigh/max only when explicitly requested.
- Never infer Git, build, test, generated-rewrite, runtime, or remote permission from implementation authorization.
- Stop when ownership overlaps, a public contract is ambiguous, or no single safe design exists.
