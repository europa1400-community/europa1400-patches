/* Test target for the netfix test: reads framed messages [u8 command, u16 total length, body] from a socket with the call
 * pattern of the game server: at most one message per call, the missing header bytes first, then the missing body bytes.
 * This reader itself is correct; the test checks that behind netfix every call either completes a message or reads
 * nothing, so a reader never has to continue a half-read message. */
#include <winsock2.h>

/* 1 = message complete, 0 = nothing (more) to read now, -1 = peer closed, otherwise -(WinSock error). */
static int result(int received)
{
    int error;
    if (received == 0) return -1;
    error = WSAGetLastError();
    return error == WSAEWOULDBLOCK ? 0 : -error;
}

__declspec(dllexport) int __cdecl reader_step(SOCKET socket, unsigned char *message, int *position)
{
    int received, length;
    if (*position < 3) {
        received = recv(socket, (char *)message + *position, 3 - *position, 0);
        if (received <= 0) return result(received);
        *position += received;
        if (*position < 3) return 0;
    }
    length = message[1] | message[2] << 8;
    received = recv(socket, (char *)message + *position, length - *position, 0);
    if (received <= 0) return result(received);
    *position += received;
    if (*position < length) return 0;
    *position = 0;
    return 1;
}

__declspec(dllexport) SOCKET __cdecl reader_accept(SOCKET listener)
{
    return accept(listener, NULL, NULL);
}

__declspec(dllexport) int __cdecl reader_close(SOCKET socket)
{
    return closesocket(socket);
}
