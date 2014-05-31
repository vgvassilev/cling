//------------------------------------------------------------------------------
// CLING - the C++ LLVM-based InterpreterG :)
// author:  Vassil Vassilev <vasil.georgiev.vasilev@cern.ch>
//
// This file is dual-licensed: you can choose to license it under the University
// of Illinois Open Source License or the GNU Lesser General Public License. See
// LICENSE.TXT for details.
//------------------------------------------------------------------------------

#include "ValueExtractionSynthesizer.h"

#include "cling/Interpreter/Interpreter.h"
#include "cling/Interpreter/Transaction.h"
#include "cling/Interpreter/Value.h"

#include "cling/Utils/AST.h"

#include "clang/AST/ASTContext.h"
#include "clang/AST/DeclGroup.h"
#include "clang/AST/StmtVisitor.h"
#include "clang/Sema/Lookup.h"
#include "clang/Sema/Sema.h"

using namespace clang;

namespace cling {
  ValueExtractionSynthesizer::ValueExtractionSynthesizer(clang::Sema* S)
    : TransactionTransformer(S), m_Context(&S->getASTContext()), m_gClingVD(0),
      m_UnresolvedNoAlloc(0), m_UnresolvedWithAlloc(0),
      m_UnresolvedCopyArray(0) { }

  // pin the vtable here.
  ValueExtractionSynthesizer::~ValueExtractionSynthesizer() { }

  namespace {
    class ReturnStmtCollector : public StmtVisitor<ReturnStmtCollector> {
    private:
      llvm::SmallVectorImpl<Stmt**>& m_Stmts;
    public:
      ReturnStmtCollector(llvm::SmallVectorImpl<Stmt**>& S)
        : m_Stmts(S) {}

      void VisitStmt(Stmt* S) {
        for(Stmt::child_iterator I = S->child_begin(), E = S->child_end();
            I != E; ++I) {
          if (!*I)
            continue;
          Visit(*I);
          if (isa<ReturnStmt>(*I))
            m_Stmts.push_back(&*I);
        }
      }
    };
  }

  void ValueExtractionSynthesizer::Transform() {
    const CompilationOptions& CO = getTransaction()->getCompilationOpts();
    // If we do not evaluate the result, or printing out the result return.
    if (!(CO.ResultEvaluation || CO.ValuePrinting))
      return;

    FunctionDecl* FD = getTransaction()->getWrapperFD();

    int foundAtPos = -1;
    Expr* lastExpr = utils::Analyze::GetOrCreateLastExpr(FD, &foundAtPos,
                                                         /*omitDS*/false,
                                                         m_Sema);
    if (foundAtPos < 0)
      return;

    typedef llvm::SmallVector<Stmt**, 4> StmtIters;
    StmtIters returnStmts;
    ReturnStmtCollector collector(returnStmts);
    CompoundStmt* CS = cast<CompoundStmt>(FD->getBody());
    collector.VisitStmt(CS);

    if (isa<Expr>(*(CS->body_begin() + foundAtPos)))
      returnStmts.push_back(CS->body_begin() + foundAtPos);

    // We want to support cases such as:
    // gCling->evaluate("if() return 'A' else return 12", V), that puts in V,
    // either A or 12.
    // In this case the void wrapper is compiled with the stmts returning
    // values. Sema would cast them to void, but the code will still be
    // executed. For example:
    // int g(); void f () { return g(); } will still call g().
    //
    for (StmtIters::iterator I = returnStmts.begin(), E = returnStmts.end();
         I != E; ++I) {
      ReturnStmt* RS = dyn_cast<ReturnStmt>(**I);
      if (RS) {
        // When we are handling a return stmt, the last expression must be the
        // return stmt value. Ignore the calculation of the lastStmt because it
        // might be wrong, in cases where the return is not in the end of the 
        // function.
        lastExpr = RS->getRetValue();
        if (lastExpr) {
          assert (lastExpr->getType()->isVoidType() && "Must be void type.");
          // Any return statement will have been "healed" by Sema
          // to correspond to the original void return type of the
          // wrapper, using a ImplicitCastExpr 'void' <ToVoid>.
          // Remove that.
          if (ImplicitCastExpr* VoidCast
              = dyn_cast<ImplicitCastExpr>(lastExpr)) {
            lastExpr = VoidCast->getSubExpr();
          }
        }
        // if no value assume void
        else {
          // We can't PushDeclContext, because we don't have scope.
          Sema::ContextRAII pushedDC(*m_Sema, FD);
          RS->setRetValue(SynthesizeSVRInit(0));
        }

      }
      else
        lastExpr = cast<Expr>(**I);

      if (lastExpr) {
        QualType lastExprTy = lastExpr->getType();
        // May happen on auto types which resolve to dependent.
        if (lastExprTy->isDependentType())
          continue;
        // Set up lastExpr properly.
        // Change the void function's return type
        // We can't PushDeclContext, because we don't have scope.
        Sema::ContextRAII pushedDC(*m_Sema, FD);

        if (lastExprTy->isFunctionType()) {
          // A return type of function needs to be converted to
          // pointer to function.
          lastExprTy = m_Context->getPointerType(lastExprTy);
          lastExpr = m_Sema->ImpCastExprToType(lastExpr, lastExprTy,
                                               CK_FunctionToPointerDecay,
                                               VK_RValue).take();
        }

        //
        // Here we don't want to depend on the JIT runFunction, because of its
        // limitations, when it comes to return value handling. There it is
        // not clear who provides the storage and who cleans it up in a
        // platform independent way.
        //
        // Depending on the type we need to synthesize a call to cling:
        // 0) void : set the value's type to void;
        // 1) enum, integral, float, double, referece, pointer types :
        //      call to cling::internal::setValueNoAlloc(...);
        // 2) object type (alloc on the stack) :
        //      cling::internal::setValueWithAlloc
        //   2.1) constant arrays:
        //          call to cling::runtime::internal::copyArray(...)
        //
        // We need to synthesize later:
        // Wrapper has signature: void w(cling::Value SVR)
        // case 1):
        //   setValueNoAlloc(gCling, &SVR, lastExprTy, lastExpr())
        // case 2):
        //   new (setValueWithAlloc(gCling, &SVR, lastExprTy)) (lastExpr)
        // case 2.1):
        //   copyArray(src, placement, size)

        Expr* SVRInit = SynthesizeSVRInit(lastExpr);
        // if we had return stmt update to execute the SVR init, even if the
        // wrapper returns void.
        if (RS) {
          if (ImplicitCastExpr* VoidCast
              = dyn_cast<ImplicitCastExpr>(RS->getRetValue()))
            VoidCast->setSubExpr(SVRInit);
        }
        else
          **I = SVRInit;
      }
    }
  }

// Helper function for the SynthesizeSVRInit
namespace {
  static bool availableCopyConstructor(QualType QT, clang::Sema* S) {
    // Check the the existance of the copy constructor the tha placement new will use.
    if (CXXRecordDecl* RD = QT->getAsCXXRecordDecl()) {
      // If it has a trivial copy constructor it is accessible and it is callable.
      if(RD->hasTrivialCopyConstructor()) return true;
      // Lookup the copy canstructor and check its accessiblity.
      if (CXXConstructorDecl* CD = S->LookupCopyingConstructor(RD, QT.getCVRQualifiers())) {
        if (CD ->getAccess() == clang::AccessSpecifier::AS_public) return true;
      }
      return false;
    }
    return true;
  }
}

  Expr* ValueExtractionSynthesizer::SynthesizeSVRInit(Expr* E) {
    if (!m_gClingVD)
      FindAndCacheRuntimeDecls();

    // Build a reference to gCling
    ExprResult gClingDRE
      = m_Sema->BuildDeclRefExpr(m_gClingVD, m_Context->VoidPtrTy,
                                 VK_RValue, SourceLocation());
    // We have the wrapper as Sema's CurContext
    FunctionDecl* FD = cast<FunctionDecl>(m_Sema->CurContext);

    ExprWithCleanups* Cleanups = 0;
    // In case of ExprWithCleanups we need to extend its 'scope' to the call.
    if (E && isa<ExprWithCleanups>(E)) {
      Cleanups = cast<ExprWithCleanups>(E);
      E = Cleanups->getSubExpr();
    }

    // Build a reference to Value* in the wrapper, should be
    // the only argument of the wrapper.
    SourceLocation locStart = (E) ? E->getLocStart() : FD->getLocStart();
    SourceLocation locEnd = (E) ? E->getLocEnd() : FD->getLocEnd();
    ExprResult wrapperSVRDRE
      = m_Sema->BuildDeclRefExpr(FD->getParamDecl(0), m_Context->VoidPtrTy,
                                 VK_RValue, locStart);
    QualType ETy = (E) ? E->getType() : m_Context->VoidTy;
    QualType desugaredTy = ETy.getDesugaredType(*m_Context);

    // The expr result is transported as reference, pointer, array, float etc
    // based on the desugared type. We should still expose the typedef'ed
    // (sugared) type to the cling::Value.
    if (desugaredTy->isRecordType() && E->getValueKind() == VK_LValue) {
      // returning a lvalue (not a temporary): the value should contain
      // a reference to the lvalue instead of copying it.
      desugaredTy = m_Context->getLValueReferenceType(desugaredTy);
      ETy = m_Context->getLValueReferenceType(ETy);
    }
    Expr* ETyVP
      = utils::Synthesize::CStyleCastPtrExpr(m_Sema, m_Context->VoidPtrTy,
                                             (uint64_t)ETy.getAsOpaquePtr());
    Expr* ETransaction
      = utils::Synthesize::CStyleCastPtrExpr(m_Sema, m_Context->VoidPtrTy,
                                             (uint64_t)getTransaction());

    llvm::SmallVector<Expr*, 6> CallArgs;
    CallArgs.push_back(gClingDRE.take());
    CallArgs.push_back(wrapperSVRDRE.take());
    CallArgs.push_back(ETyVP);
    CallArgs.push_back(ETransaction);

    ExprResult Call;
    SourceLocation noLoc;
    if (desugaredTy->isVoidType()) {
      // In cases where the cling::Value gets reused we need to reset the
      // previous settings to void.
      // We need to synthesize setValueNoAlloc(...), E, because we still need
      // to run E.

      // FIXME: Suboptimal: this discards the already created AST nodes.
      QualType vpQT = m_Context->VoidPtrTy;
      QualType vQT = m_Context->VoidTy;
      Expr* vpQTVP
        = utils::Synthesize::CStyleCastPtrExpr(m_Sema, vpQT,
                                               (uint64_t)vQT.getAsOpaquePtr());
      CallArgs[2] = vpQTVP;


      Call = m_Sema->ActOnCallExpr(/*Scope*/0, m_UnresolvedNoAlloc,
                                   locStart, CallArgs, locEnd);

      if (E)
        Call = m_Sema->CreateBuiltinBinOp(locStart, BO_Comma, Call.take(), E);

    }
    else if (desugaredTy->isRecordType() || desugaredTy->isConstantArrayType()){
      // 2) object types :
      // check existance of copy constructor before call
      if (!availableCopyConstructor(desugaredTy, m_Sema))
        return E;
      // call new (setValueWithAlloc(gCling, &SVR, ETy)) (E)
      Call = m_Sema->ActOnCallExpr(/*Scope*/0, m_UnresolvedWithAlloc,
                                   locStart, CallArgs, locEnd);
      Expr* placement = Call.take();
      if (const ConstantArrayType* constArray
          = dyn_cast<ConstantArrayType>(desugaredTy.getTypePtr())) {
        CallArgs.clear();
        CallArgs.push_back(E);
        CallArgs.push_back(placement);
        uint64_t arrSize
          = m_Context->getConstantArrayElementCount(constArray);
        Expr* arrSizeExpr
          = utils::Synthesize::IntegerLiteralExpr(*m_Context, arrSize);

        CallArgs.push_back(arrSizeExpr);
        // 2.1) arrays:
        // call copyArray(T* src, void* placement, int size)
        Call = m_Sema->ActOnCallExpr(/*Scope*/0, m_UnresolvedCopyArray,
                                     locStart, CallArgs, locEnd);

      }
      else {
        TypeSourceInfo* ETSI
          = m_Context->getTrivialTypeSourceInfo(ETy, noLoc);

        Call = m_Sema->BuildCXXNew(E->getSourceRange(),
                                   /*useGlobal ::*/true,
                                   /*placementLParen*/ noLoc,
                                   MultiExprArg(placement),
                                   /*placementRParen*/ noLoc,
                                   /*TypeIdParens*/ SourceRange(),
                                   /*allocType*/ ETSI->getType(),
                                   /*allocTypeInfo*/ETSI,
                                   /*arraySize*/0,
                                   /*directInitRange*/E->getSourceRange(),
                                   /*initializer*/E,
                                   /*mayContainAuto*/false
                                   );
      }
    }
    else if (desugaredTy->isIntegralOrEnumerationType()
             || desugaredTy->isReferenceType()
             || desugaredTy->isPointerType()
             || desugaredTy->isFloatingType()) {
      if (desugaredTy->isIntegralOrEnumerationType()) {
        // 1)  enum, integral, float, double, referece, pointer types :
        //      call to cling::internal::setValueNoAlloc(...);

        // If the type is enum or integral we need to force-cast it into
        // uint64 in order to pick up the correct overload.
        if (desugaredTy->isIntegralOrEnumerationType()) {
          QualType UInt64Ty = m_Context->UnsignedLongLongTy;
          TypeSourceInfo* TSI
            = m_Context->getTrivialTypeSourceInfo(UInt64Ty, noLoc);
          Expr* castedE
            = m_Sema->BuildCStyleCastExpr(noLoc, TSI, noLoc, E).take();
          CallArgs.push_back(castedE);
        }
      }
      else if (desugaredTy->isReferenceType()) {
        // we need to get the address of the references
        Expr* AddrOfE = m_Sema->BuildUnaryOp(/*Scope*/0, noLoc, UO_AddrOf,
                                             E).take();
        CallArgs.push_back(AddrOfE);
      }
      else if (desugaredTy->isPointerType()) {
        // function pointers need explicit void* cast.
        QualType VoidPtrTy = m_Context->VoidPtrTy;
        TypeSourceInfo* TSI
          = m_Context->getTrivialTypeSourceInfo(VoidPtrTy, noLoc);
        Expr* castedE
          = m_Sema->BuildCStyleCastExpr(noLoc, TSI, noLoc, E).take();
        CallArgs.push_back(castedE);
      }
      else if (desugaredTy->isFloatingType()) {
        // floats and double will fall naturally in the correct
        // case, because of the overload resolution.
        CallArgs.push_back(E);
      }
      Call = m_Sema->ActOnCallExpr(/*Scope*/0, m_UnresolvedNoAlloc,
                                   locStart, CallArgs, locEnd);
    }
    else
      assert(0 && "Unhandled code path?");

    assert(!Call.isInvalid() && "Invalid Call");

    // Extend the scope of the temporary cleaner if applicable.
    if (Cleanups) {
      Cleanups->setSubExpr(Call.take());
      Cleanups->setValueKind(Call.take()->getValueKind());
      Cleanups->setType(Call.take()->getType());
      return Cleanups;
    }
    return Call.take();
  }

  void ValueExtractionSynthesizer::FindAndCacheRuntimeDecls() {
    assert(!m_gClingVD && "Called multiple times!?");
    DeclContext* NSD = m_Context->getTranslationUnitDecl();
    if (m_Sema->getLangOpts().CPlusPlus) {
      NSD = utils::Lookup::Namespace(m_Sema, "cling");
      NSD = utils::Lookup::Namespace(m_Sema, "runtime", NSD);
      m_gClingVD = cast<VarDecl>(utils::Lookup::Named(m_Sema, "gCling", NSD));
      NSD = utils::Lookup::Namespace(m_Sema, "internal",NSD);
    }
    LookupResult R(*m_Sema, &m_Context->Idents.get("setValueNoAlloc"),
                   SourceLocation(), Sema::LookupOrdinaryName,
                   Sema::ForRedeclaration);

    m_Sema->LookupQualifiedName(R, NSD);
    assert(!R.empty()
           && "Cannot find cling::runtime::internal::setValueNoAlloc");

    CXXScopeSpec CSS;
    m_UnresolvedNoAlloc
      = m_Sema->BuildDeclarationNameExpr(CSS, R, /*ADL*/ false).take();

    R.clear();
    R.setLookupName(&m_Context->Idents.get("setValueWithAlloc"));
    m_Sema->LookupQualifiedName(R, NSD);
    assert(!R.empty()
           && "Cannot find cling::runtime::internal::setValueWithAlloc");
    m_UnresolvedWithAlloc
      = m_Sema->BuildDeclarationNameExpr(CSS, R, /*ADL*/ false).take();

    R.clear();
    R.setLookupName(&m_Context->Idents.get("copyArray"));
    m_Sema->LookupQualifiedName(R, NSD);
    assert(!R.empty() && "Cannot find cling::runtime::internal::copyArray");
    m_UnresolvedCopyArray
      = m_Sema->BuildDeclarationNameExpr(CSS, R, /*ADL*/ false).take();
  }
} // end namespace cling


// Provide implementation of the functions that ValueExtractionSynthesizer calls
namespace {

  static void dumpIfNoStorage(void* vpV, void* vpT) {
    const cling::Value& V = *(cling::Value*)vpV;
    //const cling::Transaction& T = *(cling::Transaction*)vpT);
    // If the value copies over the temporary we must delay the printing until
    // the temporary gets copied over. For the rest of the temporaries we *must*
    // dump here because their lifetime will be gone otherwise. Eg.
    //
    // std::string f(); f().c_str() // have to dump during the same stmt.
    //
    assert(!V.needsManagedAllocation() && "Must contain non managed temporary");
    cling::Transaction* T = ((cling::Transaction*)vpT);
    const cling::CompilationOptions& CO = T->getCompilationOpts();
    if (CO.ValuePrinting == cling::CompilationOptions::VPEnabled)
      V.dump();
    assert(CO.ValuePrinting != cling::CompilationOptions::VPAuto
           && "VPAuto must have been expanded earlier.");
  }

  ///\brief Allocate the Value and return the Value
  /// for an expression evaluated at the prompt.
  ///
  ///\param [in] interp - The cling::Interpreter to allocate the SToredValueRef.
  ///\param [in] vpQT - The opaque ptr for the clang::QualType of value stored.
  ///\param [out] vpStoredValRef - The Value that is allocated.
  static cling::Value&
  allocateStoredRefValueAndGetGV(void* vpI, void* vpSVR, void* vpQT) {
    cling::Interpreter* i = (cling::Interpreter*)vpI;
    clang::QualType QT = clang::QualType::getFromOpaquePtr(vpQT);
    cling::Value& SVR = *(cling::Value*)vpSVR;
    // Here the copy keeps the refcounted value alive.
    SVR = cling::Value(QT, *i);
    return SVR;
  }
}
namespace cling {
namespace runtime {
  namespace internal {
    void setValueNoAlloc(void* vpI, void* vpSVR, void* vpQT, void* vpT) {
      // In cases of void we 'just' need to change the type of the value.
      allocateStoredRefValueAndGetGV(vpI, vpSVR, vpQT);
    }
    void setValueNoAlloc(void* vpI, void* vpSVR, void* vpQT, void* vpT,
                         float value) {
      allocateStoredRefValueAndGetGV(vpI, vpSVR, vpQT).getAs<float>() = value;
      dumpIfNoStorage(vpSVR, vpT);
    }
    void setValueNoAlloc(void* vpI, void* vpSVR, void* vpQT, void* vpT,
                         double value) {
      allocateStoredRefValueAndGetGV(vpI, vpSVR, vpQT).getAs<double>() = value;
      dumpIfNoStorage(vpSVR, vpT);
    }
    void setValueNoAlloc(void* vpI, void* vpSVR, void* vpQT, void* vpT,
                         long double value) {
      allocateStoredRefValueAndGetGV(vpI, vpSVR, vpQT).getAs<long double>()
        = value;
      dumpIfNoStorage(vpSVR, vpT);
    }
    void setValueNoAlloc(void* vpI, void* vpSVR, void* vpQT, void* vpT,
                         unsigned long long value) {
      allocateStoredRefValueAndGetGV(vpI, vpSVR, vpQT)
        .getAs<unsigned long long>() = value;
      dumpIfNoStorage(vpSVR, vpT);
    }
    void setValueNoAlloc(void* vpI, void* vpSVR, void* vpQT, void* vpT,
                         const void* value){
      allocateStoredRefValueAndGetGV(vpI, vpSVR, vpQT).getAs<void*>()
        = const_cast<void*>(value);
      dumpIfNoStorage(vpSVR, vpT);
    }
    void* setValueWithAlloc(void* vpI, void* vpSVR, void* vpQT, void* vpT) {
      return allocateStoredRefValueAndGetGV(vpI, vpSVR, vpQT).getAs<void*>();
    }
  } // end namespace internal
} // end namespace runtime
} // end namespace cling
