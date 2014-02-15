/*
 * Virtual remote device for bluetooth.
 *
 * Copyright (C) 2014 Mozilla.org.
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

static int bt_remote_enumerate_properties(struct bt_device_s *dev,
                int(*callback)(void*, const char*, const char*), void *opaque)
{
    static const char *enumerables[] = {
        "is_remote"
    };

    return _bt_device_enumerate_properties_loop(dev, callback, opaque,
                    enumerables, ARRAY_SIZE(enumerables));
}

static int bt_remote_get_property(struct bt_device_s *dev, const char *property,
                char *out_buf, size_t out_len)
{
    if (!strcmp(property, "is_remote")) {
        return snprintf(out_buf, out_len, "true");
    }

    return -1;
}

static void bt_remote_handle_destroy(struct bt_device_s *dev)
{
    struct bt_remote_device_s *remote_dev;

    remote_dev = (struct bt_remote_device_s *)dev;
    bt_l2cap_device_done(&remote_dev->l2cap_dev);

    g_free(remote_dev);
}

void bt_remote_device_init(struct bt_remote_device_s *remote_dev,
                struct bt_scatternet_s *net)
{
    struct bt_l2cap_device_s *l2cap_dev;
    struct bt_device_s *dev;

    l2cap_dev = &remote_dev->l2cap_dev;
    bt_l2cap_device_init(l2cap_dev, net);
    bt_l2cap_sdp_init(l2cap_dev);

    dev = &l2cap_dev->device;
    dev->enumerate_properties = bt_remote_enumerate_properties;
    dev->get_property = bt_remote_get_property;
    dev->handle_destroy = bt_remote_handle_destroy;
}

struct bt_device_s *bt_remote_device_new(struct bt_scatternet_s *net)
{
    struct bt_remote_device_s *remote_dev;

    remote_dev = g_malloc0(sizeof(*remote_dev));
    if (!remote_dev) {
        return NULL;
    }

    bt_remote_device_init(remote_dev, net);

    return &remote_dev->l2cap_dev.device;
}
