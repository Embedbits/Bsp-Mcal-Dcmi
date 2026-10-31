/**
 * \author Mr.Nobody
 * \file Test_Dcmi.c
 * \ingroup Dcmi
 * \brief Unit tests of the Dcmi module (STM32H5).
 *
 * Dcmi.c and Dcmi_Reg.c are compiled unchanged with the real CMSIS registers emulated by
 * RegMem (plain host memory); Rcc, Nvic, Gpio, Gpdma and Cache are mocked by CMock. On the
 * lines without a DCMI peripheral (H503/H543/H553) every public function checks the CMSIS
 * definition DCMI and only returns DCMI_REQUEST_ERROR - the tests of this file compile and run
 * on all 12 lines, every DCMI-specific assertion is guarded by `#if defined(DCMI)`.
 *
 * \note  Emulated registers are plain RAM: DCMI_RIS / DCMI_SR (hardware set, read only) and the
 *        write-only DCMI_ICR (set-to-clear) are not modeled with their real hardware behaviour,
 *        a test that needs a raw interrupt flag presets DCMI->RISR directly. DCMI_CR.ENABLE /
 *        DCMI_CR.CAPTURE read-back loops (Dcmi_Reg_Set_Enable / Dcmi_Reg_Get_CaptureState)
 *        succeed immediately because the write already lands in the same plain memory cell.
 *
 * \note  DcmiPriv_Get_Instance() is used by several tests to put the single instance into an
 *        initialized state without calling Dcmi_Init (which would need a longer, strictly
 *        ordered chain of Rcc / Nvic / Gpio mock expectations for a result these tests do not
 *        need to re-check) - see its own Doxygen comment in Dcmi.c for why it exists.
 */

/* ============================= INCLUDES =================================== */
#include "unity.h"                          /* Unity testing framework        */
#include "RegMem.h"                         /* Register memory emulation      */
#include "Dcmi_Port.h"                      /* Module under test (public)     */
#include "Dcmi.h"                           /* Module under test (private)    */
#include "Dcmi_Reg.h"                       /* Register access (private)      */
#include "MockRcc_Port.h"                   /* RCC module mock                */
#include "MockNvic_Port.h"                  /* NVIC module mock                */
#include "MockGpio_Port.h"                  /* GPIO module mock                */
#include "MockGpdma_Port.h"                 /* GPDMA module mock               */
#include "MockCache_Port.h"                 /* Cache module mock               */
#include "Stm32.h"                          /* DCMI registers definition      */
/* ============================= TYPEDEFS =================================== */

/* ======================= FORWARD DECLARATIONS ============================= */

#if defined(DCMI)
static dcmi_Config_t   UtDcmi_Get_MinimalConfig        ( void );
static void             UtDcmi_Force_Initialized        ( dcmi_CaptureState_t captureState,
                                                           dcmi_FunctionState_t cropActive );
#endif /* DCMI */

/* ========================== SYMBOLIC CONSTANTS ============================= */

/** NVIC priority used by the tests */
#define UT_DCMI_IRQ_PRIO                     ( 5u )

/** Frame buffer size used by the capture start tests (a multiple of 4) */
#define UT_DCMI_BUFFER_SIZE_BYTE             ( 64u )

/** Buffer size that is not a multiple of 4 (word alignment check) */
#define UT_DCMI_BUFFER_SIZE_UNALIGNED        ( 3u )

/** Crop window line count used by the tests (QVGA height) */
#define UT_DCMI_WINDOW_LINE_CNT               ( 240u )

/** Crop window pixel count used by the tests (QVGA width) */
#define UT_DCMI_WINDOW_PIXEL_CNT               ( 320u )

/** Crop window first line used by one test (non-zero start) */
#define UT_DCMI_WINDOW_FIRST_LINE               ( 10u )

/** Crop window first pixel used by one test (non-zero start) */
#define UT_DCMI_WINDOW_FIRST_PIXEL               ( 4u )

/** Embedded synchronization frame start delimiter code used by one test */
#define UT_DCMI_SYNC_FRAME_START_CODE             ( 0xA5u )

/** Embedded synchronization frame end delimiter code used by one test */
#define UT_DCMI_SYNC_FRAME_END_CODE                 ( 0x5Au )

/** Embedded synchronization line start delimiter code used by one test */
#define UT_DCMI_SYNC_LINE_START_CODE                 ( 0x11u )

/** Embedded synchronization line end delimiter code used by one test */
#define UT_DCMI_SYNC_LINE_END_CODE                     ( 0x22u )

/** Embedded synchronization unmask byte (every bit compared) used by one test */
#define UT_DCMI_SYNC_UNMASK_ALL                          ( 0xFFu )

/** Gpdma block size field value used to check DcmiPriv_ComputeDmaBlocks (block size) */
#define UT_DCMI_DMA_BLOCK_SIZE                             ( 4u )

/** Frame size (bytes) that is not a multiple of UT_DCMI_DMA_BLOCK_SIZE */
#define UT_DCMI_DMA_SIZE_NOT_WORD_ALIGNED                     ( 5u )

/** Margin (bytes) subtracted from DCMI_DMA_BLOCK_SIZE_MAX to keep it a multiple of 4 */
#define UT_DCMI_DMA_BLOCK_SIZE_MAX_MARGIN                       ( 3u )

/** Margin (bytes) added to DCMI_DMA_BLOCK_SIZE_MAX to cross into the repeated-block branch */
#define UT_DCMI_DMA_BLOCK_SIZE_MAX_STEP                           ( 1u )

/** Margin (bytes) added to DCMI_DMA_FRAME_SIZE_MAX to cross the accepted frame size range */
#define UT_DCMI_DMA_FRAME_SIZE_MAX_STEP                             ( 4u )

/** Sentinel value written before a DcmiPriv_ComputeDmaBlocks call, to prove it really wrote blockRepeatCnt */
#define UT_DCMI_DMA_BLOCK_REPEAT_SENTINEL                             ( 1u )

/** Value of the output parameters of the peripheral check test, proves that they stay unchanged */
#define UT_DCMI_UNTOUCHED_VALUE                                       ( 77u )

/* ============================ TEST FIXTURE ================================ */

void setUp( void )
{
    TEST_ASSERT_EQUAL( REGMEM_REQUEST_OK, RegMem_Reset() );
}


void tearDown( void )
{
    /* No mock to verify beyond the ordered expectations of each test */
}

/* ========================== MODULE VERSION ================================ */

/**
 * \brief   Dcmi_Get_ModuleVersion() returns the version of the module.
 *
 * \par Expected results
 * - Version is 1.0.0.
 */
void Ut_Dcmi_Get_ModuleVersion_Call_ReturnsVersion( void )
{
    dcmi_ModuleVersion_t version = Dcmi_Get_ModuleVersion();

    TEST_ASSERT_EQUAL_UINT8( 1u, version.Major );
    TEST_ASSERT_EQUAL_UINT8( 0u, version.Minor );
    TEST_ASSERT_EQUAL_UINT8( 0u, version.Patch );
}

/* ========================= DEFAULT CONFIGURATION =========================== */

/**
 * \brief   Dcmi_Get_DefaultConfig() rejects a null pointer.
 *
 * \par Expected results
 * - DCMI_REQUEST_ERROR is returned.
 */
void Ut_Dcmi_Get_DefaultConfig_NullPtr_ReturnsError( void )
{
    TEST_ASSERT_EQUAL( DCMI_REQUEST_ERROR, Dcmi_Get_DefaultConfig( DCMI_NULL_PTR ) );
}


/**
 * \brief   Dcmi_Get_DefaultConfig() fills a safe configuration (8-bit bus, hardware sync,
 *          snapshot, no pin, no callback).
 *
 * \par Expected results
 * - DCMI_REQUEST_OK is returned.
 * - DataWidth is 8-bit, SyncMode is hardware, CaptureMode is snapshot.
 * - Every pin and every callback is the "unused" / null item.
 */
void Ut_Dcmi_Get_DefaultConfig_Call_FillsSafeDefaults( void )
{
    dcmi_Config_t config;

#if defined(DCMI)
    TEST_ASSERT_EQUAL( DCMI_REQUEST_OK, Dcmi_Get_DefaultConfig( &config ) );
    TEST_ASSERT_EQUAL( DCMI_DATA_WIDTH_8, config.DataWidth );
    TEST_ASSERT_EQUAL( DCMI_SYNC_HARDWARE, config.SyncMode );
    TEST_ASSERT_EQUAL( DCMI_CAPTURE_SNAPSHOT, config.CaptureMode );
    TEST_ASSERT_EQUAL( DCMI_PIXCLK_PIN_UNUSED, config.PixClkPin );
    TEST_ASSERT_EQUAL( DCMI_HSYNC_PIN_UNUSED, config.HsyncPin );
    TEST_ASSERT_EQUAL( DCMI_VSYNC_PIN_UNUSED, config.VsyncPin );
    TEST_ASSERT_EQUAL( DCMI_DATA_PIN_UNUSED, config.DataPin[0] );
    TEST_ASSERT_EQUAL( DCMI_DATA_PIN_UNUSED, config.DataPin[DCMI_DATA_PIN_CNT - 1u] );
    TEST_ASSERT_NULL( (void *)config.FrameCallback );
    TEST_ASSERT_NULL( (void *)config.ErrorCallback );
#else
    /* No DCMI peripheral on this line: the peripheral check always rejects it */
    TEST_ASSERT_EQUAL( DCMI_REQUEST_ERROR, Dcmi_Get_DefaultConfig( &config ) );
#endif /* DCMI */
}

/* ============================= INITIALIZATION ================================ */

/**
 * \brief   Dcmi_Init() rejects a null configuration pointer (no register and no other module is
 *          touched, so this test needs no mock expectation).
 *
 * \par Expected results
 * - DCMI_REQUEST_ERROR is returned.
 */
void Ut_Dcmi_Init_NullConfig_ReturnsError( void )
{
    TEST_ASSERT_EQUAL( DCMI_REQUEST_ERROR, Dcmi_Init( DCMI_NULL_PTR ) );
}


/**
 * \brief   Dcmi_Init() rejects a configuration with no pixel clock pin configured.
 *
 * \par Expected results
 * - DCMI_REQUEST_ERROR is returned.
 */
void Ut_Dcmi_Init_PixClkPinUnused_ReturnsError( void )
{
    dcmi_Config_t config;

#if defined(DCMI)
    TEST_ASSERT_EQUAL( DCMI_REQUEST_OK, Dcmi_Get_DefaultConfig( &config ) );
#else
    TEST_ASSERT_EQUAL( DCMI_REQUEST_ERROR, Dcmi_Get_DefaultConfig( &config ) );
#endif /* DCMI */

    TEST_ASSERT_EQUAL( DCMI_REQUEST_ERROR, Dcmi_Init( &config ) );
}


/**
 * \brief   Dcmi_Init() with a minimal configuration (embedded synchronization, so no HSYNC /
 *          VSYNC pin, and no data pin) enables the peripheral and configures DCMI_CR.
 *
 * \par Expected results
 * - DCMI_REQUEST_OK is returned.
 * - DCMI_CR.ENABLE is set, DCMI_CR.ESS (embedded sync) is set, DCMI_CR.EDM / CM match the
 *   configuration.
 */
void Ut_Dcmi_Init_Minimal_EnablesPeripheral( void )
{
#if defined(DCMI)
    dcmi_Config_t config = UtDcmi_Get_MinimalConfig();

    Rcc_Set_PeriphActive_ExpectAndReturn( RCC_PERIPH_DCMI_PSSI, RCC_REQUEST_OK );
    Nvic_Set_PeriphIrq_Handler_ExpectAnyArgsAndReturn( NVIC_REQUEST_OK );
    Nvic_Set_PeriphIrq_Prio_ExpectAndReturn( NVIC_PERIPH_IRQ_DCMI_PSSI, UT_DCMI_IRQ_PRIO, NVIC_REQUEST_OK );
    Gpio_Init_ExpectAnyArgsAndReturn( GPIO_REQUEST_OK );
    Nvic_Set_PeriphIrq_Active_ExpectAndReturn( NVIC_PERIPH_IRQ_DCMI_PSSI, NVIC_REQUEST_OK );

    TEST_ASSERT_EQUAL( DCMI_REQUEST_OK, Dcmi_Init( &config ) );

    TEST_ASSERT_EQUAL_HEX32( DCMI_CR_ENABLE, DCMI->CR & DCMI_CR_ENABLE );
    TEST_ASSERT_EQUAL_HEX32( DCMI_CR_ESS, DCMI->CR & DCMI_CR_ESS );
    TEST_ASSERT_EQUAL_HEX32( DCMI_CR_CM, DCMI->CR & DCMI_CR_CM );
#else
    TEST_IGNORE_MESSAGE( "No DCMI peripheral on this line" );
#endif /* DCMI */
}


/**
 * \brief   Dcmi_Init() rejects a second call while an instance is already armed/running is out
 *          of scope here (single instance, no concurrent Init path); this test only checks that
 *          Dcmi_Deinit() on an instance that was never initialized is rejected.
 *
 * \par Expected results
 * - DCMI_REQUEST_ERROR is returned.
 */
void Ut_Dcmi_Deinit_NotInitialized_ReturnsError( void )
{
    TEST_ASSERT_EQUAL( DCMI_REQUEST_ERROR, Dcmi_Deinit() );
}


/**
 * \brief   Dcmi_Deinit() on an instance forced into the initialized state (no Gpdma channel was
 *          ever created) disables the peripheral and releases Rcc / Nvic.
 *
 * \par Expected results
 * - DCMI_REQUEST_OK is returned.
 * - DCMI_CR.ENABLE is cleared.
 */
void Ut_Dcmi_Deinit_Initialized_DisablesPeripheral( void )
{
#if defined(DCMI)
    UtDcmi_Force_Initialized( DCMI_STATE_IDLE, DCMI_FUNCTION_INACTIVE );
    Dcmi_Set_RegBit( DCMI_REG_CR, DCMI_CR_ENABLE );

    Nvic_Set_PeriphIrq_Inactive_ExpectAndReturn( NVIC_PERIPH_IRQ_DCMI_PSSI, NVIC_REQUEST_OK );
    Rcc_Set_PeriphInactive_ExpectAndReturn( RCC_PERIPH_DCMI_PSSI, RCC_REQUEST_OK );

    TEST_ASSERT_EQUAL( DCMI_REQUEST_OK, Dcmi_Deinit() );
    TEST_ASSERT_EQUAL_HEX32( 0u, DCMI->CR & DCMI_CR_ENABLE );
#else
    TEST_IGNORE_MESSAGE( "No DCMI peripheral on this line" );
#endif /* DCMI */
}


/**
 * \brief   Dcmi_Task() never fails (no polled work exists, see the file header deviation note).
 */
void Ut_Dcmi_Task_Call_DoesNotCrash( void )
{
    Dcmi_Task();
}

/* ============================== CAPTURE START ================================ */

/**
 * \brief   Dcmi_Set_CaptureStart() rejects the request on an instance that was never initialized.
 *
 * \par Expected results
 * - DCMI_REQUEST_ERROR is returned.
 */
void Ut_Dcmi_Set_CaptureStart_NotInitialized_ReturnsError( void )
{
    dcmi_Pixel_t buffer[ UT_DCMI_BUFFER_SIZE_BYTE ];

    TEST_ASSERT_EQUAL( DCMI_REQUEST_ERROR, Dcmi_Set_CaptureStart( buffer, UT_DCMI_BUFFER_SIZE_BYTE ) );
}


/**
 * \brief   Dcmi_Set_CaptureStart() rejects a buffer size that is not a multiple of 4 (the frame
 *          buffer is a raster of 32-bit words, see DCMI_WORD_SIZE_BYTE).
 *
 * \par Expected results
 * - DCMI_REQUEST_ERROR is returned.
 */
void Ut_Dcmi_Set_CaptureStart_BufferNotWordAligned_ReturnsError( void )
{
#if defined(DCMI)
    dcmi_Pixel_t buffer[ UT_DCMI_BUFFER_SIZE_BYTE ];

    UtDcmi_Force_Initialized( DCMI_STATE_IDLE, DCMI_FUNCTION_INACTIVE );

    TEST_ASSERT_EQUAL( DCMI_REQUEST_ERROR, Dcmi_Set_CaptureStart( buffer, UT_DCMI_BUFFER_SIZE_UNALIGNED ) );
#else
    TEST_IGNORE_MESSAGE( "No DCMI peripheral on this line" );
#endif /* DCMI */
}


/**
 * \brief   Dcmi_Set_CaptureStart() rejects the request while a capture is already armed/running.
 *
 * \par Expected results
 * - DCMI_REQUEST_ERROR is returned.
 */
void Ut_Dcmi_Set_CaptureStart_AlreadyCapturing_ReturnsError( void )
{
#if defined(DCMI)
    dcmi_Pixel_t buffer[ UT_DCMI_BUFFER_SIZE_BYTE ];

    UtDcmi_Force_Initialized( DCMI_STATE_CAPTURING, DCMI_FUNCTION_INACTIVE );

    TEST_ASSERT_EQUAL( DCMI_REQUEST_ERROR, Dcmi_Set_CaptureStart( buffer, UT_DCMI_BUFFER_SIZE_BYTE ) );
#else
    TEST_IGNORE_MESSAGE( "No DCMI peripheral on this line" );
#endif /* DCMI */
}


/**
 * \brief   Dcmi_Set_CaptureStart() arms the Gpdma channel and DCMI_CR.CAPTURE when the Gpdma
 *          configuration succeeds.
 *
 * \note    Regression test (AB#1696): DcmiPriv_Dma_Init() configures the Gpdma channel with
 *          placeholder BlockSize / BlockRepetitionCount / DestinationAddr (0) because the real
 *          frame buffer / size are known only at Dcmi_Set_CaptureStart() time, not at Init time
 *          (the channel is reused across captures). DcmiPriv_Dma_Start() must therefore push the
 *          computed block layout and the real buffer address onto the already-initialized
 *          channel before arming it - the exact arguments are asserted below (not just
 *          "any args"), so a future regression that forgets this per-capture push (leaving the
 *          channel at BlockSize=0 / DestinationAddr=0, meaning the frame would never actually
 *          land in the caller's buffer) fails this test instead of only being caught by a
 *          hardware test no board can run.
 *
 * \par Expected results
 * - DCMI_REQUEST_OK is returned.
 * - Gpdma_Set_BlockSize / Gpdma_Set_BlockRepeatCount / Gpdma_Set_DestinationAddr are called,
 *   in this order, with the real buffer size / 0 repeats / the real buffer address.
 * - DCMI_CR.CAPTURE is set, the capture state becomes DCMI_STATE_WAITING_FRAME.
 */
void Ut_Dcmi_Set_CaptureStart_DmaConfigOk_ArmsCapture( void )
{
#if defined(DCMI)
    dcmi_Pixel_t         buffer[ UT_DCMI_BUFFER_SIZE_BYTE ];
    dcmi_CaptureState_t   captureState = DCMI_STATE_ERROR;

    UtDcmi_Force_Initialized( DCMI_STATE_IDLE, DCMI_FUNCTION_INACTIVE );

    Gpdma_Get_DefaultConfig_ExpectAnyArgsAndReturn( GPDMA_REQUEST_OK );
    Gpdma_Init_ExpectAnyArgsAndReturn( GPDMA_REQUEST_OK );
    Gpdma_Set_InterruptActive_ExpectAnyArgsAndReturn( GPDMA_REQUEST_OK );
    /* UtDcmi_Force_Initialized() does not set DmaPeriphId / DmaChannelId - the static instance
     * is zero-initialized (fresh process per test case), so they are GPDMA_PERIPH_1 / _CHANNEL_0 */
    Gpdma_Set_BlockSize_ExpectAndReturn( GPDMA_PERIPH_1,
                                         GPDMA_CHANNEL_0,
                                         (gpdma_BlockSize_t)UT_DCMI_BUFFER_SIZE_BYTE,
                                         GPDMA_REQUEST_OK );
    Gpdma_Set_BlockRepeatCount_ExpectAndReturn( GPDMA_PERIPH_1,
                                                GPDMA_CHANNEL_0,
                                                (gpdma_BlockRep_t)0u,
                                                GPDMA_REQUEST_OK );
    Gpdma_Set_DestinationAddr_ExpectAndReturn( GPDMA_PERIPH_1,
                                               GPDMA_CHANNEL_0,
                                               (gpdma_DstAddr_t)buffer,
                                               GPDMA_REQUEST_OK );
    Cache_Set_InvalidateRange_ExpectAnyArgsAndReturn( CACHE_REQUEST_OK );
    Gpdma_Set_ChannelActive_ExpectAnyArgsAndReturn( GPDMA_REQUEST_OK );

    TEST_ASSERT_EQUAL( DCMI_REQUEST_OK, Dcmi_Set_CaptureStart( buffer, UT_DCMI_BUFFER_SIZE_BYTE ) );
    TEST_ASSERT_EQUAL_HEX32( DCMI_CR_CAPTURE, DCMI->CR & DCMI_CR_CAPTURE );
    TEST_ASSERT_EQUAL( DCMI_REQUEST_OK, Dcmi_Get_CaptureState( &captureState ) );
    TEST_ASSERT_EQUAL( DCMI_STATE_WAITING_FRAME, captureState );
#else
    TEST_IGNORE_MESSAGE( "No DCMI peripheral on this line" );
#endif /* DCMI */
}


/**
 * \brief   Dcmi_Set_CaptureStart() does not arm DCMI_CR.CAPTURE when the Gpdma channel could not
 *          be initialized.
 *
 * \par Expected results
 * - DCMI_REQUEST_ERROR is returned.
 * - DCMI_CR.CAPTURE stays cleared.
 */
void Ut_Dcmi_Set_CaptureStart_DmaConfigFails_LeavesCaptureCleared( void )
{
#if defined(DCMI)
    dcmi_Pixel_t buffer[ UT_DCMI_BUFFER_SIZE_BYTE ];

    UtDcmi_Force_Initialized( DCMI_STATE_IDLE, DCMI_FUNCTION_INACTIVE );

    Gpdma_Get_DefaultConfig_ExpectAnyArgsAndReturn( GPDMA_REQUEST_OK );
    Gpdma_Init_ExpectAnyArgsAndReturn( GPDMA_REQUEST_ERROR );

    TEST_ASSERT_EQUAL( DCMI_REQUEST_ERROR, Dcmi_Set_CaptureStart( buffer, UT_DCMI_BUFFER_SIZE_BYTE ) );
    TEST_ASSERT_EQUAL_HEX32( 0u, DCMI->CR & DCMI_CR_CAPTURE );
#else
    TEST_IGNORE_MESSAGE( "No DCMI peripheral on this line" );
#endif /* DCMI */
}


/**
 * \brief   Dcmi_Set_CaptureStop() rejects the request on an instance that was never initialized.
 *
 * \par Expected results
 * - DCMI_REQUEST_ERROR is returned.
 */
void Ut_Dcmi_Set_CaptureStop_NotInitialized_ReturnsError( void )
{
    TEST_ASSERT_EQUAL( DCMI_REQUEST_ERROR, Dcmi_Set_CaptureStop() );
}


/**
 * \brief   Dcmi_Set_CaptureStop() rejects the request on an idle instance (nothing armed).
 *
 * \par Expected results
 * - DCMI_REQUEST_ERROR is returned.
 */
void Ut_Dcmi_Set_CaptureStop_Idle_ReturnsError( void )
{
#if defined(DCMI)
    UtDcmi_Force_Initialized( DCMI_STATE_IDLE, DCMI_FUNCTION_INACTIVE );

    TEST_ASSERT_EQUAL( DCMI_REQUEST_ERROR, Dcmi_Set_CaptureStop() );
#else
    TEST_IGNORE_MESSAGE( "No DCMI peripheral on this line" );
#endif /* DCMI */
}


/**
 * \brief   Dcmi_Set_CaptureStop() on a running capture clears DCMI_CR.CAPTURE.
 *
 * \par Expected results
 * - DCMI_REQUEST_OK is returned.
 * - DCMI_CR.CAPTURE is cleared.
 */
void Ut_Dcmi_Set_CaptureStop_Capturing_ClearsCaptureBit( void )
{
#if defined(DCMI)
    UtDcmi_Force_Initialized( DCMI_STATE_CAPTURING, DCMI_FUNCTION_INACTIVE );
    Dcmi_Set_RegBit( DCMI_REG_CR, DCMI_CR_CAPTURE );

    TEST_ASSERT_EQUAL( DCMI_REQUEST_OK, Dcmi_Set_CaptureStop() );
    TEST_ASSERT_EQUAL_HEX32( 0u, DCMI->CR & DCMI_CR_CAPTURE );
#else
    TEST_IGNORE_MESSAGE( "No DCMI peripheral on this line" );
#endif /* DCMI */
}


/**
 * \brief   Dcmi_Set_CaptureAbort() rejects the request on an instance that was never initialized.
 *
 * \par Expected results
 * - DCMI_REQUEST_ERROR is returned.
 */
void Ut_Dcmi_Set_CaptureAbort_NotInitialized_ReturnsError( void )
{
    TEST_ASSERT_EQUAL( DCMI_REQUEST_ERROR, Dcmi_Set_CaptureAbort() );
}


/**
 * \brief   Dcmi_Set_CaptureAbort() on an initialized instance (no Gpdma channel was ever created)
 *          clears DCMI_CR.CAPTURE and leaves DCMI_STATE_IDLE without touching Gpdma.
 *
 * \par Expected results
 * - DCMI_REQUEST_OK is returned.
 * - The capture state is DCMI_STATE_IDLE.
 */
void Ut_Dcmi_Set_CaptureAbort_NoDmaChannel_LeavesIdle( void )
{
#if defined(DCMI)
    dcmi_CaptureState_t captureState = DCMI_STATE_ERROR;

    UtDcmi_Force_Initialized( DCMI_STATE_CAPTURING, DCMI_FUNCTION_INACTIVE );
    Dcmi_Set_RegBit( DCMI_REG_CR, DCMI_CR_CAPTURE );

    TEST_ASSERT_EQUAL( DCMI_REQUEST_OK, Dcmi_Set_CaptureAbort() );
    TEST_ASSERT_EQUAL_HEX32( 0u, DCMI->CR & DCMI_CR_CAPTURE );
    TEST_ASSERT_EQUAL( DCMI_REQUEST_OK, Dcmi_Get_CaptureState( &captureState ) );
    TEST_ASSERT_EQUAL( DCMI_STATE_IDLE, captureState );
#else
    TEST_IGNORE_MESSAGE( "No DCMI peripheral on this line" );
#endif /* DCMI */
}

/* ============================ STATE ACCESSORS ================================ */

/**
 * \brief   Dcmi_Get_CaptureState() rejects a null output pointer.
 *
 * \par Expected results
 * - DCMI_REQUEST_ERROR is returned.
 */
void Ut_Dcmi_Get_CaptureState_NullPtr_ReturnsError( void )
{
#if defined(DCMI)
    UtDcmi_Force_Initialized( DCMI_STATE_IDLE, DCMI_FUNCTION_INACTIVE );
#endif /* DCMI */

    TEST_ASSERT_EQUAL( DCMI_REQUEST_ERROR, Dcmi_Get_CaptureState( DCMI_NULL_PTR ) );
}


/**
 * \brief   Dcmi_Get_FrameCount() rejects the request on an instance that was never initialized.
 *
 * \par Expected results
 * - DCMI_REQUEST_ERROR is returned.
 */
void Ut_Dcmi_Get_FrameCount_NotInitialized_ReturnsError( void )
{
    uint32_t frameCount = 0u;

    TEST_ASSERT_EQUAL( DCMI_REQUEST_ERROR, Dcmi_Get_FrameCount( &frameCount ) );
}

/* ========================= CROP WINDOW / SELECTION ============================ */

/**
 * \brief   Dcmi_Set_CropWindow() rejects the request on an instance that was never initialized.
 *
 * \par Expected results
 * - DCMI_REQUEST_ERROR is returned.
 */
void Ut_Dcmi_Set_CropWindow_NotInitialized_ReturnsError( void )
{
    dcmi_Window_t window = { .FirstLine = 0u, .FirstPixel = 0u,
                              .LineCnt = UT_DCMI_WINDOW_LINE_CNT, .PixelCnt = UT_DCMI_WINDOW_PIXEL_CNT };

    TEST_ASSERT_EQUAL( DCMI_REQUEST_ERROR, Dcmi_Set_CropWindow( &window ) );
}


/**
 * \brief   Dcmi_Set_CropWindow() with a valid window writes DCMI_CWSTRTR / DCMI_CWSIZER, sets
 *          DCMI_CR.CROP and marks the crop feature active.
 *
 * \par Expected results
 * - DCMI_REQUEST_OK is returned.
 * - DCMI_CWSIZER.CAPCNT is (PixelCnt - 1), DCMI_CR.CROP is set.
 */
void Ut_Dcmi_Set_CropWindow_ValidWindow_WritesRegisters( void )
{
#if defined(DCMI)
    dcmi_Window_t window = { .FirstLine  = UT_DCMI_WINDOW_FIRST_LINE, .FirstPixel = UT_DCMI_WINDOW_FIRST_PIXEL,
                              .LineCnt    = UT_DCMI_WINDOW_LINE_CNT,   .PixelCnt   = UT_DCMI_WINDOW_PIXEL_CNT };

    UtDcmi_Force_Initialized( DCMI_STATE_IDLE, DCMI_FUNCTION_INACTIVE );

    TEST_ASSERT_EQUAL( DCMI_REQUEST_OK, Dcmi_Set_CropWindow( &window ) );
    TEST_ASSERT_EQUAL_HEX32( ( window.PixelCnt - 1u ), DCMI->CWSIZER & DCMI_CWSIZE_CAPCNT );
    TEST_ASSERT_EQUAL_HEX32( DCMI_CR_CROP, DCMI->CR & DCMI_CR_CROP );
#else
    TEST_IGNORE_MESSAGE( "No DCMI peripheral on this line" );
#endif /* DCMI */
}


/**
 * \brief   Dcmi_Set_CropWindow() with a zero line/pixel count disables the crop feature again.
 *
 * \par Expected results
 * - DCMI_REQUEST_OK is returned.
 * - DCMI_CR.CROP is cleared.
 */
void Ut_Dcmi_Set_CropWindow_ZeroWindow_DisablesCrop( void )
{
#if defined(DCMI)
    dcmi_Window_t zeroWindow = { .FirstLine = 0u, .FirstPixel = 0u, .LineCnt = 0u, .PixelCnt = 0u };

    UtDcmi_Force_Initialized( DCMI_STATE_IDLE, DCMI_FUNCTION_ACTIVE );
    Dcmi_Set_RegBit( DCMI_REG_CR, DCMI_CR_CROP );

    TEST_ASSERT_EQUAL( DCMI_REQUEST_OK, Dcmi_Set_CropWindow( &zeroWindow ) );
    TEST_ASSERT_EQUAL_HEX32( 0u, DCMI->CR & DCMI_CR_CROP );
#else
    TEST_IGNORE_MESSAGE( "No DCMI peripheral on this line" );
#endif /* DCMI */
}


/**
 * \brief   Dcmi_Set_Selection() rejects the request on an instance that was never initialized.
 *
 * \par Expected results
 * - DCMI_REQUEST_ERROR is returned.
 */
void Ut_Dcmi_Set_Selection_NotInitialized_ReturnsError( void )
{
    dcmi_Selection_t selection =
    {
        .FrameRateDiv     = DCMI_FRAME_RATE_HALF,
        .ByteSelectMode   = DCMI_BYTE_SELECT_ALL,
        .ByteSelectStart  = DCMI_FUNCTION_INACTIVE,
        .LineSelectMode   = DCMI_FUNCTION_INACTIVE,
        .LineSelectStart  = DCMI_FUNCTION_INACTIVE,
    };

    TEST_ASSERT_EQUAL( DCMI_REQUEST_ERROR, Dcmi_Set_Selection( &selection ) );
}


/**
 * \brief   Dcmi_Set_Selection() with a valid selection writes the FCRC / LSM bits of DCMI_CR.
 *
 * \par Expected results
 * - DCMI_REQUEST_OK is returned.
 * - DCMI_CR.FCRC is DCMI_FRAME_RATE_HALF, DCMI_CR.LSM is set.
 */
void Ut_Dcmi_Set_Selection_ValidSelection_WritesRegister( void )
{
#if defined(DCMI)
    dcmi_Selection_t selection =
    {
        .FrameRateDiv     = DCMI_FRAME_RATE_HALF,
        .ByteSelectMode   = DCMI_BYTE_SELECT_ALL,
        .ByteSelectStart  = DCMI_FUNCTION_INACTIVE,
        .LineSelectMode   = DCMI_FUNCTION_ACTIVE,
        .LineSelectStart  = DCMI_FUNCTION_INACTIVE,
    };

    UtDcmi_Force_Initialized( DCMI_STATE_IDLE, DCMI_FUNCTION_INACTIVE );

    TEST_ASSERT_EQUAL( DCMI_REQUEST_OK, Dcmi_Set_Selection( &selection ) );
    TEST_ASSERT_EQUAL_HEX32( ( (uint32_t)DCMI_FRAME_RATE_HALF << DCMI_CR_FCRC_Pos ), DCMI->CR & DCMI_CR_FCRC );
    TEST_ASSERT_EQUAL_HEX32( DCMI_CR_LSM, DCMI->CR & DCMI_CR_LSM );
#else
    TEST_IGNORE_MESSAGE( "No DCMI peripheral on this line" );
#endif /* DCMI */
}

/* ============================== FRAME SIZE ==================================== */

/**
 * \brief   Dcmi_Get_FrameSize() returns an error when the crop window is not active (the module
 *          has no notion of the sensor resolution, see the function's Doxygen comment).
 *
 * \par Expected results
 * - DCMI_REQUEST_ERROR is returned.
 */
void Ut_Dcmi_Get_FrameSize_CropInactive_ReturnsError( void )
{
#if defined(DCMI)
    dcmi_Size_t frameSize = 0u;

    UtDcmi_Force_Initialized( DCMI_STATE_IDLE, DCMI_FUNCTION_INACTIVE );

    TEST_ASSERT_EQUAL( DCMI_REQUEST_ERROR, Dcmi_Get_FrameSize( &frameSize ) );
#else
    TEST_IGNORE_MESSAGE( "No DCMI peripheral on this line" );
#endif /* DCMI */
}


/**
 * \brief   Dcmi_Get_FrameSize() with an active crop window and RGB565 format returns
 *          PixelCnt * LineCnt * 2.
 *
 * \par Expected results
 * - DCMI_REQUEST_OK is returned.
 * - frameSize is 320 * 240 * 2.
 */
void Ut_Dcmi_Get_FrameSize_CropActiveRgb565_ReturnsPixelTimesLineTimesTwo( void )
{
#if defined(DCMI)
    dcmi_Size_t           frameSize = 0u;
    dcmi_InstanceData_t * instance  = DCMI_NULL_PTR;
    dcmi_Window_t          window    = { .FirstLine = 0u, .FirstPixel = 0u,
                                           .LineCnt = UT_DCMI_WINDOW_LINE_CNT, .PixelCnt = UT_DCMI_WINDOW_PIXEL_CNT };

    UtDcmi_Force_Initialized( DCMI_STATE_IDLE, DCMI_FUNCTION_ACTIVE );
    TEST_ASSERT_EQUAL( DCMI_REQUEST_OK, DcmiPriv_Get_Instance( &instance ) );
    instance->Config.Format = DCMI_FORMAT_RGB565;
    instance->Window         = window;

    TEST_ASSERT_EQUAL( DCMI_REQUEST_OK, Dcmi_Get_FrameSize( &frameSize ) );
    TEST_ASSERT_EQUAL_UINT32( ( UT_DCMI_WINDOW_PIXEL_CNT * UT_DCMI_WINDOW_LINE_CNT * DCMI_BYTES_PER_PIXEL_16BIT ),
                              frameSize );
#else
    TEST_IGNORE_MESSAGE( "No DCMI peripheral on this line" );
#endif /* DCMI */
}


/**
 * \brief   Dcmi_Get_FrameSize() returns an error for the JPEG format (variable size).
 *
 * \par Expected results
 * - DCMI_REQUEST_ERROR is returned.
 */
void Ut_Dcmi_Get_FrameSize_JpegFormat_ReturnsError( void )
{
#if defined(DCMI)
    dcmi_Size_t           frameSize = 0u;
    dcmi_InstanceData_t * instance  = DCMI_NULL_PTR;

    UtDcmi_Force_Initialized( DCMI_STATE_IDLE, DCMI_FUNCTION_ACTIVE );
    TEST_ASSERT_EQUAL( DCMI_REQUEST_OK, DcmiPriv_Get_Instance( &instance ) );
    instance->Config.Format = DCMI_FORMAT_JPEG;
    instance->Window.LineCnt  = 1u;
    instance->Window.PixelCnt = 1u;

    TEST_ASSERT_EQUAL( DCMI_REQUEST_ERROR, Dcmi_Get_FrameSize( &frameSize ) );
#else
    TEST_IGNORE_MESSAGE( "No DCMI peripheral on this line" );
#endif /* DCMI */
}

/* ====================== EMBEDDED SYNCHRONIZATION CODES ========================= */

/**
 * \brief   Dcmi_Set_EmbeddedSyncCodes() rejects the request when the configuration does not use
 *          embedded synchronization (RM0481: valid only for the 8-bit bus with DCMI_CR.ESS set).
 *
 * \par Expected results
 * - DCMI_REQUEST_ERROR is returned.
 */
void Ut_Dcmi_Set_EmbeddedSyncCodes_HardwareSyncMode_ReturnsError( void )
{
#if defined(DCMI)
    dcmi_SyncCodes_t syncCodes = { 0 };

    UtDcmi_Force_Initialized( DCMI_STATE_IDLE, DCMI_FUNCTION_INACTIVE );

    TEST_ASSERT_EQUAL( DCMI_REQUEST_ERROR, Dcmi_Set_EmbeddedSyncCodes( &syncCodes ) );
#else
    TEST_IGNORE_MESSAGE( "No DCMI peripheral on this line" );
#endif /* DCMI */
}


/**
 * \brief   Dcmi_Set_EmbeddedSyncCodes() with the embedded mode selected writes DCMI_ESCR / ESUR.
 *
 * \par Expected results
 * - DCMI_REQUEST_OK is returned.
 * - DCMI_ESCR.FSC matches FrameStartCode.
 */
void Ut_Dcmi_Set_EmbeddedSyncCodes_EmbeddedSyncMode_WritesRegisters( void )
{
#if defined(DCMI)
    dcmi_InstanceData_t * instance = DCMI_NULL_PTR;
    dcmi_SyncCodes_t       syncCodes =
    {
        .FrameStartCode = UT_DCMI_SYNC_FRAME_START_CODE, .FrameEndCode = UT_DCMI_SYNC_FRAME_END_CODE,
        .LineStartCode = UT_DCMI_SYNC_LINE_START_CODE, .LineEndCode = UT_DCMI_SYNC_LINE_END_CODE,
        .FrameStartUnmask = UT_DCMI_SYNC_UNMASK_ALL, .FrameEndUnmask = UT_DCMI_SYNC_UNMASK_ALL,
        .LineStartUnmask = UT_DCMI_SYNC_UNMASK_ALL, .LineEndUnmask = UT_DCMI_SYNC_UNMASK_ALL,
    };

    UtDcmi_Force_Initialized( DCMI_STATE_IDLE, DCMI_FUNCTION_INACTIVE );
    TEST_ASSERT_EQUAL( DCMI_REQUEST_OK, DcmiPriv_Get_Instance( &instance ) );
    instance->Config.SyncMode  = DCMI_SYNC_EMBEDDED;
    instance->Config.DataWidth = DCMI_DATA_WIDTH_8;

    TEST_ASSERT_EQUAL( DCMI_REQUEST_OK, Dcmi_Set_EmbeddedSyncCodes( &syncCodes ) );
    TEST_ASSERT_EQUAL_HEX32( UT_DCMI_SYNC_FRAME_START_CODE, DCMI->ESCR & DCMI_ESCR_FSC );
#else
    TEST_IGNORE_MESSAGE( "No DCMI peripheral on this line" );
#endif /* DCMI */
}

/* ============================== INTERRUPT PRIORITY ============================= */

/**
 * \brief   Dcmi_Set_IrqPriority() rejects the request on an instance that was never initialized.
 *
 * \par Expected results
 * - DCMI_REQUEST_ERROR is returned.
 */
void Ut_Dcmi_Set_IrqPriority_NotInitialized_ReturnsError( void )
{
    TEST_ASSERT_EQUAL( DCMI_REQUEST_ERROR, Dcmi_Set_IrqPriority( UT_DCMI_IRQ_PRIO ) );
}


/**
 * \brief   Dcmi_Set_IrqPriority() on an initialized instance forwards the priority to Nvic.
 *
 * \par Expected results
 * - DCMI_REQUEST_OK is returned.
 */
void Ut_Dcmi_Set_IrqPriority_Initialized_ForwardsToNvic( void )
{
#if defined(DCMI)
    UtDcmi_Force_Initialized( DCMI_STATE_IDLE, DCMI_FUNCTION_INACTIVE );

    Nvic_Set_PeriphIrq_Prio_ExpectAndReturn( NVIC_PERIPH_IRQ_DCMI_PSSI, UT_DCMI_IRQ_PRIO, NVIC_REQUEST_OK );

    TEST_ASSERT_EQUAL( DCMI_REQUEST_OK, Dcmi_Set_IrqPriority( UT_DCMI_IRQ_PRIO ) );
#else
    TEST_IGNORE_MESSAGE( "No DCMI peripheral on this line" );
#endif /* DCMI */
}


/**
 * \brief   Dcmi_Get_IrqPriority() rejects a null output pointer.
 *
 * \par Expected results
 * - DCMI_REQUEST_ERROR is returned.
 */
void Ut_Dcmi_Get_IrqPriority_NullPtr_ReturnsError( void )
{
#if defined(DCMI)
    UtDcmi_Force_Initialized( DCMI_STATE_IDLE, DCMI_FUNCTION_INACTIVE );
#endif /* DCMI */

    TEST_ASSERT_EQUAL( DCMI_REQUEST_ERROR, Dcmi_Get_IrqPriority( DCMI_NULL_PTR ) );
}

/* ===================================== PINS ===================================== */

/**
 * \brief   Dcmi_InitPixClkGpio() rejects the "unused" pin item.
 *
 * \par Expected results
 * - DCMI_REQUEST_ERROR is returned.
 */
void Ut_Dcmi_InitPixClkGpio_Unused_ReturnsError( void )
{
    TEST_ASSERT_EQUAL( DCMI_REQUEST_ERROR, Dcmi_InitPixClkGpio( DCMI_PIXCLK_PIN_UNUSED ) );
}


/**
 * \brief   Dcmi_InitSyncGpio() rejects the request when either pin is "unused".
 *
 * \par Expected results
 * - DCMI_REQUEST_ERROR is returned.
 */
void Ut_Dcmi_InitSyncGpio_OneUnused_ReturnsError( void )
{
    TEST_ASSERT_EQUAL( DCMI_REQUEST_ERROR,
                       Dcmi_InitSyncGpio( DCMI_HSYNC_PIN_UNUSED, DCMI_VSYNC_PIN_UNUSED ) );
}


/**
 * \brief   Dcmi_InitDataGpio() rejects the "unused" pin item.
 *
 * \par Expected results
 * - DCMI_REQUEST_ERROR is returned.
 */
void Ut_Dcmi_InitDataGpio_Unused_ReturnsError( void )
{
    TEST_ASSERT_EQUAL( DCMI_REQUEST_ERROR, Dcmi_InitDataGpio( DCMI_DATA_PIN_UNUSED ) );
}

/* ========================= DMA BLOCK COMPUTATION (PURE LOGIC) =================== */
/* DcmiPriv_ComputeDmaBlocks is pure logic (no register, no mock), see the file header of
 * Dcmi.c for why the module needs it (frame larger than one Gpdma block). A mutation check was
 * run by hand (changing "- 1u" to "+ 0u" in the repeated-block branch of the real function made
 * Ut_Dcmi_ComputeDmaBlocks_AboveBlockMax_RepeatsWordBlock fail as expected, then the function was
 * restored) - this is reported instead of being left in the source, per the agent rules. */

/**
 * \brief   DcmiPriv_ComputeDmaBlocks() rejects a null output pointer.
 *
 * \par Expected results
 * - DCMI_REQUEST_ERROR is returned.
 */
void Ut_Dcmi_ComputeDmaBlocks_NullPtr_ReturnsError( void )
{
#if defined(DCMI)
    uint32_t blockSize = 0u;
    uint32_t blockRepeatCnt = 0u;

    TEST_ASSERT_EQUAL( DCMI_REQUEST_ERROR,
                       DcmiPriv_ComputeDmaBlocks( UT_DCMI_DMA_BLOCK_SIZE, DCMI_NULL_PTR, &blockRepeatCnt ) );
    TEST_ASSERT_EQUAL( DCMI_REQUEST_ERROR,
                       DcmiPriv_ComputeDmaBlocks( UT_DCMI_DMA_BLOCK_SIZE, &blockSize, DCMI_NULL_PTR ) );
#else
    /* No DCMI peripheral on this line: the private helper does not exist */
    TEST_IGNORE_MESSAGE( "No DCMI peripheral on this line" );
#endif /* DCMI */
}


/**
 * \brief   DcmiPriv_ComputeDmaBlocks() rejects a zero size and a size that is not a multiple of 4.
 *
 * \par Expected results
 * - DCMI_REQUEST_ERROR is returned for both.
 */
void Ut_Dcmi_ComputeDmaBlocks_InvalidSize_ReturnsError( void )
{
#if defined(DCMI)
    uint32_t blockSize = 0u;
    uint32_t blockRepeatCnt = 0u;

    TEST_ASSERT_EQUAL( DCMI_REQUEST_ERROR, DcmiPriv_ComputeDmaBlocks( 0u, &blockSize, &blockRepeatCnt ) );
    TEST_ASSERT_EQUAL( DCMI_REQUEST_ERROR,
                       DcmiPriv_ComputeDmaBlocks( UT_DCMI_DMA_SIZE_NOT_WORD_ALIGNED, &blockSize, &blockRepeatCnt ) );
#else
    /* No DCMI peripheral on this line: the private helper does not exist */
    TEST_IGNORE_MESSAGE( "No DCMI peripheral on this line" );
#endif /* DCMI */
}


/**
 * \brief   DcmiPriv_ComputeDmaBlocks() at or below DCMI_DMA_BLOCK_SIZE_MAX returns a single block
 *          (no repetition).
 *
 * \par Expected results
 * - DCMI_REQUEST_OK is returned, blockSize is the total size, blockRepeatCnt is 0.
 */
void Ut_Dcmi_ComputeDmaBlocks_AtBlockMax_ReturnsSingleBlock( void )
{
#if defined(DCMI)
    uint32_t blockSize = 0u;
    uint32_t blockRepeatCnt = UT_DCMI_DMA_BLOCK_REPEAT_SENTINEL;

    TEST_ASSERT_EQUAL( DCMI_REQUEST_OK,
                       DcmiPriv_ComputeDmaBlocks( ( DCMI_DMA_BLOCK_SIZE_MAX - UT_DCMI_DMA_BLOCK_SIZE_MAX_MARGIN ),
                                                  &blockSize,
                                                  &blockRepeatCnt ) );
    TEST_ASSERT_EQUAL_UINT32( ( DCMI_DMA_BLOCK_SIZE_MAX - UT_DCMI_DMA_BLOCK_SIZE_MAX_MARGIN ), blockSize );
    TEST_ASSERT_EQUAL_UINT32( 0u, blockRepeatCnt );
#else
    /* No DCMI peripheral on this line: the private helper does not exist */
    TEST_IGNORE_MESSAGE( "No DCMI peripheral on this line" );
#endif /* DCMI */
}


/**
 * \brief   DcmiPriv_ComputeDmaBlocks() above DCMI_DMA_BLOCK_SIZE_MAX repeats a 4-byte block
 *          (totalSize / 4) times (the mutation-checked branch, see the section header note).
 *
 * \par Expected results
 * - DCMI_REQUEST_OK is returned, blockSize is 4, blockRepeatCnt is (totalSize / 4) - 1.
 */
void Ut_Dcmi_ComputeDmaBlocks_AboveBlockMax_RepeatsWordBlock( void )
{
#if defined(DCMI)
    uint32_t              blockSize = 0u;
    uint32_t              blockRepeatCnt = 0u;
    /* 65536, a multiple of 4 */
    const dcmi_Size_t     totalSize = ( DCMI_DMA_BLOCK_SIZE_MAX + UT_DCMI_DMA_BLOCK_SIZE_MAX_STEP );

    TEST_ASSERT_EQUAL( DCMI_REQUEST_OK, DcmiPriv_ComputeDmaBlocks( totalSize, &blockSize, &blockRepeatCnt ) );
    TEST_ASSERT_EQUAL_UINT32( DCMI_WORD_SIZE_BYTE, blockSize );
    TEST_ASSERT_EQUAL_UINT32( ( ( totalSize / DCMI_WORD_SIZE_BYTE ) - 1u ), blockRepeatCnt );
#else
    /* No DCMI peripheral on this line: the private helper does not exist */
    TEST_IGNORE_MESSAGE( "No DCMI peripheral on this line" );
#endif /* DCMI */
}


/**
 * \brief   DcmiPriv_ComputeDmaBlocks() rejects a frame larger than DCMI_DMA_FRAME_SIZE_MAX (the
 *          gpdma_BlockRep_t range would overflow).
 *
 * \par Expected results
 * - DCMI_REQUEST_ERROR is returned.
 */
void Ut_Dcmi_ComputeDmaBlocks_AboveFrameMax_ReturnsError( void )
{
#if defined(DCMI)
    uint32_t blockSize = 0u;
    uint32_t blockRepeatCnt = 0u;

    TEST_ASSERT_EQUAL( DCMI_REQUEST_ERROR,
                       DcmiPriv_ComputeDmaBlocks( ( DCMI_DMA_FRAME_SIZE_MAX + UT_DCMI_DMA_FRAME_SIZE_MAX_STEP ),
                                                  &blockSize,
                                                  &blockRepeatCnt ) );
#else
    /* No DCMI peripheral on this line: the private helper does not exist */
    TEST_IGNORE_MESSAGE( "No DCMI peripheral on this line" );
#endif /* DCMI */
}

/* ===================== LINES WITHOUT THE DCMI PERIPHERAL ====================== */

/**
 * \brief   Peripheral check of every public function on a line without the DCMI peripheral
 *          (STM32H503, STM32H543, STM32H553).
 *
 * \par Expected results
 * - Every function returns DCMI_REQUEST_ERROR and leaves its output parameter unchanged.
 * - Dcmi_Task does nothing, Dcmi_Get_ModuleVersion still returns the version of the module.
 */
void Ut_Dcmi_NoPeripheral_AllFunctions_ReturnError( void )
{
#if !defined(DCMI)
    dcmi_Config_t          config       = { DCMI_DATA_WIDTH_8 };
    dcmi_Pixel_t           buffer[ UT_DCMI_BUFFER_SIZE_BYTE ];
    dcmi_CaptureState_t    captureState = DCMI_STATE_ERROR;
    dcmi_FrameCnt_t        frameCount   = UT_DCMI_UNTOUCHED_VALUE;
    dcmi_Size_t            frameSize    = UT_DCMI_UNTOUCHED_VALUE;
    dcmi_IrqPrio_t         irqPriority  = UT_DCMI_IRQ_PRIO;
    const dcmi_Window_t    window       = { 0u };
    const dcmi_Selection_t selection    = { DCMI_FRAME_RATE_ALL };
    const dcmi_SyncCodes_t syncCodes    = { 0u };
    dcmi_ModuleVersion_t   version      = Dcmi_Get_ModuleVersion();

    TEST_ASSERT_EQUAL_UINT8( 1u, version.Major );
    TEST_ASSERT_EQUAL( DCMI_REQUEST_ERROR, Dcmi_Get_DefaultConfig( &config ) );
    TEST_ASSERT_EQUAL( DCMI_DATA_WIDTH_8, config.DataWidth );
    TEST_ASSERT_EQUAL( DCMI_REQUEST_ERROR, Dcmi_Init( &config ) );
    TEST_ASSERT_EQUAL( DCMI_REQUEST_ERROR, Dcmi_Deinit() );
    Dcmi_Task();
    TEST_ASSERT_EQUAL( DCMI_REQUEST_ERROR, Dcmi_Set_CaptureStart( buffer, UT_DCMI_BUFFER_SIZE_BYTE ) );
    TEST_ASSERT_EQUAL( DCMI_REQUEST_ERROR, Dcmi_Set_CaptureStop() );
    TEST_ASSERT_EQUAL( DCMI_REQUEST_ERROR, Dcmi_Set_CaptureAbort() );
    TEST_ASSERT_EQUAL( DCMI_REQUEST_ERROR, Dcmi_Get_CaptureState( &captureState ) );
    TEST_ASSERT_EQUAL( DCMI_STATE_ERROR, captureState );
    TEST_ASSERT_EQUAL( DCMI_REQUEST_ERROR, Dcmi_Get_FrameCount( &frameCount ) );
    TEST_ASSERT_EQUAL_UINT32( UT_DCMI_UNTOUCHED_VALUE, frameCount );
    TEST_ASSERT_EQUAL( DCMI_REQUEST_ERROR, Dcmi_Set_CropWindow( &window ) );
    TEST_ASSERT_EQUAL( DCMI_REQUEST_ERROR, Dcmi_Set_Selection( &selection ) );
    TEST_ASSERT_EQUAL( DCMI_REQUEST_ERROR, Dcmi_Get_FrameSize( &frameSize ) );
    TEST_ASSERT_EQUAL_UINT32( UT_DCMI_UNTOUCHED_VALUE, frameSize );
    TEST_ASSERT_EQUAL( DCMI_REQUEST_ERROR, Dcmi_Set_EmbeddedSyncCodes( &syncCodes ) );
    TEST_ASSERT_EQUAL( DCMI_REQUEST_ERROR, Dcmi_Set_IrqPriority( UT_DCMI_IRQ_PRIO ) );
    TEST_ASSERT_EQUAL( DCMI_REQUEST_ERROR, Dcmi_Get_IrqPriority( &irqPriority ) );
    TEST_ASSERT_EQUAL_UINT32( UT_DCMI_IRQ_PRIO, irqPriority );
    TEST_ASSERT_EQUAL( DCMI_REQUEST_ERROR, Dcmi_InitPixClkGpio( DCMI_PIXCLK_PIN_UNUSED ) );
    TEST_ASSERT_EQUAL( DCMI_REQUEST_ERROR, Dcmi_InitSyncGpio( DCMI_HSYNC_PIN_UNUSED, DCMI_VSYNC_PIN_UNUSED ) );
    TEST_ASSERT_EQUAL( DCMI_REQUEST_ERROR, Dcmi_InitDataGpio( DCMI_DATA_PIN_UNUSED ) );
#else
    TEST_IGNORE_MESSAGE( "MCU with DCMI" );
#endif /* DCMI */
}

/* =========================== LOCAL HELPER FUNCTIONS ============================ */

#if defined(DCMI)

/**
 * \brief Minimal valid configuration: embedded 8-bit synchronization (no HSYNC/VSYNC pin, no
 *        data pin needed by the parameter checks of Dcmi_Init), one pixel clock pin.
 */
static dcmi_Config_t UtDcmi_Get_MinimalConfig( void )
{
    dcmi_Config_t config;

    (void)Dcmi_Get_DefaultConfig( &config );
    config.SyncMode    = DCMI_SYNC_EMBEDDED;
    config.DataWidth   = DCMI_DATA_WIDTH_8;
    config.IrqPriority = UT_DCMI_IRQ_PRIO;
    config.PixClkPin   = DCMI_PIXCLK_PIN_PA6;

    return ( config );
}


/**
 * \brief Forces the single Dcmi instance into the initialized state through
 *        \ref DcmiPriv_Get_Instance, without calling Dcmi_Init (see the file header note).
 *
 * \param captureState [in] : Capture state to force.
 * \param cropActive   [in] : Crop feature state to force.
 */
static void UtDcmi_Force_Initialized( dcmi_CaptureState_t captureState, dcmi_FunctionState_t cropActive )
{
    dcmi_InstanceData_t * instance = DCMI_NULL_PTR;

    TEST_ASSERT_EQUAL( DCMI_REQUEST_OK, DcmiPriv_Get_Instance( &instance ) );

    instance->Initialized       = DCMI_FUNCTION_ACTIVE;
    instance->CaptureState      = captureState;
    instance->FrameCount        = 0u;
    instance->CropActive        = cropActive;
    instance->Window.FirstLine  = 0u;
    instance->Window.FirstPixel = 0u;
    instance->Window.LineCnt    = 0u;
    instance->Window.PixelCnt   = 0u;
    instance->Config.Format     = DCMI_FORMAT_RAW;
    instance->Config.DataWidth  = DCMI_DATA_WIDTH_8;
    instance->Config.SyncMode   = DCMI_SYNC_HARDWARE;
    instance->Config.IrqPriority = UT_DCMI_IRQ_PRIO;
    instance->Config.FrameCallback = DCMI_NULL_PTR;
    instance->Config.ErrorCallback = DCMI_NULL_PTR;
}

#endif /* DCMI */
