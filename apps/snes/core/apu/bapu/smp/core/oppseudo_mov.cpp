void SMP::op_7d() {
  op_io();
  regs.B.a = regs.x;
  regs.p.n = !!(regs.B.a & 0x80);
  regs.p.z = (regs.B.a == 0);
  return;
}

void SMP::op_dd() {
  op_io();
  regs.B.a = regs.B.y;
  regs.p.n = !!(regs.B.a & 0x80);
  regs.p.z = (regs.B.a == 0);
  return;
}

void SMP::op_5d() {
  op_io();
  regs.x = regs.B.a;
  regs.p.n = !!(regs.x & 0x80);
  regs.p.z = (regs.x == 0);
  return;
}

void SMP::op_fd() {
  op_io();
  regs.B.y = regs.B.a;
  regs.p.n = !!(regs.B.y & 0x80);
  regs.p.z = (regs.B.y == 0);
  return;
}

void SMP::op_9d() {
  op_io();
  regs.x = regs.sp;
  regs.p.n = !!(regs.x & 0x80);
  regs.p.z = (regs.x == 0);
  return;
}

void SMP::op_bd() {
  op_io();
  regs.sp = regs.x;
  return;
}

void SMP::op_e8() {
  regs.B.a = op_readpc();
  regs.p.n = !!(regs.B.a & 0x80);
  regs.p.z = (regs.B.a == 0);
  return;
}

void SMP::op_cd() {
  regs.x = op_readpc();
  regs.p.n = !!(regs.x & 0x80);
  regs.p.z = (regs.x == 0);
  return;
}

void SMP::op_8d() {
  regs.B.y = op_readpc();
  regs.p.n = !!(regs.B.y & 0x80);
  regs.p.z = (regs.B.y == 0);
  return;
}

void SMP::op_e6() {
  switch(++opcode_cycle) {
  case 1:
    op_io();
    break;
  case 2:
    regs.B.a = op_readdp(regs.x);
    regs.p.n = !!(regs.B.a & 0x80);
    regs.p.z = (regs.B.a == 0);
    opcode_cycle = 0;
    break;
  }
  return;
}

void SMP::op_bf() {
  switch(++opcode_cycle) {
  case 1:
    op_io();
    break;
  case 2:
    regs.B.a = op_readdp(regs.x++);
    op_io();
    regs.p.n = !!(regs.B.a & 0x80);
    regs.p.z = (regs.B.a == 0);
    opcode_cycle = 0;
    break;
  }
  return;
}

void SMP::op_e4() {
  switch(++opcode_cycle) {
  case 1:
    sp = op_readpc();
    break;
  case 2:
    regs.B.a = op_readdp(sp);
    regs.p.n = !!(regs.B.a & 0x80);
    regs.p.z = (regs.B.a == 0);
    opcode_cycle = 0;
    break;
  }
  return;
}

void SMP::op_f8() {
  switch(++opcode_cycle) {
  case 1:
    sp = op_readpc();
    break;
  case 2:
    regs.x = op_readdp(sp);
    regs.p.n = !!(regs.x & 0x80);
    regs.p.z = (regs.x == 0);
    opcode_cycle = 0;
    break;
  }
  return;
}

void SMP::op_eb() {
  switch(++opcode_cycle) {
  case 1:
    sp = op_readpc();
    break;
  case 2:
    regs.B.y = op_readdp(sp);
    regs.p.n = !!(regs.B.y & 0x80);
    regs.p.z = (regs.B.y == 0);
    opcode_cycle = 0;
    break;
  }
  return;
}

void SMP::op_f4() {
  switch(++opcode_cycle) {
  case 1:
    sp = op_readpc();
    op_io();
    break;
  case 2:
    regs.B.a = op_readdp(sp + regs.x);
    regs.p.n = !!(regs.B.a & 0x80);
    regs.p.z = (regs.B.a == 0);
    opcode_cycle = 0;
    break;
  }
  return;
}

void SMP::op_f9() {
  switch(++opcode_cycle) {
  case 1:
    sp = op_readpc();
    op_io();
    break;
  case 2:
    regs.x = op_readdp(sp + regs.B.y);
    regs.p.n = !!(regs.x & 0x80);
    regs.p.z = (regs.x == 0);
    opcode_cycle = 0;
    break;
  }
  return;
}

void SMP::op_fb() {
  switch(++opcode_cycle) {
  case 1:
    sp = op_readpc();
    op_io();
    break;
  case 2:
    regs.B.y = op_readdp(sp + regs.x);
    regs.p.n = !!(regs.B.y & 0x80);
    regs.p.z = (regs.B.y == 0);
    opcode_cycle = 0;
    break;
  }
  return;
}

void SMP::op_e5() {
  switch(++opcode_cycle) {
  case 1:
    sp  = op_readpc();
    break;
  case 2:
    sp |= op_readpc() << 8;
    break;
  case 3:
    regs.B.a = op_readaddr(sp);
    regs.p.n = !!(regs.B.a & 0x80);
    regs.p.z = (regs.B.a == 0);
    opcode_cycle = 0;
    break;
  }
  return;
}

void SMP::op_e9() {
  switch(++opcode_cycle) {
  case 1:
    sp  = op_readpc();
    sp |= op_readpc() << 8;
    break;
  case 2:
    regs.x = op_readaddr(sp);
    regs.p.n = !!(regs.x & 0x80);
    regs.p.z = (regs.x == 0);
    opcode_cycle = 0;
    break;
  }
  return;
}

void SMP::op_ec() {
  switch(++opcode_cycle) {
  case 1:
    sp  = op_readpc();
    sp |= op_readpc() << 8;
    break;
  case 2:
    regs.B.y = op_readaddr(sp);
    regs.p.n = !!(regs.B.y & 0x80);
    regs.p.z = (regs.B.y == 0);
    opcode_cycle = 0;
    break;
  }
  return;
}

void SMP::op_f5() {
  switch(++opcode_cycle) {
  case 1:
    sp  = op_readpc();
    sp |= op_readpc() << 8;
    op_io();
    break;
  case 2:
    regs.B.a = op_readaddr(sp + regs.x);
    regs.p.n = !!(regs.B.a & 0x80);
    regs.p.z = (regs.B.a == 0);
    opcode_cycle = 0;
    break;
  }
  return;
}

void SMP::op_f6() {
  switch(++opcode_cycle) {
  case 1:
    sp  = op_readpc();
    sp |= op_readpc() << 8;
    op_io();
    break;
  case 2:
    regs.B.a = op_readaddr(sp + regs.B.y);
    regs.p.n = !!(regs.B.a & 0x80);
    regs.p.z = (regs.B.a == 0);
    opcode_cycle = 0;
    break;
  }
  return;
}

void SMP::op_e7() {
  switch(++opcode_cycle) {
  case 1:
    dp = op_readpc() + regs.x;
    op_io();
    break;
  case 2:
    sp  = op_readdp(dp);
    break;
  case 3:
    sp |= op_readdp(dp + 1) << 8;
    break;
  case 4:
    regs.B.a = op_readaddr(sp);
    regs.p.n = !!(regs.B.a & 0x80);
    regs.p.z = (regs.B.a == 0);
    opcode_cycle = 0;
    break;
  }
  return;
}

void SMP::op_f7() {
  switch(++opcode_cycle) {
  case 1:
    dp = op_readpc();
    op_io();
    break;
  case 2:
    sp  = op_readdp(dp);
    break;
  case 3:
    sp |= op_readdp(dp + 1) << 8;
    break;
  case 4:
    regs.B.a = op_readaddr(sp + regs.B.y);
    regs.p.n = !!(regs.B.a & 0x80);
    regs.p.z = (regs.B.a == 0);
    opcode_cycle = 0;
    break;
  }
  return;
}

void SMP::op_fa() {
  switch(++opcode_cycle) {
  case 1:
    sp = op_readpc();
    break;
  case 2:
    rd = op_readdp(sp);
    break;
  case 3:
    dp = op_readpc();
    break;
  case 4:
    op_writedp(dp, rd);
    opcode_cycle = 0;
    break;
  }
  return;
}

void SMP::op_8f() {
  switch(++opcode_cycle) {
  case 1:
    rd = op_readpc();
    dp = op_readpc();
    break;
  case 2:
    op_readdp(dp);
    break;
  case 3:
    op_writedp(dp, rd);
    opcode_cycle = 0;
    break;
  }
  return;
}

void SMP::op_c6() {
  switch(++opcode_cycle) {
  case 1:
    op_io();
    break;
  case 2:
    op_readdp(regs.x);
    break;
  case 3:
    op_writedp(regs.x, regs.B.a);
    opcode_cycle = 0;
    break;
  }
  return;
}

void SMP::op_af() {
  switch(++opcode_cycle) {
  case 1:
    op_io(2);
    break;
  case 2:
    op_writedp(regs.x++, regs.B.a);
    opcode_cycle = 0;
    break;
  }
  return;
}

void SMP::op_c4() {
  switch(++opcode_cycle) {
  case 1:
    dp = op_readpc();
    break;
  case 2:
    op_readdp(dp);
    break;
  case 3:
    op_writedp(dp, regs.B.a);
    opcode_cycle = 0;
    break;
  }
  return;
}

void SMP::op_d8() {
  switch(++opcode_cycle) {
  case 1:
    dp = op_readpc();
    break;
  case 2:
    op_readdp(dp);
    break;
  case 3:
    op_writedp(dp, regs.x);
    opcode_cycle = 0;
    break;
  }
  return;
}

void SMP::op_cb() {
  switch(++opcode_cycle) {
  case 1:
    dp = op_readpc();
    break;
  case 2:
    op_readdp(dp);
    break;
  case 3:
    op_writedp(dp, regs.B.y);
    opcode_cycle = 0;
    break;
  }
  return;
}

void SMP::op_d4() {
  switch(++opcode_cycle) {
  case 1:
    dp  = op_readpc();
    op_io();
    dp += regs.x;
    break;
  case 2:
    op_readdp(dp);
    break;
  case 3:
    op_writedp(dp, regs.B.a);
    opcode_cycle = 0;
    break;
  }
  return;
}

void SMP::op_d9() {
  switch(++opcode_cycle) {
  case 1:
    dp  = op_readpc();
    op_io();
    dp += regs.B.y;
    break;
  case 2:
    op_readdp(dp);
    break;
  case 3:
    op_writedp(dp, regs.x);
    opcode_cycle = 0;
    break;
  }
  return;
}

void SMP::op_db() {
  switch(++opcode_cycle) {
  case 1:
    dp  = op_readpc();
    op_io();
    dp += regs.x;
    break;
  case 2:
    op_readdp(dp);
    break;
  case 3:
    op_writedp(dp, regs.B.y);
    opcode_cycle = 0;
    break;
  }
  return;
}

void SMP::op_c5() {
  switch(++opcode_cycle) {
  case 1:
    dp  = op_readpc();
    break;
  case 2:
    dp |= op_readpc() << 8;
    break;
  case 3:
    op_readaddr(dp);
    break;
  case 4:
    op_writeaddr(dp, regs.B.a);
    opcode_cycle = 0;
    break;
  }
  return;
}

void SMP::op_c9() {
  switch(++opcode_cycle) {
  case 1:
    dp  = op_readpc();
    break;
  case 2:
    dp |= op_readpc() << 8;
    break;
  case 3:
    op_readaddr(dp);
    break;
  case 4:
    op_writeaddr(dp, regs.x);
    opcode_cycle = 0;
    break;
  }
  return;
}

void SMP::op_cc() {
  switch(++opcode_cycle) {
  case 1:
    dp  = op_readpc();
    break;
  case 2:
    dp |= op_readpc() << 8;
    break;
  case 3:
    op_readaddr(dp);
    break;
  case 4:
    op_writeaddr(dp, regs.B.y);
    opcode_cycle = 0;
    break;
  }
  return;
}

void SMP::op_d5() {
  switch(++opcode_cycle) {
  case 1:
    dp  = op_readpc();
    dp |= op_readpc() << 8;
    op_io();
    dp += regs.x;
    break;
  case 2:
    op_readaddr(dp);
    break;
  case 3:
    op_writeaddr(dp, regs.B.a);
    opcode_cycle = 0;
    break;
  }
  return;
}

void SMP::op_d6() {
  switch(++opcode_cycle) {
  case 1:
    dp  = op_readpc();
    dp |= op_readpc() << 8;
    op_io();
    dp += regs.B.y;
    break;
  case 2:
    op_readaddr(dp);
    break;
  case 3:
    op_writeaddr(dp, regs.B.a);
    opcode_cycle = 0;
    break;
  }
  return;
}

void SMP::op_c7() {
  switch(++opcode_cycle) {
  case 1:
    sp  = op_readpc();
    op_io();
    sp += regs.x;
    break;
  case 2:
    dp  = op_readdp(sp);
    break;
  case 3:
    dp |= op_readdp(sp + 1) << 8;
    break;
  case 4:
    op_readaddr(dp);
    break;
  case 5:
    op_writeaddr(dp, regs.B.a);
    opcode_cycle = 0;
    break;
  }
  return;
}

void SMP::op_d7() {
  switch(++opcode_cycle) {
  case 1:
    sp  = op_readpc();
    break;
  case 2:
    dp  = op_readdp(sp);
    break;
  case 3:
    dp |= op_readdp(sp + 1) << 8;
    op_io();
    dp += regs.B.y;
    break;
  case 4:
    op_readaddr(dp);
    break;
  case 5:
    op_writeaddr(dp, regs.B.a);
    opcode_cycle = 0;
    break;
  }
  return;
}

void SMP::op_ba() {
  switch(++opcode_cycle) {
  case 1:
    sp = op_readpc();
    break;
  case 2:
    regs.B.a = op_readdp(sp);
    op_io();
    break;
  case 3:
    regs.B.y = op_readdp(sp + 1);
    regs.p.n = !!(regs.ya & 0x8000);
    regs.p.z = (regs.ya == 0);
    opcode_cycle = 0;
    break;
  }
  return;
}

void SMP::op_da() {
  switch(++opcode_cycle) {
  case 1:
    dp = op_readpc();
    break;
  case 2:
    op_readdp(dp);
    break;
  case 3:
    op_writedp(dp,     regs.B.a);
    break;
  case 4:
    op_writedp(dp + 1, regs.B.y);
    opcode_cycle = 0;
    break;
  }
  return;
}

void SMP::op_aa() {
  switch(++opcode_cycle) {
  case 1:
    sp  = op_readpc();
    sp |= op_readpc() << 8;
    break;
  case 2:
    bit = sp >> 13;
    sp &= 0x1fff;
    rd = op_readaddr(sp);
    regs.p.c = !!(rd & (1 << bit));
    opcode_cycle = 0;
    break;
  }
  return;
}

void SMP::op_ca() {
  switch(++opcode_cycle) {
  case 1:
    dp  = op_readpc();
    dp |= op_readpc() << 8;
    break;
  case 2:
    bit = dp >> 13;
    dp &= 0x1fff;
    rd = op_readaddr(dp);
    if(regs.p.c)rd |=  (1 << bit);
    else        rd &= ~(1 << bit);
    op_io();
    break;
  case 3:
    op_writeaddr(dp, rd);
    opcode_cycle = 0;
    break;
  }
  return;
}

