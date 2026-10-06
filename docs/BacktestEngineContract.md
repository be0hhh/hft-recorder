# Текущий контракт backtest и captured-arrival replay

Status: implemented source contract, static-only evidence.

Этот документ описывает один текущий путь market data от `hft-parser` до
стратегии в offline backtest. Старые recorder-сессии, плоский `depth.jsonl` и
симуляция задержки получения market data не поддерживаются.

Главное правило: биржевое время отвечает за состояние площадки, локальное время
получения отвечает за то, когда данные стали доступны стратегии. Эти две оси
нельзя подменять друг другом.

## Граница capture

Для live market data точка arrival определяется как
`hft-parser.application-frame-ready`: полный application frame уже принят,
разобран и принят зарегистрированным parser callback.

В этой точке, только при подключённом recorder consumer, `hft-parser` подряд
снимает:

- `CLOCK_REALTIME` в наносекундах;
- `CLOCK_MONOTONIC` в наносекундах.

Каждая запись сохраняет обе метки и детерминированную идентичность:

- producer epoch;
- source generation;
- session epoch;
- frame sequence;
- shard sequence;
- source/shard id;
- event ordinal внутри frame.

Одна метка arrival применяется ко всем событиям, полученным из одного frame.
Несколько depth chunks одного frame также имеют одну пару часов и различаются
порядковыми полями. Время disk write, queue drain или recorder callback не
считается временем получения market data.

Если системные часы недоступны, timestamp не подставляется. Parser публикует
loss-ledger gap с неизвестным receive range (`0/0`); такой gap пересекает любой
выбранный интервал данного source/channel и делает exact backtest невозможным.

## Два формата текущего corpus

### Parser binary corpus

Live parser feed передаётся через bounded shared-memory rings, а recorder владеет
disk I/O и sealed binary corpus. ABI/schema/version, CRC, source directory,
segments, index и gap ledger проверяются fail-closed.

Текущие binary guards: market-capture arena version `6`, record schema `8`,
control version `2`, corpus schema `7` и binary record schema `6`.
Несовпадение любого guard запрещает attach/load;
совместимый fallback отсутствует. UUID trade identity хранит только tail64
(shape `4`, second `0`), включая нулевой tail; исходные 128 бит восстановить нельзя.

Retained product channels:

- bookticker;
- trades;
- depth;

Административный `SourceLifecycle` имеет отдельный tag `9` и не является
market event. Он сохраняет фактическую membership/health границу и её clocks.

Source metadata отдельно сохраняет точную quantity authority выбранного BBO,
Trade и Depth descriptor. `CanonicalBase` обозначает уже выполненную Core
нормализацию; native Base/Contract/Lot имеют исходные multipliers и CB/lot E8
до denomination rescale. Corpus сохраняет native payload без преобразования.
Backtest переводит его копии через существующую exact Core math; неизвестная
единица, недостающий фактор, overflow или неточное деление запрещают replay.

Immutable corpus protocol также сохраняет numeric tags liquidations, mark,
index, funding и price limit. Эти retired channels не читаются в product rows,
не воспроизводятся и не отображаются. Их framing/count evidence сохраняется;
неизвестный tag отклоняется. Enum или payload не доказывает готовность канала.

Depth записывается не из сырого входного frame, а из транзакции, уже принятой
`DepthSharedPublisher`. Обычная delta является одной транзакцией. Rebase
содержит snapshot первым и затем все buffered replay-delta; каждый logical part
несёт `transactionPartIndex/Count`, исходные source receive/freshness clocks и
одну общую application-arrival пару транзакции. Backtest проверяет полноту,
publication sequence, sequence alignment и непрерывность shard records, затем
применяет все части атомарно и вызывает стратегию только после последней.

Переход Depth в `Gap`, в том числе transport/recovery gap без входного market
frame, сохраняется отдельной recorded-only state-записью. Она помечена
degraded/sequence-gap и не может быть допущена как replay event.

Trade с `Unknown` aggressor side также сохраняется, но только как
`RecordedOnly`: canonical Trade и market publication сохраняют Unknown, но
текущий binary capture producer не ставит такой записи флаг ExactTraderReplay.
Recorder не выдумывает сторону; exact-интервал с такой записью отвергается.
Прямой JSON capture также отклоняет Unknown с явной ошибкой канала.

`ExactTraderReplay` в binary directory доказывает представимость captured
parser event в текущих trader runtime primitives. Равенство фактического числа
live strategy cycles для составных side-tape событий остаётся отдельным
differential runtime gate и не выводится из одного ABI/schema совпадения.

### Recorder JSON corpus

Текущий JSON-контракт:

- `manifest_schema_version = 3`;
- `corpus_schema_version = 3`;
- `capture_contract_version = hftrec.captured_arrival_rows_json.v4`;
- manifest-declared current row schemas;
- paired `jsonl/depth_tape.jsonl` + `jsonl/depth_sidecar.jsonl`.

Каждая live row содержит captured-arrival tail. Archive/REST history помечается
как historical backfill, имеет нулевые live-arrival/identity поля и используется
только для seed/warmup. Historical candles и trades не становятся live market
deliveries.

Плоский `depth.jsonl`, fallback filenames и старые manifest/corpus schemas
отвергаются без migration reader.

## Допуск сессии к backtest

JSON-сессия допускается только если одновременно выполнены условия:

- status ровно `complete`;
- `structurally_loadable = true`;
- clean integrity;
- `exact_replay_eligible = true`;
- arrival boundary равен `hft-parser.application-frame-ready`;
- `captured_rows > 0`, `unavailable_rows = 0`;
- arrival summary точно покрывает все объявленные canonical rows;
- все требуемые стратегией каналы существуют, не пусты и clean;
- depth tape и sidecar образуют точные пары.

Для binary corpus дополнительно обязательны sealed manifest, точный ABI/schema,
CRC всех таблиц/segments и отсутствие необъяснённых capture-loss gaps. Точные
membership и native-health записи приостанавливают затронутые контексты и
сохраняют заявки и позиции; они не заменяют недостающие market payloads.
Переходы поколений допустимы только с точными записанными границами.

При выборе всего binary рынка Backtest расширяет symbols настроенных продуктов
по записанному каталогу и одной доказанной валюте котировки. Исходные strategy
params, группы, balances и risk policy сохраняются. Неизвестная или смешанная
валюта котировки отклоняется до создания торговых контекстов.
Требуемая возможность канала должна быть настроена в source metadata. Если у
такого источника ещё нет данных, его контексты остаются на паузе, а здоровые
соседние инструменты продолжают replay. Явный выбор интервала сохраняет строгую
проверку наличия требуемых rows. Пустой источник не создаёт синтетических events.

Optional channel может отсутствовать с явным warning. Required channel никогда
не заменяется другим каналом и не фабрикуется.

## Replay clock

Backtest строит две координаты для каждого market event.

### Strategy delivery plane

Captured `receive_monotonic_ns` проецируется на replay coordinate через одну
session anchor-пару realtime/monotonic. Market events сортируются по этой
координате, затем по producer/shard/frame identity и event ordinal.

Стратегия видит snapshot и получает market-driven `runCycle` только в этой
точке. Дополнительная synthetic market-data latency отсутствует.

Equal-sequence BBO freshness для уже установленного состояния является только
application-plane liveness: обновляет captured receive clocks, не создаёт
venue event/fill и не вызывает немедленный `runCycle`. Первый такой full-payload
record в выбранном интервале сам устанавливает BBO state.

### Venue execution plane

В binary folder replay venue state обновляется в captured arrival. Поздний
frame нельзя применить раньше по его exchange timestamp. Биржевое время
сохраняется как payload evidence. Legacy JSON остаётся доступен для структурного
чтения и анализа архива; исполняемый replay требует текущую binary quantity
authority. Старые JSON не дают точных единиц рыночного объёма, а их объединение
также не доказывает общую валюту котировки.

Venue plane обновляет только venue snapshot/order book и обслуживает fills,
stops и execution state. Он не делает market data видимой стратегии раньше
captured arrival.

Rebase является локальной recovery-транзакцией, а не новым биржевым событием,
поэтому venue plane применяет её целиком в captured application-arrival. Это
сохраняет исходные exchange timestamps как evidence, но не позволяет
переставить snapshot и replay-delta по разным биржевым временам.

Order submit/cancel/user-data latency остаётся отдельной синтетической моделью
исполнения. Она не должна смешиваться с наблюдённой задержкой market data.
Venue fills меняют финансовый результат в момент исполнения. Стратегия видит
позицию, общий кошелёк и ACK/terminal только после exact private delivery.
Private source FIFO продолжает ingestion при paused контексте, без callback
его стратегии.

## Clock anomalies

Recorder не исправляет и не отбрасывает по умолчанию:

- regression `CLOCK_REALTIME`;
- равный или не возрастающий `CLOCK_MONOTONIC` sample;
- exchange timestamp впереди локального realtime;
- отсутствующий exchange timestamp.

Аномалии сохраняются флагами и счётчиками. Детерминированный identity tail
разрешает ties. Result содержит исходные clock ranges, replay delivery ranges,
venue ranges и число скорректированных venue events.

## Strategy и accounting boundary

Стратегия остаётся неизменённой и запускается через generated registry и
`StrategyDescriptor`. Она получает обычный `StrategyContext` и выпускает
`OrderIntent`/`CancelIntent`.

Backtest владеет:

- order acceptance/reject/cancel state machine;
- market и limit fills;
- fees;
- positions и average entry;
- realized/unrealized PnL;
- balance/equity;
- rate/risk state;
- final artifacts.

GUI только отображает результат и не пересчитывает торговую математику.

Спот использует общий quote cash и резервы покупок аккаунта, а также base
inventory и резервы продаж источника. Покупка списывает principal и комиссию,
продажа зачисляет principal за вычетом комиссии. Недостаток денег или актива
запрещает исполнение. Линейные Futures/Swaps сохраняют модель PnL wallet.
Локальные pending-резервы известны сразу; venue cash, fills, rejects и terminal
не становятся observed фактами до соответствующей private delivery. Порядок
private событий одного аккаунта сохраняется между всеми его источниками.

Spot equity включает положительный base inventory. При паузе источника его
valuation использует последний полученный bid отдельно на actual/observed
плоскостях. Эта историческая оценка не создаёт готовность котировки или
ликвидность. Service unwind для Spot и Futures требует доступную встречную
цену и количество; отсутствие данных сохраняет экспозицию и вызывает отказ.
Warmup ACK и service exposure в strategy view принадлежат private delivery.

Source PnL curves включают realized и unrealized Core PnL. Итоговые wallet и
risk/PnL артефакты суммируют quote-деноминированные факты источников; смешанное
количество разных базовых активов не оценивается одной общей ценой.
Canonical simulation evidence ограничен 1024 заявками на источник, включая
terminal records; исчерпание явно запрещает продолжение без удаления evidence.

## Result contract

Текущий final artifact имеет `type = run.result` и обязательный
`schema_version = 5`. Recorder может читать старые финансовые summaries schema
`4` без исполнения; неизвестный или отсутствующий guard отклоняется.
В нём replay-clock evidence
должен явно указывать:

- `arrival_boundary = hft-parser.application-frame-ready`;
- `market_data_delivery = captured_application_frame_arrival`;
- `venue_schedule = captured_arrival_no_future_lookahead` для binary folders;
- captured/replay/venue ranges и anomaly counters.

Финальный watermark берётся из максимальной реально обработанной replay
координаты и не может откатиться к последнему exchange timestamp.

## Acceptance criteria

Реализация считается подтверждённой только после отдельных доказательств:

- parser/recorder protocol и все consumers собираются на одном ABI;
- loss-ledger тест покрывает известный receive range и неизвестный `0/0` range;
- every-row arrival identity и paired depth проходят corpus tests;
- legacy JSON/depth paths детерминированно отвергаются;
- backtest test доказывает, что стратегия не видит event до captured delivery;
- venue fills не происходят ретроактивно для ещё не активированной заявки;
- private position/account/order facts не видны до их delivery;
- market-data latency knobs отсутствуют, execution latency остаётся;
- одинаковые corpus/config/strategy дают одинаковый result.

Сборка, unit tests, runtime capture и live exchange proof являются разными
уровнями evidence и не заменяют друг друга.
