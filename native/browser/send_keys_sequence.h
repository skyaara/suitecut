// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include "input_keys.h"

// Authored streaming interpretation of WebDriver's reserved key characters.
// The existing ordinary type/press key descriptions and dispatch stay separate.
// No input state, scheduling or DOM operation lives in this lexical helper.
struct SuiteCutSendKeysToken {
  enum class Kind { End, Invalid, Text, Key, ReleaseModifiers };
  Kind kind = Kind::End;
  size_t bytes = 0;
  std::string text;
  SuiteCutKey key;
  bool private_key = false;
};
inline bool SuiteCutSendKeysShifted(std::string_view text) {
  return text.size()==1 && ((text[0]>='A'&&text[0]<='Z') || std::string_view("~!@#$%^&*()_+{}|:\"<>?").find(text[0])!=std::string_view::npos);
}
inline std::optional<SuiteCutKey> SuiteCutSendKeysPrivateKey(uint32_t point,int modifiers) {
  if(point>=0xe01a && point<=0xe023){
    const int digit=static_cast<int>(point-0xe01a);const std::string text(1,static_cast<char>('0'+digit));
    return SuiteCutKey{text,"Numpad"+text,(modifiers&7)?"":text,text,96+digit,0,3};
  }
  if(point>=0xe031 && point<=0xe03c)return SuiteCutDescribeKey("F"+std::to_string(point-0xe030),modifiers);
  struct Named {uint32_t point;const char* name;};
  static constexpr Named names[]={
    {0xe003,"Backspace"},{0xe004,"Tab"},{0xe006,"Enter"},{0xe007,"NumpadEnter"},
    {0xe008,"ShiftLeft"},{0xe009,"ControlLeft"},{0xe00a,"AltLeft"},{0xe00b,"Pause"},{0xe00c,"Escape"},{0xe00d," "},
    {0xe00e,"PageUp"},{0xe00f,"PageDown"},{0xe010,"End"},{0xe011,"Home"},{0xe012,"ArrowLeft"},{0xe013,"ArrowUp"},
    {0xe014,"ArrowRight"},{0xe015,"ArrowDown"},{0xe016,"Insert"},{0xe017,"Delete"},{0xe018,"Semicolon"},
    {0xe024,"NumpadMultiply"},{0xe025,"NumpadAdd"},{0xe027,"NumpadSubtract"},{0xe029,"NumpadDivide"},
    {0xe03d,"MetaLeft"},{0xe050,"ShiftRight"},{0xe051,"ControlRight"},{0xe052,"AltRight"},{0xe053,"MetaRight"},
  };
  for(const auto& entry:names)if(point==entry.point)return SuiteCutDescribeKey(entry.name,modifiers);
  struct Raw {uint32_t point;const char* key;const char* code;int virtual_key;const char* text;int location;};
  static constexpr Raw raw[]={
    {0xe001,"Cancel","",3,"",0},{0xe002,"Help","Help",47,"",0},{0xe005,"Clear","",12,"",0},
    {0xe019,"=","NumpadEqual",187,"=",3},{0xe026,",","NumpadComma",108,",",3},
    {0xe028,".","NumpadDecimal",110,".",3},{0xe040,"ZenkakuHankaku","",244,"",0},
    {0xe054,"PageUp","Numpad9",33,"",3},{0xe055,"PageDown","Numpad3",34,"",3},
    {0xe056,"End","Numpad1",35,"",3},{0xe057,"Home","Numpad7",36,"",3},
    {0xe058,"ArrowLeft","Numpad4",37,"",3},{0xe059,"ArrowUp","Numpad8",38,"",3},
    {0xe05a,"ArrowRight","Numpad6",39,"",3},{0xe05b,"ArrowDown","Numpad2",40,"",3},
    {0xe05c,"Insert","Numpad0",45,"",3},{0xe05d,"Delete","NumpadDecimal",46,"",3},
  };
  for(const auto& entry:raw)if(point==entry.point)return SuiteCutKey{entry.key,entry.code,(modifiers&7)?"":entry.text,entry.text,entry.virtual_key,0,entry.location};
  return std::nullopt;
}
inline SuiteCutSendKeysToken SuiteCutNextSendKeysToken(std::string_view text,size_t cursor,int modifiers) {
  if(cursor==text.size())return {};
  SuiteCutSendKeysToken result;result.kind=SuiteCutSendKeysToken::Kind::Invalid;
  if(cursor>text.size())return result;
  const auto first=static_cast<uint8_t>(text[cursor]);uint32_t point=0;size_t length=0;
  if(first<0x80){point=first;length=1;}
  else if(first>=0xc2&&first<=0xdf){point=first&0x1f;length=2;}
  else if(first>=0xe0&&first<=0xef){point=first&0x0f;length=3;}
  else if(first>=0xf0&&first<=0xf4){point=first&0x07;length=4;}
  else return result;
  if(length>text.size()-cursor)return result;
  for(size_t index=1;index<length;++index){const auto next=static_cast<uint8_t>(text[cursor+index]);if((next&0xc0)!=0x80)return result;point=(point<<6)|(next&0x3f);}
  if((length==2&&point<0x80)||(length==3&&point<0x800)||(length==4&&point<0x10000)||point>0x10ffff||(point>=0xd800&&point<=0xdfff))return result;
  result.bytes=length;
  if(point==0xe000){result.kind=SuiteCutSendKeysToken::Kind::ReleaseModifiers;result.private_key=true;return result;}
  if(auto key=SuiteCutSendKeysPrivateKey(point,modifiers)){result.kind=SuiteCutSendKeysToken::Kind::Key;result.private_key=true;result.key=std::move(*key);return result;}
  result.text=std::string(text.substr(cursor,length));
  if(auto key=SuiteCutDescribeKey(result.text,modifiers)){result.kind=SuiteCutSendKeysToken::Kind::Key;result.key=std::move(*key);}
  else result.kind=SuiteCutSendKeysToken::Kind::Text;
  return result;
}
