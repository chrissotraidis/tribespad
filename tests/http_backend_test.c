/* Native dependency test. These are host allocators, NOT guest callbacks. */
#include <curl/curl.h>
#include <assert.h>
#include <inttypes.h>
#include <poll.h>
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define CHECK(call) assert((call) == CURLE_OK)
#define MULTI(call) assert((call) == CURLM_OK)
#ifdef TRIBES_CURL_GUEST_ALLOCATOR_TEST
#include "fixtures/curl_allocator_driver.h"
#else
#define MAGIC UINT64_C(0x7452494245534832)
typedef union Header Header;
union Header {
    max_align_t alignment;
    struct { uint64_t magic; size_t size; Header *previous, *next; } h;
};
static Header *allocations;
static size_t live, peak, counts[5];
static int fail_next;
static pthread_t owner;

static void owner_check(void) { assert(pthread_equal(owner, pthread_self())); }
static Header *header(void *p) {
    Header *h = (Header *)p - 1;
    uint64_t tail;
    assert(h->h.magic == MAGIC);
    memcpy(&tail, (char *)p + h->h.size, sizeof tail);
    assert(tail == MAGIC);
    return h;
}
static void *memory_malloc(size_t n) {
    owner_check(); counts[0]++;
    if(fail_next) { fail_next = 0; return NULL; }
    assert(n <= SIZE_MAX - sizeof(Header) - sizeof(uint64_t));
    Header *h = malloc(sizeof *h + n + sizeof(uint64_t));
    assert(h);
    h->h.magic = MAGIC; h->h.size = n; h->h.previous = NULL; h->h.next = allocations;
    if(allocations) allocations->h.previous = h;
    allocations = h;
    uint64_t tail = MAGIC;
    memcpy((char *)(h + 1) + n, &tail, sizeof tail);
    live++; if(live > peak) peak = live;
    return h + 1;
}
static void memory_free(void *p) {
    owner_check(); counts[1]++;
    if(!p) return;
    Header *h = header(p);
    if(h->h.previous) h->h.previous->h.next = h->h.next;
    else allocations = h->h.next;
    if(h->h.next) h->h.next->h.previous = h->h.previous;
    memset(p, 0xdd, h->h.size); h->h.magic = 0; live--; free(h);
}
static void *memory_realloc(void *p, size_t n) {
    owner_check(); counts[2]++;
    if(!p) return memory_malloc(n);
    Header *h = header(p);
    if(!n) { memory_free(p); return NULL; }
    void *q = memory_malloc(n);
    if(q) { memcpy(q, p, n < h->h.size ? n : h->h.size); memory_free(p); }
    return q;
}
static char *memory_strdup(const char *s) {
    owner_check(); counts[3]++;
    size_t n = strlen(s) + 1;
    char *p = memory_malloc(n);
    if(p) memcpy(p, s, n);
    return p;
}
static void *memory_calloc(size_t n, size_t size) {
    owner_check(); counts[4]++;
    if(size && n > SIZE_MAX / size) return NULL;
    void *p = memory_malloc(n * size);
    if(p) memset(p, 0, n * size);
    return p;
}
#endif
static void *unexpected_malloc(size_t n) { (void)n; abort(); }
static void unexpected_free(void *p) { (void)p; abort(); }
static void *unexpected_realloc(void *p, size_t n) { (void)p; (void)n; abort(); }
static char *unexpected_strdup(const char *s) { (void)s; abort(); }
static void *unexpected_calloc(size_t n, size_t s) { (void)n; (void)s; abort(); }

typedef struct { unsigned char bytes[100000]; size_t size; unsigned calls; int reject; } Body;
static size_t receive(char *data, size_t size, size_t count, void *opaque) {
    owner_check(); Body *body = opaque;
    assert(!size || count <= SIZE_MAX / size);
    size_t n = size * count;
    body->calls++;
    if(body->reject) return 0;
    assert(n <= sizeof body->bytes - body->size);
    memcpy(body->bytes + body->size, data, n); body->size += n;
    return n;
}
static unsigned requests;
static CURL *easy(const char *url, const char *ca, Body *body) {
    CURL *handle = curl_easy_init(); assert(handle);
    CHECK(curl_easy_setopt(handle, CURLOPT_URL, url));
    CHECK(curl_easy_setopt(handle, CURLOPT_NOPROXY, "*"));
    CHECK(curl_easy_setopt(handle, CURLOPT_NOSIGNAL, 1L));
    CHECK(curl_easy_setopt(handle, CURLOPT_TIMEOUT_MS, 5000L));
    CHECK(curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, receive));
    CHECK(curl_easy_setopt(handle, CURLOPT_WRITEDATA, body));
    if(ca) CHECK(curl_easy_setopt(handle, CURLOPT_CAINFO, ca));
    return handle;
}
static void response(CURL *handle, Body *body, size_t expected) {
    long status = 0;
    CHECK(curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &status));
    assert(status == 200 && body->size == expected && body->calls);
    for(size_t i = 0; i < expected; i++) assert(body->bytes[i] == (unsigned char)(i * 37 + 11));
    requests++;
}
static void url_build(char *out, size_t size, const char *base, const char *path) {
    int length = snprintf(out, size, "%s%s", base, path); assert(length > 0 && (size_t)length < size);
}
static int64_t now_ms(void) {
    struct timespec ts; assert(!clock_gettime(CLOCK_MONOTONIC, &ts));
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}
typedef struct { curl_socket_t fd; int events; unsigned used; } Socket;
typedef struct {
    CURLM *multi; Socket sockets[16]; int64_t deadline;
    unsigned socket_calls, timer_calls, removals, assigned;
} Loop;
static int socket_event(CURL *easy_handle, curl_socket_t fd, int what, void *opaque, void *private) {
    (void)easy_handle; owner_check(); Loop *loop = opaque; loop->socket_calls++;
    Socket *socket = private;
    if(what == CURL_POLL_REMOVE) {
        assert(socket && socket->used && socket->fd == fd);
        socket->used = 0; loop->removals++; return 0;
    }
    if(!socket) {
        for(size_t i = 0; i < 16; i++) if(!loop->sockets[i].used) { socket = &loop->sockets[i]; break; }
        assert(socket); socket->used = 1; socket->fd = fd;
        MULTI(curl_multi_assign(loop->multi, fd, socket)); loop->assigned++;
    }
    assert(socket->used && socket->fd == fd);
    socket->events = what;
    return 0;
}
static int timer_event(CURLM *multi, long timeout, void *opaque) {
    owner_check(); Loop *loop = opaque; assert(multi == loop->multi);
    loop->timer_calls++; loop->deadline = timeout < 0 ? -1 : now_ms() + timeout;
    return 0;
}
static void multi_transfer(const char *http, const char *https, const char *ca) {
    Loop loop = {0}; loop.deadline = -1;
    loop.multi = curl_multi_init(); assert(loop.multi);
    MULTI(curl_multi_setopt(loop.multi, CURLMOPT_SOCKETFUNCTION, socket_event));
    MULTI(curl_multi_setopt(loop.multi, CURLMOPT_SOCKETDATA, &loop));
    MULTI(curl_multi_setopt(loop.multi, CURLMOPT_TIMERFUNCTION, timer_event));
    MULTI(curl_multi_setopt(loop.multi, CURLMOPT_TIMERDATA, &loop));
    Body bodies[2] = {0}; CURL *handles[2]; char url[256];
    for(unsigned i = 0; i < 2; i++) {
        url_build(url, sizeof url, i ? https : http, "/bytes");
        handles[i] = easy(url, ca, &bodies[i]);
        MULTI(curl_multi_add_handle(loop.multi, handles[i]));
    }
    int running = 0;
    MULTI(curl_multi_socket_action(loop.multi, CURL_SOCKET_TIMEOUT, 0, &running));
    int64_t end = now_ms() + 10000;
    while(running) {
        assert(now_ms() < end);
        struct pollfd polls[16]; nfds_t count = 0;
        for(unsigned i = 0; i < 16; i++) if(loop.sockets[i].used) {
            Socket *socket = &loop.sockets[i]; short events = 0;
            if(socket->events == CURL_POLL_IN || socket->events == CURL_POLL_INOUT) events |= POLLIN;
            if(socket->events == CURL_POLL_OUT || socket->events == CURL_POLL_INOUT) events |= POLLOUT;
            polls[count++] = (struct pollfd){socket->fd, events, 0};
        }
        int64_t delay = loop.deadline < 0 ? 100 : loop.deadline - now_ms();
        if(delay < 0) delay = 0;
        if(delay > 100) delay = 100;
        int ready = poll(polls, count, (int)delay); assert(ready >= 0);
        for(nfds_t i = 0; i < count; i++) if(polls[i].revents) {
            int flags = 0;
            if(polls[i].revents & POLLIN) flags |= CURL_CSELECT_IN;
            if(polls[i].revents & POLLOUT) flags |= CURL_CSELECT_OUT;
            if(polls[i].revents & (POLLERR | POLLHUP | POLLNVAL)) flags |= CURL_CSELECT_ERR;
            MULTI(curl_multi_socket_action(loop.multi, polls[i].fd, flags, &running));
        }
        if(loop.deadline >= 0 && now_ms() >= loop.deadline)
            MULTI(curl_multi_socket_action(loop.multi, CURL_SOCKET_TIMEOUT, 0, &running));
    }
    unsigned completed = 0; int remaining; CURLMsg *message;
    while((message = curl_multi_info_read(loop.multi, &remaining))) {
        assert(message->msg == CURLMSG_DONE && message->data.result == CURLE_OK);
        unsigned i = message->easy_handle == handles[0] ? 0 : 1;
        assert(message->easy_handle == handles[i] && !(completed & (1u << i)));
        response(handles[i], &bodies[i], 65539); completed |= 1u << i;
    }
    assert(completed == 3);
    for(unsigned i = 0; i < 2; i++) {
        MULTI(curl_multi_remove_handle(loop.multi, handles[i])); curl_easy_cleanup(handles[i]);
    }
    MULTI(curl_multi_cleanup(loop.multi));
    assert(loop.socket_calls && loop.timer_calls && loop.assigned >= 2 && loop.removals == loop.assigned);
    printf("multi: socket=%u timer=%u assigned=%u removed=%u\n", loop.socket_calls, loop.timer_calls, loop.assigned, loop.removals);
}
int main(int argc, char **argv) {
    owner = pthread_self();
#ifdef TRIBES_CURL_GUEST_ALLOCATOR_TEST
    assert(argc == 5 || argc == 6);
    curl_allocator_test_setup();
    if(argc == 6 && !strncmp(argv[5], "protected-", 10)) {
        uint32_t address = curl_allocator_test_prepare_protected(argv[5]);
        assert(tribes_memory_permissions_set(address, 4096, !strcmp(argv[5], "protected-export") ? 1 : 0));
    }
    if(argc == 6 && !strcmp(argv[5], "wrong-thread")) {
        pthread_t thread;
        assert(!pthread_create(&thread, NULL, curl_allocator_test_thread, NULL));
        assert(!pthread_join(thread, NULL));
        return 99;
    }
    if(argc == 6) curl_allocator_test_fault(argv[5]);
#else
    assert(argc == 5);
#endif
    const char *http = argv[1], *https = argv[2], *wrong_host = argv[3], *ca = argv[4];
    /* Invalid arguments must not acquire a global reference. */
    assert(curl_global_init_mem(1, NULL, memory_free, memory_realloc, memory_strdup, memory_calloc) == CURLE_FAILED_INIT);
    CHECK(curl_global_init_mem(1, memory_malloc, memory_free, memory_realloc, memory_strdup, memory_calloc));
    /* Upstream's second init retains the first callbacks and acquires a ref. */
    CHECK(curl_global_init_mem(1, unexpected_malloc, unexpected_free, unexpected_realloc, unexpected_strdup, unexpected_calloc));
    curl_global_cleanup();
    const curl_version_info_data *version = curl_version_info(CURLVERSION_NOW);
    assert(!strcmp(version->version, "8.22.0"));
    assert(version->ssl_version && strstr(version->ssl_version, "LibreSSL/4.3.2"));
    assert((version->features & CURL_VERSION_SSL) && (version->features & CURL_VERSION_LIBZ));
    assert(!(version->features & (CURL_VERSION_ASYNCHDNS | CURL_VERSION_HTTP2 | CURL_VERSION_HTTP3)));
    assert(!strcmp(version->protocols[0], "http") && !strcmp(version->protocols[1], "https") && !version->protocols[2]);
    printf("version: %s\n", curl_version());
    CURLU *url_handle = curl_url(); assert(url_handle);
    assert(curl_url_set(url_handle, CURLUPART_URL, "https://localhost:8443/a%20b?q=hello", 0) == CURLUE_OK);
    char *part = NULL;
    assert(curl_url_get(url_handle, CURLUPART_PATH, &part, CURLU_URLDECODE) == CURLUE_OK);
    assert(!strcmp(part, "/a b")); curl_free(part);
    assert(curl_url_set(url_handle, CURLUPART_PORT, "99999", 0) == CURLUE_BAD_PORT_NUMBER);
    assert(strlen(curl_url_strerror(CURLUE_BAD_PORT_NUMBER)) > 0); curl_url_cleanup(url_handle);
    fail_next = 1; assert(!curl_easy_init() && !fail_next);
    fail_next = 1; assert(!curl_multi_init() && !fail_next);
    char url[256]; Body body = {0}; url_build(url, sizeof url, http, "/redirect");
    CURL *handle = easy(url, ca, &body);
    CHECK(curl_easy_setopt(handle, CURLOPT_FOLLOWLOCATION, 1L));
    CHECK(curl_easy_setopt(handle, CURLOPT_ACCEPT_ENCODING, ""));
    CHECK(curl_easy_perform(handle)); response(handle, &body, 65539);
    long redirects = 0; CHECK(curl_easy_getinfo(handle, CURLINFO_REDIRECT_COUNT, &redirects)); assert(redirects == 1);
    curl_easy_reset(handle); curl_easy_cleanup(handle);
    unsigned char post[4099]; for(size_t i = 0; i < sizeof post; i++) post[i] = (unsigned char)(i * 37 + 11);
    memset(&body, 0, sizeof body); url_build(url, sizeof url, https, "/echo"); handle = easy(url, ca, &body);
    struct curl_slist *headers = curl_slist_append(NULL, "Content-Type: application/octet-stream"); assert(headers);
    struct curl_slist *extended = curl_slist_append(headers, "X-TribesPad-Test: binary"); assert(extended); headers = extended;
    CHECK(curl_easy_setopt(handle, CURLOPT_HTTPHEADER, headers));
    CHECK(curl_easy_setopt(handle, CURLOPT_POSTFIELDSIZE, (long)sizeof post));
    CHECK(curl_easy_setopt(handle, CURLOPT_POSTFIELDS, post));
    CHECK(curl_easy_perform(handle)); response(handle, &body, sizeof post);
    curl_easy_cleanup(handle); curl_slist_free_all(headers);
    multi_transfer(http, https, ca);
    memset(&body, 0, sizeof body); url_build(url, sizeof url, https, "/bytes"); handle = easy(url, NULL, &body);
    assert(curl_easy_perform(handle) == CURLE_PEER_FAILED_VERIFICATION && !body.size); curl_easy_cleanup(handle);
    url_build(url, sizeof url, wrong_host, "/bytes"); handle = easy(url, ca, &body);
    assert(curl_easy_perform(handle) == CURLE_PEER_FAILED_VERIFICATION && !body.size); curl_easy_cleanup(handle);
    url_build(url, sizeof url, http, "/slow"); handle = easy(url, ca, &body);
    CHECK(curl_easy_setopt(handle, CURLOPT_TIMEOUT_MS, 50L));
    assert(curl_easy_perform(handle) == CURLE_OPERATION_TIMEDOUT && !body.size); curl_easy_cleanup(handle);
    body.reject = 1; url_build(url, sizeof url, http, "/bytes"); handle = easy(url, ca, &body);
    assert(curl_easy_perform(handle) == CURLE_WRITE_ERROR && body.calls && !body.size); curl_easy_cleanup(handle);
    assert(strlen(curl_easy_strerror(CURLE_WRITE_ERROR)) > 0);
    curl_global_cleanup();
    assert(!live && !allocations && peak > 10 && requests == 4);
    for(unsigned i = 0; i < 5; i++) assert(counts[i]);
#ifdef TRIBES_CURL_GUEST_ALLOCATOR_TEST
    curl_allocator_test_finish();
#endif
    printf("PASS: 4 real HTTP/TLS transfers; 2 TLS rejections; timeout; callback rejection; URL API; 2 allocation failures; no callback allocation leaks. peak=%zu malloc=%zu free=%zu realloc=%zu strdup=%zu calloc=%zu\n", peak, counts[0], counts[1], counts[2], counts[3], counts[4]);
    return 0;
}
