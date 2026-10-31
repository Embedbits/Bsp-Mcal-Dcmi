/**
 * \author Mr.Nobody
 * \file ItTest_Dcmi.c
 * \ingroup Dcmi
 * \brief Integration tests of the Dcmi module on target.
 *
 * Dcmi module runs on the MCU together with the real DCMI / GPIO / GPDMA / RCC / NVIC / CACHE
 * hardware. A bare Nucleo board carries no camera module, so these tests verify what a single
 * board without a camera can prove without the camera itself: the module initializes and
 * deinitializes without a fault (embedded 8-bit synchronization, so only the DCMI_PIXCLK pin is
 * needed - no HSYNC/VSYNC/data pin wiring is required by the configuration itself), the crop
 * window / selection / embedded synchronization code setters reject or accept a configuration
 * without a fault, and arming a capture with no pixel clock ever toggling (no camera connected)
 * leaves the module waiting for the start of a frame instead of faulting.
 *
 * Used resources:
 * - DCMI (shared RCC_PERIPH_DCMI_PSSI, see the module README "Shared pins" note - Pssi is not
 *   touched by this test set)
 * - GPIO PA6 (DCMI_PIXCLK, present on every STM32H5 line with DCMI) - not wired to a real
 *   camera, only the configuration and the waiting state are checked (no fault)
 *
 * \note On STM32H503 / STM32H543 / STM32H553 (no DCMI peripheral) every test is ignored
 *       (`TEST_IGNORE_MESSAGE`), see the module README availability table.
 * \note Compiled for the target but not run on hardware, see the module report.
 */

/* ============================= INCLUDES =================================== */
#include "unity.h"                          /* Unity testing framework        */
#include "IntegrationTesting.h"             /* Integration testing on target  */
#include "Dcmi_Port.h"                      /* Module under test              */
/* ============================= TYPEDEFS =================================== */

/* ======================= FORWARD DECLARATIONS ============================= */

static dcmi_RequestState_t ItDcmi_Get_MinimalConfig( dcmi_Config_t * const config );

/* ========================= SYMBOLIC CONSTANTS ============================= */

/** Interrupt priority used by the tests */
#define IT_DCMI_IRQ_PRIO                      ( 5u )

/** Crop window pixel count used by the JPEG-rejection test (word aligned, arbitrary) */
#define IT_DCMI_WINDOW_PIXEL_CNT               ( 4u )

/** Frame buffer used by the capture start test (a multiple of 4, no camera ever fills it) */
#define IT_DCMI_BUFFER_SIZE_BYTE              ( 64u )

/* ========================== LOCAL VARIABLES =============================== */

/** Frame buffer used by the capture start test (DMA reachable, static - not on the test stack) */
static dcmi_Pixel_t itDcmi_FrameBuffer[ IT_DCMI_BUFFER_SIZE_BYTE ];

/* ============================= TEST SETUP ================================= */

void setUp( void )
{
    /* No per-test state */
}


void tearDown( void )
{
    /* Peripheral state is cleared by system reset after the test case */
}

/* ========================== LOCAL FUNCTIONS ================================ */

/**
 * \brief Fills a default configuration with embedded 8-bit synchronization and the common
 *        Nucleo DCMI_PIXCLK pin (PA6) - no HSYNC/VSYNC/data pin is required by this mode.
 *
 * \param config [out]: Pointer to the configuration structure filled in place.
 *
 * \return DCMI_REQUEST_OK on success, DCMI_REQUEST_ERROR otherwise.
 */
static dcmi_RequestState_t ItDcmi_Get_MinimalConfig( dcmi_Config_t * const config )
{
    dcmi_RequestState_t retState = Dcmi_Get_DefaultConfig( config );

#if defined(DCMI)
    if( DCMI_REQUEST_OK == retState )
    {
        config->SyncMode    = DCMI_SYNC_EMBEDDED;
        config->DataWidth   = DCMI_DATA_WIDTH_8;
        config->IrqPriority = IT_DCMI_IRQ_PRIO;
        config->PixClkPin   = DCMI_PIXCLK_PIN_PA6;
    }
    else
    {
        /* Default configuration itself failed, nothing else to fill */
    }
#endif /* DCMI */

    return ( retState );
}

/* =============================== TESTS ==================================== */

/**
 * \brief The module version is reported without a fault.
 * \par Expected results
 * \ref Dcmi_Get_ModuleVersion returns major version 1.
 */
void It_Dcmi_Get_ModuleVersion_ReturnsVersion( void )
{
    dcmi_ModuleVersion_t version = Dcmi_Get_ModuleVersion();

    TEST_ASSERT_EQUAL_UINT8( 1u, version.Major );
}


/**
 * \brief The module initializes with embedded synchronization and the common Nucleo
 *        DCMI_PIXCLK pin and deinitializes, without a fault (no camera connected, so the pixel
 *        clock never toggles and the capture is never armed in this test).
 * \par Expected results
 * \ref Dcmi_Init returns \ref DCMI_REQUEST_OK, \ref Dcmi_Deinit returns \ref DCMI_REQUEST_OK.
 */
void It_Dcmi_Init_MinimalConfig_ReturnsOk( void )
{
#if defined(DCMI)
    dcmi_Config_t config;

    TEST_ASSERT_EQUAL( DCMI_REQUEST_OK, ItDcmi_Get_MinimalConfig( &config ) );
    TEST_ASSERT_EQUAL( DCMI_REQUEST_OK, Dcmi_Init( &config ) );
    TEST_ASSERT_EQUAL( DCMI_REQUEST_OK, Dcmi_Deinit() );
#else
    TEST_IGNORE_MESSAGE( "No DCMI peripheral on this line." );
#endif /* DCMI */
}


/**
 * \brief Arming a capture with no camera connected (pixel clock never toggles) leaves the
 *        module waiting for the start of a frame instead of faulting; aborting and
 *        deinitializing afterwards does not fault either.
 * \par Expected results
 * \ref Dcmi_Set_CaptureStart returns \ref DCMI_REQUEST_OK, the capture state is
 * \ref DCMI_STATE_WAITING_FRAME, \ref Dcmi_Set_CaptureAbort and \ref Dcmi_Deinit return
 * \ref DCMI_REQUEST_OK.
 */
void It_Dcmi_Set_CaptureStart_NoCameraConnected_WaitsWithoutFault( void )
{
#if defined(DCMI)
    dcmi_Config_t        config;
    dcmi_CaptureState_t   captureState = DCMI_STATE_ERROR;

    TEST_ASSERT_EQUAL( DCMI_REQUEST_OK, ItDcmi_Get_MinimalConfig( &config ) );
    TEST_ASSERT_EQUAL( DCMI_REQUEST_OK, Dcmi_Init( &config ) );

    TEST_ASSERT_EQUAL( DCMI_REQUEST_OK,
                        Dcmi_Set_CaptureStart( itDcmi_FrameBuffer, IT_DCMI_BUFFER_SIZE_BYTE ) );
    TEST_ASSERT_EQUAL( DCMI_REQUEST_OK, Dcmi_Get_CaptureState( &captureState ) );
    TEST_ASSERT_EQUAL( DCMI_STATE_WAITING_FRAME, captureState );

    TEST_ASSERT_EQUAL( DCMI_REQUEST_OK, Dcmi_Set_CaptureAbort() );
    TEST_ASSERT_EQUAL( DCMI_REQUEST_OK, Dcmi_Deinit() );
#else
    TEST_IGNORE_MESSAGE( "No DCMI peripheral on this line." );
#endif /* DCMI */
}


/**
 * \brief Setting the crop window while the JPEG format is selected is rejected without a fault
 *        (RM0481: crop can not be used in JPEG mode, see Dcmi_Set_CropWindow).
 * \par Expected results
 * \ref Dcmi_Set_CropWindow returns \ref DCMI_REQUEST_ERROR.
 */
void It_Dcmi_Set_CropWindow_JpegFormat_ReturnsErrorNoFault( void )
{
#if defined(DCMI)
    dcmi_Config_t   config;
    dcmi_Window_t     window = { .FirstLine = 0u, .FirstPixel = 0u,
                                   .LineCnt = 1u, .PixelCnt = IT_DCMI_WINDOW_PIXEL_CNT };

    TEST_ASSERT_EQUAL( DCMI_REQUEST_OK, ItDcmi_Get_MinimalConfig( &config ) );
    config.Format = DCMI_FORMAT_JPEG;
    TEST_ASSERT_EQUAL( DCMI_REQUEST_OK, Dcmi_Init( &config ) );

    TEST_ASSERT_EQUAL( DCMI_REQUEST_ERROR, Dcmi_Set_CropWindow( &window ) );

    (void)Dcmi_Deinit();
#else
    TEST_IGNORE_MESSAGE( "No DCMI peripheral on this line." );
#endif /* DCMI */
}


/**
 * \brief Setting the embedded synchronization codes on an embedded-sync / 8-bit configuration
 *        is accepted without a fault.
 * \par Expected results
 * \ref Dcmi_Set_EmbeddedSyncCodes returns \ref DCMI_REQUEST_OK.
 */
void It_Dcmi_Set_EmbeddedSyncCodes_EmbeddedSyncMode_ReturnsOkNoFault( void )
{
#if defined(DCMI)
    dcmi_Config_t     config;
    dcmi_SyncCodes_t   syncCodes = { 0 };

    TEST_ASSERT_EQUAL( DCMI_REQUEST_OK, ItDcmi_Get_MinimalConfig( &config ) );
    TEST_ASSERT_EQUAL( DCMI_REQUEST_OK, Dcmi_Init( &config ) );

    TEST_ASSERT_EQUAL( DCMI_REQUEST_OK, Dcmi_Set_EmbeddedSyncCodes( &syncCodes ) );

    (void)Dcmi_Deinit();
#else
    TEST_IGNORE_MESSAGE( "No DCMI peripheral on this line." );
#endif /* DCMI */
}
