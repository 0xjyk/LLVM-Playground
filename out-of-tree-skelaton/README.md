# Out-of-tree skelaton
skelaton for out-of-tree LLVM@18 pass development

## Setup Instructions
The first thing we'd want to do is invoke cmake to creake the make files and proceed to compile the files
``` bash 
export LLVM_DIR=<installation/dir/of/llvm/18> # alternatively add it to ~/.bash_profile once and for all
mkdir build
cd build
cmake -DLT_LLVM_INSTALL_DIR=$LLVM_DIR <source/dir/of/this>/repo>/HelloWorld/
make
```
``` bash 
# Generate an LLVM test file
clang -O1 -S -emit-llvm -<source/dir/of/this/repo>/inputs/input_for_hello.c -o input_for_hello.ll
```
``` bash
# Run the pass 
$LLVM_DIR/bin/opt -load-pass-plugin ./libHelloWorld.{so|dylib} -passes=hw -disable-output input_for_hello.ll
# Output of the pass 
foo
bar 
baz
main
```
Sweet! Now you're all setup. Next, we need to get rid of all the "hello world" stuff and customize it for out own project







## Attributions
The CMake files, example and directory structure has been sourced from [banach-space/llvm-tutor](https://github.com/banach-space/llvm-tutor/tree/main)
