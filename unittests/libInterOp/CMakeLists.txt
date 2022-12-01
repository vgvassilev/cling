set(LLVM_LINK_COMPONENTS
  Support
  )

add_cling_unittest(libInterOpTests
  ScopeReflectionTest.cpp
  FunctionReflectionTest.cpp
  VariableReflectionTest.cpp
  TypeReflectionTest.cpp
  EnumReflectionTest.cpp
  )

clang_target_link_libraries(libInterOpTests
  PRIVATE
  clingInterpreter
  clangAST
  clangBasic
  clangLex
  )
