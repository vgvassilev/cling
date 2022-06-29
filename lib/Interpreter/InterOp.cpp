//--------------------------------------------------------------------*- C++ -*-
// CLING - the C++ LLVM-based InterpreterG :)
// author:  Vassil Vassilev <vvasilev@cern.ch>
//
// This file is dual-licensed: you can choose to license it under the University
// of Illinois Open Source License or the GNU Lesser General Public License. See
// LICENSE.TXT for details.
//------------------------------------------------------------------------------

#include "cling/Interpreter/InterOp.h"
#include "cling/Utils/AST.h"

#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/RecordLayout.h"
#include "clang/Sema/Sema.h"
#include "clang/Sema/Lookup.h"

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Casting.h"

namespace cling {
namespace InterOp {
  using namespace clang;
  using namespace llvm;

  bool IsNamespace(TCppScope_t scope) {
    Decl *D = static_cast<Decl*>(scope);
    return isa<NamespaceDecl>(D);
  }
  // See TClingClassInfo::IsLoaded
  bool IsComplete(TCppScope_t scope) {
    if (!scope)
      return false;
    Decl *D = static_cast<Decl*>(scope);
    if (auto *CXXRD = dyn_cast<CXXRecordDecl>(D))
      return CXXRD->hasDefinition();
    else if (auto *TD = dyn_cast<TagDecl>(D))
      return TD->getDefinition();

    // Everything else is considered complete.
    return true;
  }

  size_t SizeOf(TCppScope_t scope) {
    assert (scope);
    if (!IsComplete(scope))
      return 0;

    if (auto *RD = dyn_cast<RecordDecl>(static_cast<Decl*>(scope))) {
      ASTContext &Context = RD->getASTContext();
      const ASTRecordLayout &Layout = Context.getASTRecordLayout(RD);
      return Layout.getSize().getQuantity();
    }

    return 0;
  }

  bool IsBuiltin(TCppType_t type) {
    QualType Ty = QualType::getFromOpaquePtr(type);
    if (Ty->isBuiltinType() || Ty->isAnyComplexType())
      return true;
    // FIXME: Figure out how to avoid the string comparison.
    return llvm::StringRef(Ty.getAsString()).contains("complex");
  }

  bool IsTemplate(TCppScope_t handle) {
    auto *D = (clang::Decl *)handle;
    return llvm::isa_and_nonnull<clang::TemplateDecl>(D);
  }

  bool IsAbstract(TCppType_t klass) {
    auto *D = (clang::Decl *)klass;
    if (auto *CXXRD = llvm::dyn_cast_or_null<clang::CXXRecordDecl>(D))
      return CXXRD->isAbstract();

    return false;
  }

  bool IsEnum(TCppScope_t handle) {
    auto *D = (clang::Decl *)handle;
    return llvm::isa_and_nonnull<clang::EnumDecl>(D);
  }

  bool IsVariable(TCppScope_t scope) {
    auto *D = (clang::Decl *)scope;
    return llvm::isa_and_nonnull<clang::VarDecl>(D);
  }

  std::string GetName(TCppType_t klass) {
    // In cppyy GlobalScope is represented by empty string
    // if (klass == Cppyy::NewGetGlobalScope())
    //     return "";

    auto *D = (clang::NamedDecl *) klass;
    return D->getNameAsString();
  }

  std::string GetCompleteName(TCppType_t klass)
  {
    auto *D = (Decl *) klass;
    if (auto *ND = llvm::dyn_cast_or_null<NamedDecl>(D)) {
      return ND->getQualifiedNameAsString();
    }

    if (llvm::isa_and_nonnull<TranslationUnitDecl>(D)) {
      return "";
    }

    return "<unnamed>";
  }

  std::vector<TCppScope_t> GetUsingNamespaces(TCppScope_t scope) {
    auto *D = (clang::Decl *) scope;

    if (auto *DC = llvm::dyn_cast_or_null<clang::DeclContext>(D)) {
      std::vector<TCppScope_t> namespaces;
      for (auto UD : DC->using_directives()) {
        namespaces.push_back((TCppScope_t) UD->getNominatedNamespace());
      }
      return namespaces;
    }

    return {};
  }

  TCppScope_t GetGlobalScope(TCppSema_t sema)
  {
    auto *S = (Sema *) sema;
    return S->getASTContext().getTranslationUnitDecl();
  }

  TCppScope_t GetScope(TCppSema_t sema, const std::string &name, TCppScope_t parent)
  {
    if (name == "")
        return GetGlobalScope(sema);

    DeclContext *Within = 0;
    if (parent) {
      auto *D = (Decl *)parent;
      Within = llvm::dyn_cast<DeclContext>(D);
    }

    auto *S = (Sema *) sema;
    auto *ND = cling::utils::Lookup::Named(S, name, Within);
    if (!(ND == (NamedDecl *) -1) &&
            (llvm::isa_and_nonnull<NamespaceDecl>(ND) ||
             llvm::isa_and_nonnull<RecordDecl>(ND)))
      return (TCppScope_t)(ND->getCanonicalDecl());

    return 0;
  }

  TCppScope_t GetScopeFromCompleteName(TCppSema_t sema, const std::string &name)
  {
    std::string delim = "::";
    size_t start = 0;
    size_t end = name.find(delim);
    TCppScope_t curr_scope = 0;
    auto *S = (Sema *) sema;
    while (end != std::string::npos)
    {
      curr_scope = GetScope(S, name.substr(start, end - start), curr_scope);
      start = end + delim.length();
      end = name.find(delim, start);
    }
    return GetScope(S, name.substr(start, end), curr_scope);
  }

  TCppScope_t GetNamed(TCppSema_t sema, const std::string &name, TCppScope_t parent)
  {
    clang::DeclContext *Within = 0;
    if (parent) {
      auto *D = (clang::Decl *)parent;
      Within = llvm::dyn_cast<clang::DeclContext>(D);
    }

    auto *S = (Sema *) sema;
    auto *ND = cling::utils::Lookup::Named(S, name, Within);
    if (ND && ND != (clang::NamedDecl*) -1) {
      return (TCppScope_t)(ND->getCanonicalDecl());
    }

    return 0;
  }

  TCppScope_t GetParentScope(TCppScope_t scope)
  {
    auto *D = (clang::Decl *) scope;

    if (llvm::isa_and_nonnull<TranslationUnitDecl>(D)) {
      return 0;
    }
    auto *ParentDC = D->getDeclContext();

    if (!ParentDC)
      return 0;

    return (TCppScope_t) clang::Decl::castFromDeclContext(
            ParentDC)->getCanonicalDecl();
  }
} // end namespace InterOp

} // end namespace cling

