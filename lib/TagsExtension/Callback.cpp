#include "cling/TagsExtension/Callback.h"
#include "llvm/ADT/SmallVector.h"

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

      /* Doesn't work very well now, so turning off
      llvm::SmallVector<std::string,30> incpaths;
      ip->GetIncludePaths(incpaths,true,false);
//      for(auto x:incpaths)
//          llvm::outs()<<x<<'\n';
      tags->AddTagFile(incpaths[0],true);//[0] seems to contain stl headers
      //Make the second arg true after the preprocessor pathway is implemented
      */
  }
  TagManager* CtagsInterpreterCallback::getTagManager()
  {
    return tags;
  }

}
