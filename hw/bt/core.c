/*
 * Convenience functions for bluetooth.
 *
 * Copyright (C) 2008 Andrzej Zaborowski  <balrog@zabor.org>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 or
 * (at your option) version 3 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu-common.h"
#include "net/net.h"
#include "hw/bt.h"

int ba_from_str(bdaddr_t *addr, const char *str)
{
    return sscanf(str, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
                  &addr->b[5], &addr->b[4], &addr->b[3],
                  &addr->b[2], &addr->b[1], &addr->b[0]) == 6 ? 0 : -1;
}

void ba_to_str(char *buf, const bdaddr_t *addr)
{
    sprintf(buf, "%02hhx:%02hhx:%02hhx:%02hhx:%02hhx:%02hhx",
            addr->b[5], addr->b[4], addr->b[3],
            addr->b[2], addr->b[1], addr->b[0]);
}

/* Slave implementations can ignore this */
static void bt_dummy_lmp_mode_change(struct bt_link_s *link)
{
}

/* Slaves should never receive these PDUs */
static void bt_dummy_lmp_connection_complete(struct bt_link_s *link)
{
    if (link->slave->reject_reason)
        fprintf(stderr, "%s: stray LMP_not_accepted received, fixme\n",
                        __FUNCTION__);
    else
        fprintf(stderr, "%s: stray LMP_accepted received, fixme\n",
                        __FUNCTION__);
    exit(-1);
}

static void bt_dummy_lmp_disconnect_master(struct bt_link_s *link)
{
    fprintf(stderr, "%s: stray LMP_detach received, fixme\n", __FUNCTION__);
    exit(-1);
}

static void bt_dummy_lmp_acl_resp(struct bt_link_s *link,
                const uint8_t *data, int start, int len)
{
    fprintf(stderr, "%s: stray ACL response PDU, fixme\n", __FUNCTION__);
    exit(-1);
}

/* Slaves that don't hold any additional per link state can use these */
static void bt_dummy_lmp_connection_request(struct bt_link_s *req)
{
    struct bt_link_s *link = g_malloc0(sizeof(struct bt_link_s));

    link->slave = req->slave;
    link->host = req->host;

    req->host->reject_reason = 0;
    req->host->lmp_connection_complete(link);
}

static void bt_dummy_lmp_disconnect_slave(struct bt_link_s *link)
{
    g_free(link);
}

static void bt_dummy_destroy(struct bt_device_s *device)
{
    bt_device_done(device);
    g_free(device);
}

static int bt_dev_idx = 0;

void bt_device_init(struct bt_device_s *dev, struct bt_scatternet_s *net)
{
    memset(dev, 0, sizeof(*dev));
    dev->inquiry_scan = 1;
    dev->page_scan = 1;

    dev->bd_addr.b[0] = bt_dev_idx & 0xff;
    dev->bd_addr.b[1] = bt_dev_idx >> 8;
    dev->bd_addr.b[2] = 0xd0;
    dev->bd_addr.b[3] = 0xba;
    dev->bd_addr.b[4] = 0xbe;
    dev->bd_addr.b[5] = 0xba;
    bt_dev_idx ++;

    /* Simple slave-only devices need to implement only .lmp_acl_data */
    dev->lmp_connection_complete = bt_dummy_lmp_connection_complete;
    dev->lmp_disconnect_master = bt_dummy_lmp_disconnect_master;
    dev->lmp_acl_resp = bt_dummy_lmp_acl_resp;
    dev->lmp_mode_change = bt_dummy_lmp_mode_change;
    dev->lmp_connection_request = bt_dummy_lmp_connection_request;
    dev->lmp_disconnect_slave = bt_dummy_lmp_disconnect_slave;

    dev->handle_destroy = bt_dummy_destroy;

    dev->net = net;
    dev->next = net->slave;
    net->slave = dev;
}

void bt_device_done(struct bt_device_s *dev)
{
    struct bt_device_s **p = &dev->net->slave;

    while (*p && *p != dev)
        p = &(*p)->next;
    if (*p != dev) {
        fprintf(stderr, "%s: bad bt device \"%s\"\n", __FUNCTION__,
                        dev->lmp_name ?: "(null)");
        exit(-1);
    }

    *p = dev->next;
}

int _bt_device_enumerate_properties_loop(struct bt_device_s *dev,
                int(*callback)(void*, const char*, const char*), void *opaque,
                const char **enumerables, size_t len)
{
    char buf[1024];
    const char *prop;
    size_t i;
    int ret;

    for (i = 0; i < len; i++) {
        prop = enumerables[i];
        if (bt_device_get_property(dev, prop, buf, sizeof buf) < 0) {
            continue;
        }

        ret = callback(opaque, prop, buf);
        if (ret < 0) {
            return ret;
        }
    }

    return 0;
}

int bt_device_enumerate_properties(struct bt_device_s *dev,
                int(*callback)(void*, const char*, const char*), void *opaque)
{
    static const char *enumerables[] = {
        "address", "cod", "discoverable", "name"
    };

    int ret;

    ret = _bt_device_enumerate_properties_loop(dev, callback, opaque,
                    enumerables, ARRAY_SIZE(enumerables));
    if (ret < 0) {
        return ret;
    }

    if (dev->enumerate_properties) {
        return (dev->enumerate_properties)(dev, callback, opaque);
    }

    return 0;
}

int bt_device_get_property(struct bt_device_s *dev, const char *property,
                char *out_buf, size_t out_len)
{
    if (!strcmp(property, "address")) {
        char str[BDADDR_BUF_LEN];

        ba_to_str(str, &dev->bd_addr);
        return snprintf(out_buf, out_len, str);
    }

    if (!strcmp(property, "cod")) {
        return snprintf(out_buf, out_len, "0x%02x%02x%02x",
                        dev->class[2], dev->class[1], dev->class[0]);
    }

    if (!strcmp(property, "discoverable")) {
        const char *ret;

        ret = dev->inquiry_scan == 0 ? "false" : "true";
        return snprintf(out_buf, out_len, ret);
    }

    if (!strcmp(property, "name")) {
        if (!dev->lmp_name) {
            if (out_len) *out_buf = '\0';
            return 0;
        }

        return snprintf(out_buf, out_len, dev->lmp_name);
    }

    if (dev->get_property) {
        return dev->get_property(dev, property, out_buf, out_len);
    }

    fprintf(stderr, "%s: getting unknown property \"%s\" to bt device \"%s\"\n",
                    __FUNCTION__, property, dev->lmp_name ?: "(null)");
    return -1;
}

int bt_device_set_property(struct bt_device_s *dev, const char *property,
                const char *value)
{
    if (!strcmp(property, "discoverable")) {
        if (!strcmp(value, "true")) {
            dev->inquiry_scan = 1;
        } else if (!strcmp(value, "false")) {
            dev->inquiry_scan = 0;
        } else {
            return -1;
        }

        return 0;
    }

    if (!strcmp(property, "name")) {
        if (dev->lmp_name) {
            g_free((void *) dev->lmp_name);
        }
        if (value) {
            dev->lmp_name = g_strdup(value);
        } else {
            dev->lmp_name = NULL;
        }

        return 0;
    }

    if (dev->set_property) {
        return dev->set_property(dev, property, value);
    }

    fprintf(stderr, "%s: setting unknown property \"%s\" to bt device \"%s\"\n",
                    __FUNCTION__, property, dev->lmp_name ?: "(null)");
    return -1;
}

struct bt_device_s *bt_scatternet_find_slave(struct bt_scatternet_s *net,
                const bdaddr_t *addr)
{
    struct bt_device_s *slave;

    slave = net->slave;
    while (slave) {
        if (!bacmp(&slave->bd_addr, addr)) {
            break;
        }

        slave = slave->next;
    }

    return slave;
}
