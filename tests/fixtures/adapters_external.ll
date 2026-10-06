declare i32 @getifaddrs(ptr)
declare void @freeifaddrs(ptr)
define i32 @adapters_guest_get(ptr %p) { %r = call i32 @getifaddrs(ptr %p)
 ret i32 %r }
define void @adapters_guest_free(ptr %p) { call void @freeifaddrs(ptr %p)
 ret void }
