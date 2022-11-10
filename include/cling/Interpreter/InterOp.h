//--------------------------------------------------------------------*- C++ -*-
// CLING - the C++ LLVM-based InterpreterG :)
// author:  Vassil Vassilev <vvasilev@cern.ch>
//
// This file is dual-licensed: you can choose to license it under the University
// of Illinois Open Source License or the GNU Lesser General Public License. See
// LICENSE.TXT for details.
//------------------------------------------------------------------------------

#ifndef CLING_INTERPRETER_INTEROP_H
#define CLING_INTERPRETER_INTEROP_H

#include <string>
#include <vector>

namespace cling {
namespace InterOp {
  using TCppIndex_t = size_t;
  using TCppScope_t = void*;
  using TCppType_t = void*;
  using TCppFunction_t = void*;
  using TCppFuncAddr_t = void*;
  using TCppSema_t = void*;
  using TInterp_t = void*;
  typedef void (*CallFuncWrapper_t)(void*, int, void**, void*);

  bool IsNamespace(TCppScope_t scope);
  // See TClingClassInfo::IsLoaded
  bool IsComplete(TCppScope_t scope);

  size_t SizeOf(TCppScope_t scope);
  bool IsBuiltin(TCppType_t type);

  bool IsTemplate(TCppScope_t handle);

  bool IsAbstract(TCppType_t klass);

  bool IsEnum(TCppScope_t handle);

  bool IsEnumType(TCppType_t type);

  size_t GetSizeOfType(TCppSema_t sema, TCppType_t type);

  bool IsVariable(TCppScope_t scope);

  std::string GetName(TCppType_t klass);

  std::string GetCompleteName(TCppType_t klass);

  std::vector<TCppScope_t> GetUsingNamespaces(TCppScope_t scope);

  TCppScope_t GetGlobalScope(TCppSema_t sema);

  TCppScope_t GetScope(TCppSema_t sema, const std::string &name,
                       TCppScope_t parent = 0);

  TCppScope_t GetScopeFromCompleteName(TCppSema_t sema, const std::string &name);

  TCppScope_t GetNamed(TCppSema_t sema, const std::string &name, TCppScope_t parent);

  TCppScope_t GetParentScope(TCppScope_t scope);

  TCppScope_t GetScopeFromType(TCppType_t type);

  TCppIndex_t GetNumBases(TCppType_t klass);

  TCppScope_t GetBaseClass(TCppType_t klass, TCppIndex_t ibase);

  bool IsSubclass(TCppScope_t derived, TCppScope_t base);

  std::vector<TCppFunction_t> GetClassMethods(TCppScope_t klass);

  std::vector<TCppFunction_t> GetFunctionsUsingName(
        TCppSema_t sema, TCppScope_t scope, const std::string& name);

  TCppType_t GetFunctionReturnType(TCppFunction_t func);

  TCppIndex_t GetFunctionNumArgs(TCppFunction_t func);

  TCppIndex_t GetFunctionRequiredArgs(TCppFunction_t func);

  TCppType_t GetFunctionArgType(TCppFunction_t func, TCppIndex_t iarg);

  std::string GetFunctionSignature(
          TCppFunction_t func,
          bool show_formal_args = false,
          TCppIndex_t max_args = -1);

  std::string GetFunctionPrototype(TCppFunction_t func, bool show_formal_args = false);

  bool IsTemplatedFunction(TCppFunction_t func);

  bool ExistsFunctionTemplate(TCppSema_t sema, const std::string& name,
          TCppScope_t parent = 0);

  bool IsPublicMethod(TCppFunction_t method);

  bool IsProtectedMethod(TCppFunction_t method);

  bool IsPrivateMethod(TCppFunction_t method);

  bool IsConstructor(TCppFunction_t method);

  bool IsDestructor(TCppFunction_t method);

  bool IsStaticMethod(TCppFunction_t method);

  TCppFuncAddr_t GetFunctionAddress(TInterp_t interp, TCppFunction_t method);

  std::vector<TCppScope_t> GetDatamembers(TCppScope_t scope);

  TCppType_t GetVariableType(TCppScope_t var);

  intptr_t GetVariableOffset(TInterp_t interp, TCppScope_t var);

  bool IsPublicVariable(TCppScope_t var);

  bool IsProtectedVariable(TCppScope_t var);

  bool IsPrivateVariable(TCppScope_t var);

  bool IsStaticVariable(TCppScope_t var);

  bool IsConstVariable(TCppScope_t var);

  std::string GetTypeAsString(TCppType_t type);

  TCppType_t GetCanonicalType(TCppType_t type);

  CallFuncWrapper_t GetFunctionCallWrapper(TInterp_t interp,
                                           TCppFunction_t func);

} // end namespace InterOp

} // end namespace cling

#endif // CLING_INTERPRETER_INTEROP_H
