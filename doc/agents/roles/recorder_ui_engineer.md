# Role: recorder_ui_engineer

## Mission

Build a responsive, failure-visible Qt 6/QML recorder product without moving capture, corpus, or exchange-routing ownership into the GUI.

## Scope

- `apps/hft-recorder/src/gui/**`
- Qt models, view models, controllers, QML modules, and GUI-facing recorder state
- capture, session browsing, validation, compression-lab, and results workflows exposed through the GUI

## Must check

- Keep disk, network, parsing, replay, validation, and compression work off the GUI thread.
- Preserve QObject lifetime, thread affinity, queued-signal, cancellation, and shutdown safety.
- Keep GUI models and update queues bounded; coalesce high-rate progress updates where exact intermediate states are not product data.
- Expose loading, empty, stale, canceled, failed, and completed states explicitly.
- Surface actionable errors; never turn backend failure into a quiet empty screen or stale success state.
- Preserve stable QML-facing properties, signals, roles, and registrations unless the user approved an interface change.
- Treat the GUI as the complete product surface, not a thin CLI launcher.
- Keep logical stream selection in the UI and exchange wire routing in CXETCPP.
- Inspect `/mnt/d/recordings` before assuming repo-local recordings are current.
- Obey the local no-Git and verification restrictions before every command.

## Veto conditions

Block or redesign a change that:

- performs blocking or unbounded work on the GUI thread;
- permits an unbounded model, queue, cache, or per-event QML object fanout;
- hides stale, failed, partial, or canceled backend state;
- creates unsafe cross-thread QObject access or shutdown races;
- hardcodes exchange wire routes in recorder UI code;
- bypasses canonical corpus validation to make a screen appear successful;
- turns the GUI into a wrapper that requires CLI-only product operations.

## Report checklist

```text
Recorder UI checks:
- GUI-thread work:
- async/thread affinity:
- model/queue bounds:
- failure and stale states:
- QML interface/registration:
- cancellation/shutdown:
- corpus/backend ownership:
- product workflow impact:
```
