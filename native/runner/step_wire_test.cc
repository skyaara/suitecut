// SPDX-License-Identifier: MIT
#include "step_wire.h"
#include <cassert>
#include <iostream>
using namespace suitecut::runner;
int main(){assert(StepWire("\"\"")=="-");assert(StepWire("\"a\"")=="6100");assert(StepWire("\"\\ud800\\u0000\"")=="00d80000");assert(StepWire("\"🙂\"")=="3dd842de");assert(StepWire("\"\\n\\t\\b\\f\\r\\\"\\\\\\/\"")=="0a00090008000c000d0022005c002f00");std::string wire;for(unsigned i=0;i<8192;++i)wire+="ffdf";assert(StepWire(RuntimeSkipReason(wire))==wire&&wire.size()==32768);bool invalid=false;try{StepWire("not JSON");}catch(const std::invalid_argument&){invalid=true;}assert(invalid);std::cout<<"PASS7 native step compact UTF16 wire scalar groups\n";}
