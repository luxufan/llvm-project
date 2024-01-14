; RUN: opt -module-summary %s -o - | llvm-dis | FileCheck %s
;
; CHECK: ^5 = dyncastdst: ((name: "_ZTSB", count: 1))

@_ZTIA = external global ptr
@_ZTIB = external global ptr

declare ptr @__dynamic_cast(ptr, ptr, ptr, i64)

define ptr @test(ptr %a) {
entry:
  %b = call ptr @__dynamic_cast(ptr %a, ptr @_ZTIA, ptr @_ZTIB, i64 0)
  ret ptr %b
}
