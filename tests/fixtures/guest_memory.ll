target triple = "arm64-apple-macos12"
target datalayout = "e-m:o-i64:64-i128:128-n32:64-S128-Fn32"
declare void @llvm.memcpy.p0.p0.i64(ptr, ptr, i64, i1)
declare void @llvm.memset.p0.i64(ptr, i8, i64, i1)
declare ptr @memmove(ptr,ptr,i64)
declare ptr @memset(ptr,i32,i64)
define i64 @checked_read(ptr %p) memory(argmem: read) nosync willreturn {
 %x = load volatile i64, ptr %p, align 1
 ret i64 %x
}
define void @checked_write(ptr %p,i64 %v) memory(argmem: write) nosync willreturn {
 store volatile i64 %v, ptr %p, align 1
 ret void
}
define void @checked_vector(ptr %dest,ptr %source) {
 %v = load <4 x i32>, ptr %source, align 1
 store <4 x i32> %v, ptr %dest, align 1
 ret void
}
define i32 @checked_atomic(ptr %p) {
 %v = atomicrmw add ptr %p, i32 1 seq_cst
 ret i32 %v
}
define i32 @checked_cas(ptr %p) {
 %pair = cmpxchg ptr %p, i32 17, i32 31 seq_cst seq_cst
 %v = extractvalue {i32,i1} %pair,0
 ret i32 %v
}
define void @checked_copy(ptr %dest,ptr %source,i64 %size) {
 call void @llvm.memcpy.p0.p0.i64(ptr %dest,ptr %source,i64 %size,i1 false)
 ret void
}
define void @checked_move(ptr %dest,ptr %source,i64 %size) {
 %p = tail call ptr @memmove(ptr %dest,ptr %source,i64 %size)
 ret void
}
define void @checked_set(ptr %dest,i64 %size) {
 call void @llvm.memset.p0.i64(ptr %dest,i8 79,i64 %size,i1 false)
 ret void
}
define void @checked_cset(ptr %dest,i64 %size) {
 %p = tail call ptr @memset(ptr %dest,i32 58,i64 %size)
 ret void
}
declare ptr @__memcpy_chk(ptr,ptr,i64,i64)
declare ptr @__memmove_chk(ptr,ptr,i64,i64)
declare ptr @__memset_chk(ptr,i32,i64,i64)
declare ptr @"\01_memmove"(ptr,ptr,i64)
define void @checked_copy_chk(ptr %dest,ptr %source,i64 %size) {
 %p = call ptr @__memcpy_chk(ptr %dest,ptr %source,i64 %size,i64 65536)
 ret void
}
define void @checked_move_chk(ptr %dest,ptr %source,i64 %size) {
 %p = call ptr @__memmove_chk(ptr %dest,ptr %source,i64 %size,i64 65536)
 ret void
}
define void @checked_set_chk(ptr %dest,i64 %size) {
 %p = call ptr @__memset_chk(ptr %dest,i32 43,i64 %size,i64 65536)
 ret void
}
define void @checked_named_move(ptr %dest,ptr %source,i64 %size) {
 %p = call ptr @"\01_memmove"(ptr %dest,ptr %source,i64 %size)
 ret void
}
