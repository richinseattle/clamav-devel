#if HAVE_CONFIG_H
#include "clamav-config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if HAVE_UNISTD_H
#include <unistd.h>
#endif

#include <sys/types.h>
#include <fcntl.h>

#if !defined(_WIN32)
#include <sys/socket.h>
#include <netdb.h>
#include <sys/ioctl.h>
#endif

#if defined(HAVE_GETIFADDRS)
#include <net/if.h>
#if defined(HAVE_NET_IF_DL_H)
#include <net/if_dl.h>
#endif
#include <ifaddrs.h>
#endif

#if defined(SIOCGIFHWADDR)
#include <linux/sockios.h>
#endif

#include <errno.h>

#include "hostid.h"
#include "libclamav/md5.h"

struct device *get_device_entry(struct device *devices, size_t *ndevices, const char *name)
{
    struct device *device;
    void *p;
    size_t i;

    if ((devices)) {
        int found = 0;

        for (device = devices; device < devices + *ndevices; device++) {
            if (!strcmp(device->name, name)) {
                found = 1;
                break;
            }
        }

        if (!found) {
            p = realloc(devices, sizeof(struct device) * (*ndevices + 1));
            if (!(p)) {
                for (i=0; i < *ndevices; i++)
                    free(devices[i].name);
                free(devices);
                return NULL;
            }

            devices = p;
            device = devices + *ndevices;
            (*ndevices)++;
            memset(device, 0x00, sizeof(struct device));
        }
    } else {
        devices = calloc(1, sizeof(device));
        if (!(devices))
            return NULL;

        device = devices;
        *ndevices = 1;
    }

    if (!(device->name))
        device->name = strdup(name);

    return devices;
}

#if HAVE_GETIFADDRS
struct device *get_devices(void)
{
    struct ifaddrs *addrs=NULL, *addr;
    struct device *devices=NULL, *device=NULL;
    size_t ndevices=0, i;
    void *p;
    uint8_t *mac;
    int sock;

#if defined(SIOCGIFHWADDR)
    struct ifreq ifr;
#else
    struct sockaddr_dl *sdl;
#endif

    if (getifaddrs(&addrs))
        return NULL;

    for (addr = addrs; addr != NULL; addr = addr->ifa_next) {
        if (!(addr->ifa_addr))
            continue;

        /*
         * Even though POSIX (BSD) sockets define AF_LINK, Linux decided to be clever
         * and use AF_PACKET instead.
         */
#if defined(AF_PACKET)
        if (addr->ifa_addr->sa_family != AF_PACKET)
            continue;
#elif defined(AF_LINK)
        if (addr->ifa_addr->sa_family != AF_LINK)
            continue;
#else
        break; /* We don't support anything else */
#endif

        devices = get_device_entry(devices, &ndevices, addr->ifa_name);
        if (!(devices)) {
            freeifaddrs(addrs);
            return NULL;
        }

        /*
         * Grab the MAC address for all devices that have them.
         * Linux doesn't support (struct sockaddr_dl) as POSIX (BSD) sockets require.
         * Instead, Linux uses its own ioctl. This code only runs if we're not Linux,
         * Windows, or FreeBSD.
         */
#if !defined(SIOCGIFHWADDR)
        for (device = devices; device < devices + ndevices; device++) {
            if (!(strcmp(device->name, addr->ifa_name))) {
                sdl = (struct sockaddr_dl *)(addr->ifa_addr);

#if defined(LLADDR)
                mac = LLADDR(sdl);
#else
                mac = ((uint8_t *)(sdl->sdl_data + sdl->sdl_nlen));
#endif
                for (i=0; i<6; i++)
                    snprintf(device->mac+strlen(device->mac), sizeof(device->mac)-strlen(device->mac)-1, "%02x:", mac[i]);

                break;
            }
        }
#endif
    }

    if (addrs) {
        freeifaddrs(addrs);
        addrs = NULL;
    }

    /* This is the Linux version of getting the MAC addresses */
#if defined(SIOCGIFHWADDR)
    for (device = devices; device < devices + (ndevices); device++) {
        memset(&ifr, 0x00, sizeof(struct ifreq));

        strcpy(ifr.ifr_name, device->name);

        sock = socket(AF_INET, SOCK_DGRAM, 0);
        if (sock < 0)
            goto err;

        if (ioctl(sock, SIOCGIFHWADDR, &ifr)) {
            close(sock);
            goto err;
        }

        close(sock);
        mac = ((uint8_t *)(ifr.ifr_ifru.ifru_hwaddr.sa_data));

        for (i=0; i<6; i++)
            snprintf(device->mac+strlen(device->mac), sizeof(device->mac)-strlen(device->mac)-1, "%02x:", mac[i]);
    }
#endif

    close(sock);
    
    p = realloc(devices, sizeof(struct device) * (ndevices + 1));
    if (!(p))
        goto err;

    devices = p;
    devices[ndevices].name =  NULL;
    memset(devices[ndevices].mac, 0x00, sizeof(devices[ndevices].mac));

    return devices;

err:
    if (addrs)
        freeifaddrs(addrs);

    if (devices) {
        for (device = devices; device < devices + ndevices; device++)
            if (device->name)
                free(device->name);

        free(devices);
    }

    return NULL;
}
#else
struct device *get_devices(void)
{
    return NULL;
}
#endif /* HAVE_GETIFADDRS */

#if !HAVE_SYSCTLBYNAME && !defined(_WIN32)
/*
 * Since we're getting potentially sensitive data (MAC addresses for all devices on the system),
 * hash all the MAC addresses to provide basic anonymity and security.
 */
char *internal_get_host_id(void)
{
    size_t i;
    unsigned char raw_md5[16];
    char *printable_md5;
    cli_md5_ctx ctx;
    struct device *devices;

    devices = get_devices();
    if (!(devices))
        return NULL;

    printable_md5 = calloc(1, 33);
    if (!(printable_md5))
        return NULL;

    cli_md5_init(&ctx);
    for (i=0; devices[i].name != NULL; i++)
        cli_md5_update(&ctx, devices[i].mac, sizeof(devices[i].mac));

    cli_md5_final(raw_md5, &ctx);

    for (i=0; devices[i].name != NULL; i++)
        free(devices[i].name);
    free(devices);

    for (i=0; i < sizeof(raw_md5); i++)
        sprintf(printable_md5+(i*2), "%02x", raw_md5[i]);

    return printable_md5;
}
#endif
