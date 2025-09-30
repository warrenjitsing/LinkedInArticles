fn main() {
    if cfg!(debug_assertions) {
        // This code will only be included in debug builds
        println!("This is a debug build!");
    } else {
        // This code will only be included in release builds
        println!("This is a release build!");
    }
}
