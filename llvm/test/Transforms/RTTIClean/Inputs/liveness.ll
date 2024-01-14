target datalayout = "e-p:64:64"
target triple = "aarch64-unknown-linux-gnu"

declare i1 @llvm.type.test(ptr, metadata)
declare void @llvm.assume(i1)
declare ptr @__dynamic_cast(ptr, ptr, ptr, i64)
declare void @__cxa_throw(ptr, ptr, ptr)

%vtTy = type { [3 x ptr] }

declare i32 @vf()

@_ZTIvt = internal constant %vtTy zeroinitializer
@_ZTIvt1 = internal constant %vtTy zeroinitializer
@_ZTIvt2 = internal constant %vtTy zeroinitializer
@_ZTIvt4 = internal constant %vtTy zeroinitializer


define i32 @vcall(ptr %p) {
  %vtable = load ptr, ptr %p
  %t = call i1 @llvm.type.test(ptr %vtable, metadata !"_ZTSvt")
  call void @llvm.assume(i1 %t)
  %fptr = load ptr, ptr %vtable
  %result = call i32 %fptr()
  ret i32 %result
}

define ptr @rtti_load(ptr %p) {
  %vtable = load ptr, ptr %p
  %t = call i1 @llvm.type.test(ptr %vtable, metadata !"_ZTSvt1")
  call void @llvm.assume(i1 %t)
  %ap = getelementptr inbounds i8, ptr %vtable, i64 -8
  %rtti = load ptr, ptr %ap
  ret ptr %rtti
}

define ptr @dyncast(ptr %a) {
  %b = call ptr @__dynamic_cast(ptr %a, ptr @_ZTIvt2, ptr @_ZTIvt2, i64 0)
  ret ptr %b
}

define void @eh(ptr %a) {
  call void @__cxa_throw(ptr %a, ptr @_ZTIvt4, ptr %a)
  ret void
}
