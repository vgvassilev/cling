#include "cling/CtagsExtension/Callback.h"

namespace cling {
  bool CtagsInterpreterCallback::LookupObject (clang::LookupResult &R, clang::Scope *){
    std::string in=R.getLookupName().getAsString();
    if(in[0]!='_' && table.find(in)==table.end())
    {
//       llvm::outs()<<"Missing name:"<<in<<"\n";
      for(auto& tagfile:tags)
      {
        for(auto result:tagfile.match(in,true))
        {
          llvm::outs()<<result.first
            <<'\t'<<result.second.name
            <<'\t'<<result.second.kind
            <<'\n';
        }
      }
      table[in]=LookupInfo();
    }
    return false;
  }
  void CtagsInterpreterCallback::AddTagFile(llvm::StringRef path) {
    //llvm::outs()<<"Tagfile generated from: "<<path<<"\n";
    TagFileWrapper tf(path);
    if(!tf.emptyFile())
        tags.push_back(tf);
  }

  CtagsInterpreterCallback::CtagsInterpreterCallback
  (cling::Interpreter* interp) :
    InterpreterCallbacks(interp,true),
    ip(interp){
      llvm::SmallVector<std::string,10> paths;
      interp->GetIncludePaths(paths,true,false);
      for(auto p:paths)
      {
          //llvm::outs()<<p<<"\n";
          //Takes too long
      }
      //AddTagFile("/usr/include/c++/");

  }

}
