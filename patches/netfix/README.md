# netfix: network fix for the multiplayer host

Only the **hosting** player needs it: the game loads `server.dll` (and with it this patch) only when hosting. Clients
connect as before.

## What goes wrong in the original

The game server reads messages `[u8 command, u16 total length, body]` from each client, at most one per server tick:
the header first, then the rest. If the rest of a message has not fully arrived yet, the server continues the next tick
from a corrupted read position. On a LAN a message practically always arrives in one piece, so the bug rarely shows.
Over VPNs (Radmin, Hamachi, ZeroTier, ...) and the Internet, smaller packets, packet loss and retransmissions split
messages regularly; the server then processes garbage or drops the client: "out of sync" and lost connections.

Two smaller problems:

- A client announcing a message longer than 306 bytes makes the server write past its receive buffer for that client.
- The server's small messages are held back by Nagle's algorithm (extra latency on every reply).

## What the patch changes

All changes sit at three WinSock imports of `server.dll` (`recv`, `accept`, `closesocket`); no game function is
modified.

- `recv`: incoming data of every client connection is buffered by the patch. The server gets a message header only when
  the complete message is there, and the body right after it in the same tick. A message is therefore always read in
  one piece, however the network splits it.
- Lengths outside 4..`max_message_length` end the connection (`WSAECONNABORTED`): the server drops that client the
  regular way instead of corrupting memory.
- `accept`: `TCP_NODELAY` on client connections.

Settings (`e1400patch.ini`, section `[netfix]`):

| Key | Default | Meaning |
|---|---|---|
| `tcp_nodelay` | `1` | send small messages immediately |
| `max_message_length` | `306` | longest accepted message in bytes (the size of the server's receive buffer) |

The log (`e1400patch/logs/e1400patch.log`) shows one line per hosted session, for example
`netfix: session: 3 connections, 41 split messages completed, 0 invalid lengths, 0 untracked`. "Split messages
completed" counts messages the original server would have read in pieces.

## Tests

- `tests/test_netfix.c` (CI): a stand-in for `server.dll` with the same `recv` call pattern, real loopback TCP. Without
  the patch a message sent in two pieces is read in two pieces. With the patch 3000 messages cut into random pieces of
  1..64 bytes arrive complete and in order, every read is all-or-nothing; `TCP_NODELAY` is set; too long and too short
  lengths abort the connection; a peer closing mid-message is reported as closed after the complete messages.
- Real `server.dll` (DE 2.06): the patch applies (build identified, all three imports hooked). The recorded LAN
  session cannot prove the fix (it contains no split message) and diverges by design because the patch reads from the
  socket differently.
- Game test: host a multiplayer game over a VPN with the patch installed.

## Builds

`server-de-2.06` (German Gold 2.06, GOG/Steam). Other builds follow once their build tables exist.
