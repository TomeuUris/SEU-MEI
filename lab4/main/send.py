import os
import can
import time
import sys
import select
import tty
import termios
import threading

# --- CAN Protocol Configuration (Based on ESP32 C code) ---
# Expected identifiers for ESP32 command messages
PC1_CAN_ID = 0x101  # Cat Command ID
PC2_CAN_ID = 0x102  # Mouse Command ID
CATCH_MSG_ID = 0x200  # "Game Over" message ID sent by ESP32

# Data code expected by ESP32 (data[0])
CMD_UP = 0
CMD_DOWN = 1
CMD_LEFT = 2
CMD_RIGHT = 3
# --------------------------------------------------------------------

# Check system name
print(f"Operating System: {os.name}")

# Select Player (Cat or Mouse)
print("\n=== CAT AND MOUSE GAME (CAN Protocol v1.0) ===")
print("Choose your player:")
print(f"1 - Cat - Command ID: 0x{PC1_CAN_ID:03X}")
print(f"2 - Mouse - Command ID: 0x{PC2_CAN_ID:03X}")

while True:
    choice = input("Choose (1 or 2): ").strip()
    if choice == '1':
        player = "Cat"
        command_id = PC1_CAN_ID # 0x101
        break
    elif choice == '2':
        player = "Mouse"
        command_id = PC2_CAN_ID # 0x102
        break
    else:
        print("Invalid option. Choose 1 or 2.")

print(f"\n✓ You chose: {player} (Command ID: 0x{command_id:03X})")

# For macOS with Innomaker USB2CAN adapter
print("\nLooking for USB2CAN adapter...")
print("Configuring CAN interface at 500 kbps...")

# Try to use gs_usb interface (for Innomaker USB2CAN)
try:
    can1 = can.interface.Bus(
        interface='gs_usb',
        channel=0,
        bitrate=500000
    )
    print("✓ CAN bus initialized successfully with gs_usb (USB2CAN adapter)!")
except Exception as e:
    print(f"✗ gs_usb interface failed: {e}")
    print("\nTroubleshooting:")
    print("1. Make sure the Innomaker USB2CAN adapter is plugged in")
    print("2. Close the Innomaker application if it's running")
    print("3. Try unplugging and replugging the USB2CAN adapter")
    print("4. If the problem persists, try: sudo python3 send.py")
    sys.exit(1)

# Message definitions mapping keyboard keys to the single command ID and data code
messages = {
    # All use the same command_id (0x101 or 0x102). Commands are differentiated by data[0].
    'w': can.Message(arbitration_id=command_id, data=[CMD_UP, 0, 0, 0, 0, 0, 0, 0], is_extended_id=False),    # W -> 0 (UP)
    'a': can.Message(arbitration_id=command_id, data=[CMD_LEFT, 0, 0, 0, 0, 0, 0, 0], is_extended_id=False),  # A -> 2 (LEFT)
    's': can.Message(arbitration_id=command_id, data=[CMD_DOWN, 0, 0, 0, 0, 0, 0, 0], is_extended_id=False),  # S -> 1 (DOWN)
    'd': can.Message(arbitration_id=command_id, data=[CMD_RIGHT, 0, 0, 0, 0, 0, 0, 0], is_extended_id=False), # D -> 3 (RIGHT)
}

# Flag to control the receiver thread and game state
running = True

def receive_messages():
    """Background thread to receive CAN messages"""
    global running
    print("[Receiver] Starting receive thread...\n")
    
    while running:
        try:
            # Check for messages with a short timeout
            msg = can1.recv(timeout=0.5)
            
            if msg is not None:
                
                # Check for Game Over message from ESP32 (ID 0x200, Data 0x01)
                if msg.arbitration_id == CATCH_MSG_ID and len(msg.data) >= 1 and msg.data[0] == 0x01:
                    print("\r\n=======================================================")
                    print("!!! GAME OVER: CAT CATCHES MOUSE !!!")
                    print("=======================================================\n")
                    running = False # Stop the main loop
                    break # Exit the receiver loop
                
                # Print generic received message
                data_hex = ' '.join([f'{b:02X}' for b in msg.data])
                timestamp = time.strftime('%H:%M:%S')
                print(f"\r[{timestamp}] ← CAN RX - ID: 0x{msg.arbitration_id:03X}, Data: [{data_hex}]")
                print(f"[{player}] Press W/A/S/D to move, Q to quit: ", end='', flush=True)
                
        except Exception as e:
            if running:
                print(f"\r[Receiver] Error: {e}")
    
    print("[Receiver] Thread stopped")

def get_key():
    """Get a single keypress without requiring Enter"""
    # This setup is necessary for non-blocking single-key input on Unix-like systems
    fd = sys.stdin.fileno()
    old_settings = termios.tcgetattr(fd)
    try:
        tty.setraw(sys.stdin.fileno())
        if select.select([sys.stdin], [], [], 0.1)[0]:
            ch = sys.stdin.read(1)
            return ch
        return None
    finally:
        termios.tcsetattr(fd, termios.TCSADRAIN, old_settings)

print("\n=== CAN Bus Game Controller ===")
print(f"Player: {player}")
print(f"Command ID: 0x{command_id:03X}")
print("Bitrate: 500 kbps")
print("\nControls:")
print("  W (Data: 0) - Move Up")
print("  A (Data: 2) - Move Left")
print("  S (Data: 1) - Move Down")
print("  D (Data: 3) - Move Right")
print("  Q - Quit\n")

# Start the receiver thread
receiver_thread = threading.Thread(target=receive_messages, daemon=True)
receiver_thread.start()
time.sleep(0.5)

print(f"[{player}] Press W/A/S/D to move, Q to quit: ", end='', flush=True)

try:
    while running:
        key = get_key()
        
        if key:
            key_lower = key.lower()
            
            if key_lower == 'q':
                print("\n\nQuitting...")
                running = False
                break
            
            if key_lower in messages:
                msg = messages[key_lower]
                can1.send(msg)
                timestamp = time.strftime('%H:%M:%S')
                data_hex = ' '.join([f'{b:02X}' for b in msg.data])
                
                # Determine direction string for printing
                if msg.data[0] == CMD_UP: direction = 'UP'
                elif msg.data[0] == CMD_LEFT: direction = 'LEFT'
                elif msg.data[0] == CMD_DOWN: direction = 'DOWN'
                elif msg.data[0] == CMD_RIGHT: direction = 'RIGHT'
                else: direction = 'UNKNOWN'

                print(f"\r[{timestamp}] → [{player}] {direction}")
                print(f"[{player}] Press W/A/S/D to move, Q to quit: ", end='', flush=True)
        
        time.sleep(0.01)

except KeyboardInterrupt:
    print("\n\nInterrupted by user")
    running = False
finally:
    # Ensure all threads and resources are closed cleanly
    running = False
    if receiver_thread.is_alive():
        receiver_thread.join(timeout=1.0)
    
    # Check if can1 exists and is not None before calling shutdown
    if 'can1' in locals() and can1 is not None:
        can1.shutdown()
        print("CAN interface closed")
        
    print(f"\nGame Over! Thanks for playing as {player}!")