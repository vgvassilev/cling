//------------------------------------------------------------------------------
// CLING - the C++ LLVM-based InterpreterG :)
// author:  Axel Naumann <axel@cern.ch>
//
// This file is dual-licensed: you can choose to license it under the University
// of Illinois Open Source License or the GNU Lesser General Public License. See
// LICENSE.TXT for details.
//------------------------------------------------------------------------------

#include "IncrementalParser.h"

#include "AutoSynthesizer.h"
#include "AutoloadingTransform.h"
#include "BackendPass.h"
#include "CheckEmptyTransactionTransformer.h"
#include "DeclCollector.h"
#include "DeclExtractor.h"
#include "DynamicLookup.h"
#include "NullDerefProtectionTransformer.h"
#include "ValueExtractionSynthesizer.h"
#include "TransactionPool.h"
#include "TransactionUnloader.h"
#include "ValuePrinterSynthesizer.h"
#include "cling/Interpreter/CIFactory.h"
#include "cling/Interpreter/Interpreter.h"
#include "cling/Interpreter/InterpreterCallbacks.h"
#include "cling/Interpreter/Transaction.h"

#include "clang/AST/Attr.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclGroup.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Basic/FileManager.h"
#include "clang/CodeGen/ModuleBuilder.h"
#include "clang/Parse/Parser.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Serialization/ASTWriter.h"

#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/CrashRecoveryContext.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/raw_os_ostream.h"

#include <iostream>
#include <stdio.h>
#include <sstream>

using namespace clang;

namespace cling {
  IncrementalParser::IncrementalParser(Interpreter* interp,
                                       int argc, const char* const *argv,
                                       const char* llvmdir):
    m_Interpreter(interp), m_Consumer(0) {

    CompilerInstance* CI = CIFactory::createCI("", argc, argv, llvmdir);
    assert(CI && "CompilerInstance is (null)!");

    m_Consumer = dyn_cast<DeclCollector>(&CI->getSema().getASTConsumer());
    assert(m_Consumer && "Expected ChainedConsumer!");

    m_CI.reset(CI);

    if (CI->getFrontendOpts().ProgramAction != clang::frontend::ParseSyntaxOnly){
      m_CodeGen.reset(CreateLLVMCodeGen(CI->getDiagnostics(), "cling input",
                                        CI->getCodeGenOpts(),
                                        CI->getTargetOpts(),
                                        *m_Interpreter->getLLVMContext()
                                        ));
    }

    initializeVirtualFile();

    // Add transformers to the IncrementalParser, which owns them
    Sema* TheSema = &CI->getSema();
    // Register the AST Transformers
    m_ASTTransformers.push_back(new AutoSynthesizer(TheSema));
    m_ASTTransformers.push_back(new AutoloadingTransform(TheSema));
    m_ASTTransformers.push_back(new EvaluateTSynthesizer(TheSema));
    m_ASTTransformers.push_back(new ValuePrinterSynthesizer(TheSema, 0));
    m_ASTTransformers.push_back(new DeclExtractor(TheSema));
    m_ASTTransformers.push_back(new ValueExtractionSynthesizer(TheSema));
    m_ASTTransformers.push_back(new NullDerefProtectionTransformer(TheSema));
    m_ASTTransformers.push_back(new CheckEmptyTransactionTransformer(TheSema));


#ifdef _LIBCPP_VERSION
    // libc++ relies on force_inline attributes, else symbols will be missing.
    // But its passes (CallGraph and Inliner) - being module passes - have a
    // quadratically increasing runtime: for each transaction they need to
    // iterate over all previous transactions' functions.
    // Until this is solved (for instance by feeding only the new functions
    // to the CallGraph) we penalize only the use of libc++.
    if (m_CodeGen) {
      llvm::Module* TheModule = m_CodeGen->GetModule();
      // IR passes make sense if we do CodeGen.
      m_IRTransformers.push_back(new BackendPass(TheSema, TheModule,
                                                 CI->getDiagnostics(),
                                                 CI->getTargetOpts(),
                                                 CI->getLangOpts(),
                                                 CI->getCodeGenOpts()));
    }
#endif
  }

  void
  IncrementalParser::Initialize(llvm::SmallVectorImpl<Transaction*> &result) {
    m_TransactionPool.reset(new TransactionPool(getCI()->getSema()));
    if (hasCodeGenerator())
      getCodeGenerator()->Initialize(getCI()->getASTContext());

    CompilationOptions CO;
    CO.DeclarationExtraction = 0;
    CO.ValuePrinting = CompilationOptions::VPDisabled;
    CO.CodeGeneration = hasCodeGenerator();

    // pull in PCHs
    const std::string& PCHFileName
      = m_CI->getInvocation ().getPreprocessorOpts().ImplicitPCHInclude;
    if (!PCHFileName.empty()) {
      Transaction* CurT = beginTransaction(CO);
      m_CI->createPCHExternalASTSource(PCHFileName,
                                       true /*DisablePCHValidation*/,
                                       true /*AllowPCHWithCompilerErrors*/,
                                       0 /*DeserializationListener*/);
      if (Transaction* EndedT = endTransaction(CurT))
        result.push_back(EndedT);
    }

    Transaction* CurT = beginTransaction(CO);
    Sema* TheSema = &m_CI->getSema();
    m_Parser.reset(new Parser(m_CI->getPreprocessor(), *TheSema,
                              false /*skipFuncBodies*/));
    m_CI->getPreprocessor().EnterMainSourceFile();
    // Initialize the parser after we have entered the main source file.
    m_Parser->Initialize();
    // Perform initialization that occurs after the parser has been initialized
    // but before it parses anything. Initializes the consumers too.
    // No - already done by m_Parser->Initialize().
    // TheSema->Initialize();

    ExternalASTSource *External = TheSema->getASTContext().getExternalSource();
    if (External)
      External->StartTranslationUnit(m_Consumer);

    // DO NOT commit the transactions here: static initialization in these
    // transactions requires gCling through local_cxa_atexit(), but that has not
    // been defined yet!
    if (Transaction* EndedT = endTransaction(CurT))
      result.push_back(EndedT);

  }

  const Transaction* IncrementalParser::getCurrentTransaction() const {
    return m_Consumer->getTransaction();
  }

  SourceLocation IncrementalParser::getLastMemoryBufferEndLoc() const {
    const SourceManager& SM = getCI()->getSourceManager();
    SourceLocation Result = SM.getLocForStartOfFile(m_VirtualFileID);
    return Result.getLocWithOffset(m_MemoryBuffers.size() + 1);
  }

  IncrementalParser::~IncrementalParser() {
    if (hasCodeGenerator()) {
      getCodeGenerator()->ReleaseModule();
    }
    const Transaction* T = getFirstTransaction();
    const Transaction* nextT = 0;
    while (T) {
      assert((T->getState() == Transaction::kCommitted
              || T->getState() == Transaction::kRolledBackWithErrors 
              || T->getState() == Transaction::kNumStates // reset from the pool
              || T->getState() == Transaction::kRolledBack)
             && "Not committed?");
      nextT = T->getNext();
      delete T;
      T = nextT;
    }

    for (size_t i = 0; i < m_ASTTransformers.size(); ++i)
      delete m_ASTTransformers[i];

    for (size_t i = 0; i < m_IRTransformers.size(); ++i)
      delete m_IRTransformers[i];
  }

  Transaction* IncrementalParser::beginTransaction(const CompilationOptions& 
                                                   Opts) {
    Transaction* OldCurT = m_Consumer->getTransaction();
    Transaction* NewCurT = m_TransactionPool->takeTransaction();
    NewCurT->setCompilationOpts(Opts);
    // If we are in the middle of transaction and we see another begin 
    // transaction - it must be nested transaction.
    if (OldCurT && OldCurT != NewCurT 
        && (OldCurT->getState() == Transaction::kCollecting
            || OldCurT->getState() == Transaction::kCompleted)) {
      OldCurT->addNestedTransaction(NewCurT); // takes the ownership
    }

    m_Consumer->setTransaction(NewCurT);
    return NewCurT;
  }

  Transaction* IncrementalParser::endTransaction(Transaction* T) {
    assert(T && "Null transaction!?");
    assert(T->getState() == Transaction::kCollecting);

#ifndef NDEBUG
    if (T->hasNestedTransactions()) {
      for(Transaction::const_nested_iterator I = T->nested_begin(),
            E = T->nested_end(); I != E; ++I)
        assert((*I)->isCompleted() && "Nested transaction not completed!?");
    }
#endif

    T->setState(Transaction::kCompleted);
    // Empty transaction send it back to the pool.
    if (T->empty()) {
      assert((!m_Consumer->getTransaction() 
              || (m_Consumer->getTransaction() == T))
             && "Cannot release different T");
      // If a nested transaction the active one should be its parent 
      // from now on. FIXME: Merge conditional with commitTransaction
      if (T->isNestedTransaction())
        m_Consumer->setTransaction(T->getParent());
      else
        m_Consumer->setTransaction((Transaction*)0);

      m_TransactionPool->releaseTransaction(T);
      return 0;
    }
    
    transformTransactionAST(T);
    if (T->empty()) {
      m_TransactionPool->releaseTransaction(T);
      return 0;
    }

    const DiagnosticsEngine& Diags = getCI()->getSema().getDiagnostics();

    //TODO: Make the enum orable.
    if (Diags.getNumWarnings() > 0)
      T->setIssuedDiags(Transaction::kWarnings);

    if (Diags.hasErrorOccurred() || Diags.hasFatalErrorOccurred())
      T->setIssuedDiags(Transaction::kErrors);

    if (!T->isNestedTransaction() && T != getLastTransaction()) {
      if (getLastTransaction())
        m_Transactions.back()->setNext(T);
      m_Transactions.push_back(T);
    }
    return T;
  }

  void IncrementalParser::commitTransaction(Transaction* T) {
    //Transaction* CurT = m_Consumer->getTransaction();
    assert(T->isCompleted() && "Transaction not ended!?");
    assert(T->getState() != Transaction::kCommitted
           && "Committing an already committed transaction.");
    assert(!T->empty() && "Transactions must not be empty;");

    // If committing a nested transaction the active one should be its parent
    // from now on.
    if (T->isNestedTransaction())
      m_Consumer->setTransaction(T->getParent());

    // Check for errors...
    if (T->getIssuedDiags() == Transaction::kErrors) {
      rollbackTransaction(T);
      return;
    }

    if (T->hasNestedTransactions()) {
      for (Transaction::const_nested_iterator I = T->nested_begin(),
            E = T->nested_end(); I != E; ++I)
        if ((*I)->getState() != Transaction::kCommitted)
          commitTransaction(*I);
    }

    // If there was an error coming from the transformers.
    if (T->getIssuedDiags() == Transaction::kErrors) {
      rollbackTransaction(T);
      return;
    }

    // Here we expect a template instantiation. We need to open the transaction
    // that we are currently work with.
    {
      Transaction* prevConsumerT = m_Consumer->getTransaction();
      m_Consumer->setTransaction(T);
      Transaction* nestedT = beginTransaction(CompilationOptions());
      // Pull all template instantiations in that came from the consumers.
      getCI()->getSema().PerformPendingInstantiations();
      if (Transaction* T = endTransaction(nestedT))
        commitTransaction(T);
      m_Consumer->setTransaction(prevConsumerT);
    }
    m_Consumer->HandleTranslationUnit(getCI()->getASTContext());


    // The static initializers might run anything and can thus cause more
    // decls that need to end up in a transaction. But this one is done
    // with CodeGen...
    if (T->getCompilationOpts().CodeGeneration && hasCodeGenerator()) {
      Transaction* prevConsumerT = m_Consumer->getTransaction();
      m_Consumer->setTransaction(T);
      codeGenTransaction(T);
      transformTransactionIR(T);
      T->setState(Transaction::kCommitted);
      if (!T->getParent()) {
        if (m_Interpreter->runStaticInitializersOnce(*T)
            >= Interpreter::kExeFirstError) {
          // Roll back on error in a transformer
          assert(0 && "Error on inits.");
          //rollbackTransaction(nestedT);
          return;
        }
      }
      m_Consumer->setTransaction(prevConsumerT);
    }
    T->setState(Transaction::kCommitted);

    if (InterpreterCallbacks* callbacks = m_Interpreter->getCallbacks())
      callbacks->TransactionCommitted(*T);

  }

  void IncrementalParser::markWholeTransactionAsUsed(Transaction* T) const {
    ASTContext& C = m_CI->getASTContext();
    for (Transaction::const_iterator I = T->decls_begin(), E = T->decls_end();
         I != E; ++I) {
      // Copy DCI; it might get relocated below.
      Transaction::DelayCallInfo DCI = *I;
      // FIXME: implement for multiple decls in a DGR.
      assert(DCI.m_DGR.isSingleDecl());
      Decl* D = DCI.m_DGR.getSingleDecl();
      if (!D->hasAttr<clang::UsedAttr>())
        D->addAttr(::new (D->getASTContext())
                   clang::UsedAttr(D->getSourceRange(), D->getASTContext(),
                                   0/*AttributeSpellingListIndex*/));
    }
    for (Transaction::iterator I = T->deserialized_decls_begin(), 
           E = T->deserialized_decls_end(); I != E; ++I) {
      // FIXME: implement for multiple decls in a DGR.
      assert(I->m_DGR.isSingleDecl());
      Decl* D = I->m_DGR.getSingleDecl();
      if (!D->hasAttr<clang::UsedAttr>())
        D->addAttr(::new (C) clang::UsedAttr(D->getSourceRange(), C,
                                   0/*AttributeSpellingListIndex*/));
    }
  }

  void IncrementalParser::codeGenTransaction(Transaction* T) {
    // codegen the transaction
    assert(T->getCompilationOpts().CodeGeneration && "CodeGen turned off");
    assert(T->getState() == Transaction::kCompleted && "Must be completed");
    assert(hasCodeGenerator() && "No CodeGen");

    T->setModule(getCodeGenerator()->GetModule());

    // Could trigger derserialization of decls.
    Transaction* deserT = beginTransaction(CompilationOptions());
    for (Transaction::const_iterator TI = T->decls_begin(), TE = T->decls_end();
         TI != TE; ++TI) {
      // Copy DCI; it might get relocated below.
      Transaction::DelayCallInfo I = *TI;

      if (I.m_Call == Transaction::kCCIHandleTopLevelDecl)
        getCodeGenerator()->HandleTopLevelDecl(I.m_DGR);
      else if (I.m_Call == Transaction::kCCIHandleInterestingDecl) {
        // Usually through BackendConsumer which doesn't implement
        // HandleInterestingDecl() and thus calls
        // ASTConsumer::HandleInterestingDecl()
        getCodeGenerator()->HandleTopLevelDecl(I.m_DGR);
      } else if(I.m_Call == Transaction::kCCIHandleTagDeclDefinition) {
        TagDecl* TD = cast<TagDecl>(I.m_DGR.getSingleDecl());
        getCodeGenerator()->HandleTagDeclDefinition(TD);
      }
      else if (I.m_Call == Transaction::kCCIHandleVTable) {
        CXXRecordDecl* CXXRD = cast<CXXRecordDecl>(I.m_DGR.getSingleDecl());
        getCodeGenerator()->HandleVTable(CXXRD, /*isRequired*/true);
      }
      else if (I.m_Call
               == Transaction::kCCIHandleCXXImplicitFunctionInstantiation) {
        FunctionDecl* FD = cast<FunctionDecl>(I.m_DGR.getSingleDecl());
        getCodeGenerator()->HandleCXXImplicitFunctionInstantiation(FD);
      }
      else if (I.m_Call
               == Transaction::kCCIHandleCXXStaticMemberVarInstantiation) {
        VarDecl* VD = cast<VarDecl>(I.m_DGR.getSingleDecl());
        getCodeGenerator()->HandleCXXStaticMemberVarInstantiation(VD);
      }
      else if (I.m_Call == Transaction::kCCICompleteTentativeDefinition) {
        VarDecl* VD = cast<VarDecl>(I.m_DGR.getSingleDecl());
        getCodeGenerator()->CompleteTentativeDefinition(VD);
      }
      else if (I.m_Call == Transaction::kCCINone)
        ; // We use that internally as delimiter in the Transaction.
      else
        llvm_unreachable("We shouldn't have decl without call info.");
    }

    // Treat the deserialized decls differently.
    for (Transaction::iterator I = T->deserialized_decls_begin(), 
           E = T->deserialized_decls_end(); I != E; ++I) {

      for (DeclGroupRef::iterator DI = I->m_DGR.begin(), DE = I->m_DGR.end();
           DI != DE; ++DI) {
        DeclGroupRef SplitDGR(*DI);
        if (I->m_Call == Transaction::kCCIHandleTopLevelDecl) {
          // FIXME: The special namespace treatment (not sending itself to
          // CodeGen, but only its content - if the contained decl should be
          // emitted) works around issue with the static initialization when
          // having a PCH and loading a library. We don't want to generate
          // code for the static that will come through the library.
          //
          // This will be fixed with the clang::Modules. Make sure we remember.
          assert(!getCI()->getLangOpts().Modules && "Please revisit!");
          if (NamespaceDecl* ND = dyn_cast<NamespaceDecl>(*DI)) {
            for (NamespaceDecl::decl_iterator IN = ND->decls_begin(),
                   EN = ND->decls_end(); IN != EN; ++IN) {
              // Recurse over decls inside the namespace, like
              // CodeGenModule::EmitNamespace() does.
              if (!shouldIgnore(*IN))
                getCodeGenerator()->HandleTopLevelDecl(DeclGroupRef(*IN));
            }
          } else if (!shouldIgnore(*DI)) {
            getCodeGenerator()->HandleTopLevelDecl(SplitDGR);
          }
          continue;
        } // HandleTopLevel

        if (shouldIgnore(*DI))
          continue;

        if (I->m_Call == Transaction::kCCIHandleInterestingDecl) {
          // Usually through BackendConsumer which doesn't implement
          // HandleInterestingDecl() and thus calls
          // ASTConsumer::HandleInterestingDecl()
          getCodeGenerator()->HandleTopLevelDecl(SplitDGR);
        } else if(I->m_Call == Transaction::kCCIHandleTagDeclDefinition) {
          TagDecl* TD = cast<TagDecl>(*DI);
          getCodeGenerator()->HandleTagDeclDefinition(TD);
        }
        else if (I->m_Call == Transaction::kCCIHandleVTable) {
          CXXRecordDecl* CXXRD = cast<CXXRecordDecl>(*DI);
          getCodeGenerator()->HandleVTable(CXXRD, /*isRequired*/true);
        }
        else if (I->m_Call
                 == Transaction::kCCIHandleCXXImplicitFunctionInstantiation) {
          FunctionDecl* FD = cast<FunctionDecl>(*DI);
          getCodeGenerator()->HandleCXXImplicitFunctionInstantiation(FD);
        }
        else if (I->m_Call
                 == Transaction::kCCIHandleCXXStaticMemberVarInstantiation) {
          VarDecl* VD = cast<VarDecl>(*DI);
          getCodeGenerator()->HandleCXXStaticMemberVarInstantiation(VD);
        }
        else if (I->m_Call == Transaction::kCCICompleteTentativeDefinition) {
          VarDecl* VD = cast<VarDecl>(I->m_DGR.getSingleDecl());
          getCodeGenerator()->CompleteTentativeDefinition(VD);
        }
        else if (I->m_Call == Transaction::kCCINone)
          ; // We use that internally as delimiter in the Transaction.
        else
          llvm_unreachable("We shouldn't have decl without call info.");
      } // for decls in DGR
    } // for deserialized DGRs

    getCodeGenerator()->HandleTranslationUnit(getCI()->getASTContext());
    if ((deserT = endTransaction(deserT)))
      commitTransaction(deserT);
  }

  void IncrementalParser::transformTransactionAST(Transaction* T) {
    bool success = true;
    // We are sure it's safe to pipe it through the transformers
    // Consume late transformers init
    Transaction* initT = beginTransaction(CompilationOptions());

    for (size_t i = 0; success && i < m_ASTTransformers.size(); ++i)
      success = m_ASTTransformers[i]->TransformTransaction(*T);

    if (endTransaction(initT))
      commitTransaction(initT);

    if (!success)
      T->setIssuedDiags(Transaction::kErrors);
  }

  bool IncrementalParser::transformTransactionIR(Transaction* T) {
    // Transform IR
    bool success = true;
    for (size_t i = 0; success && i < m_IRTransformers.size(); ++i)
      success = m_IRTransformers[i]->TransformTransaction(*T);
    if (!success)
      rollbackTransaction(T);
    return success;
  }

  void IncrementalParser::rollbackTransaction(Transaction* T) {
    assert(T && "Must have value");
    assert(T == getLastTransaction() && "We always must revert the last T");
    assert((T->getState() != Transaction::kRolledBack ||
            T->getState() != Transaction::kRolledBackWithErrors) &&
           "Transaction already rolled back.");
    if (m_Interpreter->getOptions().ErrorOut)
      return;

    TransactionUnloader U(&getCI()->getSema(), m_CodeGen.get(),
                          m_Interpreter->getExecutionEngine());

    if (U.RevertTransaction(T))
      T->setState(Transaction::kRolledBack);
    else
      T->setState(Transaction::kRolledBackWithErrors);

    // Remove from the queue
    m_Transactions.pop_back();
    //m_TransactionPool->releaseTransaction(T);
  }

  std::vector<const Transaction*> IncrementalParser::getAllTransactions() {
    std::vector<const Transaction*> result(m_Transactions.size());
    const cling::Transaction* T = getFirstTransaction();
    while (T) {
      result.push_back(T);
      T = T->getNext();
    }
    return result;
  }

  // Each input line is contained in separate memory buffer. The SourceManager
  // assigns sort-of invalid FileID for each buffer, i.e there is no FileEntry
  // for the MemoryBuffer's FileID. That in turn is problem because invalid
  // SourceLocations are given to the diagnostics. Thus the diagnostics cannot
  // order the overloads, for example
  //
  // Our work-around is creating a virtual file, which doesn't exist on the disk
  // with enormous size (no allocation is done). That file has valid FileEntry
  // and so on... We use it for generating valid SourceLocations with valid
  // offsets so that it doesn't cause any troubles to the diagnostics.
  //
  // +---------------------+
  // | Main memory buffer  |
  // +---------------------+
  // |  Virtual file SLoc  |
  // |    address space    |<-----------------+
  // |         ...         |<------------+    |
  // |         ...         |             |    |
  // |         ...         |<----+       |    |
  // |         ...         |     |       |    |
  // +~~~~~~~~~~~~~~~~~~~~~+     |       |    |
  // |     input_line_1    | ....+.......+..--+
  // +---------------------+     |       |
  // |     input_line_2    | ....+.....--+
  // +---------------------+     |
  // |          ...        |     |
  // +---------------------+     |
  // |     input_line_N    | ..--+
  // +---------------------+
  //
  void IncrementalParser::initializeVirtualFile() {
    SourceManager& SM = getCI()->getSourceManager();
    m_VirtualFileID = SM.getMainFileID();
    assert(!m_VirtualFileID.isInvalid() && "No VirtualFileID created?");
  }

  Transaction* IncrementalParser::Compile(llvm::StringRef input,
                                          const CompilationOptions& Opts) {
    Transaction* CurT = beginTransaction(Opts);
    EParseResult ParseRes = ParseInternal(input);

    if (ParseRes == kSuccessWithWarnings)
      CurT->setIssuedDiags(Transaction::kWarnings);
    else if (ParseRes == kFailed)
      CurT->setIssuedDiags(Transaction::kErrors);

    if ((CurT = endTransaction(CurT))) {
      commitTransaction(CurT);
    }

    return CurT;
  }

  Transaction* IncrementalParser::Parse(llvm::StringRef input,
                                        const CompilationOptions& Opts) {
    Transaction* CurT = beginTransaction(Opts);
    ParseInternal(input);
    Transaction* EndedT = endTransaction(CurT);
    assert(EndedT == CurT && "Not ending the expected transaction.");
    return EndedT;
  }

  // Add the input to the memory buffer, parse it, and add it to the AST.
  IncrementalParser::EParseResult
  IncrementalParser::ParseInternal(llvm::StringRef input) {
    if (input.empty()) return IncrementalParser::kSuccess;

    Sema& S = getCI()->getSema();

    assert(!(S.getLangOpts().Modules
             && m_Consumer->getTransaction()->getCompilationOpts()
              .CodeGenerationForModule)
           && "CodeGenerationForModule to be removed once PCMs are available!");

    // Recover resources if we crash before exiting this method.
    llvm::CrashRecoveryContextCleanupRegistrar<Sema> CleanupSema(&S);

    Preprocessor& PP = m_CI->getPreprocessor();
    if (!PP.getCurrentLexer()) {
       PP.EnterSourceFile(m_CI->getSourceManager().getMainFileID(),
                          0, SourceLocation());
    }
    assert(PP.isIncrementalProcessingEnabled() && "Not in incremental mode!?");
    PP.enableIncrementalProcessing();

    std::ostringstream source_name;
    source_name << "input_line_" << (m_MemoryBuffers.size() + 1);

    // Create an uninitialized memory buffer, copy code in and append "\n"
    size_t InputSize = input.size(); // don't include trailing 0
    // MemBuffer size should *not* include terminating zero
    llvm::MemoryBuffer* MB
      = llvm::MemoryBuffer::getNewUninitMemBuffer(InputSize + 1,
                                                  source_name.str());
    char* MBStart = const_cast<char*>(MB->getBufferStart());
    memcpy(MBStart, input.data(), InputSize);
    memcpy(MBStart + InputSize, "\n", 2);

    m_MemoryBuffers.push_back(MB);
    SourceManager& SM = getCI()->getSourceManager();

    // Create SourceLocation, which will allow clang to order the overload
    // candidates for example
    SourceLocation NewLoc = getLastMemoryBufferEndLoc().getLocWithOffset(1);

    // Create FileID for the current buffer
    FileID FID = SM.createFileIDForMemBuffer(m_MemoryBuffers.back(),
                                             SrcMgr::C_User,
                                             /*LoadedID*/0,
                                             /*LoadedOffset*/0, NewLoc);

    PP.EnterSourceFile(FID, /*DirLookup*/0, NewLoc);
    m_Consumer->getTransaction()->setBufferFID(FID);

    Parser::DeclGroupPtrTy ADecl;

    while (!m_Parser->ParseTopLevelDecl(ADecl)) {
      // If we got a null return and something *was* parsed, ignore it.  This
      // is due to a top-level semicolon, an action override, or a parse error
      // skipping something.
      if (ADecl)
        m_Consumer->HandleTopLevelDecl(ADecl.get());
    };

    // Process any TopLevelDecls generated by #pragma weak.
    for (llvm::SmallVector<Decl*,2>::iterator I = S.WeakTopLevelDecls().begin(),
           E = S.WeakTopLevelDecls().end(); I != E; ++I) {
      m_Consumer->HandleTopLevelDecl(DeclGroupRef(*I));
    }

    DiagnosticsEngine& Diag = S.getDiagnostics();
    if (Diag.hasErrorOccurred())
      return IncrementalParser::kFailed;
    else if (Diag.getNumWarnings())
      return IncrementalParser::kSuccessWithWarnings;

    return IncrementalParser::kSuccess;
  }

  void IncrementalParser::printTransactionStructure() const {
    for(size_t i = 0, e = m_Transactions.size(); i < e; ++i) {
      m_Transactions[i]->printStructureBrief();
    }
  }

  bool IncrementalParser::shouldIgnore(const Decl* D) const {
    // This function is called for all "deserialized" decls, where the
    // "deserialized" decl either really comes from an AST file or from
    // a header that's loaded to import the AST for a library with a dictionary
    // (the non-PCM case).
    //
    // Functions that are inlined must be sent to CodeGen - they will not have a
    // symbol in the library.
    if (const FunctionDecl* FD = dyn_cast<FunctionDecl>(D)) {
      if (D->isFromASTFile()) {
        return !FD->hasBody();
      } else {
        // If the decl must be emitted then it will be in the library.
        // If not, we must expose it to CodeGen now because it might
        // not be in the library. Does this correspond to a weak symbol
        // by definition?
        return !(FD->isInlined() || FD->isTemplateInstantiation());
      }
    }

    // Don't codegen statics coming in from a module; they are already part of
    // the library.
    // We do need to expose static variables from template instantiations.
    if (const VarDecl* VD = dyn_cast<VarDecl>(D))
      if (VD->hasGlobalStorage() && !VD->getType().isConstQualified()
          && VD->getTemplateSpecializationKind() == TSK_Undeclared)
        return true;
    return false;
  }

} // namespace cling
