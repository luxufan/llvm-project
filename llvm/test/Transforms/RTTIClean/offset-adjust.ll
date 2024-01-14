; RUN: opt < %s -passes=rtti-clean -rtti-clean-read-import-summary=%S/Inputs/type.yaml -S | FileCheck %s
;
; Check the adjust offset in ModuleSummaryIndex works well.
%vtTy = type { [3 x ptr] }
@_ZTVvt = external constant %vtTy

define void @use(ptr %p) {
; CHECK-LABEL: define void @use(
; CHECK-SAME: ptr [[P:%.*]]) {
; CHECK-NEXT:    store ptr @_ZTVvt, ptr [[P]], align 8
; CHECK-NEXT:    ret void
  store ptr getelementptr inbounds ( %vtTy, ptr @_ZTVvt, i32 0, i32 0, i32 2), ptr %p
  ret void
}
