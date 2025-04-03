import platform
import serial
import serial.tools.list_ports
import os
import time
import sys
import threading

# --- ANSI Color Codes ---
class bcolors:
    HEADER = '\033[95m'
    OKBLUE = '\033[94m'
    OKCYAN = '\033[96m'
    OKGREEN = '\033[92m'
    WARNING = '\033[93m'
    FAIL = '\033[91m'
    ENDC = '\033[0m'
    BOLD = '\033[1m'
    UNDERLINE = '\033[4m'

# --- Helper Functions ---
def find_first_file(directory, extension=".bin"):
    """Finds the first file with the given extension in a directory."""
    for root, dirs, files in os.walk(directory):
        for file in files:
            if file.endswith(extension):
                return os.path.join(root, file)
    return None

def run_command(command):
    """Runs a shell command and prints its output."""
    print(f"{bcolors.BOLD}Running: {command}{bcolors.ENDC}")
    process = os.popen(command)
    output = process.read()
    print(output)
    status = process.close()
    if status:
        exit_code = os.waitstatus_to_exitcode(status)
        if exit_code != 0:
            print(f"{bcolors.FAIL}Command failed with exit code {exit_code}{bcolors.ENDC}")
            exit(exit_code)
    return output

def serial_reader(ser_instance):
    """Continuously reads from the serial port and prints lines."""
    try:
        while True:
            if ser_instance.in_waiting > 0:
                try:
                    line = ser_instance.readline().decode('utf-8', errors='replace').rstrip()
                    print(line)
                except serial.SerialException as e:
                    print(f"{bcolors.FAIL}Serial error: {e}{bcolors.ENDC}")
                    break
                except UnicodeDecodeError as e:
                    print(f"{bcolors.WARNING}Decode error: {e}{bcolors.ENDC}")
                    # Optionally read the raw bytes if decoding fails
                    # raw_data = ser_instance.read(ser_instance.in_waiting)
                    # print(f"Raw data: {raw_data}")
            time.sleep(0.01) # Small delay to prevent busy-waiting
    except Exception as e:
        print(f"{bcolors.FAIL}Error in reader thread: {e}{bcolors.ENDC}")


# --- Main Script Logic ---

# 1. Build Project
print(bcolors.OKBLUE + "--- Building Project ---" + bcolors.ENDC)
if len(sys.argv) < 2:
    print(f"{bcolors.FAIL}Error: Build target not specified.{bcolors.ENDC}")
    print("Usage: python dfu.py <target_name>")
    exit(1)

target = sys.argv[1]
build_dir = "cmake-build-debug" # Or your actual build directory

run_command(f"cmake -B {build_dir}/ -DCMAKE_BUILD_TYPE=PRODUCTION")
run_command(f"cmake --build {build_dir}/ --config PRODUCTION --target {target} -j 8")

# 2. Find Binary File
print(bcolors.OKCYAN + f"--- Searching for .bin file in '{build_dir}' ---" + bcolors.ENDC)
binpath = find_first_file(build_dir)

if not binpath:
    print(f"{bcolors.FAIL}No binary file (.bin) found in '{build_dir}'. Make sure the build was successful.{bcolors.ENDC}")
    exit(-1)

print(bcolors.OKGREEN + f"Found binary file: {binpath}\n" + bcolors.ENDC)

# 3. Find Device Serial Port
print(bcolors.OKCYAN + "--- Finding Device Serial Port ---" + bcolors.ENDC)
ports = serial.tools.list_ports.comports()
serial_port_name = None
target_desc = "lhre"  # Part of the description to look for

if(len(sys.argv) > 2):
    target_desc = sys.argv[2]

target_hwid = "0483:5740" # VID:PID to look for

for port in sorted(ports):
    print(f"Checking port: {port.device} - {port.description} [{port.hwid}]")
    # Check if description or hwid contains the target identifiers
    if (target_desc and target_desc in port.description.lower()) or \
            (target_hwid and target_hwid in port.hwid.lower()):
        print(bcolors.OKGREEN + f"Found target device at port {port.device}" + bcolors.ENDC)
        serial_port_name = port.device
        break # Stop after finding the first match

if not serial_port_name:
    print(f"{bcolors.WARNING}Target device not found automatically. Proceeding directly to DFU.{bcolors.ENDC}")
else:
    # 4. Send Update Command (Optional - only if device found)
    print(bcolors.OKBLUE + "--- Sending 'update' command ---" + bcolors.ENDC)
    try:
        with serial.Serial(serial_port_name, baudrate=115200, timeout=1) as ser:
            ser.write("update\n\r".encode('utf-8')) # Send update command
            print("Command sent. Waiting for device to potentially reset...")
        time.sleep(2) # Give time for the device to potentially reset to DFU mode
    except serial.SerialException as e:
        print(f"{bcolors.FAIL}Could not open or write to {serial_port_name}: {e}{bcolors.ENDC}")
        print("Proceeding to DFU update anyway.")
    except Exception as e:
        print(f"{bcolors.FAIL}An unexpected error occurred: {e}{bcolors.ENDC}")
        print("Proceeding to DFU update anyway.")


# 5. Update Device via DFU
print(bcolors.OKCYAN + "\n--- Updating Device via DFU ---" + bcolors.ENDC)
# Note: Ensure dfu-util is installed and in your system's PATH
dfu_command = f"dfu-util -a 0 -d 0483:df11 --dfuse-address 0x08000000:leave -D \"{binpath}\""
run_command(dfu_command)

# The ':leave' suffix in the command above combines the download and reset steps.
# If your dfu-util version doesn't support :leave or you prefer separate steps:
# print(bcolors.OKCYAN + "--- Resetting device ---" + bcolors.ENDC)
# run_command("dfu-util -a 0 -d 0483:df11 -e") # -e performs the reset/leave action

print(f"{bcolors.OKGREEN}DFU Update potentially complete.{bcolors.ENDC}")
print("Waiting for device to re-enumerate...")
time.sleep(5) # Increase delay to allow device to reset and reappear as serial

# 6. Re-Find Serial Port (Device might have a new port name after reset)
print(bcolors.OKCYAN + "\n--- Finding Serial Port Post-Update ---" + bcolors.ENDC)
ports = serial.tools.list_ports.comports()
serial_port_name_after_dfu = None
retries = 5
found = False
for i in range(retries):
    ports = serial.tools.list_ports.comports()
    for port in sorted(ports):
        # Check if description or hwid contains the target identifiers
        if (target_desc and target_desc in port.description.lower()) or \
                (target_hwid and target_hwid in port.hwid.lower()):
            print(bcolors.OKGREEN + f"Found device post-update at port {port.device}" + bcolors.ENDC)
            serial_port_name_after_dfu = port.device
            found = True
            break # Stop after finding the first match
    if found:
        break
    print(f"Device not found, retrying ({i+1}/{retries})...")
    time.sleep(2)


# 7. Connect to Serial Monitor
if serial_port_name_after_dfu:
    print(bcolors.OKBLUE + f"--- Opening Serial Monitor on {serial_port_name_after_dfu} ---" + bcolors.ENDC)
    print(f"{bcolors.WARNING}Type your commands and press Enter. Press Ctrl+C to exit.{bcolors.ENDC}")
    try:
        # Use a context manager for the serial port
        with serial.Serial(serial_port_name_after_dfu, baudrate=115200, timeout=0.1) as ser:
            # Start the reader thread
            reader = threading.Thread(target=serial_reader, args=(ser,), daemon=True)
            reader.start()

            # Main thread handles writing
            while True:
                try:
                    # Get input from the user
                    command = input()
                    # Send command followed by newline (common for microcontrollers)
                    ser.write((command + '\n').encode('utf-8'))
                except EOFError: # Handle Ctrl+D
                    print(f"\n{bcolors.WARNING}EOF detected, exiting.{bcolors.ENDC}")
                    break
                except serial.SerialException as e:
                    print(f"{bcolors.FAIL}Serial write error: {e}{bcolors.ENDC}")
                    break # Exit if writing fails

    except serial.SerialException as e:
        print(f"{bcolors.FAIL}Failed to open serial port {serial_port_name_after_dfu}: {e}{bcolors.ENDC}")
    except KeyboardInterrupt:
        print(f"\n{bcolors.WARNING}Ctrl+C detected. Exiting serial monitor.{bcolors.ENDC}")
    except Exception as e:
        print(f"{bcolors.FAIL}An unexpected error occurred in serial monitor: {e}{bcolors.ENDC}")
    finally:
        print(bcolors.OKBLUE + "--- Serial Monitor Closed ---" + bcolors.ENDC)

else:
    print(f"{bcolors.FAIL}Could not find the serial port after DFU update. Cannot open serial monitor.{bcolors.ENDC}")

print(f"{bcolors.OKGREEN}--- Script Finished ---{bcolors.ENDC}")

