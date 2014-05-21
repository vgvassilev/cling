//------------------------------------------------------------------------------
// CLING - the C++ LLVM-based InterpreterG :)
//
// This file is dual-licensed: you can choose to license it under the University
// of Illinois Open Source License or the GNU Lesser General Public License. See
// LICENSE.TXT for details.
//------------------------------------------------------------------------------

// RUN: cat %s | %cling 2>&1 
// Test NoDups

.rawInput 0
#include "cling/TagsExtension/TagManager.h"
#include "cling/TagsExtension/Callback.h"
cling::TagManager t;
gCling->setCallbacks(new cling::AutoloadCallback(gCling,&t));

.T %S
.T %S
.T %S

t.size()
//CHECK: "(std::size_t) 1"

.q






