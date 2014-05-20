#include "cling/TagsExtension/Callback.h"
#include "llvm/ADT/SmallVector.h"

namespace cling {
  bool AutoloadCallback::LookupObject (clang::LookupResult &R, clang::Scope *){
    std::string in=R.getLookupName().getAsString();
    for(auto it=m_tags->begin(in);it!=m_tags->end(in);++it)
    {
      auto lookup=it->second;
      llvm::outs() << lookup.header
                  << '\t' << lookup.name
                  << '\t' <<lookup.type
                  << '\n';
    }
    return false;
  }


  AutoloadCallback::AutoloadCallback
  (cling::Interpreter* interp, TagManager *t) :
    InterpreterCallbacks(interp,true),
    m_ip(interp),
    m_tags(t) {

      /* Doesn't work very well now, so turning off
      llvm::SmallVector<std::string,30> incpaths;
      ip->GetIncludePaths(incpaths,true,false);
//      for(auto x:incpaths)
//          llvm::outs()<<x<<'\n';
      tags->AddTagFile(incpaths[0],true);//[0] seems to contain stl headers
      //Make the second arg true after the preprocessor pathway is implemented
      */
  }
  TagManager* AutoloadCallback::getTagManager() {
    return m_tags;
  }

}//end namespace cling//end namespace cling
