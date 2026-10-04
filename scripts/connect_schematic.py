import sys
from pathlib import Path

sys.path.append(r'C:\Users\admin\Documents\GitHub\KiCAD-MCP-Server\python')
from commands.pin_locator import PinLocator
from commands.wire_manager import WireManager

sch_path = Path(r'C:/Users/admin/Documents/GitHub/Decor_Esp32/hardware/Keypad_Power_Shield/Keypad_Power_Shield.kicad_sch')
locator = PinLocator()

CONNECTIONS = {
    # Power & Connectors
    "J_MHCD42": { "1": "VIN_5V", "2": "VBAT", "3": "GND", "4": "VOUT_5V", "5": "GND", "6": "MH_KEY" },
    "J_BAT":    { "1": "VBAT", "2": "GND" },
    "J_ESP":    { "1": "+5V", "2": "+3V3", "3": "GND", "4": "KEY_ADC", "5": "CHG_STAT", "6": "BAT_ADC" },
    # Switch & LED
    "SW_PWR":   { "1": "VOUT_5V", "2": "+5V" },
    "R_LED":    { "1": "+5V", "2": "NET_LED_A" },
    "D_PWR":    { "2": "NET_LED_A", "1": "GND" },
    # MH-CD42 Charge detect -> GPIO 21
    "R_CHG1":   { "1": "VIN_5V", "2": "CHG_STAT" },
    "R_CHG2":   { "1": "CHG_STAT", "2": "GND" },
    "C_CHG":    { "1": "CHG_STAT", "2": "GND" },
    # Battery Voltage Monitor -> GPIO 18
    "R_BAT1":   { "1": "VBAT", "2": "BAT_ADC" },
    "R_BAT2":   { "1": "BAT_ADC", "2": "GND" },
    "C_BAT":    { "1": "BAT_ADC", "2": "GND" },
    # 7-Key Resistor Ladder Keypad -> GPIO 3
    "R_PU":     { "1": "+3V3", "2": "KEY_ADC" },
    "C_KEY":    { "1": "KEY_ADC", "2": "GND" },
    "SW_OK":    { "1": "KEY_ADC", "2": "GND" },
    "SW_UP":    { "1": "KEY_ADC", "2": "NET_UP" },
    "R_UP":     { "1": "NET_UP", "2": "GND" },
    "SW_DOWN":  { "1": "KEY_ADC", "2": "NET_DOWN" },
    "R_DOWN":   { "1": "NET_DOWN", "2": "GND" },
    "SW_LEFT":  { "1": "KEY_ADC", "2": "NET_LEFT" },
    "R_LEFT":   { "1": "NET_LEFT", "2": "GND" },
    "SW_RIGHT": { "1": "KEY_ADC", "2": "NET_RIGHT" },
    "R_RIGHT":  { "1": "NET_RIGHT", "2": "GND" },
    "SW_MENU":  { "1": "KEY_ADC", "2": "NET_MENU" },
    "R_MENU":   { "1": "NET_MENU", "2": "GND" },
    "SW_EXIT":  { "1": "KEY_ADC", "2": "NET_EXIT" },
    "R_EXIT":   { "1": "NET_EXIT", "2": "GND" }
}

count = 0
for ref, pins in CONNECTIONS.items():
    all_pins = locator.get_all_symbol_pins(sch_path, ref)
    if not all_pins:
        print(f"Warning: Symbol {ref} not found!")
        continue
    for pin, net in pins.items():
        pos = locator.get_pin_location(sch_path, ref, pin)
        if not pos:
            print(f"Warning: Pin {pin} on {ref} not found! Available: {all_pins.keys()}")
            continue
        # determine orientation based on symbol pin direction or default
        ok = WireManager.add_label(sch_path, net, pos, label_type="label", orientation=0)
        if ok:
            count += 1
            print(f"Connected {ref}.{pin} -> {net} at {pos}")
        else:
            print(f"Failed to connect {ref}.{pin}")

print(f"\nDone! Successfully placed {count} net labels.")

