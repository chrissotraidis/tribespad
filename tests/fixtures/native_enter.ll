; Minimal finite native host-entry fixture, no guest interpreter.
target triple = "arm64-apple-macos12"
declare ptr @tribes_lookup(i32)
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
