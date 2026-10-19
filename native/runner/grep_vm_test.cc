// SPDX-License-Identifier: MIT
#include "grep_expression.h"
#include <iostream>
#include <chrono>
namespace sr=suitecut::runner;
namespace{
  unsigned checks=0;
  void Check(bool value){
    ++checks;
    if(!value)throw std::runtime_error("native RegExp VM control failed "+std::to_string(checks));
  }
  bool Match(std::u16string source,std::u16string_view title,std::string flags={
  }
  ){
    sr::GrepWork work;
    return sr::GrepExpression(std::move(source),std::move(flags)).Matches(title,work);
  }
  template<class Callback>void Reject(Callback callback,const std::string& message){
    bool rejected=false;
    try{
      callback();
    }
    catch(const std::invalid_argument& error){
      rejected=std::string(error.what()).find(message)!=std::string::npos;
    }
    Check(rejected);
  }
}
int main(){
  Check(Match(u"^(a+)b\\1$",u"aabaa"));
  Check(!Match(u"^(a+)b\\1$",u"aaba"));
  Check(Match(u"^(?<word>ab)\\k<word>$",u"abab"));
  Check(!Match(u"^(?<word>ab)\\k<word>$",u"abac"));
  Check(Match(u"^\\1(a)$",u"a"));
  Check(Match(u"^(a|(b))+\\2c$",u"abbac"));
  Check(Match(u"^(?=a)a$",u"a"));
  Check(Match(u"^(?!b)a$",u"a"));
  Check(!Match(u"^(?!a)a$",u"a"));
  Check(Match(u"^(?=(a+))a*b\\1$",u"aba"));
  Check(!Match(u"^(?=(a+))a*b\\1$",u"aaba"));
  Check(Match(u"(?<=case )aa$",u"case aa"));
  Check(!Match(u"(?<!case )aa$",u"case aa"));
  Check(Match(u"(?<=([ab]+)([bc]+))\\2\\1$",u"abcbca"));
  Check(Match(u"^(?:a?)*b$",u"aaaaab"));
  Check(Match(u"^(?:a+?)+b$",u"aaaaab"));
  Check(Match(u"^(a*)*$",u""));
  Check(Match(u"^(a*)+$",u""));
  Check(Match(u"^a{257}$",std::u16string(257,u'a')));
  Check(!Match(u"^a{257}$",std::u16string(256,u'a')));
  Check(Match(u"^a{1,foo}$",u"a{1,foo}"));
  Check(Match(u"^a{1$",u"a{1"));
  Check(Match(u"^\\141b$",u"ab"));
  Check(Match(u"^\\8$",u"8"));
  Check(Match(u"^\\cA$",std::u16string(1,1)));
  Check(Match(u"^\\q$",u"q"));
  Check(Match(u"^\\xq$",u"xq"));
  Check(Match(u"^\\uqqqq$",u"uqqqq"));
  Check(Match(u"^(?i:ab)(?-i:CD)$",u"AbCD"));
  Check(!Match(u"^(?i:ab)(?-i:CD)$",u"Abcd"));
  Check(Match(u"^\\k<word>$",u"k<word>"));
  Check(Match(u"^.$",u"\U0001f600","u"));
  Check(!Match(u"^.$",u"\U0001f600"));
  Check(Match(u"^..$",u"\U0001f600"));
  Check(!Match(u"^..$",u"\U0001f600","u"));
  Check(Match(u"^\\u{1f600}$",u"\U0001f600","u"));
  Check(Match(u"^.$",std::u16string(1,0xd800),"u"));
  Check(Match(u"^\\w$",u"ſ","iu"));
  Check(!Match(u"^\\W$",u"ſ","iu"));
  Check(!Match(u"^\\w$",u"ſ","i"));
  Check(Match(u"^π$",u"Π","i"));
  Check(Match(u"^[π-σ]$",u"Σ","i"));
  Check(!Match(u"^ß$",u"ẞ","i"));
  Check(Match(u"^ß$",u"ẞ","iu"));
  Check(Match(u"^x",u"a\nx","m"));
  Check(Match(u"x$",u"x\ny","m"));
  Check(Match(u"x.y",u"x\ny","s"));
  Check(Match(u"x",u"x other","y"));
  Check(!Match(u"x",u"other x","y"));
  Check(Match(u"^\\p{Script=Greek}+$",u"ΠΣ","u"));
  Check(!Match(u"^\\p{Script=Greek}+$",u"ΠA","u"));
  Check(Match(u"^\\p{scx=Hira}$",u"ー","u"));
  Check(!Match(u"^\\p{sc=Hira}$",u"ー","u"));
  Check(Match(u"^\\p{General_Category=Letter}+$",u"AbΠ","u"));
  Check(Match(u"^\\p{Lu}$",u"A","u"));
  Check(Match(u"^\\P{Lowercase_Letter}$",u"a","iu"));
  Check(!Match(u"^\\P{Lowercase_Letter}$",u"a","iv"));
  Check(Match(u"^[[a-z]&&[^aeiou]]+$",u"bcdf","v"));
  Check(!Match(u"^[[a-z]&&[^aeiou]]+$",u"abc","v"));
  Check(Match(u"^[[a-z]--[aeiou]]+$",u"bcdf","v"));
  Check(!Match(u"^[[a-z]--[aeiou]]+$",u"abc","v"));
  Check(Match(u"^[\\q{abc|xyxy}]$",u"abc","v"));
  Check(Match(u"^[\\q{abc|xyxy}]$",u"xyxy","v"));
  Check(!Match(u"^[\\q{abc|xyxy}]$",u"a","v"));
  Check(Match(u"^[\\q{ab|a}]b$",u"ab","v"));
  Check(Match(u"^[\\q{}]$",u"","v"));
  Check(Match(u"^[^\\q{a}]$",u"b","v"));
  Check(!Match(u"^[^\\q{a}]$",u"a","v"));
  Check(Match(u"^\\p{RGI_Emoji_Flag_Sequence}$",u"\U0001f1ec\U0001f1e7","v"));
  Check(Match(u"^(?<π>a)\\k<π>$",u"aa"));
  Check(Match(u"^(?<\\u03c0>a)\\k<π>$",u"aa","u"));
  Check(Match(u"^(?:(?<x>a)|(?<x>b))\\k<x>$",u"aa"));
  Check(Match(u"^(?:(?<x>a)|(?<x>b))\\k<x>$",u"bb"));
  Check(!Match(u"^(?:(?<x>a)|(?<x>b))\\k<x>$",u"ab"));
  Check(Match(u"^\\c_$",u"\\c_"));
  Check(Match(u"^[\\c]$",u"\\"));
  Check(Match(u"^[\\c]$",u"c"));
  Check(Match(u"^[\\d-a]$",u"-"));
  Check(Match(u"^[a-\\d]$",u"5"));
  Reject([]{
    sr::GrepExpression expression(u"(?<x>a)(?<x>b)");
  }
  ,"same alternative");
  Reject([]{
    sr::GrepExpression expression(u"[ab&&c]","v");
  }
  ,"single operand");
  Reject([]{
    sr::GrepExpression expression(u"[^\\q{ab}]","v");
  }
  ,"complemented");
  Reject([]{
    sr::GrepExpression expression(u"\\p{script=Greek}","u");
  }
  ,"unknown Unicode property");
  Reject([]{
    sr::GrepExpression expression(u"\\P{RGI_Emoji}","v");
  }
  ,"string property");
  sr::GrepWork decode_budget{1};
  Reject([&]{sr::GrepExpression(u"(?:)","u").Matches(std::u16string(32768,u'a'),decode_budget);},"work capacity");
  Check(decode_budget.remaining==0);
  sr::GrepWork deadline_work;
  const auto original_deadline=std::chrono::steady_clock::now()-std::chrono::milliseconds(1);
  unsigned polls=0;deadline_work.check_stopped=[&]{++polls;if(std::chrono::steady_clock::now()>=original_deadline)throw std::runtime_error("actual original native deadline");};
  bool original_stopped=false;
  try{sr::GrepExpression(u"^(?<x>a+)+b$").Matches(std::u16string(20000,u'a'),deadline_work);}
  catch(const std::runtime_error& error){original_stopped=std::string(error.what())=="actual original native deadline";}
  Check(original_stopped&&polls>0&&deadline_work.remaining>0);
  sr::GrepWork interrupted_work;interrupted_work.check_stopped=[] {throw std::runtime_error("actual native stop callback");};
  bool stop_preserved=false;
  try{sr::GrepExpression(u"^(a+)\\1+$").Matches(std::u16string(20000,u'a')+u"b",interrupted_work);}
  catch(const std::runtime_error& error){stop_preserved=std::string(error.what())=="actual native stop callback";}
  Check(stop_preserved&&interrupted_work.remaining>0);
  sr::GrepWork bursts;unsigned burst_polls=0;
  bursts.check_stopped=[&]{if(++burst_polls==2)throw std::runtime_error("bulk original stop");};
  bursts.Charge(3000);bursts.Charge(2000);bursts.Charge(3191);
  bool bulk_stopped=false;try{bursts.Charge();}catch(const std::runtime_error& error){bulk_stopped=std::string(error.what())=="bulk original stop";}
  Check(bulk_stopped&&burst_polls==2&&bursts.remaining>0);
  sr::GrepExpression regular(u"^(a+)+b$");
  Check(!regular.UsesVM());
  sr::GrepWork linear;
  Check(!regular.Matches(std::u16string(20000,u'a'),linear));
  Check(linear.remaining>60*1024*1024);
  sr::GrepExpression advanced(u"^(a+)\\1+$");
  Check(advanced.UsesVM());
  sr::GrepWork tiny{
    20
  }
  ;
  Reject([&]{
    advanced.Matches(std::u16string(20000,u'a')+u"b",tiny);
  }
  ,"work capacity");
  Check(tiny.remaining==0);
  std::u16string memory_source=u"^((?:";
  for(unsigned i=0;
  i<128;
  ++i)memory_source+=u"a+";
  memory_source+=u")+)\\1$";
  sr::GrepExpression memory(memory_source);
  sr::GrepWork memory_work;
  Reject([&]{
    memory.Matches(std::u16string(20000,u'a')+u"b",memory_work);
  }
  ,"memory capacity");
  std::cout<<"Native bounded capture/assertion/Unicode RegExp VM "<<checks<<" controls PASS\n";
}
