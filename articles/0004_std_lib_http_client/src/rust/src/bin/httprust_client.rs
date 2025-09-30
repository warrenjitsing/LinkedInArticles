fn main() {
    if cfg!(debug_assertions) {
        // This code will only be included in debug builds
        println!("[httprust] This is a debug build!");
    } else {
        // This code will only be included in release builds
        println!("[httprust] This is a release build!");
    }
}
