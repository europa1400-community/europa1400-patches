/* Network fix (netfix) for the game server, server.dll of the hosting player.
 *
 * Own code only (docs/legal.md). What the original server does wrong, as observed behaviour:
 *
 * - Messages are framed as [u8 command, u16 total length, body]. Each server tick reads at most one message per client:
 *   the 3-byte header first, then the rest. When the rest has not arrived completely, the next tick continues with a
 *   corrupted read position. On a LAN a message practically always arrives in one piece; over VPNs or the Internet
 *   (smaller MTU, packet loss, retransmissions) messages arrive split, and the session breaks: garbage is processed as a
 *   message, or the client is dropped ("out of sync", lost connections).
 * - A message announcing more than 306 bytes overruns the per-client receive buffer of the server.
 * - The server's small messages are delayed by Nagle's algorithm.
 *
 * What this patch changes, only at the server's imports of recv, accept and closesocket:
 *
 * - recv: data of a client connection is buffered here. The server sees a message header only once the whole message
 *   has arrived and then gets the body in the same tick, so a message is always read completely in one tick.
 * - A length outside 4..max_message_length is reported as an aborted connection (WSAECONNABORTED); the server then drops
 *   that client the regular way instead of corrupting its memory.
 * - accept: TCP_NODELAY on client connections (setting tcp_nodelay).
 *
 * The server calls these functions from its own thread only; the state needs no locking. */
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <string.h>

#include "e1400patch/patch_api.h"

#define HEADER_SIZE 3
#define BUFFER_SIZE 0x10000
#define MAX_CONNECTIONS 16 /* the server has 8 client slots */

typedef struct Connection {
    SOCKET socket;
    int used;
    int closed;    /* the peer closed the connection (recv returned 0) */
    int error;     /* WinSock error that ended the connection, 0 = none */
    int body_left; /* body bytes of the message whose header was handed out */
    int waiting;   /* the message at the buffer start is incomplete (counted once) */
    int start, end;
    unsigned char data[BUFFER_SIZE];
} Connection;

static const E1400PatchApi *g_api;
static int(WSAAPI *g_recv)(SOCKET, char *, int, int);
static SOCKET(WSAAPI *g_accept)(SOCKET, struct sockaddr *, int *);
static int(WSAAPI *g_closesocket)(SOCKET);
static Connection g_connections[MAX_CONNECTIONS];
static int g_tcp_nodelay, g_max_length;
static struct {
    long connections, held_back, oversized, untracked;
} g_stats;

static Connection *connection_find(SOCKET socket)
{
    for (int i = 0; i < MAX_CONNECTIONS; i++)
        if (g_connections[i].used && g_connections[i].socket == socket) return &g_connections[i];
    return NULL;
}

static Connection *connection_get(SOCKET socket)
{
    Connection *connection = connection_find(socket);
    if (connection) return connection;
    for (int i = 0; i < MAX_CONNECTIONS; i++)
        if (!g_connections[i].used) {
            connection = &g_connections[i];
            connection->socket = socket;
            connection->used = 1;
            connection->closed = connection->error = connection->body_left = connection->waiting = 0;
            connection->start = connection->end = 0;
            return connection;
        }
    if (!g_stats.untracked++) g_api->log(g_api, "more than %d connections, further ones are not fixed", MAX_CONNECTIONS);
    return NULL;
}

static void connection_forget(SOCKET socket)
{
    Connection *connection = connection_find(socket);
    if (connection) connection->used = 0;
}

/* Moves everything the socket has into the buffer, without blocking (select with zero timeout). */
static void fill(Connection *connection)
{
    while (!connection->closed && !connection->error) {
        fd_set readable;
        struct timeval now = {0, 0};
        int ready, received;

        if (connection->start == connection->end) {
            connection->start = connection->end = 0;
        } else if (connection->end == BUFFER_SIZE && connection->start > 0) {
            memmove(connection->data, connection->data + connection->start, (size_t)(connection->end - connection->start));
            connection->end -= connection->start;
            connection->start = 0;
        }
        if (connection->end == BUFFER_SIZE) return;

        FD_ZERO(&readable);
        FD_SET(connection->socket, &readable);
        ready = select(0, &readable, NULL, NULL, &now);
        if (ready == 0) return;
        if (ready == SOCKET_ERROR) {
            connection->error = WSAGetLastError();
            return;
        }
        received = g_recv(connection->socket, (char *)connection->data + connection->end, BUFFER_SIZE - connection->end, 0);
        if (received > 0) {
            connection->end += received;
        } else if (received == 0) {
            connection->closed = 1;
        } else {
            int error = WSAGetLastError();
            if (error != WSAEWOULDBLOCK) connection->error = error;
            return;
        }
    }
}

/* Nothing to hand out: report what a direct recv would report (would block, closed or the error). */
static int no_data(const Connection *connection)
{
    if (connection->error) {
        WSASetLastError(connection->error);
        return SOCKET_ERROR;
    }
    if (connection->closed) return 0;
    WSASetLastError(WSAEWOULDBLOCK);
    return SOCKET_ERROR;
}

static int take(Connection *connection, char *buffer, int length)
{
    memcpy(buffer, connection->data + connection->start, (size_t)length);
    connection->start += length;
    return length;
}

static int WSAAPI netfix_recv(SOCKET socket, char *buffer, int length, int flags)
{
    Connection *connection;
    int available;

    if (flags != 0 || length <= 0 || !(connection = connection_get(socket))) return g_recv(socket, buffer, length, flags);
    fill(connection);
    available = connection->end - connection->start;

    if (connection->body_left > 0) { /* the rest of the message whose header was just handed out */
        int count = min(length, min(connection->body_left, available));
        if (count == 0) return no_data(connection);
        connection->body_left -= count;
        return take(connection, buffer, count);
    }
    if (length != HEADER_SIZE) { /* not a header request (not seen from the server): pass the bytes through */
        int count = min(length, available);
        return count ? take(connection, buffer, count) : no_data(connection);
    }
    if (available >= HEADER_SIZE) {
        const unsigned char *header = connection->data + connection->start;
        int total = header[1] | header[2] << 8;
        if (total <= HEADER_SIZE || total > g_max_length) {
            g_stats.oversized++;
            g_api->log(g_api, "message 0x%02x with length %d from socket %u: connection aborted", header[0], total,
                       (unsigned)socket);
            connection->error = WSAECONNABORTED;
            connection->start = connection->end = 0;
            return no_data(connection);
        }
        if (available >= total) {
            connection->body_left = total - HEADER_SIZE;
            connection->waiting = 0;
            return take(connection, buffer, HEADER_SIZE);
        }
        if (!connection->waiting) { /* the original would have read this message in pieces */
            connection->waiting = 1;
            g_stats.held_back++;
        }
    }
    return no_data(connection);
}

static SOCKET WSAAPI netfix_accept(SOCKET listener, struct sockaddr *address, int *address_length)
{
    SOCKET socket = g_accept(listener, address, address_length);
    if (socket != INVALID_SOCKET) {
        connection_forget(socket);
        g_stats.connections++;
        if (g_tcp_nodelay) {
            BOOL on = TRUE;
            if (setsockopt(socket, IPPROTO_TCP, TCP_NODELAY, (const char *)&on, sizeof(on)))
                g_api->log(g_api, "TCP_NODELAY failed: %d", WSAGetLastError());
        }
    }
    return socket;
}

static int WSAAPI netfix_closesocket(SOCKET socket)
{
    connection_forget(socket);
    return g_closesocket(socket);
}

E1400_PATCH_EXPORT int __cdecl e1400_patch_apply(const E1400PatchApi *api, E1400Target target)
{
    if (target != E1400_TARGET_SERVER || api->struct_size < sizeof(E1400PatchApi)) return 1;
    g_api = api;
    memset(g_connections, 0, sizeof(g_connections));
    memset(&g_stats, 0, sizeof(g_stats));
    g_tcp_nodelay = api->setting_int(api, "tcp_nodelay", 1);
    g_max_length = api->setting_int(api, "max_message_length", 306);
    if (g_max_length < 4 || g_max_length > 0xffff) g_max_length = 0xffff;

    if (api->hook_import(api, target, "WS2_32.dll", "#16", (void *)netfix_recv, (void **)&g_recv)) return 2;
    if (api->hook_import(api, target, "WS2_32.dll", "#1", (void *)netfix_accept, (void **)&g_accept)) return 3;
    if (api->hook_import(api, target, "WS2_32.dll", "#3", (void *)netfix_closesocket, (void **)&g_closesocket)) return 4;
    api->log(api, "build %s, tcp_nodelay=%d, max_message_length=%d",
             api->target(target)->build_id ? api->target(target)->build_id : "unknown", g_tcp_nodelay, g_max_length);
    return 0;
}

E1400_PATCH_EXPORT void __cdecl e1400_patch_release(const E1400PatchApi *api, E1400Target target)
{
    (void)target;
    api->log(api, "session: %ld connections, %ld split messages completed, %ld invalid lengths, %ld untracked",
             g_stats.connections, g_stats.held_back, g_stats.oversized, g_stats.untracked);
    memset(g_connections, 0, sizeof(g_connections));
}
