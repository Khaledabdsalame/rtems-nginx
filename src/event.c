#include "event.h"

EventSystem* event_system_init() {
    EventSystem* es = (EventSystem*) malloc(sizeof(EventSystem));
    if(!es) {
        fprintf(stderr, "Failed to allocate EventSystem\n");
        exit(EXIT_FAILURE);
    }
    es->nfds = 0;
    memset(es->events, 0, sizeof(es->events));
    memset(es->fds, 0, sizeof(es->fds));
    return es;
}


int epoll_ctl_rtems(EventSystem* es, int op, int fd, poll_event* event) {
    if(!es) return -1;

    if(op == EPOLL_CTL_ADD) {
        if(es->nfds >= MAX_EVENTS) return -1;
        es->fds[es->nfds].fd = fd;
        es->fds[es->nfds].events = event->events;
        es->fds[es->nfds].revents = 0;
        es->events[es->nfds] = *event;
        es->nfds++;
        return 0;
    }

    if(op == EPOLL_CTL_DEL) {
        for(int i = 0; i < es->nfds; i++) {
            if(es->fds[i].fd == fd) {
                es->fds[i] = es->fds[es->nfds-1];
                es->events[i] = es->events[es->nfds-1];
                es->nfds--;
                return 0;
            }
        }
        return -1;
    }

    if(op == EPOLL_CTL_MOD) {
        for(int i = 0; i < es->nfds; i++) {
            if(es->fds[i].fd == fd) {
                es->fds[i].events = event->events;
                es->events[i] = *event;
                return 0;
            }
        }
        return -1;
    }

    return -1;
}


void es_add(EventSystem* es, EventBase* data, uint32_t events) {
    printf("[ES] Adding fd %d, events=%u, nfds=%d\n", data->fd, events, es->nfds);
    fflush(stdout);
    poll_event ev;
    ev.events = events;
    ev.data.ptr = data;
    if(epoll_ctl_rtems(es, EPOLL_CTL_ADD, data->fd, &ev) == -1) {
        fprintf(stderr, "Failed to add event\n");
        exit(EXIT_FAILURE);
    }
    printf("[ES] Added, nfds now %d\n", es->nfds);
    fflush(stdout);
}


void es_mod(EventSystem* es, EventBase* data, uint32_t events) {
    poll_event ev;
    ev.events = events;
    ev.data.ptr = data;
    if(epoll_ctl_rtems(es, EPOLL_CTL_MOD, data->fd, &ev) == -1) {
        fprintf(stderr, "Failed to modify event\n");
        exit(EXIT_FAILURE);
    }
}


void es_del(EventSystem* es, int fd) {
    if(epoll_ctl_rtems(es, EPOLL_CTL_DEL, fd, NULL) == -1) {
        fprintf(stderr, "Failed to delete event\n");
        exit(EXIT_FAILURE);
    }
}


int es_wait(EventSystem* es, int timeout) {
    int ret = poll(es->fds, es->nfds, timeout);
    
    if (ret == -1) {
        printf("[ES_WAIT] poll returned-1\n");
    } else if (ret == 0) {
        // No output for timeout
    } else {
        printf("[ES_WAIT] %d ready:", ret);
        for (int i = 0; i < es->nfds; i++) {
            if (es->fds[i].revents) {
                printf(" fd%d(%x)", es->fds[i].fd, es->fds[i].revents);
            }
        }
        printf("\n");
    }
    fflush(stdout);
    
    if(ret > 0) {
        for(int i=0;i<es->nfds;i++){
            es->events[i].events = es->fds[i].revents;
        }
    }
    return ret;
}