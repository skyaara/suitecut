// SPDX-License-Identifier: MIT
#include "suitecut/browser.hpp"
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
using suitecut::Json;
int main(int argc,char** argv) {
  if(argc!=3)return 2;
  const auto out=std::filesystem::absolute(argv[2]);std::filesystem::create_directories(out);
  Json report{{"cases",Json::array{}},{"ok",true}};
  const auto check=[&](const char* name,const std::function<void()>& test){Json row{{"name",name},{"ok",true}};try{test();}catch(const std::exception& e){row["ok"]=false;row["error"]=e.what();report["ok"]=false;}report["cases"].as_array().push_back(row);};
  try {
    suitecut::LaunchOptions options;options.executable=argv[1];options.width=800;options.height=600;
    auto browser=suitecut::Browser::launch(options);auto page=browser.main_page();
    page.navigate("data:text/html,%3Ctitle%3ECEF%20artifacts%3C/title%3E%3Cbody%20style='height:1600px;background:lightblue'%3E%3Ch1%3ENative%20artifacts%3C/h1%3E%3Ciframe%20id='child'%20srcdoc='%3Clabel%3EChild%3Cinput%20aria-label=Child%3E%3C/label%3E'%3E%3C/iframe%3E");
    check("PNG, JPEG and WebP screenshot bytes",[&]{for(const char* format:{"png","jpeg","webp"}){auto data=page.screenshot(Json{{"format",format}});if(data.size()<100)throw std::runtime_error("Empty screenshot");std::ofstream file(out/(std::string("viewport.")+format),std::ios::binary);file.write(reinterpret_cast<const char*>(data.data()),data.size());}});
    check("full-page and clipped screenshot bytes",[&]{if(page.screenshot(Json{{"fullPage",true}}).size()<100||page.screenshot(Json{{"clip",Json{{"x",0},{"y",0},{"width",100},{"height",100}}}}).size()<100)throw std::runtime_error("Empty full-page or clipped capture");});
    check("actual PDF output",[&]{auto data=page.pdf();std::ofstream file(out/"page.pdf",std::ios::binary);file.write(reinterpret_cast<const char*>(data.data()),data.size());});
    check("child-frame trusted input and readback",[&]{auto child=page.frame_locator("#child");child.get_by_label("Child").fill("Frame λ");if(child.get_by_label("Child").input_value()!="Frame λ")throw std::runtime_error("Frame input did not change");});
    check("viewport resize changes real layout",[&]{page.set_viewport(640,480);if(page.evaluate("innerWidth").as_int()!=640||page.evaluate("innerHeight").as_int()!=480)throw std::runtime_error("Viewport did not change");});
    check("popup lifetime and actual page inventory",[&]{const auto before=browser.pages().as_array().size();page.evaluate("window.open('about:blank','proof')!==null");page.wait_for_function("true");if(browser.pages().as_array().size()<=before)throw std::runtime_error("Popup not in native page inventory");});
    check("frame subscriptions produce actual pixel packets",[&]{page.subscribe_frames(true);page.evaluate("document.body.style.background='red'");bool found=false;const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(3);while(std::chrono::steady_clock::now()<end){const auto event=browser.transport()->poll(std::chrono::milliseconds(100));if(event){const auto header=Json::parse(event->header);if(header.contains("type")&&header.at("type")==Json("frame")&&!event->payload.empty()){found=true;break;}}}page.subscribe_frames(false);if(!found)throw std::runtime_error("No actual pixel packet");});
    browser.close();
  }catch(const std::exception& e){report["ok"]=false;report["fatalError"]=e.what();}
  std::ofstream(out/"results.json")<<report.dump()<<'\n';std::cout<<report.dump()<<'\n';return report.at("ok").as_bool()?0:1;
}
