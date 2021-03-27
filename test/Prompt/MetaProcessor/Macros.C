//------------------------------------------------------------------------------
// CLING - the C++ LLVM-based InterpreterG :)
//
// This file is dual-licensed: you can choose to license it under the University
// of Illinois Open Source License or the GNU Lesser General Public License. See
// LICENSE.TXT for details.
//------------------------------------------------------------------------------

// RUN: cat %s | %cling | %filecheck %s
//XFAIL: *
// This test should test the unnamed macro support once it is moved in cling.
.x Commands.macro
// CHECK: I am a function called f.
// CHECK-NOT: 0
