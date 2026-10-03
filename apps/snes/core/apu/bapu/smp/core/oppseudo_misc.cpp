void SMP::op_00() {
  op_io();
  return;
}

void SMP::op_ef() {
  op_io(2);
  regs.pc--;
  return;
}

void SMP::op_ff() {
  op_io(2);
  regs.pc--;
  return;
}

void SMP::op_9f() {
  op_io(4);
  regs.B.a = (regs.B.a >> 4) | (regs.B.a << 4);
  regs.p.n = !!(regs.B.a & 0x80);
  regs.p.z = (regs.B.a == 0);
  return;
}

void SMP::op_df() {
  op_io(2);
  if(regs.p.c || (regs.B.a) > 0x99) {
    regs.B.a += 0x60;
    regs.p.c = 1;
  }
  if(regs.p.h || (regs.B.a & 15) > 0x09) {
    regs.B.a += 0x06;
  }
  regs.p.n = !!(regs.B.a & 0x80);
  regs.p.z = (regs.B.a == 0);
  return;
}

void SMP::op_be() {
  op_io(2);
  if(!regs.p.c || (regs.B.a) > 0x99) {
    regs.B.a -= 0x60;
    regs.p.c = 0;
  }
  if(!regs.p.h || (regs.B.a & 15) > 0x09) {
    regs.B.a -= 0x06;
  }
  regs.p.n = !!(regs.B.a & 0x80);
  regs.p.z = (regs.B.a == 0);
  return;
}

void SMP::op_60() {
  op_io();
  regs.p.c = 0;
  return;
}

void SMP::op_20() {
  op_io();
  regs.p.p = 0;
  return;
}

void SMP::op_80() {
  op_io();
  regs.p.c = 1;
  return;
}

void SMP::op_40() {
  op_io();
  regs.p.p = 1;
  return;
}

void SMP::op_e0() {
  op_io();
  regs.p.v = 0;
  regs.p.h = 0;
  return;
}

void SMP::op_ed() {
  op_io(2);
  regs.p.c = !regs.p.c;
  return;
}

void SMP::op_a0() {
  op_io(2);
  regs.p.i = 1;
  return;
}

void SMP::op_c0() {
  op_io(2);
  regs.p.i = 0;
  return;
}

void SMP::op_02() {
  dp = op_readpc();
  rd = op_readdp(dp);
  rd |=  0x01;
  op_writedp(dp, rd);
  return;
}

void SMP::op_12() {
  dp = op_readpc();
  rd = op_readdp(dp);
  rd &= ~0x01;
  op_writedp(dp, rd);
  return;
}

void SMP::op_22() {
  dp = op_readpc();
  rd = op_readdp(dp);
  rd |=  0x02;
  op_writedp(dp, rd);
  return;
}

void SMP::op_32() {
  dp = op_readpc();
  rd = op_readdp(dp);
  rd &= ~0x02;
  op_writedp(dp, rd);
  return;
}

void SMP::op_42() {
  dp = op_readpc();
  rd = op_readdp(dp);
  rd |=  0x04;
  op_writedp(dp, rd);
  return;
}

void SMP::op_52() {
  dp = op_readpc();
  rd = op_readdp(dp);
  rd &= ~0x04;
  op_writedp(dp, rd);
  return;
}

void SMP::op_62() {
  dp = op_readpc();
  rd = op_readdp(dp);
  rd |=  0x08;
  op_writedp(dp, rd);
  return;
}

void SMP::op_72() {
  dp = op_readpc();
  rd = op_readdp(dp);
  rd &= ~0x08;
  op_writedp(dp, rd);
  return;
}

void SMP::op_82() {
  dp = op_readpc();
  rd = op_readdp(dp);
  rd |=  0x10;
  op_writedp(dp, rd);
  return;
}

void SMP::op_92() {
  dp = op_readpc();
  rd = op_readdp(dp);
  rd &= ~0x10;
  op_writedp(dp, rd);
  return;
}

void SMP::op_a2() {
  dp = op_readpc();
  rd = op_readdp(dp);
  rd |=  0x20;
  op_writedp(dp, rd);
  return;
}

void SMP::op_b2() {
  dp = op_readpc();
  rd = op_readdp(dp);
  rd &= ~0x20;
  op_writedp(dp, rd);
  return;
}

void SMP::op_c2() {
  dp = op_readpc();
  rd = op_readdp(dp);
  rd |=  0x40;
  op_writedp(dp, rd);
  return;
}

void SMP::op_d2() {
  dp = op_readpc();
  rd = op_readdp(dp);
  rd &= ~0x40;
  op_writedp(dp, rd);
  return;
}

void SMP::op_e2() {
  dp = op_readpc();
  rd = op_readdp(dp);
  rd |=  0x80;
  op_writedp(dp, rd);
  return;
}

void SMP::op_f2() {
  dp = op_readpc();
  rd = op_readdp(dp);
  rd &= ~0x80;
  op_writedp(dp, rd);
  return;
}

void SMP::op_2d() {
  op_io(2);
  op_writestack(regs.B.a);
  return;
}

void SMP::op_4d() {
  op_io(2);
  op_writestack(regs.x);
  return;
}

void SMP::op_6d() {
  op_io(2);
  op_writestack(regs.B.y);
  return;
}

void SMP::op_0d() {
  op_io(2);
  op_writestack(regs.p);
  return;
}

void SMP::op_ae() {
  op_io(2);
  regs.B.a = op_readstack();
  return;
}

void SMP::op_ce() {
  op_io(2);
  regs.x = op_readstack();
  return;
}

void SMP::op_ee() {
  op_io(2);
  regs.B.y = op_readstack();
  return;
}

void SMP::op_8e() {
  op_io(2);
  regs.p = op_readstack();
  return;
}

void SMP::op_cf() {
  op_io(8);
  ya = regs.B.y * regs.B.a;
  regs.B.a = ya;
  regs.B.y = ya >> 8;
  //result is set based on y (high-byte) only
  regs.p.n = !!(regs.B.y & 0x80);
  regs.p.z = (regs.B.y == 0);
  return;
}

void SMP::op_9e() {
  op_io(11);
  ya = regs.ya;
  //overflow set if quotient >= 256
  regs.p.v = !!(regs.B.y >= regs.x);
  regs.p.h = !!((regs.B.y & 15) >= (regs.x & 15));
  if(regs.B.y < (regs.x << 1)) {
    //if quotient is <= 511 (will fit into 9-bit result)
    regs.B.a = ya / regs.x;
    regs.B.y = ya % regs.x;
  } else {
    //otherwise, the quotient won't fit into regs.p.v + regs.B.a
    //this emulates the odd behavior of the S-SMP in this case
    regs.B.a = 255    - (ya - (regs.x << 9)) / (256 - regs.x);
    regs.B.y = regs.x + (ya - (regs.x << 9)) % (256 - regs.x);
  }
  //result is set based on a (quotient) only
  regs.p.n = !!(regs.B.a & 0x80);
  regs.p.z = (regs.B.a == 0);
  return;
}

