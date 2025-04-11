import json
import argparse
import re
import os

# --- Configuration ---
DEFAULT_INPUT_FILENAME = "can_packets.json"
DEFAULT_OUTPUT_FILENAME = "night_can_ids.h"


def to_macro_name(name):
    """Converts a readable name to an uppercase C macro name."""
    # Replace non-alphanumeric characters (except _) with underscore
    s1 = re.sub(r"[^\w]+", "_", name)
    # Remove leading/trailing underscores that might result
    s1 = s1.strip("_")
    # Convert to uppercase
    return s1.upper()


def generate_header(json_data, input_filename, output_filename=DEFAULT_OUTPUT_FILENAME):
    """Generates the C header file content from the parsed JSON data."""

    header_lines = []
    header_guard = os.path.basename(output_filename).upper().replace(".", "_")

    # --- Header Start ---
    header_lines.append(f"#ifndef {header_guard}")
    header_lines.append(f"#define {header_guard}")
    header_lines.append("")
    header_lines.append("// Auto-generated CAN ID header file")
    header_lines.append(f"// Generated from: {input_filename}")
    header_lines.append("// DO NOT EDIT MANUALLY")
    header_lines.append("")
    header_lines.append("#include <stdint.h> // For fixed-width integer types")
    header_lines.append("")

    # --- Type Mapping ---
    c_type_map = {
        "float": "float",
        "uint8": "uint8_t",
        "int8": "int8_t",
        "uint16": "uint16_t",
        "int16": "int16_t",
        # Add more types if needed (e.g., uint32, int32, double)
        "uint32": "uint32_t",
        "int32": "int32_t",
        "double": "double",
    }

    # --- Process Each Packet ---
    packet_index = 0  # For better error reporting if packet_name is missing
    for packet in json_data:
        packet_index += 1
        packet_name_for_error = (
            f"at index {packet_index - 1}"  # Default error identifier
        )
        try:
            packet_name = packet["packet_name"]
            packet_name_for_error = f"'{packet_name}'"  # Use name if available
            packet_id = packet["packet_id"]
            data_length = packet["data_length"]
            quantity = packet.get("quantity", 1)  # Default quantity if missing
            bytes_data = packet.get("bytes", [])  # Default to empty list

            # --- Handle frequency_ms (allowing for null/None) ---
            freq_ms_value = packet.get(
                "frequency_ms"
            )  # Get value, could be None or missing
            frequency_ms_int = 0  # Default frequency value

            if freq_ms_value is not None:
                try:
                    # Attempt to convert to float first (as input might be float) then to int
                    frequency_ms_int = int(float(freq_ms_value))
                except (ValueError, TypeError):
                    print(
                        f"Warning: Invalid or non-numeric value '{freq_ms_value}' for 'frequency_ms' in packet {packet_name_for_error}. Using default 0."
                    )
                    # frequency_ms_int remains 0
            else:
                # frequency_ms was null or missing
                print(
                    f"Warning: 'frequency_ms' is null or missing for packet {packet_name_for_error}. Using default frequency 0."
                )
                # frequency_ms_int remains 0

            # Note: The 'frequency' field from JSON is not used for the _FREQ define,
            # based on the requirement to use frequency_ms. No specific handling needed for it here.

            packet_macro_base = to_macro_name(packet_name)

            header_lines.append(f"// Packet: {packet_name}")
            header_lines.append(f"#define {packet_macro_base}_ID {packet_id}")
            header_lines.append(f"#define {packet_macro_base}_DLC {data_length}")
            header_lines.append(
                f"#define {packet_macro_base}_FREQ {frequency_ms_int}"
            )  # Use the processed integer value
            header_lines.append(f"#define {packet_macro_base}_QUANTITY {quantity}")
            header_lines.append("")

            # --- Process Each Byte/Signal in the Packet ---
            signal_index = 0
            for byte_info in bytes_data:
                signal_index += 1
                signal_name_for_error = (
                    f"signal index {signal_index - 1} in packet {packet_name_for_error}"
                )
                try:
                    signal_name = byte_info["name"]
                    signal_name_for_error = f"'{signal_name}' in packet {packet_name_for_error}"  # Use name if available
                    start_byte = byte_info["start_byte"]
                    length = byte_info["length"]
                    conv_type = byte_info["conv_type"]
                    precision = byte_info["precision"]

                    signal_macro_name = to_macro_name(signal_name)
                    signal_macro_base = f"{packet_macro_base}_{signal_macro_name}"

                    # Get the C type, default to 'unknown_type_t' if not found
                    c_type = c_type_map.get(
                        conv_type, f"unknown_type_t /* {conv_type} */"
                    )
                    if conv_type not in c_type_map:
                        print(
                            f"Warning: Unknown conversion type '{conv_type}' for signal {signal_name_for_error}. Using fallback type."
                        )

                    header_lines.append(
                        f"#define {signal_macro_base}_BYTE {start_byte}"
                    )
                    # Add 'f' suffix for float literals in C
                    header_lines.append(
                        f"#define {signal_macro_base}_PREC {precision}f"
                    )
                    header_lines.append(f"#define {signal_macro_base}_LENGTH {length}")
                    header_lines.append(f"#define {signal_macro_base}_TYPE {c_type}")
                    header_lines.append("")  # Add a blank line for readability

                except KeyError as e:
                    print(
                        f"Warning: Missing key {e} in signal definition {signal_name_for_error}. Skipping signal."
                    )
                except (
                    Exception
                ) as e:  # Catch other potential errors within signal processing
                    print(
                        f"Error processing signal {signal_name_for_error}: {e}. Skipping signal."
                    )

            header_lines.append("// End Packet: " + packet_name)
            header_lines.append("")  # Blank line after packet definition

        except KeyError as e:
            print(
                f"Warning: Missing required key {e} in top-level packet definition {packet_name_for_error}. Skipping packet."
            )
        except Exception as e:  # Catch other potential errors during packet processing
            print(
                f"Error processing packet {packet_name_for_error}: {e}. Skipping packet."
            )

    # --- Header End ---
    header_lines.append(f"#endif // {header_guard}")
    header_lines.append("")  # Ensure trailing newline

    # --- Write to File ---
    try:
        with open(output_filename, "w") as f:
            f.write("\n".join(header_lines))
        print(f"Successfully generated '{output_filename}' from '{input_filename}'")
    except IOError as e:
        print(f"Error writing to output file '{output_filename}': {e}")
        exit(1)


# --- Main Execution ---
if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description=f"Generate a C header file for CAN IDs from '{DEFAULT_INPUT_FILENAME}'."
    )
    # Only argument is for the output file (optional)
    parser.add_argument(
        "-o",
        "--output",
        default=DEFAULT_OUTPUT_FILENAME,
        help=f"Path to the output C header file (default: {DEFAULT_OUTPUT_FILENAME})",
    )

    args = parser.parse_args()
    output_file = args.output  # Get the potentially specified output filename

    # --- Read Hardcoded JSON File ---
    input_file = DEFAULT_INPUT_FILENAME
    try:
        with open(input_file, "r") as f:
            try:
                can_data = json.load(f)
            except json.JSONDecodeError as e:
                print(f"Error: Invalid JSON format in '{input_file}': {e}")
                exit(1)
    except FileNotFoundError:
        print(f"Error: Input JSON file not found: '{input_file}'")
        print(
            f"Please ensure '{DEFAULT_INPUT_FILENAME}' exists in the same directory as the script."
        )
        exit(1)
    except IOError as e:
        print(f"Error reading input file '{input_file}': {e}")
        exit(1)

    # --- Validate Top-Level Structure (must be a list) ---
    if not isinstance(can_data, list):
        print(
            f"Error: The top-level structure in '{input_file}' must be a JSON array (list)."
        )
        exit(1)

    # --- Generate Header ---
    generate_header(
        can_data, input_file, output_file
    )  # Pass input filename for header comment
