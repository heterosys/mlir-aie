# MLIR-based AIEngine toolchain

![GitHub Workflow Status](https://img.shields.io/github/workflow/status/heterosys/mlir-aie/Build%20and%20Test)
![GitHub Pull Requests](https://img.shields.io/github/issues-pr-raw/heterosys/mlir-aie)

![](https://mlir.llvm.org//mlir-logo.png)

This repository contains an [MLIR-based](https://mlir.llvm.org/) toolchain for Xilinx Versal AIEngine-based devices.  This can be used to generate low-level configuration for the AIEngine portion of the device, including processors, stream switches, TileDMA and ShimDMA blocks. Backend code generation is included, targetting the LibXAIE library.  This project is primarily intended to support tool builders with convenient low-level access to devices and enable the development of a wide variety of programming models from higher level abstractions.  As such, although it contains some examples, this project is not intended to represent end-to-end compilation flows or to be particularly easy to use for system design.

[Full Documentation](https://xilinx.github.io/mlir-aie/)

## How to Build

### 0. Clone MLIR-AIE and install prerequisites

```
git clone https://github.com/heterosys/mlir-aie.git
cd mlir-aie
git clone --depth 1 https://github.com/Xilinx/cmakeModules.git

sudo apt-get install -y build-essential python3-pip libboost-all-dev
pip3 install cmake ninja lit psutil
```

### 1. Install LLVM, Clang, and MLIR

You can download our nightly pre-built snapshot from https://github.com/heterosys/llvm-nightly.

```sh
OS_DISTRIBUTER=$(lsb_release -is | tr '[:upper:]' '[:lower:]')
OS_RELEASE=$(lsb_release -rs)
LLVM_VERSION="snapshot-20220706"
LLVM_URL="https://github.com/heterosys/llvm-nightly/releases/download/${LLVM_VERSION}/llvm-clang-mlir-dev-15.${OS_DISTRIBUTER}-${OS_RELEASE}.deb"

TEMP_DEB="$(mktemp)" && \
  wget -O "${TEMP_DEB}" ${LLVM_URL} && \
  (sudo dpkg -i "${TEMP_DEB}" || sudo apt-get -yf install)
rm -f "${TEMP_DEB}"
```

Please update the variable `LLVM_VERSION` according to `.github/scripts/install-build-deps.sh`.

Or alternatively, build LLVM from scratch using the commit hash in `utils/clone-llvm.sh`.  When building manually, remember to enable `clang;lld;mlir` in `-DLLVM_ENABLE_PROJECTS` and `X86;ARM;AArch64` in `-DLLVM_TARGETS_TO_BUILD`.  `-DLLVM_BUILD_UTILS=ON` and `-DLLVM_INSTALL_UTILS=ON` shall be set as well.

### 2. Build MLIR-AIE

```sh
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Debug \
  -DLLVM_EXTERNAL_LIT=`which lit` \
  -DCMAKE_MODULE_PATH=$(pwd)/cmakeModules \
  -DCMAKE_MAKE_PROGRAM=ninja -G Ninja
  # If you build llvm-project manually, add the following options:
  # -DLLVM_DIR=${absolute path to llvm-project}/build/lib/cmake/llvm
  # -DMLIR_DIR=${absolute path to llvm-project}/build/lib/cmake/mlir
cmake --build build --target all
```

To test MLIR-AIE:

```sh
cmake --build build --target check-aie
```

### 3. Generate ELF of benchmarks

```sh
VITIS=[path_to_vitis] \
  aiecc.py -v --aie-generate-xaiev2
    --sysroot=[path_to_sysroot]
    test/benchmarks/01*/*
    runtime_lib/test_library.cpp
    -I runtime_lib/ -o kernel.elf

file kernel.elf
```

Cheers! 🍺

-----
<p align="center">Copyright&copy; 2019-2021 Xilinx</p>
