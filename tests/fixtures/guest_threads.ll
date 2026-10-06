target triple = "arm64-apple-macos12"
declare void @fixture_notify(ptr, i32)
declare void @fixture_procedure(ptr)
declare void @fixture_cleanup(ptr)
define fastcc void @fixture_tls_a(ptr %cpu) { call void @fixture_notify(ptr %cpu, i32 0)
 ret void
}
define fastcc void @fixture_dll_a(ptr %cpu) { call void @fixture_notify(ptr %cpu, i32 1)
 ret void
}
define fastcc void @fixture_tls_b(ptr %cpu) { call void @fixture_notify(ptr %cpu, i32 2)
 ret void
}
define fastcc void @fixture_dll_b(ptr %cpu) { call void @fixture_notify(ptr %cpu, i32 3)
 ret void
}
define fastcc void @fixture_start(ptr %cpu) { call void @fixture_procedure(ptr %cpu)
 ret void
}
define fastcc void @fixture_fls(ptr %cpu) { call void @fixture_cleanup(ptr %cpu)
 ret void
}
