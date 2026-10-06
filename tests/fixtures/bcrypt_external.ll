declare void @arc4random_buf(ptr, i64)
define void @rng_guest_pointer(ptr %p) { call void @arc4random_buf(ptr %p, i64 128)
 ret void
}
