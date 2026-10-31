/**
 * \author Mr.Nobody
 * \file Dcmi_Port.h
 * \ingroup Dcmi
 * \brief Dcmi module public interface header file
 *
 * This file contains the public interface of the module, identical for all
 * supported families. Include this file to use the module from an application
 * or from another module. Every function is documented at its definition in
 * Dcmi.c (on STM32H503/H543/H553, which have no DCMI peripheral, every function
 * only returns DCMI_REQUEST_ERROR).
 *
 */

#ifndef DCMI_DCMI_PORT_H
#define DCMI_DCMI_PORT_H

#ifdef __cplusplus
extern "C" {
#endif

/* ============================= INCLUDES =================================== */
#include "Dcmi_Types.h"                      /* Module types definition        */
/* ============================= TYPEDEFS =================================== */
/* ========================= SYMBOLIC CONSTANTS ============================= */
/* ========================= EXPORTED MACROS ================================ */
/* ========================= EXPORTED VARIABLES ============================= */
/* ======================== EXPORTED FUNCTIONS ============================== */

/* --------------------------- Module management ----------------------------- */

dcmi_ModuleVersion_t    Dcmi_Get_ModuleVersion     ( void );

dcmi_RequestState_t     Dcmi_Init                  ( const dcmi_Config_t * const dcmiConfig );
dcmi_RequestState_t     Dcmi_Deinit                ( void );
void                    Dcmi_Task                  ( void );

dcmi_RequestState_t     Dcmi_Get_DefaultConfig     ( dcmi_Config_t * const dcmiConfig );

/* -------------------------------- Capture ----------------------------------- */

dcmi_RequestState_t     Dcmi_Set_CaptureStart      ( dcmi_Pixel_t * const frameBuffer, dcmi_Size_t bufferSize );
dcmi_RequestState_t     Dcmi_Set_CaptureStop       ( void );
dcmi_RequestState_t     Dcmi_Set_CaptureAbort      ( void );
dcmi_RequestState_t     Dcmi_Get_CaptureState      ( dcmi_CaptureState_t * const captureState );
dcmi_RequestState_t     Dcmi_Get_FrameCount        ( dcmi_FrameCnt_t * const frameCount );

/* --------------------------- Window and selection --------------------------- */

dcmi_RequestState_t     Dcmi_Set_CropWindow        ( const dcmi_Window_t * const window );
dcmi_RequestState_t     Dcmi_Set_Selection         ( const dcmi_Selection_t * const selection );
dcmi_RequestState_t     Dcmi_Get_FrameSize         ( dcmi_Size_t * const frameSize );

/* ------------------------------ Synchronization ------------------------------ */

dcmi_RequestState_t     Dcmi_Set_EmbeddedSyncCodes ( const dcmi_SyncCodes_t * const syncCodes );

/* -------------------------------- Interrupts -------------------------------- */

dcmi_RequestState_t     Dcmi_Set_IrqPriority       ( dcmi_IrqPrio_t irqPriority );
dcmi_RequestState_t     Dcmi_Get_IrqPriority       ( dcmi_IrqPrio_t * const irqPriority );

/* ------------------------------------ Pins ----------------------------------- */

dcmi_RequestState_t     Dcmi_InitPixClkGpio        ( dcmi_PixClkPin_t pixClkPin );
dcmi_RequestState_t     Dcmi_InitSyncGpio          ( dcmi_HsyncPin_t hsyncPin, dcmi_VsyncPin_t vsyncPin );
dcmi_RequestState_t     Dcmi_InitDataGpio          ( dcmi_DataPin_t dataPin );

#ifdef __cplusplus
}
#endif

#endif /* DCMI_DCMI_PORT_H */
