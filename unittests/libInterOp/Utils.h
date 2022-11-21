#ifndef CLING_UNITTESTS_LIBINTEROP_UTILS_H
#define CLING_UNITTESTS_LIBINTEROP_UTILS_H

#include "cling/Interpreter/Interpreter.h"
#include "cling/Interpreter/Transaction.h"

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"

#include "clang/Config/config.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/Basic/Version.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Sema/Sema.h"

using namespace cling;
using namespace clang;
using namespace llvm;

// This function isn't referenced outside its translation unit, but it
// can't use the "static" keyword because its address is used for
// GetMainExecutable (since some platforms don't support taking the
// address of main, and some platforms can't implement GetMainExecutable
// without being given the address of a function in the main executable).
static std::string GetExecutablePath(const char *Argv0, void *MainAddr) {
  return sys::fs::getMainExecutable(Argv0, MainAddr);
}

static std::string MakeResourcesPath() {
  // Dir is bin/ or lib/, depending on where BinaryPath is.
  void *MainAddr = (void *)(intptr_t)GetExecutablePath;
  std::string BinaryPath = GetExecutablePath(/*Argv0=*/nullptr, MainAddr);

  // build/tools/clang/unittests/Interpreter/Executable -> build/
  StringRef Dir = sys::path::parent_path(BinaryPath);

  Dir = sys::path::parent_path(Dir);
  Dir = sys::path::parent_path(Dir);
  Dir = sys::path::parent_path(Dir);
  Dir = sys::path::parent_path(Dir);
  //Dir = sys::path::parent_path(Dir);
  SmallString<128> P(Dir);
  sys::path::append(P, Twine("lib") + CLANG_LIBDIR_SUFFIX, "clang",
                          CLANG_VERSION_STRING);

  return std::string(P.str());
}

static std::unique_ptr<Interpreter> createInterpreter() {
  std::string MainExecutableName =
    sys::fs::getMainExecutable(nullptr, nullptr);
  std::string ResourceDir = MakeResourcesPath();
  std::vector<const char *> ClingArgv = {"-resource-dir", ResourceDir.c_str()};
  ClingArgv.insert(ClingArgv.begin(), MainExecutableName.c_str());
  return make_unique<Interpreter>(ClingArgv.size(), &ClingArgv[0]);
}

static std::unique_ptr<Interpreter> Interp;

static void GetAllTopLevelDecls(const std::string& code, std::vector<Decl*>& Decls) {
  Interp.reset();
  Interp = createInterpreter();
  Transaction *T = nullptr;
  Interp->declare(code, &T);
  for (auto DCI = T->decls_begin(), E = T->decls_end(); DCI != E; ++DCI) {
    if (DCI->m_Call != Transaction::kCCIHandleTopLevelDecl)
      continue;
    assert(DCI->m_DGR.isSingleDecl());
    Decls.push_back(DCI->m_DGR.getSingleDecl());
  }
}

static void GetAllSubDecls(Decl *D, std::vector<Decl*>& SubDecls) {
  if (!isa_and_nonnull<DeclContext>(D))
    return;
  DeclContext *DC = Decl::castToDeclContext(D);
  for (auto DCI = DC->decls_begin(), E = DC->decls_end(); DCI != E; ++DCI) {
    SubDecls.push_back(*DCI);
  }
}

#endif // CLING_UNITTESTS_LIBINTEROP_UTILS_H

