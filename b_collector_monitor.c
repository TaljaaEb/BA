#define _WIN32_WINNT 0x0601
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <winhttp.h>
#include <stdio.h>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "winhttp.lib")

#define LISTEN_PORT 5051
#define C_HOST "127.0.0.1"
#define C_PORT 5000

#define MAX_ITEMS 128
#define BUF_SIZE 8192

char items[MAX_ITEMS][256];
int item_count = 0;

/* ---- Simple <custom> extractor ---- */
void extract_custom(const char* text) {
    const char* p = text;
    while ((p = strstr(p, "<custom>")) != NULL) {
        p += 8;
        const char* end = strstr(p, "</custom>");
        if (!end) break;
        int len = (int)(end - p);
        if (len > 0 && item_count < MAX_ITEMS) {
            strncpy(items[item_count], p, len);
            items[item_count][len] = 0;
            item_count++;
        }
        p = end + 9;
    }
}

/* ---- HTTP GET ---- */
void fetch_from_A() {
    HINTERNET hSession = WinHttpOpen(L"BCollector",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        NULL, NULL, 0);

    HINTERNET hConnect = WinHttpConnect(
        hSession, L"127.0.0.1", 8000, 0);

    HINTERNET hRequest = WinHttpOpenRequest(
        hConnect, L"GET", L"/itemlines",
        NULL, WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES, 0);

    WinHttpSendRequest(hRequest,
        WINHTTP_NO_ADDITIONAL_HEADERS, 0,
        NULL, 0, 0, 0);

    WinHttpReceiveResponse(hRequest, NULL);

    char buffer[BUF_SIZE] = {0};
    DWORD bytesRead = 0;

    WinHttpReadData(hRequest, buffer, BUF_SIZE - 1, &bytesRead);
    extract_custom(buffer);

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
}

/* ---- Send JSON to C ---- */
void send_to_C() {
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(C_PORT);
    inet_pton(AF_INET, C_HOST, &addr.sin_addr);

    connect(s, (SOCKADDR*)&addr, sizeof(addr));

    char json[4096] = "{\"source\":\"B\",\"transactions\":[";
    for (int i = 0; i < item_count; i++) {
        strcat(json, "\"");
        strcat(json, items[i]);
        strcat(json, "\"");
        if (i < item_count - 1) strcat(json, ",");
    }
    strcat(json, "]}");

    send(s, json, (int)strlen(json), 0);
    closesocket(s);
}

/* ---- Wait for SUCCESS ---- */
int main() {
    WSADATA wsa;
    WSAStartup(MAKEWORD(2,2), &wsa);

    SOCKET srv = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(LISTEN_PORT);
    addr.sin_addr.s_addr = INADDR_ANY;

    bind(srv, (SOCKADDR*)&addr, sizeof(addr));
    listen(srv, 5);

    printf("[B] Waiting for SUCCESS...\n");

    while (1) {
        SOCKET c = accept(srv, NULL, NULL);
        char buf[64] = {0};
        recv(c, buf, sizeof(buf), 0);
        closesocket(c);

        if (strcmp(buf, "SUCCESS") == 0) {
            printf("[B] SUCCESS received\n");
            item_count = 0;
            fetch_from_A();
            send_to_C();
        }
    }
}
