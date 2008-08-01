/*
  MAPDEV.h - include file for VxD MAPDEV
  Copyright (c) 1996 Vireo Software, Inc.
  Modified for libdha by Nick Kurshev.
*/

#include <windows.h>

/*
  This is the request structure that applications use
  to request services from the MAPDEV VxD.
*/

typedef struct _MapDevRequest
{
	DWORD	mdr_ServiceID;		/* supplied by caller */
	LPVOID	mdr_PhysicalAddress;	/* supplied by caller */
	DWORD	mdr_SizeInBytes;	/* supplied by caller */
	LPVOID	mdr_LinearAddress;	/* returned by VxD */
	WORD	mdr_Selector;		/* returned if 16-bit caller */
	WORD	mdr_Status;		/* MDR_xxxx code below */
} MAPDEVREQUEST, *PMAPDEVREQUEST;

#define MDR_SERVICE_MAP		CTL_CODE(FILE_DEVICE_UNKNOWN, 1, METHOD_NEITHER, FILE_ANY_ACCESS)
#define MDR_SERVICE_UNMAP	CTL_CODE(FILE_DEVICE_UNKNOWN, 2, METHOD_NEITHER, FILE_ANY_ACCESS)

#define MDR_STATUS_SUCCESS	1
#define MDR_STATUS_ERROR	0
/*#include "winioctl.h"*/
#define FILE_DEVICE_UNKNOWN             0x00000022
#define METHOD_NEITHER                  3
#define FILE_ANY_ACCESS                 0
#define CTL_CODE( DeviceType, Function, Method, Access ) ( \
    ((DeviceType)<<16) | ((Access)<<14) | ((Function)<<2) | (Method) )

/* Memory Map a piece of Real Memory */
void *map_phys_mem(unsigned base, unsigned size) {

  HANDLE hDevice ;
  PVOID inBuf[1] ;		/* buffer for struct pointer to VxD */
  DWORD RetInfo[2] ;		/* buffer to receive data from VxD */
  DWORD cbBytesReturned ;	/* count of bytes returned from VxD */
  MAPDEVREQUEST req ;		/* map device request structure */
  DWORD *pNicstar, Status, Time ; int i ; char *endptr ;
  const PCHAR VxDName = "\\\\.\\MAPDEV.VXD" ;
  const PCHAR VxDNameAlreadyLoaded = "\\\\.\\MAPDEV" ;

  hDevice = CreateFile(VxDName, 0,0,0,
                       CREATE_NEW, FILE_FLAG_DELETE_ON_CLOSE, 0) ;
  if (hDevice == INVALID_HANDLE_VALUE)
    hDevice = CreateFile(VxDNameAlreadyLoaded, 0,0,0,
                         CREATE_NEW, FILE_FLAG_DELETE_ON_CLOSE, 0) ;
  if (hDevice == INVALID_HANDLE_VALUE) {
    fprintf(stderr, "Cannot open driver, error=%08lx\n", GetLastError()) ;
    exit(1) ; }

  req.mdr_ServiceID = MDR_SERVICE_MAP ;
  req.mdr_PhysicalAddress = (PVOID)base ;
  req.mdr_SizeInBytes = size ;
  inBuf[0] = &req ;

  if ( ! DeviceIoControl(hDevice, MDR_SERVICE_MAP, inBuf, sizeof(PVOID),
         NULL, 0, &cbBytesReturned, NULL) ) {
    fprintf(stderr, "Failed to map device\n") ; exit(1) ; }

  return (void*)req.mdr_LinearAddress ;
}

void unmap_phys_mem(void *ptr, unsigned size) { }

