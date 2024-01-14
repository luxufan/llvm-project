; RUN: opt -module-summary %s -o %t.bc
; RUN: opt -passes=rtti-clean -rtti-clean-read-export-summary=%t.bc -rtti-clean-write-summary=%t1.bc %s
; RUN: llvm-dis %t1.bc -o - | FileCheck %s
;
; CHECK: ^2 = gv: (guid: 16572084593444778981, summaries: (variable: (module: ^0, flags: (linkage: internal, visibility: default, notEligibleToImport: 0, live: 0, dsoLocal: 1, canAutoHide: 0), varFlags: (readonly: 1, writeonly: 0, constant: 1))))
;
target datalayout = "e-p:64:64"
target triple = "aarch64-unknown-linux-gnu"

%vtTy = type { [3 x ptr] }

declare i32 @vf()
@_ZTIvt = internal constant %vtTy zeroinitializer
@_ZTVvt = internal constant %vtTy { [3 x ptr] [ptr null, ptr @_ZTIvt, ptr @vf] }, !type !0

!0 = !{i32 16, !"_ZTSvt"}
