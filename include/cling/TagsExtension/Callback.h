#ifndef CLING_CTAGS_CALLBACK_H
#define CLING_CTAGS_CALLBACK_H

#include "cling/Interpreter/Interpreter.h"
#include "cling/Interpreter/InterpreterCallbacks.h"
#include "cling/TagsExtension/TagManager.h"
#include "clang/Sema/Lookup.h"

#if 0
This feature is disabled by default until stable.
To enable, execute the following code as runtime input.
Note that, for now, the T meta command will cause the interpreter to segfault,
unless these objects are loaded.

.rawInput 0
#include "cling/TagsExtension/TagManager.h"
#include "cling/TagsExtension/Callback.h"
cling::TagManager t;
gCling->setCallbacks(new cling::CtagsInterpreterCallback(gCling,&t));

#endif

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
    cling::Interpreter* m_ip;
    cling::TagManager* m_tags;
  };
}

#endif

