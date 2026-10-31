/**
 * \author Mr.Nobody
 * \file Dcmi_Reg.h
 * \ingroup Dcmi
 * \brief Dcmi module Reg component header file.
 *
 * This component contains functionality for register operations. It is private
 * to the module (the register access is not shared with other modules).
 *
 */

#ifndef DCMI_DCMI_REG_H
#define DCMI_DCMI_REG_H

#ifdef __cplusplus
extern "C" {
#endif

/* ============================= INCLUDES =================================== */
#include "Dcmi_Types.h"                      /* Module types definition        */
/* ============================= TYPEDEFS =================================== */

/** \brief List of DCMI registers used by the module (DCMI_DR is read only by the DMA, it is
 *  never touched by software - its address is used directly as the Gpdma source address) */
typedef enum
{
    DCMI_REG_CR     = 0u, /**< DCMI control register                        */
    DCMI_REG_SR         , /**< DCMI status register (read only)             */
    DCMI_REG_RIS        , /**< DCMI raw interrupt status register (read only) */
    DCMI_REG_IER        , /**< DCMI interrupt enable register               */
    DCMI_REG_ICR        , /**< DCMI interrupt clear register (write only)   */
    DCMI_REG_ESCR       , /**< DCMI embedded synchronization code register  */
    DCMI_REG_ESUR       , /**< DCMI embedded synchronization unmask register */
    DCMI_REG_CWSTRT     , /**< DCMI crop window start register              */
    DCMI_REG_CWSIZE     , /**< DCMI crop window size register               */
    DCMI_REG_CNT          /**< Count of available DCMI registers            */
}   dcmi_RegId_t;

/* ========================= SYMBOLIC CONSTANTS ============================= */

/* ========================= EXPORTED MACROS ================================ */

/* ========================= EXPORTED VARIABLES ============================= */

/* ======================== EXPORTED FUNCTIONS ============================== */

/* ---------------------------- Generic accessors --------------------------- */

void                        Dcmi_Set_RegBit             ( dcmi_RegId_t regId, uint32_t bitMask );
void                        Dcmi_Reset_RegBit           ( dcmi_RegId_t regId, uint32_t bitMask );
uint32_t                    Dcmi_Get_RegBit             ( dcmi_RegId_t regId, uint32_t bitMask );

void                        Dcmi_Set_RegVal             ( dcmi_RegId_t regId, uint32_t regMask, uint32_t regValue );
uint32_t                    Dcmi_Get_RegVal             ( dcmi_RegId_t regId, uint32_t regMask );

uint32_t                    Dcmi_Reg_Get_DataRegAddr    ( void );

/* ----------------------------- Register protocol -------------------------- */

dcmi_RequestState_t         Dcmi_Reg_Set_Enable         ( void );
dcmi_RequestState_t         Dcmi_Reg_Set_Disable        ( void );
dcmi_FunctionState_t        Dcmi_Reg_Get_EnableState     ( void );

void                        Dcmi_Reg_Set_CaptureActive  ( void );
void                        Dcmi_Reg_Set_CaptureInactive( void );
dcmi_FunctionState_t        Dcmi_Reg_Get_CaptureState    ( void );

#ifdef __cplusplus
}
#endif

#endif /* DCMI_DCMI_REG_H */
