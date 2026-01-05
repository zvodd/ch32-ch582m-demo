# CH582M USB Buffer Fix - Quick Reference

## The Problem

USB HID keyboard was sending garbage data (random modifier keys like ALT, SHIFT, CTRL) instead of the correct key presses.

**Serial log showed:**
```
KeyBuf prepared: [00 00 04 00 00 00 00 00]  ← We prepared correct data
EP1_TX_Buf:      [00 00 04 00 00 00 00 00]  ← We wrote it correctly
DMA buffer:      [2A 83 11 C6 23 00 B3 00]  ← USB hardware read GARBAGE!
```

## Root Cause

**Buffer was too small and had wrong layout!**

### Wrong Code (BEFORE):
```c
__attribute__((aligned(4))) uint8_t EP1_Databuf[8 + 8];  // Only 16 bytes - WRONG!
#define EP1_TX_Buf (EP1_Databuf)  // Wrong offset - WRONG!
```

### Why This Failed:
1. CH582M USB peripheral **requires 64-byte buffers** (hardware design)
2. IN buffer **must be at offset +64** from base address
3. Even though we only send 8 bytes, the buffer must be 64+64=128 bytes total
4. With wrong size/offset, `R16_UEP1_DMA` pointed to wrong memory
5. USB DMA read uninitialized memory → garbage data → random keys on host

## The Fix

### Correct Code (AFTER):
```c
__attribute__((aligned(4))) uint8_t EP1_Databuf[64 + 64];  // 128 bytes: OUT + IN
#define EP1_TX_Buf (EP1_Databuf + 64)  // IN buffer at offset +64
```

### Why This Works:
- Matches WCH SDK standard buffer layout
- OUT buffer: bytes 0-63
- IN buffer: bytes 64-127
- USB DMA now reads from correct location
- Clean data → correct key presses

## Key Lesson

**Always use 64-byte aligned buffers for CH582M USB endpoints**, even if your actual payload is smaller!

This is a hardware requirement, not optional.

## Reference: WCH SDK Example

From working example code:
```c
__attribute__((aligned(4))) UINT8 EP1_Databuf[64 + 64];  // ep1_out(64)+ep1_in(64)
```

SDK header macros:
```c
#define pEP1_OUT_DataBuf      (pEP1_RAM_Addr)      // Offset 0
#define pEP1_IN_DataBuf       (pEP1_RAM_Addr + 64)  // Offset +64
```

## Memory Layout Diagram

```
EP1_Databuf memory layout:
┌──────────────────────────────────┬──────────────────────────────────┐
│     OUT Buffer (64 bytes)        │     IN Buffer (64 bytes)         │
│    EP1_Databuf[0..63]            │    EP1_Databuf[64..127]          │
│  (For receiving from host)       │  (For transmitting to host)      │
└──────────────────────────────────┴──────────────────────────────────┘
 ^                                  ^
 pEP1_RAM_Addr                      EP1_TX_Buf (pEP1_RAM_Addr + 64)
 
R16_UEP1_DMA points here ──────────┘
```

## Status

✅ **FIXED** - USB now sends correct keyboard data with no garbage modifiers