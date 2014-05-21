#include "cling/TagsExtension/Callback.h"
#include "clang/Basic/Diagnostic.h"
#include "llvm/ADT/SmallVector.h"
#include "clang/Sema/Sema.h"
#include "llvm/Support/Path.h"

namespace cling {

  bool AutoloadCallback::LookupObject (clang::LookupResult &R, clang::Scope *){
    std::string in=R.getLookupName().getAsString();
    auto & sema= m_Interpreter->getSema();
//    const char fmt[]="Note: '%0' can be found in %1";
//    const char note[]="Type : %0 , Full Path: %1";

    auto id=sema.getDiagnostics().getCustomDiagID
            (clang::DiagnosticsEngine::Level::Warning,
                "Note: '%0' can be found in %1");
    auto idn=sema.getDiagnostics().getCustomDiagID
            (clang::DiagnosticsEngine::Level::Note,
                "Type : %0 , Full Path: %1");

    for (auto it=m_Tags->begin(in); it!=m_Tags->end(in); ++it)
    {
      auto lookup=it->second;
//      llvm::outs() << lookup.header
//                  << '\t' << lookup.name
//                  << '\t' <<lookup.type
//                  << '\n';
      sema.Diags.Report(id)
              << lookup.name
              << llvm::sys::path::filename(lookup.header);

      sema.Diags.Report(idn)
              << lookup.type
              << lookup.header;

    }

    return false;
  }


  AutoloadCallback::AutoloadCallback
  (cling::Interpreter* interp, TagManager *t) :
    InterpreterCallbacks(interp,true),
    m_Interpreter(interp),
    m_Tags(t) {

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
    return m_Tags;
  }

}//end namespace cling//end namespace cling
