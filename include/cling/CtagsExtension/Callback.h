#ifndef CLING_CTAGS_CALLBACK_H
#define CLING_CTAGS_CALLBACK_H

#include "cling/Interpreter/Interpreter.h"
#include "cling/Interpreter/InterpreterCallbacks.h"
#include "clang/Sema/Lookup.h"
#include <string>
#include <map>

namespace cling{
  class LookupInfo{};
  
  class CtagsInterpreterCallback : public cling::InterpreterCallbacks{
  public:
    CtagsInterpreterCallback(cling::Interpreter* interp) :
    InterpreterCallbacks(interp,true),ip(interp){}
    virtual bool LookupObject (clang::LookupResult &R, clang::Scope *);
    private:
    std::map<std::string,LookupInfo> table;
    cling::Interpreter* ip;
  };
}

#endif

