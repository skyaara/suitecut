// SPDX-License-Identifier: MIT
#include "grep_expression.h"
#include <iostream>
#include <stdexcept>
#include <thread>
#include <array>
using suitecut::runner::CompileCLIGrep;
using suitecut::runner::GrepPattern;
namespace {
unsigned checks=0;
void Check(bool actual){++checks;if(!actual)throw std::runtime_error("native grep control failed "+std::to_string(checks));}
bool Match(std::u16string pattern,std::u16string_view title){GrepPattern::Work work;return CompileCLIGrep(std::move(pattern)).Matches(title,work);}
void Reject(std::u16string pattern){bool rejected=false;try{(void)CompileCLIGrep(std::move(pattern));}catch(const std::invalid_argument&){rejected=true;}Check(rejected);}
}
int main(){
  Check(Match(u"TesT Aa",u"project file outer test AA"));
  Check(!Match(u"/TesT Aa/",u"project file outer test AA"));
  Check(Match(u"/TesT Aa/i",u"project file outer test AA"));
  Check(Match(u"/test [A-B]/",u"p f outer test BB"));
  Check(!Match(u"/test [A-B]/",u"p f outer test CC"));
  Check(Match(u"/^Project One nested/alpha.spec.mjs outer suite @inherited @same test AA @leaf @same$/",u"Project One nested/alpha.spec.mjs outer suite @inherited @same test AA @leaf @same"));
  Check(Match(u"/slash a\\/b/",u"p f slash a/b"));
  Check(Match(u"/emoji .. @/",u"p f emoji \U0001f600 @tag"));
  Check(!Match(u"/emoji . @/",u"p f emoji \U0001f600 @tag"));
  std::u16string lone=u"p f lone ";lone.push_back(0xd800);Check(Match(u"/lone \\ud800/",lone));
  Check(Match(u"/π/",u"p π"));Check(Match(u"/π/i",u"Π"));Check(Match(u"/[π]/i",u"Π"));Check(Match(u"/\\u03c0/i",u"Π"));
  Check(Match(u"/[π-σ]/i",u"Σ"));Check(!Match(u"/[^π-σ]/i",u"Σ"));
  Check(!Match(u"/ß/i",u"ẞ"));Check(!Match(u"/ω/i",u"Ω"));Check(Match(u"/ω/i",u"Ω"));Check(!Match(u"/S/i",u"ſ"));Check(!Match(u"/K/i",u"K"));
  Check(!Match(u"/line x.y/",u"line x\ny"));Check(!Match(u"/line x.y/s",u"line x\ny"));
  Check(Match(u"/test AA/g",u"test AA"));Check(Match(u"/test AA/gi",u"TEST aa"));
  Check(Match(u"/(?:ab|cd){2,3}e?$/",u"x abcdcde"));
  Check(!Match(u"/^(?:ab|cd){2,3}e?$/",u"x abcdcde"));
  Check(Match(u"/^a?b*c+d{2,4}$/",u"bbbcccddd"));
  Check(Match(u"/^[A-z]+$/i",u"[Z_z]"));
  Check(Match(u"/^[a-z]+$/i",u"AzZ"));Check(!Match(u"/^[a-z]+$/i",u"["));
  Check(Match(u"/^\\s\\S\\d\\D\\w\\W$/",u"\u2028a2!Z!"));
  Check(Match(u"/^[]*[^]$/",u"\n"));Check(!Match(u"/[]/",u"x"));
  Check(Match(u"/\\btest\\b/",u"test!"));Check(!Match(u"/\\btest\\b/",u"attest"));
  Check(Match(u"/\\Btest/",u"attest"));
  Check(Match(u"/^a$|^b$/",u"b"));Check(!Match(u"/^a$|^b$/",u"ab"));
  Check(Match(u"/^(a?)*$/",u"aaaa"));Check(Match(u"/(?:)*x/",u"x"));
  for(const auto* pattern:{u"/(?i)a/",u"/a/ii",u"/[z-a]/"})Reject(pattern);
  Reject(std::u16string(4097,u'a'));
  // The ambiguous nested repetition is linear in actual automaton operations;
  // a depleted budget fails concretely, without a detached matcher or timeout.
  auto hostile=CompileCLIGrep(u"/^(a+)+b$/");GrepPattern::Work sufficient;Check(!hostile.Matches(std::u16string(20000,u'a'),sufficient));Check(sufficient.remaining>60*1024*1024);
  GrepPattern::Work tiny{20};bool exhausted=false;try{(void)hostile.Matches(std::u16string(20000,u'a'),tiny);}catch(const std::invalid_argument&){exhausted=true;}Check(exhausted&&tiny.remaining==0);
  GrepPattern::Work title_work;bool too_long=false;try{(void)hostile.Matches(std::u16string(32769,u'a'),title_work);}catch(const std::invalid_argument&){too_long=true;}Check(too_long);
  // Reusing a budget across different immutable patterns/titles must preserve
  // exact charged work, including after early acceptance and budget failure.
  GrepPattern simple(u"^ab+$"),other(u"(?:invoice|payment|checkout).*done$");
  GrepPattern::Work reused;
  for(const auto& title:{u"abbb",u"missing",u"invoice done",u"a",u"payment done",u"ab"}){
    for(const auto* pattern:{&simple,&other}){
      GrepPattern::Work fresh;const auto before=reused.remaining;
      Check(pattern->Matches(title,reused)==pattern->Matches(title,fresh));
      Check(before-reused.remaining==64*1024*1024-fresh.remaining);
    }
  }
  reused.remaining=1;bool reuse_exhausted=false;
  try{(void)other.Matches(u"payment done",reused);}catch(const std::invalid_argument&){reuse_exhausted=true;}
  Check(reuse_exhausted);reused.remaining=64*1024*1024;Check(simple.Matches(u"abbb",reused));
  // Original native stop callbacks may throw or synchronously perform another
  // match. Neither path may leave scratch leased or corrupt the outer closure.
  GrepPattern::Work stopped;stopped.until_poll=1;
  stopped.check_stopped=[] {throw std::runtime_error("actual stop");};bool stop_seen=false;
  try{(void)simple.Matches(u"ab",stopped);}catch(const std::runtime_error& error){stop_seen=std::string(error.what())=="actual stop";}
  Check(stop_seen);stopped.check_stopped={};Check(simple.Matches(u"ab",stopped));
  GrepPattern::Work reentrant;bool nested=false,nested_matched=false;reentrant.until_poll=1;
  reentrant.check_stopped=[&]{if(!nested){nested=true;nested_matched=simple.Matches(u"abbb",reentrant);}};
  Check(!hostile.Matches(std::u16string(10000,u'a'),reentrant));Check(nested&&nested_matched);
  // A compiled expression remains immutable and can be shared by independent
  // native matching owners with no common scratch or result state.
  std::array<unsigned,2> successes{};std::array<std::thread,2> threads;
  for(size_t i=0;i<threads.size();++i)threads[i]=std::thread([&,i]{GrepPattern::Work own;for(unsigned j=0;j<64;++j)if(simple.Matches(u"abbb",own))++successes[i];});
  for(auto& thread:threads)thread.join();
  Check(successes[0]==64&&successes[1]==64);
  std::cout<<"Native UTF16 grep policy "<<checks<<" controls PASS\n";
}
