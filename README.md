# Offload Kernel Identification
Code for my thesis on automated kernel identification for offloading to processing-in-memory cores using LLVM.

This is a public reupload from the private project repository.

## Compiler Pass

Install LLVM package (Ubuntu-Bionic):


> wget -O - https://apt.llvm.org/llvm-snapshot.gpg.key | sudo apt-key add -  
> sudo apt-add-repository "deb http://apt.llvm.org/bionic/ llvm-toolchain-bionic-11 main"  
> sudo apt-get update  
> sudo apt-get install -y llvm-11 llvm-11-dev clang-11 llvm-11-tools  

This will install all the required header files, libraries and tools in /usr/lib/llvm-11/

To compile pass:

> cd /passes_srcbuild  
> cmake -DLT_LLVM_INSTALL_DIR=<installation/dir/of/llvm/11> .  
> make

To generate input file and run pass:

> export LLVM_DIR_11=<installation/dir/of/llvm/11>  
> \$LLVM_DIR_11/bin/clang -g -O2 -S -emit-llvm -fno-discard-value-names benchmark.c -o benchmark.ll  
> \$LLVM_DIR/bin/opt -load-pass-plugin ./libFindOffloadLoops.so -passes=loop-simplify,find-offload-loops -disable-output benchmark.ll 2>&1 
