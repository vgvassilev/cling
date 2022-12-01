#include "Utils.h"

#include "cling/Interpreter/InterOp.h"

#include "gtest/gtest.h"

// Check that the CharInfo table has been constructed reasonably.
TEST(ScopeReflectionTest, IsNamespace) {
  std::vector<Decl*> Decls;
  GetAllTopLevelDecls("namespace N {} class C{}; int I;", Decls);
  EXPECT_TRUE(InterOp::IsNamespace(Decls[0]));
  EXPECT_FALSE(InterOp::IsNamespace(Decls[1]));
  EXPECT_FALSE(InterOp::IsNamespace(Decls[2]));
}

TEST(ScopeReflectionTest, IsComplete) {
  std::vector<Decl*> Decls;
  GetAllTopLevelDecls("namespace N {} class C{}; int I; struct S; enum E : int; union U{};",
                      Decls);
  EXPECT_TRUE(InterOp::IsComplete(Decls[0]));
  EXPECT_TRUE(InterOp::IsComplete(Decls[1]));
  EXPECT_TRUE(InterOp::IsComplete(Decls[2]));
  EXPECT_FALSE(InterOp::IsComplete(Decls[3]));
  EXPECT_FALSE(InterOp::IsComplete(Decls[4]));
  EXPECT_TRUE(InterOp::IsComplete(Decls[5]));
}

TEST(ScopeReflectionTest, SizeOf) {
  std::vector<Decl*> Decls;
  std::string code = R"(namespace N {} class C{}; int I; struct S;
                        enum E : int; union U{}; class Size4{int i;};
                        struct Size16 {short a; double b;};
                       )";
  GetAllTopLevelDecls(code, Decls);
  EXPECT_EQ(InterOp::SizeOf(Decls[0]), (size_t)0);
  EXPECT_EQ(InterOp::SizeOf(Decls[1]), (size_t)1);
  EXPECT_EQ(InterOp::SizeOf(Decls[2]), (size_t)0);
  EXPECT_EQ(InterOp::SizeOf(Decls[3]), (size_t)0);
  EXPECT_EQ(InterOp::SizeOf(Decls[4]), (size_t)0);
  EXPECT_EQ(InterOp::SizeOf(Decls[5]), (size_t)1);
  EXPECT_EQ(InterOp::SizeOf(Decls[6]), (size_t)4);
  EXPECT_EQ(InterOp::SizeOf(Decls[7]), (size_t)16);
}

TEST(ScopeReflectionTest, IsBuiltin) {
  // static std::set<std::string> g_builtins =
  // {"bool", "char", "signed char", "unsigned char", "wchar_t", "short", "unsigned short",
  //  "int", "unsigned int", "long", "unsigned long", "long long", "unsigned long long",
  //  "float", "double", "long double", "void"}

  Interp.reset();
  Interp = createInterpreter();
  ASTContext &C = Interp->getCI()->getASTContext();
  EXPECT_TRUE(InterOp::IsBuiltin(C.BoolTy.getAsOpaquePtr()));
  EXPECT_TRUE(InterOp::IsBuiltin(C.CharTy.getAsOpaquePtr()));
  EXPECT_TRUE(InterOp::IsBuiltin(C.SignedCharTy.getAsOpaquePtr()));
  EXPECT_TRUE(InterOp::IsBuiltin(C.VoidTy.getAsOpaquePtr()));
  // ...

  // complex
  EXPECT_TRUE(InterOp::IsBuiltin(C.getComplexType(C.FloatTy).getAsOpaquePtr()));
  EXPECT_TRUE(InterOp::IsBuiltin(C.getComplexType(C.DoubleTy).getAsOpaquePtr()));
  EXPECT_TRUE(InterOp::IsBuiltin(C.getComplexType(C.LongDoubleTy).getAsOpaquePtr()));
  EXPECT_TRUE(InterOp::IsBuiltin(C.getComplexType(C.Float128Ty).getAsOpaquePtr()));

  // std::complex
  std::vector<Decl*> Decls;
  Interp->declare("#include <complex>");
  Sema &S = Interp->getCI()->getSema();
  auto lookup = S.getStdNamespace()->lookup(&C.Idents.get("complex"));
  auto *CTD = cast<ClassTemplateDecl>(lookup.front());
  for (ClassTemplateSpecializationDecl *CTSD : CTD->specializations())
    EXPECT_TRUE(InterOp::IsBuiltin(C.getTypeDeclType(CTSD).getAsOpaquePtr()));
}

TEST(ScopeReflectionTest, IsTemplate) {
  std::vector<Decl *> Decls;
  std::string code = R"(template<typename T>
                        class A{};

                        class C{
                          template<typename T>
                          int func(T t) {
                            return 0;
                          }
                        };

                        template<typename T>
                        T f(T t) {
                          return t;
                        }

                        void g() {} )";

  GetAllTopLevelDecls(code, Decls);
  EXPECT_TRUE(InterOp::IsTemplate(Decls[0]));
  EXPECT_FALSE(InterOp::IsTemplate(Decls[1]));
  EXPECT_TRUE(InterOp::IsTemplate(Decls[2]));
  EXPECT_FALSE(InterOp::IsTemplate(Decls[3]));
}

TEST(ScopeReflectionTest, IsAbstract) {
  std::vector<Decl *> Decls;
  std::string code = R"(
    class A {};

    class B {
      virtual int f() = 0;
    };
  )";

  GetAllTopLevelDecls(code, Decls);
  EXPECT_FALSE(InterOp::IsAbstract(Decls[0]));
  EXPECT_TRUE(InterOp::IsAbstract(Decls[1]));
}

TEST(ScopeReflectionTest, IsVariable) {
  std::vector<Decl *> Decls;
  std::string code = R"(
    int i;

    class C {
    public:
      int a;
      static int b;
    };
  )";

  GetAllTopLevelDecls(code, Decls);
  EXPECT_TRUE(InterOp::IsVariable(Decls[0]));
  EXPECT_FALSE(InterOp::IsVariable(Decls[1]));

  std::vector<Decl *> SubDecls;
  GetAllSubDecls(Decls[1], SubDecls);
  EXPECT_FALSE(InterOp::IsVariable(SubDecls[0]));
  EXPECT_FALSE(InterOp::IsVariable(SubDecls[1]));
  EXPECT_FALSE(InterOp::IsVariable(SubDecls[2]));
  EXPECT_TRUE(InterOp::IsVariable(SubDecls[3]));
}

TEST(ScopeReflectionTest, GetName) {
  std::vector<Decl*> Decls;
  std::string code = R"(namespace N {} class C{}; int I; struct S;
                        enum E : int; union U{}; class Size4{int i;};
                        struct Size16 {short a; double b;};
                       )";
  GetAllTopLevelDecls(code, Decls);
  EXPECT_EQ(InterOp::GetName(Decls[0]), "N");
  EXPECT_EQ(InterOp::GetName(Decls[1]), "C");
  EXPECT_EQ(InterOp::GetName(Decls[2]), "I");
  EXPECT_EQ(InterOp::GetName(Decls[3]), "S");
  EXPECT_EQ(InterOp::GetName(Decls[4]), "E");
  EXPECT_EQ(InterOp::GetName(Decls[5]), "U");
  EXPECT_EQ(InterOp::GetName(Decls[6]), "Size4");
  EXPECT_EQ(InterOp::GetName(Decls[7]), "Size16");
}

TEST(ScopeReflectionTest, GetCompleteName) {
  std::vector<Decl*> Decls;
  std::string code = R"(namespace N {
                        class C {
                          int i;
                          enum E { A, B };
                        };
                        }
                       )";
  GetAllTopLevelDecls(code, Decls);
  GetAllSubDecls(Decls[0], Decls);
  GetAllSubDecls(Decls[1], Decls);

  EXPECT_EQ(InterOp::GetCompleteName(0), "<unnamed>");
  EXPECT_EQ(InterOp::GetCompleteName(Decls[0]), "N");
  EXPECT_EQ(InterOp::GetCompleteName(Decls[1]), "N::C");
  EXPECT_EQ(InterOp::GetCompleteName(Decls[3]), "N::C::i");
  EXPECT_EQ(InterOp::GetCompleteName(Decls[4]), "N::C::E");
}

TEST(ScopeReflectionTest, GetUsingNamespaces) {
  std::vector<Decl *> Decls;
  std::string code = R"(
    namespace abc {

    class C {};

    }
    using namespace std;
    using namespace abc;

    using I = int;
  )";

  GetAllTopLevelDecls(code, Decls);
  std::vector<void *> usingNamespaces;
  usingNamespaces = InterOp::GetUsingNamespaces(
          Decls[0]->getASTContext().getTranslationUnitDecl());

  EXPECT_EQ(InterOp::GetName(usingNamespaces[0]), "runtime");
  EXPECT_EQ(InterOp::GetName(usingNamespaces[1]), "std");
  EXPECT_EQ(InterOp::GetName(usingNamespaces[2]), "abc");
}

TEST(ScopeReflectionTest, GetGlobalScope) {
  Interp = createInterpreter();
  Sema *S = &Interp->getCI()->getSema();

  EXPECT_EQ(InterOp::GetCompleteName(InterOp::GetGlobalScope(S)), "");
  EXPECT_EQ(InterOp::GetName(InterOp::GetGlobalScope(S)), "");
}

TEST(ScopeReflectionTest, GetScope) {
  std::vector<Decl*> Decls;
  std::string code = R"(namespace N {
                        class C {
                          int i;
                          enum E { A, B };
                        };
                        }
                       )";

  Interp = createInterpreter();
  Interp->declare(code);
  Sema *S = &Interp->getCI()->getSema();
  cling::InterOp::TCppScope_t tu = InterOp::GetScope(S, "", 0);
  cling::InterOp::TCppScope_t ns_N = InterOp::GetScope(S, "N", 0);
  cling::InterOp::TCppScope_t cl_C = InterOp::GetScope(S, "C", ns_N);

  EXPECT_EQ(InterOp::GetCompleteName(tu), "");
  EXPECT_EQ(InterOp::GetCompleteName(ns_N), "N");
  EXPECT_EQ(InterOp::GetCompleteName(cl_C), "N::C");
}

TEST(ScopeReflectionTest, GetScopefromCompleteName) {
  std::vector<Decl*> Decls;
  std::string code = R"(namespace N1 {
                        namespace N2 {
                          class C {
                            struct S {};
                          };
                        }
                        }
                       )";

  Interp = createInterpreter();
  Interp->declare(code);
  Sema *S = &Interp->getCI()->getSema();
  EXPECT_EQ(InterOp::GetCompleteName(InterOp::GetScopeFromCompleteName(S, "N1")), "N1");
  EXPECT_EQ(InterOp::GetCompleteName(InterOp::GetScopeFromCompleteName(S, "N1::N2")), "N1::N2");
  EXPECT_EQ(InterOp::GetCompleteName(InterOp::GetScopeFromCompleteName(S, "N1::N2::C")), "N1::N2::C");
  EXPECT_EQ(InterOp::GetCompleteName(InterOp::GetScopeFromCompleteName(S, "N1::N2::C::S")), "N1::N2::C::S");
}

TEST(ScopeReflectionTest, GetNamed) {
  std::vector<Decl*> Decls;
  std::string code = R"(namespace N1 {
                        namespace N2 {
                          class C {
                            int i;
                            enum E { A, B };
                            struct S {};
                          };
                        }
                        }
                       )";

  Interp = createInterpreter();
  Interp->declare(code);
  Sema *S = &Interp->getCI()->getSema();
  cling::InterOp::TCppScope_t ns_N1 = InterOp::GetNamed(S, "N1", 0);
  cling::InterOp::TCppScope_t ns_N2 = InterOp::GetNamed(S, "N2", ns_N1);
  cling::InterOp::TCppScope_t cl_C = InterOp::GetNamed(S, "C", ns_N2);
  cling::InterOp::TCppScope_t en_E = InterOp::GetNamed(S, "E", cl_C);
  EXPECT_EQ(InterOp::GetCompleteName(ns_N1), "N1");
  EXPECT_EQ(InterOp::GetCompleteName(ns_N2), "N1::N2");
  EXPECT_EQ(InterOp::GetCompleteName(cl_C), "N1::N2::C");
  EXPECT_EQ(InterOp::GetCompleteName(InterOp::GetNamed(S, "i", cl_C)), "N1::N2::C::i");
  EXPECT_EQ(InterOp::GetCompleteName(en_E), "N1::N2::C::E");
  EXPECT_EQ(InterOp::GetCompleteName(InterOp::GetNamed(S, "A", en_E)), "N1::N2::C::A");
  EXPECT_EQ(InterOp::GetCompleteName(InterOp::GetNamed(S, "B", en_E)), "N1::N2::C::B");
  EXPECT_EQ(InterOp::GetCompleteName(InterOp::GetNamed(S, "A", cl_C)), "N1::N2::C::A");
  EXPECT_EQ(InterOp::GetCompleteName(InterOp::GetNamed(S, "B", cl_C)), "N1::N2::C::B");
  EXPECT_EQ(InterOp::GetCompleteName(InterOp::GetNamed(S, "S", cl_C)), "N1::N2::C::S");
}

TEST(ScopeReflectionTest, GetParentScope) {
  std::vector<Decl*> Decls;
  std::string code = R"(namespace N1 {
                        namespace N2 {
                          class C {
                            int i;
                            enum E { A, B };
                            struct S {};
                          };
                        }
                        }
                       )";

  Interp = createInterpreter();
  Interp->declare(code);
  Sema *S = &Interp->getCI()->getSema();
  cling::InterOp::TCppScope_t ns_N1 = InterOp::GetNamed(S, "N1", 0);
  cling::InterOp::TCppScope_t ns_N2 = InterOp::GetNamed(S, "N2", ns_N1);
  cling::InterOp::TCppScope_t cl_C = InterOp::GetNamed(S, "C", ns_N2);
  cling::InterOp::TCppScope_t int_i = InterOp::GetNamed(S, "i", cl_C);
  cling::InterOp::TCppScope_t en_E = InterOp::GetNamed(S, "E", cl_C);
  cling::InterOp::TCppScope_t en_A = InterOp::GetNamed(S, "A", cl_C);
  cling::InterOp::TCppScope_t en_B = InterOp::GetNamed(S, "B", cl_C);

  EXPECT_EQ(InterOp::GetCompleteName(InterOp::GetParentScope(ns_N1)), "");
  EXPECT_EQ(InterOp::GetCompleteName(InterOp::GetParentScope(ns_N2)), "N1");
  EXPECT_EQ(InterOp::GetCompleteName(InterOp::GetParentScope(cl_C)), "N1::N2");
  EXPECT_EQ(InterOp::GetCompleteName(InterOp::GetParentScope(int_i)), "N1::N2::C");
  EXPECT_EQ(InterOp::GetCompleteName(InterOp::GetParentScope(en_E)), "N1::N2::C");
  EXPECT_EQ(InterOp::GetCompleteName(InterOp::GetParentScope(en_A)), "N1::N2::C::E");
  EXPECT_EQ(InterOp::GetCompleteName(InterOp::GetParentScope(en_B)), "N1::N2::C::E");
}

TEST(ScopeReflectionTest, GetScopeFromType) {
  std::vector<Decl *> Decls;
  std::string code = R"(
    namespace N {
    class C {};
    struct S {};
    }

    N::C c;

    N::S s;

    int i;
  )";

  GetAllTopLevelDecls(code, Decls);
  QualType QT1 = llvm::dyn_cast<VarDecl>(Decls[1])->getType();
  QualType QT2 = llvm::dyn_cast<VarDecl>(Decls[2])->getType();
  QualType QT3 = llvm::dyn_cast<VarDecl>(Decls[3])->getType();
  EXPECT_EQ(InterOp::GetCompleteName(InterOp::GetScopeFromType(QT1.getAsOpaquePtr())),
          "N::C");
  EXPECT_EQ(InterOp::GetCompleteName(InterOp::GetScopeFromType(QT2.getAsOpaquePtr())),
          "N::S");
  EXPECT_EQ(InterOp::GetScopeFromType(QT3.getAsOpaquePtr()),
          (cling::InterOp::TCppScope_t) 0);
}

TEST(ScopeReflectionTest, GetNumBases) {
  std::vector<Decl *> Decls;
  std::string code = R"(
    class A {};
    class B : virtual public A {};
    class C : virtual public A {};
    class D : public B, public C {};
    class E : public D {};
  )";

  GetAllTopLevelDecls(code, Decls);

  EXPECT_EQ(InterOp::GetNumBases(Decls[0]), 0);
  EXPECT_EQ(InterOp::GetNumBases(Decls[1]), 1);
  EXPECT_EQ(InterOp::GetNumBases(Decls[2]), 1);
  EXPECT_EQ(InterOp::GetNumBases(Decls[3]), 2);
  EXPECT_EQ(InterOp::GetNumBases(Decls[4]), 1);
}


TEST(ScopeReflectionTest, GetBaseClass) {
  std::vector<Decl *> Decls;
  std::string code = R"(
    class A {};
    class B : virtual public A {};
    class C : virtual public A {};
    class D : public B, public C {};
    class E : public D {};
  )";

  GetAllTopLevelDecls(code, Decls);

  auto get_base_class_name = [](Decl *D, int i) {
      return InterOp::GetCompleteName(InterOp::GetBaseClass(D, i));
  };

  EXPECT_EQ(get_base_class_name(Decls[1], 0), "A");
  EXPECT_EQ(get_base_class_name(Decls[2], 0), "A");
  EXPECT_EQ(get_base_class_name(Decls[3], 0), "B");
  EXPECT_EQ(get_base_class_name(Decls[3], 1), "C");
  EXPECT_EQ(get_base_class_name(Decls[4], 0), "D");
}

TEST(ScopeReflectionTest, IsSubclass) {
  std::vector<Decl *> Decls;
  std::string code = R"(
    class A {};
    class B : virtual public A {};
    class C : virtual public A {};
    class D : public B, public C {};
    class E : public D {};
  )";

  GetAllTopLevelDecls(code, Decls);

  auto check_subclass = [](Decl *derived_D, Decl *base_D) {
      return InterOp::IsSubclass(derived_D, base_D);
  };

  EXPECT_TRUE(check_subclass(Decls[0], Decls[0]));
  EXPECT_TRUE(check_subclass(Decls[1], Decls[0]));
  EXPECT_TRUE(check_subclass(Decls[2], Decls[0]));
  EXPECT_TRUE(check_subclass(Decls[3], Decls[0]));
  EXPECT_TRUE(check_subclass(Decls[4], Decls[0]));
  EXPECT_FALSE(check_subclass(Decls[0], Decls[1]));
  EXPECT_TRUE(check_subclass(Decls[1], Decls[1]));
  EXPECT_FALSE(check_subclass(Decls[2], Decls[1]));
  EXPECT_TRUE(check_subclass(Decls[3], Decls[1]));
  EXPECT_TRUE(check_subclass(Decls[4], Decls[1]));
  EXPECT_FALSE(check_subclass(Decls[0], Decls[2]));
  EXPECT_FALSE(check_subclass(Decls[1], Decls[2]));
  EXPECT_TRUE(check_subclass(Decls[2], Decls[2]));
  EXPECT_TRUE(check_subclass(Decls[3], Decls[2]));
  EXPECT_TRUE(check_subclass(Decls[4], Decls[2]));
  EXPECT_FALSE(check_subclass(Decls[0], Decls[3]));
  EXPECT_FALSE(check_subclass(Decls[1], Decls[3]));
  EXPECT_FALSE(check_subclass(Decls[2], Decls[3]));
  EXPECT_TRUE(check_subclass(Decls[3], Decls[3]));
  EXPECT_TRUE(check_subclass(Decls[4], Decls[3]));
  EXPECT_FALSE(check_subclass(Decls[0], Decls[4]));
  EXPECT_FALSE(check_subclass(Decls[1], Decls[4]));
  EXPECT_FALSE(check_subclass(Decls[2], Decls[4]));
  EXPECT_FALSE(check_subclass(Decls[3], Decls[4]));
  EXPECT_TRUE(check_subclass(Decls[4], Decls[4]));
}

TEST(ScopeReflectionTest, GetBaseClassOffset) {
  std::vector<Decl *> Decls;
  std::string code = R"(
    class A { int a; };
    class B { int b; };
    class C : public A, public B { int c; };
    class D : public A, public B, public C { int d; };
  )";

  GetAllTopLevelDecls(code, Decls);
  Sema *S = &Interp->getCI()->getSema();

  EXPECT_EQ(InterOp::GetBaseClassOffset(S, Decls[2], Decls[0]), 0);
  EXPECT_EQ(InterOp::GetBaseClassOffset(S, Decls[2], Decls[1]), 4);
  EXPECT_EQ(InterOp::GetBaseClassOffset(S, Decls[3], Decls[0]), 0);
  EXPECT_EQ(InterOp::GetBaseClassOffset(S, Decls[3], Decls[1]), 4);
  EXPECT_EQ(InterOp::GetBaseClassOffset(S, Decls[3], Decls[2]), 8);
}
