; RUN: opt -module-summary %s -o %t.bc
; RUN: llvm-dis %t.bc -o - | FileCheck %s
;
; CHECK: ^10 = vtableaccesses: ((name: "_ZTSA", offset: -8), (name: "_ZTSA", offset: -16))
@_ZTSA = external global ptr
@_ZTIB = external global ptr
@_ZTIC = external global ptr

declare ptr @__dynamic_cast(ptr, ptr, ptr, i64)

declare void @llvm.assume(i1)
declare i1 @llvm.type.test(ptr, metadata)

define internal ptr @t(ptr noundef %a) {
entry:
  %vtable = load ptr, ptr %a, align 8
  %ass = tail call i1 @llvm.type.test(ptr %vtable, metadata !"_ZTSA")
  tail call void @llvm.assume(i1 %ass)
  %ap = getelementptr inbounds ptr, ptr %vtable, i64 -1
  %r = load ptr, ptr %ap, align 8
  ret ptr %r
}

define internal ptr @t1(ptr noundef %a) {
entry:
  %vtable = load ptr, ptr %a, align 8
  %ass = tail call i1 @llvm.type.test(ptr %vtable, metadata !"_ZTSA")
  tail call void @llvm.assume(i1 %ass)
  %ap = getelementptr inbounds ptr, ptr %vtable, i64 -2
  %r = load ptr, ptr %ap, align 8
  ret ptr %r
}

define internal ptr @t2(ptr %a) {
  %b = call ptr @__dynamic_cast(ptr %a, ptr @_ZTIC, ptr @_ZTIB, i64 0)
  ret ptr %b
}
