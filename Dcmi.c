/**
 * \author Mr.Nobody
 * \file Dcmi.c
 * \ingroup Dcmi
 * \brief Dcmi module functionality
 *
 * Implementation for the STM32H5 lines. The DCMI peripheral exists on H523/H533/H562/H563/H573/
 * H5E4/H5E5/H5F4/H5F5 (CMSIS definition \c DCMI); every public function checks it
 * (\c defined(DCMI)): on H503/H543/H553 it only returns \ref DCMI_REQUEST_ERROR (\ref Dcmi_Task
 * does nothing, \ref Dcmi_Get_ModuleVersion returns the version). The module owns the DCMI_CR /
 * DCMI_SR / DCMI_RIS / DCMI_IER / DCMI_ICR / DCMI_ESCR / DCMI_ESUR / DCMI_CWSTRTR / DCMI_CWSIZER
 * registers (Dcmi_Reg.c) and drives the frame capture through one Gpdma channel
 * (GPDMA_DIR_PERIPH_TO_MEMORY, source DCMI_DR, request GPDMA_REQ_DCMI) configured by the
 * application (DmaPeriphId / DmaChannelId / DmaPriority of dcmi_Config_t).
 *
 * Frame-ready signalling note (deviation, reported): DCMI_RIS.FRAME_RIS is set by the hardware
 * on the line/window boundary, which can occur before the last DMA burst of the frame has
 * actually left the DCMI FIFO for memory (same limitation ST's own HAL works around by not
 * relying on it alone). The module therefore invalidates the frame buffer's data cache range
 * and calls FrameCallback from the Gpdma transfer-complete interrupt (fires once per whole
 * capture thanks to GPDMA_TRANSFER_EVENT_2D_BLOCK, after every repeated block), not from
 * DCMI_RIS.FRAME_RIS - that flag is used only to clear it (and to leave DCMI_STATE_IDLE in
 * snapshot mode, see Dcmi_Isr). This could not be verified on hardware (no board access).
 *
 * Large frame note (deviation, reported): a frame buffer over DCMI_DMA_BLOCK_SIZE_MAX
 * (65535 bytes, the gpdma_BlockSize_t range) is moved with one repeated 4-byte (word) block
 * (DcmiPriv_ComputeDmaBlocks) instead of linked transfer descriptors - this needs a
 * 2D-capable Gpdma channel (see the module README) and was not verified on hardware either.
 *
 */
/* ============================== INCLUDES ================================== */
#include "Dcmi.h"                           /* Own header include             */
#include "Dcmi_Reg.h"                       /* Register access                */
#include "Rcc_Port.h"                       /* RCC Port module definitions    */
#include "Gpio_Port.h"                      /* GPIO Port module definitions   */
#include "Gpdma_Port.h"                     /* GPDMA Port module definitions  */
#include "Cache_Port.h"                     /* Cache Port module definitions  */
#include "Stm32.h"                          /* DCMI registers definition      */
/* ============================== TYPEDEFS ================================== */

/* ======================== FORWARD DECLARATIONS ============================ */

#if defined(DCMI)

static dcmi_RequestState_t DcmiPriv_InitAltFuncGpio  ( gpio_PortId_t portId,
                                                       gpio_PinId_t pinId,
                                                       gpio_AltFunction_t altFunc );
static dcmi_RequestState_t DcmiPriv_Dma_Init         ( void );
static dcmi_RequestState_t DcmiPriv_Dma_Deinit       ( void );
static dcmi_RequestState_t DcmiPriv_Dma_Start        ( dcmi_Pixel_t * const frameBuffer, dcmi_Size_t bufferSize );
static dcmi_RequestState_t DcmiPriv_Dma_Stop         ( void );
static dcmi_RequestState_t DcmiPriv_BytesPerPixel    ( dcmi_Format_t format,
                                                       dcmi_DataWidth_t dataWidth,
                                                       uint32_t * const bytesPerPixel );
static void                Dcmi_Dma_TransferComplete ( void );
static void                Dcmi_Dma_Error            ( gpdma_ErrorMaskId_t errorMask );
static void                Dcmi_Isr                  ( void );

#endif /* DCMI */

/* ========================== SYMBOLIC CONSTANTS ============================ */

/** Module version major number */
#define DCMI_VERSION_MAJOR                      ( 1u )

/** Module version minor number */
#define DCMI_VERSION_MINOR                       ( 0u )

/** Module version patch number */
#define DCMI_VERSION_PATCH                        ( 0u )

/** DCMI interrupts used by the module (OVR / ERR / VSYNC / LINE / FRAME) */
#define DCMI_IT_ALL                               ( DCMI_IER_FRAME_IE | DCMI_IER_OVR_IE | \
                                                     DCMI_IER_ERR_IE   | DCMI_IER_VSYNC_IE | \
                                                     DCMI_IER_LINE_IE    )

/** Gpdma errors reported by the module as \ref DCMI_ERROR_DMA */
#define DCMI_DMA_ERROR_MASK                        ( GPDMA_ERROR_TRANSFER      | \
                                                      GPDMA_ERROR_CONFIG_UPDATE | \
                                                      GPDMA_ERROR_CONFIG_ERROR  | \
                                                      GPDMA_ERROR_TRIG_OVERRUN    )

/* =============================== MACROS =================================== */

/* ========================== EXPORTED VARIABLES ============================ */

/* =========================== LOCAL VARIABLES ============================== */

#if defined(DCMI)

/** \brief Single DCMI instance run-time state (the device has only one instance) */
static dcmi_InstanceData_t    dcmi_Instance;

/** \brief Gpdma transfer list of the frame capture channel (must be static, used by GPDMA HW) */
static gpdma_XferList_t       dcmi_DmaXferList;

/** \brief Gpdma channel ownership (Gpdma does not support de-initialization of a single channel) */
static dcmi_FunctionState_t   dcmi_DmaChannelInit = DCMI_FUNCTION_INACTIVE;

#endif /* DCMI */

/* ========================= EXPORTED FUNCTIONS ============================= */

/**
 * \brief Returns the module version.
 *
 * \return Module version.
 */
dcmi_ModuleVersion_t Dcmi_Get_ModuleVersion( void )
{
    dcmi_ModuleVersion_t version =
    {
        .Major = DCMI_VERSION_MAJOR,
        .Minor = DCMI_VERSION_MINOR,
        .Patch = DCMI_VERSION_PATCH,
    };

    return ( version );
}


/**
 * \brief Fills a configuration structure with safe default values (8-bit bus, hardware
 *        synchronization, snapshot capture, RAW format, linear buffer, no pins, no callbacks).
 *
 * \param dcmiConfig [out] : Pointer to the configuration structure.
 *
 * \return DCMI_REQUEST_OK on success, DCMI_REQUEST_ERROR for an invalid parameter.
 */
dcmi_RequestState_t Dcmi_Get_DefaultConfig( dcmi_Config_t * const dcmiConfig )
{
    dcmi_RequestState_t retState = DCMI_REQUEST_ERROR;

#if defined(DCMI)
    if( DCMI_NULL_PTR != dcmiConfig )
    {
        dcmiConfig->DataWidth      = DCMI_DATA_WIDTH_8;
        dcmiConfig->SyncMode       = DCMI_SYNC_HARDWARE;
        dcmiConfig->PixClkPolarity = DCMI_PIXCLK_FALLING;
        dcmiConfig->HsyncPolarity  = DCMI_SYNC_ACTIVE_LOW;
        dcmiConfig->VsyncPolarity  = DCMI_SYNC_ACTIVE_LOW;
        dcmiConfig->Format         = DCMI_FORMAT_RAW;
        dcmiConfig->CaptureMode    = DCMI_CAPTURE_SNAPSHOT;
        dcmiConfig->BufferMode     = DCMI_BUFFER_LINEAR;
        dcmiConfig->IrqPriority    = 0u;
        dcmiConfig->DmaPeriphId    = GPDMA_PERIPH_1;
        dcmiConfig->DmaChannelId   = GPDMA_CHANNEL_0;
        dcmiConfig->DmaPriority    = GPDMA_PRIORITY_LOW;
        dcmiConfig->PixClkPin      = DCMI_PIXCLK_PIN_UNUSED;
        dcmiConfig->HsyncPin       = DCMI_HSYNC_PIN_UNUSED;
        dcmiConfig->VsyncPin       = DCMI_VSYNC_PIN_UNUSED;
        for( uint32_t idx = 0u; ( DCMI_DATA_PIN_CNT > idx ); idx++ )
        {
            dcmiConfig->DataPin[idx] = DCMI_DATA_PIN_UNUSED;
        }
        dcmiConfig->FrameCallback = DCMI_NULL_PTR;
        dcmiConfig->LineCallback  = DCMI_NULL_PTR;
        dcmiConfig->VsyncCallback = DCMI_NULL_PTR;
        dcmiConfig->ErrorCallback = DCMI_NULL_PTR;

        retState = DCMI_REQUEST_OK;
    }
    else
    {
        /* Invalid parameter, no action required */
        retState = DCMI_REQUEST_ERROR;
    }
#else
    (void)dcmiConfig;
#endif /* DCMI */

    return ( retState );
}


/**
 * \brief Initializes the DCMI instance: the peripheral clock, the pins, the interrupt priority
 *        and the control register. The capture is left stopped (\ref DCMI_STATE_IDLE); use
 *        \ref Dcmi_Set_CaptureStart to arm it.
 *
 * \note  Enabling the DCMI clock / pins means the Pssi module can not be used concurrently on
 *        the same device (shared RCC_PERIPH_DCMI_PSSI, pins and Gpdma request, see the README).
 *
 * \param dcmiConfig [in] : Pointer to the configuration structure.
 *
 * \return DCMI_REQUEST_OK on success, DCMI_REQUEST_ERROR otherwise (the instance is deinitialized).
 */
dcmi_RequestState_t Dcmi_Init( const dcmi_Config_t * const dcmiConfig )
{
    dcmi_RequestState_t retState = DCMI_REQUEST_ERROR;

#if defined(DCMI)
    uint32_t crValue = 0u;

    if( ( DCMI_NULL_PTR         != dcmiConfig                ) &&
        ( DCMI_DATA_WIDTH_CNT    > dcmiConfig->DataWidth      ) &&
        ( DCMI_SYNC_CNT          > dcmiConfig->SyncMode       ) &&
        ( DCMI_FORMAT_CNT        > dcmiConfig->Format         ) &&
        ( DCMI_CAPTURE_MODE_CNT  > dcmiConfig->CaptureMode    ) &&
        ( DCMI_BUFFER_MODE_CNT   > dcmiConfig->BufferMode     ) &&
        ( DCMI_PIXCLK_PIN_UNUSED != dcmiConfig->PixClkPin     )    )
    {
        dcmi_Instance.Initialized  = DCMI_FUNCTION_ACTIVE;
        dcmi_Instance.Config        = *dcmiConfig;
        dcmi_Instance.CaptureState  = DCMI_STATE_IDLE;
        dcmi_Instance.FrameCount    = 0u;
        dcmi_Instance.Window.FirstLine  = 0u;
        dcmi_Instance.Window.FirstPixel = 0u;
        dcmi_Instance.Window.LineCnt    = 0u;
        dcmi_Instance.Window.PixelCnt   = 0u;
        dcmi_Instance.CropActive        = DCMI_FUNCTION_INACTIVE;

        (void)Rcc_Set_PeriphActive( RCC_PERIPH_DCMI_PSSI );

        (void)Nvic_Set_PeriphIrq_Handler( NVIC_PERIPH_IRQ_DCMI_PSSI, Dcmi_Isr );
        (void)Nvic_Set_PeriphIrq_Prio( NVIC_PERIPH_IRQ_DCMI_PSSI, dcmiConfig->IrqPriority );

        (void)Dcmi_InitPixClkGpio( dcmiConfig->PixClkPin );
        if( DCMI_SYNC_HARDWARE == dcmiConfig->SyncMode )
        {
            (void)Dcmi_InitSyncGpio( dcmiConfig->HsyncPin, dcmiConfig->VsyncPin );
        }
        else
        {
            /* Embedded synchronization, no HSYNC/VSYNC pin is used */
        }
        for( uint32_t idx = 0u; ( DCMI_DATA_PIN_CNT > idx ); idx++ )
        {
            if( DCMI_DATA_PIN_UNUSED != dcmiConfig->DataPin[idx] )
            {
                (void)Dcmi_InitDataGpio( dcmiConfig->DataPin[idx] );
            }
            else
            {
                /* Data pin not needed at this bus width, no action required */
            }
        }

        Dcmi_Set_RegVal( DCMI_REG_CR, DCMI_CR_ENABLE, 0u );
        Dcmi_Set_RegVal( DCMI_REG_CR, DCMI_CR_CAPTURE, 0u );

        /* dcmi_PixClkPolarity_t / dcmi_SyncPolarity_t / dcmi_SyncMode_t / dcmi_CaptureMode_t
         * values are already 0/1 in the same meaning as the matching DCMI_CR bit, so they are
         * cast directly (no ternary operator, see the rules); the JPEG bit needs a comparison
         * (dcmi_Format_t has more than two items), it is resolved by DcmiJpegBit below. */
        uint32_t dcmiJpegBit = 0u;

        if( DCMI_FORMAT_JPEG == dcmiConfig->Format )
        {
            dcmiJpegBit = 1u;
        }
        else
        {
            dcmiJpegBit = 0u;
        }

        crValue = ( ( (uint32_t)dcmiConfig->DataWidth      << DCMI_CR_EDM_Pos    ) |
                    ( (uint32_t)dcmiConfig->PixClkPolarity << DCMI_CR_PCKPOL_Pos ) |
                    ( (uint32_t)dcmiConfig->HsyncPolarity  << DCMI_CR_HSPOL_Pos  ) |
                    ( (uint32_t)dcmiConfig->VsyncPolarity  << DCMI_CR_VSPOL_Pos  ) |
                    ( (uint32_t)dcmiConfig->SyncMode        << DCMI_CR_ESS_Pos    ) |
                    ( dcmiJpegBit                            << DCMI_CR_JPEG_Pos   ) |
                    ( (uint32_t)dcmiConfig->CaptureMode     << DCMI_CR_CM_Pos     )   );

        Dcmi_Set_RegVal( DCMI_REG_CR,
                         ( DCMI_CR_EDM | DCMI_CR_PCKPOL | DCMI_CR_HSPOL | DCMI_CR_VSPOL |
                           DCMI_CR_ESS | DCMI_CR_JPEG   | DCMI_CR_CM                      ),
                         crValue );

        Dcmi_Set_RegVal( DCMI_REG_IER, DCMI_IT_ALL, DCMI_IT_ALL );
        (void)Nvic_Set_PeriphIrq_Active( NVIC_PERIPH_IRQ_DCMI_PSSI );

        retState = Dcmi_Reg_Set_Enable();

        if( DCMI_REQUEST_OK != retState )
        {
            (void)Dcmi_Deinit();
        }
        else
        {
            /* Interface is enabled and ready, no action required */
        }
    }
    else
    {
        /* Invalid parameter, no action required */
        retState = DCMI_REQUEST_ERROR;
    }
#else
    (void)dcmiConfig;
#endif /* DCMI */

    return ( retState );
}


/**
 * \brief Deinitializes the DCMI instance: stops the capture, disables the interface and the
 *        interrupt, releases the Gpdma channel ownership and disables the peripheral clock.
 *
 * \return DCMI_REQUEST_OK on success, DCMI_REQUEST_ERROR for an uninitialized instance.
 */
dcmi_RequestState_t Dcmi_Deinit( void )
{
    dcmi_RequestState_t retState = DCMI_REQUEST_ERROR;

#if defined(DCMI)
    if( DCMI_FUNCTION_ACTIVE == dcmi_Instance.Initialized )
    {
        (void)DcmiPriv_Dma_Stop();
        (void)DcmiPriv_Dma_Deinit();

        Dcmi_Reset_RegBit( DCMI_REG_CR, DCMI_CR_CAPTURE );
        (void)Dcmi_Reg_Set_Disable();
        Dcmi_Set_RegVal( DCMI_REG_IER, DCMI_IT_ALL, 0u );
        Dcmi_Set_RegVal( DCMI_REG_ICR, DCMI_IT_ALL, DCMI_IT_ALL );

        (void)Nvic_Set_PeriphIrq_Inactive( NVIC_PERIPH_IRQ_DCMI_PSSI );
        (void)Rcc_Set_PeriphInactive( RCC_PERIPH_DCMI_PSSI );

        dcmi_Instance.Initialized = DCMI_FUNCTION_INACTIVE;
        dcmi_Instance.CaptureState = DCMI_STATE_IDLE;

        retState = DCMI_REQUEST_OK;
    }
    else
    {
        /* Instance not initialized, no action required */
        retState = DCMI_REQUEST_ERROR;
    }
#else
    /* No peripheral on the device, no action required */
#endif /* DCMI */

    return ( retState );
}


/**
 * \brief The module has no polled work (the frame capture is fully interrupt / DMA driven),
 *        kept for interface symmetry with the other Mcal modules.
 */
void Dcmi_Task( void )
{
    return;
}


/**
 * \brief Arms the DMA and the interface for a frame capture. The configuration (bus, sync,
 *        format, crop) must not change while a capture is armed/running.
 *
 * \param frameBuffer [out] : Destination buffer, must be in DMA reachable memory.
 * \param bufferSize  [in]  : Size of \p frameBuffer in bytes, must be a multiple of 4
 *                             (DCMI_WORD_SIZE_BYTE) and not smaller than the known frame size
 *                             (see \ref Dcmi_Get_FrameSize) when the crop window is active.
 *
 * \return DCMI_REQUEST_OK on success, DCMI_REQUEST_ERROR for an invalid parameter, an
 *         uninitialized instance, a capture already armed/running or a buffer too small.
 */
dcmi_RequestState_t Dcmi_Set_CaptureStart( dcmi_Pixel_t * const frameBuffer, dcmi_Size_t bufferSize )
{
    dcmi_RequestState_t retState = DCMI_REQUEST_ERROR;

#if defined(DCMI)
    dcmi_Size_t               frameSize            = 0u;
    const dcmi_RequestState_t sizeState            = Dcmi_Get_FrameSize( &frameSize );
    dcmi_FlagState_t          sizeKnownAndTooSmall = DCMI_FLAG_INACTIVE;

    if( ( DCMI_REQUEST_OK == sizeState  ) &&
        ( frameSize        > bufferSize )    )
    {
        sizeKnownAndTooSmall = DCMI_FLAG_ACTIVE;
    }
    else
    {
        sizeKnownAndTooSmall = DCMI_FLAG_INACTIVE;
    }

    if( ( DCMI_FUNCTION_ACTIVE == dcmi_Instance.Initialized                      ) &&
        ( DCMI_STATE_IDLE      == dcmi_Instance.CaptureState                     ) &&
        ( DCMI_NULL_PTR        != frameBuffer                                    ) &&
        ( 0u                    < bufferSize                                     ) &&
        ( 0u                   == ( bufferSize % DCMI_WORD_SIZE_BYTE )           ) &&
        ( DCMI_FLAG_ACTIVE     != sizeKnownAndTooSmall                           )    )
    {
        retState = DcmiPriv_Dma_Start( frameBuffer, bufferSize );

        if( DCMI_REQUEST_OK == retState )
        {
            Dcmi_Reg_Set_CaptureActive();
            dcmi_Instance.CaptureState = DCMI_STATE_WAITING_FRAME;
            retState = DCMI_REQUEST_OK;
        }
        else
        {
            /* DMA could not be armed, no action required */
        }
    }
    else
    {
        /* Invalid parameter, uninitialized instance, capture already active or buffer too small */
        retState = DCMI_REQUEST_ERROR;
    }
#else
    (void)frameBuffer;
    (void)bufferSize;
#endif /* DCMI */

    return ( retState );
}


/**
 * \brief Requests the stop of a continuous capture. The current frame is still completed by the
 *        hardware (CAPTURE is cleared effectively after the frame end, see DCMI_CR.CAPTURE note).
 *
 * \return DCMI_REQUEST_OK on success, DCMI_REQUEST_ERROR for an uninitialized or idle instance.
 */
dcmi_RequestState_t Dcmi_Set_CaptureStop( void )
{
    dcmi_RequestState_t retState = DCMI_REQUEST_ERROR;

#if defined(DCMI)
    if( ( DCMI_FUNCTION_ACTIVE == dcmi_Instance.Initialized  ) &&
        ( DCMI_STATE_IDLE      != dcmi_Instance.CaptureState )    )
    {
        Dcmi_Reg_Set_CaptureInactive();
        retState = DCMI_REQUEST_OK;
    }
    else
    {
        /* Uninitialized or idle instance, no action required */
        retState = DCMI_REQUEST_ERROR;
    }
#else
    /* No peripheral on the device, no action required */
#endif /* DCMI */

    return ( retState );
}


/**
 * \brief Aborts the capture at once (does not wait for the end of the current frame), stops the
 *        Gpdma channel and leaves the instance \ref DCMI_STATE_IDLE.
 *
 * \return DCMI_REQUEST_OK on success, DCMI_REQUEST_ERROR for an uninitialized instance.
 */
dcmi_RequestState_t Dcmi_Set_CaptureAbort( void )
{
    dcmi_RequestState_t retState = DCMI_REQUEST_ERROR;

#if defined(DCMI)
    if( DCMI_FUNCTION_ACTIVE == dcmi_Instance.Initialized )
    {
        Dcmi_Reg_Set_CaptureInactive();
        retState = DcmiPriv_Dma_Stop();
        dcmi_Instance.CaptureState = DCMI_STATE_IDLE;
    }
    else
    {
        /* Uninitialized instance, no action required */
        retState = DCMI_REQUEST_ERROR;
    }
#else
    /* No peripheral on the device, no action required */
#endif /* DCMI */

    return ( retState );
}


/**
 * \brief Reads the capture state machine.
 *
 * \param captureState [out] : Pointer to the capture state.
 *
 * \return DCMI_REQUEST_OK on success, DCMI_REQUEST_ERROR for an invalid parameter or an
 *         uninitialized instance (the state is not changed).
 */
dcmi_RequestState_t Dcmi_Get_CaptureState( dcmi_CaptureState_t * const captureState )
{
    dcmi_RequestState_t retState = DCMI_REQUEST_ERROR;

#if defined(DCMI)
    if( ( DCMI_FUNCTION_ACTIVE == dcmi_Instance.Initialized ) &&
        ( DCMI_NULL_PTR         != captureState              )    )
    {
        *captureState = dcmi_Instance.CaptureState;
        retState = DCMI_REQUEST_OK;
    }
    else
    {
        /* Invalid parameter or uninitialized instance, no action required */
        retState = DCMI_REQUEST_ERROR;
    }
#else
    (void)captureState;
#endif /* DCMI */

    return ( retState );
}


/**
 * \brief Reads the number of frames captured without error since \ref Dcmi_Init.
 *
 * \param frameCount [out] : Pointer to the frame count.
 *
 * \return DCMI_REQUEST_OK on success, DCMI_REQUEST_ERROR for an invalid parameter or an
 *         uninitialized instance (the count is not changed).
 */
dcmi_RequestState_t Dcmi_Get_FrameCount( dcmi_FrameCnt_t * const frameCount )
{
    dcmi_RequestState_t retState = DCMI_REQUEST_ERROR;

#if defined(DCMI)
    if( ( DCMI_FUNCTION_ACTIVE == dcmi_Instance.Initialized ) &&
        ( DCMI_NULL_PTR         != frameCount                )    )
    {
        *frameCount = dcmi_Instance.FrameCount;
        retState = DCMI_REQUEST_OK;
    }
    else
    {
        /* Invalid parameter or uninitialized instance, no action required */
        retState = DCMI_REQUEST_ERROR;
    }
#else
    (void)frameCount;
#endif /* DCMI */

    return ( retState );
}


/**
 * \brief Configures the crop window (DCMI_CWSTRTR / DCMI_CWSIZER) and enables DCMI_CR.CROP.
 *        Rejected while JPEG format is selected (RM0481: crop can not be used in JPEG mode) or
 *        while a capture is armed/running. A window with 0 line/pixel count disables the crop
 *        feature again (full image).
 *
 * \param window [in] : Pointer to the window. CAPCNT (PixelCnt - 1) is rounded down to a
 *                       multiple of 4 by the hardware (RM0481); pass a value already a multiple
 *                       of 4 to avoid a silent reduction of the window.
 *
 * \return DCMI_REQUEST_OK on success, DCMI_REQUEST_ERROR for an invalid parameter, JPEG format,
 *         an uninitialized instance or a capture already armed/running.
 */
dcmi_RequestState_t Dcmi_Set_CropWindow( const dcmi_Window_t * const window )
{
    dcmi_RequestState_t retState = DCMI_REQUEST_ERROR;

#if defined(DCMI)
    if( ( DCMI_FUNCTION_ACTIVE == dcmi_Instance.Initialized           ) &&
        ( DCMI_STATE_IDLE      == dcmi_Instance.CaptureState          ) &&
        ( DCMI_FORMAT_JPEG     != dcmi_Instance.Config.Format         ) &&
        ( DCMI_NULL_PTR         != window                              ) &&
        ( DCMI_CWSTRT_VST_Msk  >= ( (uint32_t)window->FirstLine  << DCMI_CWSTRT_VST_Pos    ) ) &&
        ( DCMI_CWSTRT_HOFFCNT_Msk >= (uint32_t)window->FirstPixel                              ) &&
        ( DCMI_CWSIZE_VLINE_Msk   >= ( (uint32_t)window->LineCnt  << DCMI_CWSIZE_VLINE_Pos  ) ) &&
        ( DCMI_CWSIZE_CAPCNT_Msk  >= (uint32_t)window->PixelCnt                                 )    )
    {
        if( ( 0u == window->LineCnt  ) &&
            ( 0u == window->PixelCnt )    )
        {
            Dcmi_Reset_RegBit( DCMI_REG_CR, DCMI_CR_CROP );
            dcmi_Instance.CropActive = DCMI_FUNCTION_INACTIVE;
        }
        else
        {
            Dcmi_Set_RegVal( DCMI_REG_CWSTRT,
                             ( DCMI_CWSTRT_VST | DCMI_CWSTRT_HOFFCNT ),
                             ( ( (uint32_t)window->FirstLine  << DCMI_CWSTRT_VST_Pos ) |
                               (uint32_t)window->FirstPixel                              ) );
            Dcmi_Set_RegVal( DCMI_REG_CWSIZE,
                             ( DCMI_CWSIZE_VLINE | DCMI_CWSIZE_CAPCNT ),
                             ( ( (uint32_t)window->LineCnt << DCMI_CWSIZE_VLINE_Pos ) |
                               ( (uint32_t)window->PixelCnt - 1u )                       ) );
            Dcmi_Set_RegBit( DCMI_REG_CR, DCMI_CR_CROP );
            dcmi_Instance.CropActive = DCMI_FUNCTION_ACTIVE;
        }

        dcmi_Instance.Window = *window;
        retState = DCMI_REQUEST_OK;
    }
    else
    {
        /* Invalid parameter, JPEG format, uninitialized instance or capture active */
        retState = DCMI_REQUEST_ERROR;
    }
#else
    (void)window;
#endif /* DCMI */

    return ( retState );
}


/**
 * \brief Configures the byte / line selection (frame rate and resolution reduction, DCMI_CR.FCRC
 *        / BSM / OEBS / LSM / OELS). Rejected while a capture is armed/running. The byte
 *        selection fields are meaningful only on the 8-bit bus (RM0481 note); they are written
 *        as given, their effect on a wider bus is undefined by the hardware.
 *
 * \param selection [in] : Pointer to the selection structure.
 *
 * \return DCMI_REQUEST_OK on success, DCMI_REQUEST_ERROR for an invalid parameter, an
 *         uninitialized instance or a capture already armed/running.
 */
dcmi_RequestState_t Dcmi_Set_Selection( const dcmi_Selection_t * const selection )
{
    dcmi_RequestState_t retState = DCMI_REQUEST_ERROR;

#if defined(DCMI)
    uint32_t crValue = 0u;

    if( ( DCMI_FUNCTION_ACTIVE == dcmi_Instance.Initialized       ) &&
        ( DCMI_STATE_IDLE      == dcmi_Instance.CaptureState      ) &&
        ( DCMI_NULL_PTR         != selection                       ) &&
        ( DCMI_FRAME_RATE_CNT   > selection->FrameRateDiv          ) &&
        ( DCMI_BYTE_SELECT_CNT  > selection->ByteSelectMode         )    )
    {
        /* dcmi_FunctionState_t is already 0/1 in the same meaning as the matching DCMI_CR bit */
        crValue = ( ( (uint32_t)selection->FrameRateDiv     << DCMI_CR_FCRC_Pos ) |
                    ( (uint32_t)selection->ByteSelectMode    << DCMI_CR_BSM_Pos  ) |
                    ( (uint32_t)selection->ByteSelectStart   << DCMI_CR_OEBS_Pos ) |
                    ( (uint32_t)selection->LineSelectMode    << DCMI_CR_LSM_Pos  ) |
                    ( (uint32_t)selection->LineSelectStart   << DCMI_CR_OELS_Pos )   );

        Dcmi_Set_RegVal( DCMI_REG_CR,
                         ( DCMI_CR_FCRC | DCMI_CR_BSM | DCMI_CR_OEBS | DCMI_CR_LSM | DCMI_CR_OELS ),
                         crValue );
        retState = DCMI_REQUEST_OK;
    }
    else
    {
        /* Invalid parameter, uninitialized instance or capture active */
        retState = DCMI_REQUEST_ERROR;
    }
#else
    (void)selection;
#endif /* DCMI */

    return ( retState );
}


/**
 * \brief Computes the frame size from the crop window and the pixel format.
 *
 * \note  Without an active crop window the module has no notion of the sensor resolution (the
 *        DCMI hardware itself does not know it either, see the module README "Frame size"); the
 *        caller must then size the buffer itself and this function returns DCMI_REQUEST_ERROR.
 *        A JPEG stream has a variable size, also DCMI_REQUEST_ERROR.
 *
 * \param frameSize [out] : Pointer to the frame size in bytes.
 *
 * \return DCMI_REQUEST_OK on success, DCMI_REQUEST_ERROR for an invalid parameter, an
 *         uninitialized instance, JPEG format or no active crop window (size unknown).
 */
dcmi_RequestState_t Dcmi_Get_FrameSize( dcmi_Size_t * const frameSize )
{
    dcmi_RequestState_t retState = DCMI_REQUEST_ERROR;

#if defined(DCMI)
    uint32_t bytesPerPixel = 0u;

    if( ( DCMI_FUNCTION_ACTIVE == dcmi_Instance.Initialized   ) &&
        ( DCMI_NULL_PTR         != frameSize                   ) &&
        ( DCMI_FUNCTION_ACTIVE == dcmi_Instance.CropActive     )    )
    {
        retState = DcmiPriv_BytesPerPixel( dcmi_Instance.Config.Format,
                                           dcmi_Instance.Config.DataWidth,
                                           &bytesPerPixel );

        if( DCMI_REQUEST_OK == retState )
        {
            *frameSize = ( (dcmi_Size_t)dcmi_Instance.Window.PixelCnt *
                            (dcmi_Size_t)dcmi_Instance.Window.LineCnt * bytesPerPixel );
            retState = DCMI_REQUEST_OK;
        }
        else
        {
            /* JPEG format, size is variable */
            retState = DCMI_REQUEST_ERROR;
        }
    }
    else
    {
        /* Invalid parameter, uninitialized instance or no active crop window */
        retState = DCMI_REQUEST_ERROR;
    }
#else
    (void)frameSize;
#endif /* DCMI */

    return ( retState );
}


/**
 * \brief Configures the embedded synchronization codes (DCMI_ESCR / DCMI_ESUR). Rejected unless
 *        \ref DCMI_SYNC_EMBEDDED is selected and the bus is 8-bit (RM0481: embedded codes are
 *        supported only for the 8-bit parallel data width) or a capture is armed/running.
 *
 * \param syncCodes [in] : Pointer to the synchronization codes structure.
 *
 * \return DCMI_REQUEST_OK on success, DCMI_REQUEST_ERROR for an invalid parameter, a
 *         configuration not using embedded synchronization, an uninitialized instance or a
 *         capture already armed/running.
 */
dcmi_RequestState_t Dcmi_Set_EmbeddedSyncCodes( const dcmi_SyncCodes_t * const syncCodes )
{
    dcmi_RequestState_t retState = DCMI_REQUEST_ERROR;

#if defined(DCMI)
    if( ( DCMI_FUNCTION_ACTIVE  == dcmi_Instance.Initialized       ) &&
        ( DCMI_STATE_IDLE       == dcmi_Instance.CaptureState      ) &&
        ( DCMI_SYNC_EMBEDDED    == dcmi_Instance.Config.SyncMode   ) &&
        ( DCMI_DATA_WIDTH_8     == dcmi_Instance.Config.DataWidth  ) &&
        ( DCMI_NULL_PTR          != syncCodes                       )    )
    {
        Dcmi_Set_RegVal( DCMI_REG_ESCR,
                         ( DCMI_ESCR_FSC | DCMI_ESCR_LSC | DCMI_ESCR_LEC | DCMI_ESCR_FEC ),
                         ( ( (uint32_t)syncCodes->FrameStartCode                    ) |
                           ( (uint32_t)syncCodes->LineStartCode  << DCMI_ESCR_LSC_Pos ) |
                           ( (uint32_t)syncCodes->LineEndCode    << DCMI_ESCR_LEC_Pos ) |
                           ( (uint32_t)syncCodes->FrameEndCode   << DCMI_ESCR_FEC_Pos )   ) );
        Dcmi_Set_RegVal( DCMI_REG_ESUR,
                         ( DCMI_ESUR_FSU | DCMI_ESUR_LSU | DCMI_ESUR_LEU | DCMI_ESUR_FEU ),
                         ( ( (uint32_t)syncCodes->FrameStartUnmask                    ) |
                           ( (uint32_t)syncCodes->LineStartUnmask  << DCMI_ESUR_LSU_Pos ) |
                           ( (uint32_t)syncCodes->LineEndUnmask    << DCMI_ESUR_LEU_Pos ) |
                           ( (uint32_t)syncCodes->FrameEndUnmask   << DCMI_ESUR_FEU_Pos )   ) );
        retState = DCMI_REQUEST_OK;
    }
    else
    {
        /* Invalid parameter, wrong synchronization mode / bus width, uninitialized instance
         * or capture active */
        retState = DCMI_REQUEST_ERROR;
    }
#else
    (void)syncCodes;
#endif /* DCMI */

    return ( retState );
}


/**
 * \brief Sets the NVIC priority of the shared DCMI/PSSI interrupt.
 *
 * \param irqPriority [in] : Requested priority.
 *
 * \return DCMI_REQUEST_OK on success, DCMI_REQUEST_ERROR for an uninitialized instance or a
 *         priority rejected by Nvic.
 */
dcmi_RequestState_t Dcmi_Set_IrqPriority( dcmi_IrqPrio_t irqPriority )
{
    dcmi_RequestState_t retState = DCMI_REQUEST_ERROR;

#if defined(DCMI)
    if( DCMI_FUNCTION_ACTIVE == dcmi_Instance.Initialized )
    {
        const nvic_RequestState_t nvicState = Nvic_Set_PeriphIrq_Prio( NVIC_PERIPH_IRQ_DCMI_PSSI, irqPriority );

        if( NVIC_REQUEST_OK == nvicState )
        {
            dcmi_Instance.Config.IrqPriority = irqPriority;
            retState = DCMI_REQUEST_OK;
        }
        else
        {
            retState = DCMI_REQUEST_ERROR;
        }
    }
    else
    {
        /* Uninitialized instance, no action required */
        retState = DCMI_REQUEST_ERROR;
    }
#else
    (void)irqPriority;
#endif /* DCMI */

    return ( retState );
}


/**
 * \brief Reads back the NVIC priority of the shared DCMI/PSSI interrupt.
 *
 * \param irqPriority [out] : Pointer to the priority.
 *
 * \return DCMI_REQUEST_OK on success, DCMI_REQUEST_ERROR for an invalid parameter, an
 *         uninitialized instance or a read rejected by Nvic.
 */
dcmi_RequestState_t Dcmi_Get_IrqPriority( dcmi_IrqPrio_t * const irqPriority )
{
    dcmi_RequestState_t retState = DCMI_REQUEST_ERROR;

#if defined(DCMI)
    if( ( DCMI_FUNCTION_ACTIVE == dcmi_Instance.Initialized ) &&
        ( DCMI_NULL_PTR         != irqPriority                )    )
    {
        const nvic_RequestState_t nvicState = Nvic_Get_PeriphIrq_Prio( NVIC_PERIPH_IRQ_DCMI_PSSI, irqPriority );

        if( NVIC_REQUEST_OK == nvicState )
        {
            retState = DCMI_REQUEST_OK;
        }
        else
        {
            retState = DCMI_REQUEST_ERROR;
        }
    }
    else
    {
        /* Invalid parameter or uninitialized instance, no action required */
        retState = DCMI_REQUEST_ERROR;
    }
#else
    (void)irqPriority;
#endif /* DCMI */

    return ( retState );
}


/**
 * \brief Configures the DCMI_PIXCLK pin decoded from an encoded pin value (alternate function).
 *
 * \param pixClkPin [in] : Encoded pin value (dcmi_PixClkPin_t item).
 *
 * \return DCMI_REQUEST_OK on success, DCMI_REQUEST_ERROR otherwise.
 */
dcmi_RequestState_t Dcmi_InitPixClkGpio( dcmi_PixClkPin_t pixClkPin )
{
    dcmi_RequestState_t retState = DCMI_REQUEST_ERROR;

#if defined(DCMI)
    if( DCMI_PIXCLK_PIN_UNUSED != pixClkPin )
    {
        retState = DcmiPriv_InitAltFuncGpio( (gpio_PortId_t)DCMI_BIT_MASK_DECODE_PORT( pixClkPin ),
                                              (gpio_PinId_t)DCMI_BIT_MASK_DECODE_PIN( pixClkPin ),
                                              (gpio_AltFunction_t)DCMI_BIT_MASK_DECODE_AF( pixClkPin ) );
    }
    else
    {
        /* Unused pin, no action required */
        retState = DCMI_REQUEST_ERROR;
    }
#else
    (void)pixClkPin;
#endif /* DCMI */

    return ( retState );
}


/**
 * \brief Configures the DCMI_HSYNC and DCMI_VSYNC pins decoded from encoded pin values
 *        (alternate function). Called by Dcmi_Init only when hardware synchronization is used.
 *
 * \param hsyncPin [in] : Encoded pin value (dcmi_HsyncPin_t item).
 * \param vsyncPin [in] : Encoded pin value (dcmi_VsyncPin_t item).
 *
 * \return DCMI_REQUEST_OK when both pins were configured, DCMI_REQUEST_ERROR otherwise.
 */
dcmi_RequestState_t Dcmi_InitSyncGpio( dcmi_HsyncPin_t hsyncPin, dcmi_VsyncPin_t vsyncPin )
{
    dcmi_RequestState_t retState = DCMI_REQUEST_ERROR;

#if defined(DCMI)
    if( ( DCMI_HSYNC_PIN_UNUSED != hsyncPin ) &&
        ( DCMI_VSYNC_PIN_UNUSED != vsyncPin )    )
    {
        const dcmi_RequestState_t hsyncState =
            DcmiPriv_InitAltFuncGpio( (gpio_PortId_t)DCMI_BIT_MASK_DECODE_PORT( hsyncPin ),
                                      (gpio_PinId_t)DCMI_BIT_MASK_DECODE_PIN( hsyncPin ),
                                      (gpio_AltFunction_t)DCMI_BIT_MASK_DECODE_AF( hsyncPin ) );
        const dcmi_RequestState_t vsyncState =
            DcmiPriv_InitAltFuncGpio( (gpio_PortId_t)DCMI_BIT_MASK_DECODE_PORT( vsyncPin ),
                                      (gpio_PinId_t)DCMI_BIT_MASK_DECODE_PIN( vsyncPin ),
                                      (gpio_AltFunction_t)DCMI_BIT_MASK_DECODE_AF( vsyncPin ) );

        if( ( DCMI_REQUEST_OK == hsyncState ) &&
            ( DCMI_REQUEST_OK == vsyncState )    )
        {
            retState = DCMI_REQUEST_OK;
        }
        else
        {
            retState = DCMI_REQUEST_ERROR;
        }
    }
    else
    {
        /* Unused pin, no action required */
        retState = DCMI_REQUEST_ERROR;
    }
#else
    (void)hsyncPin;
    (void)vsyncPin;
#endif /* DCMI */

    return ( retState );
}


/**
 * \brief Configures one DCMI data pin decoded from an encoded pin value (alternate function).
 *        The line number encoded in the value is not used here (Gpio has no notion of it), it
 *        is only used by Dcmi_Init to tell which dcmiConfig->DataPin[] array item it came from.
 *
 * \param dataPin [in] : Encoded pin value (dcmi_DataPin_t item).
 *
 * \return DCMI_REQUEST_OK on success, DCMI_REQUEST_ERROR otherwise.
 */
dcmi_RequestState_t Dcmi_InitDataGpio( dcmi_DataPin_t dataPin )
{
    dcmi_RequestState_t retState = DCMI_REQUEST_ERROR;

#if defined(DCMI)
    if( DCMI_DATA_PIN_UNUSED != dataPin )
    {
        retState = DcmiPriv_InitAltFuncGpio( (gpio_PortId_t)DCMI_BIT_MASK_DECODE_PORT( dataPin ),
                                              (gpio_PinId_t)DCMI_BIT_MASK_DECODE_PIN( dataPin ),
                                              (gpio_AltFunction_t)DCMI_BIT_MASK_DECODE_AF( dataPin ) );
    }
    else
    {
        /* Unused pin, no action required */
        retState = DCMI_REQUEST_ERROR;
    }
#else
    (void)dataPin;
#endif /* DCMI */

    return ( retState );
}


#if defined(DCMI)

/**
 * \brief Returns a pointer to the single DCMI instance run-time state (for the unit tests only,
 *        the module itself always addresses dcmi_Instance directly - this accessor exists so a
 *        test can inspect / corrupt the state without a second copy of it).
 *
 * \param instance [out] : Pointer receiving the address of the instance state.
 *
 * \return DCMI_REQUEST_OK on success, DCMI_REQUEST_ERROR for an invalid parameter.
 */
dcmi_RequestState_t DcmiPriv_Get_Instance( dcmi_InstanceData_t ** const instance )
{
    dcmi_RequestState_t retState = DCMI_REQUEST_ERROR;

    if( DCMI_NULL_PTR != instance )
    {
        *instance = &dcmi_Instance;
        retState = DCMI_REQUEST_OK;
    }
    else
    {
        /* Invalid parameter, no action required */
        retState = DCMI_REQUEST_ERROR;
    }

    return ( retState );
}


/**
 * \brief Computes the Gpdma block size and block repetition count moving \p totalSize bytes of
 *        the frame in one Gpdma transfer (2D block, see GPDMA_TRANSFER_EVENT_2D_BLOCK).
 *
 * - Up to DCMI_DMA_BLOCK_SIZE_MAX (65535) bytes: one block, no repetition.
 * - Above that and up to DCMI_DMA_FRAME_SIZE_MAX: one word (4 byte) block repeated
 *   (totalSize / 4) times - needs a 2D-capable Gpdma channel (see the module README), not
 *   verified on hardware (see the file header note).
 *
 * \param totalSize      [in]  : Total frame byte count, must be a multiple of 4.
 * \param blockSize       [out] : Pointer to the computed block size (bytes).
 * \param blockRepeatCnt  [out] : Pointer to the computed Gpdma block repetition count
 *                                (gpdma_TransferConfig_t.BlockRepetitionCount: 0 means one block).
 *
 * \return DCMI_REQUEST_OK on success, DCMI_REQUEST_ERROR for an invalid parameter (size not a
 *         multiple of 4, zero, or above DCMI_DMA_FRAME_SIZE_MAX).
 */
dcmi_RequestState_t DcmiPriv_ComputeDmaBlocks( dcmi_Size_t totalSize,
                                               dcmi_Size_t * const blockSize,
                                               dcmi_BlockCnt_t * const blockRepeatCnt )
{
    dcmi_RequestState_t retState = DCMI_REQUEST_ERROR;

    if( ( DCMI_NULL_PTR        != blockSize                          ) &&
        ( DCMI_NULL_PTR        != blockRepeatCnt                      ) &&
        ( 0u                    <  totalSize                          ) &&
        ( 0u                    == ( totalSize % DCMI_WORD_SIZE_BYTE ) ) &&
        ( DCMI_DMA_FRAME_SIZE_MAX >= totalSize                          )    )
    {
        if( DCMI_DMA_BLOCK_SIZE_MAX >= totalSize )
        {
            *blockSize      = totalSize;
            *blockRepeatCnt = 0u;
        }
        else
        {
            *blockSize      = DCMI_WORD_SIZE_BYTE;
            *blockRepeatCnt = ( ( totalSize / DCMI_WORD_SIZE_BYTE ) - 1u );
        }

        retState = DCMI_REQUEST_OK;
    }
    else
    {
        /* Invalid parameter, no action required */
        retState = DCMI_REQUEST_ERROR;
    }

    return ( retState );
}

/* =========================== LOCAL FUNCTIONS ============================== */

/**
 * \brief Configures one GPIO pin as a DCMI input (alternate function, no pull - the camera
 *        drives the signal).
 *
 * \param portId  [in] : Port of the pin.
 * \param pinId   [in] : Pin identification.
 * \param altFunc [in] : Alternate function of the pin.
 *
 * \return DCMI_REQUEST_OK on success, DCMI_REQUEST_ERROR when the Gpio module rejects a step.
 */
static dcmi_RequestState_t DcmiPriv_InitAltFuncGpio( gpio_PortId_t portId,
                                                     gpio_PinId_t pinId,
                                                     gpio_AltFunction_t altFunc )
{
    dcmi_RequestState_t retState  = DCMI_REQUEST_ERROR;
    gpio_Config_t         gpioConfig;
    gpio_RequestState_t    gpioState = GPIO_REQUEST_ERROR;

    gpioConfig.PortId         = portId;
    gpioConfig.PinId          = pinId;
    gpioConfig.PinMode        = GPIO_PIN_MODE_ALTERNATE;
    gpioConfig.PinPull        = GPIO_PIN_PULL_NONE;
    gpioConfig.PinSpeed       = GPIO_PIN_SPEED_VERY_HIGH;
    gpioConfig.PinOutType     = GPIO_PIN_OUTPUT_PUSHPULL;
    gpioConfig.PinAltFunction = altFunc;
    gpioConfig.PinActiveLevel = GPIO_PIN_LEVEL_HIGH;
    gpioState                 = Gpio_Init( &gpioConfig );

    if( GPIO_REQUEST_OK == gpioState )
    {
        retState = DCMI_REQUEST_OK;
    }
    else
    {
        /* Gpio rejected the configuration, no action required */
        retState = DCMI_REQUEST_ERROR;
    }

    return ( retState );
}


/**
 * \brief Initializes (or reuses) the Gpdma channel of the frame transfer and enables its error
 *        interrupt. The channel address / size are set at every \ref DcmiPriv_Dma_Start.
 *
 * \return DCMI_REQUEST_OK on success, DCMI_REQUEST_ERROR otherwise.
 */
static dcmi_RequestState_t DcmiPriv_Dma_Init( void )
{
    dcmi_RequestState_t    retState   = DCMI_REQUEST_ERROR;
    gpdma_ConfigStruct_t    dmaConfig  = { 0 };
    gpdma_TransferConfig_t  xferConfig = { 0 };
    gpdma_RequestState_t    dmaState   = GPDMA_REQUEST_ERROR;

    xferConfig.Direction              = GPDMA_DIR_PERIPH_TO_MEMORY;
    xferConfig.EventMode              = GPDMA_TRANSFER_EVENT_2D_BLOCK;
    xferConfig.TriggerType            = GPDMA_TRG_NOT_USED;
    xferConfig.TriggerMode            = GPDMA_TRIGGER_BLOCK;
    xferConfig.RequestSource          = GPDMA_REQ_DCMI;
    xferConfig.RequestMode            = GPDMA_PERIPH_REQ_SINGLE;
    xferConfig.BlockSize              = 0u;
    xferConfig.BlockRepetitionCount   = 0u;
    xferConfig.SourceAddr             = (gpdma_SrcAddr_t)Dcmi_Reg_Get_DataRegAddr();
    xferConfig.SourceDataSize         = GPDMA_DATA_SIZE_32BITS;
    xferConfig.SourceBurstLength      = 1u;
    xferConfig.SourceAddrMode         = GPDMA_ADDR_STATIC;
    xferConfig.SourcePortId           = GPDMA_PORT_DEFAULT;
    xferConfig.SourceDataOp           = GPDMA_SRC_DATA_PRESERVE;
    xferConfig.SourceBlockOffset2D    = 0u;
    xferConfig.SourceRepBlockOffset2D = 0u;
    xferConfig.DestinationAddr        = 0u;
    xferConfig.DestinationDataSize    = GPDMA_DATA_SIZE_32BITS;
    xferConfig.DestinationBurstLength = 1u;
    xferConfig.DestinationAddrMode    = GPDMA_ADDR_INCREMENT;
    xferConfig.DestinationPortId      = GPDMA_PORT_DEFAULT;
    xferConfig.DestinationDataOp      = GPDMA_DEST_DATA_PRESERVE;
    xferConfig.DestinationBlockOffset2D    = 0u;
    xferConfig.DestinationRepBlockOffset2D = 0u;

    dmaState = Gpdma_Get_DefaultConfig( &dmaConfig );

    dmaConfig.PeriphId            = (gpdma_PeriphId_t)dcmi_Instance.Config.DmaPeriphId;
    dmaConfig.ChannelId           = (gpdma_ChannelId_t)dcmi_Instance.Config.DmaChannelId;
    dmaConfig.ChannelPrio         = (gpdma_Priority_t)dcmi_Instance.Config.DmaPriority;
    dmaConfig.TransferExecMode    = GPDMA_XFER_EXEC_CONTINUOUS;
    dmaConfig.TransferConfig      = &xferConfig;
    dmaConfig.TransfersCount      = 1u;
    dmaConfig.XferListAccessMode  = GPDMA_TRANSFER_LIST_ACCESS_SINGLE;
    dmaConfig.XferList            = &dcmi_DmaXferList;
    dmaConfig.TransferLockState    = GPDMA_TRANSFER_LIST_LOCKED;
    dmaConfig.TransferCompleteIsr = Dcmi_Dma_TransferComplete;
    dmaConfig.HalfTransferIsr     = GPDMA_NULL_PTR;
    dmaConfig.ErrorIsr            = Dcmi_Dma_Error;
    dmaConfig.ErrorMask           = DCMI_DMA_ERROR_MASK;

    if( DCMI_FUNCTION_ACTIVE == dcmi_DmaChannelInit )
    {
        dmaState = Gpdma_Set_Priority( dmaConfig.PeriphId, dmaConfig.ChannelId, dmaConfig.ChannelPrio );
    }
    else if( GPDMA_REQUEST_OK == dmaState )
    {
        dmaState = Gpdma_Init( &dmaConfig );

        if( GPDMA_REQUEST_OK == dmaState )
        {
            dcmi_DmaChannelInit = DCMI_FUNCTION_ACTIVE;
        }
        else
        {
            /* Gpdma channel initialization failed */
        }
    }
    else
    {
        /* Default configuration is not available */
    }

    if( GPDMA_REQUEST_OK == dmaState )
    {
        dmaState = Gpdma_Set_InterruptActive( dmaConfig.PeriphId, dmaConfig.ChannelId );
    }
    else
    {
        /* Gpdma channel configuration failed */
    }

    if( GPDMA_REQUEST_OK == dmaState )
    {
        retState = DCMI_REQUEST_OK;
    }
    else
    {
        retState = DCMI_REQUEST_ERROR;
    }

    return ( retState );
}


/**
 * \brief Disables the Gpdma channel interrupt (channel configuration is kept, see
 *        \ref DcmiPriv_Dma_Init).
 *
 * \return DCMI_REQUEST_OK on success (also if no channel was initialized), DCMI_REQUEST_ERROR
 *         otherwise.
 */
static dcmi_RequestState_t DcmiPriv_Dma_Deinit( void )
{
    dcmi_RequestState_t retState = DCMI_REQUEST_ERROR;

    if( DCMI_FUNCTION_ACTIVE == dcmi_DmaChannelInit )
    {
        const gpdma_PeriphId_t  dmaPeriph = (gpdma_PeriphId_t)dcmi_Instance.Config.DmaPeriphId;
        const gpdma_ChannelId_t dmaChan   = (gpdma_ChannelId_t)dcmi_Instance.Config.DmaChannelId;
        gpdma_RequestState_t     dmaState  = Gpdma_Set_ChannelInactive( dmaPeriph, dmaChan );

        if( GPDMA_REQUEST_OK == dmaState )
        {
            dmaState = Gpdma_Set_InterruptInactive( dmaPeriph, dmaChan );
        }
        else
        {
            /* Channel could not be disabled */
        }

        if( GPDMA_REQUEST_OK == dmaState )
        {
            retState = DCMI_REQUEST_OK;
        }
        else
        {
            retState = DCMI_REQUEST_ERROR;
        }
    }
    else
    {
        /* No Gpdma channel was initialized */
        retState = DCMI_REQUEST_OK;
    }

    return ( retState );
}


/**
 * \brief Initializes (or reuses) the Gpdma channel and arms it with the frame buffer address and
 *        the block size / repetition count of \ref DcmiPriv_ComputeDmaBlocks.
 *
 * \param frameBuffer [out] : Destination buffer.
 * \param bufferSize  [in]  : Size of \p frameBuffer in bytes (already checked by the caller).
 *
 * \return DCMI_REQUEST_OK on success, DCMI_REQUEST_ERROR otherwise.
 */
static dcmi_RequestState_t DcmiPriv_Dma_Start( dcmi_Pixel_t * const frameBuffer, dcmi_Size_t bufferSize )
{
    dcmi_RequestState_t retState       = DcmiPriv_Dma_Init();
    dcmi_Size_t         blockSize      = 0u;
    dcmi_BlockCnt_t     blockRepeatCnt = 0u;

    if( DCMI_REQUEST_OK == retState )
    {
        retState = DcmiPriv_ComputeDmaBlocks( bufferSize, &blockSize, &blockRepeatCnt );
    }
    else
    {
        /* Gpdma channel could not be initialized */
    }

    if( DCMI_REQUEST_OK == retState )
    {
        const gpdma_PeriphId_t  dmaPeriph = (gpdma_PeriphId_t)dcmi_Instance.Config.DmaPeriphId;
        const gpdma_ChannelId_t dmaChan   = (gpdma_ChannelId_t)dcmi_Instance.Config.DmaChannelId;

        /* The channel was configured with placeholder BlockSize / BlockRepetitionCount /
         * DestinationAddr at DcmiPriv_Dma_Init (reused across captures); the real frame buffer
         * address and the block layout just computed above are pushed onto the channel here,
         * once per capture, before it is armed (same pattern as I2c_Dma_Set_Transfer). */
        gpdma_RequestState_t dmaState = Gpdma_Set_BlockSize( dmaPeriph, dmaChan, (gpdma_BlockSize_t)blockSize );

        if( GPDMA_REQUEST_OK == dmaState )
        {
            dmaState = Gpdma_Set_BlockRepeatCount( dmaPeriph, dmaChan, (gpdma_BlockRep_t)blockRepeatCnt );
        }
        else
        {
            /* Block size configuration failed */
        }

        if( GPDMA_REQUEST_OK == dmaState )
        {
            dmaState = Gpdma_Set_DestinationAddr( dmaPeriph, dmaChan, (gpdma_DstAddr_t)frameBuffer );
        }
        else
        {
            /* Block repetition count configuration failed */
        }

        if( GPDMA_REQUEST_OK == dmaState )
        {
            (void)Cache_Set_InvalidateRange( CACHE_DATA, (uint32_t)frameBuffer, bufferSize );
            dmaState = Gpdma_Set_ChannelActive( dmaPeriph, dmaChan );
        }
        else
        {
            /* Destination address configuration failed */
        }

        if( GPDMA_REQUEST_OK == dmaState )
        {
            retState = DCMI_REQUEST_OK;
        }
        else
        {
            retState = DCMI_REQUEST_ERROR;
        }
    }
    else
    {
        /* Block computation failed (frame too large or not word aligned) */
    }

    return ( retState );
}


/**
 * \brief Disables the Gpdma channel (keeps its configuration, see \ref DcmiPriv_Dma_Deinit).
 *
 * \return DCMI_REQUEST_OK on success, DCMI_REQUEST_ERROR otherwise.
 */
static dcmi_RequestState_t DcmiPriv_Dma_Stop( void )
{
    dcmi_RequestState_t retState = DCMI_REQUEST_ERROR;

    if( DCMI_FUNCTION_ACTIVE == dcmi_DmaChannelInit )
    {
        const gpdma_PeriphId_t     dmaPeriph = (gpdma_PeriphId_t)dcmi_Instance.Config.DmaPeriphId;
        const gpdma_ChannelId_t    dmaChan   = (gpdma_ChannelId_t)dcmi_Instance.Config.DmaChannelId;
        const gpdma_RequestState_t dmaState  = Gpdma_Set_ChannelInactive( dmaPeriph, dmaChan );

        if( GPDMA_REQUEST_OK == dmaState )
        {
            retState = DCMI_REQUEST_OK;
        }
        else
        {
            retState = DCMI_REQUEST_ERROR;
        }
    }
    else
    {
        /* No Gpdma channel was initialized, no action required */
        retState = DCMI_REQUEST_OK;
    }

    return ( retState );
}


/**
 * \brief Returns the byte count of one captured pixel for a format / bus width combination.
 *
 * \param format        [in]  : Pixel format.
 * \param dataWidth     [in]  : Parallel bus width.
 * \param bytesPerPixel [out] : Pointer to the byte count.
 *
 * \return DCMI_REQUEST_OK on success, DCMI_REQUEST_ERROR for JPEG (variable size) or an invalid
 *         parameter.
 */
static dcmi_RequestState_t DcmiPriv_BytesPerPixel( dcmi_Format_t format,
                                                   dcmi_DataWidth_t dataWidth,
                                                   uint32_t * const bytesPerPixel )
{
    dcmi_RequestState_t retState = DCMI_REQUEST_ERROR;

    if( ( DCMI_NULL_PTR        != bytesPerPixel ) &&
        ( DCMI_FORMAT_CNT       > format         ) &&
        ( DCMI_DATA_WIDTH_CNT   > dataWidth      )    )
    {
        if( DCMI_FORMAT_RAW == format )
        {
            if( DCMI_DATA_WIDTH_8 == dataWidth )
            {
                *bytesPerPixel = DCMI_BYTES_PER_PIXEL_8BIT;
            }
            else
            {
                *bytesPerPixel = DCMI_BYTES_PER_PIXEL_16BIT;
            }
            retState = DCMI_REQUEST_OK;
        }
        else if( DCMI_FORMAT_MONOCHROME == format )
        {
            *bytesPerPixel = DCMI_BYTES_PER_PIXEL_8BIT;
            retState = DCMI_REQUEST_OK;
        }
        else if( ( DCMI_FORMAT_YCBCR422 == format ) ||
                 ( DCMI_FORMAT_RGB565    == format )    )
        {
            *bytesPerPixel = DCMI_BYTES_PER_PIXEL_16BIT;
            retState = DCMI_REQUEST_OK;
        }
        else
        {
            /* DCMI_FORMAT_JPEG, the size is variable */
            retState = DCMI_REQUEST_ERROR;
        }
    }
    else
    {
        /* Invalid parameter, no action required */
        retState = DCMI_REQUEST_ERROR;
    }

    return ( retState );
}


/**
 * \brief Gpdma transfer-complete handler of the frame channel: the whole configured transfer
 *        (every repeated block, see GPDMA_TRANSFER_EVENT_2D_BLOCK) has reached memory - this is
 *        the frame-ready signal used instead of DCMI_RIS.FRAME_RIS (see the file header note).
 *        Invalidates the frame buffer's data cache range so the application reads fresh data,
 *        then reports the frame to the application.
 */
static void Dcmi_Dma_TransferComplete( void )
{
    dcmi_Instance.FrameCount++;

    if( DCMI_CAPTURE_SNAPSHOT == dcmi_Instance.Config.CaptureMode )
    {
        dcmi_Instance.CaptureState = DCMI_STATE_IDLE;
    }
    else
    {
        dcmi_Instance.CaptureState = DCMI_STATE_WAITING_FRAME;
    }

    if( DCMI_NULL_PTR != dcmi_Instance.Config.FrameCallback )
    {
        dcmi_Instance.Config.FrameCallback();
    }
    else
    {
        /* No callback registered, no action required */
    }
}


/**
 * \brief Gpdma error handler of the frame channel, forwarded as \ref DCMI_ERROR_DMA.
 *
 * \param errorMask [in] : Gpdma error bit mask (not forwarded as is, only the module's own
 *                          DCMI_ERROR_DMA bit is reported - see dcmi_ErrorMask_t).
 */
static void Dcmi_Dma_Error( gpdma_ErrorMaskId_t errorMask )
{
    (void)errorMask;

    dcmi_Instance.CaptureState = DCMI_STATE_ERROR;

    if( DCMI_NULL_PTR != dcmi_Instance.Config.ErrorCallback )
    {
        dcmi_Instance.Config.ErrorCallback( DCMI_ERROR_DMA );
    }
    else
    {
        /* No callback registered, no action required */
    }
}


/**
 * \brief DCMI/PSSI shared interrupt handler. Processes VSYNC (start of frame), LINE, OVR and ERR;
 *        FRAME is only cleared here (the frame-ready signal is the Gpdma transfer complete, see
 *        \ref Dcmi_Dma_TransferComplete and the file header note).
 */
static void Dcmi_Isr( void )
{
    const uint32_t rawStatus = Dcmi_Get_RegVal( DCMI_REG_RIS, DCMI_IT_ALL );

    if( 0u != ( rawStatus & DCMI_RIS_VSYNC_RIS ) )
    {
        Dcmi_Set_RegBit( DCMI_REG_ICR, DCMI_ICR_VSYNC_ISC );

        if( DCMI_STATE_WAITING_FRAME == dcmi_Instance.CaptureState )
        {
            dcmi_Instance.CaptureState = DCMI_STATE_CAPTURING;
        }
        else
        {
            /* Not armed (hardware sync mode reports VSYNC even without CAPTURE set), no action required */
        }

        if( DCMI_NULL_PTR != dcmi_Instance.Config.VsyncCallback )
        {
            dcmi_Instance.Config.VsyncCallback();
        }
        else
        {
            /* No callback registered, no action required */
        }
    }
    else
    {
        /* No VSYNC event, no action required */
    }

    if( 0u != ( rawStatus & DCMI_RIS_LINE_RIS ) )
    {
        Dcmi_Set_RegBit( DCMI_REG_ICR, DCMI_ICR_LINE_ISC );

        if( DCMI_NULL_PTR != dcmi_Instance.Config.LineCallback )
        {
            dcmi_Instance.Config.LineCallback();
        }
        else
        {
            /* No callback registered, no action required */
        }
    }
    else
    {
        /* No LINE event, no action required */
    }

    if( 0u != ( rawStatus & DCMI_RIS_FRAME_RIS ) )
    {
        Dcmi_Set_RegBit( DCMI_REG_ICR, DCMI_ICR_FRAME_ISC );
    }
    else
    {
        /* No FRAME event, no action required */
    }

    if( 0u != ( rawStatus & DCMI_RIS_OVR_RIS ) )
    {
        Dcmi_Set_RegBit( DCMI_REG_ICR, DCMI_ICR_OVR_ISC );
        dcmi_Instance.CaptureState = DCMI_STATE_ERROR;

        if( DCMI_NULL_PTR != dcmi_Instance.Config.ErrorCallback )
        {
            dcmi_Instance.Config.ErrorCallback( DCMI_ERROR_OVERRUN );
        }
        else
        {
            /* No callback registered, no action required */
        }
    }
    else
    {
        /* No overrun, no action required */
    }

    if( 0u != ( rawStatus & DCMI_RIS_ERR_RIS ) )
    {
        Dcmi_Set_RegBit( DCMI_REG_ICR, DCMI_ICR_ERR_ISC );
        dcmi_Instance.CaptureState = DCMI_STATE_ERROR;

        if( DCMI_NULL_PTR != dcmi_Instance.Config.ErrorCallback )
        {
            dcmi_Instance.Config.ErrorCallback( DCMI_ERROR_SYNC );
        }
        else
        {
            /* No callback registered, no action required */
        }
    }
    else
    {
        /* No synchronization error, no action required */
    }
}

#endif /* DCMI */
