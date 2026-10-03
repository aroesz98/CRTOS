void SMP::op_2f() {
  rd = op_readpc();
  if(0){ return; }
  op_io(2);
  regs.pc += (int8)rd;
  return;
}

void SMP::op_f0() {
  rd = op_readpc();
  if(!regs.p.z){ return; }
  op_io(2);
  regs.pc += (int8)rd;
  return;
}

void SMP::op_d0() {
  rd = op_readpc();
  if(regs.p.z){ return; }
  op_io(2);
  regs.pc += (int8)rd;
  return;
}

void SMP::op_b0() {
  rd = op_readpc();
  if(!regs.p.c){ return; }
  op_io(2);
  regs.pc += (int8)rd;
  return;
}

void SMP::op_90() {
  rd = op_readpc();
  if(regs.p.c){ return; }
  op_io(2);
  regs.pc += (int8)rd;
  return;
}

void SMP::op_70() {
  rd = op_readpc();
  if(!regs.p.v){ return; }
  op_io(2);
  regs.pc += (int8)rd;
  return;
}

void SMP::op_50() {
  rd = op_readpc();
  if(regs.p.v){ return; }
  op_io(2);
  regs.pc += (int8)rd;
  return;
}

void SMP::op_30() {
  rd = op_readpc();
  if(!regs.p.n){ return; }
  op_io(2);
  regs.pc += (int8)rd;
  return;
}

void SMP::op_10() {
  rd = op_readpc();
  if(regs.p.n){ return; }
  op_io(2);
  regs.pc += (int8)rd;
  return;
}

void SMP::op_03() {
  dp = op_readpc();
  sp = op_readdp(dp);
  rd = op_readpc();
  op_io();
  if((sp & 0x01) != 0x01){ return; }
  op_io(2);
  regs.pc += (int8)rd;
  return;
}

void SMP::op_13() {
  dp = op_readpc();
  sp = op_readdp(dp);
  rd = op_readpc();
  op_io();
  if((sp & 0x01) == 0x01){ return; }
  op_io(2);
  regs.pc += (int8)rd;
  return;
}

void SMP::op_23() {
  dp = op_readpc();
  sp = op_readdp(dp);
  rd = op_readpc();
  op_io();
  if((sp & 0x02) != 0x02){ return; }
  op_io(2);
  regs.pc += (int8)rd;
  return;
}

void SMP::op_33() {
  dp = op_readpc();
  sp = op_readdp(dp);
  rd = op_readpc();
  op_io();
  if((sp & 0x02) == 0x02){ return; }
  op_io(2);
  regs.pc += (int8)rd;
  return;
}

void SMP::op_43() {
  dp = op_readpc();
  sp = op_readdp(dp);
  rd = op_readpc();
  op_io();
  if((sp & 0x04) != 0x04){ return; }
  op_io(2);
  regs.pc += (int8)rd;
  return;
}

void SMP::op_53() {
  dp = op_readpc();
  sp = op_readdp(dp);
  rd = op_readpc();
  op_io();
  if((sp & 0x04) == 0x04){ return; }
  op_io(2);
  regs.pc += (int8)rd;
  return;
}

void SMP::op_63() {
  dp = op_readpc();
  sp = op_readdp(dp);
  rd = op_readpc();
  op_io();
  if((sp & 0x08) != 0x08){ return; }
  op_io(2);
  regs.pc += (int8)rd;
  return;
}

void SMP::op_73() {
  dp = op_readpc();
  sp = op_readdp(dp);
  rd = op_readpc();
  op_io();
  if((sp & 0x08) == 0x08){ return; }
  op_io(2);
  regs.pc += (int8)rd;
  return;
}

void SMP::op_83() {
  dp = op_readpc();
  sp = op_readdp(dp);
  rd = op_readpc();
  op_io();
  if((sp & 0x10) != 0x10){ return; }
  op_io(2);
  regs.pc += (int8)rd;
  return;
}

void SMP::op_93() {
  dp = op_readpc();
  sp = op_readdp(dp);
  rd = op_readpc();
  op_io();
  if((sp & 0x10) == 0x10){ return; }
  op_io(2);
  regs.pc += (int8)rd;
  return;
}

void SMP::op_a3() {
  dp = op_readpc();
  sp = op_readdp(dp);
  rd = op_readpc();
  op_io();
  if((sp & 0x20) != 0x20){ return; }
  op_io(2);
  regs.pc += (int8)rd;
  return;
}

void SMP::op_b3() {
  dp = op_readpc();
  sp = op_readdp(dp);
  rd = op_readpc();
  op_io();
  if((sp & 0x20) == 0x20){ return; }
  op_io(2);
  regs.pc += (int8)rd;
  return;
}

void SMP::op_c3() {
  dp = op_readpc();
  sp = op_readdp(dp);
  rd = op_readpc();
  op_io();
  if((sp & 0x40) != 0x40){ return; }
  op_io(2);
  regs.pc += (int8)rd;
  return;
}

void SMP::op_d3() {
  dp = op_readpc();
  sp = op_readdp(dp);
  rd = op_readpc();
  op_io();
  if((sp & 0x40) == 0x40){ return; }
  op_io(2);
  regs.pc += (int8)rd;
  return;
}

void SMP::op_e3() {
  dp = op_readpc();
  sp = op_readdp(dp);
  rd = op_readpc();
  op_io();
  if((sp & 0x80) != 0x80){ return; }
  op_io(2);
  regs.pc += (int8)rd;
  return;
}

void SMP::op_f3() {
  dp = op_readpc();
  sp = op_readdp(dp);
  rd = op_readpc();
  op_io();
  if((sp & 0x80) == 0x80){ return; }
  op_io(2);
  regs.pc += (int8)rd;
  return;
}

void SMP::op_2e() {
  dp = op_readpc();
  sp = op_readdp(dp);
  rd = op_readpc();
  op_io();
  if(regs.B.a == sp){ return; }
  op_io(2);
  regs.pc += (int8)rd;
  return;
}

void SMP::op_de() {
  dp = op_readpc();
  op_io();
  sp = op_readdp(dp + regs.x);
  rd = op_readpc();
  op_io();
  if(regs.B.a == sp){ return; }
  op_io(2);
  regs.pc += (int8)rd;
  return;
}

void SMP::op_6e() {
  dp = op_readpc();
  wr = op_readdp(dp);
  op_writedp(dp, --wr);
  rd = op_readpc();
  if(wr == 0x00){ return; }
  op_io(2);
  regs.pc += (int8)rd;
  return;
}

void SMP::op_fe() {
  rd = op_readpc();
  op_io();
  regs.B.y--;
  op_io();
  if(regs.B.y == 0x00){ return; }
  op_io(2);
  regs.pc += (int8)rd;
  return;
}

void SMP::op_5f() {
  rd  = op_readpc();
  rd |= op_readpc() << 8;
  regs.pc = rd;
  return;
}

void SMP::op_1f() {
  dp  = op_readpc();
  dp |= op_readpc() << 8;
  op_io();
  dp += regs.x;
  rd  = op_readaddr(dp);
  rd |= op_readaddr(dp + 1) << 8;
  regs.pc = rd;
  return;
}

void SMP::op_3f() {
  rd  = op_readpc();
  rd |= op_readpc() << 8;
  op_io(3);
  op_writestack(regs.pc >> 8);
  op_writestack(regs.pc);
  regs.pc = rd;
  return;
}

void SMP::op_4f() {
  rd = op_readpc();
  op_io(2);
  op_writestack(regs.pc >> 8);
  op_writestack(regs.pc);
  regs.pc = 0xff00 | rd;
  return;
}

void SMP::op_01() {
  dp = 0xffde - (0 << 1);
  rd  = op_readaddr(dp);
  rd |= op_readaddr(dp + 1) << 8;
  op_io(3);
  op_writestack(regs.pc >> 8);
  op_writestack(regs.pc);
  regs.pc = rd;
  return;
}

void SMP::op_11() {
  dp = 0xffde - (1 << 1);
  rd  = op_readaddr(dp);
  rd |= op_readaddr(dp + 1) << 8;
  op_io(3);
  op_writestack(regs.pc >> 8);
  op_writestack(regs.pc);
  regs.pc = rd;
  return;
}

void SMP::op_21() {
  dp = 0xffde - (2 << 1);
  rd  = op_readaddr(dp);
  rd |= op_readaddr(dp + 1) << 8;
  op_io(3);
  op_writestack(regs.pc >> 8);
  op_writestack(regs.pc);
  regs.pc = rd;
  return;
}

void SMP::op_31() {
  dp = 0xffde - (3 << 1);
  rd  = op_readaddr(dp);
  rd |= op_readaddr(dp + 1) << 8;
  op_io(3);
  op_writestack(regs.pc >> 8);
  op_writestack(regs.pc);
  regs.pc = rd;
  return;
}

void SMP::op_41() {
  dp = 0xffde - (4 << 1);
  rd  = op_readaddr(dp);
  rd |= op_readaddr(dp + 1) << 8;
  op_io(3);
  op_writestack(regs.pc >> 8);
  op_writestack(regs.pc);
  regs.pc = rd;
  return;
}

void SMP::op_51() {
  dp = 0xffde - (5 << 1);
  rd  = op_readaddr(dp);
  rd |= op_readaddr(dp + 1) << 8;
  op_io(3);
  op_writestack(regs.pc >> 8);
  op_writestack(regs.pc);
  regs.pc = rd;
  return;
}

void SMP::op_61() {
  dp = 0xffde - (6 << 1);
  rd  = op_readaddr(dp);
  rd |= op_readaddr(dp + 1) << 8;
  op_io(3);
  op_writestack(regs.pc >> 8);
  op_writestack(regs.pc);
  regs.pc = rd;
  return;
}

void SMP::op_71() {
  dp = 0xffde - (7 << 1);
  rd  = op_readaddr(dp);
  rd |= op_readaddr(dp + 1) << 8;
  op_io(3);
  op_writestack(regs.pc >> 8);
  op_writestack(regs.pc);
  regs.pc = rd;
  return;
}

void SMP::op_81() {
  dp = 0xffde - (8 << 1);
  rd  = op_readaddr(dp);
  rd |= op_readaddr(dp + 1) << 8;
  op_io(3);
  op_writestack(regs.pc >> 8);
  op_writestack(regs.pc);
  regs.pc = rd;
  return;
}

void SMP::op_91() {
  dp = 0xffde - (9 << 1);
  rd  = op_readaddr(dp);
  rd |= op_readaddr(dp + 1) << 8;
  op_io(3);
  op_writestack(regs.pc >> 8);
  op_writestack(regs.pc);
  regs.pc = rd;
  return;
}

void SMP::op_a1() {
  dp = 0xffde - (10 << 1);
  rd  = op_readaddr(dp);
  rd |= op_readaddr(dp + 1) << 8;
  op_io(3);
  op_writestack(regs.pc >> 8);
  op_writestack(regs.pc);
  regs.pc = rd;
  return;
}

void SMP::op_b1() {
  dp = 0xffde - (11 << 1);
  rd  = op_readaddr(dp);
  rd |= op_readaddr(dp + 1) << 8;
  op_io(3);
  op_writestack(regs.pc >> 8);
  op_writestack(regs.pc);
  regs.pc = rd;
  return;
}

void SMP::op_c1() {
  dp = 0xffde - (12 << 1);
  rd  = op_readaddr(dp);
  rd |= op_readaddr(dp + 1) << 8;
  op_io(3);
  op_writestack(regs.pc >> 8);
  op_writestack(regs.pc);
  regs.pc = rd;
  return;
}

void SMP::op_d1() {
  dp = 0xffde - (13 << 1);
  rd  = op_readaddr(dp);
  rd |= op_readaddr(dp + 1) << 8;
  op_io(3);
  op_writestack(regs.pc >> 8);
  op_writestack(regs.pc);
  regs.pc = rd;
  return;
}

void SMP::op_e1() {
  dp = 0xffde - (14 << 1);
  rd  = op_readaddr(dp);
  rd |= op_readaddr(dp + 1) << 8;
  op_io(3);
  op_writestack(regs.pc >> 8);
  op_writestack(regs.pc);
  regs.pc = rd;
  return;
}

void SMP::op_f1() {
  dp = 0xffde - (15 << 1);
  rd  = op_readaddr(dp);
  rd |= op_readaddr(dp + 1) << 8;
  op_io(3);
  op_writestack(regs.pc >> 8);
  op_writestack(regs.pc);
  regs.pc = rd;
  return;
}

void SMP::op_0f() {
  rd  = op_readaddr(0xffde);
  rd |= op_readaddr(0xffdf) << 8;
  op_io(2);
  op_writestack(regs.pc >> 8);
  op_writestack(regs.pc);
  op_writestack(regs.p);
  regs.pc = rd;
  regs.p.b = 1;
  regs.p.i = 0;
  return;
}

void SMP::op_6f() {
  rd  = op_readstack();
  rd |= op_readstack() << 8;
  op_io(2);
  regs.pc = rd;
  return;
}

void SMP::op_7f() {
  regs.p = op_readstack();
  rd  = op_readstack();
  rd |= op_readstack() << 8;
  op_io(2);
  regs.pc = rd;
  return;
}

