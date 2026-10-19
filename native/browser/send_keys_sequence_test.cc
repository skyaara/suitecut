// SPDX-License-Identifier: MIT
#include <cassert>
#include <string>
#include "send_keys_sequence.h"
static std::string UTF8(uint32_t point){std::string value;if(point<128)value+=static_cast<char>(point);else if(point<2048){value+=static_cast<char>(0xc0|(point>>6));value+=static_cast<char>(0x80|(point&63));}else{value+=static_cast<char>(0xe0|(point>>12));value+=static_cast<char>(0x80|((point>>6)&63));value+=static_cast<char>(0x80|(point&63));}return value;}
int main(){
  using Kind=SuiteCutSendKeysToken::Kind;
  assert(SuiteCutNextSendKeysToken("",0,0).kind==Kind::End);
  assert(SuiteCutNextSendKeysToken("abc",4,0).kind==Kind::Invalid);
  for(const std::string& invalid:{std::string("\xc0\xaf",2),std::string("\xed\xa0\x80",3),std::string("\xf4\x90\x80\x80",4),std::string("\xe0\x80\x80",3),std::string("\xf0\x9f",2)})assert(SuiteCutNextSendKeysToken(invalid,0,0).kind==Kind::Invalid);
  assert(SuiteCutNextSendKeysToken(UTF8(0xe000),0,0).kind==Kind::ReleaseModifiers);
  const auto left=SuiteCutNextSendKeysToken(UTF8(0xe008),0,0),right=SuiteCutNextSendKeysToken(UTF8(0xe050),0,0);
  assert(left.key.modifier==8&&left.key.code=="ShiftLeft"&&left.key.location==1);
  assert(right.key.modifier==8&&right.key.code=="ShiftRight"&&right.key.location==2);
  for(uint32_t point=0xe01a;point<=0xe023;++point){const auto key=SuiteCutNextSendKeysToken(UTF8(point),0,0);assert(key.kind==Kind::Key&&key.key.location==3&&key.key.key==std::to_string(point-0xe01a)&&key.key.text==key.key.key);}
  assert(SuiteCutNextSendKeysToken(UTF8(0xe028),0,0).key.key==".");
  assert(SuiteCutNextSendKeysToken(UTF8(0xe056),0,0).key.key=="End");
  assert(SuiteCutNextSendKeysToken(UTF8(0xe056),0,0).key.code=="Numpad1");
  for(uint32_t point=0xe031;point<=0xe03c;++point)assert(SuiteCutNextSendKeysToken(UTF8(point),0,0).key.key=="F"+std::to_string(point-0xe030));
  assert(SuiteCutNextSendKeysToken(UTF8(0xe01b),0,2).key.text.empty());
  assert(SuiteCutNextSendKeysToken("a",0,8).key.text=="A");
  assert(SuiteCutNextSendKeysToken("!",0,8).key.code=="Digit1");
  assert(SuiteCutSendKeysShifted("!")&&SuiteCutSendKeysShifted("Z")&&!SuiteCutSendKeysShifted("a"));
  assert(SuiteCutNextSendKeysToken(UTF8(0xe100),0,0).kind==Kind::Text);
  assert(SuiteCutNextSendKeysToken("\xf0\x9f\xa6\xa5",0,0).bytes==4);
  // Existing ordinary typing/numpad interpretation is deliberately unchanged.
  assert(SuiteCutDescribeKey("Numpad1",0)->key=="End");
  for(int repetition=0;repetition<100000;++repetition){auto token=SuiteCutNextSendKeysToken(UTF8(0xe009),0,0);assert(token.key.modifier==2);}
}
