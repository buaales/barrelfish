#ifndef __ZYNQMP_CNI_DEBUG_H__
#define __ZYNQMP_CNI_DEBUG_H__

/*****************************************************************
 * Debug printer:
 *****************************************************************/
#if defined(ZYNQMP_CNI_DEBUG_ON) || defined(GLOBAL_DEBUG)
#define ZYNQMP_CNI_DEBUG(x...) printf("CNI DEBUG: " x)
#else
#define ZYNQMP_CNI_DEBUG(x...) ((void)0)
#endif

#endif //  __CNI_DEBUG_H__
