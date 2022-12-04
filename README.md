# A tutorial for accessing C/C++ functions within a shared library (.dll/.so/.dylib) from Rust

## Background

At my workplace, we are developing algorithms for a cloud-based analysis app. The algorithms are written in C++ and exported as a shared library to be used by the middleware in Java and the front end in JavaScript. Everything was working except for one problem, the service we had deployed faced the issue of memory pile-up. And since all the different components were tightly coupled it became very difficult to debug the issue.

So, after some discussion, we decided to decouple the different components by running the C++ algorithms as a REST web service.

We searched for C++ libraries to develop the REST endpoint but found that either they were not maintained or were extremely hard to set up.

Having some prior experience with Rust (especially REST web service development using Actix), I suggested that we could develop a wrapper in Rust and write the REST web API using some framework like Actix. I knew it was possible to wrap C/C++ code in Rust but not how. I searched for and found bits and pieces here and there but not a complete tutorial. This article is my attempt to compile all the information that I found and present it as a tutorial. I assume you already have some familiarity with C/C++, CMake, and Rust.

## Introduction

Let's start with some of the basics first.

1.  Consider, we are developing a shared library named `c_cpp`. The shared library file will be called `c_ccp.dll` in Windows, `libc_cpp.so` in Linux, and `libc_cpp.dylib` in macOS.
    
2.  When you compile a C++ function its name gets mangled, and Rust won't know how to call it. Hence, If you are writing a C++ function you need to put it in your header(s) within the `extern "C" { }` block. This tells the C++ compiler not to mangle its name.
    
3.  If you are writing a C function you don't have to put it inside the `extern "C" { }` block, as the Rust compiler knows how to call it.
    

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
#include "lib.h"

std::stringstream ss;

const char * introduce(const char * name, int age) {
    ss.clear();
    ss << "Hi, I am " << name << ". My age is " << age << ".";
    return strdup(ss.str().c_str());
}
```

I'm also going to create the main source file (`c_cpp\src\main.cpp`) to build an executable and quickly test my function.

```cpp
#include <iostream>

#include "lib.h"

int main() {
    std::cout << introduce("Srikanth", 31) << std::endl;
}
```

I will use CMake as the build tool. So, the following is my `c_cpp\CMakeLists.txt` file.

```cmake
cmake_minimum_required(VERSION 3.24)

project("c_cpp")

include_directories(./include)

add_library(${PROJECT_NAME} SHARED ./src/lib.cpp)

add_executable(${PROJECT_NAME}_main ./src/main.cpp ./src/lib.cpp)
```

I'll build the shared library and the test executable using the following commands:

```bat
cd c_cpp
cmake.exe -S . -B build
msbuild build\c_cpp.sln
```

Running the test executable should be straightforward:
```bat
C:\rust_ffi_c_cpp\c_cpp> .\build\Debug\c_cpp_main.exe
Hi, I am Srikanth. My age is 31.
```

## The Rust wrapper

For developing the Rust wrapper I'm first going to create a library project using cargo.
```bat
C:\rust_ffi_c_cpp> cargo new --lib rust_ffi
```

We are going to use the `libc` crate for working with the C types. So add the following line to `Cargo.toml` under `[dependencies]`:
```toml
libc = "0.2.137"
```

Now replace the contents of the `rust_ffi\src\lib.rs` file with the following:
```rs
use std::{ffi::{CString, CStr}, error::Error};

use libc::{c_char, c_int};

#[link(name = "c_cpp")]  // name of the C shared library
extern "C" {
    fn introduce(name: *const c_char, age: c_int) -> *const c_char;
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
    Ok(introduction_string)
}
```

The first `use` line imports standard library features related to handling C-style strings and the Error trait.

The second `use` line imports the C types from `libc`.

The next block of code is where we specify the name of the C shared library and the function(s) within it that we are going to use. In our case, the signature of the function(s) should match those in the `c_cpp\include\lib.h` file.

The `get_str_slice_from_c_char_ptr` convenience function is the one that converts the raw `const char *` pointer from C to a `&str` in Rust. It makes use of an `unsafe` code block to accomplish this.

The `introduce_rust` function is the actual wrapper around the `introduce` C/C++ function. It takes the first input parameter as `&str` and converts it into a C-style string. The second parameter is an integer and is passed as it is. This function also makes use of an unsafe code block for the C function. Then it makes use of the `get_str_slice_from_c_char_ptr` convenience function to convert the returned C-style string into a Rust `String`.

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
```bat
set RUSTFLAGS=-L..\c_cpp\build\Debug
cargo build
```

The first command sets the Rust linker flag `-L` to the path containing the C shared library. And the second command builds the project.

Now we can run the built Rust executable by using the following commands from the `rust_ffi` directory:
```bat
set PATH=..\c_cpp\build\Debug;%PATH%
target\debug\rust_ffi.exe
```

And voila we get our expected output:
```bat
Hi, I am Srikanth. My age is 31.
```

[Link to the code](https://github.com/sria91/rust_ffi_c_cpp.git)
