/*-------------------------------------------------------------

usbstorage_starlet.c -- USB mass storage support, inside starlet
Copyright (C) 2009 Kwiirk

If this driver is linked before libogc, this will replace the original 
usbstorage driver by svpe from libogc
This software is provided 'as-is', without any express or implied
warranty.  In no event will the authors be held liable for any
damages arising from the use of this software.

Permission is granted to anyone to use this software for any
purpose, including commercial applications, and to alter it and
redistribute it freely, subject to the following restrictions:

1.	The origin of this software must not be misrepresented; you
must not claim that you wrote the original software. If you use
this software in a product, an acknowledgment in the product
documentation would be appreciated but is not required.

2.	Altered source versions must be plainly marked as such, and
must not be misrepresented as being the original software.

3.	This notice may not be removed or altered from any source
distribution.

-------------------------------------------------------------*/

#include "mem.h"
#include "syscalls.h"
#include "timer.h"
#include "types.h"
#include "usbstorage.h"

#include <stdio.h>
#include <string.h>

#define DEVICE_TYPE_WII_USB		(('W'<<24)|('U'<<16)|('S'<<8)|'B')

/* IOCTL commands */
#define UMS_BASE			(('U'<<24)|('M'<<16)|('S'<<8))
#define USB_IOCTL_UMS_INIT	        (UMS_BASE+0x1)
#define USB_IOCTL_UMS_GET_CAPACITY      (UMS_BASE+0x2)
#define USB_IOCTL_UMS_READ_SECTORS      (UMS_BASE+0x3)
#define USB_IOCTL_UMS_WRITE_SECTORS	(UMS_BASE+0x4)
#define USB_IOCTL_UMS_READ_STRESS	(UMS_BASE+0x5)
#define USB_IOCTL_UMS_SET_VERBOSE	(UMS_BASE+0x6)

/* Constants */
#define USB_MAX_SECTORS			64

/* Variables */
static char fs[] ATTRIBUTE_ALIGN(32) = "/dev/usb/ehc";
static s32  fd = -1;

static u32  sectorSz = 0;


bool __usbstorage_Read(u32 sector, u32 numSectors, void *buffer)
{
	STACK_ALIGN(ioctlv, vector,     3, 32);
	STACK_ALIGN(u32,   _sector,     1, 32);
	STACK_ALIGN(u32,   _numSectors, 1, 32);

	u32 cnt, len = (sectorSz * numSectors);
	s32 ret;

	/* Device not opened */
	if (fd < 0)
		return false;

	/* Sector info */
	*_sector     = sector;
	*_numSectors = numSectors;

	/* Setup vector */
	vector[0].data = _sector;
	vector[0].len  = sizeof(u32);
	vector[1].data = _numSectors;
	vector[1].len  = sizeof(u32);
	vector[2].data = buffer;
	vector[2].len  = len;

	/* Flush cache */
	for (cnt = 0; cnt < 3; cnt++)
		os_sync_after_write(vector[cnt].data, vector[cnt].len);

	os_sync_after_write(vector, sizeof(ioctlv) * 3);

	/* Read data */
	ret = os_ioctlv(fd, USB_IOCTL_UMS_READ_SECTORS, 2, 1, vector);
	if (ret < 0)
		return false;

	/* Invalidate cache */
	for (cnt = 0; cnt < 3; cnt++)
		os_sync_before_read(vector[cnt].data, vector[cnt].len);

	return true;
}

bool __usbstorage_Write(u32 sector, u32 numSectors, void *buffer)
{
	STACK_ALIGN(ioctlv, vector,     3, 32);
	STACK_ALIGN(u32,   _sector,     1, 32);
	STACK_ALIGN(u32,   _numSectors, 1, 32);

	u32 cnt, len = (sectorSz * numSectors);
	s32 ret;

	/* Device not opened */
	if (fd < 0)
		return false;

	/* Sector info */
	*_sector     = sector;
	*_numSectors = numSectors;

	/* Setup vector */
	vector[0].data = _sector;
	vector[0].len  = sizeof(u32);
	vector[1].data = _numSectors;
	vector[1].len  = sizeof(u32);
	vector[2].data = buffer;
	vector[2].len  = len;

	/* Flush cache */
	for (cnt = 0; cnt < 3; cnt++)
		os_sync_after_write(vector[cnt].data, vector[cnt].len);

	os_sync_after_write(vector, sizeof(ioctlv) * 3);

	/* Write data */
	ret = os_ioctlv(fd, USB_IOCTL_UMS_WRITE_SECTORS, 3, 0, vector);
	if (ret < 0)
		return false;

	/* Invalidate cache */
	for (cnt = 0; cnt < 3; cnt++)
		os_sync_before_read(vector[cnt].data, vector[cnt].len);

	return true;
}

s32 __usbstorage_GetCapacity(u32 *_sectorSz)
{
	STACK_ALIGN(ioctlv, vector, 1, 32);
	STACK_ALIGN(u32,    buffer, 1, 32);

	if (fd >= 0) {
		s32 ret;

		/* Setup vector */
		vector[0].data = buffer;
		vector[0].len  = sizeof(u32);

		os_sync_after_write(vector, sizeof(ioctlv));

		/* Get capacity */
		ret = os_ioctlv(fd, USB_IOCTL_UMS_GET_CAPACITY, 0, 1, vector);

		os_sync_after_write(buffer, sizeof(u32));

		/* Set sector size */
		sectorSz = buffer[0];

		if (ret && _sectorSz)
			*_sectorSz = sectorSz;

		return ret;
	}

	return IPC_ENOENT;
}


bool usbstorage_Init(void)
{
	s32 ret;

	/* Already open */
	if (fd >= 0)
		return true;

	/* Open USB device */
	fd = os_open(fs, 0);
	if (fd < 0)
		return false;

	/* Initialize USB storage */
	os_ioctlv(fd, USB_IOCTL_UMS_INIT, 0, 0, NULL);

	/* Get device capacity */
	ret = __usbstorage_GetCapacity(NULL);
	if (ret <= 0)
		goto err;

	return true;

err:
	/* Close USB device */
	usbstorage_Shutdown();	

	return false;
}

bool usbstorage_Shutdown(void)
{
	if (fd >= 0) {
		/* Close USB device */
		os_close(fd);

		/* Remove descriptor */
		fd = -1;
	}

	return true;
}

bool usbstorage_IsInserted(void)
{
	s32 ret;

	/* Get device capacity */
	ret = __usbstorage_GetCapacity(NULL);

	return (ret > 0);
}

bool usbstorage_ReadSectors(u32 sector, u32 numSectors, void *buffer)
{
	u32 cnt = 0;
	s32 ret;

	/* Device not opened */
	if (fd < 0)
		return false;

	while (cnt < numSectors) {
		void *ptr = (char *)buffer + (sectorSz * cnt);

		u32  _sector     = sector + cnt;
		u32  _numSectors = numSectors - cnt;

		/* Limit sector count */
		if (_numSectors > USB_MAX_SECTORS)
			_numSectors = USB_MAX_SECTORS;

		/* Read sectors */
		ret = __usbstorage_Read(_sector, _numSectors, ptr);
		if (!ret)
			return false;

		/* Increase counter */
		cnt += _numSectors;
	}

	return true;
}

bool usbstorage_WriteSectors(u32 sector, u32 numSectors, void *buffer)
{
	u32 cnt = 0;
	s32 ret;

	/* Device not opened */
	if (fd < 0)
		return false;

	while (cnt < numSectors) {
		void *ptr = (char *)buffer + (sectorSz * cnt);

		u32  _sector     = sector + cnt;
		u32  _numSectors = numSectors - cnt;

		/* Limit sector count */
		if (_numSectors > USB_MAX_SECTORS)
			_numSectors = USB_MAX_SECTORS;

		/* Write sectors */
		ret = __usbstorage_Write(_sector, _numSectors, ptr);
		if (!ret)
			return false;

		/* Increase counter */
		cnt += _numSectors;
	}

	return true;
}

bool usbstorage_ClearStatus(void)
{
	return true;
}


const DISC_INTERFACE __io_usbstorage = {
	DEVICE_TYPE_WII_USB,
	FEATURE_MEDIUM_CANREAD | FEATURE_MEDIUM_CANWRITE | FEATURE_WII_USB,
	(FN_MEDIUM_STARTUP)&usbstorage_Init,
	(FN_MEDIUM_ISINSERTED)&usbstorage_IsInserted,
	(FN_MEDIUM_READSECTORS)&usbstorage_ReadSectors,
	(FN_MEDIUM_WRITESECTORS)&usbstorage_WriteSectors,
	(FN_MEDIUM_CLEARSTATUS)&usbstorage_ClearStatus,
	(FN_MEDIUM_SHUTDOWN)&usbstorage_Shutdown
};
