; RUN: opt < %s -passes=rtti-clean -S | FileCheck %s
; XFAIL: *
; TODO: support structure vtable instead of array to make the pass more
; robost
target datalayout = "e-p:64:64"
target triple = "aarch64-unknown-linux-gnu"

%vtTy = type { {ptr, ptr, ptr} }

; Check _ZTVvt is optimized which the first two vtable slots are eliminated.
@_ZTVvt = internal constant %vtTy { {ptr, ptr, ptr} {ptr null, ptr null, ptr @vf} }, !type !0

define i32 @vf() {
; CHECK-LABEL: define i32 @vf() {
; CHECK-NEXT:    ret i32 0
;
  ret i32 0
}

!0 = !{i32 16, !"_ZTSvt"}
