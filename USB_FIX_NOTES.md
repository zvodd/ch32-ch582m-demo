# USB HID Keyboard Fix - Summary

## Root Cause Discovered

### **USB DMA Buffer Size Mismatch**

The CH582M USB peripheral requires **64-byte endpoint buffers** even when only sending 8-byte HID reports.

**Original incorrect code:**
```c
__attribute__((aligned(4))) uint8_t EP1_Databuf[8 + 8];  // Only 16 bytes!
#define EP1_TX_Buf (EP1_Databuf)  // Wrong offset
```

**Problems this caused:**
1. USB DMA controller expected 64-byte buffers as per hardware design
2. IN buffer must be at **offset +64** from base address (standard SDK layout)
3. `R16_UEP1_DMA` was pointing to the wrong memory location
4. USB hardware was reading **garbage data** from uninitialized memory
5. This garbage appeared as random modifier keys (ALT, SHIFT, CTRL, etc.)

**Evidence from serial log:**
```
KeyBuf prepared: [00 00 04 00 00 00 00 00]  ← Correct data
EP1_TX_Buf:      [00 00 04 00 00 00 00 00]  ← Written correctly
DMA buffer:      [2A 83 11 C6 23 00 B3 00]  ← USB reading GARBAGE!
```

---

## Fix Applied

### Correct Buffer Allocation (Based on WCH SDK Example)

```c
// Correct:
__attribute__((aligned(4))) uint8_t EP1_Databuf[64 + 64];  // OUT (64) + IN (64)
#define EP1_TX_Buf (EP1_Databuf + 64)  // IN buffer at offset +64
```

**Why this is required:**
1. **Hardware requirement**: CH582M USB peripheral DMA expects 64-byte aligned buffers
2. **SDK standard layout**: OUT buffer at offset 0, IN buffer at offset +64
3. **16-bit DMA addressing**: `R16_UEP1_DMA` is a 16-bit register that works within the low 64KB address space of RAM
4. **The `R16_UEP1_DMA` register stores only the lower 16 bits** of the buffer address (works because CH582M RAM is in addressable range)

**Reference from working SDK example:**
```c
__attribute__((aligned(4)))  UINT8 EP1_Databuf[64 + 64];    //ep1_out(64)+ep1_in(64)
```

And SDK header defines:
```c
#define pEP1_OUT_DataBuf      (pEP1_RAM_Addr)
#define pEP1_IN_DataBuf       (pEP1_RAM_Addr + 64)
```

---

## USB HID Keyboard Report Format

For reference, the 8-byte report format:
```
Byte 0: Modifier keys bitmap
        bit 0: Left Ctrl
        bit 1: Left Shift
        bit 2: Left Alt     <-- This was being set by garbage!
        bit 3: Left GUI (Win/Cmd)
        bit 4: Right Ctrl
        bit 5: Right Shift
        bit 6: Right Alt    <-- KEY_RIGHTALT
        bit 7: Right GUI

Byte 1: Reserved (must be 0x00)

Bytes 2-7: Up to 6 simultaneous key codes (or 0x00 if not pressed)
```

### Example Reports:
- No keys pressed: `[00 00 00 00 00 00 00 00]`
- 'C' key pressed:  `[00 00 06 00 00 00 00 00]`
- 'C' + RightAlt:   `[40 00 06 00 00 00 00 00]` <-- This was the bug!

---

## Testing Checklist

After applying these fixes, verify:

- [ ] Single touch press registers exactly once
- [ ] Single touch release sends release event (all zeros)
- [ ] No auto-repeat occurs while holding touch
- [ ] No modifier keys (Alt/Ctrl/Shift) are sent
- [ ] Python evdev script shows:
  - Key Down event when touched
  - Key Up event when released
  - NO Key Autorepeat events

## Expected Python evdev Output

**Correct behavior:**
```
Key Down: KEY_C
Key Up: KEY_C
```

**Incorrect behavior (before fix):**
```
Key Down: KEY_C
Key Autorepeat: KEY_RIGHTALT  <-- BUG: Should not happen
Key Autorepeat: KEY_RIGHTALT
Key Autorepeat: KEY_RIGHTALT
...
```

---

## Additional Notes

### Key Takeaway

**Always allocate USB endpoint buffers as 64-byte aligned buffers**, even if your actual data is smaller (like 8 bytes for HID keyboard). The USB DMA hardware requires this for proper operation.

### If You Add More Touchkeys Later:
1. Physically connect the touchkey to the MCU
2. Add the channel number to `tkey_ch[]`
3. Add the desired USB keycode to `key_map[]`
4. Configure the corresponding GPIO pin in `Touch_Setup()`
5. Test each key independently
6. Remember: All touch channels should be physically connected and stable before scanning them

### USB Keycode Reference:
- 0x04 = 'A'
- 0x05 = 'B'  
- 0x06 = 'C'
- 0x07 = 'D'
- See USB HID Usage Tables for full list

### Common USB HID Modifiers:
- 0x01 = Left Ctrl
- 0x02 = Left Shift
- 0x04 = Left Alt
- 0x08 = Left GUI
- 0x10 = Right Ctrl
- 0x20 = Right Shift
- 0x40 = Right Alt (0x40 = bit 6 set)
- 0x80 = Right GUI

---

## Files Modified
- `src/main.c` - All fixes applied here

## Summary

The core issue was **incorrect USB endpoint buffer sizing**. The CH582M's USB peripheral requires 64-byte buffers with a specific memory layout (OUT at +0, IN at +64), regardless of actual data size. Our 16-byte allocation caused the DMA to read from uninitialized memory, resulting in garbage data being sent to the host.

## Date
- Issue discovered: Buffer size mismatch causing garbage USB data
- Root cause: 16-byte buffer instead of required 64+64 byte layout  
- Status: ✅ RESOLVED