; RUN: llvm-as %p/Input/basic.ll -o %t.index.bc
; RUN: llvm-as %p/Input/basic-non-hidden.ll -o %t.index.nonhidden.bc
; RUN: opt %s -S -passes=dyncastopt -dyncastopt-read-summary=%t.index.bc -o - | FileCheck --check-prefix=CHECK-OPT %s
; RUN: opt %s -S -passes=dyncastopt -dyncastopt-read-summary=%t.index.nonhidden.bc -o - | FileCheck --check-prefix=CHECK-NONHIDDEN %s
;
@_ZTIvt1 = external global ptr
@_ZTIvt2 = external global ptr

declare ptr @__dynamic_cast(ptr, ptr, ptr, i64)
define ptr @cast(ptr %a) {
; CHECK-OPT-LABEL: define ptr @cast(
; CHECK-OPT-SAME: ptr [[A:%.*]]) {
; CHECK-OPT-NEXT:  [[LOAD_BLOCK:.*:]]
; CHECK-OPT-NEXT:    [[RUNTIME_VPTR:%.*]] = load ptr, ptr [[A]], align 8
; CHECK-OPT-NEXT:    br label %[[CHECK_POINT_0:.*]]
; CHECK-OPT:       [[CHECK_POINT_0]]:
; CHECK-OPT-NEXT:    [[TMP0:%.*]] = icmp eq ptr [[RUNTIME_VPTR]], @_ZTVvt2
; CHECK-OPT-NEXT:    br i1 [[TMP0]], label %[[HANDLE_OFFSET:.*]], label %[[BB1:.*]]
; CHECK-OPT:       [[HANDLE_OFFSET]]:
; CHECK-OPT-NEXT:    br label %[[BB1]]
; CHECK-OPT:       [[BB1]]:
; CHECK-OPT-NEXT:    [[TMP2:%.*]] = phi ptr [ null, %[[CHECK_POINT_0]] ], [ [[A]], %[[HANDLE_OFFSET]] ]
; CHECK-OPT-NEXT:    ret ptr [[TMP2]]
;
; CHECK-NONHIDDEN: @__dynamic_cast(ptr %a, ptr @_ZTIvt1, ptr @_ZTIvt2, i64 0)
  %1 = call ptr @__dynamic_cast(ptr %a, ptr @_ZTIvt1, ptr @_ZTIvt2, i64 0)
  ret ptr %1
}

!0 = !{i32 0, !"_ZTSvt1"}
!1 = !{i32 0, !"_ZTSvt2"}
