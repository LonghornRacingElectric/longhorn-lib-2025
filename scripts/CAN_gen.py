import csv
import json
import re
import math
import os  # Import os for file path handling

# --- Configuration ---
# Define the input CSV filename here
CSV_FILENAME = "CAN.csv"
# Define the output JSON filename
JSON_FILENAME = "can_packets.json"

# Regex to extract type and precision from data descriptions like "(int16, 0.1V)" or "(uint8, boolean)" or "(int16, 2^-7 rad/s)"
# It looks for content within the outermost parentheses.
# Group 1: Data type (e.g., int16, uint8, uint32, bool, byte)
# Group 2: Optional precision string (e.g., 0.1, 1.0, 2^-7)
byte_pattern = re.compile(r"\(([^,]+)(?:,\s*([^)\s]+))?.*\)", re.IGNORECASE)

# Regex to parse power-of-2 notation like "2^-7"
pow2_pattern = re.compile(r"2\^(-?\d+)")

# Type mapping to byte length
type_lengths = {
    "uint8": 1,
    "int8": 1,
    "uint16": 2,
    "int16": 2,
    "uint32": 4,
    "int32": 4,
    "uint64": 8,  # Assuming 64-bit types if they appear
    "int64": 8,
    "bool": 1,  # Often represented as uint8
    "boolean": 1,  # Alias for bool
    "byte": 1,  # Explicit byte
    "float": 4,  # Standard float, though often scaled integers are used in CAN
    "float32": 4,
    "double": 8,  # Standard double
    "float64": 8,
}

# Type normalization (map aliases to standard names used in type_lengths)
type_normalization = {
    "boolean": "uint8",  # Represent boolean as uint8
    "bool": "uint8",
    "byte": "uint8",
    # Add other normalizations if needed
}


def parse_dlc(dlc_str):
    """Parses the DLC string, handling single numbers and ranges like '(1-8)'."""
    dlc_str = dlc_str.strip()
    if not dlc_str:
        return 0  # Default DLC if empty

    # Check for range format like (1-8)
    range_match = re.match(r"\(\s*(\d+)\s*-\s*(\d+)\s*\)", dlc_str)
    if range_match:
        # For ranges, return the max value as specified in the example 0x04
        return int(range_match.group(2))

    # Try converting directly to int
    try:
        return int(dlc_str)
    except ValueError:
        print(f"Warning: Could not parse DLC '{dlc_str}'. Defaulting to 0.")
        return 0  # Default DLC if invalid format


def parse_frequency(freq_str):
    """Parses frequency string, returns float or None if invalid/NA."""
    freq_str = freq_str.strip().lower()
    if not freq_str or freq_str == "na" or freq_str == "0":
        return None
    try:
        return float(freq_str)
    except ValueError:
        print(f"Warning: Could not parse Frequency '{freq_str}'.")
        return None


def calculate_frequency_ms(frequency_hz):
    """Calculates frequency in milliseconds."""
    if frequency_hz is not None and frequency_hz > 0:
        try:
            return (1.0 / frequency_hz) * 1000.0
        except ZeroDivisionError:
            return None  # Should be caught by freq > 0 check, but safety first
    return None


def parse_quantity(quantity_str):
    """Parses quantity string, returns int or 0 if invalid."""
    quantity_str = quantity_str.strip()
    # Handle specific cases like 'Depends' or 'Max 32' - returning 0 as quantity might not be fixed
    if not quantity_str or quantity_str.lower() in ["depends", "max 32"]:
        return 0
    try:
        return int(quantity_str)
    except ValueError:
        print(f"Warning: Could not parse Quantity '{quantity_str}'. Defaulting to 0.")
        return 0


def process_csv(csv_filepath):
    """Reads a CSV file and converts it to the desired JSON structure."""
    can_packets = []
    if not os.path.exists(csv_filepath):
        print(f"Error: CSV file not found at '{csv_filepath}'")
        return []

    try:
        with open(csv_filepath, mode="r", encoding="utf-8") as csvfile:
            # Handle potential empty lines or lines not matching headers
            # Filter out rows where the essential 'CAN ID' might be missing or implied
            reader = csv.DictReader(filter(lambda row: row.strip(), csvfile))
            current_packet_id = (
                None  # Keep track for potential future use (not used currently)
            )

            for i, row in enumerate(reader):
                # --- 1. Get Packet ID ---
                packet_id_str = row.get("CAN ID", "").strip()
                packet_id = None  # Reset for each row

                if packet_id_str:
                    if packet_id_str.startswith("0x"):
                        try:
                            packet_id = int(packet_id_str, 16)
                            current_packet_id = packet_id  # Update the current ID
                        except ValueError:
                            print(
                                f"Warning: Row {i+2}: Invalid hex CAN ID '{packet_id_str}'. Skipping row."
                            )
                            continue
                    else:
                        # Attempt hex conversion even without 0x prefix if digits are hex-compatible
                        try:
                            packet_id = int(packet_id_str, 16)
                            current_packet_id = packet_id
                        except ValueError:
                            print(
                                f"Warning: Row {i+2}: Could not parse CAN ID '{packet_id_str}' as hex. Skipping row."
                            )
                            continue
                else:
                    # Skip rows that don't explicitly define a CAN ID in the first column
                    # print(f"Info: Row {i+2}: Skipping row due to missing CAN ID.")
                    continue  # Skip rows without an explicit CAN ID

                # --- 2. Get Other Basic Fields ---
                dlc = parse_dlc(row.get("Data Length Code (DLC)", "0"))
                frequency_hz = parse_frequency(row.get("Frequency (Hz)", "NA"))
                frequency_ms = calculate_frequency_ms(frequency_hz)
                quantity = parse_quantity(row.get("Quantity", "0"))

                # --- 3. Parse Data Bytes ---
                bytes_list = []
                current_byte_index = 0

                for byte_num in range(8):  # Process Data[0] through Data[7]
                    data_key = f"Data[{byte_num}]"
                    byte_info_str_raw = row.get(data_key, "").strip()

                    # Clean surrounding quotes if present
                    byte_info_str = byte_info_str_raw.strip('"')

                    if (
                        not byte_info_str
                        or byte_info_str.lower() == "unused"
                        or byte_info_str == ","
                    ):
                        # Skip empty/unused definitions without advancing current_byte_index,
                        # as a multi-byte field might occupy this space.
                        continue

                    match = byte_pattern.search(byte_info_str)
                    if match:
                        # --- Extract Name ---
                        match_start_index = match.start()
                        name = byte_info_str[:match_start_index].strip()
                        if not name:
                            # If no text before parenthesis, use a placeholder or skip name
                            name = f"Unnamed Field {byte_num}"  # Placeholder if desired

                        # --- Extract Type and Precision ---
                        type_str = (
                            match.group(1).lower().strip() if match.group(1) else ""
                        )
                        precision_str = match.group(2) if match.group(2) else None

                        # Normalize type
                        normalized_type = type_normalization.get(type_str, type_str)

                        # Get byte length
                        length = type_lengths.get(normalized_type)
                        if length is None:
                            print(
                                f"Warning: Row {i+2}, CAN ID {packet_id_str}: Unknown data type '{type_str}' in '{byte_info_str}'. Assuming length 1."
                            )
                            length = 1  # Default length if type is unknown
                            conv_type = type_str
                        else:
                            conv_type = normalized_type

                        # Parse precision
                        precision = 1.0  # Default precision
                        if precision_str:
                            precision_str = precision_str.strip()
                            pow2_match = pow2_pattern.match(precision_str)
                            if pow2_match:
                                try:
                                    exponent = int(pow2_match.group(1))
                                    precision = math.pow(2, exponent)
                                except ValueError:
                                    print(
                                        f"Warning: Row {i+2}, CAN ID {packet_id_str}: Invalid exponent in power-of-2 precision '{precision_str}'. Using 1.0."
                                    )
                            else:
                                try:
                                    # Handle potential non-numeric chars if unit is attached without space
                                    numeric_part_match = re.match(
                                        r"([-+]?\d*\.?\d+([eE][-+]?\d+)?)",
                                        precision_str,
                                    )
                                    if numeric_part_match:
                                        precision = float(numeric_part_match.group(1))
                                    else:
                                        # Handle cases like 'boolean' or other non-numeric descriptions as precision 1
                                        if precision_str.isalpha():
                                            precision = 1.0  # Treat alpha descriptors like boolean as precision 1
                                        else:
                                            print(
                                                f"Warning: Row {i+2}, CAN ID {packet_id_str}: Could not parse precision '{precision_str}'. Using 1.0."
                                            )
                                except ValueError:
                                    print(
                                        f"Warning: Row {i+2}, CAN ID {packet_id_str}: Invalid precision format '{precision_str}'. Using 1.0."
                                    )

                        # Check if field definition starts beyond the current byte index
                        # This indicates sparse definitions (e.g., Data[0] defines bytes 0-1, Data[2] defines byte 2)
                        # If so, assume bytes in between are unused/padding.
                        if byte_num > current_byte_index:
                            print(
                                f"Info: Row {i+2}, CAN ID {packet_id_str}: Gap detected. Assuming padding bytes between byte {current_byte_index} and {byte_num}."
                            )
                            # You could optionally create "padding" byte entries here if needed.
                            # For now, we just update the index.
                            current_byte_index = (
                                byte_num  # Jump index to the start of this field
                            )

                        # Check if adding this field exceeds expected DLC (optional sanity check)
                        # if current_byte_index + length > dlc:
                        #     print(f"Warning: Row {i+2}, CAN ID {packet_id_str}: Byte definition {name} (start: {current_byte_index}, len: {length}) may exceed DLC ({dlc}).")

                        byte_def = {
                            "start_byte": current_byte_index,
                            "name": name,
                            "length": length,
                            "conv_type": conv_type,
                            "precision": precision,
                        }
                        bytes_list.append(byte_def)

                        # IMPORTANT: Increment index *after* processing the field
                        current_byte_index += length
                    else:
                        # Optional: Warn if Data[n] has content but doesn't match the expected '(type, precision)' pattern
                        # Check if it's just a type like '(byte)'
                        simple_type_match = re.match(r"\(\s*(\w+)\s*\)", byte_info_str)
                        if simple_type_match:
                            type_str = simple_type_match.group(1).lower().strip()
                            normalized_type = type_normalization.get(type_str, type_str)
                            length = type_lengths.get(normalized_type)
                            if length:
                                # Try to find a name before the parenthesis
                                paren_index = byte_info_str.find("(")
                                name = (
                                    byte_info_str[:paren_index].strip()
                                    if paren_index != -1
                                    else f"Unnamed Field {byte_num}"
                                )
                                name = (
                                    name or f"Unnamed Field {byte_num}"
                                )  # Ensure name is not empty

                                byte_def = {
                                    "start_byte": current_byte_index,
                                    "name": name,
                                    "length": length,
                                    "conv_type": normalized_type,
                                    "precision": 1.0,  # Default precision for simple type
                                }
                                bytes_list.append(byte_def)
                                current_byte_index += length
                            else:
                                print(
                                    f"Warning: Row {i+2}, CAN ID {packet_id_str}: Simple type '{type_str}' found but unknown length in '{byte_info_str}'. Skipping field."
                                )

                        # else: # Uncomment to warn about any unparsed fields
                        # print(f"Warning: Row {i+2}, CAN ID {packet_id_str}: Could not parse byte info structure in '{byte_info_str}'. Skipping field.")

                # --- 4. Construct Packet JSON ---
                # Only add if packet_id was valid and processed
                if packet_id is not None:
                    # Adjust DLC if calculated total bytes differ (optional)
                    # calculated_dlc = sum(b['length'] for b in bytes_list)
                    # final_dlc = max(dlc, calculated_dlc) # Or just use the CSV DLC 'dlc'

                    can_packet_json = {
                        "packet_id": packet_id,
                        "data_length": dlc,  # Use DLC from CSV
                        "frequency_ms": frequency_ms,
                        "frequency": frequency_hz,
                        "quantity": quantity,
                        "bytes": bytes_list,
                    }
                    can_packets.append(can_packet_json)

    except FileNotFoundError:
        print(f"Error: CSV file not found at '{csv_filepath}'")
        return []
    except Exception as e:
        print(f"An error occurred while processing the CSV file: {e}")
        return []

    return can_packets


# --- Main Execution ---
if __name__ == "__main__":
    print(f"Processing CSV file: {CSV_FILENAME}")
    processed_packets = process_csv(CSV_FILENAME)

    if processed_packets:
        # Output the result as JSON
        json_output = json.dumps(processed_packets, indent=4)
        # print(json_output) # Optionally print to console

        # Write to the output JSON file
        try:
            with open(JSON_FILENAME, "w", encoding="utf-8") as outfile:
                outfile.write(json_output)
            print(f"\nOutput successfully written to {JSON_FILENAME}")
        except IOError as e:
            print(f"\nError writing JSON to file: {e}")
    else:
        print("No packets were processed.")
