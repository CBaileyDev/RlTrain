// Release builds link against the Windows GUI subsystem so launching RL Studio never
// pops a console window behind it. Debug builds keep the console attached, because that
// is where `println!`, panics and Tauri's own logging show up during development.
#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

fn main() {
    rl_studio_lib::run();
}
