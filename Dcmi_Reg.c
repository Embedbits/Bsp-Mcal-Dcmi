/**
 * \author Mr.Nobody
 * \file Dcmi_Reg.c
 * \ingroup Dcmi
 * \brief Dcmi module Reg component functionality.
 *
 * Register access of the module: generic accessors (bounds checked register ID) and the
 * register protocol of the digital camera interface. The registers are written only here,
 * the masks come from the CMSIS device header.
 *
 * Properties of the registers, which the protocol respects (RM0481 / RM0517, DCMI chapter):
 * - DCMI_CR.ENABLE must be set before DCMI_CR.CAPTURE (note of DCMI_CR bit 0).
 * - DCMI_SR and DCMI_RIS are read only, DCMI_ICR is write only (setting a bit clears the
 *   matching DCMI_RIS bit, writing 0 has no effect).
 * - DCMI_CR.CAPTURE is cleared by the hardware itself at the end of a snapshot frame, so the
 *   module reads it back instead of assuming the bit stays as written.
 *
 */
/* ============================== INCLUDES ================================== */
#include "Dcmi_Reg.h"                       /* Self include                   */
#include "Dcmi_Types.h"                     /* Module types definitions       */
#include "Stm32.h"                          /* DCMI registers definition      */
/* ============================== TYPEDEFS ================================== */

#if defined(DCMI)

/** \brief Structure type used to store physical address of DCMI registers.
 * This array is used to reduce size of configuration array. With this style
 * the registers are referenced through enumeration.
 *
 */
typedef struct
{
    dcmi_RegId_t        RegId;   /**< Peripheral register ID      */
    volatile uint32_t * RegAddr; /**< Peripheral register address */
}   dcmi_RegList_t;

/* ======================== FORWARD DECLARATIONS ============================ */

static dcmi_FunctionState_t Dcmi_Reg_Get_BitState   ( dcmi_RegId_t regId, uint32_t bitMask );

/* ========================== SYMBOLIC CONSTANTS ============================ */

/** \brief Count of read-back iterations of a written register value */
#define DCMI_REG_READBACK_TIMEOUT_RAW   ( 0x1000u )

/** \brief Register value without any bit set (masked register value of an inactive bit field) */
#define DCMI_REG_BITS_CLEAR             ( 0u )

/* =============================== MACROS =================================== */

/* ========================== EXPORTED VARIABLES ============================ */

/* =========================== LOCAL VARIABLES ============================== */

/** \brief DCMI registers configuration structure
 *
 * This structure is used to store addresses of DCMI registers.
 * It is used to reduce code size while referring to DCMI registers.
 */
static const dcmi_RegList_t             dcmi_RegList[] =
{
    { .RegId = DCMI_REG_CR     , .RegAddr = &(DCMI->CR     ) }, /**< DCMI control register               */
    { .RegId = DCMI_REG_SR     , .RegAddr = &(DCMI->SR     ) }, /**< DCMI status register                */
    { .RegId = DCMI_REG_RIS    , .RegAddr = &(DCMI->RISR   ) }, /**< DCMI raw interrupt status register  */
    { .RegId = DCMI_REG_IER    , .RegAddr = &(DCMI->IER    ) }, /**< DCMI interrupt enable register      */
    { .RegId = DCMI_REG_ICR    , .RegAddr = &(DCMI->ICR    ) }, /**< DCMI interrupt clear register       */
    { .RegId = DCMI_REG_ESCR   , .RegAddr = &(DCMI->ESCR   ) }, /**< DCMI embedded sync code register    */
    { .RegId = DCMI_REG_ESUR   , .RegAddr = &(DCMI->ESUR   ) }, /**< DCMI embedded sync unmask register  */
    { .RegId = DCMI_REG_CWSTRT , .RegAddr = &(DCMI->CWSTRTR) }, /**< DCMI crop window start register     */
    { .RegId = DCMI_REG_CWSIZE , .RegAddr = &(DCMI->CWSIZER) }, /**< DCMI crop window size register      */
};

_Static_assert( (sizeof(dcmi_RegList) / sizeof(dcmi_RegList_t)) == DCMI_REG_CNT,
                "Dcmi_Reg: dcmi_RegList has incorrect size." );

/* ========================= EXPORTED FUNCTIONS ============================= */

/**
 * \brief Sets the register bit with given mask.
 *
 * \param regId   [in]: DCMI register ID
 * \param bitMask [in]: Mask to apply to the register
 */
void Dcmi_Set_RegBit( dcmi_RegId_t regId, uint32_t bitMask )
{
    if( DCMI_REG_CNT > regId )
    {
        *dcmi_RegList[regId].RegAddr |= bitMask;
    }
    else
    {
        /* Invalid register ID, no action required */
    }
}


/**
 * \brief Resets the register bit with given mask.
 *
 * \param regId   [in]: DCMI register ID
 * \param bitMask [in]: Mask to apply to the register
 */
void Dcmi_Reset_RegBit( dcmi_RegId_t regId, uint32_t bitMask )
{
    if( DCMI_REG_CNT > regId )
    {
        *dcmi_RegList[regId].RegAddr &= ~bitMask;
    }
    else
    {
        /* Invalid register ID, no action required */
    }
}


/**
 * \brief Gets the register bit value with given mask.
 *
 * \param regId   [in]: DCMI register ID
 * \param bitMask [in]: Mask to apply to the register
 * \return Register bit value with applied mask, no bit set for invalid register ID
 */
uint32_t Dcmi_Get_RegBit( dcmi_RegId_t regId, uint32_t bitMask )
{
    uint32_t retValue = DCMI_REG_BITS_CLEAR;

    if( DCMI_REG_CNT > regId )
    {
        retValue = ( *dcmi_RegList[regId].RegAddr & bitMask );
    }
    else
    {
        /* Invalid register ID, no bit is set */
        retValue = DCMI_REG_BITS_CLEAR;
    }

    return ( retValue );
}


/**
 * \brief Updates the register value with given mask and value.
 *
 * \param regId    [in]: DCMI register ID
 * \param regMask  [in]: Mask to apply to the register
 * \param regValue [in]: Value to set in the register
 */
void Dcmi_Set_RegVal( dcmi_RegId_t regId, uint32_t regMask, uint32_t regValue )
{
    if( DCMI_REG_CNT > regId )
    {
        /* Set the register value with mask */
        *dcmi_RegList[regId].RegAddr = ( ( *dcmi_RegList[regId].RegAddr & ~regMask ) |
                                         ( regValue & regMask ) );
    }
    else
    {
        /* Invalid register ID, no action required */
    }
}


/**
 * \brief Gets the register value with given mask.
 *
 * \param regId   [in]: DCMI register ID.
 * \param regMask [in]: Mask to apply to the register
 * \return Register value with applied mask, no bit set for invalid register ID
 */
uint32_t Dcmi_Get_RegVal( dcmi_RegId_t regId, uint32_t regMask )
{
    uint32_t retValue = DCMI_REG_BITS_CLEAR;

    if( DCMI_REG_CNT > regId )
    {
        /* Get the register value with mask */
        retValue = ( *dcmi_RegList[regId].RegAddr & regMask );
    }
    else
    {
        /* Invalid register ID, no bit is set */
        retValue = DCMI_REG_BITS_CLEAR;
    }

    return ( retValue );
}


/**
 * \brief Returns the address of the DCMI data register (DCMI_DR), used as the Gpdma source
 *        address of the frame transfer. The register itself is never read by software.
 *
 * \return Address of DCMI_DR.
 */
uint32_t Dcmi_Reg_Get_DataRegAddr( void )
{
    return ( (uint32_t)&(DCMI->DR) );
}


/**
 * \brief Sets DCMI_CR.ENABLE and verifies it by read-back.
 *
 * \return Returns \ref DCMI_REQUEST_OK if the bit read back within the timeout. Otherwise
 *         returns \ref DCMI_REQUEST_ERROR.
 */
dcmi_RequestState_t Dcmi_Reg_Set_Enable( void )
{
    dcmi_RequestState_t retState = DCMI_REQUEST_ERROR;

    Dcmi_Set_RegBit( DCMI_REG_CR, DCMI_CR_ENABLE );

    for( uint32_t iterationCnt = 0u; DCMI_REG_READBACK_TIMEOUT_RAW > iterationCnt; iterationCnt++ )
    {
        const dcmi_FunctionState_t enableState = Dcmi_Reg_Get_EnableState();

        if( DCMI_FUNCTION_ACTIVE == enableState )
        {
            retState = DCMI_REQUEST_OK;
            break;
        }
        else
        {
            /* Not confirmed yet */
            retState = DCMI_REQUEST_ERROR;
        }
    }

    return ( retState );
}


/**
 * \brief Resets DCMI_CR.ENABLE and verifies it by read-back.
 *
 * \return Returns \ref DCMI_REQUEST_OK if the bit read back within the timeout. Otherwise
 *         returns \ref DCMI_REQUEST_ERROR.
 */
dcmi_RequestState_t Dcmi_Reg_Set_Disable( void )
{
    dcmi_RequestState_t retState = DCMI_REQUEST_ERROR;

    Dcmi_Reset_RegBit( DCMI_REG_CR, DCMI_CR_ENABLE );

    for( uint32_t iterationCnt = 0u; DCMI_REG_READBACK_TIMEOUT_RAW > iterationCnt; iterationCnt++ )
    {
        const dcmi_FunctionState_t enableState = Dcmi_Reg_Get_EnableState();

        if( DCMI_FUNCTION_INACTIVE == enableState )
        {
            retState = DCMI_REQUEST_OK;
            break;
        }
        else
        {
            /* Not confirmed yet */
            retState = DCMI_REQUEST_ERROR;
        }
    }

    return ( retState );
}


/**
 * \brief Returns the DCMI_CR.ENABLE bit.
 *
 * \return \ref DCMI_FUNCTION_ACTIVE if the interface is enabled, otherwise
 *         \ref DCMI_FUNCTION_INACTIVE.
 */
dcmi_FunctionState_t Dcmi_Reg_Get_EnableState( void )
{
    return ( Dcmi_Reg_Get_BitState( DCMI_REG_CR, DCMI_CR_ENABLE ) );
}


/**
 * \brief Sets DCMI_CR.CAPTURE (arms the capture, DCMI_CR.ENABLE must already be set).
 */
void Dcmi_Reg_Set_CaptureActive( void )
{
    Dcmi_Set_RegBit( DCMI_REG_CR, DCMI_CR_CAPTURE );
}


/**
 * \brief Resets DCMI_CR.CAPTURE (requests the stop of a continuous capture).
 */
void Dcmi_Reg_Set_CaptureInactive( void )
{
    Dcmi_Reset_RegBit( DCMI_REG_CR, DCMI_CR_CAPTURE );
}


/**
 * \brief Returns the DCMI_CR.CAPTURE bit (read back, since the hardware clears it by itself
 *        at the end of a snapshot frame).
 *
 * \return \ref DCMI_FUNCTION_ACTIVE if the capture is armed / running, otherwise
 *         \ref DCMI_FUNCTION_INACTIVE.
 */
dcmi_FunctionState_t Dcmi_Reg_Get_CaptureState( void )
{
    return ( Dcmi_Reg_Get_BitState( DCMI_REG_CR, DCMI_CR_CAPTURE ) );
}

/* =========================== LOCAL FUNCTIONS ============================== */

/**
 * \brief Returns the state of a register bit field as module enumeration.
 *
 * \param regId   [in]: DCMI register ID
 * \param bitMask [in]: Mask of the checked bits
 *
 * \return \ref DCMI_FUNCTION_ACTIVE if any bit of the mask is set, otherwise
 *         \ref DCMI_FUNCTION_INACTIVE (also for an invalid register ID).
 */
static dcmi_FunctionState_t Dcmi_Reg_Get_BitState( dcmi_RegId_t regId, uint32_t bitMask )
{
    dcmi_FunctionState_t retState = DCMI_FUNCTION_INACTIVE;
    const uint32_t        bitValue = Dcmi_Get_RegBit( regId, bitMask );

    if( DCMI_REG_BITS_CLEAR != bitValue )
    {
        retState = DCMI_FUNCTION_ACTIVE;
    }
    else
    {
        retState = DCMI_FUNCTION_INACTIVE;
    }

    return ( retState );
}

/* =========================== INTERRUPT HANDLERS =========================== */

/* ================================ TASKS =================================== */

#endif /* DCMI */
