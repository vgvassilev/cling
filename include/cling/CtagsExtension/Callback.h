#ifndef CLING_CTAGS_CALLBACK_H
#define CLING_CTAGS_CALLBACK_H

#include "cling/Interpreter/Interpreter.h"
#include "cling/Interpreter/InterpreterCallbacks.h"
#include "cling/CtagsExtension/TagManager.h"
#include "clang/Sema/Lookup.h"

namespace cling{
  class LookupInfo{};//TODO: Would contain info regarding previous lookups; get rid of the map in LookupObject
  
  class CtagsInterpreterCallback : public cling::InterpreterCallbacks{
  public:
      CtagsInterpreterCallback(cling::Interpreter* interp,cling::TagManager* t);
    
    using cling::InterpreterCallbacks::LookupObject;
      //^to get rid of bogus warning : "-Woverloaded-virtual"
      //virtual functions ARE meant to be overriden!

    bool LookupObject (clang::LookupResult &R, clang::Scope *);
    
    cling::TagManager* getTagManager();
  private:
    cling::Interpreter* ip;
    cling::TagManager* tags;
  };
}

#endif

