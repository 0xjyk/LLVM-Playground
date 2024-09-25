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
## Project specific Updates
There's a lot of room for changes here but, this should atleast get you up and running. 
Consider a dummy project, with the following structure 
project: prof-llvm 
pass directory: prof/

#### Rename HelloWorld/
``` bash
# note: prof is the new name that we'd like to give the passes directory
mv HelloWorld/ prof
```

#### CMakeLists.txt
1. Change line 2 
   From: ``project(llvm-tutor)``
   To:   ``project(prof-llvm)``     # note: ``prof-llvm`` is the projects name 
2. Change line 144 
   From: ``add_subdirectory(HelloWorld)``
   To:   ``add_subdirectory(prof)    # note: ``prof`` is the pass's directory

#### Rename prof/HelloWorld.(cpp|h)
``` bash 
# note: nothing special, just renaming the files
mv HelloWorld.cpp prof.cpp
mv HelloWorld.h prof.h
```
#### prof/CMakeLists.txtA
1. Change line 2
   From: ``project()``
   To:   ``project(prof-pass)``       # note: ``prof-pass`` is just a name we give this sub-project
2. Change line 36
   From: ``add_library(prof SHARED prof.cpp)``
   To:   ``add_library(prof SHARED prof.cpp)``
3. Change line 40 
   From:  ``target_link_libraries(prof
                "$<$PLATFORM_ID:Darwin>: -undefined dynamic_lookup>")``






## Attributions
The CMake files, examples and directory structure has been sourced from [banach-space/llvm-tutor](https://github.com/banach-space/llvm-tutor/tree/main)
