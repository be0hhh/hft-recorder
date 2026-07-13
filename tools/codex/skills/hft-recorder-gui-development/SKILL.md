---
name: hft-recorder-gui-development
description: Build and review robust Qt 6/QML hft-recorder product workflows. Use for recorder GUI, models, view models, controllers, QML registration, async progress, session browsing, validation, compression-lab, or results-dashboard changes.
---

# HFT Recorder GUI Development

## Workflow

1. Read `apps/hft-recorder/AGENTS.md`, `apps/hft-recorder/doc/agents/ROLE_ROUTER.md`, and `apps/hft-recorder/doc/agents/roles/recorder_ui_engineer.md`.
2. Select `recorder_ui_engineer`; add `recorder_corpus_engineer` when exposing corpus state and `architect` when changing interfaces or boundaries.
3. Trace the complete QML-to-model-to-backend path, including startup, progress, cancellation, failure, shutdown, and stale-result handling.
4. Keep disk, network, parsing, replay, validation, and compression work off the GUI thread. Use safe Qt ownership, thread affinity, and queued delivery.
5. Bound models, queues, caches, and update rates. Coalesce disposable progress updates without dropping product data or terminal state.
6. Preserve QML-facing properties, signals, roles, and registrations unless the user explicitly approves an interface change.
7. Show loading, empty, stale, failed, canceled, partial, and completed states explicitly. Keep errors actionable and failure-visible.
8. Keep the GUI a complete product surface for capture, sessions, validation, compression lab, and results; do not make required flows CLI-only.
9. Implement only after explicit authorization and keep worker file ownership disjoint.
10. Run no Git or verification command unless separately and exactly authorized in the current message.

## Safety gates

- Reject synchronous heavy work or unbounded per-event QML object creation on the GUI thread.
- Reject unsafe cross-thread QObject access, late callbacks into destroyed objects, and shutdown races.
- Keep logical stream selection in recorder and exchange wire routing in CXETCPP.
- Do not hide backend errors behind empty screens, stale data, or optimistic success.
- Report code-only conclusions as `static-only`.

## Report

State GUI-thread impact, ownership/threading, bounds, error/stale states, QML interface impact, corpus/backend boundary, changed files, risks, manual verification commands, and what was not verified.
