#ifndef __CNI_DRIVER_DEBUG_H__
#define __CNI_DRIVER_DEBUG_H__

/*****************************************************************
 * Debug printer:
 *****************************************************************/
#if defined(CNI_DRIVER_DEBUG_ON) || defined(GLOBAL_DEBUG)
#define CNI_DRIVER_DEBUG(x...) printf("CNI DRIVER: " x)
#else
#define CNI_DRIVER_DEBUG(x...) ((void)0)
#endif

#endif //  __CNI_DEBUG_H__
