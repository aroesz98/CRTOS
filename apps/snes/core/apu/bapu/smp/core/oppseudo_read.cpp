void SMP::op_88() {
  rd = op_readpc();
  regs.B.a = op_adc(regs.B.a, rd);
  return;
}

void SMP::op_28() {
  rd = op_readpc();
  regs.B.a = op_and(regs.B.a, rd);
  return;
}

void SMP::op_68() {
  rd = op_readpc();
  regs.B.a = op_cmp(regs.B.a, rd);
  return;
}

void SMP::op_c8() {
  rd = op_readpc();
  regs.x = op_cmp(regs.x, rd);
  return;
}

void SMP::op_ad() {
  rd = op_readpc();
  regs.B.y = op_cmp(regs.B.y, rd);
  return;
}

void SMP::op_48() {
  rd = op_readpc();
  regs.B.a = op_eor(regs.B.a, rd);
  return;
}

void SMP::op_08() {
  rd = op_readpc();
  regs.B.a = op_or(regs.B.a, rd);
  return;
}

void SMP::op_a8() {
  rd = op_readpc();
  regs.B.a = op_sbc(regs.B.a, rd);
  return;
}

void SMP::op_86() {
  op_io();
  rd = op_readdp(regs.x);
  regs.B.a = op_adc(regs.B.a, rd);
  return;
}

void SMP::op_26() {
  op_io();
  rd = op_readdp(regs.x);
  regs.B.a = op_and(regs.B.a, rd);
  return;
}

void SMP::op_66() {
  op_io();
  rd = op_readdp(regs.x);
  regs.B.a = op_cmp(regs.B.a, rd);
  return;
}

void SMP::op_46() {
  op_io();
  rd = op_readdp(regs.x);
  regs.B.a = op_eor(regs.B.a, rd);
  return;
}

void SMP::op_06() {
  op_io();
  rd = op_readdp(regs.x);
  regs.B.a = op_or(regs.B.a, rd);
  return;
}

void SMP::op_a6() {
  op_io();
  rd = op_readdp(regs.x);
  regs.B.a = op_sbc(regs.B.a, rd);
  return;
}

void SMP::op_84() {
  dp = op_readpc();
  rd = op_readdp(dp);
  regs.B.a = op_adc(regs.B.a, rd);
  return;
}

void SMP::op_24() {
  dp = op_readpc();
  rd = op_readdp(dp);
  regs.B.a = op_and(regs.B.a, rd);
  return;
}

void SMP::op_64() {
  dp = op_readpc();
  rd = op_readdp(dp);
  regs.B.a = op_cmp(regs.B.a, rd);
  return;
}

void SMP::op_3e() {
  dp = op_readpc();
  rd = op_readdp(dp);
  regs.x = op_cmp(regs.x, rd);
  return;
}

void SMP::op_7e() {
  switch(++opcode_cycle) {
  case 1:
    dp = op_readpc();
    break;
  case 2:
    rd = op_readdp(dp);
    regs.B.y = op_cmp(regs.B.y, rd);
    opcode_cycle = 0;
    break;
  }
  return;
}

void SMP::op_44() {
  dp = op_readpc();
  rd = op_readdp(dp);
  regs.B.a = op_eor(regs.B.a, rd);
  return;
}

void SMP::op_04() {
  dp = op_readpc();
  rd = op_readdp(dp);
  regs.B.a = op_or(regs.B.a, rd);
  return;
}

void SMP::op_a4() {
  dp = op_readpc();
  rd = op_readdp(dp);
  regs.B.a = op_sbc(regs.B.a, rd);
  return;
}

void SMP::op_94() {
  dp = op_readpc();
  op_io();
  rd = op_readdp(dp + regs.x);
  regs.B.a = op_adc(regs.B.a, rd);
  return;
}

void SMP::op_34() {
  dp = op_readpc();
  op_io();
  rd = op_readdp(dp + regs.x);
  regs.B.a = op_and(regs.B.a, rd);
  return;
}

void SMP::op_74() {
  dp = op_readpc();
  op_io();
  rd = op_readdp(dp + regs.x);
  regs.B.a = op_cmp(regs.B.a, rd);
  return;
}

void SMP::op_54() {
  dp = op_readpc();
  op_io();
  rd = op_readdp(dp + regs.x);
  regs.B.a = op_eor(regs.B.a, rd);
  return;
}

void SMP::op_14() {
  dp = op_readpc();
  op_io();
  rd = op_readdp(dp + regs.x);
  regs.B.a = op_or(regs.B.a, rd);
  return;
}

void SMP::op_b4() {
  dp = op_readpc();
  op_io();
  rd = op_readdp(dp + regs.x);
  regs.B.a = op_sbc(regs.B.a, rd);
  return;
}

void SMP::op_85() {
  dp  = op_readpc();
  dp |= op_readpc() << 8;
  rd = op_readaddr(dp);
  regs.B.a = op_adc(regs.B.a, rd);
  return;
}

void SMP::op_25() {
  dp  = op_readpc();
  dp |= op_readpc() << 8;
  rd = op_readaddr(dp);
  regs.B.a = op_and(regs.B.a, rd);
  return;
}

void SMP::op_65() {
  dp  = op_readpc();
  dp |= op_readpc() << 8;
  rd = op_readaddr(dp);
  regs.B.a = op_cmp(regs.B.a, rd);
  return;
}

void SMP::op_1e() {
  dp  = op_readpc();
  dp |= op_readpc() << 8;
  rd = op_readaddr(dp);
  regs.x = op_cmp(regs.x, rd);
  return;
}

void SMP::op_5e() {
  dp  = op_readpc();
  dp |= op_readpc() << 8;
  rd = op_readaddr(dp);
  regs.B.y = op_cmp(regs.B.y, rd);
  return;
}

void SMP::op_45() {
  dp  = op_readpc();
  dp |= op_readpc() << 8;
  rd = op_readaddr(dp);
  regs.B.a = op_eor(regs.B.a, rd);
  return;
}

void SMP::op_05() {
  dp  = op_readpc();
  dp |= op_readpc() << 8;
  rd = op_readaddr(dp);
  regs.B.a = op_or(regs.B.a, rd);
  return;
}

void SMP::op_a5() {
  dp  = op_readpc();
  dp |= op_readpc() << 8;
  rd = op_readaddr(dp);
  regs.B.a = op_sbc(regs.B.a, rd);
  return;
}

void SMP::op_95() {
  dp  = op_readpc();
  dp |= op_readpc() << 8;
  op_io();
  rd = op_readaddr(dp + regs.x);
  regs.B.a = op_adc(regs.B.a, rd);
  return;
}

void SMP::op_96() {
  dp  = op_readpc();
  dp |= op_readpc() << 8;
  op_io();
  rd = op_readaddr(dp + regs.B.y);
  regs.B.a = op_adc(regs.B.a, rd);
  return;
}

void SMP::op_35() {
  dp  = op_readpc();
  dp |= op_readpc() << 8;
  op_io();
  rd = op_readaddr(dp + regs.x);
  regs.B.a = op_and(regs.B.a, rd);
  return;
}

void SMP::op_36() {
  dp  = op_readpc();
  dp |= op_readpc() << 8;
  op_io();
  rd = op_readaddr(dp + regs.B.y);
  regs.B.a = op_and(regs.B.a, rd);
  return;
}

void SMP::op_75() {
  dp  = op_readpc();
  dp |= op_readpc() << 8;
  op_io();
  rd = op_readaddr(dp + regs.x);
  regs.B.a = op_cmp(regs.B.a, rd);
  return;
}

void SMP::op_76() {
  dp  = op_readpc();
  dp |= op_readpc() << 8;
  op_io();
  rd = op_readaddr(dp + regs.B.y);
  regs.B.a = op_cmp(regs.B.a, rd);
  return;
}

void SMP::op_55() {
  dp  = op_readpc();
  dp |= op_readpc() << 8;
  op_io();
  rd = op_readaddr(dp + regs.x);
  regs.B.a = op_eor(regs.B.a, rd);
  return;
}

void SMP::op_56() {
  dp  = op_readpc();
  dp |= op_readpc() << 8;
  op_io();
  rd = op_readaddr(dp + regs.B.y);
  regs.B.a = op_eor(regs.B.a, rd);
  return;
}

void SMP::op_15() {
  dp  = op_readpc();
  dp |= op_readpc() << 8;
  op_io();
  rd = op_readaddr(dp + regs.x);
  regs.B.a = op_or(regs.B.a, rd);
  return;
}

void SMP::op_16() {
  dp  = op_readpc();
  dp |= op_readpc() << 8;
  op_io();
  rd = op_readaddr(dp + regs.B.y);
  regs.B.a = op_or(regs.B.a, rd);
  return;
}

void SMP::op_b5() {
  dp  = op_readpc();
  dp |= op_readpc() << 8;
  op_io();
  rd = op_readaddr(dp + regs.x);
  regs.B.a = op_sbc(regs.B.a, rd);
  return;
}

void SMP::op_b6() {
  dp  = op_readpc();
  dp |= op_readpc() << 8;
  op_io();
  rd = op_readaddr(dp + regs.B.y);
  regs.B.a = op_sbc(regs.B.a, rd);
  return;
}

void SMP::op_87() {
  dp = op_readpc() + regs.x;
  op_io();
  sp  = op_readdp(dp);
  sp |= op_readdp(dp + 1) << 8;
  rd = op_readaddr(sp);
  regs.B.a = op_adc(regs.B.a, rd);
  return;
}

void SMP::op_27() {
  dp = op_readpc() + regs.x;
  op_io();
  sp  = op_readdp(dp);
  sp |= op_readdp(dp + 1) << 8;
  rd = op_readaddr(sp);
  regs.B.a = op_and(regs.B.a, rd);
  return;
}

void SMP::op_67() {
  dp = op_readpc() + regs.x;
  op_io();
  sp  = op_readdp(dp);
  sp |= op_readdp(dp + 1) << 8;
  rd = op_readaddr(sp);
  regs.B.a = op_cmp(regs.B.a, rd);
  return;
}

void SMP::op_47() {
  dp = op_readpc() + regs.x;
  op_io();
  sp  = op_readdp(dp);
  sp |= op_readdp(dp + 1) << 8;
  rd = op_readaddr(sp);
  regs.B.a = op_eor(regs.B.a, rd);
  return;
}

void SMP::op_07() {
  dp = op_readpc() + regs.x;
  op_io();
  sp  = op_readdp(dp);
  sp |= op_readdp(dp + 1) << 8;
  rd = op_readaddr(sp);
  regs.B.a = op_or(regs.B.a, rd);
  return;
}

void SMP::op_a7() {
  dp = op_readpc() + regs.x;
  op_io();
  sp  = op_readdp(dp);
  sp |= op_readdp(dp + 1) << 8;
  rd = op_readaddr(sp);
  regs.B.a = op_sbc(regs.B.a, rd);
  return;
}

void SMP::op_97() {
  dp  = op_readpc();
  op_io();
  sp  = op_readdp(dp);
  sp |= op_readdp(dp + 1) << 8;
  rd = op_readaddr(sp + regs.B.y);
  regs.B.a = op_adc(regs.B.a, rd);
  return;
}

void SMP::op_37() {
  dp  = op_readpc();
  op_io();
  sp  = op_readdp(dp);
  sp |= op_readdp(dp + 1) << 8;
  rd = op_readaddr(sp + regs.B.y);
  regs.B.a = op_and(regs.B.a, rd);
  return;
}

void SMP::op_77() {
  dp  = op_readpc();
  op_io();
  sp  = op_readdp(dp);
  sp |= op_readdp(dp + 1) << 8;
  rd = op_readaddr(sp + regs.B.y);
  regs.B.a = op_cmp(regs.B.a, rd);
  return;
}

void SMP::op_57() {
  dp  = op_readpc();
  op_io();
  sp  = op_readdp(dp);
  sp |= op_readdp(dp + 1) << 8;
  rd = op_readaddr(sp + regs.B.y);
  regs.B.a = op_eor(regs.B.a, rd);
  return;
}

void SMP::op_17() {
  dp  = op_readpc();
  op_io();
  sp  = op_readdp(dp);
  sp |= op_readdp(dp + 1) << 8;
  rd = op_readaddr(sp + regs.B.y);
  regs.B.a = op_or(regs.B.a, rd);
  return;
}

void SMP::op_b7() {
  dp  = op_readpc();
  op_io();
  sp  = op_readdp(dp);
  sp |= op_readdp(dp + 1) << 8;
  rd = op_readaddr(sp + regs.B.y);
  regs.B.a = op_sbc(regs.B.a, rd);
  return;
}

void SMP::op_99() {
  op_io();
  rd = op_readdp(regs.B.y);
  wr = op_readdp(regs.x);
  wr = op_adc(wr, rd);
  (1) ? op_writedp(regs.x, wr) : op_io();
  return;
}

void SMP::op_39() {
  op_io();
  rd = op_readdp(regs.B.y);
  wr = op_readdp(regs.x);
  wr = op_and(wr, rd);
  (1) ? op_writedp(regs.x, wr) : op_io();
  return;
}

void SMP::op_79() {
  op_io();
  rd = op_readdp(regs.B.y);
  wr = op_readdp(regs.x);
  wr = op_cmp(wr, rd);
  (0) ? op_writedp(regs.x, wr) : op_io();
  return;
}

void SMP::op_59() {
  op_io();
  rd = op_readdp(regs.B.y);
  wr = op_readdp(regs.x);
  wr = op_eor(wr, rd);
  (1) ? op_writedp(regs.x, wr) : op_io();
  return;
}

void SMP::op_19() {
  op_io();
  rd = op_readdp(regs.B.y);
  wr = op_readdp(regs.x);
  wr = op_or(wr, rd);
  (1) ? op_writedp(regs.x, wr) : op_io();
  return;
}

void SMP::op_b9() {
  op_io();
  rd = op_readdp(regs.B.y);
  wr = op_readdp(regs.x);
  wr = op_sbc(wr, rd);
  (1) ? op_writedp(regs.x, wr) : op_io();
  return;
}

void SMP::op_89() {
  sp = op_readpc();
  rd = op_readdp(sp);
  dp = op_readpc();
  wr = op_readdp(dp);
  wr = op_adc(wr, rd);
  (1) ? op_writedp(dp, wr) : op_io();
  return;
}

void SMP::op_29() {
  sp = op_readpc();
  rd = op_readdp(sp);
  dp = op_readpc();
  wr = op_readdp(dp);
  wr = op_and(wr, rd);
  (1) ? op_writedp(dp, wr) : op_io();
  return;
}

void SMP::op_69() {
  sp = op_readpc();
  rd = op_readdp(sp);
  dp = op_readpc();
  wr = op_readdp(dp);
  wr = op_cmp(wr, rd);
  (0) ? op_writedp(dp, wr) : op_io();
  return;
}

void SMP::op_49() {
  sp = op_readpc();
  rd = op_readdp(sp);
  dp = op_readpc();
  wr = op_readdp(dp);
  wr = op_eor(wr, rd);
  (1) ? op_writedp(dp, wr) : op_io();
  return;
}

void SMP::op_09() {
  sp = op_readpc();
  rd = op_readdp(sp);
  dp = op_readpc();
  wr = op_readdp(dp);
  wr = op_or(wr, rd);
  (1) ? op_writedp(dp, wr) : op_io();
  return;
}

void SMP::op_a9() {
  sp = op_readpc();
  rd = op_readdp(sp);
  dp = op_readpc();
  wr = op_readdp(dp);
  wr = op_sbc(wr, rd);
  (1) ? op_writedp(dp, wr) : op_io();
  return;
}

void SMP::op_98() {
  rd = op_readpc();
  dp = op_readpc();
  wr = op_readdp(dp);
  wr = op_adc(wr, rd);
  (1) ? op_writedp(dp, wr) : op_io();
  return;
}

void SMP::op_38() {
  rd = op_readpc();
  dp = op_readpc();
  wr = op_readdp(dp);
  wr = op_and(wr, rd);
  (1) ? op_writedp(dp, wr) : op_io();
  return;
}

void SMP::op_78() {
  rd = op_readpc();
  dp = op_readpc();
  wr = op_readdp(dp);
  wr = op_cmp(wr, rd);
  (0) ? op_writedp(dp, wr) : op_io();
  return;
}

void SMP::op_58() {
  rd = op_readpc();
  dp = op_readpc();
  wr = op_readdp(dp);
  wr = op_eor(wr, rd);
  (1) ? op_writedp(dp, wr) : op_io();
  return;
}

void SMP::op_18() {
  rd = op_readpc();
  dp = op_readpc();
  wr = op_readdp(dp);
  wr = op_or(wr, rd);
  (1) ? op_writedp(dp, wr) : op_io();
  return;
}

void SMP::op_b8() {
  rd = op_readpc();
  dp = op_readpc();
  wr = op_readdp(dp);
  wr = op_sbc(wr, rd);
  (1) ? op_writedp(dp, wr) : op_io();
  return;
}

void SMP::op_7a() {
  dp  = op_readpc();
  rd  = op_readdp(dp);
  op_io();
  rd |= op_readdp(dp + 1) << 8;
  regs.ya = op_addw(regs.ya, rd);
  return;
}

void SMP::op_9a() {
  dp  = op_readpc();
  rd  = op_readdp(dp);
  op_io();
  rd |= op_readdp(dp + 1) << 8;
  regs.ya = op_subw(regs.ya, rd);
  return;
}

void SMP::op_5a() {
  dp  = op_readpc();
  rd  = op_readdp(dp);
  rd |= op_readdp(dp + 1) << 8;
  op_cmpw(regs.ya, rd);
  return;
}

void SMP::op_4a() {
  dp  = op_readpc();
  dp |= op_readpc() << 8;
  bit = dp >> 13;
  dp &= 0x1fff;
  rd = op_readaddr(dp);
  regs.p.c = regs.p.c & !!(rd & (1 << bit));
  return;
}

void SMP::op_6a() {
  dp  = op_readpc();
  dp |= op_readpc() << 8;
  bit = dp >> 13;
  dp &= 0x1fff;
  rd = op_readaddr(dp);
  regs.p.c = regs.p.c & !(rd & (1 << bit));
  return;
}

void SMP::op_8a() {
  dp  = op_readpc();
  dp |= op_readpc() << 8;
  bit = dp >> 13;
  dp &= 0x1fff;
  rd = op_readaddr(dp);
  op_io();
  regs.p.c = regs.p.c ^ !!(rd & (1 << bit));
  return;
}

void SMP::op_ea() {
  dp  = op_readpc();
  dp |= op_readpc() << 8;
  bit = dp >> 13;
  dp &= 0x1fff;
  rd = op_readaddr(dp);
  rd ^= (1 << bit);
  op_writeaddr(dp, rd);
  return;
}

void SMP::op_0a() {
  dp  = op_readpc();
  dp |= op_readpc() << 8;
  bit = dp >> 13;
  dp &= 0x1fff;
  rd = op_readaddr(dp);
  op_io();
  regs.p.c = regs.p.c | !!(rd & (1 << bit));
  return;
}

void SMP::op_2a() {
  dp  = op_readpc();
  dp |= op_readpc() << 8;
  bit = dp >> 13;
  dp &= 0x1fff;
  rd = op_readaddr(dp);
  op_io();
  regs.p.c = regs.p.c | !(rd & (1 << bit));
  return;
}

