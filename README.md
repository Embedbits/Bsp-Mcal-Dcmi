# DCMI Peripheral Driver

This module provides an abstraction layer for the **digital camera interface** (`DCMI`) of STM32 MCUs: the synchronous parallel
interface that receives the pixel stream of a CMOS camera sensor - the 8, 10, 12 or 14-bit data bus, the pixel clock, the hardware
(`HSYNC`, `VSYNC`) or the embedded synchronization, the continuous and the snapshot capture, the crop window and the selection of
the data, and the transfer of the frames into the memory by the DMA. It is a low-level MCAL module: it owns the DCMI hardware, the
application and the other modules never touch the registers.

The public interface (`Dcmi_Port.h`, `Dcmi_Types.h`) is **identical for all supported families**. The implementation
for a family lives in its own branch (`Dev/<Family>` for development, `Releases/<Family>` for the released versions), so
a module or an application written against the interface does not change when the MCU family changes.

This README is the landing page and the **specification of the interface** (master). The family branches contain their
own README with everything that is specific to the family: the instances of the device, their settings and the status of
the implementation.

> **Status: STM32H5 implemented (`Dev/STM32H5`).** Unit tests pass on all 12 STM32H5 lines, integration
> tests compile for the target but were not run on hardware (no board access). Deviations from this
> specification (frame-ready signalling through the Gpdma transfer complete interrupt instead of
> DCMI_RIS.FRAME_RIS, the large-frame DMA block repetition) are documented at the top of `Dcmi.c`.

---

## Intent and purpose

- A camera sensor sends a **stream of pixels with the timing signals**; the microcontroller must catch every pixel of every line at the
  pixel clock and put the frame into a buffer without the CPU. The DCMI packs the pixel bytes into 32-bit words, counts the lines and the
  frames, and raises the line, the frame and the error events. The module owns the whole chain: the interface settings, the DMA
  to the frame buffer, the start and the stop, the events.
- The user describes **the format of the camera** - the data width, the polarity of the pixel clock and of the synchronization signals,
  the embedded or the separate synchronization, the pixel format (YCbCr 4:2:2, RGB565, monochrome / raw Bayer, JPEG) - and **what to
  capture**: continuously, or a single frame (snapshot), the whole frame or a crop window, every pixel or every second one.
- The captured frame is a block of memory that the DMA fills: `Dcmi_Set_CaptureStart( frameBuffer, size )`. A frame larger than the limit of one
  DMA transfer is split into the linked transfers by the module, a frame buffer in the circular mode is a continuous video stream.
- The module hides the differences between the families: the data width, the JPEG mode, the byte and the line selection, the crop,
  the DMA structure and the interrupts differ, the interface stays the same.

What the module is **not**: it does not configure the camera sensor (the clock `MCLK`, the I2C / SCCB registers - a camera driver
on the top of the I2c module), it does not convert the pixel formats and it does not display (the Ltdc and the Dma2D modules).

---

## Concept

| Item | Meaning |
|---|---|
| **Bus** | The data width (`DCMI_DATA_WIDTH_8` / `10` / `12` / `14`), the pixel clock edge, the polarity of `HSYNC` and `VSYNC`. |
| **Synchronization** | `DCMI_SYNC_HARDWARE` (the `HSYNC` and `VSYNC` pins) or `DCMI_SYNC_EMBEDDED` (the frame / line start and end codes in the data, programmable codes and masks). |
| **Capture mode** | `DCMI_CAPTURE_CONTINUOUS` (frame after frame until the stop) or `DCMI_CAPTURE_SNAPSHOT` (one frame). |
| **Selection** | The crop window (the first line, the first pixel, the number of lines and pixels), the selection of every second byte / line (frame rate and resolution reduction). |
| **Format** | `DCMI_FORMAT_RAW`, `YCBCR422`, `RGB565`, `MONOCHROME`, `JPEG` - the module sets the packing and the JPEG mode of the hardware. |
| **Frame buffer** | The memory that receives the frame, its size, the circular or one-shot use. |

Rules:

1. `Dcmi_Init` configures the bus, the synchronization, the format, the DMA and the interrupt and leaves the capture **stopped**. The
   configuration is changed only while the capture is stopped.
2. `Dcmi_Set_CaptureStart` arms the DMA and the interface; in the snapshot mode the capture ends after one frame, in the continuous mode it runs
   until `Dcmi_Set_CaptureStop`. The start is synchronized to the **beginning of a frame** (the hardware waits for the `VSYNC`).
3. The size of the buffer is checked against the frame size calculated from the crop window and the format; a buffer that is too small is rejected.
4. The events are reported by the callbacks: the frame complete (`FrameCallback`), the line complete (`LineCallback`), the `VSYNC` (`VsyncCallback`), the
   overrun (the DMA did not take the data in time), the synchronization error (a line of a wrong length with the embedded codes), the DMA error.
5. A stopped capture waits for the end of the current frame or aborts it at once (`Dcmi_Set_CaptureAbort`).
6. All parameters are checked before a register is written.

---

## Supported families

Every family gets its own story, its own tasks and its own branches `Dev/<Family>` and `Releases/<Family>` cut from this
master.

| Family | Branch | Interface | Status |
|---|---|---|---|
| STM32H5 | `Dev/STM32H5` | `DCMI` on H523/H533/H562/H563/H573/H5E4/H5E5/H5F4/H5F5 (missing on H503/H543/H553, confirmed from the CMSIS headers; every function checks `defined(DCMI)` and only returns `DCMI_REQUEST_ERROR` there): 8 / 10 / 12 / 14-bit bus, hardware and embedded synchronization, snapshot and continuous, crop, YCbCr 4:2:2, RGB565, monochrome / Bayer, JPEG (JPEG mode accepted by the interface, not covered by the test set, no JPEG source on the test board); shares the pins, the RCC enable and the Gpdma request with the `PSSI` (only one of them can be used) | implemented |
| STM32U5 | `Dev/STM32U5` | `DCMI` on the lines that have it | planned |
| STM32H7 | `Dev/STM32H7` | `DCMI` | planned |
| STM32F7 | `Dev/STM32F7` | `DCMI` | planned |
| STM32F4 | `Dev/STM32F4` | `DCMI` on the F407 / F417 / F427 / F429 / F437 / F439 / F446 / F469 / F479 lines | planned |
| STM32L4 | `Dev/STM32L4` | `DCMI` on the lines that have it | planned |
| STM32G4, STM32U3 | `Dev/STM32G4`, `Dev/STM32U3` | no DCMI on the device - every function checks the peripheral and returns `DCMI_REQUEST_ERROR` | planned |

A function without a meaning in a family stays in the interface, returns `DCMI_REQUEST_ERROR` and suppresses the unused
parameters (for example the 12 / 14-bit bus on a 10-bit interface, the JPEG mode of a device without it).

---

## Public interface in short (proposal)

| Group | Functions |
|---|---|
| Module management | `Dcmi_Get_ModuleVersion`, `Dcmi_Init`, `Dcmi_Deinit`, `Dcmi_Task`, `Dcmi_Get_DefaultConfig` |
| Capture | `Dcmi_Set_CaptureStart`, `Dcmi_Set_CaptureStop`, `Dcmi_Set_CaptureAbort`, `Dcmi_Get_CaptureState`, `Dcmi_Get_FrameCount` |
| Window and selection | `Dcmi_Set_CropWindow`, `Dcmi_Set_Selection`, `Dcmi_Get_FrameSize` |
| Synchronization | `Dcmi_Set_EmbeddedSyncCodes` |
| Interrupts | `Dcmi_Set_IrqPriority` / `Get`, the callbacks are a part of the configuration |
| Pins | `Dcmi_InitPixClkGpio`, `Dcmi_InitSyncGpio`, `Dcmi_InitDataGpio` |

```c
dcmi_ModuleVersion_t Dcmi_Get_ModuleVersion     ( void );

dcmi_RequestState_t  Dcmi_Init                  ( const dcmi_Config_t * const dcmiConfig );
dcmi_RequestState_t  Dcmi_Deinit                ( void );
void                 Dcmi_Task                  ( void );
dcmi_RequestState_t  Dcmi_Get_DefaultConfig     ( dcmi_Config_t * const dcmiConfig );

dcmi_RequestState_t  Dcmi_Set_CaptureStart      ( dcmi_Pixel_t * const frameBuffer, dcmi_Size_t bufferSize );
dcmi_RequestState_t  Dcmi_Set_CaptureStop       ( void );
dcmi_RequestState_t  Dcmi_Set_CaptureAbort      ( void );
dcmi_RequestState_t  Dcmi_Get_CaptureState      ( dcmi_CaptureState_t * const captureState );

dcmi_RequestState_t  Dcmi_Set_CropWindow        ( const dcmi_Window_t * const window );
dcmi_RequestState_t  Dcmi_Get_FrameSize         ( dcmi_Size_t * const frameSize );
dcmi_RequestState_t  Dcmi_Set_EmbeddedSyncCodes ( const dcmi_SyncCodes_t * const syncCodes );
```

Every function returns `DCMI_REQUEST_OK` / `DCMI_REQUEST_ERROR` and checks its parameters and the state of the module
before it touches a register.

| Type | Meaning |
|---|---|
| `dcmi_Config_t` | Bus width, edges and polarities, synchronization mode, format, capture mode, crop window, selection, DMA channel and priority, pins, callbacks, interrupt priority |
| `dcmi_Window_t` | The first pixel and line, the number of pixels and lines |
| `dcmi_CaptureState_t` | `DCMI_STATE_IDLE`, `DCMI_STATE_WAITING_FRAME`, `DCMI_STATE_CAPTURING`, `DCMI_STATE_ERROR` |
| Callbacks | `FrameCallback`, `LineCallback`, `VsyncCallback`, `ErrorCallback( errorMask )` |

---

## Usage

### A QVGA RGB565 snapshot

```c
#include "Dcmi_Port.h"

static dcmi_Pixel_t frame[ 320u * 240u * 2u ];                   /* the frame buffer in the DMA reachable RAM */

static void App_FrameDone( void ) { /* the frame is in the buffer: process or display it */ }

dcmi_Config_t dcmiConfig;

(void)Dcmi_Get_DefaultConfig( &dcmiConfig );

dcmiConfig.DataWidth     = DCMI_DATA_WIDTH_8;
dcmiConfig.SyncMode      = DCMI_SYNC_HARDWARE;
dcmiConfig.Format        = DCMI_FORMAT_RGB565;
dcmiConfig.CaptureMode   = DCMI_CAPTURE_SNAPSHOT;
dcmiConfig.FrameCallback = App_FrameDone;

(void)Dcmi_Init( &dcmiConfig );
/* the camera driver configures the sensor and starts its clock */
(void)Dcmi_Set_CaptureStart( frame, sizeof( frame ) );
```

### Continuous video into a circular buffer

```c
dcmiConfig.CaptureMode = DCMI_CAPTURE_CONTINUOUS;
dcmiConfig.BufferMode  = DCMI_BUFFER_CIRCULAR;
(void)Dcmi_Init( &dcmiConfig );
(void)Dcmi_Set_CaptureStart( videoBuffer, sizeof( videoBuffer ) );     /* FrameCallback at every frame */
```

---

## Behaviour and limits

- **Bandwidth.** The pixel clock can be tens of MHz; the DMA and the memory bus must keep up, otherwise the **overrun** is reported and the
  frame is damaged. A frame buffer in the fast internal RAM or a fast external memory is a must for the large resolutions.
- **Cache and the buffer.** The buffer must be in a DMA reachable and (with a data cache) non-cacheable memory, or invalidated by the application before it is read.
- **Frame size.** The size follows from the crop window and the format; a JPEG stream has a variable size (the buffer must be large enough
  for the worst case, the real length is the number of bytes received).
- **Shared pins.** The DCMI and the PSSI use the same pins; a product uses one of them.
- **Not part of the first version:** the control of the camera (a separate driver), the conversion of the pixel format, the
  double-buffer ping-pong policy beyond the circular buffer, the capture synchronized to an external trigger.

---

## Dependencies

- **Rcc** - peripheral clock and reset
- **Nvic** - interrupt
- **Gpio** - pixel clock, synchronization and data pins
- **Gpdma** - DMA transfer of the frame into the memory
- **Cache** - the data cache maintenance of the frame buffer on the families that have it
- **RAL (Register Abstraction Layer)** - used internally to access the registers

All mandatory RCC, NVIC, GPIO and DMA configurations are handled internally.

---

## Testing

- **Unit tests** (host, Unity + CMock + RegMem): the frame size calculation for the crop windows and the formats, the buffer size
  check, the configuration to register mapping for every option, the capture state machine (start, stop, abort, error), the
  splitting of a large frame into the DMA transfers, the callbacks.
- **Integration tests** (target, probe-rs): a frame generator (a timer driven pixel source or a test camera) captured in the snapshot and the
  continuous mode and compared with the pattern, the overrun with a slowed DMA, a real camera sensor.

---

## Interface conventions

The module follows the common rules of the MCAL layer, so that it behaves like the other modules:

- **Lists instead of free members.** Everything that the hardware restricts (the pins, the kernel clock sources, the DMA
  channels, the triggers and the other selectable sources) is a `typedef enum` list with exactly the items that are valid
  for the device line; a pin is an item `DCMI_<SIGNAL>_PIN_<INSTANCE>_P<port><pin>` and there is an `*_UNUSED` item. A choice
  that is not in the list cannot be written.
- **Own peripheral only.** The module calls only the registers / LL functions of its own peripheral. The clocks, the
  interrupts, the pins, the DMA and the EXTI lines go through the API of the Rcc, Nvic, Gpio, Gpdma and Exti modules; a
  missing function is added to that module, it is not bypassed.
- **Init verifies itself.** `Dcmi_Init` checks the result at the end; when it is not as required, the module is
  deinitialized and an error is returned. The waiting for the hardware is a loop on the register state with a timeout,
  never a blind delay.
- **Transfer modes.** Where the module moves data, the POLL (served by `Dcmi_Task`), ISR and DMA modes have the same
  configuration structure, the same request and the same callbacks (error, half transfer, transfer complete).
- **Flags and states are enums of the module** (`dcmi_FunctionState_t`, `dcmi_FlagState_t`), the limits and the constants are
  named, there are no magic numbers in the interface.
- **Every function stays in the interface.** A function without a meaning in a family, or on a device without the
  peripheral, keeps its signature, suppresses the unused parameters and returns `DCMI_REQUEST_ERROR`.
- **Documentation.** Every function and type has a Doxygen description, the main header has the `\defgroup`.

---

## Repository layout and branches

The master holds this README and the license. The family branches (`Dev/<Family>`, `Releases/<Family>`) hold:

| Path | Content |
|---|---|
| `README.md` | README of the family: the instances of the device, specifics, errata, status |
| `Dcmi_Port.h`, `Dcmi_Types.h` | Public interface (identical in all families) |
| `Dcmi*.c`, `Dcmi.h` | Implementation of the family |
| `CMakeLists.txt` | Library `Dcmi_Lib` of the EmBi platform |
| `Tests/UnitTests` | Unit tests (Unity + CMock + RegMem) |
| `Tests/IntegrationTests` | Integration tests on the target (probe-rs) |

A change of the public interface is made in all families.

---

## CMake integration

```cmake
target_link_libraries( User_Lib PRIVATE Dcmi_Lib )
```

1. Link `Dcmi_Lib` to your CMake library.
2. Include `Dcmi_Port.h` in your project.
3. Configure the module as needed for your hardware.

---

## License

This project is licensed under the **Creative Commons Attribution-NonCommercial 4.0 International (CC BY-NC 4.0)**.

You are free to use, modify, and share this work for **non-commercial purposes**, provided appropriate credit is given.

See [LICENSE.md](LICENSE.md) for full terms or visit [creativecommons.org/licenses/by-nc/4.0](https://creativecommons.org/licenses/by-nc/4.0/).

---

## Authors

- **Mr.Nobody** - [embedbits.com](https://embedbits.com)

Contributions are welcome! Please open a pull request.
