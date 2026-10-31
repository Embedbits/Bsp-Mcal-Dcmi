/**
 * \author Mr.Nobody
 * \file Dcmi.h
 * \ingroup Dcmi
 * \brief Dcmi module private declarations
 *
 * Internal state and helpers of the module, not part of the public interface
 * (Dcmi_Port.h). Shared between Dcmi.c and Dcmi_Reg.c.
 *
 */

#ifndef DCMI_DCMI_H
#define DCMI_DCMI_H
/* ============================== INCLUDES ================================== */
#include "Dcmi_Port.h"                       /* Own port file include          */
/* ========================== SYMBOLIC CONSTANTS ============================ */

/** Number of loop iterations of a bounded wait on a register flag before a timeout is declared */
#define DCMI_REG_WAIT_LOOP_CNT                  ( 100000u )

/** Largest block size (bytes) a single Gpdma transfer block can move (gpdma_BlockSize_t range) */
#define DCMI_DMA_BLOCK_SIZE_MAX                  ( 65535u )

/** Largest total frame size (bytes) the module accepts for a single capture: a 4-byte (word)
 *  block repeated up to 65536 times (gpdma_BlockRep_t range + 1), see DcmiPriv_ComputeDmaBlocks */
#define DCMI_DMA_FRAME_SIZE_MAX                   ( 0x40000u )

/** One byte per pixel (8-bit raw/Bayer, monochrome, see DcmiPriv_BytesPerPixel) */
#define DCMI_BYTES_PER_PIXEL_8BIT                 ( 1u )

/** Two bytes per pixel (10/12/14-bit raw/Bayer stored as 16-bit, YCbCr422, RGB565) */
#define DCMI_BYTES_PER_PIXEL_16BIT                 ( 2u )

/* ============================== TYPEDEFS ================================== */

/** Repetition count of the Gpdma block that moves a frame larger than one block (see \ref DcmiPriv_ComputeDmaBlocks) */
typedef uint32_t dcmi_BlockCnt_t;

/** Run-time state of the DCMI instance */
typedef struct
{
    dcmi_FunctionState_t Initialized;  /**< Instance configured by Dcmi_Init              */
    dcmi_Config_t        Config;       /**< Copy of the configuration passed to Dcmi_Init */
    dcmi_CaptureState_t  CaptureState; /**< Current capture state                         */
    dcmi_FrameCnt_t      FrameCount;   /**< Frames captured since Dcmi_Init               */
    dcmi_Window_t        Window;       /**< Crop window currently configured              */
    dcmi_FunctionState_t CropActive;   /**< Crop feature currently enabled                */
}   dcmi_InstanceData_t;

/* ========================= EXPORTED VARIABLES ============================= */
/* ======================== EXPORTED FUNCTIONS ============================== */

dcmi_RequestState_t    DcmiPriv_Get_Instance          ( dcmi_InstanceData_t ** const instance );
dcmi_RequestState_t    DcmiPriv_ComputeDmaBlocks      ( dcmi_Size_t totalSize,
                                                        dcmi_Size_t * const blockSize,
                                                        dcmi_BlockCnt_t * const blockRepeatCnt );

#endif /* DCMI_DCMI_H */
