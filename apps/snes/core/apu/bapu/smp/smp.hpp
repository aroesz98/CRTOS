//CRTOS: the opcodes, each a member function op_NN (core.cpp: op_step)
#define SMP_OPCODES(X) \
  X(00) X(01) X(02) X(03) X(04) X(05) X(06) X(07) X(08) X(09) X(0a) X(0b) X(0c) X(0d) X(0e) X(0f) \
  X(10) X(11) X(12) X(13) X(14) X(15) X(16) X(17) X(18) X(19) X(1a) X(1b) X(1c) X(1d) X(1e) X(1f) \
  X(20) X(21) X(22) X(23) X(24) X(25) X(26) X(27) X(28) X(29) X(2a) X(2b) X(2c) X(2d) X(2e) X(2f) \
  X(30) X(31) X(32) X(33) X(34) X(35) X(36) X(37) X(38) X(39) X(3a) X(3b) X(3c) X(3d) X(3e) X(3f) \
  X(40) X(41) X(42) X(43) X(44) X(45) X(46) X(47) X(48) X(49) X(4a) X(4b) X(4c) X(4d) X(4e) X(4f) \
  X(50) X(51) X(52) X(53) X(54) X(55) X(56) X(57) X(58) X(59) X(5a) X(5b) X(5c) X(5d) X(5e) X(5f) \
  X(60) X(61) X(62) X(63) X(64) X(65) X(66) X(67) X(68) X(69) X(6a) X(6b) X(6c) X(6d) X(6e) X(6f) \
  X(70) X(71) X(72) X(73) X(74) X(75) X(76) X(77) X(78) X(79) X(7a) X(7b) X(7c) X(7d) X(7e) X(7f) \
  X(80) X(81) X(82) X(83) X(84) X(85) X(86) X(87) X(88) X(89) X(8a) X(8b) X(8c) X(8d) X(8e) X(8f) \
  X(90) X(91) X(92) X(93) X(94) X(95) X(96) X(97) X(98) X(99) X(9a) X(9b) X(9c) X(9d) X(9e) X(9f) \
  X(a0) X(a1) X(a2) X(a3) X(a4) X(a5) X(a6) X(a7) X(a8) X(a9) X(aa) X(ab) X(ac) X(ad) X(ae) X(af) \
  X(b0) X(b1) X(b2) X(b3) X(b4) X(b5) X(b6) X(b7) X(b8) X(b9) X(ba) X(bb) X(bc) X(bd) X(be) X(bf) \
  X(c0) X(c1) X(c2) X(c3) X(c4) X(c5) X(c6) X(c7) X(c8) X(c9) X(ca) X(cb) X(cc) X(cd) X(ce) X(cf) \
  X(d0) X(d1) X(d2) X(d3) X(d4) X(d5) X(d6) X(d7) X(d8) X(d9) X(da) X(db) X(dc) X(dd) X(de) X(df) \
  X(e0) X(e1) X(e2) X(e3) X(e4) X(e5) X(e6) X(e7) X(e8) X(e9) X(ea) X(eb) X(ec) X(ed) X(ee) X(ef) \
  X(f0) X(f1) X(f2) X(f3) X(f4) X(f5) X(f6) X(f7) X(f8) X(f9) X(fa) X(fb) X(fc) X(fd) X(fe) X(ff)

class SMP : public Processor {
public:
  static const uint8 iplrom[64];
  uint8 *apuram;

  unsigned port_read(unsigned port);
  void port_write(unsigned port, unsigned data);

  __attribute__((noinline)) unsigned mmio_read(unsigned addr);          //CRTOS: noinline
  __attribute__((noinline)) void mmio_write(unsigned addr, unsigned data);

  //CRTOS: the clock value up to which the timers and the DSP got their cycles (catch_up)
  int32 mark;
  void catch_up();
  __attribute__((noinline)) uint8 op_read_rare(uint16 addr);

  void enter();
  void power();
  void reset();

  void load_state(uint8 **);
  void save_state(uint8 **);
  void save_spc (uint8 *);
  SMP();
  ~SMP();

//private:
  struct Flags {
    bool n, v, p, b, h, i, z, c;

    alwaysinline operator unsigned() const {
      return (n << 7) | (v << 6) | (p << 5) | (b << 4)
           | (h << 3) | (i << 2) | (z << 1) | (c << 0);
    };

    alwaysinline unsigned operator=(unsigned data) {
      n = data & 0x80; v = data & 0x40; p = data & 0x20; b = data & 0x10;
      h = data & 0x08; i = data & 0x04; z = data & 0x02; c = data & 0x01;
      return data;
    }

    alwaysinline unsigned operator|=(unsigned data) { return operator=(operator unsigned() | data); }
    alwaysinline unsigned operator^=(unsigned data) { return operator=(operator unsigned() ^ data); }
    alwaysinline unsigned operator&=(unsigned data) { return operator=(operator unsigned() & data); }
  };

  unsigned opcode_number;
  unsigned opcode_cycle;

  //CRTOS: one function per opcode (core/oppseudo_*.cpp), op_step calls them
#define SMP_DECLARE_OP(n) __attribute__((noinline)) void op_##n();
  SMP_OPCODES(SMP_DECLARE_OP)
#undef SMP_DECLARE_OP

  uint16 rd, wr, dp, sp, ya, bit;

  struct Regs {
    uint16 pc;
    uint8 sp;
    union {
      uint16 ya;
#ifndef __BIG_ENDIAN__
      struct { uint8 a, y; } B;
#else
      struct { uint8 y, a; } B;
#endif
    };
    uint8 x;
    Flags p;
  } regs;

  struct Status {
    //$00f1
    bool iplrom_enable;

    //$00f2
    unsigned dsp_addr;

    //$00f8,$00f9
    unsigned ram00f8;
    unsigned ram00f9;
  } status;

  template<unsigned frequency>
  struct Timer {
    bool enable;
    uint8 target;
    uint8 stage1_ticks;
    uint8 stage2_ticks;
    uint8 stage3_ticks;

    inline void tick();
    inline void tick(unsigned clocks);
    inline void tick_bulk(unsigned clocks);  //CRTOS
  };

  Timer<128> timer0;
  Timer<128> timer1;
  Timer< 16> timer2;

  inline void tick();
  inline void tick(unsigned clocks);
  alwaysinline void op_io();
  alwaysinline void op_io(unsigned clocks);
  debugvirtual alwaysinline uint8 op_read(uint16 addr);
  debugvirtual alwaysinline void op_write(uint16 addr, uint8 data);
  debugvirtual alwaysinline void op_step();
  alwaysinline void op_writestack(uint8 data);
  alwaysinline uint8 op_readstack();
  static const unsigned cycle_count_table[256];
  uint64 cycle_table_cpu[256];
  unsigned cycle_table_dsp[256];
  uint64 cycle_step_cpu;

  inline uint8  op_adc (uint8  x, uint8  y);
  inline uint16 op_addw(uint16 x, uint16 y);
  inline uint8  op_and (uint8  x, uint8  y);
  inline uint8  op_cmp (uint8  x, uint8  y);
  inline uint16 op_cmpw(uint16 x, uint16 y);
  inline uint8  op_eor (uint8  x, uint8  y);
  inline uint8  op_inc (uint8  x);
  inline uint8  op_dec (uint8  x);
  inline uint8  op_or  (uint8  x, uint8  y);
  inline uint8  op_sbc (uint8  x, uint8  y);
  inline uint16 op_subw(uint16 x, uint16 y);
  inline uint8  op_asl (uint8  x);
  inline uint8  op_lsr (uint8  x);
  inline uint8  op_rol (uint8  x);
  inline uint8  op_ror (uint8  x);
#ifdef DEBUGGER
  void disassemble_opcode(char *output, uint16 addr);
  inline uint8 disassemble_read(uint16 addr);
  inline uint16 relb(int8 offset, int op_len);
#endif
};

extern SMP smp;
