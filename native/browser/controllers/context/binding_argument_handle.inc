// SPDX-License-Identifier: MIT
// The retained wrapper has a native-authored accessor around the actual browser
// argument. Reading its property descriptor must never serialize a large scalar
// or await a Promise. Only bounded descriptive metadata crosses the host edge.
void BindingArgumentMetadata(int id,const std::string& wrapper,const std::string& group,DevToolsCallback done){
  auto params=CefDictionaryValue::Create();params->SetString("objectId",wrapper);params->SetString("objectGroup",group);params->SetBool("returnByValue",true);
  params->SetString("functionDeclaration",R"JS(function(){const value=this.value,type=typeof value;let preview=type;if(type==='string')preview=value.length>80?value.slice(0,80)+'…':value;else if(type==='number')preview=Object.is(value,-0)?'-0':value+'';else if(type==='boolean')preview=value?'true':'false';else if(value===null)preview='null';return {type,preview,object:type==='object'&&value!==null};})JS");
  DevTools(id,"Runtime.callFunctionOn",params,[this,id,wrapper,group,done=std::move(done)](bool ok,const std::string& json)mutable{
    auto parsed=CefParseJSON(json,JSON_PARSER_RFC);auto result=parsed?parsed->GetDictionary():nullptr;auto remote=result?result->GetDictionary("result"):nullptr;auto summary=remote?remote->GetDictionary("value"):nullptr;
    if(!ok||!result||result->HasKey("exceptionDetails")||!summary||summary->GetType("type")!=VTYPE_STRING||(summary->GetType("preview")!=VTYPE_STRING&&summary->GetType("preview")!=VTYPE_DICTIONARY)){done(false,json);return;}
    auto finish=[done=std::move(done)](Dict value,const std::string& error="")mutable{
      if(!value){done(false,error);return;}
      const auto description=suitecut::DevToolsString(value->GetValue("description"));if(description.size()>256)suitecut::DevToolsSetString(value,"description",suitecut::DevToolsTruncateWTF8(description,256));
      auto field=CefDictionaryValue::Create();field->SetString("name","value");field->SetDictionary("value",value);
      auto fields=CefListValue::Create();fields->SetDictionary(0,field);auto result=CefDictionaryValue::Create();result->SetList("result",fields);
      done(true,Json(result));
    };
    if(!summary->GetBool("object")){
      auto value=CefDictionaryValue::Create();value->SetString("type",summary->GetString("type"));suitecut::DevToolsSetString(value,"description",suitecut::DevToolsString(summary->GetValue("preview")));
      if(summary->GetString("type")=="object"&&summary->GetString("preview")=="null")value->SetString("subtype","null");
      finish(value);return;
    }
    auto params=CefDictionaryValue::Create();params->SetString("objectId",wrapper);params->SetString("objectGroup",group);params->SetBool("returnByValue",false);
    params->SetString("functionDeclaration","function(){return this.value;}");
    DevTools(id,"Runtime.callFunctionOn",params,[done=std::move(finish)](bool ok,const std::string& json)mutable{
      auto parsed=CefParseJSON(json,JSON_PARSER_RFC);auto result=parsed?parsed->GetDictionary():nullptr;auto value=result?result->GetDictionary("result"):nullptr;
      if(!ok||!result||result->HasKey("exceptionDetails")||!value||value->GetType("objectId")!=VTYPE_STRING){done(nullptr,json);return;}
      done(value);
    },std::nullopt,false,1048576,true);
  },std::nullopt,false,1048576,true);
}
