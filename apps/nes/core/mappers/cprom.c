#include "mapper.h"
#include "emulator.h"

static void write_PRG(Mapper *mapper, uint16_t address, uint8_t value);
static uint8_t read_CHR(Mapper *mapper, uint16_t address);
static const uint8_t *chr_ptr(Mapper *mapper, uint16_t address); // CRTOS

int load_CPROM(Mapper *mapper) {
    mapper->write_PRG = write_PRG;
    mapper->read_CHR = read_CHR;
    mapper->chr_ptr = chr_ptr; // CRTOS
    return 0;
}

static void write_PRG(Mapper *mapper, uint16_t address, uint8_t value) {
    mapper->CHR_ptrs[0] = mapper->CHR_ROM + ((value & 0x3) << 12);
}

static uint8_t read_CHR(Mapper *mapper, uint16_t address) {
    if (address < 0x1000)
        return mapper->CHR_ROM[address];
    return mapper->CHR_ptrs[0][address & 0xfff];
}

// CRTOS: Mapper.chr_ptr (as read_CHR)
static const uint8_t *chr_ptr(Mapper *mapper, uint16_t address) {
    if (address < 0x1000)
        return mapper->CHR_ROM + address;
    return mapper->CHR_ptrs[0] + (address & 0xfff);
}
