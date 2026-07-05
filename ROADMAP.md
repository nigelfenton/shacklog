# Roadmap

Where ShackLog is headed. Nothing here is a promise or a schedule — it's the
current list of things worth building next, roughly in priority order. Done
items live in the [CHANGELOG](CHANGELOG.md).

## Near term

- **LoTW upload helper** — sign and upload logged QSOs to ARRL Logbook of the
  World via `tqsl`: select a date range / station, invoke tqsl to sign the
  ADIF, upload, and mark the QSOs as uploaded. The single biggest missing piece
  for confirmation tracking, especially for digital operating.
- **Visual stats dashboard** — the awards data is computed today but only shown
  as a text report. A visual panel (worked/confirmed by band + mode + DXCC,
  a simple chart) would make it far more useful at a glance.
- **DXCC entity numbers offline** — `cty.dat` already resolves country names
  and zones; adding the numeric DXCC entity id (and current/deleted status)
  would complete offline DXCC tracking without an online lookup.

## Awards, phase 2

- Per-band awards (DXCC by band, VUCC), DXCC Challenge, and current-vs-deleted
  entity validation, building on the awards summary that already exists.

## Contesting

- **Super Check Partial** — a `master.scp` callsign-hint dropdown during entry.
- **Cabrillo validation** — sanity-check a log against the selected contest's
  rules before export.

## Multi-station / server

- **Live remote-log mode** — view and log against a running `shacklog-server`
  directly, rather than pulling a periodic snapshot.
- Broaden the Field Day scoring/server work into a general multi-station
  networked-logging story.

## Housekeeping / tech debt

- **Consolidate ADIF parsing** — `server/WsjtxAdifReceiver` still carries its
  own ADIF field parser; fold it onto the shared `AdifReader` so there is one
  parser to maintain. *(Done — see CHANGELOG once landed.)*

---

Ideas and pull requests welcome — open an issue on
[GitHub](https://github.com/nigelfenton/shacklog/issues).
