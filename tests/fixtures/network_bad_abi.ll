; Deliberate wrong first-argument widths: guard must stop before libc.
declare i32 @socket(i64, i32, i32)
declare i32 @shutdown(i64, i32)
declare i32 @getrlimit(i64, ptr)
declare i32 @setsockopt(i64, i32, i32, ptr, i32)
declare i32 @bind(i64, ptr, i32)
declare i32 @getsockname(i64, ptr, ptr)
define void @network_bad_abi(i32 %kind) {
 switch i32 %kind, label %a [i32 1, label %b i32 2, label %c i32 3, label %d i32 4, label %e i32 5, label %f]
a:
 %r0 = call i32 @socket(i64 2,i32 2,i32 0)
 ret void
b:
 %r1 = call i32 @shutdown(i64 0,i32 2)
 ret void
c:
 %r2 = call i32 @getrlimit(i64 8,ptr null)
 ret void
d:
 %r3 = call i32 @setsockopt(i64 0,i32 0,i32 0,ptr null,i32 4)
 ret void
e:
 %r4 = call i32 @bind(i64 0,ptr null,i32 16)
 ret void
f:
 %r5 = call i32 @getsockname(i64 0,ptr null,ptr null)
 ret void
}
