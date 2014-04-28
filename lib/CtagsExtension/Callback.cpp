#include "cling/CtagsExtension/Callback.h"

namespace cling {
  bool CtagsInterpreterCallback::LookupObject (clang::LookupResult &R, clang::Scope *){
    std::string in=R.getLookupName().getAsString();
    if(in[0]!='_' && table.find(in)==table.end())
    {
      llvm::outs()<<"Missing name:"<<in<<"\n";
      table[in]=LookupInfo();
    }
    return false;
  }
}