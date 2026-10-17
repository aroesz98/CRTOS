template<unsigned cycle_frequency>
void SMP::Timer<cycle_frequency>::tick() {
  if(++stage1_ticks < cycle_frequency) return;

  stage1_ticks = 0;
  if(enable == false) return;

  if(++stage2_ticks != target) return;

  stage2_ticks = 0;
  stage3_ticks = (stage3_ticks + 1) & 15;
}

//CRTOS: any number of cycles at once, the same as that many tick()s
template<unsigned cycle_frequency>
void SMP::Timer<cycle_frequency>::tick_bulk(unsigned clocks) {
  unsigned t = stage1_ticks + clocks;
  if(t < cycle_frequency) { stage1_ticks = t; return; }
  unsigned n = t / cycle_frequency;
  stage1_ticks = t % cycle_frequency;
  if(enable == false) return;

  //stage 2 counts up until ++stage2_ticks == target (8 bits, so target 0 means 256)
  unsigned d = (uint8)(target - stage2_ticks);
  if(d == 0) d = 256;
  if(n < d) { stage2_ticks += n; return; }
  n -= d;
  unsigned period = target ? target : 256;
  stage2_ticks = n % period;
  stage3_ticks = (stage3_ticks + 1 + n / period) & 15;
}

template<unsigned cycle_frequency>
void SMP::Timer<cycle_frequency>::tick(unsigned clocks) {
  stage1_ticks += clocks;
  if(stage1_ticks < cycle_frequency) return;

  stage1_ticks -= cycle_frequency;
  if(enable == false) return;

  if(++stage2_ticks != target) return;

  stage2_ticks = 0;
  stage3_ticks = (stage3_ticks + 1) & 15;
}
