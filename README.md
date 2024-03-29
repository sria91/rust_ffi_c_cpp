# A tutorial for accessing C/C++ functions within a shared library (.dll/.so/.dylib) from Rust

_[19-09-2023] Updates:_
+ Add instructions and code for an alternative way to build and run using `build.rs` and cmake-rs
+ Fix the reason why not to link to C++ functions directly
+ Include `<cstring>` in the code and use `free` instead of `delete` as `strdup` generally uses `malloc` to allocate
+ _Update code and add information to prevent memory leakage when returning a heap-allocated object through the FFI_
+ _Removed libc dependency in favour of std::ffi in the Rust wrapper_
+ _In the C/C++ example, used cmake instead of msbuild as it is more cross-platform._

## Background

At my workplace, we are developing algorithms for a cloud-based analysis app. The algorithms are written in C++ and exported as a shared library to be used by the middleware in Java and the front end in JavaScript. Everything was working except for one problem, the service we had deployed faced the issue of memory pile-up. And since all the different components were tightly coupled it became very difficult to debug the issue.

So, after some discussion, we decided to decouple the different components by running the C++ algorithms as a REST web service.

We searched for C++ libraries to develop the REST endpoint but found that either they were not maintained or were extremely hard to set up.

Having some prior experience with Rust (especially REST web service development using Actix), I suggested that we could develop a wrapper in Rust and write the REST web API using some framework like Actix. I knew it was possible to wrap C/C++ code in Rust but not how. I searched for and found bits and pieces here and there but not a complete tutorial. This article is my attempt to compile all the information that I found and present it as a tutorial. I assume you already have some familiarity with C/C++, CMake, and Rust.

## Introduction

Let's start with some of the basics first.

1. Consider, we are developing a shared library named `c_cpp`. The shared library file will be called `c_ccp.dll` in Windows, `libc_cpp.so` in Linux, and `libc_cpp.dylib` in macOS.
    
2. It is not a good idea to link to C++ functions directly, as C++ doesn't have a stable ABI. Hence, If you are writing a C++ function you need to put it in your header(s) within the `extern "C" { }` block. This also tells the C++ compiler not to mangle its name.
    
3. If you are writing a C function you don't have to put it inside the `extern "C" { }` block, as the Rust compiler knows how to call it.
    

## The C/C++ example

I am taking an example of a C++ function called `introduce`. Let's see what its function declaration in the header (`c_cpp\include\lib.h`) looks like.

```cpp
#if defined(WIN32) || defined(_WIN32)
#define EXPORT __declspec(dllexport)
#else
#define EXPORT
#endif

extern "C" {
    EXPORT const char * introduce(const char * name, int age);
}
```

The `introduce` function takes a C-style string (name) and an int (age) as parameters, constructs another string from the parameters, and returns it as a C-style string. In this tutorial, I'll be using Windows as the development platform. You should be able to easily adapt it to other platforms.

Following is the function definition in the source file (`c_cpp\src\lib.cpp`).

```cpp
#include <sstream>
#include <cstring>
#include "lib.h"

std::stringstream ss;

const char * introduce(const char * name, int age) {
    ss.clear();
    ss << "Hi, I am " << name << ". My age is " << age << ".";
    return strdup(ss.str().c_str());
}
```

To the above code files, we will also add the `deallocate_string` function. This will aid in preventing memory leaks that can occur if the heap-allocated string is not deallocated. We are going to call this function from the Rust wrapper.

`c_cpp\include\lib.h`

```cpp
#if defined(WIN32) || defined(_WIN32)
#define EXPORT __declspec(dllexport)
#else
#define EXPORT
#endif

extern "C" {
    EXPORT const char * introduce(const char * name, int age);

    EXPORT void deallocate_string(const char * s);
}
```

`c_cpp\src\lib.cpp`

```cpp
#include <sstream>
#include <cstdlib>
#include <cstring>
#include "lib.h"

std::stringstream ss;

const char * introduce(const char * name, int age) {
    ss.clear();
    ss << "Hi, I am " << name << ". My age is " << age << ".";
    return strdup(ss.str().c_str());
}

void deallocate_string(const char * s) {
    free((void*)s);
}
```

I'm also going to create the main source file (`c_cpp\src\main.cpp`) to build an executable and quickly test my function.

```cpp
#include <iostream>

#include "lib.h"

int main() {
    const char * s = introduce("Srikanth", 31);
    std::cout << s << std::endl;

    deallocate_string(s);

    return 0;
}
```

I will use CMake as the build tool. So, the following is my `c_cpp\CMakeLists.txt` file.

```plaintext
cmake_minimum_required(VERSION 3.24)

project("c_cpp")

include_directories(./include)

add_library(${PROJECT_NAME} SHARED ./src/lib.cpp)

add_executable(${PROJECT_NAME}_main ./src/main.cpp ./src/lib.cpp)

install(TARGETS ${PROJECT_NAME} DESTINATION .)
```

I'll build the shared library and the test executable using the following commands:

```plaintext
cd c_cpp
cmake -S . -B .\build\
cmake --build .\build\
```

Running the test executable should be straightforward:

```plaintext
C:\rust_ffi_c_cpp\c_cpp> .\build\Debug\c_cpp_main.exe
Hi, I am Srikanth. My age is 31.
```

## The Rust wrapper

For developing the Rust wrapper I'm first going to create a library project using cargo.

```plaintext
C:\rust_ffi_c_cpp> cargo new --lib rust_ffi
```

Now replace the contents of the `rust_ffi\src\lib.rs` file with the following:

```rust
use std::{ffi::{CString, CStr, c_char, c_int}, error::Error};

#[link(name = "c_cpp")]  // name of the C shared library
extern "C" {
    fn introduce(name: *const c_char, age: c_int) -> *const c_char;

    fn deallocate_string(s: *const c_char);
}

fn get_str_slice_from_c_char_ptr<'a>(input_c_char: *const c_char) -> Result<&'a str, Box<dyn Error>> {
    let input_c_str = unsafe { CStr::from_ptr(input_c_char) };
    match input_c_str.to_str() {
        Ok(str_slice) => Ok(str_slice),
        Err(error) => Err(Box::new(error)),
    }
}

pub fn introduce_rust(name: &str, age: c_int) -> Result<String, Box<dyn Error>> {
    let name = CString::new(name)?;
    let introduction = unsafe { introduce(name.as_ptr(), age) };
    let introduction_string = get_str_slice_from_c_char_ptr(introduction)?.to_string();
    unsafe { deallocate_string(introduction) };
    Ok(introduction_string)
}
```

The first `use` line imports standard library features related to handling C-style strings, the C types and the Error trait.

The next block of code is where we specify the name of the C/C++ shared library and the function(s) within it that we are going to use. In our case, the signature of the function(s) should match those in the `c_cpp\include\lib.h` file.

The `get_str_slice_from_c_char_ptr` convenience function is the one that converts the raw `const char *` pointer from C to a `&str` in Rust. It makes use of an `unsafe` code block to accomplish this.

The `introduce_rust` function is the actual wrapper around the `introduce` C/C++ function. It takes the first input parameter as `&str` and converts it into a C-style string. The second parameter is an integer and is passed as it is. This function also makes use of an unsafe code block for the C function. Then it makes use of the `get_str_slice_from_c_char_ptr` convenience function to convert the returned C-style string into a Rust `String`. And it deallocates the c-string to prevent memory leakage.

In order to test that our approach actually works let's write a `rust_ffi\src\main.rs`. Following are its contents:

```rs
use std::error::Error;

use rust_ffi::introduce_rust;

fn main() -> Result<(), Box<dyn Error>> {
    let introduction = introduce_rust("Srikanth", 31)?;
    println!("{introduction}");
    Ok(())
}
```

We can build our project by using the following commands from the `rust_ffi` directory:

```plaintext
set RUSTFLAGS=-L..\c_cpp\build\Debug
cargo build
```

The first command sets the Rust linker flag `-L` to the path containing the C/C++ shared library. And the second command builds the project.

Now we can run the built Rust executable by using the following commands from the `rust_ffi` directory:

```plaintext
set PATH=..\c_cpp\build\Debug;%PATH%
target\debug\rust_ffi.exe
```

And voila we get our expected output:

```plaintext
Hi, I am Srikanth. My age is 31.
```
### An alternative way to build and run the Rust wrapper using build.rs and cmake-rs

Being in the same Rust project modify `rust_ffi\Cargo.toml` to look like as follows:

```ini
[package]
name = "rust_ffi"
version = "0.1.0"
edition = "2021"
build = "build.rs"

# See more keys and their definitions at https://doc.rust-lang.org/cargo/reference/manifest.html

[dependencies]

[build-dependencies]
cmake = "0.1.50"
```

And add `rust_ffi\build.rs` with the following contents:

```rust
use std::path::Path;
use cmake::Config;

fn main()
{
    let dst = Config::new(Path::new("..").join("c_cpp")).build();

    println!("cargo:rustc-link-search=native={}", dst.display());
    println!("cargo:rustc-link-lib=dylib=c_cpp");
}
```

Now one can simply run from the `rust_ffi` directory, the command `cargo build` to build the C/C++ shared library and the Rust executable in one shot. One can also run the Rust executable by simply running the command `cargo run`.

[Link to the code](https://github.com/sria91/rust_ffi_c_cpp.git)

[Try online in a new Codespace](https://codespace.new/sria91/rust_ffi_c_cpp)
