unsigned SMP::port_read(unsigned addr) {
  return apuram[0xf4 + (addr & 3)];
}

void SMP::port_write(unsigned addr, unsigned data) {
  apuram[0xf4 + (addr & 3)] = data;
}

//CRTOS: credits the cycles since @mark to the timers and the DSP - before anything looks at
//them, so the results are the same as ticking them every cycle
void SMP::catch_up() {
  int32 n = clock - mark;
  if(n <= 0) return;
  mark = clock;
  dsp.clock += n;
  timer0.tick_bulk(n);
  timer1.tick_bulk(n);
  timer2.tick_bulk(n);
}

unsigned SMP::mmio_read(unsigned addr) {
  if(addr == 0xf3 || addr >= 0xfd) catch_up();  //CRTOS
  switch(addr) {

  case 0xf2:
    return status.dsp_addr;

  case 0xf3:
    return dsp.read(status.dsp_addr & 0x7f);

  case 0xf4:
  case 0xf5:
  case 0xf6:
  case 0xf7:
    return cpu.port_read(addr);

  case 0xf8:
    return status.ram00f8;

  case 0xf9:
    return status.ram00f9;

  case 0xfd: {
    unsigned result = timer0.stage3_ticks & 15;
    timer0.stage3_ticks = 0;
    return result;
  }

  case 0xfe: {
    unsigned result = timer1.stage3_ticks & 15;
    timer1.stage3_ticks = 0;
    return result;
  }

  case 0xff: {
    unsigned result = timer2.stage3_ticks & 15;
    timer2.stage3_ticks = 0;
    return result;
  }

  }

  return 0x00;
}

void SMP::mmio_write(unsigned addr, unsigned data) {
  if(addr == 0xf1 || addr == 0xf3 || addr >= 0xfa) catch_up();  //CRTOS
  switch(addr) {

  case 0xf1:
    status.iplrom_enable = data & 0x80;

    if(data & 0x30) {
      if(data & 0x20) {
        cpu.port_write(3, 0x00);
        cpu.port_write(2, 0x00);
      }
      if(data & 0x10) {
        cpu.port_write(1, 0x00);
        cpu.port_write(0, 0x00);
      }
    }

    if(timer2.enable == false && (data & 0x04)) {
      timer2.stage2_ticks = 0;
      timer2.stage3_ticks = 0;
    }
    timer2.enable = data & 0x04;

    if(timer1.enable == false && (data & 0x02)) {
      timer1.stage2_ticks = 0;
      timer1.stage3_ticks = 0;
    }
    timer1.enable = data & 0x02;

    if(timer0.enable == false && (data & 0x01)) {
      timer0.stage2_ticks = 0;
      timer0.stage3_ticks = 0;
    }
    timer0.enable = data & 0x01;

    break;

  case 0xf2:
    status.dsp_addr = data;
    break;

  case 0xf3:
    if(status.dsp_addr & 0x80) break;
    dsp.write(status.dsp_addr, data);
    break;

  case 0xf4:
  case 0xf5:
  case 0xf6:
  case 0xf7:
    port_write(addr, data);
    break;

  case 0xf8:
    status.ram00f8 = data;
    break;

  case 0xf9:
    status.ram00f9 = data;
    break;

  case 0xfa:
    timer0.target = data;
    break;

  case 0xfb:
    timer1.target = data;
    break;

  case 0xfc:
    timer2.target = data;
    break;
  }
}
