; RUN: opt < %s -passes=rtti-clean -S | FileCheck %s
; XFAIL: *
; TODO: more rebost to support getelementptr with index more than 2
target datalayout = "e-p:64:64"
target triple = "aarch64-unknown-linux-gnu"

@_ZTVvt = internal constant %vtTy { [3 x ptr] [ptr null, ptr null, ptr @vf] }, !type !0

@temp1 = internal constant ptr getelementptr inbounds (%vtTy, ptr @_ZTVvt, i32 0, i32 0, i32 3)

!0 = !{i64 16, !"_ZTSvt"}
