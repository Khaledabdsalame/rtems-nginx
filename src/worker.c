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

// http_worker_rtems.c
#include <rtems.h>
#include <rtems/bsd/bsd.h>
#include <ifaddrs.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <errno.h>
#include <poll.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "event.h"
#include "client.h"
#include "dbg.h"
#include "server.h"

#define PUBLIC_DIR "/www"
#define POLL_TIMEOUT 1000  // 1 second timeout for poll()

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

static void nginx_log_info(const char* fmt, ...) {
    time_t now = time(NULL);
    struct tm* tm_info = localtime(&now);
    char timestamp[32];
    strftime(timestamp, sizeof(timestamp), "%Y/%m/%d %H:%M:%S", tm_info);
    
    printf("%s [info] ", timestamp);
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
    printf("\n");
    fflush(stdout);
}

/* Client connection tracking */
typedef struct {
    HTTPClient* client;
    EventBase* event_base;
    int state; // 0=reading request, 1=sending response, 2=done
} ConnectionHandler;

static ConnectionHandler* connections[MAX_EVENTS];
static int num_connections = 0;

static void handle_client_read(ConnectionHandler* handler, EventSystem* es);
static void handle_client_write(ConnectionHandler* handler, EventSystem* es);
static void handle_server_accept(TCPServer* server, EventSystem* es);
static void parse_http_request(HTTPClient* client);
static void set_http_error_headers(HTTPClient* client, const char* type);
static void resolve_path(HTTPClient* client, const char* path);
static void decode_url(const char* src, char* dst, size_t dst_size);
static const char* get_mime_type(const char* path);

void run_worker_process(TCPServer* server) {
    nginx_log_notice("worker process started");
    
    // Initialize event system
    EventSystem* es = event_system_init();
    if (!es) {
        nginx_log_error("event system initialization failed");
        return;
    }
    
    // Add server socket to event system for poll()
    es_add(es, &server->event, POLLIN);
    nginx_log_notice("listening socket added to event system, fd=%d", server->event.fd);
    
    // Main poll loop
    while (1) {
        int poll_ret = es_wait(es, POLL_TIMEOUT);
        
        if (poll_ret < 0) {
            nginx_log_error("poll() failed: %s", strerror(errno));
            continue;
        }
        
        if (poll_ret == 0) {
            // Timeout occurred
            continue;
        }
        
        // Process ready file descriptors
        for (int i = 0; i < es->nfds; i++) {
            if (es->fds[i].revents == 0) continue;
            
            // Check if it's the server socket
            if (es->fds[i].fd == server->event.fd) {
                if (es->fds[i].revents & POLLIN) {
                    handle_server_accept(server, es);
                }
            } else {
                // It's a client connection
                for (int j = 0; j < num_connections; j++) {
                    if (connections[j] && connections[j]->client->event.fd == es->fds[i].fd) {
                        if (es->fds[i].revents & POLLIN) {
                            handle_client_read(connections[j], es);
                        } else if (es->fds[i].revents & POLLOUT) {
                            handle_client_write(connections[j], es);
                        }
                        break;
                    }
                }
            }
        }
    }
    
    free(es);
}

static void handle_server_accept(TCPServer* server, EventSystem* es) {
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);
    
    int client_fd = accept(server->event.fd, (struct sockaddr*)&client_addr, &addr_len);
    
    if (client_fd < 0) {
        nginx_log_error("accept() failed: %s", strerror(errno));
        return;
    }
    
    nginx_log_info("%s - - [client] accepted connection", 
             inet_ntoa(client_addr.sin_addr), "client");
    
    // Set non-blocking for client communication
    fcntl(client_fd, F_SETFL, O_NONBLOCK);
    int opt = 1;
    setsockopt(client_fd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));
    
    // Initialize client
    HTTPClient* client = http_client_init(client_fd, CLIENT_RECEIVING_REQUEST);
    
    // Create connection handler
    ConnectionHandler* handler = (ConnectionHandler*) malloc(sizeof(ConnectionHandler));
    handler->client = client;
    handler->event_base = &client->event;
    handler->state = 0; // Reading request
    
    if (num_connections < MAX_EVENTS - 1) {
        connections[num_connections++] = handler;
        
        // Add client to event system - monitor for reading
        es_add(es, &client->event, POLLIN);
    } else {
        nginx_log_error("too many active connections, closing: %s", inet_ntoa(client_addr.sin_addr));
        close(client_fd);
        free(client);
        free(handler);
    }
}

static void handle_client_read(ConnectionHandler* handler, EventSystem* es) {
    HTTPClient* client = handler->client;
    
    char buf[4096];
    ssize_t n = recv(client->event.fd, buf, sizeof(buf) - 1, 0);
    
    if (n <= 0) {
        // Client closed or error
        nginx_log_info("client disconnected, fd=%d", client->event.fd);
        
        // Remove from event system
        es_del(es, client->event.fd);
        
        // Close and clean up
        if (client->file_fd >= 0) close(client->file_fd);
        close(client->event.fd);
        free(client);
        free(handler);
        
        // Remove from connections array
        for (int i = 0; i < num_connections; i++) {
            if (connections[i] == handler) {
                connections[i] = connections[num_connections - 1];
                num_connections--;
                break;
            }
        }
        return;
    }
    
    // Append to request buffer
    if (client->request_len + n < sizeof(client->request)) {
        memcpy(client->request + client->request_len, buf, n);
        client->request_len += n;
        client->request[client->request_len] = '\0';
        
        // Check if we have complete HTTP headers
        if (strstr(client->request, "\r\n\r\n")) {
            // Parse and prepare response
            parse_http_request(client);
            
            // Switch to writing mode
            handler->state = 1;
            es_mod(es, &client->event, POLLOUT);
        }
    }
}

static void handle_client_write(ConnectionHandler* handler, EventSystem* es) {
    HTTPClient* client = handler->client;
    
    // Send headers if not sent yet
    if (client->headers_sent < client->headers_len) {
        ssize_t sent = send(client->event.fd, 
                           client->headers + client->headers_sent,
                           client->headers_len - client->headers_sent, 0);
        
        if (sent < 0) {
            nginx_log_error("send error: %s", strerror(errno));
            // Error - close connection
            es_del(es, client->event.fd);
            if (client->file_fd >= 0) close(client->file_fd);
            close(client->event.fd);
            free(client);
            free(handler);
            return;
        }
        
        client->headers_sent += sent;
    }
    
    // Send file if needed and headers are complete
    if (client->headers_sent >= client->headers_len && 
        !client->http_error && client->file_fd >= 0) {
        
        char fbuf[4096];
        size_t to_read = sizeof(fbuf);
        if (client->file_offset + to_read > client->file_size) {
            to_read = client->file_size - client->file_offset;
        }
        
        if (to_read > 0) {
            lseek(client->file_fd, client->file_offset, SEEK_SET);
            ssize_t n = read(client->file_fd, fbuf, to_read);
            
            if (n > 0) {
                ssize_t sent = send(client->event.fd, fbuf, n, 0);
                
                if (sent > 0) {
                    client->file_offset += sent;
                }
            }
        }
        
        // Check if file transfer is complete
        if (client->file_offset >= client->file_size) {
            handler->state = 2; // Mark as done
        }
    } else if (client->headers_sent >= client->headers_len) {
        // Error response or no file - we're done
        handler->state = 2;
    }
    
    // If done, close connection
    if (handler->state == 2) {
        
        es_del(es, client->event.fd);
        if (client->file_fd >= 0) close(client->file_fd);
        close(client->event.fd);
        free(client);
        free(handler);
        
        // Remove from connections array
        for (int i = 0; i < num_connections; i++) {
            if (connections[i] == handler) {
                connections[i] = connections[num_connections - 1];
                num_connections--;
                break;
            }
        }
    }
}

// ====== HTTP Helper Functions ======
static void parse_http_request(HTTPClient* client) {
    char* line_copy = (char*) malloc(client->request_len + 1);
    strcpy(line_copy, client->request);
    
    char* line = strtok(line_copy, "\r\n");
    if (!line) return set_http_error_headers(client, "400 Bad Request");

    char* method = strtok(line, " ");
    char* path = strtok(NULL, " ");
    char* version = strtok(NULL, " ");
    if (!method || !path || !version)
        return set_http_error_headers(client, "400 Bad Request");

    if (strncmp(method, "GET", 3) != 0) return set_http_error_headers(client, "501 Not Implemented");
    if (strncmp(version, "HTTP/1.", 7) != 0) return set_http_error_headers(client, "505 HTTP Version Not Supported");

    resolve_path(client, path);

    if (!client->http_error) {
        snprintf(client->headers, sizeof(client->headers),
                 "HTTP/1.1 200 OK\r\n"
                 "Content-Length: %ld\r\n"
                 "Content-Type: %s\r\n"
                 "Connection: close\r\n\r\n",
                 client->file_size, client->file_mime_type);
        client->headers_len = strlen(client->headers);
        nginx_log_notice("\"%s %s %s\" 200 %ld", method, path, version, client->file_size);
    } else {
        nginx_log_notice("\"%s %s %s\" %s", method, path, version, client->headers);
    }
    
    free(line_copy);
}

static void set_http_error_headers(HTTPClient* client, const char* type) {
    client->http_error = true;
    snprintf(client->headers, sizeof(client->headers),
             "HTTP/1.1 %s\r\n"
             "Content-Length: 0\r\n"
             "Content-Type: text/plain\r\n"
             "Connection: close\r\n\r\n",
             type);
    client->headers_len = strlen(client->headers);
    nginx_log_info("HTTP error: %s", type);
}

// --- File system helpers ---
static void resolve_path(HTTPClient* client, const char* path) {
    char decoded[MAX_PATH_BUFFER];
    decode_url(path, decoded, sizeof(decoded));

    if (decoded[strlen(decoded)-1] == '/')
        strncat(decoded, "index.html", sizeof(decoded) - strlen(decoded) - 1);

    char full[MAX_PATH_BUFFER];
    snprintf(full, sizeof(full), "%s/%s", PUBLIC_DIR, decoded);

    char resolved[MAX_PATH_BUFFER];
    if (realpath(full, resolved) == NULL)
        return set_http_error_headers(client, "404 Not Found");

    if (strncmp(resolved, PUBLIC_DIR, strlen(PUBLIC_DIR)) != 0)
        return set_http_error_headers(client, "404 Not Found");

    int fd = open(resolved, O_RDONLY);
    if (fd < 0) return set_http_error_headers(client, "404 Not Found");

    struct stat st;
    if (fstat(fd, &st) < 0 || !S_ISREG(st.st_mode)) {
        close(fd);
        return set_http_error_headers(client, "404 Not Found");
    }

    client->file_fd = fd;
    client->file_size = st.st_size;
    client->file_mime_type = get_mime_type(resolved);
}

static void decode_url(const char* src, char* dst, size_t dst_size) {
    size_t j = 0;
    for (size_t i = 0; src[i] != '\0' && j < dst_size-1; i++, j++) {
        if (src[i] == '%' && src[i+1] && src[i+2]) {
            unsigned int byte;
            sscanf(src+i+1, "%2x", &byte);
            dst[j] = (char) byte;
            i += 2;
        } else if (src[i] == '+') dst[j] = ' ';
        else dst[j] = src[i];
    }
    dst[j] = '\0';
}

static const char* get_mime_type(const char* path) {
    const char* dot = strrchr(path, '.');
    if (!dot || dot == path) return "application/octet-stream";
    const char* ext = dot + 1;

    if (!strcasecmp(ext, "html") || !strcasecmp(ext, "htm")) return "text/html";
    if (!strcasecmp(ext, "css")) return "text/css";
    if (!strcasecmp(ext, "js")) return "application/javascript";
    if (!strcasecmp(ext, "json")) return "application/json";
    if (!strcasecmp(ext, "txt")) return "text/plain";
    if (!strcasecmp(ext, "jpg") || !strcasecmp(ext, "jpeg")) return "image/jpeg";
    if (!strcasecmp(ext, "png")) return "image/png";
    if (!strcasecmp(ext, "gif")) return "image/gif";
    if (!strcasecmp(ext, "svg")) return "image/svg+xml";
    if (!strcasecmp(ext, "ico")) return "image/x-icon";
    if (!strcasecmp(ext, "woff")) return "font/woff";
    if (!strcasecmp(ext, "woff2")) return "font/woff2";
    if (!strcasecmp(ext, "ttf")) return "font/ttf";
    if (!strcasecmp(ext, "pdf")) return "application/pdf";
    if (!strcasecmp(ext, "zip")) return "application/zip";

    return "application/octet-stream";
}