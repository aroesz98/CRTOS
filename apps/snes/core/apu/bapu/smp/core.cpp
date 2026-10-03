//CRTOS: the timers and the DSP clock follow lazily (catch_up(), memory.cpp): a cycle only
//counts here, which keeps every opcode of the switch small
void SMP::tick() {
  clock++;
}

void SMP::tick(unsigned clocks) {
  clock += clocks;
}

void SMP::op_io() {
  tick();
}

void SMP::op_io(unsigned clocks) {
  tick(clocks);
}

//CRTOS: registers and the IPL ROM out of line, so each opcode has only one test inline
uint8 SMP::op_read_rare(uint16 addr) {
  if((addr & 0xfff0) == 0x00f0) return mmio_read(addr);
  if(status.iplrom_enable) return iplrom[addr & 0x3f];
  return apuram[addr];
}

uint8 SMP::op_read(uint16 addr) {
  tick();
  if(__builtin_expect((addr & 0xfff0) == 0x00f0 || addr >= 0xffc0, 0)) return op_read_rare(addr);
  return apuram[addr];
}

void SMP::op_write(uint16 addr, uint8 data) {
  tick();
  if((addr & 0xfff0) == 0x00f0) mmio_write(addr, data);
  apuram[addr] = data;  //all writes go to RAM, even MMIO writes
}

uint8 SMP::op_readstack()
{
  tick();
  return apuram[0x0100 | ++regs.sp];
}

void SMP::op_writestack(uint8 data)
{
  tick();
  apuram[0x0100 | regs.sp--] = data;
}

//CRTOS: every opcode is a function of its own (core/oppseudo_*.cpp), called from op_step,
//instead of a case of one switch in op_step: that single function of some 2600 lines, with the
//memory accesses inlined into every case, needed more memory to compile than the board has
//(its optimisation passes took over 30 MB), while the dispatch costs the same
#define op_readpc() op_read(regs.pc++)
#define op_readdp(addr) op_read((regs.p.p << 8) + ((addr) & 0xff))
#define op_writedp(addr, data) op_write((regs.p.p << 8) + ((addr) & 0xff), data)
#define op_readaddr(addr) op_read(addr)
#define op_writeaddr(addr, data) op_write(addr, data)

#include "core/oppseudo_misc.cpp"
#include "core/oppseudo_mov.cpp"
#include "core/oppseudo_pc.cpp"
#include "core/oppseudo_read.cpp"
#include "core/oppseudo_rmw.cpp"

void SMP::op_step() {
  if(opcode_cycle == 0)
  {
#ifdef DEBUGGER
    if (Settings.TraceSMP)
    {
      disassemble_opcode(tmp, regs.pc);
      S9xTraceMessage (tmp);
    }
#endif
    opcode_number = op_readpc();
  }

  //(a jump table of direct calls: cheaper than a pointer to a member function; the functions
  //are noinline, else the compiler would put the one big function together again)
  switch(opcode_number) {
#define SMP_OP_CASE(n) case 0x##n: op_##n(); break;
  SMP_OPCODES(SMP_OP_CASE)
#undef SMP_OP_CASE
  }
}
