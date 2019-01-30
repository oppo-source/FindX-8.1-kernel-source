/************************************************************************************
** File:  \\192.168.144.3\Linux_Share\12015\ics2\development\mediatek\custom\oppo77_12015\kernel\battery\battery
** VENDOR_EDIT
** Copyright (C), 2008-2012, OPPO Mobile Comm Corp., Ltd
**
** Description:
**          for dc-dc sn111008 charg
**
** Version: 1.0
** Date created: 21:03:46, 05/04/2012
** Author: Fanhong.Kong@ProDrv.CHG
**
** --------------------------- Revision History: ------------------------------------------------------------
* <version>           <date>                <author>                             <desc>
* Revision 1.0       2018-04-14        Fanhong.Kong@ProDrv.CHG       Upgrade for SVOOC
************************************************************************************************************/

#define OPPO_SHA1_HMAC
//#define SHA1_DBG

#define UINT8  unsigned char
#define UINT16 unsigned short
#define UINT32 unsigned int

#define RANDMESGNUMBYTES  20
#define DIGESTNUMBYTES    20
#define DEVICEIDNUMBYTES   8
#define SECRETKEYNUMBYTES 16

//*****************************************************************************
// void SHA1_authenticate(void)
//
// Description : Computes the SHA1/HMAC as required by the bq26100
// Arguments : i - times that SHA1 is executing
//             t - index 0 through 79
//             temp - Used to update working variables
// Global Variables : Random[], Message[], Key[], Ws[], H[], A, B, C1, D, E
// Returns : Result of 32-bit word rotated n times
//*****************************************************************************
void SHA1_authenticate(void);
