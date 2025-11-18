# DMOD Compatibility Issue - DIF Signature Stringification Bug

## Problem Description

DMVFS tests fail because the VFS cannot find mounted file system modules. The root cause is a C preprocessor macro bug in DMOD that prevents proper DIF (DMOD Interface) signature generation.

### Symptom
When mounting a file system module in DMVFS, the following error occurs:
```
[WARN] File system 'testfs' not found
[ERROR] Cannot mount file system 'testfs': Not found
```

### Root Cause

The `DMOD_MAKE_DIF_SIGNATURE` macro in `dmod/inc/dmod_defs.h` uses direct stringification (`#VERSION`) which does not expand nested macros before stringification.

When `DMOD_MAKE_VERSION(1.0, 1.0)` is passed as the VERSION parameter, it gets stringified as the literal text `"DMOD_MAKE_VERSION(1.0,1.0)"` instead of being expanded to `"1.0/1.0"` first.

This causes DIF signatures to be:
- **Generated (incorrect)**: `DDIF_fopen@dmfsi:DMOD_MAKE_VERSION(1.0,1.0)`
- **Expected (correct)**: `DDIF_fopen@dmfsi:1.0/1.0`

Since the signatures don't match, `Dmod_GetNextDifModule()` cannot find modules that implement the DIF interface.

## Technical Details

### Affected Macros
In `dmod/inc/dmod_defs.h` (lines ~102-105):
```c
#define DMOD_MAKE_SIGNATURE( MODULE, VERSION, NAME )		DMOD_SIGNATURE_PREFIX #NAME "@" #MODULE ":" #VERSION DMOD_SIGNATURE_SUFFIX
#define DMOD_MAKE_MAL_SIGNATURE( MODULE, VERSION, NAME )	DMOD_MAL_SIGNATURE_PREFIX #NAME "@" #MODULE ":" #VERSION DMOD_SIGNATURE_SUFFIX
#define DMOD_MAKE_DIF_SIGNATURE( MODULE, VERSION, NAME )	DMOD_DIF_SIGNATURE_PREFIX #NAME "@" #MODULE ":" #VERSION DMOD_SIGNATURE_SUFFIX
```

### C Preprocessor Issue
The `#` operator (stringification) converts its argument to a string literal WITHOUT expanding macros first. This is standard C preprocessor behavior.

To expand macros before stringification, an extra level of macro indirection is required.

## Solution

Add helper macros for proper macro expansion before stringification:

```c
// Helper macros for stringification with macro expansion
#define DMOD_STRINGIFY(x) #x
#define DMOD_STRINGIFY_EXPANDED(x) DMOD_STRINGIFY(x)
```

Then update the signature macros to use `DMOD_STRINGIFY_EXPANDED(VERSION)` instead of `#VERSION`:

```c
#define DMOD_MAKE_SIGNATURE( MODULE, VERSION, NAME )		DMOD_SIGNATURE_PREFIX #NAME "@" #MODULE ":" DMOD_STRINGIFY_EXPANDED(VERSION) DMOD_SIGNATURE_SUFFIX
#define DMOD_MAKE_MAL_SIGNATURE( MODULE, VERSION, NAME )	DMOD_MAL_SIGNATURE_PREFIX #NAME "@" #MODULE ":" DMOD_STRINGIFY_EXPANDED(VERSION) DMOD_SIGNATURE_SUFFIX
#define DMOD_MAKE_DIF_SIGNATURE( MODULE, VERSION, NAME )	DMOD_DIF_SIGNATURE_PREFIX #NAME "@" #MODULE ":" DMOD_STRINGIFY_EXPANDED(VERSION) DMOD_SIGNATURE_SUFFIX
```

## Verification

After applying the fix and rebuilding:

1. **Before fix**: `strings testfs.dmf | grep "_fopen@dmfsi"` shows:
   ```
   _fopen@dmfsi:1.0/1.0
   _fopen@dmfsi:DMOD_MAKE_VERSION(1.0,1.0)  <-- INCORRECT
   ```

2. **After fix**: `strings testfs.dmf | grep "_fopen@dmfsi"` shows:
   ```
   _fopen@dmfsi:1.0/1.0  <-- CORRECT
   ```

3. **Tests pass**: All DMVFS tests pass successfully after the fix.

## Impact

This bug affects:
- Any module using DIF interfaces (DMOD Interface Functions)
- Any system trying to find and connect to DIF-based modules
- Specifically affects DMVFS mounting file systems that use DMFSI interface

## Patch Location

**Repository**: https://github.com/choco-technologies/dmod
**Branch**: develop
**File**: `inc/dmod_defs.h`
**Lines**: Around 97-106

See attached patch file for the complete fix.
