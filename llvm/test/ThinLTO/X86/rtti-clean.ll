; RUN: opt -thinlto-bc -thinlto-split-lto-unit -o %t.o %s
; RUN: opt -thinlto-bc -thinlto-split-lto-unit -o %t1.o %p/Inputs/rtti-clean.ll
;
; RUN: llvm-lto2 run %t.o %t1.o -o %t2 -save-temps \
; RUN:           -r=%t.o,_ZTIvtbase \
; RUN:           -r=%t.o,_ZTIvt,p \
; RUN:           -r=%t.o,_ZTVvt,p \
; RUN:           -r=%t.o,_ZTVvt1,p \
; RUN:           -r=%t.o,_ZTIvt1 \
; RUN:           -r=%t.o,export,px \
; RUN:           -r=%t1.o,_ZTIvtbase,p \
; RUN:           -r=%t1.o,_ZTIvt \
; RUN:           -r=%t1.o,_ZTIvt1,p \
; RUN:           -r=%t1.o,_ZTVvt \
; RUN:           -r=%t1.o,_ZTVvt1 \
; RUN:           -whole-program-visibility
; RUN: llvm-dis %t2.1.4.opt.bc -o - | FileCheck %s
; RUN: llvm-dis %t2.0.4.opt.bc -o - | FileCheck %s --check-prefix=SPLIT
;
; CHECK: @_ZTIvtbase = weak_odr local_unnamed_addr constant { ptr } { ptr @_ZTIvt }
;
; SPLIT: @_ZTIvt = weak_odr local_unnamed_addr constant { ptr } { ptr @_ZTIvtbase }
; SPLIT-NOT: @_ZTIvt1 = weak_odr local_unnamed_addr constant { ptr } { ptr @_ZTIvt }

target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"
@_ZTIvtbase = external constant ptr
@_ZTIvt1 = external constant ptr
@_ZTIvt = weak_odr constant { ptr } { ptr @_ZTIvtbase }

%vtTy = type { [3 x ptr] }

@_ZTVvt = weak_odr constant %vtTy { [3 x ptr] [ptr null, ptr @_ZTIvt, ptr @vf] }, !type !0, !vcall_visitbiliy !1
@_ZTVvt1 = weak_odr constant %vtTy { [3 x ptr] [ptr null, ptr @_ZTIvt1, ptr @vf] }, !type !2, !vcall_visitbiliy !1

@export = weak_odr constant { ptr, ptr } { ptr @_ZTVvt, ptr @_ZTVvt1 }

define internal void @vf() {
  ret void
}

!0 = !{i32 16, !"_ZTSvt"}
!1 = !{i64 1}
!2 = !{i32 16, !"_ZTSvt1"}
