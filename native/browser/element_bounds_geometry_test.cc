// SPDX-License-Identifier: MIT
#include "element_bounds_geometry.h"
#include <cassert>
#include <iostream>
#include <limits>
using Quad=std::array<double,8>;
void Near(const Quad&actual,const Quad&expected){for(std::size_t i=0;i<actual.size();++i)assert(std::abs(actual[i]-expected[i])<1e-8);}
int main(){
 int checks=0;Quad out{};
 const Quad rect{20,30,80,30,80,55,20,55};
 assert(suitecut::MapElementBoundsQuad(rect,{0,0,160,0,160,140,0,140},160,140,out));Near(out,rect);++checks;
 assert(suitecut::MapElementBoundsQuad(rect,{107,57,267,57,267,197,107,197},160,140,out));Near(out,{127,87,187,87,187,112,127,112});++checks;
 assert(suitecut::MapElementBoundsQuad(rect,{0,0,320,0,320,420,0,420},160,140,out));Near(out,{40,90,160,90,160,165,40,165});++checks;
 assert(suitecut::MapElementBoundsQuad(rect,{0,0,0,160,-140,160,-140,0},160,140,out));Near(out,{-30,20,-30,80,-55,80,-55,20});++checks;
 assert(suitecut::MapElementBoundsQuad({0,0,100,0,100,100,0,100},{10,20,220,40,180,210,-20,170},100,100,out));Near(out,{10,20,220,40,180,210,-20,170});++checks;
 assert(suitecut::MapElementBoundsQuad({-20,-30,80,-30,80,55,-20,55},{107,57,267,57,267,197,107,197},160,140,out));Near(out,{87,27,187,27,187,112,87,112});++checks;
 for(const auto invalid:{0.0,-1.0,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()}){assert(!suitecut::MapElementBoundsQuad(rect,rect,invalid,140,out));}++checks;
 auto invalid=rect;invalid[0]=std::numeric_limits<double>::infinity();const auto before=out;assert(!suitecut::MapElementBoundsQuad(invalid,rect,160,140,out));assert(out==before);++checks;
 invalid=rect;invalid[1]=std::numeric_limits<double>::quiet_NaN();assert(!suitecut::MapElementBoundsQuad(rect,invalid,160,140,out));assert(out==before);++checks;
 assert(!suitecut::MapElementBoundsQuad(rect,{0,0,10,10,20,20,30,30},160,140,out));assert(out==before);++checks;
 std::cout<<"Native element quad mapping "<<checks<<" controlled groups PASS\n";
}
