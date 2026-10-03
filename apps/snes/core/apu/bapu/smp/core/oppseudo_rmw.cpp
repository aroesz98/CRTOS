void SMP::op_bc() {
  op_io();
  regs.B.a = op_inc(regs.B.a);
  return;
}

void SMP::op_3d() {
  op_io();
  regs.x = op_inc(regs.x);
  return;
}

void SMP::op_fc() {
  op_io();
  regs.B.y = op_inc(regs.B.y);
  return;
}

void SMP::op_9c() {
  op_io();
  regs.B.a = op_dec(regs.B.a);
  return;
}

void SMP::op_1d() {
  op_io();
  regs.x = op_dec(regs.x);
  return;
}

void SMP::op_dc() {
  op_io();
  regs.B.y = op_dec(regs.B.y);
  return;
}

void SMP::op_1c() {
  op_io();
  regs.B.a = op_asl(regs.B.a);
  return;
}

void SMP::op_5c() {
  op_io();
  regs.B.a = op_lsr(regs.B.a);
  return;
}

void SMP::op_3c() {
  op_io();
  regs.B.a = op_rol(regs.B.a);
  return;
}

void SMP::op_7c() {
  op_io();
  regs.B.a = op_ror(regs.B.a);
    return;
}

void SMP::op_ab() {
  dp = op_readpc();
  rd = op_readdp(dp);
  rd = op_inc(rd);
  op_writedp(dp, rd);
  return;
}

void SMP::op_8b() {
  dp = op_readpc();
  rd = op_readdp(dp);
  rd = op_dec(rd);
  op_writedp(dp, rd);
  return;
}

void SMP::op_0b() {
  dp = op_readpc();
  rd = op_readdp(dp);
  rd = op_asl(rd);
  op_writedp(dp, rd);
  return;
}

void SMP::op_4b() {
  dp = op_readpc();
  rd = op_readdp(dp);
  rd = op_lsr(rd);
  op_writedp(dp, rd);
  return;
}

void SMP::op_2b() {
  dp = op_readpc();
  rd = op_readdp(dp);
  rd = op_rol(rd);
  op_writedp(dp, rd);
  return;
}

void SMP::op_6b() {
  dp = op_readpc();
  rd = op_readdp(dp);
  rd = op_ror(rd);
  op_writedp(dp, rd);
  return;
}

void SMP::op_bb() {
  dp = op_readpc();
  op_io();
  rd = op_readdp(dp + regs.x);
  rd = op_inc(rd);
  op_writedp(dp + regs.x, rd);
  return;
}

void SMP::op_9b() {
  dp = op_readpc();
  op_io();
  rd = op_readdp(dp + regs.x);
  rd = op_dec(rd);
  op_writedp(dp + regs.x, rd);
  return;
}

void SMP::op_1b() {
  dp = op_readpc();
  op_io();
  rd = op_readdp(dp + regs.x);
  rd = op_asl(rd);
  op_writedp(dp + regs.x, rd);
  return;
}

void SMP::op_5b() {
  dp = op_readpc();
  op_io();
  rd = op_readdp(dp + regs.x);
  rd = op_lsr(rd);
  op_writedp(dp + regs.x, rd);
  return;
}

void SMP::op_3b() {
  dp = op_readpc();
  op_io();
  rd = op_readdp(dp + regs.x);
  rd = op_rol(rd);
  op_writedp(dp + regs.x, rd);
  return;
}

void SMP::op_7b() {
  dp = op_readpc();
  op_io();
  rd = op_readdp(dp + regs.x);
  rd = op_ror(rd);
  op_writedp(dp + regs.x, rd);
  return;
}

void SMP::op_ac() {
  dp  = op_readpc();
  dp |= op_readpc() << 8;
  rd = op_readaddr(dp);
  rd = op_inc(rd);
  op_writeaddr(dp, rd);
  return;
}

void SMP::op_8c() {
  dp  = op_readpc();
  dp |= op_readpc() << 8;
  rd = op_readaddr(dp);
  rd = op_dec(rd);
  op_writeaddr(dp, rd);
  return;
}

void SMP::op_0c() {
  dp  = op_readpc();
  dp |= op_readpc() << 8;
  rd = op_readaddr(dp);
  rd = op_asl(rd);
  op_writeaddr(dp, rd);
  return;
}

void SMP::op_4c() {
  dp  = op_readpc();
  dp |= op_readpc() << 8;
  rd = op_readaddr(dp);
  rd = op_lsr(rd);
  op_writeaddr(dp, rd);
  return;
}

void SMP::op_2c() {
  dp  = op_readpc();
  dp |= op_readpc() << 8;
  rd = op_readaddr(dp);
  rd = op_rol(rd);
  op_writeaddr(dp, rd);
  return;
}

void SMP::op_6c() {
  dp  = op_readpc();
  dp |= op_readpc() << 8;
  rd = op_readaddr(dp);
  rd = op_ror(rd);
  op_writeaddr(dp, rd);
  return;
}

void SMP::op_0e() {
  dp  = op_readpc();
  dp |= op_readpc() << 8;
  rd = op_readaddr(dp);
  regs.p.n = !!((regs.B.a - rd) & 0x80);
  regs.p.z = ((regs.B.a - rd) == 0);
  op_readaddr(dp);
  op_writeaddr(dp, rd | regs.B.a);
  return;
}

void SMP::op_4e() {
  dp  = op_readpc();
  dp |= op_readpc() << 8;
  rd = op_readaddr(dp);
  regs.p.n = !!((regs.B.a - rd) & 0x80);
  regs.p.z = ((regs.B.a - rd) == 0);
  op_readaddr(dp);
  op_writeaddr(dp, rd &~ regs.B.a);
  return;
}

void SMP::op_3a() {
  dp = op_readpc();
  rd = op_readdp(dp);
  rd++;
  op_writedp(dp++, rd);
  rd += op_readdp(dp) << 8;
  op_writedp(dp, rd >> 8);
  regs.p.n = !!(rd & 0x8000);
  regs.p.z = (rd == 0);
  return;
}

void SMP::op_1a() {
  dp = op_readpc();
  rd = op_readdp(dp);
  rd--;
  op_writedp(dp++, rd);
  rd += op_readdp(dp) << 8;
  op_writedp(dp, rd >> 8);
  regs.p.n = !!(rd & 0x8000);
  regs.p.z = (rd == 0);
  return;
}

