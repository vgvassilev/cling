#ifndef CLING_CTAGS_CALLBACK_H
#define CLING_CTAGS_CALLBACK_H

#include "cling/Interpreter/Interpreter.h"
#include "cling/Interpreter/InterpreterCallbacks.h"
#include "cling/CtagsExtension/Wrapper.h"
#include "cling/CtagsExtension/TagManager.h"
#include "clang/Sema/Lookup.h"
#include <string>
#include <map>
#include <vector>
namespace cling{
  class LookupInfo{};//TODO: Would contain info regarding previous lookups; get rid of the map in LookupObject
  
  class CtagsInterpreterCallback : public cling::InterpreterCallbacks{
  public:
      CtagsInterpreterCallback(cling::Interpreter* interp,cling::TagManager* t);
    
    bool LookupObject (clang::LookupResult &R, clang::Scope *);
    
    //void AddTagFile(llvm::StringRef path);
    cling::TagManager* getTagManager();
  private:
    //std::map<std::string,LookupInfo> table;
    cling::Interpreter* ip;
    //std::vector<TagFileWrapper> tags;
    cling::TagManager* tags;
  };
}

#endif

