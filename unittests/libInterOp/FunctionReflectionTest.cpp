#include "Utils.h"

#include "cling/Interpreter/InterOp.h"

#include "gtest/gtest.h"


TEST(FunctionReflectionTest, GetClassMethods) {
  std::vector<Decl*> Decls;
  std::string code = R"(
    class A {
    public:
      int f1(int a, int b) { return a + b; }
      const A *f2() const { return this; }
    private:
      int f3() { return 0; }
      void f4() {}
    protected:
      int f5(int i) { return i; }
    };
    )";

  GetAllTopLevelDecls(code, Decls);
  auto methods = InterOp::GetClassMethods(Decls[0]);

  auto get_method_name = [](InterOp::TCppFunction_t method) {
    return InterOp::GetCompleteName(method);
  };

  EXPECT_EQ(get_method_name(methods[0]), "A::f1");
  EXPECT_EQ(get_method_name(methods[1]), "A::f2");
  EXPECT_EQ(get_method_name(methods[2]), "A::f3");
  EXPECT_EQ(get_method_name(methods[3]), "A::f4");
  EXPECT_EQ(get_method_name(methods[4]), "A::f5");
}

TEST(FunctionReflectionTest, GetFunctionsUsingName) {
  std::vector<Decl*> Decls;
  std::string code = R"(
    class A {
    public:
      int f1(int a, int b) { return a + b; }
      int f1(int a) { return f1(a, 10); }
      int f1() { return f1(10); }
    private:
      int f2() { return 0; }
    protected:
      int f3(int i) { return i; }
    };

    namespace N {
      int f4(int a) { return a + 1; }
      int f4() { return 0; }
    }
    )";

  GetAllTopLevelDecls(code, Decls);

  // This lambda can take in the scope and the name of the function
  // and check if GetFunctionsUsingName is returning a vector of functions
  // with size equal to number_of_overloads
  auto test_get_funcs_using_name = [&](InterOp::TCppScope_t scope,
          const std::string name, std::size_t number_of_overloads) {
    Sema *S = &Interp->getCI()->getSema();
    auto Funcs = InterOp::GetFunctionsUsingName(S, scope, name);

    // Check if the number of functions returned is equal to the
    // number_of_overloads given by the user
    EXPECT_TRUE(Funcs.size() == number_of_overloads);
    for (auto *F : Funcs) {
      // Check if the fully scoped name of the function matches its
      // expected fully scoped name
      EXPECT_EQ(InterOp::GetCompleteName(F),
              InterOp::GetCompleteName(scope) + "::" + name);
    }
  };

  test_get_funcs_using_name(Decls[0], "f1", 3);
  test_get_funcs_using_name(Decls[0], "f2", 1);
  test_get_funcs_using_name(Decls[0], "f3", 1);
  test_get_funcs_using_name(Decls[1], "f4", 2);
}

TEST(FunctionReflectionTest, GetFunctionReturnTypeAsString) {
  std::vector<Decl*> Decls, SubDecls;
  std::string code = R"(
    namespace N { class C {}; }
    enum Switch { OFF, ON };

    class A {
      A (int i) { i++; }
      int f () { return 0; }
    };


    void f1() {}
    double f2() { return 0.2; }
    Switch f3() { return ON; }
    N::C f4() { return N::C(); }
    N::C *f5() { return new N::C(); }
    const N::C f6() { return N::C(); }
    volatile N::C f7() { return N::C(); }
    const volatile N::C f8() { return N::C(); }
    )";

  GetAllTopLevelDecls(code, Decls);
  GetAllSubDecls(Decls[2], SubDecls);

  EXPECT_EQ(InterOp::GetFunctionReturnTypeAsString(Decls[3]), "void");
  EXPECT_EQ(InterOp::GetFunctionReturnTypeAsString(Decls[4]), "double");
  EXPECT_EQ(InterOp::GetFunctionReturnTypeAsString(Decls[5]), "enum Switch");
  EXPECT_EQ(InterOp::GetFunctionReturnTypeAsString(Decls[6]), "N::C");
  EXPECT_EQ(InterOp::GetFunctionReturnTypeAsString(Decls[7]), "N::C *");
  EXPECT_EQ(InterOp::GetFunctionReturnTypeAsString(Decls[8]), "const N::C");
  EXPECT_EQ(InterOp::GetFunctionReturnTypeAsString(Decls[9]), "volatile N::C");
  EXPECT_EQ(InterOp::GetFunctionReturnTypeAsString(Decls[10]), "const volatile N::C");
  EXPECT_EQ(InterOp::GetFunctionReturnTypeAsString(SubDecls[1]), "void");
  EXPECT_EQ(InterOp::GetFunctionReturnTypeAsString(SubDecls[2]), "int");
}

TEST(FunctionReflectionTest, GetFunctionNumArgs) {
  std::vector<Decl*> Decls, SubDecls;
  std::string code = R"(
    void f1() {}
    void f2(int i, double d, long l, char ch) {}
    void f3(int i, double d, long l = 0, char ch = 'a') {}
    void f4(int i = 0, double d = 0.0, long l = 0, char ch = 'a') {}
    )";

  GetAllTopLevelDecls(code, Decls);
  EXPECT_EQ(InterOp::GetFunctionNumArgs(Decls[0]), (size_t) 0);
  EXPECT_EQ(InterOp::GetFunctionNumArgs(Decls[1]), (size_t) 4);
  EXPECT_EQ(InterOp::GetFunctionNumArgs(Decls[2]), (size_t) 4);
  EXPECT_EQ(InterOp::GetFunctionNumArgs(Decls[3]), (size_t) 4);
}

TEST(FunctionReflectionTest, GetFunctionRequiredArgs) {
  std::vector<Decl*> Decls, SubDecls;
  std::string code = R"(
    void f1() {}
    void f2(int i, double d, long l, char ch) {}
    void f3(int i, double d, long l = 0, char ch = 'a') {}
    void f4(int i = 0, double d = 0.0, long l = 0, char ch = 'a') {}
    )";

  GetAllTopLevelDecls(code, Decls);
  EXPECT_EQ(InterOp::GetFunctionRequiredArgs(Decls[0]), (size_t) 0);
  EXPECT_EQ(InterOp::GetFunctionRequiredArgs(Decls[1]), (size_t) 4);
  EXPECT_EQ(InterOp::GetFunctionRequiredArgs(Decls[2]), (size_t) 2);
  EXPECT_EQ(InterOp::GetFunctionRequiredArgs(Decls[3]), (size_t) 0);
}
