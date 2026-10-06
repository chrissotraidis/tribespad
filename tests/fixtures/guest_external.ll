target triple = "arm64-apple-macos12"
target datalayout = "e-m:o-i64:64-i128:128-n32:64-S128-Fn32"
declare i64 @strlen(ptr)
declare i32 @getpid()
declare ptr @memcpy(ptr,ptr,i64)
declare ptr @malloc(i64)
declare void @unsafe_aggregate({ptr,i64})
define i64 @external_length(ptr %p) {
 %n = call i64 @strlen(ptr %p)
 ret i64 %n
}
define i64 @external_read(ptr %p) {
 %n = load i64,ptr %p,align 1
 ret i64 %n
}
define i64 @external_indirect(ptr %fn,ptr %p) {
 %n = call i64 %fn(ptr %p)
 ret i64 %n
}
define i32 @external_unknown() {
 %v = call i32 @getpid()
 ret i32 %v
}
define void @external_copy(ptr %d,ptr %s,i64 %n) {
 %p = call ptr @memcpy(ptr %d,ptr %s,i64 %n)
 ret void
}
define ptr @external_allocate(i64 %size) {
 %p = call ptr @malloc(i64 %size)
 ret ptr %p
}
define void @external_aggregate(ptr %p) {
 %a = insertvalue {ptr,i64} zeroinitializer,ptr %p,0
 call void @unsafe_aggregate({ptr,i64} %a)
 ret void
}

declare i64 @strnlen(ptr,i64)
define i64 @external_bounded(ptr %p,i64 %maximum) {
 %n = call i64 @strnlen(ptr %p,i64 %maximum)
 ret i64 %n
}

declare i32 @strcmp(ptr,ptr)
declare i32 @strncmp(ptr,ptr,i64)
define i32 @external_compare(ptr %a,ptr %b) {
 %n = call i32 @strcmp(ptr %a,ptr %b)
 ret i32 %n
}
define i32 @external_ncompare(ptr %a,ptr %b,i64 %limit) {
 %n = call i32 @strncmp(ptr %a,ptr %b,i64 %limit)
 ret i32 %n
}

@external_initializations = global i32 0
declare i32 @pthread_once(ptr,ptr)
declare i32 @pthread_key_create(ptr,ptr)
define void @external_initializer() {
 %v = load i32,ptr @external_initializations
 %next = add i32 %v,1
 store i32 %next,ptr @external_initializations
 ret void
}
define i32 @external_once(ptr %control,ptr %callback) {
 %v = call i32 @pthread_once(ptr %control,ptr %callback)
 ret i32 %v
}
define i32 @external_keycreate(ptr %key,ptr %callback) {
 %v = call i32 @pthread_key_create(ptr %key,ptr %callback)
 ret i32 %v
}

declare i32 @fcntl(i32,i32,...)
define i32 @external_fdflags(i32 %fd) {
 %v = call i32 (i32,i32,...) @fcntl(i32 %fd,i32 1)
 ret i32 %v
}
define i32 @external_fddup(i32 %fd) {
 %v = call i32 (i32,i32,...) @fcntl(i32 %fd,i32 67,i32 3)
 ret i32 %v
}
define i32 @external_fdunknown(i32 %fd,i32 %cmd,ptr %p) {
 %v = call i32 (i32,i32,...) @fcntl(i32 %fd,i32 %cmd,ptr %p)
 ret i32 %v
}
declare i64 @f64_sub(i64,i64)
declare i64 @f64_add(i64,i64)
define i64 @external_f64_sub(i64 %a,i64 %b) {
 %v = call i64 @f64_sub(i64 %a,i64 %b)
 ret i64 %v
}
define i64 @external_f64_add(i64 %a,i64 %b) {
 %v = call i64 @f64_add(i64 %a,i64 %b)
 ret i64 %v
}
define i32 @external_f64_bad() {
 %v = call i32 @f64_sub(i32 1,i32 2)
 ret i32 %v
}
define void @tb_sse_fence(ptr %cpu,i32 %kind) {
 %bad = icmp eq i32 %kind,99
 br i1 %bad,label %unknown,label %barriers
barriers:
 call void asm sideeffect "dsb sy", "~{memory}"()
 call void asm sideeffect "isb", "~{memory}"()
 ret void
unknown:
 call void asm sideeffect "nop", "~{memory}"()
 ret void
}
declare i32 @f32_add(i64,i64)
define i32 @external_f32_add(i64 %a,i64 %b) {
 %v = call i32 @f32_add(i64 %a,i64 %b)
 ret i32 %v
}
declare i32 @f32_sub(i64,i64)
define i32 @external_f32_sub(i64 %a,i64 %b) {
 %v = call i32 @f32_sub(i64 %a,i64 %b)
 ret i32 %v
}
declare i32 @f32_mul(i64,i64)
define i32 @external_f32_mul(i64 %a,i64 %b) {
 %v = call i32 @f32_mul(i64 %a,i64 %b)
 ret i32 %v
}
declare i32 @f32_div(i64,i64)
define i32 @external_f32_div(i64 %a,i64 %b) {
 %v = call i32 @f32_div(i64 %a,i64 %b)
 ret i32 %v
}
declare i64 @f64_mul(i64,i64)
define i64 @external_f64_mul(i64 %a,i64 %b) {
 %v = call i64 @f64_mul(i64 %a,i64 %b)
 ret i64 %v
}
declare i64 @f64_div(i64,i64)
define i64 @external_f64_div(i64 %a,i64 %b) {
 %v = call i64 @f64_div(i64 %a,i64 %b)
 ret i64 %v
}
define i64 @external_f32_bad() {
 %v = call i64 @f32_mul(i64 1,i64 2)
 ret i64 %v
}
declare [2 x i64] @f32_to_extF80(i64)
declare [2 x i64] @f64_to_extF80(i64)
declare [2 x i64] @i64_to_extF80(i64)
declare [2 x i64] @ui64_to_extF80(i64)
declare i32 @extF80_to_f32([2 x i64])
declare i64 @extF80_to_f64([2 x i64])
define i32 @external_ext_round32(i64 %a) {
 %e = call [2 x i64] @f32_to_extF80(i64 %a)
 %r = call i32 @extF80_to_f32([2 x i64] %e)
 ret i32 %r
}
define i64 @external_ext_round64(i64 %a) {
 %e = call [2 x i64] @f64_to_extF80(i64 %a)
 %r = call i64 @extF80_to_f64([2 x i64] %e)
 ret i64 %r
}
define i64 @external_ext_i64(i64 %a) {
 %e = call [2 x i64] @i64_to_extF80(i64 %a)
 %r = call i64 @extF80_to_f64([2 x i64] %e)
 ret i64 %r
}
define i64 @external_ext_ui64(i64 %a) {
 %e = call [2 x i64] @ui64_to_extF80(i64 %a)
 %r = call i64 @extF80_to_f64([2 x i64] %e)
 ret i64 %r
}
define i64 @external_ext_bad_result() {
 %r = call i64 @f32_to_extF80(i64 1065353216)
 ret i64 %r
}
define i32 @external_ext_bad_argument() {
 %r = call i32 @extF80_to_f32(i64 1)
 ret i32 %r
}
