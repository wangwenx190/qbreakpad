/*
 * MIT License
 *
 * Copyright (C) 2020 by wangwenx190 (Yuhang Zhao)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/*
 * Copied from https://github.com/md748/QBreakpad/blob/master/breakpad/WindowsDllInterceptor.h
 * With some modifications, most of them are format changes.
 */

#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <stdint.h>
#include <stdio.h>
#include <windows.h>
#include <winternl.h>

class WindowsDllNopSpacePatcher
{
    using byteptr_t = unsigned char *;
    HMODULE mModule = nullptr;

    // Dumb array for remembering the addresses of functions we've patched.
    // (This should be nsTArray, but non-XPCOM code uses this class.)
    static const size_t maxPatchedFns = 128;
    byteptr_t mPatchedFns[maxPatchedFns];
    int mPatchedFnsLen = 0;

public:
    explicit WindowsDllNopSpacePatcher() {}

    ~WindowsDllNopSpacePatcher()
    {
        // Restore the mov edi, edi to the beginning of each function we patched.

        for (int i = 0; i != mPatchedFnsLen; ++i) {
            byteptr_t fn = mPatchedFns[i];

            // Ensure we can write to the code.
            DWORD op = 0;
            if (!VirtualProtectEx(GetCurrentProcess(), fn, 2, PAGE_EXECUTE_READWRITE, &op)) {
                fwprintf(stderr, L"VirtualProtectEx failed.\r\n");
                continue;
            }

            // mov edi, edi
            *((uint16_t *) fn) = 0xff8b;

            // Restore the old protection.
            VirtualProtectEx(GetCurrentProcess(), fn, 2, op, &op);

            // I don't think this is actually necessary, but it can't hurt.
            FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
        }
    }

    void Init(LPCWSTR modulename)
    {
        mModule = LoadLibraryExW(modulename, nullptr, 0);
        if (!mModule) {
            fwprintf(stderr, L"LoadLibraryEx for '%s' failed.\r\n", modulename);
            return;
        }
    }

#ifdef _M_IX86
    bool AddHook(const char *pname, intptr_t hookDest, void **origFunc)
    {
        if (!mModule) {
            return false;
        }

        if (mPatchedFnsLen == maxPatchedFns) {
            fwprintf(stderr, L"No space for hook in mPatchedFns.\r\n");
            return false;
        }

        auto fn = reinterpret_cast<byteptr_t>(GetProcAddress(mModule, pname));
        if (!fn) {
            fwprintf(stderr, L"GetProcAddress failed.\r\n");
            return false;
        }

        // Ensure we can read and write starting at fn - 5 (for the long jmp we're
        // going to write) and ending at fn + 2 (for the short jmp up to the long
        // jmp).
        DWORD op = 0;
        if (!VirtualProtectEx(GetCurrentProcess(), fn - 5, 7, PAGE_EXECUTE_READWRITE, &op)) {
            fwprintf(stderr, L"VirtualProtectEx failed.\r\n");
            return false;
        }

        bool rv = WriteHook(fn, hookDest, origFunc);

        // Re-protect, and we're done.
        VirtualProtectEx(GetCurrentProcess(), fn - 5, 7, op, &op);

        if (rv) {
            mPatchedFns[mPatchedFnsLen] = fn;
            mPatchedFnsLen++;
        }

        return rv;
    }

    bool WriteHook(byteptr_t fn, intptr_t hookDest, void **origFunc)
    {
        // Check that the 5 bytes before fn are NOP's or INT 3's,
        // and that the 2 bytes after fn are mov(edi, edi).
        //
        // It's safe to read fn[-5] because we set it to PAGE_EXECUTE_READWRITE
        // before calling WriteHook.

        for (int i = -5; i <= -1; i++) {
            // nop or int 3
            if (fn[i] != 0x90 && fn[i] != 0xcc) {
                return false;
            }
        }

        // mov edi, edi.  Yes, there are two ways to encode the same thing:
        //
        //   0x89ff == mov r/m, r
        //   0x8bff == mov r, r/m
        //
        // where "r" is register and "r/m" is register or memory.  Windows seems to
        // use 8bff; I include 89ff out of paranoia.
        if ((fn[0] != 0x8b && fn[0] != 0x89) || fn[1] != 0xff) {
            return false;
        }

        // Write a long jump into the space above the function.
        fn[-5] = 0xe9;                                         // jmp
        *((intptr_t *) (fn - 4)) = hookDest - (uintptr_t)(fn); // target displacement

        // Set origFunc here, because after this point, hookDest might be called,
        // and hookDest might use the origFunc pointer.
        *origFunc = fn + 2;

        // Short jump up into our long jump.
        *((uint16_t *) (fn)) = 0xf9eb; // jmp $-5

        // I think this routine is safe without this, but it can't hurt.
        FlushInstructionCache(GetCurrentProcess(), nullptr, 0);

        return true;
    }
#else
    bool AddHook(const char *pname, intptr_t hookDest, void **origFunc)
    {
        // Not implemented except on x86-32.
        UNREFERENCED_PARAMETER(pname);
        UNREFERENCED_PARAMETER(hookDest);
        UNREFERENCED_PARAMETER(origFunc);
        return false;
    }
#endif
};

class WindowsDllDetourPatcher
{
    using byteptr_t = unsigned char *;

public:
    explicit WindowsDllDetourPatcher() {}

    ~WindowsDllDetourPatcher()
    {
        int i = 0;
        byteptr_t p = nullptr;
        for (i = 0, p = mHookPage; i != mCurHooks; ++i, p += kHookSize) {
#ifdef _M_IX86
            size_t nBytes = 1 + sizeof(intptr_t);
#elif defined(_M_X64)
            size_t nBytes = 2 + sizeof(intptr_t);
#else
#error "Unknown processor type"
#endif
            byteptr_t origBytes = *((byteptr_t *) p);
            // ensure we can modify the original code
            DWORD op = 0;
            if (!VirtualProtectEx(GetCurrentProcess(),
                                  origBytes,
                                  nBytes,
                                  PAGE_EXECUTE_READWRITE,
                                  &op)) {
                fwprintf(stderr, L"VirtualProtectEx failed.\r\n");
                continue;
            }
            // Remove the hook by making the original function jump directly
            // in the trampoline.
            intptr_t dest = (intptr_t)(p + sizeof(void *));
#ifdef _M_IX86
            *((intptr_t *) (origBytes + 1)) = dest
                                              - (intptr_t)(origBytes + 5); // target displacement
#elif defined(_M_X64)
            *((intptr_t *) (origBytes + 2)) = dest;
#else
#error "Unknown processor type"
#endif
            // restore protection; if this fails we can't really do anything about it
            VirtualProtectEx(GetCurrentProcess(), origBytes, nBytes, op, &op);
        }
    }

    void Init(LPCWSTR modulename, int nhooks = 0)
    {
        if (mModule) {
            return;
        }

        mModule = LoadLibraryExW(modulename, nullptr, 0);
        if (!mModule) {
            fwprintf(stderr, L"LoadLibraryEx for '%s' failed.\r\n", modulename);
            return;
        }

        int hooksPerPage = 4096 / kHookSize;
        if (nhooks == 0) {
            nhooks = hooksPerPage;
        }

        mMaxHooks = nhooks + (hooksPerPage % nhooks);

        mHookPage = static_cast<byteptr_t>(VirtualAllocEx(GetCurrentProcess(),
                                                          nullptr,
                                                          mMaxHooks * kHookSize,
                                                          MEM_COMMIT | MEM_RESERVE,
                                                          PAGE_EXECUTE_READWRITE));

        if (!mHookPage) {
            mModule = nullptr;
            return;
        }
    }

    bool Initialized() { return !!mModule; }

    void LockHooks()
    {
        if (!mModule) {
            return;
        }

        DWORD op = 0;
        VirtualProtectEx(GetCurrentProcess(),
                         mHookPage,
                         mMaxHooks * kHookSize,
                         PAGE_EXECUTE_READ,
                         &op);

        mModule = nullptr;
    }

    bool AddHook(const char *pname, intptr_t hookDest, void **origFunc)
    {
        if (!mModule) {
            return false;
        }

        auto *pAddr = static_cast<void *>(GetProcAddress(mModule, pname));
        if (!pAddr) {
            fwprintf(stderr, L"GetProcAddress failed.\r\n");
            return false;
        }

        CreateTrampoline(pAddr, hookDest, origFunc);
        if (!*origFunc) {
            fwprintf(stderr, L"CreateTrampoline failed.\r\n");
            return false;
        }

        return true;
    }

protected:
    const static int kPageSize = 4096;
    const static int kHookSize = 128;

    HMODULE mModule = nullptr;
    byteptr_t mHookPage = nullptr;
    int mMaxHooks = 0;
    int mCurHooks = 0;

    void CreateTrampoline(void *origFunction, intptr_t dest, void **outTramp)
    {
        *outTramp = nullptr;

        byteptr_t tramp = FindTrampolineSpace();
        if (!tramp) {
            return;
        }

        byteptr_t origBytes = static_cast<byteptr_t>(origFunction);

        int nBytes = 0;
        int pJmp32 = -1;

#ifdef _M_IX86
        while (nBytes < 5) {
            // Understand some simple instructions that might be found in a
            // prologue; we might need to extend this as necessary.
            //
            // Note!  If we ever need to understand jump instructions, we'll
            // need to rewrite the displacement argument.
            if (origBytes[nBytes] >= 0x88 && origBytes[nBytes] <= 0x8B) {
                // various MOVs
                unsigned char b = origBytes[nBytes + 1];
                if (((b & 0xc0) == 0xc0)
                    || (((b & 0xc0) == 0x00) && ((b & 0x07) != 0x04) && ((b & 0x07) != 0x05))) {
                    // REG=r, R/M=r or REG=r, R/M=[r]
                    nBytes += 2;
                } else if ((b & 0xc0) == 0x40) {
                    if ((b & 0x07) == 0x04) {
                        // REG=r, R/M=[SIB + disp8]
                        nBytes += 4;
                    } else {
                        // REG=r, R/M=[r + disp8]
                        nBytes += 3;
                    }
                } else {
                    // complex MOV, bail
                    return;
                }
            } else if (origBytes[nBytes] == 0xB8) {
                // MOV 0xB8: http://ref.x86asm.net/coder32.html#xB8
                nBytes += 5;
            } else if (origBytes[nBytes] == 0x83) {
                // ADD|ODR|ADC|SBB|AND|SUB|XOR|CMP r/m, imm8
                unsigned char b = origBytes[nBytes + 1];
                if ((b & 0xc0) == 0xc0) {
                    // ADD|ODR|ADC|SBB|AND|SUB|XOR|CMP r, imm8
                    nBytes += 3;
                } else {
                    // bail
                    return;
                }
            } else if (origBytes[nBytes] == 0x68) {
                // PUSH with 4-byte operand
                nBytes += 5;
            } else if ((origBytes[nBytes] & 0xf0) == 0x50) {
                // 1-byte PUSH/POP
                nBytes++;
            } else if (origBytes[nBytes] == 0x6A) {
                // PUSH imm8
                nBytes += 2;
            } else if (origBytes[nBytes] == 0xe9) {
                pJmp32 = nBytes;
                // jmp 32bit offset
                nBytes += 5;
            } else {
                fwprintf(stderr,
                         L"Unknown x86 instruction byte 0x%02x, aborting trampoline.\r\n",
                         origBytes[nBytes]);
                return;
            }
        }
#elif defined(_M_X64)
        byteptr_t directJmpAddr = nullptr;

        while (nBytes < 13) {
            // if found JMP 32bit offset, next bytes must be NOP
            if (pJmp32 >= 0) {
                if (origBytes[nBytes++] != 0x90) {
                    return;
                }

                continue;
            }
            if (origBytes[nBytes] == 0x0f) {
                nBytes++;
                if (origBytes[nBytes] == 0x1f) {
                    // nop (multibyte)
                    nBytes++;
                    if ((origBytes[nBytes] & 0xc0) == 0x40 && (origBytes[nBytes] & 0x7) == 0x04) {
                        nBytes += 3;
                    } else {
                        return;
                    }
                } else if (origBytes[nBytes] == 0x05) {
                    // syscall
                    nBytes++;
                } else {
                    return;
                }
            } else if (origBytes[nBytes] == 0x41) {
                // REX.B
                nBytes++;

                if ((origBytes[nBytes] & 0xf0) == 0x50) {
                    // push/pop with Rx register
                    nBytes++;
                } else if (origBytes[nBytes] >= 0xb8 && origBytes[nBytes] <= 0xbf) {
                    // mov r32, imm32
                    nBytes += 5;
                } else {
                    return;
                }
            } else if (origBytes[nBytes] == 0x45) {
                // REX.R & REX.B
                nBytes++;

                if (origBytes[nBytes] == 0x33) {
                    // xor r32, r32
                    nBytes += 2;
                } else {
                    return;
                }
            } else if ((origBytes[nBytes] & 0xfb) == 0x48) {
                // REX.W | REX.WR
                nBytes++;

                if (origBytes[nBytes] == 0x81 && (origBytes[nBytes + 1] & 0xf8) == 0xe8) {
                    // sub r, dword
                    nBytes += 6;
                } else if (origBytes[nBytes] == 0x83 && (origBytes[nBytes + 1] & 0xf8) == 0xe8) {
                    // sub r, byte
                    nBytes += 3;
                } else if (origBytes[nBytes] == 0x83 && (origBytes[nBytes + 1] & 0xf8) == 0x60) {
                    // and [r+d], imm8
                    nBytes += 5;
                } else if ((origBytes[nBytes] & 0xfd) == 0x89) {
                    // MOV r/m64, r64 | MOV r64, r/m64
                    if ((origBytes[nBytes + 1] & 0xc0) == 0x40) {
                        if ((origBytes[nBytes + 1] & 0x7) == 0x04) {
                            // R/M=[SIB+disp8], REG=r64
                            nBytes += 4;
                        } else {
                            // R/M=[r64+disp8], REG=r64
                            nBytes += 3;
                        }
                    } else if (((origBytes[nBytes + 1] & 0xc0) == 0xc0)
                               || (((origBytes[nBytes + 1] & 0xc0) == 0x00)
                                   && ((origBytes[nBytes + 1] & 0x07) != 0x04)
                                   && ((origBytes[nBytes + 1] & 0x07) != 0x05))) {
                        // REG=r64, R/M=r64 or REG=r64, R/M=[r64]
                        nBytes += 2;
                    } else {
                        // complex MOV
                        return;
                    }
                } else if (origBytes[nBytes] == 0xc7) {
                    // MOV r/m64, imm32
                    if (origBytes[nBytes + 1] == 0x44) {
                        // MOV [r64+disp8], imm32
                        // ModR/W + SIB + disp8 + imm32
                        nBytes += 8;
                    } else {
                        return;
                    }
                } else if (origBytes[nBytes] == 0xff) {
                    pJmp32 = nBytes - 1;
                    // JMP /4
                    if ((origBytes[nBytes + 1] & 0xc0) == 0x0
                        && (origBytes[nBytes + 1] & 0x07) == 0x5) {
                        // [rip+disp32]
                        // convert JMP 32bit offset to JMP 64bit direct
                        directJmpAddr = (byteptr_t)
                                        * ((uint64_t *) (origBytes + nBytes + 6
                                                         + (*((int32_t *) (origBytes + nBytes
                                                                           + 2)))));
                        nBytes += 6;
                    } else {
                        // not support yet!
                        return;
                    }
                } else {
                    // not support yet!
                    return;
                }
            } else if ((origBytes[nBytes] & 0xf0) == 0x50) {
                // 1-byte push/pop
                nBytes++;
            } else if (origBytes[nBytes] == 0x90) {
                // nop
                nBytes++;
            } else if (origBytes[nBytes] == 0xb8) {
                // MOV 0xB8: http://ref.x86asm.net/coder32.html#xB8
                nBytes += 5;
            } else if (origBytes[nBytes] == 0xc3) {
                // ret
                nBytes++;
            } else if (origBytes[nBytes] == 0xe9) {
                pJmp32 = nBytes;
                // convert JMP 32bit offset to JMP 64bit direct
                directJmpAddr = origBytes + pJmp32 + 5 + (*((int32_t *) (origBytes + pJmp32 + 1)));
                // jmp 32bit offset
                nBytes += 5;
            } else if (origBytes[nBytes] == 0xff) {
                nBytes++;
                if ((origBytes[nBytes] & 0xf8) == 0xf0) {
                    // push r64
                    nBytes++;
                } else {
                    return;
                }
            } else {
                return;
            }
        }
#else
#error "Unknown processor type"
#endif

        if (nBytes > 100) {
            fwprintf(stderr, L"Too big!");
            return;
        }

        // We keep the address of the original function in the first bytes of
        // the trampoline buffer
        *((void **) tramp) = origFunction;
        tramp += sizeof(void *);

        memcpy(tramp, origFunction, nBytes);

        // OrigFunction+N, the target of the trampoline
        byteptr_t trampDest = origBytes + nBytes;

#ifdef _M_IX86
        if (pJmp32 >= 0) {
            // Jump directly to the original target of the jump instead of jumping to the
            // original function.
            // Adjust jump target displacement to jump location in the trampoline.
            *((intptr_t *) (tramp + pJmp32 + 1)) += origBytes - tramp;
        } else {
            tramp[nBytes] = 0xE9; // jmp
            *((intptr_t *) (tramp + nBytes + 1)) = (intptr_t) trampDest
                                                   - (intptr_t)(tramp + nBytes
                                                                + 5); // target displacement
        }
#elif defined(_M_X64)
        // If JMP32 opcode found, we don't insert to trampoline jump
        if (pJmp32 >= 0) {
            // mov r11, address
            tramp[pJmp32] = 0x49;
            tramp[pJmp32 + 1] = 0xbb;
            *((intptr_t *) (tramp + pJmp32 + 2)) = reinterpret_cast<intptr_t>(directJmpAddr);

            // jmp r11
            tramp[pJmp32 + 10] = 0x41;
            tramp[pJmp32 + 11] = 0xff;
            tramp[pJmp32 + 12] = 0xe3;
        } else {
            // mov r11, address
            tramp[nBytes] = 0x49;
            tramp[nBytes + 1] = 0xbb;
            *((intptr_t *) (tramp + nBytes + 2)) = reinterpret_cast<intptr_t>(trampDest);

            // jmp r11
            tramp[nBytes + 10] = 0x41;
            tramp[nBytes + 11] = 0xff;
            tramp[nBytes + 12] = 0xe3;
        }
#endif

        // The trampoline is now valid.
        *outTramp = tramp;

        // ensure we can modify the original code
        DWORD op = 0;
        if (!VirtualProtectEx(GetCurrentProcess(),
                              origFunction,
                              nBytes,
                              PAGE_EXECUTE_READWRITE,
                              &op)) {
            fwprintf(stderr, L"VirtualProtectEx failed.\r\n");
            return;
        }

#ifdef _M_IX86
        // now modify the original bytes
        origBytes[0] = 0xE9;                                                // jmp
        *((intptr_t *) (origBytes + 1)) = dest - (intptr_t)(origBytes + 5); // target displacement
#elif defined(_M_X64)
        // mov r11, address
        origBytes[0] = 0x49;
        origBytes[1] = 0xbb;

        *((intptr_t *) (origBytes + 2)) = dest;

        // jmp r11
        origBytes[10] = 0x41;
        origBytes[11] = 0xff;
        origBytes[12] = 0xe3;
#endif

        // restore protection; if this fails we can't really do anything about it
        VirtualProtectEx(GetCurrentProcess(), origFunction, nBytes, op, &op);
    }

    byteptr_t FindTrampolineSpace()
    {
        if (mCurHooks >= mMaxHooks) {
            return 0;
        }

        byteptr_t p = mHookPage + mCurHooks * kHookSize;

        mCurHooks++;

        return p;
    }
};

class WindowsDllInterceptor
{
    WindowsDllNopSpacePatcher mNopSpacePatcher;
    WindowsDllDetourPatcher mDetourPatcher;

    LPCWSTR mModuleName = nullptr;
    int mNHooks = 0;

public:
    explicit WindowsDllInterceptor() {}

    void Init(LPCWSTR moduleName, int nhooks = 0)
    {
        if (mModuleName) {
            return;
        }

        mModuleName = moduleName;
        mNHooks = nhooks;
        mNopSpacePatcher.Init(moduleName);

        // Lazily initialize mDetourPatcher, since it allocates memory and we might
        // not need it.
    }

    void LockHooks()
    {
        if (mDetourPatcher.Initialized()) {
            mDetourPatcher.LockHooks();
        }
    }

    bool AddHook(const char *pname, intptr_t hookDest, void **origFunc)
    {
        // Use a nop space patch if possible, otherwise fall back to a detour.
        // This should be the preferred method for adding hooks.

        if (!mModuleName) {
            return false;
        }

        if (mNopSpacePatcher.AddHook(pname, hookDest, origFunc)) {
            return true;
        }

        return AddDetour(pname, hookDest, origFunc);
    }

    bool AddDetour(const char *pname, intptr_t hookDest, void **origFunc)
    {
        // Generally, code should not call this method directly. Use AddHook unless
        // there is a specific need to avoid nop space patches.

        if (!mModuleName) {
            return false;
        }

        if (!mDetourPatcher.Initialized()) {
            mDetourPatcher.Init(mModuleName, mNHooks);
        }

        return mDetourPatcher.AddHook(pname, hookDest, origFunc);
    }
};
