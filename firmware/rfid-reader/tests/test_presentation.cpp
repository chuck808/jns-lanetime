#include "presentation.hpp"
#include <cassert>
int main() {
  const uint8_t a[7]={4,0,1,2,3,4,5},b[4]={1,2,3,4};
  Presentation p;
  assert(p.observe(a,7,0)); assert(p.size()==7); assert(p.uid()[1]==0);
  assert(!p.observe(a,7,100)); assert(!p.observe(b,4,200));
  p.absent(300);p.absent(700);assert(!p.observe(a,7,710));
  p.absent(800);p.uncertain();p.absent(1400);assert(!p.observe(a,7,1410));
  p.absent(1500);p.absent(2000);assert(p.observe(a,7,2010));
  assert(!p.observe(a,3,2020));
  p.absent(0xffffff00u);p.absent(0x100u);assert(p.observe(b,4,0x110u));
}
