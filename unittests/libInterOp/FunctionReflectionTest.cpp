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
