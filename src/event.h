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

#pragma once
#include <poll.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define MAX_EVENTS 1024

#define EPOLL_CTL_ADD 1
#define EPOLL_CTL_DEL 2
#define EPOLL_CTL_MOD 3

typedef union {
    void *ptr;
    int   fd;
} poll_data_t;

typedef struct poll_event {
    short       events;  // POLLIN, POLLOUT
    poll_data_t data;    // pointer to EventBase
} poll_event;

typedef enum { SERVER_EVENT, CLIENT_EVENT, SIGNAL_EVENT } EventType;

typedef struct EventBase {
    int fd;
    EventType type;
} EventBase;

typedef struct EventSystem {
    int nfds;
    poll_event events[MAX_EVENTS];       // معلومات مشابهة epoll_event
    struct pollfd fds[MAX_EVENTS];       // pollfd array
} EventSystem;

// دوال الواجهة
EventSystem* event_system_init();
int epoll_ctl_rtems(EventSystem* es, int op, int fd, poll_event* event);
void es_add(EventSystem* es, EventBase* data, uint32_t events);
void es_mod(EventSystem* es, EventBase* data, uint32_t events);
void es_del(EventSystem* es, int fd);
int es_wait(EventSystem* es, int timeout);