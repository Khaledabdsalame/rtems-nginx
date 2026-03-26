/*
 * MIT License
 * 
 * Copyright (c) 2025 Georgi Arnaudov
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <netinet/in.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "dbg.h"
#include "event.h"
#include "server.h"

/* Logging functions similar to nginx */
static void nginx_log_notice(const char* fmt, ...) {
    time_t now = time(NULL);
    struct tm* tm_info = localtime(&now);
    char timestamp[32];
    strftime(timestamp, sizeof(timestamp), "%Y/%m/%d %H:%M:%S", tm_info);
    
    printf("%s [notice] ", timestamp);
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
    printf("\n");
    fflush(stdout);
}

static void nginx_log_error(const char* fmt, ...) {
    time_t now = time(NULL);
    struct tm* tm_info = localtime(&now);
    char timestamp[32];
    strftime(timestamp, sizeof(timestamp), "%Y/%m/%d %H:%M:%S", tm_info);
    
    printf("%s [error] ", timestamp);
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
    printf("\n");
    fflush(stdout);
}

TCPServer* tcp_server_init() {
    TCPServer* server = (TCPServer*) malloc(sizeof(TCPServer));
    check_mem(server);

    server->port = 0;
    server->event.fd = 0;
    server->event.type = SERVER_EVENT;

    return server;
error:
    exit(EXIT_FAILURE);
}

void tcp_server_start(TCPServer* server, uint16_t port) {
    // Create socket WITHOUT non-blocking - we'll use blocking accept
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    check(fd != -1, "Server socket creation failed");
    nginx_log_notice("socket created fd=%d", fd);

    // bypass TIME_WAIT
    int opt = 1;
    int opt_result = setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    check(opt_result != -1, "Failed setting socket option (SO_REUSEADDR)");
    nginx_log_notice("SO_REUSEADDR set");

    // server address - bind to INADDR_ANY for now
    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = INADDR_ANY;

    // bind address to socket
    int br = bind(fd, (struct sockaddr*) &address, sizeof(address));
    check(br != -1, "Failed binding server address to socket");
    nginx_log_notice("socket bound to port %u", port);

    // open socket
    int lr = listen(fd, SOMAXCONN);
    check(lr != -1, "Failed to open server socket");
    nginx_log_notice("listening socket opened on fd %d", fd);
    fflush(stdout);

    server->event.fd = fd;
    server->port = port;

    return;
error:
    exit(EXIT_FAILURE);
}

void tcp_server_stop(TCPServer* server) {
    close(server->event.fd);
    return;
}
