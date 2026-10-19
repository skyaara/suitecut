// SPDX-License-Identifier: MIT
#include "control_cli.hpp"
#include <cerrno>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <openssl/evp.h>
#include <stdexcept>
#ifndef _WIN32
#include <fcntl.h>
#include <unistd.h>
#endif

namespace suitecut::control {
namespace {
void need(bool value,const char* why){if(!value)throw std::invalid_argument(why);}
double number(const std::string& input) {
  const auto value=Json::parse(input,{128,1,1,128});
  need(value.is_number()&&std::isfinite(value.as_double()),"Expected a finite numeric argument");return value.as_double();
}
Json integer(const std::string& input){auto value=Json::parse(input,{128,1,1,128});need(value.is_integer(),"Expected an integer argument");return value;}
std::string ref(std::string value) {
  if(value.starts_with('@'))value.erase(0,1);
  need(value.size()>1&&value[0]=='e'&&value.find_first_not_of("0123456789",1)==std::string::npos,"Use an eN ref from the latest observation");return value;
}
}
std::string cli_help() {
  return "Native browser control: one C++ engine behind CLI, JSONL and MCP.\n"
    "Start: suitecut-native-control --session NAME --browser /absolute/browser open [URL]\n"
    "Then:  suitecut-native-control --session NAME COMMAND [ARGS]\n"
    "Core commands:\n"
    "  snapshot [--full|--delta] [--scope CSS] [--find TEXT] [--frame ID] [--boxes]\n"
    "  find TEXT | click REF | click X Y | dblclick REF | dblclick X Y\n"
    "  fill REF TEXT | type REF TEXT | clear REF | hover REF | focus REF\n"
    "  check REF | uncheck REF | select REF VALUE... | press KEY [REF]\n"
    "  scroll DELTA_Y [DELTA_X] | move X Y [STEPS] | down [BUTTON] | up [BUTTON]\n"
    "  drag SOURCE_REF TARGET_REF | drag X1 Y1 X2 Y2 [STEPS]\n"
    "  wait REF STATE | wait text TEXT | wait url URL | wait changed\n"
    "  batch JSON_ARRAY | dialog accept [TEXT] | dialog dismiss\n"
    "  screenshot [--output FILE] | frames | tabs [new|switch|close [ARG]]\n"
    "  viewport WIDTH HEIGHT | read | get KIND [REF] | status | close\n"
    "Discovery: help [core|network|state|debug|media|all], tools [GROUP], profiles\n"
    "Advanced: --tools GROUP call TOOL JSON_ARGUMENTS\n"
    "Output: --response compact|delta|full|none; --output FILE saves exclusively.\n"
    "Sessions: --idle-ms MS (default600000), --timeout-ms MS, --session-dir DIR.\n"
    "An idle owned session closes automatically. close retires its browser.\n"
    "Refs change after observations; delta refIndex lists the current refs.\n"
    "Use --response none for a known sequence to retain refs between inputs.\n"
    "Input acknowledgement is not proof of a UI effect or task completion.\n"
    "No positional command: persistent JSONL; --mcp: MCP stdio (core default).\n";
}
Json cli_request(const std::vector<std::string>& words,Json observation,const std::string& response) {
  need(!words.empty(),"Missing CLI command");
  const auto& command=words[0];const auto size=words.size();
  Json args=Json::object{};std::string tool="act";
  auto arity=[&](std::size_t minimum,std::size_t maximum){need(size>=minimum+1&&size<=maximum+1,"Incorrect command arguments; see help");};
  if(command=="status"||command=="close"){arity(0,0);tool=command=="status"?"_session_status":"_session_close";}
  else if(command=="snapshot"||command=="observe"||command=="find"||command=="screenshot"||command=="frames") {
    tool="observe";args=std::move(observation);arity(command=="find"?1:0,command=="find"?1:0);
    if(command=="find")args["query"]=words[1];
    if(command=="screenshot")args=Json{{"mode","screenshot"}};
    if(command=="frames")args=Json{{"mode","frames"}};
  } else if(command=="open"||command=="navigate") {
    arity(command=="open"?0:1,1);
    if(size==1){tool="observe";args=std::move(observation);}
    else args=Json{{"action","navigate"},{"url",words[1]}};
  } else if(command=="click"||command=="dblclick") {
    arity(1,2);
    if(size==2)args=Json{{"action",command=="click"?"click":"doubleclick"},{"ref",ref(words[1])}};
    else args=Json{{"action","mouse"},{"event",command=="click"?"click":"doubleclick"},{"x",number(words[1])},{"y",number(words[2])}};
  } else if(command=="fill"||command=="type") {arity(2,2);args=Json{{"action",command},{"ref",ref(words[1])},{"value",words[2]}};}
  else if(command=="clear"||command=="hover"||command=="focus"||command=="check"||command=="uncheck") {arity(1,1);args=Json{{"action",command},{"ref",ref(words[1])}};}
  else if(command=="select") {
    arity(2,257);Json::array values;for(std::size_t i=2;i<size;++i)values.emplace_back(words[i]);
    args=Json{{"action","select"},{"ref",ref(words[1])},{"values",std::move(values)}};
  } else if(command=="press") {arity(1,2);args=Json{{"action","press"},{"key",words[1]}};if(size==3)args["ref"]=ref(words[2]);}
  else if(command=="scroll") {arity(1,2);args=Json{{"action","mouse"},{"event","wheel"},{"deltaY",number(words[1])},{"deltaX",size==3?number(words[2]):0}};}
  else if(command=="move") {arity(2,3);args=Json{{"action","mouse"},{"event","move"},{"x",number(words[1])},{"y",number(words[2])}};if(size==4)args["steps"]=integer(words[3]);}
  else if(command=="down"||command=="up") {arity(0,1);args=Json{{"action","mouse"},{"event",command},{"button",size==2?words[1]:"left"}};}
  else if(command=="drag") {
    arity(2,5);args["action"]="drag";
    if(size==3){args["source"]=ref(words[1]);args["target"]=ref(words[2]);}
    else {need(size==5||size==6,"Coordinate drag requires X1 Y1 X2 Y2 [STEPS]");
      args["from"]=Json{{"x",number(words[1])},{"y",number(words[2])}};
      args["to"]=Json{{"x",number(words[3])},{"y",number(words[4])}};
      args["steps"]=size==6?integer(words[5]):Json(20);
    }
  } else if(command=="wait") {
    tool="wait";arity(1,2);
    if(words[1]=="changed"){arity(1,1);args["changed"]=true;}
    else {arity(2,2);if(words[1]=="text"||words[1]=="url")args[words[1]]=words[2];
      else {args["ref"]=ref(words[1]);args["state"]=words[2];}}
  } else if(command=="dialog") {arity(1,2);need(words[1]=="accept"||words[1]=="dismiss","Dialog action must be accept or dismiss");args=Json{{"action","dialog"},{"accept",words[1]=="accept"}};if(size==3)args["promptText"]=words[2];}
  else if(command=="batch") {arity(1,1);args["actions"]=Json::parse(words[1],{256*1024,32,16384,128*1024});need(args.at("actions").is_array(),"Batch requires a JSON array of simple UI actions");}
  else if(command=="tabs") {
    tool="pages";arity(0,2);args["action"]=size==1?"list":words[1];
    if(size>1){need(words[1]=="list"||words[1]=="new"||words[1]=="switch"||words[1]=="close","Unknown tabs action");
      if(words[1]=="new"){if(size==3)args["url"]=words[2];}
      else if(words[1]=="switch"||words[1]=="close"){arity(2,2);args["pageId"]=integer(words[2]);}
      else arity(1,1);
    }
  } else if(command=="viewport") {tool="viewport";arity(2,2);args=Json{{"width",integer(words[1])},{"height",integer(words[2])}};}
  else if(command=="read") {tool="read";arity(0,0);}
  else if(command=="get") {tool="get";arity(1,2);args["kind"]=words[1];if(size==3)args["ref"]=ref(words[2]);}
  else if(command=="call") {arity(2,2);tool=words[1];args=Json::parse(words[2],{4*1024*1024,64,65536,4*1024*1024});need(args.is_object(),"call arguments must be a JSON object");}
  else throw std::invalid_argument("Unknown command: "+command+"; see help");
  if((tool=="act"||tool=="wait")&&!args.contains("response"))args["response"]=response;
  return Json{{"tool",tool},{"arguments",std::move(args)}};
}
Json save_output(const Json& reply,const std::string& requested) {
  if(!reply.contains("ok")||!reply.at("ok").as_bool())return reply;
  const auto& result=reply.at("result");
  if(result.is_object()&&result.contains("ok")&&!result.at("ok").as_bool())return reply;
  std::string bytes,mime="application/json",path=requested;
  if(result.is_object()&&result.contains("base64")) {
    const auto& encoded=result.at("base64").as_string();need(encoded.size()<=8*1024*1024&&encoded.size()%4==0,"Invalid screenshot byte budget");
    bytes.resize(encoded.size()/4*3);
    const auto size=EVP_DecodeBlock(reinterpret_cast<unsigned char*>(bytes.data()),reinterpret_cast<const unsigned char*>(encoded.data()),static_cast<int>(encoded.size()));
    need(size>=0,"Invalid screenshot encoding");std::size_t actual=static_cast<std::size_t>(size);
    if(!encoded.empty()&&encoded.back()=='=')--actual;
    if(encoded.size()>1&&encoded[encoded.size()-2]=='=')--actual;
    bytes.resize(actual);mime=result.at("mimeType").as_string();
  } else {need(!path.empty(),"Output path is required");bytes=result.dump({8*1024*1024,64,65536,8*1024*1024});bytes+='\n';}
#ifndef _WIN32
  if(path.empty())path="/tmp/suitecut-capture-"+std::to_string(::getuid())+"-"+std::to_string(::getpid())+"-"+
    std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".png";
  path=std::filesystem::absolute(path).lexically_normal().string();
  const int fd=::open(path.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC|O_NOFOLLOW,0600);
  if(fd<0)throw std::runtime_error("Cannot exclusively create output file");
  std::size_t offset=0;
  while(offset<bytes.size()) {
    const auto n=::write(fd,bytes.data()+offset,bytes.size()-offset);
    if(n<0&&errno==EINTR)continue;
    if(n<=0){::close(fd);::unlink(path.c_str());throw std::runtime_error("Output write failed");}offset+=static_cast<std::size_t>(n);
  }
  if(::close(fd)<0){::unlink(path.c_str());throw std::runtime_error("Output close failed");}
  return Json{{"ok",true},{"result",Json{{"path",path},{"mimeType",mime},{"bytes",bytes.size()}}}};
#else
  throw std::runtime_error("Exclusive CLI artifacts require POSIX");
#endif
}
} // namespace suitecut::control
