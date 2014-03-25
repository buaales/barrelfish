--------------------------------------------------------------------------
-- Copyright (c) 2007-2010, ETH Zurich.
-- All rights reserved.
--
-- This file is distributed under the terms in the attached LICENSE file.
-- If you do not find this file, copies can be found by writing to:
-- ETH Zurich D-INFK, Haldeneggsteig 4, CH-8092 Zurich. Attn: Systems Group.
--
-- Architectural definitions for Barrelfish on x86_mic.
--
-- This architecture is used to build for the Intel Xeon Phi architecture.
-- 
--------------------------------------------------------------------------

module K1om where

import HakeTypes
import Path
import qualified Config
import qualified ArchDefaults

-------------------------------------------------------------------------
--
-- Architecture specific definitions for X86_64-k1om
--
-------------------------------------------------------------------------

arch = "k1om"
archFamily = "k1om"

--compiler = "x86_64-k1om-barrelfish-gcc"
--objcopy  = "x86_64-k1om-barrelfish-objcopy"
--objdump  = "x86_64-k1om-barrelfish-objdump"
--ar       = "x86_64-k1om-barrelfish-ar"
--ranlib   = "x86_64-k1om-barrelfish-ranlib"
--cxxcompiler = "x86_64-k1om-barrelfish-g++"


compiler = "gcc"
objcopy  = "objcopy"
objdump  = "objdump"
ar       = "ar"
ranlib   = "ranlib"
cxxcompiler = "g++"


ourCommonFlags = [ --Str "-m64",
                   Str "-mno-red-zone",
                   Str "-fPIE",
                   Str "-fno-stack-protector", 
                   Str "-Wno-unused-but-set-variable",
                   Str "-Wno-packed-bitfield-compat",
-- the intel mic architecture has no "normal" SIMD extensions
--                   Str "-mno-mmx",
--                   Str "-mno-sse",
                   Str "-mno-sse2",
                   Str "-mno-sse3",
                   Str "-mno-sse4.1",
                   Str "-mno-sse4.2",
                   Str "-mno-sse4",
                   Str "-mno-sse4a",
                   Str "-mno-3dnow", 
-- specific Xeon Phi architecture
--                   Str "-Wa,-march=k1om",
--                   Str "-Wa,-mtune=k1om",
                   Str "-D__x86__" ]

cFlags = ArchDefaults.commonCFlags
               ++ ArchDefaults.commonFlags
               ++ ourCommonFlags

cxxFlags = ArchDefaults.commonCxxFlags
                 ++ ArchDefaults.commonFlags
                 ++ ourCommonFlags

cDefines = ArchDefaults.cDefines options

-- TODO> -m elf_i386
ourLdFlags = [ Str "-Wl,-z,max-page-size=0x1000",
--               Str "-Wl,-b,elf64-k1om",
--               Str "-Wl,--oformat,elf64-k1om",
               Str "-Wl,--build-id=none"]
               --Str "-m64" 


ldFlags = ArchDefaults.ldFlags arch ++ ourLdFlags
ldCxxFlags = ArchDefaults.ldCxxFlags arch ++ ourLdFlags

options = (ArchDefaults.options arch archFamily) { 
            optFlags = cFlags,
            optCxxFlags = cxxFlags,
            optDefines = cDefines,
            optLdFlags = ldFlags,
            optLdCxxFlags = ldCxxFlags,
            optInterconnectDrivers = ["lmp", "ump", "multihop"],
            optFlounderBackends = ["lmp", "ump", "multihop"]
          }

--
-- The kernel is "different"
--

kernelCFlags = [ Str s | s <- [ "-fno-builtin",
                                "-nostdinc",
                                "-std=c99",
                                "-mno-red-zone",
                                "-fPIE",
                                "-fno-stack-protector",
                                "-U__linux__",
                                "-Wall",
                                "-Wshadow",
                                "-Wstrict-prototypes",
                                "-Wold-style-definition",
                                "-Wmissing-prototypes",
                                "-Wmissing-declarations",
                                "-Wmissing-field-initializers",
                                "-Wredundant-decls",
                                "-Wno-packed-bitfield-compat",
                                "-Wno-unused-but-set-variable",
                                "-Werror",
                                "-imacros deputy/nodeputy.h",
                                "-mno-mmx",
                                "-mno-sse",
                                "-mno-sse2",
                                "-mno-sse3",
                                "-mno-sse4.1",
                                "-mno-sse4.2",
--              "-Wno-unused-but-set-variable",
                                "-mno-sse4",
                                "-mno-sse4a",
                                "-mno-3dnow" ] ]
	

kernelLdFlags = [ Str s | s <- [ "-Wl,-N",
--                                 "-Wl,-b,elf64-k1om",
                                -- "-Wl,-A,k1om",
--                                 "-Wl,--oformat,elf64-k1om",
                                 "-pie",
                                 "-fno-builtin",
                                 "-nostdlib",
                                 "-Wl,--fatal-warnings"] ]
--                                "-m64" 


------------------------------------------------------------------------
--
-- Now, commands to actually do something
--
------------------------------------------------------------------------

--
-- Compilers
--
cCompiler = ArchDefaults.cCompiler arch compiler
cxxCompiler = ArchDefaults.cxxCompiler arch cxxcompiler
makeDepend = ArchDefaults.makeDepend arch compiler
makeCxxDepend  = ArchDefaults.makeCxxDepend arch cxxcompiler
cToAssembler = ArchDefaults.cToAssembler arch compiler
assembler = ArchDefaults.assembler arch compiler
archive = ArchDefaults.archive arch
linker = ArchDefaults.linker arch compiler
cxxlinker = ArchDefaults.cxxlinker arch cxxcompiler

--
-- Link the kernel (CPU Driver)
-- 
linkKernel :: Options -> [String] -> [String] -> String -> HRule
linkKernel opts objs libs kbin = 
    let linkscript = "/kernel/linker.lds"
    in
      Rules [ Rule ([ Str compiler, Str Config.cOptFlags,
                      NStr "-T", In BuildTree arch "/kernel/linker.lds",
                      Str "-o", Out arch kbin 
                    ]
                    ++ (optLdFlags opts)
                    ++
                    [ In BuildTree arch o | o <- objs ]
                    ++
                    [ In BuildTree arch l | l <- libs ]
                    ++ 
                    [ NL, NStr "/bin/echo -e '\\0002' | dd of=",
                      Out arch kbin, 
                      Str "bs=1 seek=16 count=1 conv=notrunc status=noxfer"
                    ]
                   ),
              Rule [ Str "cpp", 
                     NStr "-I", NoDep SrcTree "src" "/kernel/include/",
                     Str "-D__ASSEMBLER__", 
                     Str "-P", In SrcTree "src" "/kernel/arch/k1om/linker.lds.in",
                     Out arch linkscript 
                   ]
            ]
