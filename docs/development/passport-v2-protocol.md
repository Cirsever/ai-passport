<p align="right">
  <a href="passport-v2-protocol.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Passport v2 wire contract

Implementation contract for the [pixel companion design](passport-pixel-ui-design.md).
All frames are flat UTF-8 JSON objects, under 512 bytes excluding the USB
`@passport ` prefix/newline. Numbers below are unsigned 32-bit integers.
Strings must be correctly JSON escaped, without truncating a UTF-8 code point.

## Connection and route

`host.hello` declares `protocol:2`, `bridge` (16 lowercase hex digits),
`sessions` and `companion` booleans. A changed bridge invalidates the current
route, requests and staging assets. The device sends periodic `device.hello`;
the host repeats its identity and current route when needed.

`session.query` carries `tx`. `session.select` adds `sid`; `session.cancel`
invalidates the earlier transaction and requests reconciliation. Transactions
increase within one connection. A stale transaction cannot overwrite a later
one. The host replies with `session.selected` carrying `tx`, `bridge`, `epoch`,
`sid`, `ide`, `title`, `state`, `summary`, `progress` and `writable`. Empty
`sid` means there is no selected session. No UI route changes until this
complete snapshot is accepted. Selection errors return `session.error` with
`tx` and `reason`; a timeout triggers a fresh query.

Every session task, approval and action carries `bridge`, `sid`, `epoch`.
Identifiers are opaque; the full native thread ID stays on the host. The
human-facing short number is never used for routing. Unscoped legacy task
and approval frames are rejected after v2 negotiation.

## Catalog and details

`session.list` requests a zero-based `page` and `tx`. The host sends
`session.catalog` (`tx`, `page`, `count` <=3, `total`) followed by exactly
`count` `session.entry` frames with the same `tx`, `index`, `sid`, `title`,
`ide`, `state` and `writable`. The device stages the page and displays it only
when complete. Repeated/late page records cannot mutate a newer page.

`approval.request` includes the route, `request_id`, `operation` (`edit`,
`command`, `review`), `summary`, `pages`, `allow` and `remaining_ms`.
Only a complete trustworthy scope enables `allow`. `approval.detail` requests
a page; `approval.page` responds with route/request identity, `page`, `pages`
and `text`. All details are retained on the host.

`approval.decision` carries the route/request and `decision` (`approve` or
`reject`). The UI waits for `approval.receipt` (`status`: `allowed`, `denied`,
`expired`, `unknown`). A receipt confirms the adapter submitted a decision
to the native request channel; it is not proof of task execution. No
session-wide grant is allowed. Lost receipts require a status query.

## Automatic companion transfer

Pet changes are made in the PC IDE only. The adapter detects the selected
asset revision, converts it locally, then transfers it during idle traffic.
Device ACKs are automatic; there is no user-facing sync command.

`companion.begin` carries the route, `asset` (SHA-256 hex digest), `bytes`,
`frames` and `name`. The implemented baseline requires `frames:1`; values up
to four are reserved for a later negotiated capability. The packed asset is
32 bytes of little-endian RGB565 palette followed by `frames * 512` bytes of
row-major 4-bit indices (high nibble first). Palette index zero is transparent;
every frame is 32 × 32.

`companion.chunk` includes `asset`, `offset`, `data` (base64 of <=128 bytes).
`companion.commit` requests validation. Each response is `companion.ack`
(`asset`, `offset`, `status`: `receiving`, `ready`, `retry`, `rejected`).
The sender retries an unacknowledged offset and checks the receiver's offset.
Only a complete matching hash can replace the current asset. Keep the previous
asset on failure and ignore transfer fragments from an old route. Transport
and storage work run outside the button callback. Approval and voice traffic
take priority over asset chunks.

## Compatibility

Legacy protocol-1 adapters retain single-session behavior. v2 capabilities
must be negotiated before routed actions. Unsupported IDE backends expose
read-only state rather than pretending a synthetic conversation is routable.
The default partition layout stays unchanged.
