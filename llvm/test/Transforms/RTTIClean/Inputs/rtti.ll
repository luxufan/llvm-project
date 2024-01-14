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

; Check _ZTVvt is optimized which the first two vtable slots are eliminated.
@_ZTVvt = internal constant %vtTy { [3 x ptr] [ptr null, ptr null, ptr @vf] }, !type !0

; Check _ZTVvt1 is not optimized since there is rtti load site.
@_ZTVvt1 = internal constant %vtTy { [3 x ptr] [ptr null, ptr null, ptr @vf] }, !type !1

; Check _ZTVvt2 is not optimized since there is dynamic cast for this type.
@_ZTVvt2 = internal constant %vtTy { [3 x ptr] [ptr null, ptr null, ptr @vf] }, !type !2

; TODO: support virtual inheritance
; Check _ZTVvt3 is not optimized since it has virtual inheritance metadata.
@_ZTVvt3 = internal constant %vtTy { [3 x ptr] [ptr null, ptr null, ptr @vf] }, !type !3, !virtual_inheritance !4

; Check _ZTVvt4 is not optimized since there is a throw site.
@_ZTVvt4 = internal constant %vtTy { [3 x ptr] [ptr null, ptr null, ptr @vf] }, !type !5

; Check vcall_visibility also works.
@_ZTVvt5 = constant %vtTy { [3 x ptr] [ptr null, ptr null, ptr @vf] }, !type !7, !vcall_visibility !6

; Check the optimization for _ZTVvt also adjust the offset in constants.
@temp = internal constant ptr getelementptr inbounds (%vtTy, ptr @_ZTVvt, i32 0, i32 0, i32 2)

; FIXME: although there is a reference to _ZTVvt, we still optimize for it.
; This may have some potential problems.
@nongepuse = internal constant [ 2 x ptr ] [ ptr null, ptr @_ZTVvt ]

; TODO: support reduce vtable slot for merged vtables
; Check that merged vtables are skipped.
@_ZTV.merged = internal constant %vtTy { [3 x ptr] [ptr null, ptr null, ptr @vf] }, !type !0

; FIXME: Optimize weak_odr is not correct.
@_ZTVvt6 = weak_odr constant %vtTy { [3 x ptr] [ptr null, ptr null, ptr @vf] }, !type !8


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

define void @use(ptr %p) {
  store ptr getelementptr inbounds ( %vtTy, ptr @_ZTVvt, i32 0, i32 0, i32 2), ptr %p
  store ptr getelementptr inbounds (i8, ptr @_ZTVvt, i64 16),  ptr %p
  store ptr getelementptr inbounds ( %vtTy, ptr @_ZTVvt1, i32 0, i32 0, i32 2), ptr %p
  store ptr getelementptr inbounds ( %vtTy, ptr @_ZTVvt2, i32 0, i32 0, i32 2), ptr %p
  ret void
}

!0 = !{i32 16, !"_ZTSvt"}
!1 = !{i32 16, !"_ZTSvt1"}
!2 = !{i32 16, !"_ZTSvt2"}
!3 = !{i32 16, !"_ZTSvt3"}
!4 = !{}
!5 = !{i32 16, !"_ZTSvt4"}
!6 = !{i64 1}
!7 = !{i32 16, !"_ZTSvt5"}
!8 = !{i32 16, !"_ZTSvt6"}
