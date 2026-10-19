// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "credential_store.h"
#include <cassert>
#include <iostream>
int main(){
  suitecut::CredentialStore store;std::string error;suitecut::VirtualCredential first,second;
  assert(suitecut::GenerateVirtualCredential("first.test",0,first,error));assert(suitecut::GenerateVirtualCredential("second.test",8,second,error));
  assert(store.Add(first,error));assert(!store.Add(first,error));assert(store.Get("first.test").size()==1);assert(store.Get("second.test").empty());
  assert(store.Replace({second},error));assert(store.Get().size()==1 && store.Get()[0].id==second.id);
  auto invalid=first;invalid.public_key=second.public_key;
  const auto before=store.Bytes();assert(!store.Replace({first,invalid},error));assert(store.Bytes()==before && store.Find(second.id));
  assert(store.Asserted(second.id,9));assert(!store.Asserted(second.id,8));assert(store.Find(second.id)->sign_count==9);
  assert(store.Replace({},error));assert(store.Size()==0 && store.Bytes()==0);
  for(int index=0;index<10000;++index){assert(store.Add(first,error));assert(store.Remove(first.id));assert(store.Size()==0 && store.Bytes()==0);}
  std::vector<suitecut::VirtualCredential> many;
  for(size_t index=0;index<suitecut::CredentialStore::kMaxCredentials;++index){auto item=first;unsigned char bytes[2]={static_cast<unsigned char>(index>>8),static_cast<unsigned char>(index)};item.id=suitecut::CredentialBase64URL(bytes,2);many.push_back(item);}
  assert(store.Replace(many,error));assert(store.Size()==suitecut::CredentialStore::kMaxCredentials);assert(!store.Add(second,error));assert(store.Replace({},error));
  std::cout<<"native credential store: validated atomic replacement, duplicate/capacity rejection, actual counter monotonicity,10000 cleanup cycles passed\n";
}
