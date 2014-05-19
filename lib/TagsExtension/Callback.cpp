#include "cling/TagsExtension/Callback.h"

namespace cling {
  bool CtagsInterpreterCallback::LookupObject (clang::LookupResult &R, clang::Scope *){

    std::string in=R.getLookupName().getAsString();
    for(auto it=tags->begin(in);it!=tags->end(in);++it)
    {
        auto lookup=it->second;
        llvm::outs()<<lookup.header
                    <<'\t'<<lookup.name
                    <<'\t'<<lookup.type
                    <<'\n';
    }
    return false;
  }


  CtagsInterpreterCallback::CtagsInterpreterCallback
  (cling::Interpreter* interp, TagManager *t) :
    InterpreterCallbacks(interp,true),
    ip(interp),
    tags(t) {
      //TODO: get the include path from clang instead of this
      tags->AddTagFile("/usr/include/c++/4.9.0/",false);

  }
  TagManager* CtagsInterpreterCallback::getTagManager()
  {
    return tags;
  }

}
