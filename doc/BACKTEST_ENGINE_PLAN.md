# План простой интеграции backtest engine

Документ для внешнего C++ разработчика. Цель - сделать простой offline
backtester, который берёт данные recorder-сессии, прогоняет неизменённую
стратегию из `hft-trader`, сам считает торговый результат и в конце одним
результатом отдаёт GUI всё, что надо показать.

Главное правило: backtester считает fills, fees, positions, PnL и equity. GUI
ничего из этого не пересчитывает, а только отображает результат.

## Что нужно сделать в v1

V1 - это не live simulation и не streaming replay. V1 работает так:

1. Recorder отправляет backtester-у запрос `run.start`.
2. Backtester загружает нужные данные из recorder session в ОЗУ.
3. Backtester запускает выбранную стратегию из `apps/hft-trader/strategy` через
   существующий `StrategyDescriptor` / generated registry.
4. Backtester собирает `OrderIntent`, `CancelIntent`, `AmendIntent`.
5. Backtester симулирует заявки, fills, cancels, fees, positions, PnL и equity.
6. Backtester возвращает один final batch `run.result`.
7. Recorder показывает orders, fills, входы/выходы, equity/PnL и summary.

Progress/live stream можно добавить позже, но это не часть v1.

## Что уже есть

### В hft-recorder

Recorder уже пишет canonical corpus в `apps/hft-recorder/recordings`.
Основные файлы сессии:
- `manifest.json`
- `candles.jsonl`
- `trades.jsonl`
- `bookticker.jsonl`
- `depth.jsonl`

Полезный код как reference:

- `src/core/replay/SessionReplay.*`
- `src/core/replay/EventRows.hpp`
- `src/core/replay/BookState.*`
- `src/core/replay/JsonLineParser.*`
- `src/core/execution/ExecutionVenue.hpp`
- `src/core/local_exchange/LocalOrderEngine.*`

`LocalOrderEngine` можно смотреть как прототип local venue, но не надо считать
его обязательным финальным ядром backtester-а.

`ExecutionEvent` уже содержит нужные поля для GUI/backtester boundary:

- `symbol`
- `orderId`
- `clientOrderId`
- `execId`
- `sideRaw`
- `typeRaw`
- `statusRaw`
- `quantityRaw`
- `priceRaw`
- `fillPriceE8`
- `filledQtyRaw`
- `feeRaw`
- `realizedPnlRaw`
- `positionQtyRaw`
- `avgEntryPriceE8`
- `walletBalanceRaw`
- `availableBalanceRaw`
- `equityRaw`
- `tsNs`
- `success`

### В hft-trader

Стратегии уже устроены правильно для backtest. Их менять нельзя.

Важные файлы:

- `apps/hft-trader/strategy/<name>/Strategy.hpp`
- `apps/hft-trader/include/hft_trader/runtime/StrategyDescriptor.hpp`
- `apps/hft-trader/include/hft_trader/core/StrategyContract.hpp`
- `apps/hft-trader/include/hft_trader/core/Intent.hpp`
- `apps/hft-trader/include/hft_trader/core/StrategyInputs.hpp`
- `apps/hft-trader/include/hft_trader/core/MarketView.hpp`

Стратегия запускается через `StrategyDescriptor::runCycle()` и получает
`StrategyContext`. На выходе она пишет только локальные intents:

- `OrderIntent`
- `CancelIntent`
- `AmendIntent`

Стратегия не должна знать, что она в backtest. Никаких `isBacktest` branches в
strategy code.

## Архитектура v1

```text
recordings/<session>
        |
        v
Backtest loader
        |
        v
Typed arrays in RAM
        |
        v
Strategy runner через StrategyDescriptor
        |
        v
Simulated venue / accounting
        |
        v
Final JSON result
        |
        v
hft-recorder GUI
```

Backtester - отдельный C++ module/library/executable. Он не должен зависеть от
QML или GUI. Recorder вызывает его через внутреннее local JSON API.

## Правило загрузки данных

Backtester не должен грузить все каналы на всякий случай.

Он должен смотреть требования выбранной стратегии:

- если стратегии нужен только `bookticker`, грузить только `bookticker.jsonl`;
- если нужны trades + bookticker, грузить только эти каналы;
- если нужен orderbook/depth, грузить `depth.jsonl` и snapshots;
- если стратегия имеет `StrategyDescriptor::seedHistory`, грузить candle history;
- лишние каналы не парсить и не держать в RAM.

Это важно для скорости, памяти и будущих больших прогонов.

## Свечи / TieredCandleHistory

Некоторые стратегии требуют свечную историю до запуска основной логики. Это уже
есть в `hft-trader` через `StrategyDescriptor::seedHistory`.

Факт из CXETCPP:

- тип: `cxet::composite::TieredCandleHistory`;
- capacity: `kTieredCandleHistoryCapacity = 512`;
- tiers:
  - `m1[512]`
  - `m15[512]`
  - `d1[512]`
- свеча для стратегии - `CandleLite`, не полный OHLCV;
- поля `CandleLite`:
  - `tier` M1/M15/D1
  - `ts` ns
  - `high` E8
  - `low` E8
  - `quoteAmount` E8

В live runtime `hft-trader` загружает это как 512 свечей `1m`, потом более
старые 512 свечей `15m`, потом более старые 512 свечей `1d`.

Правила для backtester v1:

- если `descriptor->seedHistory == nullptr`, свечи не нужны и не грузятся;
- если `descriptor->seedHistory != nullptr`, backtester должен найти candle
  artifact в recorder session и собрать `TieredCandleHistory`;
- путь к candle artifact должен браться из `manifest.json`, а не быть жёстко
  захардкожен;
- рабочее имя файла может быть `candles` / `candle_history`, но точное имя решает
  manifest;
- если стратегия требует свечи, а artifact отсутствует или повреждён, run должен
  завершиться понятной ошибкой;
- если стратегия не требует свечи, отсутствие candle artifact не является
  ошибкой.

Свечи нужны не всем стратегиям. Например, стратегии на bookticker/spread не
должны требовать candle history, если их descriptor не имеет `seedHistory`.

## Внутреннее JSON API v1

V1 API можно сделать минимальным.

### Request: run.start

Recorder отправляет backtester-у:

```json
{
  "type": "run.start",
  "request_id": "req-001",
  "session_path": "C:/.../apps/hft-recorder/recordings/SESSION_ID",
  "strategy": "spread_maker1and2",
  "config_path": "C:/.../apps/hft-trader/1and2.ini",
  "result_mode": "final_batch"
}
```

Обязательные поля:

- `type`
- `request_id`
- `session_path`
- `strategy`
- `config_path`
- `result_mode = final_batch`

Желательно сразу поддержать `run_id`. Если recorder его не передал, backtester
генерирует сам.

### Response: run.result

Backtester возвращает один JSON результат после полного прогона:

```json
{
  "type": "run.result",
  "request_id": "req-001",
  "run_id": "run-001",
  "status": "complete",
  "orders": [],
  "fills": [],
  "equity_points": [],
  "summary": {},
  "errors": []
}
```

Минимальные секции результата:

- `orders`: заявки, которые стратегия пыталась поставить/отменить;
- `fills`: реальные simulated fills;
- `equity_points`: точки equity/PnL для графика;
- `summary`: финальная статистика прогона;
- `errors`: ошибки, если run не completed.

GUI должен отрисовать эти данные как есть. GUI не пересчитывает PnL.

## Что должен считать backtester

Backtester владеет всей торговой математикой:

- order acceptance / reject;
- market order fills;
- resting limit orders;
- cancel/amend handling;
- fill price;
- partial/full fill status;
- fees;
- realized PnL;
- unrealized PnL;
- position qty;
- average entry price;
- wallet balance;
- available balance;
- equity;
- final summary metrics.

Начальные fill rules могут быть простыми и детерминированными. Главное - они
должны быть внутри backtester-а, а не в GUI.

## Что делает GUI

GUI только отображает final batch:

- точки входа/выхода;
- limit/market order markers;
- fills;
- rejected orders;
- equity/PnL curve;
- summary panel;
- таблицу orders/fills, если нужно.

`ExecutionChartAdapter` сейчас можно использовать как reference, но для backtest
result, вероятно, нужен отдельный adapter/controller, который принимает
`run.result` и раскладывает данные по viewer objects.

## План работ для внешнего C++ разработчика

1. Сделать skeleton backtest module.
   - Отдельный C++ module/library/executable.
   - Без зависимости от QML/GUI.
   - С доступом к нужным public/core contracts `hft-trader` и recorder corpus.

2. Сделать loader recorder session.
   - Читать `manifest.json`.
   - Определять доступные channels и artifacts.
   - Грузить только нужные выбранной стратегии данные.
   - Конвертировать rows в typed arrays в ОЗУ.

3. Поддержать candle history.
   - Если `descriptor->seedHistory != nullptr`, найти candle artifact через
     manifest.
   - Собрать `cxet::composite::TieredCandleHistory`.
   - Вызвать `descriptor->seedHistory(...)`.
   - Если свечи нужны, но их нет, вернуть error в `run.result`.

4. Сделать strategy runtime adapter.
   - Найти стратегию по имени через generated registry.
   - Загрузить обычный `hft-trader` config.
   - Вызвать `resolveParams` и `initState`.
   - На market events строить `StrategyContext`.
   - Вызывать `runCycle`.
   - Собирать `OrderIntent`, `CancelIntent`, `AmendIntent`.

5. Сделать simulated venue/accounting.
   - Превращать intents в simulated orders.
   - Делать ack/reject/fill/cancel state machine.
   - Считать fees, positions, PnL, balance, equity.
   - Сохранять orders/fills/equity_points/summary.

6. Сделать JSON boundary для recorder.
   - Принять `run.start`.
   - Вернуть `run.result`.
   - Ошибки отдавать структурно через `status = error` и `errors`.

7. Подключить GUI отображение.
   - Recorder принимает final batch.
   - Viewer рисует markers и curves.
   - GUI не считает торговый результат.

## Acceptance criteria

Реализация готова, если:

- стратегия из `apps/hft-trader/strategy/<name>/Strategy.hpp` не менялась;
- backtester запускает стратегию через descriptor/registry;
- загружаются только нужные стратегии channels;
- стратегия без `seedHistory` запускается без candle artifact;
- стратегия с `seedHistory` получает `TieredCandleHistory` из session artifact;
- если нужных свечей нет, backtester возвращает понятную ошибку;
- fills, fees, PnL, positions и equity считает backtester;
- recorder показывает final result и не пересчитывает его;
- одинаковые session/config/strategy дают одинаковый результат.

## Не входит в v1

- Live simulation.
- Progressive streaming / live drawing during run.
- Monte Carlo.
- Подключение к реальной бирже.
- PnL calculation на стороне GUI.
- Изменение strategy code под backtest.
- Загрузка всех каналов без необходимости.

Будущие расширения можно добавить позже: streaming progress, много прогонов,
Monte Carlo, multi-venue arbitrage/spread analysis, разные fill/latency models.

