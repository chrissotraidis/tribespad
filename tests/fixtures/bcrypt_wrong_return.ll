declare i32 @arc4random_buf(ptr, i64)
define void @rng_wrong_return(ptr %p) { %r = call i32 @arc4random_buf(ptr %p, i64 128)
 ret void
}
