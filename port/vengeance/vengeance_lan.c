#include "../core/native_winsock.h"

#include <arpa/inet.h>
#include <dispatch/dispatch.h>
#include <dns_sd.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

extern uint32_t ss_sendto(uint64_t base, uint32_t socket, uint32_t buffer,
                         uint32_t length, uint32_t flags, uint32_t address,
                         uint32_t address_length);

static const char service_type[] = "_tribesv._udp";
static const unsigned char lan_query[] = {0xfe, 0xfd, 0x02, 0, 0, 0, 0, 0};

typedef struct {
    DNSServiceRef connection;
    int socket;
    uint64_t generation;
} LanScan;

/* The serial queue owns the shared Bonjour connection, its child queries and
   the duplicate of GameSpy's socket. Replies go straight to the game's browser. */
static LanScan scan = {.socket = -1};
static dispatch_queue_t scan_queue;

static void stop_scan(void)
{
    if (scan.connection) DNSServiceRefDeallocate(scan.connection);
    if (scan.socket >= 0) close(scan.socket);
    scan.connection = NULL;
    scan.socket = -1;
}

static void DNSSD_API address_found(DNSServiceRef ref, DNSServiceFlags flags,
    uint32_t interface, DNSServiceErrorType error, const char *hostname,
    const struct sockaddr *address, uint32_t ttl, void *context)
{
    (void)interface;
    (void)hostname;
    (void)ttl;
    if (!error && (flags & kDNSServiceFlagsAdd) && address && address->sa_family == AF_INET) {
        struct sockaddr_in destination;
        memcpy(&destination, address, sizeof destination);
        destination.sin_port = (uint16_t)(uintptr_t)context;
        sendto(scan.socket, lan_query, sizeof lan_query, 0,
               (const struct sockaddr *)&destination, sizeof destination);
    }
    if (error || !(flags & kDNSServiceFlagsMoreComing)) DNSServiceRefDeallocate(ref);
}

static void DNSSD_API service_resolved(DNSServiceRef ref, DNSServiceFlags flags,
    uint32_t interface, DNSServiceErrorType error, const char *fullname,
    const char *hostname, uint16_t port, uint16_t txt_length,
    const unsigned char *txt, void *context)
{
    (void)flags;
    (void)fullname;
    (void)txt_length;
    (void)txt;
    (void)context;
    if (!error && port) {
        DNSServiceRef address_query = scan.connection;
        DNSServiceGetAddrInfo(&address_query, kDNSServiceFlagsShareConnection,
                              interface, kDNSServiceProtocol_IPv4, hostname,
                              address_found, (void *)(uintptr_t)port);
    }
    DNSServiceRefDeallocate(ref);
}

static void DNSSD_API service_found(DNSServiceRef ref, DNSServiceFlags flags,
    uint32_t interface, DNSServiceErrorType error, const char *name,
    const char *type, const char *domain, void *context)
{
    (void)ref;
    (void)context;
    if (error || !(flags & kDNSServiceFlagsAdd)) return;
    DNSServiceRef resolve = scan.connection;
    DNSServiceResolve(&resolve, kDNSServiceFlagsShareConnection, interface,
                      name, type, domain, service_resolved, NULL);
}

static void discover_servers(int socket)
{
    static dispatch_once_t once;
    dispatch_once(&once, ^{
        scan_queue = dispatch_queue_create("TribesPad.Vengeance.Bonjour", DISPATCH_QUEUE_SERIAL);
    });
    dispatch_async(scan_queue, ^{
        stop_scan();
        scan.socket = socket;
        uint64_t generation = ++scan.generation;
        DNSServiceErrorType error = DNSServiceCreateConnection(&scan.connection);
        if (!error) {
            DNSServiceRef browse = scan.connection;
            error = DNSServiceBrowse(&browse, kDNSServiceFlagsShareConnection, 0,
                                     service_type, "local.", service_found, NULL);
        }
        if (!error) error = DNSServiceSetDispatchQueue(scan.connection, scan_queue);
        if (error) {
            fprintf(stderr, "TribesPad: Vengeance Bonjour discovery failed (%d)\n", error);
            stop_scan();
            return;
        }
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 2 * NSEC_PER_SEC), scan_queue, ^{
            if (generation == scan.generation) stop_scan();
        });
    });
}

static bool starts_lan_scan(uint64_t base, uint32_t buffer, uint32_t length,
                            uint32_t flags, uint32_t address, uint32_t address_length)
{
    if (flags || length != sizeof lan_query || !buffer ||
        address_length < 16 || !address || buffer > UINT32_MAX - (sizeof lan_query - 1) ||
        address > UINT32_MAX - 15) return false;

    static const unsigned char first_destination[] = {2, 0, 0x1e, 0x62, 255, 255, 255, 255};
    return memcmp((const void *)(uintptr_t)(base + address),
                  first_destination, sizeof first_destination) == 0 &&
           memcmp((const void *)(uintptr_t)(base + buffer), lan_query, sizeof lan_query) == 0;
}

uint32_t vg_sendto(uint64_t base, uint32_t socket, uint32_t buffer, uint32_t length,
                   uint32_t flags, uint32_t address, uint32_t address_length)
{
    /* GameSpy starts each LAN broadcast sweep with this query to UDP 7778.
       Browse for advertised query endpoints once per sweep. */
    if (starts_lan_scan(base, buffer, length, flags, address, address_length)) {
        int duplicate = tribes_socket_duplicate(base, socket);
        if (duplicate >= 0) discover_servers(duplicate);
    }
    return ss_sendto(base, socket, buffer, length, flags, address, address_length);
}
