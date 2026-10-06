declare i64 @getifaddrs(ptr)
declare i32 @freeifaddrs(ptr)
define i64 @adapters_wrong_get(ptr %p) { %r = call i64 @getifaddrs(ptr %p)
 ret i64 %r }
define i32 @adapters_wrong_free(ptr %p) { %r = call i32 @freeifaddrs(ptr %p)
 ret i32 %r }
