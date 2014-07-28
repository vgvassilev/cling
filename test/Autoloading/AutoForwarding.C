//------------------------------------------------------------------------------
// CLING - the C++ LLVM-based InterpreterG :)
//
// This file is dual-licensed: you can choose to license it under the University
// of Illinois Open Source License or the GNU Lesser General Public License. See
// LICENSE.TXT for details.
//------------------------------------------------------------------------------

// RUN: cat %s | %cling -I%S -Xclang -verify
// Test FwdPrinterTest

#include "cling/Interpreter/Interpreter.h"
gCling->GenerateAutoloadingMap("Def2.h","fwd_def2.h");
gCling->EnableAutoloading();
#include "fwd_def2.h"
#include "Def2.h"

gCling->GenerateAutoloadingMap("Enum.h","fwd_enums.h");
#include "fwd_enums.h"
#include "Enum.h"

//expected-no-diagnostics
.q
