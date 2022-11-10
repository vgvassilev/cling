//--------------------------------------------------------------------*- C++ -*-
// CLING - the C++ LLVM-based InterpreterG :)
// author:  Vassil Vassilev <vvasilev@cern.ch>
//
// This file is dual-licensed: you can choose to license it under the University
// of Illinois Open Source License or the GNU Lesser General Public License. See
// LICENSE.TXT for details.
//------------------------------------------------------------------------------

#include "cling/Interpreter/InterOp.h"
#include "cling/Interpreter/Interpreter.h"
#include "cling/Utils/AST.h"

#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/GlobalDecl.h"
#include "clang/AST/Mangle.h"
#include "clang/AST/RecordLayout.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Sema/Sema.h"
#include "clang/Sema/Lookup.h"

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/raw_os_ostream.h"

#include <dlfcn.h>
#include <sstream>

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
    return llvm::isa_and_nonnull<clang::EnumDecl>(D)
        || llvm::isa_and_nonnull<clang::EnumConstantDecl>(D);
  }

  bool IsEnumType(TCppType_t type) {
    QualType QT = QualType::getFromOpaquePtr(type);
    return QT->isEnumeralType();
  }

  size_t GetSizeOfType(TCppSema_t sema, TCppType_t type) {
    auto S = (clang::Sema *) sema;
    QualType QT = QualType::getFromOpaquePtr(type);
    auto TI = S->getASTContext().getTypeInfo(QT);
    size_t TypeSize = TI.Width;
    return TypeSize/8;
  }

  bool IsVariable(TCppScope_t scope) {
    auto *D = (clang::Decl *)scope;
    return llvm::isa_and_nonnull<clang::VarDecl>(D);
  }

  std::string GetName(TCppType_t klass) {
    auto *D = (clang::NamedDecl *) klass;

    if (llvm::isa_and_nonnull<TranslationUnitDecl>(D)) {
      return "";
    }

    if (auto *ND = llvm::dyn_cast_or_null<NamedDecl>(D)) {
      return ND->getNameAsString();
    }

    return "<unnamed>";
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
             llvm::isa_and_nonnull<RecordDecl>(ND)    ||
             llvm::isa_and_nonnull<ClassTemplateDecl>(ND)))
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

  TCppScope_t GetScopeFromType(TCppType_t type)
  {
    QualType QT = QualType::getFromOpaquePtr(type);
    if (auto* Type = QT.getTypePtrOrNull()) {
      Type = Type->getPointeeOrArrayElementType();
      Type = Type->getUnqualifiedDesugaredType();
      return (TCppScope_t)Type->getAsCXXRecordDecl();
    }
    return 0;
  }

  TCppIndex_t GetNumBases(TCppScope_t klass)
  {
    auto *D = (Decl *) klass;

    if (auto *CXXRD = llvm::dyn_cast_or_null<CXXRecordDecl>(D))
      return CXXRD->getNumBases();

    return 0;
  }

  TCppScope_t GetBaseClass(TCppScope_t klass, TCppIndex_t ibase)
  {
    auto *D = (Decl *) klass;
    auto *CXXRD = llvm::dyn_cast_or_null<CXXRecordDecl>(D);
    if (!CXXRD || CXXRD->getNumBases() <= ibase) return 0;

    auto type = (CXXRD->bases_begin() + ibase)->getType();
    if (auto RT = llvm::dyn_cast<RecordType>(type)) {
      return (TCppScope_t) RT->getDecl()->getCanonicalDecl();
    } else if (auto TST = llvm::dyn_cast<clang::TemplateSpecializationType>(type)) {
      return (TCppScope_t) TST->getTemplateName()
          .getAsTemplateDecl()->getCanonicalDecl();
    }

    return 0;
  }

  bool IsSubclass(TCppScope_t derived, TCppScope_t base)
  {
    if (derived == base)
      return true;

    auto *derived_D = (clang::Decl *) derived;
    auto *base_D = (clang::Decl *) base;

    if (!derived_D ||
        !base_D    ||
        llvm::isa<TranslationUnitDecl>(derived_D) ||
        llvm::isa<TranslationUnitDecl>(base_D))
        return false;

    if (auto derived_CXXRD = llvm::dyn_cast_or_null<CXXRecordDecl>(derived_D))
      if (auto base_CXXRD = llvm::dyn_cast_or_null<CXXRecordDecl>(base_D))
        return derived_CXXRD->isDerivedFrom(base_CXXRD);
    return false;
  }

  std::vector<TCppFunction_t> GetClassMethods(TCppScope_t klass)
  {
    auto *D = (clang::Decl *) klass;

    if (auto *CXXRD = llvm::dyn_cast_or_null<CXXRecordDecl>(D)) {
      std::vector<TCppFunction_t> methods;
      for (auto it = CXXRD->method_begin(), end = CXXRD->method_end();
              it != end; it++) {
        methods.push_back((TCppFunction_t) *it);
      }
      return methods;
    }
    return {};
  }

  std::vector<TCppFunction_t> GetFunctionsUsingName(
        TCppSema_t sema, TCppScope_t scope, const std::string& name)
  {
    auto *D = (Decl *) scope;
    std::vector<TCppFunction_t> funcs;
    llvm::StringRef Name(name);
    auto *S = (Sema *) sema;
    DeclarationName DName = &S->Context.Idents.get(name);
    clang::LookupResult R(*S,
                          DName,
                          SourceLocation(),
                          Sema::LookupOrdinaryName,
                          Sema::ForVisibleRedeclaration);

    cling::utils::Lookup::Named(S, R, Decl::castToDeclContext(D));

    if (R.empty())
      return funcs;

    R.resolveKind();

    for (LookupResult::iterator Res = R.begin(), ResEnd = R.end();
         Res != ResEnd;
         ++Res) {
      if (llvm::isa<FunctionDecl>(*Res)) {
        funcs.push_back((TCppFunction_t) *Res);
      }
    }

    return funcs;
  }

  TCppType_t GetFunctionReturnType(TCppFunction_t func)
  {
    auto *D = (clang::Decl *) func;
    if (auto *FD = llvm::dyn_cast_or_null<clang::FunctionDecl>(D)) {
        return FD->getReturnType().getAsOpaquePtr();
    }

    return 0;
  }

  TCppIndex_t GetFunctionNumArgs(TCppFunction_t func)
  {
    auto *D = (clang::Decl *) func;
    if (auto *FD = llvm::dyn_cast_or_null<FunctionDecl>(D)) {
      return FD->getNumParams();
    }
    return 0;
  }

  TCppIndex_t GetFunctionRequiredArgs(TCppFunction_t func)
  {
    auto *D = (clang::Decl *) func;
    if (auto *FD = llvm::dyn_cast_or_null<FunctionDecl> (D)) {
      return FD->getMinRequiredArguments();
    }
    return 0;
  }

  TCppType_t GetFunctionArgType(TCppFunction_t func, TCppIndex_t iarg)
  {
    auto *D = (clang::Decl *) func;

    if (auto *FD = llvm::dyn_cast_or_null<clang::FunctionDecl>(D)) {
        if (iarg < FD->getNumParams()) {
            auto *PVD = FD->getParamDecl(iarg);
            return PVD->getOriginalType().getAsOpaquePtr();
        }
    }

    return 0;
  }

  void get_function_params(std::stringstream &ss, FunctionDecl *FD,
          bool show_formal_args = false, TCppIndex_t max_args = -1)
  {
    ss << "(";
    if (max_args == (size_t) -1) max_args = FD->param_size();
    max_args = std::min(max_args, FD->param_size());
    size_t i = 0;
    auto &Ctxt = FD->getASTContext();
    for (auto PI = FD->param_begin(), end = FD->param_end();
              PI != end; PI++) {
      if (i >= max_args)
          break;
      ss << (*PI)->getType().getAsString();
      if (show_formal_args) {
        ss << " " << (*PI)->getNameAsString();
        if ((*PI)->hasDefaultArg()) {
          ss << " = ";
          raw_os_ostream def_arg_os(ss);
          (*PI)->getDefaultArg()->printPretty(def_arg_os, nullptr,
                  PrintingPolicy(Ctxt.getLangOpts()));
        }
      }
      if (++i < max_args) {
        ss << ", ";
      }
    }
    ss << ")";
  }

  std::string GetFunctionSignature(
          TCppFunction_t func, bool show_formal_args, TCppIndex_t max_args)
  {
    auto *D = (clang::Decl *) func;
    if (auto *FD = llvm::dyn_cast_or_null<FunctionDecl>(D)) {
      std::stringstream sig;

      sig << FD->getReturnType().getAsString()
          << (FD->getReturnType()->isPointerType() ? "" : " ");
      get_function_params(sig, FD, show_formal_args, max_args);
      return sig.str();
    }
    return "<unknown>";
  }

  std::string GetFunctionPrototype(TCppFunction_t func, bool show_formal_args)
  {
    auto *D = (clang::Decl *) func;
    if (auto *FD = llvm::dyn_cast_or_null<FunctionDecl>(D)) {
      std::stringstream proto;

      proto << FD->getReturnType().getAsString()
            << (FD->getReturnType()->isPointerType() ? "" : " ");
      proto << FD->getQualifiedNameAsString();
      get_function_params(proto, FD, show_formal_args);
      return proto.str();
    }
    return "<unknown>";
  }

  bool IsTemplatedFunction(TCppFunction_t func)
  {
    auto *D = (Decl *) func;
    if (llvm::isa_and_nonnull<FunctionTemplateDecl>(D)) {
      return true;
    }

    if (auto *FD = llvm::dyn_cast_or_null<FunctionDecl>(D)) {
      auto TK = FD->getTemplatedKind();
      return TK == FunctionDecl::TemplatedKind::
                   TK_FunctionTemplateSpecialization
            || TK == FunctionDecl::TemplatedKind::
                     TK_DependentFunctionTemplateSpecialization
            || TK == FunctionDecl::TemplatedKind::TK_FunctionTemplate;
    }

    return false;
  }

  bool ExistsFunctionTemplate(TCppSema_t sema, const std::string& name,
          TCppScope_t parent)
  {
    DeclContext *Within = 0;
    if (parent) {
      auto *D = (Decl *)parent;
      Within = llvm::dyn_cast<DeclContext>(D);
    }

    auto *S = (Sema *) sema;
    auto *ND = cling::utils::Lookup::Named(S, name, Within);

    if (!ND)
      return false;

    return true;
  }

  bool CheckMethodAccess(TCppFunction_t method, AccessSpecifier AS)
  {
    auto *D = (Decl *) method;
    if (auto *CXXMD = llvm::dyn_cast_or_null<CXXMethodDecl>(D)) {
      return CXXMD->getAccess() == AS;
    }

    return false;
  }

  bool IsPublicMethod(TCppFunction_t method)
  {
    return CheckMethodAccess(method, AccessSpecifier::AS_public);
  }

  bool IsProtectedMethod(TCppFunction_t method)
  {
    return CheckMethodAccess(method, AccessSpecifier::AS_protected);
  }

  bool IsPrivateMethod(TCppFunction_t method)
  {
    return CheckMethodAccess(method, AccessSpecifier::AS_private);
  }

  bool IsConstructor(TCppFunction_t method)
  {
    auto *D = (Decl *) method;
    return llvm::isa_and_nonnull<CXXConstructorDecl>(D);
  }

  bool IsDestructor(TCppFunction_t method)
  {
    auto *D = (Decl *) method;
    return llvm::isa_and_nonnull<CXXDestructorDecl>(D);
  }

  bool IsStaticMethod(TCppFunction_t method)
  {
    auto *D = (Decl *) method;
    if (auto *CXXMD = llvm::dyn_cast_or_null<CXXMethodDecl>(D)) {
      return CXXMD->isStatic();
    }

    return false;
  }

  TCppFuncAddr_t GetFunctionAddress(TInterp_t interp, TCppFunction_t method)
  {
    auto *I = (cling::Interpreter *) interp;
    auto *D = (Decl *) method;

    const auto get_mangled_name = [](FunctionDecl* FD) {
      auto MangleCtxt = FD->getASTContext().createMangleContext();

      if (!MangleCtxt->shouldMangleDeclName(FD)) {
        return FD->getNameInfo().getName().getAsString();
      }
    
      std::string mangled_name;
      llvm::raw_string_ostream ostream(mangled_name);
    
      MangleCtxt->mangleName(FD, ostream);
    
      ostream.flush();
      delete MangleCtxt;
    
      return mangled_name;
    };

    if (auto *FD = llvm::dyn_cast_or_null<FunctionDecl>(D))
      return (TCppFuncAddr_t)I->getAddressOfGlobal(get_mangled_name(FD));

    return 0;
  }

  std::vector<TCppScope_t> GetDatamembers(TCppScope_t scope)
  {
    auto *D = (Decl *) scope;

    if (auto *CXXRD = llvm::dyn_cast_or_null<CXXRecordDecl>(D)) {
      std::vector<TCppScope_t> datamembers;
      for (auto it = CXXRD->field_begin(), end = CXXRD->field_end();
              it != end ; it++) {
        datamembers.push_back((TCppScope_t) *it);
      }

      return datamembers;
    }

    return {};
  }

  TCppType_t GetVariableType(TCppScope_t var)
  {
    auto D = (Decl *) var;

    if (auto DD = llvm::dyn_cast_or_null<DeclaratorDecl>(D)) {
        return DD->getType().getAsOpaquePtr();
    }

    return 0;
  }

  intptr_t GetVariableOffset(TInterp_t interp, TCppScope_t var)
  {
    auto *D = (Decl *) var;
    auto *P = (Decl *) GetParentScope(var);
    auto *I = (cling::Interpreter *) interp;
    auto *S = &I->getCI()->getSema();

    if (P == GetGlobalScope(S) || IsNamespace(P)) {
      if (auto *VD = llvm::dyn_cast_or_null<VarDecl>(D)) {
        auto GD = GlobalDecl(VD);
        std::string mangledName;
        cling::utils::Analyze::maybeMangleDeclName(GD, mangledName);
        auto address = dlsym(/*whole_process=*/0, mangledName.c_str());
        if (!address)
          address = I->getAddressOfGlobal(GD);
        return (intptr_t) address;
      }
    }

    if (auto *CXXRD = llvm::dyn_cast_or_null<CXXRecordDecl>(P)) {
      if (llvm::isa_and_nonnull<VarDecl>(D)) {
        return (intptr_t) I->process(
               (std::string("&") + GetCompleteName(D) + ";").c_str());
      }

      if (auto *FD = llvm::dyn_cast_or_null<FieldDecl>(D)) {
        auto &Cxt = S->getASTContext();
        return (intptr_t) Cxt.toCharUnitsFromBits(Cxt.getASTRecordLayout(CXXRD)
               .getFieldOffset(FD->getFieldIndex())).getQuantity();
      }
    }

    return 0;
  }

  bool CheckVariableAccess(TCppScope_t var, AccessSpecifier AS)
  {
    auto *D = (Decl *) var;
    if (auto *CXXMD = llvm::dyn_cast_or_null<DeclaratorDecl>(D)) {
      return CXXMD->getAccess() == AS;
    }

    return false;
  }

  bool IsPublicVariable(TCppScope_t var)
  {
    return CheckVariableAccess(var, AccessSpecifier::AS_public);
  }

  bool IsProtectedVariable(TCppScope_t var)
  {
    return CheckVariableAccess(var, AccessSpecifier::AS_protected);
  }

  bool IsPrivateVariable(TCppScope_t var)
  {
    return CheckVariableAccess(var, AccessSpecifier::AS_private);
  }

  bool IsStaticVariable(TCppScope_t var)
  {
    auto *D = (Decl *) var;
    if (llvm::isa_and_nonnull<VarDecl>(D)) {
      return true;
    }

    return false;
  }

  bool IsConstVariable(TCppScope_t var)
  {
    auto *D = (clang::Decl *) var;

    if (auto *VD = llvm::dyn_cast_or_null<ValueDecl>(D)) {
      return VD->getType().isConstQualified();
    }

    return false;
  }

  std::string GetTypeAsString(TCppType_t var)
  {
      QualType QT = QualType::getFromOpaquePtr(var);
      return QT.getAsString();
  }

  TCppType_t GetCanonicalType(TCppType_t type)
  {
      QualType QT = QualType::getFromOpaquePtr(type);
      return QT.getCanonicalType().getAsOpaquePtr();
  }
} // end namespace InterOp

} // end namespace cling

