; Native fastcc callback fixture; C never calls this entry directly.
target triple = "arm64-apple-macos12"
declare i32 @fixture_body(ptr, i32, i32, i32)
declare ptr @tribes_lookup(i32)
define fastcc void @fixture_callback(ptr %cpu) {
 %sp = getelementptr i8, ptr %cpu, i64 16
 %esp = load i32, ptr %sp
 %bp = getelementptr i8, ptr %cpu, i64 112
 %base = load i64, ptr %bp
 %offset = zext i32 %esp to i64
 %address = add i64 %base, %offset
 %p = inttoptr i64 %address to ptr
 %target = load i32, ptr %p, align 1
 %p0 = getelementptr i8, ptr %p, i64 4
 %p1 = getelementptr i8, ptr %p, i64 8
 %p2 = getelementptr i8, ptr %p, i64 12
 %a0 = load i32, ptr %p0, align 1
 %a1 = load i32, ptr %p1, align 1
 %a2 = load i32, ptr %p2, align 1
 %result = call i32 @fixture_body(ptr %cpu, i32 %a0, i32 %a1, i32 %a2)
 store i32 %result, ptr %cpu
 %next = add i32 %esp, 16
 store i32 %next, ptr %sp
 %fn = call ptr @tribes_lookup(i32 %target)
 musttail call fastcc void %fn(ptr %cpu)
 ret void
}
define fastcc void @tribes_return_to_host(ptr %cpu) { ret void }
define void @tribes_enter(ptr %cpu, i32 %va) {
 %sp = getelementptr i8, ptr %cpu, i64 16
 %esp = load i32, ptr %sp
 %next = sub i32 %esp, 4
 store i32 %next, ptr %sp
 %bp = getelementptr i8, ptr %cpu, i64 112
 %base = load i64, ptr %bp
 %offset = zext i32 %next to i64
 %address = add i64 %base, %offset
 %p = inttoptr i64 %address to ptr
 store i32 4294963200, ptr %p, align 1
 %fn = call ptr @tribes_lookup(i32 %va)
 call fastcc void %fn(ptr %cpu)
 ret void
}
