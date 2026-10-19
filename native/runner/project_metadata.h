// SPDX-License-Identifier: MIT
#pragma once
#include "json_text.h"
#include <cctype>
#include <string_view>
namespace suitecut::runner {
// Checked opaque JSON metadata, never executable configuration. The native
// report retains the actual serialized object instead of trusting host framing.
inline bool ProjectMetadataJSON(std::string_view text){
  if(text.empty()||text.size()>16384)return false;
  struct Parser {
    std::string_view text;size_t at=0,values=0;
    void Space(){while(at<text.size()&&(text[at]==' '||text[at]=='\t'||text[at]=='\r'||text[at]=='\n'))++at;}
    bool String(){const auto begin=at;if(at>=text.size()||text[at++]!='"')return false;bool escaped=false;
      while(at<text.size()){const char c=text[at++];if(!escaped&&c=='"')return JsonString(text.substr(begin,at-begin),8192);if(!escaped&&c=='\\')escaped=true;else escaped=false;}return false;}
    bool Literal(std::string_view literal){if(text.substr(at,literal.size())!=literal)return false;at+=literal.size();return true;}
    bool Number(){if(at<text.size()&&text[at]=='-')++at;if(at>=text.size())return false;
      if(text[at]=='0')++at;else{if(text[at]<'1'||text[at]>'9')return false;do{++at;}while(at<text.size()&&text[at]>='0'&&text[at]<='9');}
      if(at<text.size()&&text[at]=='.'){++at;const auto begin=at;while(at<text.size()&&text[at]>='0'&&text[at]<='9')++at;if(at==begin)return false;}
      if(at<text.size()&&(text[at]=='e'||text[at]=='E')){++at;if(at<text.size()&&(text[at]=='+'||text[at]=='-'))++at;const auto begin=at;while(at<text.size()&&text[at]>='0'&&text[at]<='9')++at;if(at==begin)return false;}return true;}
    bool Value(unsigned depth){Space();if(depth>16||at>=text.size()||++values>4096)return false;const char first=text[at];
      if(first=='"')return String();
      if(first=='{'||first=='['){const bool object=first=='{';const char end=object?'}':']';++at;Space();if(at<text.size()&&text[at]==end){++at;return true;}
        for(;;){if(object){if(!String())return false;Space();if(at>=text.size()||text[at++]!=':')return false;}if(!Value(depth+1))return false;Space();if(at>=text.size())return false;const char delimiter=text[at++];if(delimiter==end)return true;if(delimiter!=',')return false;Space();}
      }
      if(first=='t')return Literal("true");
      if(first=='f')return Literal("false");
      if(first=='n')return Literal("null");
      return Number();
    }
  } parser{text};
  parser.Space();if(parser.at>=text.size()||text[parser.at]!='{'||!parser.Value(0))return false;parser.Space();return parser.at==text.size();
}
}
