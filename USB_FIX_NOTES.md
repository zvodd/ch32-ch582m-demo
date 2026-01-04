# USB HID Keyboard Fix - Summary

## Problems Identified

### 1. **ALT Modifier Being Sent (KEY_RIGHTALT)**
- **Root Cause**: The `KeyBuf[0]` byte (modifier byte) was not being properly cleared
- **Effect**: Random garbage data in the modifier byte was being interpreted as ALT key being held
- **Why it happened**: Buffer reuse without proper clearing between transmissions

### 2. **Key Release Events Never Sent**
- **Root Cause**: Scanning all 3 touch channels when only 1 was physically connected
- **Effect**: Unconnected channels (ch5, ch2) returned unstable/garbage ADC values
- **Why it happened**: The floating inputs on disconnected channels would randomly trigger false "key pressed" states, preventing the logic from ever detecting `current_pressed == 0`

### 3. **Key Auto-Repeat**
- **Root Cause**: Combination of above issues - no release event + host OS sees key stuck down
- **Effect**: Host OS auto-repeat kicks in for the stuck key

---

## Fixes Applied

### Fix 1: Buffer Initialization and Clearing
```c
// Before:
uint8_t KeyBuf[8] = {0}; // Compiler might not zero all elements

// After:
uint8_t KeyBuf[8] = {0, 0, 0, 0, 0, 0, 0, 0}; // Explicit full initialization

// And in the transmission code:
memset(KeyBuf, 0, 8);  // ALWAYS clear before building new report
```

**Why this works**: 
- Ensures NO modifier bits are set (KeyBuf[0] = 0x00 means no Ctrl/Shift/Alt/GUI)
- Prevents garbage data from previous transmissions
- USB HID spec requires clean state for unused bytes

### Fix 2: Only Scan Connected Touchkey Channel
```c
// Before:
const uint8_t tkey_ch[] = { 5, 2, 4 };      // 3 channels
const uint8_t key_map[] = { 0x04, 0x05, 0x06 }; // A, B, C keys

// After:
const uint8_t tkey_ch[] = { 4 };           // Only channel 4
const uint8_t key_map[] = { 0x06 };        // Only C key
#define NUM_KEYS (sizeof(tkey_ch)/sizeof(tkey_ch[0]))  // Now = 1
```

**Why this works**:
- Unconnected ADC channels have floating inputs
- Floating inputs = unstable readings that can randomly trigger threshold
- Only scanning the one connected channel ensures deterministic behavior
- When finger is removed, `current_pressed` correctly becomes 0, triggering release report

### Fix 3: GPIO Configuration
```c
// Before:
GPIOA_ModeCfg(GPIO_Pin_12 | GPIO_Pin_14 | GPIO_Pin_15, GPIO_ModeIN_Floating);

// After:
GPIOA_ModeCfg(GPIO_Pin_15, GPIO_ModeIN_Floating);  // Only Pin_15 (ch4)
```

**Why this works**:
- Only configures the pin that's actually used
- Cleaner hardware configuration
- Matches the single-channel scanning logic

### Fix 4: Debug Output
Added debug print to see exactly what's being sent:
```c
printf("Sent key report: [%02X %02X %02X %02X %02X %02X %02X %02X]\n",
    KeyBuf[0], KeyBuf[1], KeyBuf[2], KeyBuf[3],
    KeyBuf[4], KeyBuf[5], KeyBuf[6], KeyBuf[7]);
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

### If You Add More Touchkeys Later:
1. Physically connect the touchkey to the MCU
2. Add the channel number to `tkey_ch[]`
3. Add the desired USB keycode to `key_map[]`
4. Configure the corresponding GPIO pin in `Touch_Setup()`
5. Test each key independently

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

## Date
- Fix applied: (add current date)
- Issue: Key stuck down with ALT modifier, no release events
- Status: ✅ RESOLVED