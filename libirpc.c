/**
 * libirpc - libirpc.c
 * Copyright (C) 2010 Manuel Gebele
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 **/

#include "libirpc.h"

#include <stdio.h>
#include <string.h>
#include <libusb-1.0/libusb.h>
#include "libusbi.h"
#include "tpl.h"

// TODO: Add context handler for multiple clients.
static libusb_context *irpc_ctx = NULL;

// TODO: Add device handler for multiple clients.
static struct libusb_device_handle *irpc_handle = NULL; 

static int dbgmsg = 1;

#define dbgmsg(fmt, arg...) \
do {                       \
if (dbgmsg)               \
printf(fmt, ##arg);      \
} while (0)

#define IRPC_INT_FMT                "i"
#define IRPC_STR_INT_FMT            "ic#"
#define IRPC_STR_INT_INT_FMT        "iii#"
#define IRPC_DEV_FMT                "S(iiii)"
#define IRPC_DEVLIST_FMT            "iS(iiii)#"
#define IRPC_DESC_FMT               "S(iiiiiiiiiiiiii)i"    // retval
#define IRPC_PRID_VEID_FMT          "ii"
#define IRPC_DEV_HANDLE_FMT         "S($(iiii))"
#define IRPC_DEV_HANDLE_RET_FMT     "S($(iiii))i"           // retval
#define IRPC_DEV_HANDLE_INT_FMT     IRPC_DEV_HANDLE_RET_FMT
#define IRPC_DEV_HANDLE_INT_INT_FMT "S($(iiii))ii"
#define IRPC_CTRL_TRANSFER_FMT      "S($(iiii))iiiiii"
#define IRPC_CTRL_STR_INT_INT_FMT   "iic#"
#define IRPC_BULK_TRANSFER_FMT      "S($(iiii))ciii"
#define IRPC_CLEAR_HALT_FMT         "S($(iiii))c"
#define IRPC_STRING_DESC_FMT        "S($(iiii))ii"

// -----------------------------------------------------------------------------
#pragma mark Function Call Identification
// -----------------------------------------------------------------------------

void
irpc_send_func(irpc_func_t func, int sock)
{
    tpl_node *tn = tpl_map(IRPC_INT_FMT, &func);
    tpl_pack(tn, 0);
    tpl_dump(tn, TPL_FD, sock);
    tpl_free(tn);	
}

irpc_func_t
irpc_read_func(int sock)
{
    irpc_func_t func;
    tpl_node *tn = tpl_map(IRPC_INT_FMT, &func);
    tpl_load(tn, TPL_FD, sock);
    tpl_unpack(tn, 0);
    tpl_free(tn);
    
    return func;
}

// -----------------------------------------------------------------------------
#pragma mark libusb_init
// -----------------------------------------------------------------------------

irpc_retval_t
irpc_recv_usb_init(struct irpc_connection_info *ci)
{
    tpl_node *tn = NULL;
    irpc_retval_t retval = IRPC_FAILURE; 
    irpc_func_t func = IRPC_USB_INIT;
    int sock = ci->server_sock;
    
    irpc_send_func(func, sock);
    
    // Read usb_init packet.
    tn = tpl_map(IRPC_INT_FMT, &retval);
    tpl_load(tn, TPL_FD, sock);
    tpl_unpack(tn, 0);
    tpl_free(tn);
    
    return retval;
}

void
irpc_send_usb_init(struct irpc_connection_info *ci)
{
    tpl_node *tn = NULL;
    int sock = ci->client_sock;
    irpc_retval_t retval = libusb_init(&irpc_ctx);
    
    // Send usb_init packet.
    tn = tpl_map(IRPC_INT_FMT, &retval);
    tpl_pack(tn, 0);
    tpl_dump(tn, TPL_FD, sock);
    tpl_free(tn);
}

irpc_retval_t
irpc_usb_init(struct irpc_connection_info *ci,
              irpc_context_t ctx)
{
    irpc_retval_t retval = IRPC_SUCCESS;
    
    if (ctx == IRPC_CONTEXT_SERVER)
        (void)irpc_send_usb_init(ci);
    else
        retval = irpc_recv_usb_init(ci);
    
    return retval;
}

// -----------------------------------------------------------------------------
#pragma mark libusb_exit
// -----------------------------------------------------------------------------

void
irpc_usb_exit(struct irpc_connection_info *ci,
              irpc_context_t ctx)
{    
    if (ctx == IRPC_CONTEXT_SERVER) {
        libusb_exit(irpc_ctx);
        return;
    }
    irpc_func_t func = IRPC_USB_EXIT;
    int sock = ci->server_sock;
    
    irpc_send_func(func, sock);
    irpc_ctx = NULL;
}

// -----------------------------------------------------------------------------
#pragma mark libusb_get_device_list
// -----------------------------------------------------------------------------

void
irpc_recv_usb_get_device_list(struct irpc_connection_info *ci,
                              struct irpc_device_list *devlist)
{
    tpl_node *tn = NULL;
    irpc_func_t func = IRPC_USB_GET_DEVICE_LIST;
    int sock = ci->server_sock;
    
    irpc_send_func(func, sock);
    
    // Read usb_get_device_list packet.
    tn = tpl_map(IRPC_DEVLIST_FMT,
                 &devlist->n_devs,
                 devlist->devs,
                 IRPC_MAX_DEVS);
    tpl_load(tn, TPL_FD, sock);
    tpl_unpack(tn, 0);
    tpl_free(tn);
}

void
irpc_send_usb_get_device_list(struct irpc_connection_info *ci)
{
    tpl_node *tn = NULL;
    libusb_device **list = NULL;
    struct irpc_device_list devlist;
    int sock = ci->client_sock;
    
    bzero(&devlist, sizeof(struct irpc_device_list));
    
    int i;
    ssize_t cnt = libusb_get_device_list(irpc_ctx, &list);
    for (i = 0; i < cnt && i < IRPC_MAX_DEVS; i++) {
        libusb_device *dev = *(list + i);
        irpc_device *idev = &devlist.devs[i];
        idev->bus_number = dev->bus_number;
        idev->device_address = dev->device_address;
        idev->num_configurations = dev->num_configurations;
        idev->session_data = dev->session_data;
        devlist.n_devs++;
    }
    
    // Send usb_get_device_list packet.
    tn = tpl_map(IRPC_DEVLIST_FMT,
                 &devlist.n_devs,
                 devlist.devs,
                 IRPC_MAX_DEVS);
    tpl_pack(tn, 0);
    tpl_dump(tn, TPL_FD, sock);
    tpl_free(tn);
    
    libusb_free_device_list(list, 1);
}

void
irpc_usb_get_device_list(struct irpc_connection_info *ci,
                         irpc_context_t ctx,
                         struct irpc_device_list *devlist)
{
    if (ctx == IRPC_CONTEXT_SERVER)
        irpc_send_usb_get_device_list(ci);
    else
        irpc_recv_usb_get_device_list(ci, devlist);
}

// -----------------------------------------------------------------------------
#pragma mark libusb_get_device_descriptor
// -----------------------------------------------------------------------------

irpc_retval_t
irpc_recv_usb_get_device_descriptor(struct irpc_connection_info *ci,
                                    irpc_device *idev,
                                    struct irpc_device_descriptor *desc)
{
    tpl_node *tn = NULL;
    irpc_func_t func = IRPC_USB_GET_DEVICE_DESCRIPTOR;
    irpc_retval_t retval;
    int sock = ci->server_sock;
    
    irpc_send_func(func, sock);
    
    // Send irpc_device to server.
    tn = tpl_map(IRPC_DEV_FMT, idev);
    tpl_pack(tn, 0);
    tpl_dump(tn, TPL_FD, sock);
    tpl_free(tn);
    
    // Read libusb_get_device_descriptor packet.
    tn = tpl_map(IRPC_DESC_FMT, desc, &retval);
    tpl_load(tn, TPL_FD, sock);
    tpl_unpack(tn, 0);
    tpl_free(tn);
    
    return retval;
}

void
irpc_send_usb_get_device_descriptor(struct irpc_connection_info *ci)
{
    tpl_node *tn = NULL;
    irpc_retval_t retval = IRPC_SUCCESS;
    libusb_device *f = NULL;
    libusb_device **list = NULL;
    irpc_device idev;
    struct irpc_device_descriptor idesc;
    struct libusb_device_descriptor desc;
    int sock = ci->client_sock;
    
    bzero(&idesc, sizeof(struct irpc_device_descriptor));
    
    // Read irpc_device from client.
    tn = tpl_map(IRPC_DEV_FMT, &idev);
    tpl_load(tn, TPL_FD, sock);
    tpl_unpack(tn, 0);
    tpl_free(tn);
    
    // Find corresponding usb_device.
    int i;
    ssize_t cnt = libusb_get_device_list(irpc_ctx, &list);
    for (i = 0; i < cnt; i++) {
        libusb_device *dev = list[i];
        if (dev->session_data == idev.session_data) {
            f = dev;
            break;
        }
    }
    
    if (!f) {
        retval = IRPC_FAILURE;
        goto send;
    }
    
    if (libusb_get_device_descriptor(f, &desc) < 0) {
        retval = IRPC_FAILURE;
        goto send;
    }
    libusb_free_device_list(list, 1);
    
    // Success, build descriptor
    idesc.bLength = desc.bLength;
    idesc.bDescriptorType = desc.bDescriptorType;
    idesc.bcdUSB = desc.bcdUSB;
    idesc.bDeviceClass = desc.bDeviceClass;
    idesc.bDeviceSubClass = desc.bDeviceSubClass;
    idesc.bDeviceProtocol = desc.bDeviceProtocol;
    idesc.bMaxPacketSize0 = desc.bMaxPacketSize0;
    idesc.idVendor = desc.idVendor;
    idesc.idProduct = desc.idProduct;
    idesc.bcdDevice = desc.bcdDevice;
    idesc.iManufacturer = desc.iManufacturer;
    idesc.iProduct = desc.iProduct;
    idesc.iSerialNumber = desc.iSerialNumber;
    idesc.bNumConfigurations = desc.bNumConfigurations;
    
send:
    // Send libusb_get_device_descriptor packet.
    tn = tpl_map(IRPC_DESC_FMT, &idesc, &retval);
    tpl_pack(tn, 0);
    tpl_dump(tn, TPL_FD, sock);
    tpl_free(tn);  
}

irpc_retval_t
irpc_usb_get_device_descriptor(struct irpc_connection_info *ci,
                               irpc_context_t ctx,
                               irpc_device *idev,
                               struct irpc_device_descriptor *desc)
{
    irpc_retval_t retval = IRPC_SUCCESS;
    
    if (ctx == IRPC_CONTEXT_SERVER)
        (void)irpc_send_usb_get_device_descriptor(ci);
    else
        retval = irpc_recv_usb_get_device_descriptor(ci, idev, desc);
    
    return retval;
}

// -----------------------------------------------------------------------------
#pragma mark libusb_open_device_with_vid_pid
// -----------------------------------------------------------------------------

void
irpc_recv_usb_open_device_with_vid_pid(struct irpc_connection_info *ci,
                                       int vendor_id,
                                       int product_id,
                                       irpc_device_handle *handle)
{
    tpl_node *tn = NULL;
    irpc_func_t func = IRPC_USB_OPEN_DEVICE_WITH_VID_PID;
    int sock = ci->server_sock;
    
    irpc_send_func(func, sock);
    
    // Send vendor and product id to server.
    tn = tpl_map(IRPC_PRID_VEID_FMT, &vendor_id, &product_id);
    tpl_pack(tn, 0);
    tpl_dump(tn, TPL_FD, sock);
    tpl_free(tn);
    
    // Read libusb_open_device_with_vid_pid packet.
    tn = tpl_map(IRPC_DEV_HANDLE_FMT, handle);
    tpl_load(tn, TPL_FD, sock);
    tpl_unpack(tn, 0);
    tpl_free(tn);
}

void
irpc_send_usb_open_device_with_vid_pid(struct irpc_connection_info *ci)
{
    tpl_node *tn = NULL;
    irpc_device_handle ihandle;
    int vendor_id, product_id;
    int sock = ci->client_sock;
    
    bzero(&ihandle, sizeof(irpc_device_handle));
    
    // Read vendor and product id from client.
    tn = tpl_map(IRPC_PRID_VEID_FMT,
                 &vendor_id,
                 &product_id);
    tpl_load(tn, TPL_FD, sock);
    tpl_unpack(tn, 0);
    tpl_free(tn);
    
    // Close an already opened handle.
    if (irpc_handle) {
        libusb_close(irpc_handle);
        irpc_handle = NULL;
    }
    
    irpc_handle = libusb_open_device_with_vid_pid(irpc_ctx, vendor_id, product_id);
    if (!irpc_handle)
        goto send;
    
    ihandle.dev.bus_number = irpc_handle->dev->bus_number;
    ihandle.dev.device_address = irpc_handle->dev->device_address;
    ihandle.dev.num_configurations = irpc_handle->dev->num_configurations;
    ihandle.dev.session_data = irpc_handle->dev->session_data;
    
send:
    // Send libusb_open_device_with_vid_pid packet.
    tn = tpl_map(IRPC_DEV_HANDLE_FMT, &ihandle);
    tpl_pack(tn, 0);
    tpl_dump(tn, TPL_FD, sock);
    tpl_free(tn);
}

void
irpc_usb_open_device_with_vid_pid(struct irpc_connection_info *ci,
                                  irpc_context_t ctx,
                                  int vendor_id,
                                  int product_id,
                                  irpc_device_handle *handle)
{
    if (ctx == IRPC_CONTEXT_SERVER)
        irpc_send_usb_open_device_with_vid_pid(ci);
    else
        irpc_recv_usb_open_device_with_vid_pid(ci, vendor_id, product_id, handle);
}

// -----------------------------------------------------------------------------
#pragma mark libusb_close
// -----------------------------------------------------------------------------

void
irpc_recv_usb_close(struct irpc_connection_info *ci,
                    irpc_device_handle *ihandle)
{
    /*
     * TODO: Currently we can ignore ihandle since we hold
     * only one usb_device_handle handle.  Later we need
     * to add an identifier to the irpc_device_handle
     * structure in order to determine the correct handle.
     */
    irpc_func_t func = IRPC_USB_CLOSE;
    int sock = ci->server_sock;
    
    irpc_send_func(func, sock);
}

void
irpc_send_usb_close(struct irpc_connection_info *info)
{
    if (!irpc_handle) return;
    libusb_close(irpc_handle);
    irpc_handle = NULL;
}

void
irpc_usb_close(struct irpc_connection_info *ci,
               irpc_context_t ctx,
               irpc_device_handle *ihandle)
{
    if (ctx == IRPC_CONTEXT_SERVER)
        irpc_send_usb_close(ci);
    else
        irpc_recv_usb_close(ci, ihandle);
}

// -----------------------------------------------------------------------------
#pragma mark libusb_open
// -----------------------------------------------------------------------------

irpc_retval_t
irpc_recv_usb_open(struct irpc_connection_info *ci,
                   irpc_device_handle *handle,
                   irpc_device *dev)
{
    tpl_node *tn = NULL;
    irpc_retval_t retval;
    irpc_func_t func = IRPC_USB_OPEN;
    int sock = ci->server_sock;
    
    irpc_send_func(func, sock);
    
    // Send irpc_device to server.
    tn = tpl_map(IRPC_DEV_FMT, dev);
    tpl_pack(tn, 0);
    tpl_dump(tn, TPL_FD, sock);
    tpl_free(tn);
    
    // Read libusb_open packet.
    tn = tpl_map(IRPC_DEV_HANDLE_RET_FMT, handle, &retval);
    tpl_load(tn, TPL_FD, sock);
    tpl_unpack(tn, 0);
    tpl_free(tn);
    
    return retval;
}

void
irpc_send_usb_open(struct irpc_connection_info *ci)
{
    tpl_node *tn = NULL;
    irpc_retval_t retval = IRPC_SUCCESS;
    irpc_device idev;
    libusb_device *f = NULL;
    libusb_device **list = NULL;
    irpc_device_handle ihandle;
    int sock = ci->client_sock;
    
    // Read irpc_device from client.
    tn = tpl_map(IRPC_DEV_FMT, &idev);
    tpl_load(tn, TPL_FD, sock);
    tpl_unpack(tn, 0);
    tpl_free(tn);
    
    int i;
    ssize_t cnt = libusb_get_device_list(irpc_ctx, &list);
    for (i = 0; i < cnt; i++) {
        libusb_device *dev = list[i];
        if (dev->session_data == idev.session_data) {
            f = dev;
            break;
        }
    }
    
    if (!f) {
        retval = IRPC_FAILURE;
        goto send;
    }
    
    // Close an already opened handle.
    if (irpc_handle) {
        libusb_close(irpc_handle);
        irpc_handle = NULL;
    }
    
    if (libusb_open(f, &irpc_handle) != 0) {
        retval = IRPC_FAILURE;
        goto send;
    }
    libusb_free_device_list(list, 1);
    
    ihandle.dev.bus_number = irpc_handle->dev->bus_number;
    ihandle.dev.device_address = irpc_handle->dev->device_address;
    ihandle.dev.num_configurations = irpc_handle->dev->num_configurations;
    ihandle.dev.session_data = irpc_handle->dev->session_data;
    
send:
    // Send libusb_open packet.
    tn = tpl_map(IRPC_DEV_HANDLE_RET_FMT, &ihandle, &retval);
    tpl_pack(tn, 0);
    tpl_dump(tn, TPL_FD, sock);
    tpl_free(tn);
}

irpc_retval_t
irpc_usb_open(struct irpc_connection_info *ci,
              irpc_context_t ctx,
              irpc_device_handle *handle,
              irpc_device *dev)
{
    irpc_retval_t retval = IRPC_SUCCESS;
    
    if (ctx == IRPC_CONTEXT_SERVER)
        (void)irpc_send_usb_open(ci);
    else
        retval = irpc_recv_usb_open(ci, handle, dev);
    
    return retval;
}

// -----------------------------------------------------------------------------
#pragma mark libusb_claim_interface
// -----------------------------------------------------------------------------

irpc_retval_t
irpc_recv_usb_claim_interface(struct irpc_connection_info *ci,
                              irpc_device_handle *handle,
                              int intf)
{
    tpl_node *tn = NULL;
    irpc_retval_t retval;
    irpc_func_t func = IRPC_USB_CLAIM_INTERFACE;
    int sock = ci->server_sock;
    
    irpc_send_func(func, sock);
    
    // Send irpc_device_handle and interface to server.
    tn = tpl_map(IRPC_DEV_HANDLE_INT_FMT, handle, &intf);
    tpl_pack(tn, 0);
    tpl_dump(tn, TPL_FD, sock);
    tpl_free(tn);
    
    // Read libusb_claim_interface packet.
    tn = tpl_map(IRPC_INT_FMT, &retval);
    tpl_load(tn, TPL_FD, sock);
    tpl_unpack(tn, 0);
    tpl_free(tn);
    
    return retval;
}

void
irpc_send_usb_claim_interface(struct irpc_connection_info *ci)
{
    tpl_node *tn = NULL;
    irpc_retval_t retval = IRPC_SUCCESS;
    irpc_device_handle handle;
    int sock = ci->client_sock;
    int intf;
    
    // Read irpc_device_handle and interface from client.
    tn = tpl_map(IRPC_DEV_HANDLE_INT_FMT, &handle, &intf);
    tpl_load(tn, TPL_FD, sock);
    tpl_unpack(tn, 0);
    tpl_free(tn);
    
    if (libusb_claim_interface(irpc_handle, intf) != 0)
        retval = IRPC_FAILURE;
    
    // Send libusb_claim_interface packet.
    tn = tpl_map(IRPC_INT_FMT, &retval);
    tpl_pack(tn, 0);
    tpl_dump(tn, TPL_FD, sock);
    tpl_free(tn);
}

irpc_retval_t
irpc_usb_claim_interface(struct irpc_connection_info *ci,
                         irpc_context_t ctx,
                         irpc_device_handle *handle,
                         int intf)
{
    irpc_retval_t retval = IRPC_SUCCESS;
    
    if (ctx == IRPC_CONTEXT_SERVER)
        (void)irpc_send_usb_claim_interface(ci);
    else
        retval = irpc_recv_usb_claim_interface(ci, handle, intf);
    
    return retval;
}

// -----------------------------------------------------------------------------
#pragma mark libusb_release_interface
// -----------------------------------------------------------------------------

irpc_retval_t
irpc_recv_usb_release_interface(struct irpc_connection_info *ci,
                                irpc_device_handle *handle,
                                int intf)
{
    tpl_node *tn = NULL;
    irpc_retval_t retval;
    irpc_func_t func = IRPC_USB_RELEASE_INTERFACE;
    int sock = ci->server_sock;
    
    irpc_send_func(func, sock);
    
    // Send irpc_device_handle and interface to server.
    tn = tpl_map(IRPC_DEV_HANDLE_INT_FMT, handle, &intf);
    tpl_pack(tn, 0);
    tpl_dump(tn, TPL_FD, sock);
    tpl_free(tn);
    
    // Read libusb_release_interface packet.
    tn = tpl_map(IRPC_INT_FMT, &retval);
    tpl_load(tn, TPL_FD, sock);
    tpl_unpack(tn, 0);
    tpl_free(tn);
    
    return retval;
}

void
irpc_send_usb_release_interface(struct irpc_connection_info *ci)
{
    tpl_node *tn = NULL;
    irpc_retval_t retval = IRPC_SUCCESS;
    irpc_device_handle handle;
    int sock = ci->client_sock;
    int intf;
    
    // Read irpc_device_handle and interface from client.
    tn = tpl_map(IRPC_DEV_HANDLE_INT_FMT, &handle, &intf);
    tpl_load(tn, TPL_FD, sock);
    tpl_unpack(tn, 0);
    tpl_free(tn);
    
    if (libusb_release_interface(irpc_handle, intf) != 0)
        retval = IRPC_FAILURE;
    
    // Send libusb_release_interface packet.
    tn = tpl_map(IRPC_INT_FMT, &retval);
    tpl_pack(tn, 0);
    tpl_dump(tn, TPL_FD, sock);
    tpl_free(tn);
}

irpc_retval_t
irpc_usb_release_interface(struct irpc_connection_info *ci,
                           irpc_context_t ctx,
                           irpc_device_handle *handle,
                           int intf)
{
    irpc_retval_t retval = IRPC_SUCCESS;
    
    if (ctx == IRPC_CONTEXT_SERVER)
        (void)irpc_send_usb_release_interface(ci);
    else
        retval = irpc_recv_usb_release_interface(ci, handle, intf);
    
    return retval;
}

// -----------------------------------------------------------------------------
#pragma mark libusb_get_configuration
// -----------------------------------------------------------------------------

irpc_retval_t
irpc_recv_usb_get_configuration(struct irpc_connection_info *ci,
                                irpc_device_handle *handle,
                                int config)
{
    tpl_node *tn = NULL;
    irpc_retval_t retval;
    irpc_func_t func = IRPC_USB_GET_CONFIGURATION;
    int sock = ci->server_sock;
    
    irpc_send_func(func, sock);
    
    // Send irpc_device_handle and config to server.
    tn = tpl_map(IRPC_DEV_HANDLE_INT_FMT, handle, &config);
    tpl_pack(tn, 0);
    tpl_dump(tn, TPL_FD, sock);
    tpl_free(tn);
    
    // Read libusb_get_configuration packet.
    tn = tpl_map(IRPC_INT_FMT, &retval);
    tpl_load(tn, TPL_FD, sock);
    tpl_unpack(tn, 0);
    tpl_free(tn);
    
    return retval;
}

void
irpc_send_usb_get_configuration(struct irpc_connection_info *ci)
{
    tpl_node *tn = NULL;
    irpc_retval_t retval = IRPC_SUCCESS;
    irpc_device_handle handle;
    int sock = ci->client_sock;
    int config;
    
    // Read irpc_device_handle and config from client.
    tn = tpl_map(IRPC_DEV_HANDLE_INT_FMT, &handle, &config);
    tpl_load(tn, TPL_FD, sock);
    tpl_unpack(tn, 0);
    tpl_free(tn);
    
    if (libusb_get_configuration(irpc_handle, &config) != 0)
        retval = IRPC_FAILURE;
    
    // Send libusb_get_configuration packet.
    tn = tpl_map(IRPC_INT_FMT, &retval);
    tpl_pack(tn, 0);
    tpl_dump(tn, TPL_FD, sock);
    tpl_free(tn);
}

irpc_retval_t
irpc_usb_get_configuration(struct irpc_connection_info *ci,
                           irpc_context_t ctx,
                           irpc_device_handle *handle,
                           int config)
{
    irpc_retval_t retval = IRPC_SUCCESS;
    
    if (ctx == IRPC_CONTEXT_SERVER)
        (void)irpc_send_usb_get_configuration(ci);
    else
        retval = irpc_recv_usb_get_configuration(ci, handle, config);
    
    return retval;
}

// -----------------------------------------------------------------------------
#pragma mark libusb_set_configuration
// -----------------------------------------------------------------------------

irpc_retval_t
irpc_recv_usb_set_configuration(struct irpc_connection_info *ci,
                                irpc_device_handle *handle,
                                int config)
{
    tpl_node *tn = NULL;
    irpc_retval_t retval;
    irpc_func_t func = IRPC_USB_SET_CONFIGURATION;
    int sock = ci->server_sock;
    
    irpc_send_func(func, sock);
    
    // Send irpc_device_handle and config to server.
    tn = tpl_map(IRPC_DEV_HANDLE_INT_FMT, handle, &config);
    tpl_pack(tn, 0);
    tpl_dump(tn, TPL_FD, sock);
    tpl_free(tn);
    
    // Read libusb_set_configuration packet.
    tn = tpl_map(IRPC_INT_FMT, &retval);
    tpl_load(tn, TPL_FD, sock);
    tpl_unpack(tn, 0);
    tpl_free(tn);
    
    return retval;
}

void
irpc_send_usb_set_configuration(struct irpc_connection_info *ci)
{
    tpl_node *tn = NULL;
    irpc_retval_t retval = IRPC_SUCCESS;
    irpc_device_handle handle;
    int sock = ci->client_sock;
    int config;
    
    // Read irpc_device_handle and config from client.
    tn = tpl_map(IRPC_DEV_HANDLE_INT_FMT, &handle, &config);
    tpl_load(tn, TPL_FD, sock);
    tpl_unpack(tn, 0);
    tpl_free(tn);
    
    if (libusb_set_configuration(irpc_handle, config) != 0)
        retval = IRPC_FAILURE;
    
    // Send libusb_set_configuration packet.
    tn = tpl_map(IRPC_INT_FMT, &retval);
    tpl_pack(tn, 0);
    tpl_dump(tn, TPL_FD, sock);
    tpl_free(tn);
}

irpc_retval_t
irpc_usb_set_configuration(struct irpc_connection_info *ci,
                           irpc_context_t ctx,
                           irpc_device_handle *handle,
                           int config)
{
    irpc_retval_t retval = IRPC_SUCCESS;
    
    if (ctx == IRPC_CONTEXT_SERVER)
        (void)irpc_send_usb_set_configuration(ci);
    else
        retval = irpc_recv_usb_set_configuration(ci, handle, config);
    
    return retval;
}

// -----------------------------------------------------------------------------
#pragma mark libusb_set_interface_alt_setting
// -----------------------------------------------------------------------------

irpc_retval_t
irpc_recv_usb_set_interface_alt_setting(struct irpc_connection_info *ci,
                                        irpc_device_handle *handle,
                                        int intf,
                                        int alt_setting)
{
    tpl_node *tn = NULL;
    irpc_retval_t retval;
    irpc_func_t func = IRPC_USB_SET_INTERFACE_ALT_SETTING;
    int sock = ci->server_sock;
    
    irpc_send_func(func, sock);
    
    // Send irpc_device_handle, interface, and alt_setting to server.
    tn = tpl_map(IRPC_DEV_HANDLE_INT_INT_FMT,
                 handle,
                 &intf,
                 &alt_setting);
    tpl_pack(tn, 0);
    tpl_dump(tn, TPL_FD, sock);
    tpl_free(tn);
    
    // Read libusb_set_interface_alt_setting packet.
    tn = tpl_map(IRPC_INT_FMT, &retval);
    tpl_load(tn, TPL_FD, sock);
    tpl_unpack(tn, 0);
    tpl_free(tn);
    
    return retval;
}

void
irpc_send_usb_set_interface_alt_setting(struct irpc_connection_info *ci)
{
    tpl_node *tn = NULL;
    irpc_retval_t retval = IRPC_SUCCESS;
    irpc_device_handle handle;
    int sock = ci->client_sock;
    int intf, alt_setting;
    
    // Read irpc_device_handle, interface, and alt_setting from client.
    tn = tpl_map(IRPC_DEV_HANDLE_INT_INT_FMT,
                 &handle,
                 &intf,
                 &alt_setting);
    tpl_load(tn, TPL_FD, sock);
    tpl_unpack(tn, 0);
    tpl_free(tn);
    
    if (libusb_set_interface_alt_setting(irpc_handle, intf, alt_setting) != 0)
        retval = IRPC_FAILURE;
    
    // Send libusb_set_interface_alt_setting packet.
    tn = tpl_map(IRPC_INT_FMT, &retval);
    tpl_pack(tn, 0);
    tpl_dump(tn, TPL_FD, sock);
    tpl_free(tn);
}

irpc_retval_t
irpc_usb_set_interface_alt_setting(struct irpc_connection_info *ci,
                                   irpc_context_t ctx,
                                   irpc_device_handle *handle,
                                   int intf,
                                   int alt_setting)
{
    irpc_retval_t retval = IRPC_SUCCESS;
    
    if (ctx == IRPC_CONTEXT_SERVER)
        (void)irpc_send_usb_set_interface_alt_setting(ci);
    else
        retval = irpc_recv_usb_set_interface_alt_setting(ci, handle, intf, alt_setting);
    
    return retval;
}

// -----------------------------------------------------------------------------
#pragma mark libusb_reset_device
// -----------------------------------------------------------------------------

irpc_retval_t
irpc_recv_usb_reset_device(struct irpc_connection_info *ci,
                           irpc_device_handle *handle)
{
    tpl_node *tn = NULL;
    irpc_retval_t retval;
    irpc_func_t func = IRPC_USB_RESET_DEVICE;
    int sock = ci->server_sock;
    
    irpc_send_func(func, sock);
    
    // Send irpc_device_handle to server.
    tn = tpl_map(IRPC_DEV_HANDLE_FMT, handle);
    tpl_pack(tn, 0);
    tpl_dump(tn, TPL_FD, sock);
    tpl_free(tn);
    
    // Read libusb_reset_device packet.
    tn = tpl_map(IRPC_INT_FMT, &retval);
    tpl_load(tn, TPL_FD, sock);
    tpl_unpack(tn, 0);
    tpl_free(tn);
    
    return retval;
}

void
irpc_send_usb_reset_device(struct irpc_connection_info *ci)
{
    tpl_node *tn = NULL;
    irpc_retval_t retval = IRPC_SUCCESS;
    irpc_device_handle handle;
    int sock = ci->client_sock;
    
    // Read irpc_device_handle from client.
    tn = tpl_map(IRPC_DEV_HANDLE_FMT, &handle);
    tpl_load(tn, TPL_FD, sock);
    tpl_unpack(tn, 0);
    tpl_free(tn);
    
    if (libusb_reset_device(irpc_handle) != 0)
        retval = IRPC_FAILURE;
    
    // Send libusb_reset_device packet.
    tn = tpl_map(IRPC_INT_FMT, &retval);
    tpl_pack(tn, 0);
    tpl_dump(tn, TPL_FD, sock);
    tpl_free(tn);
}

irpc_retval_t
irpc_usb_reset_device(struct irpc_connection_info *ci,
                      irpc_context_t ctx,
                      irpc_device_handle *handle)
{
    irpc_retval_t retval = IRPC_SUCCESS;
    
    if (ctx == IRPC_CONTEXT_SERVER)
        (void)irpc_send_usb_reset_device(ci);
    else
        retval = irpc_recv_usb_reset_device(ci, handle);
    
    return retval;    
}

// -----------------------------------------------------------------------------
#pragma mark libusb_control_transfer
// -----------------------------------------------------------------------------

irpc_retval_t
irpc_recv_usb_control_transfer(struct irpc_connection_info *ci,
                               irpc_device_handle *handle,
                               int req_type,
                               int req,
                               int val,
                               int idx,
                               char data[],
                               int length,
                               int timeout,
                               int *status)
{
    tpl_node *tn = NULL;
    int retval;
    irpc_func_t func = IRPC_USB_CONTROL_TRANSFER;
    int sock = ci->server_sock;
    
    irpc_send_func(func, sock);
    
    tn = tpl_map(IRPC_CTRL_TRANSFER_FMT,
                 handle,
                 &req_type,
                 &req,
                 &val,
                 &idx,
                 &length,
                 &timeout);
    tpl_pack(tn, 0);
    tpl_dump(tn, TPL_FD, sock);
    tpl_free(tn);    
    
    // Read libusb_control_transfer packet.
    tn = tpl_map(IRPC_CTRL_STR_INT_INT_FMT, &retval, status, data, IRPC_MAX_DATA);
    tpl_load(tn, TPL_FD, sock);
    tpl_unpack(tn, 0);
    tpl_free(tn);
    
    return retval;
}

void
irpc_send_usb_control_transfer(struct irpc_connection_info *ci)
{
    tpl_node *tn = NULL;
    int retval, status = 0;
    irpc_device_handle handle;
    int req_type, req, val, idx, length, timeout;
    char data[IRPC_MAX_DATA];
    int sock = ci->client_sock;
    
    tn = tpl_map(IRPC_CTRL_TRANSFER_FMT,
                 &handle,
                 &req_type,
                 &req,
                 &val,
                 &idx,
                 &length,
                 &timeout);
    tpl_load(tn, TPL_FD, sock);
    tpl_unpack(tn, 0);
    tpl_free(tn);
    
    retval = libusb_control_transfer(irpc_handle,
                                     req_type,
                                     req,
                                     val,
                                     idx,
                                     data,
                                     length,
                                     timeout);
    // FIXME: Works but its no convenience method.
    if (retval >= 5)
    {
        status = (int)data[4];
    }
    
    // Send libusb_control_transfer packet.
    tn = tpl_map(IRPC_CTRL_STR_INT_INT_FMT, &retval, &status, &data, IRPC_MAX_DATA);
    tpl_pack(tn, 0);
    tpl_dump(tn, TPL_FD, sock);
    tpl_free(tn);
}

irpc_retval_t
irpc_usb_control_transfer(struct irpc_connection_info *ci,
                          irpc_context_t ctx,
                          irpc_device_handle *handle,
                          int req_type,
                          int req,
                          int val,
                          int idx,
                          char data[],
                          int length,
                          int timeout,
                          int *status)
{
    irpc_retval_t retval = IRPC_SUCCESS;
    
    if (ctx == IRPC_CONTEXT_SERVER)
        (void)irpc_send_usb_control_transfer(ci);
    else
        retval = irpc_recv_usb_control_transfer(ci, handle, req_type, req, val, idx, data, length, timeout, status);
    
    return retval;       
}

// -----------------------------------------------------------------------------
#pragma mark libusb_bulk_transfer
// -----------------------------------------------------------------------------

irpc_retval_t
irpc_recv_usb_bulk_transfer(struct irpc_connection_info *ci,
                            irpc_device_handle *handle,
                            char endpoint,
                            char data[],
                            int length,
                            int *transfered,
                            int timeout)
{
    tpl_node *tn = NULL;
    int retval;
    irpc_func_t func = IRPC_USB_BULK_TRANSFER;
    int sock = ci->server_sock;
    
    irpc_send_func(func, sock);
    
    tn = tpl_map(IRPC_BULK_TRANSFER_FMT,
                 handle,
                 &endpoint,
                 &length,
                 transfered,
                 &timeout);
    tpl_pack(tn, 0);
    tpl_dump(tn, TPL_FD, sock);
    tpl_free(tn);
    
    
    // Read libusb_bulk_transfer packet.
    tn = tpl_map(IRPC_STR_INT_INT_FMT, &retval, &transfered, data, IRPC_MAX_DATA);
    tpl_load(tn, TPL_FD, sock);
    tpl_unpack(tn, 0);
    tpl_free(tn);
    
    return retval;
}

void
irpc_send_usb_bulk_transfer(struct irpc_connection_info *ci)
{
    tpl_node *tn = NULL;
    irpc_retval_t retval = IRPC_SUCCESS;
    irpc_device_handle handle;
    char endpoint, data[IRPC_MAX_DATA];;
    int length, transfered, timeout;
    int sock = ci->client_sock;
    
    tn = tpl_map(IRPC_BULK_TRANSFER_FMT,
                 &handle,
                 &endpoint,
                 &length,
                 &transfered,
                 &timeout);
    tpl_load(tn, TPL_FD, sock);
    tpl_unpack(tn, 0);
    tpl_free(tn);
    
    retval = libusb_bulk_transfer(irpc_handle,
                                  endpoint,
                                  data,
                                  length,
                                  &transfered,
                                  timeout);
    // Send libusb_bulk_transfer packet.
    tn = tpl_map(IRPC_STR_INT_INT_FMT, &retval, &transfered, &data, IRPC_MAX_DATA);
    tpl_pack(tn, 0);
    tpl_dump(tn, TPL_FD, sock);
    tpl_free(tn);
}

irpc_retval_t
irpc_usb_bulk_transfer(struct irpc_connection_info *ci,
                       irpc_context_t ctx,
                       irpc_device_handle *handle,
                       char endpoint,
                       char data[],
                       int length,
                       int *transfered,
                       int timeout)
{
    irpc_retval_t retval = IRPC_SUCCESS;
    
    if (ctx == IRPC_CONTEXT_SERVER)
        (void)irpc_send_usb_bulk_transfer(ci);
    else
        retval = irpc_recv_usb_bulk_transfer(ci, handle, endpoint, data, length, transfered, timeout);
    
    return retval;       
}

// -----------------------------------------------------------------------------
#pragma mark libusb_clear_halt
// -----------------------------------------------------------------------------

irpc_retval_t
irpc_recv_usb_clear_halt(struct irpc_connection_info *ci,
                         irpc_device_handle *handle,
                         char endpoint)
{
    tpl_node *tn = NULL;
    irpc_retval_t retval;
    irpc_func_t func = IRPC_USB_CLEAR_HALT;
    int sock = ci->server_sock;
    
    irpc_send_func(func, sock);
    
    // Send irpc_device_handle, and endpoint to server.
    tn = tpl_map(IRPC_CLEAR_HALT_FMT, handle, &endpoint);
    tpl_pack(tn, 0);
    tpl_dump(tn, TPL_FD, sock);
    tpl_free(tn);
    
    // Read libusb_clear_halt packet.
    tn = tpl_map(IRPC_INT_FMT, &retval);
    tpl_load(tn, TPL_FD, sock);
    tpl_unpack(tn, 0);
    tpl_free(tn);
    
    return retval;
}

void
irpc_send_usb_clear_halt(struct irpc_connection_info *ci)
{
    tpl_node *tn = NULL;
    irpc_retval_t retval = IRPC_SUCCESS;
    irpc_device_handle handle;
    char endpoint;
    int sock = ci->client_sock;
    
    // Read irpc_device_handle, and endpoint to server.
    tn = tpl_map(IRPC_CLEAR_HALT_FMT, &handle, &endpoint);
    tpl_load(tn, TPL_FD, sock);
    tpl_unpack(tn, 0);
    tpl_free(tn);
    
    if (libusb_clear_halt(irpc_handle, endpoint) != 0)
        retval = IRPC_FAILURE;
    
    // Send libusb_clear_halt packet.
    tn = tpl_map(IRPC_INT_FMT, &retval);
    tpl_pack(tn, 0);
    tpl_dump(tn, TPL_FD, sock);
    tpl_free(tn);
}

irpc_retval_t
irpc_usb_clear_halt(struct irpc_connection_info *ci,
                    irpc_context_t ctx,
                    irpc_device_handle *handle,
                    char endpoint)
{
    irpc_retval_t retval = IRPC_SUCCESS;
    
    if (ctx == IRPC_CONTEXT_SERVER)
        (void)irpc_send_usb_clear_halt(ci);
    else
        retval = irpc_recv_usb_clear_halt(ci, handle, endpoint);
    
    return retval;
}

// -----------------------------------------------------------------------------
#pragma mark libusb_get_string_descriptor_ascii
// -----------------------------------------------------------------------------

irpc_retval_t
irpc_recv_usb_get_string_descriptor_ascii(struct irpc_connection_info *ci,
                                          irpc_device_handle *handle,
                                          int idx,
                                          char data[],
                                          int length)
{
    tpl_node *tn = NULL;
    int retval;
    irpc_func_t func = IRPC_USB_GET_STRING_DESCRIPTOR_ASCII;
    int sock = ci->server_sock;
    
    irpc_send_func(func, sock);
    
    tn = tpl_map(IRPC_STRING_DESC_FMT, handle, &idx, &length);
    tpl_pack(tn, 0);
    tpl_dump(tn, TPL_FD, sock);
    tpl_free(tn);
    
    tn = tpl_map(IRPC_STR_INT_FMT,
                 &retval,
                 data,
                 IRPC_MAX_DATA);
    tpl_load(tn, TPL_FD, sock);
    tpl_unpack(tn, 0);
    tpl_free(tn);
    
    return retval;
}

void
irpc_send_usb_get_string_descriptor_ascii(struct irpc_connection_info *ci)
{
    tpl_node *tn = NULL;
    int retval;
    irpc_device_handle handle;
    int length, idx;
    char data[IRPC_MAX_DATA];
    int sock = ci->client_sock;
    
    // Read irpc_device_handle, and endpoint to server.
    tn = tpl_map(IRPC_STRING_DESC_FMT, &handle, &idx, &length);
    tpl_load(tn, TPL_FD, sock);
    tpl_unpack(tn, 0);
    tpl_free(tn);
    
    retval = libusb_get_string_descriptor_ascii(irpc_handle, idx, data, length);
    
    // Send libusb_clear_halt packet.
    tn = tpl_map(IRPC_STR_INT_FMT, &retval, &data, IRPC_MAX_DATA);
    tpl_pack(tn, 0);
    tpl_dump(tn, TPL_FD, sock);
    tpl_free(tn);
}

irpc_retval_t
irpc_usb_get_string_descriptor_ascii(struct irpc_connection_info *ci,
                                     irpc_context_t ctx,
                                     irpc_device_handle *handle,
                                     int idx,
                                     char data[],
                                     int length)
{
    irpc_retval_t retval = IRPC_SUCCESS;
    
    if (ctx == IRPC_CONTEXT_SERVER)
        (void)irpc_send_usb_get_string_descriptor_ascii(ci);
    else
        retval = irpc_recv_usb_get_string_descriptor_ascii(ci, handle, idx, data, length);
    
    return retval;
}

// -----------------------------------------------------------------------------
#pragma mark Public API
// -----------------------------------------------------------------------------

irpc_retval_t
irpc_call(irpc_func_t func, irpc_context_t ctx, struct irpc_info *info)
{
    irpc_retval_t retval = IRPC_SUCCESS;
    
    switch (func)
    {
        case IRPC_USB_INIT:
            retval = irpc_usb_init(&info->ci, ctx);
            break;
        case IRPC_USB_EXIT:
            (void)irpc_usb_exit(&info->ci, ctx);
            break;
        case IRPC_USB_GET_DEVICE_LIST:
            (void)irpc_usb_get_device_list(&info->ci, ctx, &info->devlist);
            break;
        case IRPC_USB_GET_DEVICE_DESCRIPTOR:
            retval = irpc_usb_get_device_descriptor(&info->ci, ctx, &info->dev, &info->desc);
            break;
        case IRPC_USB_OPEN_DEVICE_WITH_VID_PID:
            (void)irpc_usb_open_device_with_vid_pid(&info->ci, ctx, info->vendor_id, info->product_id, &info->handle);
            break;
        case IRPC_USB_CLOSE:
            (void)irpc_usb_close(&info->ci, ctx, &info->handle);
            break;
        case IRPC_USB_OPEN:
            retval = irpc_usb_open(&info->ci, ctx, &info->handle, &info->dev);
            break;
        case IRPC_USB_CLAIM_INTERFACE:
            retval = irpc_usb_claim_interface(&info->ci, ctx, &info->handle, info->intf);
            break;
        case IRPC_USB_RELEASE_INTERFACE:
            retval = irpc_usb_release_interface(&info->ci, ctx, &info->handle, info->intf);
            break;
        case IRPC_USB_GET_CONFIGURATION:
            retval = irpc_usb_get_configuration(&info->ci, ctx, &info->handle, info->config);
            break;
        case IRPC_USB_SET_CONFIGURATION:
            retval = irpc_usb_set_configuration(&info->ci, ctx, &info->handle, info->config);
            break;
        case IRPC_USB_SET_INTERFACE_ALT_SETTING:
            retval = irpc_usb_set_interface_alt_setting(&info->ci, ctx, &info->handle, info->intf, info->alt_setting);
            break;
        case IRPC_USB_RESET_DEVICE:
            retval = irpc_usb_reset_device(&info->ci, ctx, &info->handle);
            break;
        case IRPC_USB_CONTROL_TRANSFER:
            retval = irpc_usb_control_transfer(&info->ci, ctx, &info->handle, info->req_type, info->req, info->val, info->idx, info->data, info->length, info->timeout, &info->status);
            break;
        case IRPC_USB_BULK_TRANSFER:
            retval = irpc_usb_bulk_transfer(&info->ci, ctx, &info->handle, info->endpoint, info->data, info->length, &info->transfered, info->timeout);
            break;
        case IRPC_USB_CLEAR_HALT:
            retval = irpc_usb_clear_halt(&info->ci, ctx, &info->handle, info->endpoint);
            break;
        case IRPC_USB_GET_STRING_DESCRIPTOR_ASCII:
            retval = irpc_usb_get_string_descriptor_ascii(&info->ci, ctx, &info->handle, info->idx, info->data, info->length);
            break;
        default:
            retval = IRPC_FAILURE;
            break;
    }
    
    return retval;
}
