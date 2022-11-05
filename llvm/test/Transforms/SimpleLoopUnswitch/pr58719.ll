; RUN: opt -passes="require<globals-aa>,cgscc(instcombine),recompute-globalsaa,function(loop-mssa(simple-loop-unswitch<nontrivial>),print<memoryssa>)" -disable-output < %s 2>&1 | FileCheck %s
define void @f() {
entry:
  %0 = load i16, ptr null, align 1
  ret void
}

define void @g(i1 %tobool.not) {
entry:
  br label %for.cond

for.cond:                                         ; preds = %if.then, %for.cond, %entry
  br i1 %tobool.not, label %if.then, label %for.cond

if.then:                                          ; preds = %for.cond
; CHECK-NOT: MemoryUse(liveOnEntry)
  call void @f()
  br label %for.cond
}
