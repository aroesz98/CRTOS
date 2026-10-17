/*
 * boot/xspi.c - the two external memories of the STM32H7S78-DK, memory-mapped.
 *
 *   XSPI2 (port 2, PN0..PN11): Macronix MX66UW1G45G, 128 MB NOR flash, read in octal DTR mode
 *       (8D-8D-8D, command EE11h, 20 dummy cycles - the chip's default) at 200 MHz, with DQS
 *   XSPI1 (port 1, PO0..PO4, PP0..PP15): AP Memory APS256XXN, 32 MB PSRAM, x16 DTR at 200 MHz
 *       (read latency 7 fixed, write latency 7), 2 KB chip-select boundary, refresh every 3 us
 *
 * The NOR sequence follows ST's BSP (stm32h7s78_discovery_xspi.c and the mx66uw1g45g component):
 * the chip may be in SPI, STR OPI or DTR OPI mode after a reset of the processor alone, so it
 * gets a software reset in all three, then octal DTR is switched on in its configuration
 * register 2. The PSRAM settings are the ones the NeoChrom demo found sound on this board: ST's
 * 8 KB chip-select boundary corrupted writes, the refresh counter counts XSPI clocks.
 */
#include "boot.h"
#include "psram.h"

#define XSPI_PRESC_SLOW 3u          /* 200 MHz / 4: the NOR's SPI mode, before octal DTR */

/* MX66UW1G45G */
#define NOR_ID                 0x3B81C2u     /* Macronix, octal, 1 Gbit (read as bytes C2 81 3B) */
#define NOR_RESET_ENABLE       0x66u
#define NOR_RESET_MEMORY       0x99u
#define NOR_READ_ID            0x9Fu
#define NOR_WRITE_ENABLE       0x06u
#define NOR_READ_STATUS        0x05u
#define NOR_WRITE_CR2          0x72u
#define NOR_READ_CR2           0x71u
#define NOR_OCTA_READ_DTR      0xEE11u
#define NOR_OCTA_PAGE_PROG     0x12EDu
#define NOR_CR2_MODE_ADDR      0x00000000u
#define NOR_CR2_DOPI           0x02u
#define NOR_SR_WIP             0x01u
#define NOR_SR_WEL             0x02u
#define NOR_DUMMY_READ_DTR     20u           /* CR2 0x300 DC = 000 (default): up to 200 MHz */
#define NOR_DUMMY_REG_DTR      5u            /* as ST's BSP (DUMMY_CYCLES_REG_OCTAL_DTR) */
#define NOR_DUMMY_REG_STR      4u

/* APS256XXN (the register commands and MR0/MR1: board/psram.h, which the kernel checks) */
#define RAM_BURST_READ         0x20u
#define RAM_BURST_WRITE        0xA0u
#define RAM_DUMMY              6u
#define RAM_READ_LATENCY       7u            /* read latency code 4 */

static XSPI_HandleTypeDef s_nor, s_ram;
static uint32_t s_nor_id;

enum { SPI_STR, OPI_STR, OPI_DTR };

uint32_t nor_id(void)
{
    return s_nor_id;
}

static void pins(GPIO_TypeDef *port, uint32_t pins, uint32_t pull)
{
    GPIO_InitTypeDef io = { 0 };
    io.Pin = pins;
    io.Mode = GPIO_MODE_AF_PP;
    io.Pull = pull;
    io.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    io.Alternate = GPIO_AF9_XSPIM_P1;    /* = GPIO_AF9_XSPIM_P2: both ports are AF9 */
    HAL_GPIO_Init(port, &io);
}

/* The I/O cells of an XSPI port: high speed at low voltage (the memories run at 1.8 V; the
 * XSPI1_HSLV/XSPI2_HSLV option bytes are set on this board) and the compensation cell */
static int io_cells(uint32_t hslv, uint32_t cell, uint32_t ready)
{
    HAL_SBS_EnableIOSpeedOptimize(hslv);
    HAL_SBS_ConfigCompensationCell(cell, SBS_IO_CELL_CODE, 0, 0);
    HAL_SBS_EnableCompensationCell(cell);
    uint32_t t0 = HAL_GetTick();
    while (HAL_SBS_GetCompensationCellReadyStatus(ready) != 1u)
        if (HAL_GetTick() - t0 > 10u)
            return -1;
    return 0;
}

static void nor_command(XSPI_RegularCmdTypeDef *c, int mode, uint32_t spi_instr, uint32_t opi_instr)
{
    *c = (XSPI_RegularCmdTypeDef){ 0 };
    c->OperationType = HAL_XSPI_OPTYPE_COMMON_CFG;
    c->IOSelect = HAL_XSPI_SELECT_IO_7_0;
    if (mode == SPI_STR) {
        c->InstructionMode = HAL_XSPI_INSTRUCTION_1_LINE;
        c->InstructionWidth = HAL_XSPI_INSTRUCTION_8_BITS;
        c->Instruction = spi_instr;
    } else {
        c->InstructionMode = HAL_XSPI_INSTRUCTION_8_LINES;
        c->InstructionWidth = HAL_XSPI_INSTRUCTION_16_BITS;
        c->Instruction = opi_instr;
        c->InstructionDTRMode = mode == OPI_DTR ? HAL_XSPI_INSTRUCTION_DTR_ENABLE : HAL_XSPI_INSTRUCTION_DTR_DISABLE;
    }
    c->AddressMode = HAL_XSPI_ADDRESS_NONE;
    c->AlternateBytesMode = HAL_XSPI_ALT_BYTES_NONE;
    c->DataMode = HAL_XSPI_DATA_NONE;
    c->DQSMode = HAL_XSPI_DQS_DISABLE;
}

/* the data phase of a register access in @mode: 1 byte, or 2 in DTR (one clock edge each) */
static void nor_reg_phase(XSPI_RegularCmdTypeDef *c, int mode, uint32_t addr, int with_addr, int read)
{
    if (mode == SPI_STR) {
        if (with_addr) {
            c->AddressMode = HAL_XSPI_ADDRESS_1_LINE;
            c->AddressWidth = HAL_XSPI_ADDRESS_32_BITS;
            c->Address = addr;
        }
        c->DataMode = HAL_XSPI_DATA_1_LINE;
        c->DataLength = 1;
        return;
    }
    c->AddressMode = HAL_XSPI_ADDRESS_8_LINES;
    c->AddressWidth = HAL_XSPI_ADDRESS_32_BITS;
    c->Address = addr;
    c->DataMode = HAL_XSPI_DATA_8_LINES;
    if (mode == OPI_DTR) {
        c->AddressDTRMode = HAL_XSPI_ADDRESS_DTR_ENABLE;
        c->DataDTRMode = HAL_XSPI_DATA_DTR_ENABLE;
        c->DataLength = 2;
        if (read) {
            c->DummyCycles = NOR_DUMMY_REG_DTR;
            c->DQSMode = HAL_XSPI_DQS_ENABLE;
        }
    } else {
        c->DataLength = 1;
        if (read)
            c->DummyCycles = NOR_DUMMY_REG_STR;
    }
}

static int nor_simple(int mode, uint32_t spi_instr, uint32_t opi_instr)
{
    XSPI_RegularCmdTypeDef c;
    nor_command(&c, mode, spi_instr, opi_instr);
    return HAL_XSPI_Command(&s_nor, &c, HAL_XSPI_TIMEOUT_DEFAULT_VALUE) == HAL_OK ? 0 : -1;
}

/* wait until the status register's @mask bits are @match */
static int nor_poll(int mode, uint8_t mask, uint8_t match)
{
    XSPI_RegularCmdTypeDef c;
    nor_command(&c, mode, NOR_READ_STATUS, 0x05FAu);
    nor_reg_phase(&c, mode, 0, mode != SPI_STR, 1);
    if (HAL_XSPI_Command(&s_nor, &c, HAL_XSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
        return -1;
    XSPI_AutoPollingTypeDef p = { 0 };
    p.MatchValue = match;
    p.MatchMask = mask;
    p.MatchMode = HAL_XSPI_MATCH_MODE_AND;
    p.IntervalTime = 0x10;
    p.AutomaticStop = HAL_XSPI_AUTOMATIC_STOP_ENABLE;
    return HAL_XSPI_AutoPolling(&s_nor, &p, 1000) == HAL_OK ? 0 : -1;
}

static int nor_write_enable(int mode)
{
    if (nor_simple(mode, NOR_WRITE_ENABLE, 0x06F9u))
        return -1;
    return nor_poll(mode, NOR_SR_WEL, NOR_SR_WEL);
}

static int nor_write_cr2(int mode, uint32_t addr, uint8_t value)
{
    XSPI_RegularCmdTypeDef c;
    nor_command(&c, mode, NOR_WRITE_CR2, 0x728Du);
    nor_reg_phase(&c, mode, addr, 1, 0);
    uint8_t data[2] = { value, value };
    if (HAL_XSPI_Command(&s_nor, &c, HAL_XSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
        return -1;
    return HAL_XSPI_Transmit(&s_nor, data, HAL_XSPI_TIMEOUT_DEFAULT_VALUE) == HAL_OK ? 0 : -1;
}

static int nor_read_cr2(int mode, uint32_t addr, uint8_t *value)
{
    XSPI_RegularCmdTypeDef c;
    nor_command(&c, mode, NOR_READ_CR2, 0x718Eu);
    nor_reg_phase(&c, mode, addr, 1, 1);
    uint8_t data[2];
    if (HAL_XSPI_Command(&s_nor, &c, HAL_XSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK ||
        HAL_XSPI_Receive(&s_nor, data, HAL_XSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
        return -1;
    *value = data[0];
    return 0;
}

static int nor_read_id(int mode)
{
    XSPI_RegularCmdTypeDef c;
    nor_command(&c, mode, NOR_READ_ID, 0x9F60u);
    if (mode == SPI_STR) {
        c.DataMode = HAL_XSPI_DATA_1_LINE;
        c.DataLength = 3;
    } else {
        nor_reg_phase(&c, mode, 0, 1, 1);
        c.DataLength = mode == OPI_DTR ? 6 : 3;
    }
    uint8_t id[6] = { 0 };
    if (HAL_XSPI_Command(&s_nor, &c, HAL_XSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK ||
        HAL_XSPI_Receive(&s_nor, id, HAL_XSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
        return -1;
    /* DTR: every byte comes twice */
    if (mode == OPI_DTR)
        s_nor_id = id[0] | (uint32_t)id[2] << 8 | (uint32_t)id[4] << 16;
    else
        s_nor_id = id[0] | (uint32_t)id[1] << 8 | (uint32_t)id[2] << 16;
    return s_nor_id == NOR_ID ? 0 : -1;
}

static int xspi_setup(XSPI_HandleTypeDef *h, XSPI_TypeDef *inst, uint32_t type, uint32_t size, uint32_t presc,
                      uint32_t csht, uint32_t fifo, uint32_t csbound, uint32_t refresh)
{
    h->Instance = inst;
    h->Init.FifoThresholdByte = fifo;
    h->Init.MemoryMode = HAL_XSPI_SINGLE_MEM;
    h->Init.MemoryType = type;
    h->Init.MemorySize = size;
    h->Init.ChipSelectHighTimeCycle = csht;
    h->Init.FreeRunningClock = HAL_XSPI_FREERUNCLK_DISABLE;
    h->Init.ClockMode = HAL_XSPI_CLOCK_MODE_0;
    h->Init.WrapSize = HAL_XSPI_WRAP_NOT_SUPPORTED;
    h->Init.ClockPrescaler = presc;
    h->Init.SampleShifting = HAL_XSPI_SAMPLE_SHIFT_NONE;
    h->Init.ChipSelectBoundary = csbound;
    h->Init.MaxTran = 0;
    h->Init.Refresh = refresh;
    h->Init.MemorySelect = HAL_XSPI_CSSEL_NCS1;
    return HAL_XSPI_Init(h) == HAL_OK ? 0 : -1;
}

/* XSPI clocks, the I/O manager (direct: XSPI1 on port 1, XSPI2 on port 2) and the SBS */
static void common_init(void)
{
    static int done;
    if (done)
        return;
    done = 1;
    __HAL_RCC_SBS_CLK_ENABLE();
    __HAL_RCC_XSPIM_CLK_ENABLE();
    XSPIM->CR = 0;
    /* the I/O compensation cells run on the CSI oscillator */
    __HAL_RCC_CSI_ENABLE();
    uint32_t t0 = HAL_GetTick();
    while (!__HAL_RCC_GET_FLAG(RCC_FLAG_CSIRDY) && HAL_GetTick() - t0 < 10u) {
    }
}

int nor_init(void)
{
    common_init();
    HAL_PWREx_EnableXSPIM2();
    __HAL_RCC_XSPI2_CLK_ENABLE();
    __HAL_RCC_XSPI2_FORCE_RESET();
    __HAL_RCC_XSPI2_RELEASE_RESET();
    __HAL_RCC_GPION_CLK_ENABLE();
    if (io_cells(SBS_IO_XSPI2_HSLV, SBS_IO_XSPI2_CELL, SBS_IO_XSPI2_CELL_READY))
        return -1;
    pins(GPION, GPIO_PIN_0 | GPIO_PIN_1, GPIO_PULLUP);                    /* DQS, NCS1 */
    pins(GPION, GPIO_PIN_2 | GPIO_PIN_3 | GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_6 | /* IO0-3, CLK */
                GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10 | GPIO_PIN_11, GPIO_NOPULL);   /* IO4-7 */

    if (xspi_setup(&s_nor, XSPI2, HAL_XSPI_MEMTYPE_MACRONIX, HAL_XSPI_SIZE_1GB, XSPI_PRESC_SLOW, 2, 4,
                   HAL_XSPI_BONDARYOF_NONE, 0))
        return -2;

    /* a software reset in each mode the chip may be in (it ignores the others) */
    if (nor_simple(SPI_STR, NOR_RESET_ENABLE, 0) || nor_simple(SPI_STR, NOR_RESET_MEMORY, 0) ||
        nor_simple(OPI_STR, 0, 0x6699u) || nor_simple(OPI_STR, 0, 0x9966u) ||
        nor_simple(OPI_DTR, 0, 0x6699u) || nor_simple(OPI_DTR, 0, 0x9966u))
        return -3;
    HAL_Delay(100);     /* tREADY2: a reset during an erase */

    if (nor_read_id(SPI_STR))
        return -4;
    if (nor_write_enable(SPI_STR) || nor_write_cr2(SPI_STR, NOR_CR2_MODE_ADDR, NOR_CR2_DOPI))
        return -5;
    HAL_Delay(1);

    /* from here on octal DTR at the full 200 MHz (HAL_XSPI_Init() only sets up a handle in
     * the reset state) */
    HAL_XSPI_DeInit(&s_nor);
    if (xspi_setup(&s_nor, XSPI2, HAL_XSPI_MEMTYPE_MACRONIX, HAL_XSPI_SIZE_1GB, 0, 2, 4, HAL_XSPI_BONDARYOF_NONE, 0))
        return -6;
    uint8_t cr2 = 0;
    if (nor_poll(OPI_DTR, NOR_SR_WIP, 0) || nor_read_cr2(OPI_DTR, NOR_CR2_MODE_ADDR, &cr2) || cr2 != NOR_CR2_DOPI)
        return -7;
    if (nor_read_id(OPI_DTR))
        return -8;

    XSPI_RegularCmdTypeDef c;
    nor_command(&c, OPI_DTR, 0, NOR_OCTA_READ_DTR);
    c.OperationType = HAL_XSPI_OPTYPE_READ_CFG;
    c.AddressMode = HAL_XSPI_ADDRESS_8_LINES;
    c.AddressWidth = HAL_XSPI_ADDRESS_32_BITS;
    c.AddressDTRMode = HAL_XSPI_ADDRESS_DTR_ENABLE;
    c.DataMode = HAL_XSPI_DATA_8_LINES;
    c.DataDTRMode = HAL_XSPI_DATA_DTR_ENABLE;
    c.DummyCycles = NOR_DUMMY_READ_DTR;
    c.DQSMode = HAL_XSPI_DQS_ENABLE;
    if (HAL_XSPI_Command(&s_nor, &c, HAL_XSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
        return -9;
    c.OperationType = HAL_XSPI_OPTYPE_WRITE_CFG;
    c.Instruction = NOR_OCTA_PAGE_PROG;
    c.DummyCycles = 0;
    c.DQSMode = HAL_XSPI_DQS_DISABLE;
    if (HAL_XSPI_Command(&s_nor, &c, HAL_XSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
        return -10;
    XSPI_MemoryMappedTypeDef mm = { 0 };
    mm.TimeOutActivation = HAL_XSPI_TIMEOUT_COUNTER_DISABLE;
    if (HAL_XSPI_MemoryMapped(&s_nor, &mm) != HAL_OK)
        return -11;
    return 0;
}

static int ram_write_reg(uint32_t addr, uint8_t value, uint8_t second)
{
    XSPI_RegularCmdTypeDef c = { 0 };
    c.OperationType = HAL_XSPI_OPTYPE_COMMON_CFG;
    c.Instruction = PSRAM_WRITE_REG;
    c.InstructionMode = HAL_XSPI_INSTRUCTION_8_LINES;
    c.InstructionWidth = HAL_XSPI_INSTRUCTION_8_BITS;
    c.Address = addr;
    c.AddressMode = HAL_XSPI_ADDRESS_8_LINES;
    c.AddressWidth = HAL_XSPI_ADDRESS_32_BITS;
    c.AddressDTRMode = HAL_XSPI_ADDRESS_DTR_ENABLE;
    c.DataMode = HAL_XSPI_DATA_8_LINES;
    c.DataLength = 2;
    c.DataDTRMode = HAL_XSPI_DATA_DTR_ENABLE;
    uint8_t data[2] = { value, second };
    if (HAL_XSPI_Command(&s_ram, &c, HAL_XSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
        return -1;
    return HAL_XSPI_Transmit(&s_ram, data, HAL_XSPI_TIMEOUT_DEFAULT_VALUE) == HAL_OK ? 0 : -1;
}

static int ram_read_reg(uint32_t addr, uint8_t *value)
{
    XSPI_RegularCmdTypeDef c = { 0 };
    c.OperationType = HAL_XSPI_OPTYPE_COMMON_CFG;
    c.Instruction = PSRAM_READ_REG;
    c.InstructionMode = HAL_XSPI_INSTRUCTION_8_LINES;
    c.InstructionWidth = HAL_XSPI_INSTRUCTION_8_BITS;
    c.Address = addr;
    c.AddressMode = HAL_XSPI_ADDRESS_8_LINES;
    c.AddressWidth = HAL_XSPI_ADDRESS_32_BITS;
    c.AddressDTRMode = HAL_XSPI_ADDRESS_DTR_ENABLE;
    c.DataMode = HAL_XSPI_DATA_8_LINES;
    c.DataLength = 2;
    c.DataDTRMode = HAL_XSPI_DATA_DTR_ENABLE;
    c.DummyCycles = RAM_READ_LATENCY;
    c.DQSMode = HAL_XSPI_DQS_ENABLE;
    uint8_t data[2];
    if (HAL_XSPI_Command(&s_ram, &c, HAL_XSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK ||
        HAL_XSPI_Receive(&s_ram, data, HAL_XSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
        return -1;
    *value = data[0];
    return 0;
}

static int ram_set(uint32_t addr, uint8_t value, uint8_t second)
{
    uint8_t back = 0;
    if (ram_write_reg(addr, value, second) || ram_read_reg(addr, &back))
        return -1;
    return back == value ? 0 : -1;
}

int psram_init(void)
{
    common_init();
    HAL_PWREx_EnableXSPIM1();
    __HAL_RCC_XSPI1_CLK_ENABLE();
    __HAL_RCC_XSPI1_FORCE_RESET();
    __HAL_RCC_XSPI1_RELEASE_RESET();
    __HAL_RCC_GPIOO_CLK_ENABLE();
    __HAL_RCC_GPIOP_CLK_ENABLE();
    if (io_cells(SBS_IO_XSPI1_HSLV, SBS_IO_XSPI1_CELL, SBS_IO_XSPI1_CELL_READY))
        return -1;
    pins(GPIOO, GPIO_PIN_0 | GPIO_PIN_2 | GPIO_PIN_3, GPIO_PULLUP);          /* NCS1, DQS0, DQS1 */
    pins(GPIOO, GPIO_PIN_4, GPIO_NOPULL);                                   /* CLK */
    pins(GPIOP, 0xFFFFu, GPIO_NOPULL);                                      /* IO0-15 */

    /* 200 MHz; the chip select goes up every 600 clocks (3 us; the chip allows 4 us) so the
     * memory can refresh itself, and at 2 KB boundaries */
    if (xspi_setup(&s_ram, XSPI1, HAL_XSPI_MEMTYPE_APMEM_16BITS, HAL_XSPI_SIZE_256MB, 0, 5, 2, 11, 600))
        return -2;

    /* MR0: fixed latency, read latency code 4 (7 clocks), full drive; MR4: write latency
     * code 1 (7 clocks), 4x refresh, the whole array; MR8: x16 mode, 16-byte bursts. The
     * second byte of each write (DTR moves two) is the value of the next register as read. */
    if (ram_set(0, PSRAM_MR0_FIXED, PSRAM_MR1))
        return -3;
    if (ram_set(4, 1u << 5, 0x05u))
        return -4;
    if (ram_set(8, 0x40u, 0x08u))
        return -5;

    XSPI_RegularCmdTypeDef c = { 0 };
    c.OperationType = HAL_XSPI_OPTYPE_WRITE_CFG;
    c.Instruction = RAM_BURST_WRITE;
    c.InstructionMode = HAL_XSPI_INSTRUCTION_8_LINES;
    c.InstructionWidth = HAL_XSPI_INSTRUCTION_8_BITS;
    c.AddressMode = HAL_XSPI_ADDRESS_8_LINES;
    c.AddressWidth = HAL_XSPI_ADDRESS_32_BITS;
    c.AddressDTRMode = HAL_XSPI_ADDRESS_DTR_ENABLE;
    c.DataMode = HAL_XSPI_DATA_16_LINES;
    c.DataDTRMode = HAL_XSPI_DATA_DTR_ENABLE;
    c.DummyCycles = RAM_DUMMY;
    c.DQSMode = HAL_XSPI_DQS_ENABLE;
    if (HAL_XSPI_Command(&s_ram, &c, HAL_XSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
        return -6;
    c.OperationType = HAL_XSPI_OPTYPE_READ_CFG;
    c.Instruction = RAM_BURST_READ;
    if (HAL_XSPI_Command(&s_ram, &c, HAL_XSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
        return -7;
    XSPI_MemoryMappedTypeDef mm = { 0 };
    mm.TimeOutActivation = HAL_XSPI_TIMEOUT_COUNTER_DISABLE;
    if (HAL_XSPI_MemoryMapped(&s_ram, &mm) != HAL_OK)
        return -8;
    return 0;
}
