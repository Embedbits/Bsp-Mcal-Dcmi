/**
 * \author Mr.Nobody
 * \defgroup Dcmi Dcmi
 * \brief Dcmi module
 */

/**
 * \author Mr.Nobody
 * \file Dcmi_Types.h
 * \ingroup Dcmi
 * \brief Dcmi module global types definition
 *
 * This file contains the types definitions used across the module and are
 * available for other modules through the Port file. STM32H5 has one DCMI
 * instance on the lines H523/H533/H562/H563/H573/H5E4/H5E5/H5F4/H5F5 and no
 * DCMI at all on H503/H543/H553 (see the module README availability table;
 * every public function of the module then only returns DCMI_REQUEST_ERROR).
 *
 */

#ifndef DCMI_DCMI_TYPES_H
#define DCMI_DCMI_TYPES_H
/* ============================== INCLUDES ================================== */
#include "stdint.h"                         /* Module types definition        */
#include "Gpio_Port.h"                      /* GPIO Port module definitions   */
#include "Nvic_Port.h"                      /* NVIC Port module definitions   */
#include "Gpdma_Port.h"                     /* GPDMA Port module definitions  */
/* ========================== SYMBOLIC CONSTANTS ============================ */

/** Null pointer definition */
#define DCMI_NULL_PTR                          ( ( void* ) 0u )

/** Count of data pins usable at the maximum (14-bit) bus width */
#define DCMI_DATA_PIN_CNT                       ( 14u )

/** Byte count of a captured 32-bit data word (DCMI_DR), the frame size unit without crop */
#define DCMI_WORD_SIZE_BYTE                     ( 4u )

/** Port identification bit offset in an encoded pin value */
#define DCMI_BIT_MASK_PORT_BIT_OFFSET           ( 10u )

/** Pin identification bit offset in an encoded pin value */
#define DCMI_BIT_MASK_PIN_BIT_OFFSET            ( 5u )

/** Alternate function identification bit offset in an encoded pin value */
#define DCMI_BIT_MASK_AF_BIT_OFFSET             ( 0u )

/** Data line number identification bit offset in an encoded data pin value */
#define DCMI_BIT_MASK_LINE_BIT_OFFSET           ( 15u )

/* ========================== EXPORTED MACROS ================================ */

/** Encode a single-purpose (PIXCLK / HSYNC / VSYNC) pin configuration into a single bit-mask */
#define DCMI_PIN_BIT_MASK_ENCODE( PORT_ID, PIN_ID, AF_ID ) \
            ( ( (uint32_t)(PORT_ID) << DCMI_BIT_MASK_PORT_BIT_OFFSET ) | \
              ( (uint32_t)(PIN_ID)  << DCMI_BIT_MASK_PIN_BIT_OFFSET  ) | \
              ( (uint32_t)(AF_ID)   << DCMI_BIT_MASK_AF_BIT_OFFSET   )   )

/** Encode a data line pin configuration (line number D0..D13) into a single bit-mask */
#define DCMI_DATA_PIN_BIT_MASK_ENCODE( LINE_NO, PORT_ID, PIN_ID, AF_ID ) \
            ( ( (uint32_t)(LINE_NO)  << DCMI_BIT_MASK_LINE_BIT_OFFSET ) | \
              ( (uint32_t)(PORT_ID) << DCMI_BIT_MASK_PORT_BIT_OFFSET  ) | \
              ( (uint32_t)(PIN_ID)  << DCMI_BIT_MASK_PIN_BIT_OFFSET   ) | \
              ( (uint32_t)(AF_ID)   << DCMI_BIT_MASK_AF_BIT_OFFSET    )   )

/** Extract the port identification from an encoded pin value */
#define DCMI_BIT_MASK_DECODE_PORT( CODED_VAL ) \
            ( ( (uint32_t)(CODED_VAL) >> DCMI_BIT_MASK_PORT_BIT_OFFSET ) & 0x1Fu )

/** Extract the pin identification from an encoded pin value */
#define DCMI_BIT_MASK_DECODE_PIN( CODED_VAL ) \
            ( ( (uint32_t)(CODED_VAL) >> DCMI_BIT_MASK_PIN_BIT_OFFSET ) & 0x1Fu )

/** Extract the alternate function identification from an encoded pin value */
#define DCMI_BIT_MASK_DECODE_AF( CODED_VAL ) \
            ( ( (uint32_t)(CODED_VAL) >> DCMI_BIT_MASK_AF_BIT_OFFSET ) & 0x1Fu )

/* ============================== TYPEDEFS ================================== */

/** Type signaling major version of SW module */
typedef uint8_t dcmi_MajorVersion_t;

/** Type signaling minor version of SW module */
typedef uint8_t dcmi_MinorVersion_t;

/** Type signaling patch version of SW module */
typedef uint8_t dcmi_PatchVersion_t;

/** Module version */
typedef struct
{
    dcmi_MajorVersion_t Major; /**< Major version number */
    dcmi_MinorVersion_t Minor; /**< Minor version number */
    dcmi_PatchVersion_t Patch; /**< Patch version number */
}   dcmi_ModuleVersion_t;

/** Result of a module request */
typedef enum
{
    DCMI_REQUEST_OK    = 0u,                /**< The request succeeded */
    DCMI_REQUEST_ERROR                      /**< The request failed (bad parameter, bad state, hardware timeout) */
}   dcmi_RequestState_t;

/** Generic active / inactive function state of the module */
typedef enum
{
    DCMI_FUNCTION_INACTIVE = 0u,            /**< The function is inactive */
    DCMI_FUNCTION_ACTIVE                    /**< The function is active */
}   dcmi_FunctionState_t;

/** Generic active / inactive flag state of the module */
typedef enum
{
    DCMI_FLAG_INACTIVE = 0u,                /**< The flag is not set */
    DCMI_FLAG_ACTIVE                        /**< The flag is set */
}   dcmi_FlagState_t;

/* ========================= PIN ENUMERATIONS, EARLY ========================= */
/* Dcmi_Pins.h uses the bit-mask encode macros defined just above, and its items
 * (dcmi_PixClkPin_t, dcmi_HsyncPin_t, dcmi_VsyncPin_t, dcmi_DataPin_t) are used by
 * dcmi_Config_t below, so it is included here rather than at the end of this file. */
#include "Dcmi_Pins.h"                       /* Generated pin enumerations     */

/** Parallel data bus width */
typedef enum
{
    DCMI_DATA_WIDTH_8 = 0u,                 /**< 8-bit bus (DCMI_D0..D7)   */
    DCMI_DATA_WIDTH_10,                     /**< 10-bit bus (DCMI_D0..D9)  */
    DCMI_DATA_WIDTH_12,                     /**< 12-bit bus (DCMI_D0..D11) */
    DCMI_DATA_WIDTH_14,                     /**< 14-bit bus (DCMI_D0..D13) */
    DCMI_DATA_WIDTH_CNT                     /**< Number of data width items */
}   dcmi_DataWidth_t;

/** Synchronization mode of the data flow */
typedef enum
{
    DCMI_SYNC_HARDWARE = 0u,                /**< DCMI_HSYNC / DCMI_VSYNC pins */
    DCMI_SYNC_EMBEDDED,                     /**< Synchronization codes embedded in the 8-bit data flow */
    DCMI_SYNC_CNT                           /**< Number of synchronization mode items */
}   dcmi_SyncMode_t;

/** Capture mode */
typedef enum
{
    DCMI_CAPTURE_CONTINUOUS = 0u,           /**< Frame after frame until Dcmi_Set_CaptureStop */
    DCMI_CAPTURE_SNAPSHOT,                  /**< One frame, the capture stops by itself */
    DCMI_CAPTURE_MODE_CNT                   /**< Number of capture mode items */
}   dcmi_CaptureMode_t;

/** Pixel format (descriptive, drives the JPEG bit only - the other formats differ only in how
 *  the application interprets the raster frame buffer, see the module README) */
typedef enum
{
    DCMI_FORMAT_RAW = 0u,                   /**< 8/10/12/14-bit monochrome or raw Bayer */
    DCMI_FORMAT_YCBCR422,                   /**< YCbCr 4:2:2 progressive video */
    DCMI_FORMAT_RGB565,                     /**< RGB565 progressive video */
    DCMI_FORMAT_MONOCHROME,                 /**< 8-bit monochrome (or YCbCr Y extraction) */
    DCMI_FORMAT_JPEG,                       /**< Compressed data (sets the JPEG bit) */
    DCMI_FORMAT_CNT                         /**< Number of format items */
}   dcmi_Format_t;

/** Frame buffer handling mode, forwarded to the Gpdma transfer list execution mode */
typedef enum
{
    DCMI_BUFFER_LINEAR = 0u,                /**< One frame, the DMA stops after it */
    DCMI_BUFFER_CIRCULAR,                   /**< The DMA restarts the same buffer at every frame */
    DCMI_BUFFER_MODE_CNT                    /**< Number of buffer mode items */
}   dcmi_BufferMode_t;

/** Active edge of the pixel clock */
typedef enum
{
    DCMI_PIXCLK_FALLING = 0u,               /**< Data captured on the falling edge (reset value) */
    DCMI_PIXCLK_RISING,                     /**< Data captured on the rising edge */
    DCMI_PIXCLK_POLARITY_CNT                /**< Number of pixel clock polarity items */
}   dcmi_PixClkPolarity_t;

/** Active level of a hardware synchronization signal (DCMI_HSYNC / DCMI_VSYNC) */
typedef enum
{
    DCMI_SYNC_ACTIVE_LOW = 0u,              /**< The signal is active low (reset value) */
    DCMI_SYNC_ACTIVE_HIGH,                  /**< The signal is active high */
    DCMI_SYNC_POLARITY_CNT                  /**< Number of synchronization polarity items */
}   dcmi_SyncPolarity_t;

/** Frame capture rate control (continuous grab mode only, DCMI_CR.FCRC) */
typedef enum
{
    DCMI_FRAME_RATE_ALL = 0u,               /**< Every frame is captured */
    DCMI_FRAME_RATE_HALF,                   /**< Every other frame is captured */
    DCMI_FRAME_RATE_QUARTER,                /**< One frame out of four is captured */
    DCMI_FRAME_RATE_CNT                     /**< Number of frame rate items */
}   dcmi_FrameRateDiv_t;

/** Byte selection mode (8-bit data path only, DCMI_CR.BSM) */
typedef enum
{
    DCMI_BYTE_SELECT_ALL = 0u,              /**< Every received byte is captured */
    DCMI_BYTE_SELECT_EVERY_OTHER,           /**< Every other byte is captured */
    DCMI_BYTE_SELECT_ONE_OF_FOUR,           /**< One byte out of four is captured */
    DCMI_BYTE_SELECT_TWO_OF_FOUR,           /**< Two bytes out of four are captured */
    DCMI_BYTE_SELECT_CNT                    /**< Number of byte selection items */
}   dcmi_ByteSelectMode_t;

/** Capture state machine */
typedef enum
{
    DCMI_STATE_IDLE = 0u,                   /**< No capture requested */
    DCMI_STATE_WAITING_FRAME,               /**< Capture armed, waiting for the start of a frame */
    DCMI_STATE_CAPTURING,                   /**< A frame (or the continuous stream) is being captured */
    DCMI_STATE_ERROR,                       /**< The last capture ended with an error */
    DCMI_STATE_CNT                          /**< Number of capture state items */
}   dcmi_CaptureState_t;

/** Error bit mask type reported to \ref dcmi_ErrorCallback_t */
typedef uint32_t dcmi_ErrorMask_t;

/** Overrun error: the DMA did not take the data in time, the FIFO is corrupted */
#define DCMI_ERROR_OVERRUN                      ( 0x00000001u )

/** Synchronization error: the embedded codes were not received in the expected order */
#define DCMI_ERROR_SYNC                          ( 0x00000002u )

/** DMA transfer error reported by Gpdma (see Gpdma_Port.h GPDMA_ERROR_*) */
#define DCMI_ERROR_DMA                           ( 0x00000004u )

/** Frame byte count type */
typedef uint32_t dcmi_Size_t;

/** Count of the frames captured without error */
typedef uint32_t dcmi_FrameCnt_t;

/** Line number in the frame (first line of the crop window, DCMI_CWSTRT.VST) */
typedef uint16_t dcmi_LineIdx_t;

/** Pixel clock count of the line start offset (first pixel of the crop window, HOFFCNT) */
typedef uint16_t dcmi_PixelIdx_t;

/** Count of captured lines (DCMI_CWSIZE.VLINE) */
typedef uint16_t dcmi_LineCnt_t;

/** Count of captured pixel clocks per line (DCMI_CWSIZE.CAPCNT) */
typedef uint16_t dcmi_PixelCnt_t;

/** Code byte of an embedded synchronization delimiter (DCMI_ESCR) */
typedef uint8_t dcmi_SyncCode_t;

/** Unmask byte of an embedded synchronization delimiter (DCMI_ESUR) */
typedef uint8_t dcmi_SyncUnmask_t;

/** Captured pixel raw storage element type (the frame buffer is a raster of 32-bit words,
 *  see the module README - exposed as bytes so one buffer fits every pixel format) */
typedef uint8_t dcmi_Pixel_t;

/** Interrupt priority type of the instance (forwarded to Nvic) */
typedef nvic_IrqPrio_t dcmi_IrqPrio_t;

/** DMA peripheral identification type (forwarded to Gpdma) */
typedef gpdma_PeriphId_t dcmi_DmaPeriphId_t;

/** DMA channel identification type (forwarded to Gpdma) */
typedef gpdma_ChannelId_t dcmi_DmaChannelId_t;

/** DMA channel priority type (forwarded to Gpdma) */
typedef gpdma_Priority_t dcmi_DmaPriority_t;

/** Crop / full-frame window (first pixel and line, 0 based, pixel/line count), used by
 *  \ref Dcmi_Set_CropWindow and returned by \ref Dcmi_Get_FrameSize */
typedef struct
{
    dcmi_LineIdx_t  FirstLine;  /**< First captured line (DCMI_CWSTRT.VST)        */
    dcmi_PixelIdx_t FirstPixel; /**< Pixel clocks skipped on the line (HOFFCNT)   */
    dcmi_LineCnt_t  LineCnt;    /**< Number of captured lines (DCMI_CWSIZE.VLINE) */
    dcmi_PixelCnt_t PixelCnt;   /**< Number of captured pixel clocks (CAPCNT)     */
}   dcmi_Window_t;

/** Byte / line selection (resolution and frame rate reduction), used by \ref Dcmi_Set_Selection */
typedef struct
{
    dcmi_FrameRateDiv_t   FrameRateDiv;    /**< Continuous mode frame capture rate (FCRC) */
    dcmi_ByteSelectMode_t ByteSelectMode;  /**< Byte selection mode, 8-bit bus only (BSM) */
    dcmi_FunctionState_t  ByteSelectStart; /**< Odd/even byte select start (OEBS)         */
    dcmi_FunctionState_t  LineSelectMode;  /**< Capture one line out of two (LSM)         */
    dcmi_FunctionState_t  LineSelectStart; /**< Odd/even line select start (OELS)         */
}   dcmi_Selection_t;

/** Embedded synchronization codes (8-bit bus, \ref DCMI_SYNC_EMBEDDED only), used by
 *  \ref Dcmi_Set_EmbeddedSyncCodes. Every code / unmask byte is compared as 0xFF0000XY. */
typedef struct
{
    dcmi_SyncCode_t   FrameStartCode;   /**< Frame start delimiter code (ESCR.FSC)   */
    dcmi_SyncCode_t   FrameEndCode;     /**< Frame end delimiter code (ESCR.FEC)     */
    dcmi_SyncCode_t   LineStartCode;    /**< Line start delimiter code (ESCR.LSC)    */
    dcmi_SyncCode_t   LineEndCode;      /**< Line end delimiter code (ESCR.LEC)      */
    dcmi_SyncUnmask_t FrameStartUnmask; /**< Frame start delimiter unmask (ESUR.FSU) */
    dcmi_SyncUnmask_t FrameEndUnmask;   /**< Frame end delimiter unmask (ESUR.FEU)   */
    dcmi_SyncUnmask_t LineStartUnmask;  /**< Line start delimiter unmask (ESUR.LSU)  */
    dcmi_SyncUnmask_t LineEndUnmask;    /**< Line end delimiter unmask (ESUR.LEU)    */
}   dcmi_SyncCodes_t;

/** Callback reporting the end of a frame (or of the cropped window) without error */
typedef void ( *dcmi_FrameCallback_t )( void );

/** Callback reporting the end of a line (DCMI_HSYNC transition to active) */
typedef void ( *dcmi_LineCallback_t )( void );

/** Callback reporting a DCMI_VSYNC transition to active (start of a frame) */
typedef void ( *dcmi_VsyncCallback_t )( void );

/** Callback reporting a capture error, see DCMI_ERROR_* for errorMask */
typedef void ( *dcmi_ErrorCallback_t )( dcmi_ErrorMask_t errorMask );

/** Configuration structure of the DCMI instance, see Dcmi_Init / Dcmi_Get_DefaultConfig */
typedef struct
{
    dcmi_DataWidth_t      DataWidth;                    /**< Parallel bus width                     */
    dcmi_SyncMode_t       SyncMode;                     /**< Hardware or embedded synchronization   */
    dcmi_PixClkPolarity_t PixClkPolarity;               /**< Pixel clock capture edge               */
    dcmi_SyncPolarity_t   HsyncPolarity;                /**< DCMI_HSYNC active level (hw sync only) */
    dcmi_SyncPolarity_t   VsyncPolarity;                /**< DCMI_VSYNC active level (hw sync only) */
    dcmi_Format_t         Format;                       /**< Pixel format (JPEG sets the JPEG bit)  */
    dcmi_CaptureMode_t    CaptureMode;                  /**< Continuous or snapshot capture         */
    dcmi_BufferMode_t     BufferMode;                   /**< Linear or circular frame buffer        */
    dcmi_IrqPrio_t        IrqPriority;                  /**< NVIC priority of the shared IRQ        */
    dcmi_DmaPeriphId_t    DmaPeriphId;                  /**< Gpdma peripheral of the frame transfer */
    dcmi_DmaChannelId_t   DmaChannelId;                 /**< Gpdma channel of the frame transfer    */
    dcmi_DmaPriority_t    DmaPriority;                  /**< Gpdma channel priority                 */
    dcmi_PixClkPin_t      PixClkPin;                    /**< DCMI_PIXCLK pin                        */
    dcmi_HsyncPin_t       HsyncPin;                     /**< DCMI_HSYNC pin (hardware sync only)    */
    dcmi_VsyncPin_t       VsyncPin;                     /**< DCMI_VSYNC pin (hardware sync only)    */
    dcmi_DataPin_t        DataPin[ DCMI_DATA_PIN_CNT ]; /**< D0..D13 pins, unused above DataWidth   */
    dcmi_FrameCallback_t  FrameCallback;                /**< May be DCMI_NULL_PTR                   */
    dcmi_LineCallback_t   LineCallback;                 /**< May be DCMI_NULL_PTR                   */
    dcmi_VsyncCallback_t  VsyncCallback;                /**< May be DCMI_NULL_PTR                   */
    dcmi_ErrorCallback_t  ErrorCallback;                /**< May be DCMI_NULL_PTR                   */
}   dcmi_Config_t;

#endif /* DCMI_DCMI_TYPES_H */
