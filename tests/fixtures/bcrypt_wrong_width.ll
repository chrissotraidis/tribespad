declare void @arc4random_buf(ptr, i32)
define void @rng_wrong_width(ptr %p) { call void @arc4random_buf(ptr %p, i32 128)
 ret void
}
