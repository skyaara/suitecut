// SPDX-License-Identifier: MIT
#pragma once
#include <algorithm>
#include <string>
#include <vector>
#include "include/cef_parser.h"
#include "devtools_utf16.h"
namespace suitecut {
// Native policy only. Utility DOM observations do not decide ordering, select
// an alternative element, retry, or manufacture an identity/uniqueness result.
class LocatorSuggestionPlan {
 public:
  using Dict=CefRefPtr<CefDictionaryValue>;
  struct Candidate {std::string kind,code;CefRefPtr<CefListValue> query;int rank=0;};
  static constexpr size_t kCandidates=24,kResults=5,kBytes=65536;
  static std::string JSON(CefRefPtr<CefValue> value){return CefWriteJSON(value,JSON_WRITER_DEFAULT).ToString();}
  static std::string Quote(const CefString& value){auto v=CefValue::Create();v->SetString(value);return JSON(v);}
  static std::string CSS(const CefString& value){
    // Escaping every scalar is valid CSS string content, including quotes and
    // controls. The collector omits NUL/isolated UTF16 for attribute recipes.
    std::string out;const auto units=value.ToString16();constexpr char hex[]="0123456789abcdef";
    for(size_t i=0;i<units.size();++i){unsigned c=units[i];if(c>=0xd800&&c<=0xdbff&&i+1<units.size())c=0x10000+((c-0xd800)<<10)+(units[++i]-0xdc00);std::string digits;do{digits+=hex[c%16];c/=16;}while(c);std::reverse(digits.begin(),digits.end());out+='\\';out+=digits;out+=' ';}return out;
  }
  static bool Read(Dict observation,std::vector<Candidate>&out,std::string&error){
    out.clear();const auto list=observation?observation->GetList("candidates"):nullptr;
    if(!list||list->GetSize()>kCandidates){error="Native locator suggestion candidate capacity exceeded";return false;}
    for(size_t i=0;i<list->GetSize();++i){
      const auto item=list->GetDictionary(i);const auto query=item?item->GetList("query"):nullptr;
      if(!item||item->GetType("kind")!=VTYPE_STRING||!query||!query->GetSize()||query->GetSize()>64){error="Invalid native locator suggestion observation";return false;}
      Candidate c;c.kind=item->GetString("kind").ToString();c.query=query->Copy();
      const auto first=query->GetDictionary(0);if(!first||first->GetType("engine")!=VTYPE_STRING){error="Invalid native locator suggestion query";return false;}
      auto qvalue=CefValue::Create();qvalue->SetList(query->Copy());if(JSON(qvalue).size()>16384){error="Native locator suggestion query exceeds byte bounds";return false;}
      const auto engine=first->GetString("engine").ToString();
      const auto exact=[&](Dict value){return value&&value->GetType("text")==VTYPE_STRING&&value->GetString("text").length()<=2048&&!DevToolsHasIsolated(value->GetString("text").ToString16())&&value->GetType("exact")==VTYPE_BOOL&&value->GetBool("exact");};
      if(c.kind=="testId"&&engine=="testId"&&query->GetSize()==1&&first->GetType("attribute")==VTYPE_STRING&&exact(first)){
        const auto attr=first->GetString("attribute").ToString();const auto text=first->GetString("text");
        if(attr.empty()||attr.size()>256||text.ToString16().find(char16_t(0))!=std::u16string::npos||!std::all_of(attr.begin(),attr.end(),[](unsigned char x){return(x>='a'&&x<='z')||(x>='0'&&x<='9')||x=='-'||x=='_';})){error="Invalid native test-ID attribute";return false;}
        c.rank=100;c.code="frame.locator("+Quote("["+attr+"=\""+CSS(text)+"\"]")+")";
      }else if(c.kind=="role"&&engine=="role"&&query->GetSize()==1&&first->GetType("role")==VTYPE_STRING&&exact(first->GetDictionary("name"))){
        const auto role=first->GetString("role").ToString();if(role.empty()||role.size()>128||!std::all_of(role.begin(),role.end(),[](unsigned char x){return(x>='a'&&x<='z')||x=='-';})){error="Invalid native semantic role";return false;}
        c.rank=80;c.code="frame.getByRole("+Quote(role)+", { name: "+Quote(first->GetDictionary("name")->GetString("text"))+", exact: true })";
      }else if(query->GetSize()==1&&exact(first)&&((c.kind=="label"&&engine=="label")||(c.kind=="text"&&engine=="text")||(c.kind=="attribute"&&(engine=="placeholder"||engine=="alt"||engine=="title")))){
        c.rank=c.kind=="label"?90:c.kind=="attribute"?70:60;
        const auto method=engine=="label"?"getByLabel":engine=="text"?"getByText":engine=="placeholder"?"getByPlaceholder":engine=="alt"?"getByAltText":"getByTitle";
        c.code="frame."+std::string(method)+"("+Quote(first->GetString("text"))+", { exact: true })";
      }else if((c.kind=="id"||c.kind=="css"||c.kind=="path")&&engine=="css"&&(c.kind=="path"||query->GetSize()==1)){
        c.rank=c.kind=="id"?50:c.kind=="css"?40:10;c.code="frame";
        for(size_t n=0;n<query->GetSize();++n){const auto step=query->GetDictionary(n);if(!step||step->GetString("engine")!="css"||step->GetSize()!=2||step->GetType("selector")!=VTYPE_STRING||step->GetString("selector").empty()||step->GetString("selector").length()>16384){error="Invalid native CSS suggestion";return false;}c.code+=".locator("+Quote(step->GetString("selector"))+")";}
      }else{error="Unsupported native locator suggestion strategy";return false;}
      out.push_back(std::move(c));
    }
    std::stable_sort(out.begin(),out.end(),[](const auto&a,const auto&b){return a.rank>b.rank;});return true;
  }
  static bool Capture(Dict result,int&backend,Dict&metadata,std::string&error){
    backend=0;metadata=nullptr;const auto remote=result&&!result->HasKey("exceptionDetails")?result->GetDictionary("result"):nullptr;
    const auto deep=remote&&remote->GetString("subtype")=="array"?remote->GetDictionary("deepSerializedValue"):nullptr;
    const auto values=deep&&deep->GetString("type")=="array"?deep->GetList("value"):nullptr;
    const auto element=values&&values->GetSize()==2?values->GetDictionary(0):nullptr;
    const auto node=element&&element->GetString("type")=="node"?element->GetDictionary("value"):nullptr;
    const auto text=values&&values->GetSize()==2?values->GetDictionary(1):nullptr;
    if(!node||node->GetType("nodeType")!=VTYPE_INT||node->GetInt("nodeType")!=1||node->GetType("backendNodeId")!=VTYPE_INT||node->GetInt("backendNodeId")<=0||!text||text->GetString("type")!="string"||text->GetType("value")!=VTYPE_STRING||text->GetString("value").length()>kBytes){error="Native locator suggestion exact Element capture unavailable";return false;}
    const auto encoded=text->GetString("value").ToString();if(encoded.size()>kBytes){error="Native locator suggestion observation exceeds byte bounds";return false;}
    const auto parsed=CefParseJSON(encoded,JSON_PARSER_RFC);metadata=parsed?parsed->GetDictionary():nullptr;if(!metadata){error="Invalid native locator suggestion capture JSON";return false;}
    backend=node->GetInt("backendNodeId");return true;
  }
};
}
