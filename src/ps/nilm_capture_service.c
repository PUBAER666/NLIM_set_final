#include "nilm_capture_service.h"
#include "nilm_capture.h"
#include "xparameters.h"
#include "xil_io.h"
#include "xil_mmu.h"
#include "xil_printf.h"

#define GPIO_BASE XPAR_AXI_GPIO_0_BASEADDR
#define BRAM_BASE XPAR_AXI_BRAM_CTRL_0_BASEADDR
#define GPIO_DATA 0u
#define GPIO_TRI 4u
#define GPIO2_DATA 8u
#define GPIO2_TRI 12u

_Static_assert(sizeof(nilm_sample) == 8u, "RAW1 record size");
_Static_assert(XPAR_AXI_BRAM_CTRL_0_HIGHADDR - BRAM_BASE + 1u >=
               NILM_CAPTURE_SAMPLES * 8u, "BSP BRAM too small");

static nilm_capture capture;
static nilm_sample frame[NILM_CAPTURE_SAMPLES];
static int active;
static uint32_t timer_hz;

int nilm_capture_service_active(void)
{
    return active;
}

static uint32_t read_status(void *user)
{ (void)user; return Xil_In32(GPIO_BASE + GPIO2_DATA); }
static uint32_t read_control(void *user)
{ (void)user; return Xil_In32(GPIO_BASE + GPIO_DATA); }
static void write_control(void *user, uint32_t value)
{ (void)user; dsb(); Xil_Out32(GPIO_BASE + GPIO_DATA, value); dsb(); }
static uint32_t read_word(void *user, uint32_t offset)
{ (void)user; return Xil_In32(BRAM_BASE + offset); }
static uint32_t now_ms(void)
{
    uint32_t hi, lo, hi_after;
    uint64_t ticks;
    /* This BSP's XTime_GetTime uses a 32-bit SCU down-counter that sleep()
     * can reload. Use the independent Cortex-A9 64-bit global up-counter. */
    do {
        hi = Xil_In32(XPAR_GLOBAL_TIMER_BASEADDR + 4u);
        lo = Xil_In32(XPAR_GLOBAL_TIMER_BASEADDR);
        hi_after = Xil_In32(XPAR_GLOBAL_TIMER_BASEADDR + 4u);
    } while (hi != hi_after);
    ticks = ((uint64_t)hi << 32) | lo;
    return (uint32_t)((ticks / timer_hz) * 1000u +
                     ((ticks % timer_hz) * 1000u) / timer_hz);
}

void nilm_capture_service_init(void)
{
    const nilm_capture_io io = {
        0, read_status, read_control, write_control, read_word
    };
    uint32_t timer_control = Xil_In32(XPAR_GLOBAL_TIMER_BASEADDR + 8u);
    uint32_t prescale = ((timer_control >> 8) & 0xffu) + 1u;
    timer_hz = XPAR_CPU_CORE_CLOCK_FREQ_HZ / (2u * prescale);
    Xil_Out32(XPAR_GLOBAL_TIMER_BASEADDR + 8u, timer_control | 1u);
    dsb();
    /* These are dedicated 1 MiB sections in this hardware memory map.
     * Prevent speculative/cached reads of BRAM and the GPIO status registers. */
    Xil_SetTlbAttributes(BRAM_BASE, DEVICE_MEMORY);
    Xil_SetTlbAttributes(GPIO_BASE, DEVICE_MEMORY);
    Xil_Out32(GPIO_BASE + GPIO_TRI, 0u);
    Xil_Out32(GPIO_BASE + GPIO2_TRI, 0xffffffffu);
    dsb();
    active = nilm_capture_init(&capture, &io, frame, NILM_CAPTURE_SAMPLES) == 0;
    if (active) {
        (void)nilm_capture_start(&capture, now_ms());
        xil_printf("NILM: one-shot RAW1 capture armed (4096 pairs).\r\n");
    }
}

void nilm_capture_service_poll(void)
{
    const nilm_capture_stats *s = &capture.stats;
    if (!active) return;
    nilm_capture_poll(&capture, now_ms());
    if (capture.state == NILM_CAP_DONE) {
        xil_printf("NILM CAPTURE PASS: pairs=%u first=%u last=%u elapsed_ms=%u\r\n",
                   (unsigned)s->samples, (unsigned)s->first_id,
                   (unsigned)s->last_id, (unsigned)s->elapsed_ms);
        xil_printf("NILM RAW ADC: V=%u..%u I=%u..%u status=0x%08x\r\n",
                   (unsigned)s->voltage_min, (unsigned)s->voltage_max,
                   (unsigned)s->current_min, (unsigned)s->current_max,
                   (unsigned)s->last_status);
        xil_printf("NILM: frozen frame retained in DDR; RAW1 IDs are success counts.\r\n");
        active = 0; /* Preserve the frame. A new capture must be explicit. */
    } else if (capture.state == NILM_CAP_ERROR) {
        xil_printf("NILM CAPTURE FAIL: %s status=0x%08x copied=%u seq_errors=%u padding_errors=%u\r\n",
                   nilm_capture_error_name(capture.error), (unsigned)s->last_status,
                   (unsigned)s->samples, (unsigned)s->sequence_errors,
                   (unsigned)s->padding_errors);
        xil_printf("NILM: acquisition stopped; inspect status before coordinated PL/app restart.\r\n");
        active = 0;
    }
}
