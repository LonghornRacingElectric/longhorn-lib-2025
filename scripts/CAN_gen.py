import csv
import json
import re
import math
import os

# --- Configuration ---
# Define the input CSV filename here
CSV_FILENAME = "CAN.csv"
# Define the output JSON filename
JSON_FILENAME = "can_packets.json"

# Regex to extract CAN type and precision from data descriptions like "(int16, 0.1V)"
# Group 1: Data type (e.g., int16, uint8)
# Group 2: Optional precision string (e.g., 0.1, 1.0, 2^-7)
can_signal_pattern = re.compile(r"\(([^,]+)(?:,\s*([^)\s]+))?.*\)", re.IGNORECASE)

# Regex to parse power-of-2 notation like "2^-7"
pow2_pattern = re.compile(r"2\^(-?\d+)")

# Regex to parse the protobuf definition like "field_name (type)" or "nested.field (type)"
# Group 1: Field name (allowing letters, numbers, underscore, dot)
# Group 2: Type string
protobuf_pattern = re.compile(r"([\w\.]+)\s*\(([^)]+)\)")

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
        return 0
    range_match = re.match(r"\(\s*(\d+)\s*-\s*(\d+)\s*\)", dlc_str)
    if range_match:
        return int(range_match.group(2))
    try:
        return int(dlc_str)
    except ValueError:
        if dlc_str.lower() == "depends":
            return 0
        print(f"Warning: Could not parse DLC '{dlc_str}'. Defaulting to 0.")
        return 0


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
            return round((1.0 / frequency_hz) * 1000.0, 4)
        except ZeroDivisionError:
            return None
    return None


def parse_quantity(quantity_str):
    """Parses quantity string, returns int or 0 if invalid."""
    quantity_str = quantity_str.strip()
    if not quantity_str or quantity_str.lower() in ["depends", "max 32"]:
        return 0
    try:
        return int(quantity_str)
    except ValueError:
        print(f"Warning: Could not parse Quantity '{quantity_str}'. Defaulting to 0.")
        return 0


def parse_participants(participant_str):
    """Parses the From/To column string into an array of strings."""
    participant_str = participant_str.strip()
    if not participant_str:
        return []
    # Simple case: treat the whole string as one participant
    return [participant_str]


def process_csv(csv_filepath):
    """Reads a CSV file and converts it to the desired JSON structure."""
    can_packets = []
    if not os.path.exists(csv_filepath):
        print(f"Error: CSV file not found at '{csv_filepath}'")
        return []

    try:
        with open(csv_filepath, mode="r", encoding="utf-8-sig") as csvfile:
            reader = csv.DictReader(filter(lambda row: row.strip(), csvfile))
            processed_rows = 0

            for i, row in enumerate(reader):
                row_num = i + 2  # Account for header and 0-based index
                try:
                    # --- 1. Get Packet ID ---
                    packet_id_str = row.get("CAN ID", "").strip()
                    packet_id = None
                    if packet_id_str:
                        try:
                            packet_id = int(packet_id_str, 16)
                        except ValueError:
                            print(
                                f"Warning: Row {row_num}: Invalid hex CAN ID '{packet_id_str}'. Skipping row."
                            )
                            continue
                    else:
                        continue  # Skip rows without CAN ID

                    # --- 2. Get Other Basic Fields ---
                    packet_name = (
                        row.get("Packet Info", "").strip() or f"Packet_{packet_id_str}"
                    )
                    from_participants = parse_participants(row.get("From", ""))
                    to_participants = parse_participants(row.get("To", ""))
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
                        byte_info_str = byte_info_str_raw.strip('"')  # Clean quotes

                        if (
                            not byte_info_str
                            or byte_info_str.lower() == "unused"
                            or byte_info_str == ","
                        ):
                            continue  # Skip empty/unused

                        # --- Split CAN and Protobuf parts ---
                        can_part = byte_info_str
                        protobuf_info = None
                        if ";" in byte_info_str:
                            parts = byte_info_str.split(";", 1)
                            can_part = parts[0].strip()
                            proto_part_raw = parts[1].strip()

                            # Parse the protobuf part
                            proto_match = protobuf_pattern.search(proto_part_raw)
                            if proto_match:
                                proto_field = proto_match.group(1).strip()
                                proto_type = proto_match.group(2).strip()
                                protobuf_info = {
                                    "type": proto_type,
                                    "field": proto_field,
                                }
                            else:
                                print(
                                    f"Warning: Row {row_num}, CAN ID {packet_id_str}, Data[{byte_num}]: Could not parse protobuf definition format in '{proto_part_raw}'."
                                )

                        # --- Parse the CAN signal part ---
                        can_match = can_signal_pattern.search(can_part)
                        if can_match:
                            # Extract CAN Name (text before the parenthesis in can_part)
                            match_start_index = can_match.start()
                            name = (
                                can_part[:match_start_index].strip()
                                or f"Field_{byte_num}"
                            )

                            # Extract CAN Type and Precision
                            type_str = (
                                can_match.group(1).lower().strip()
                                if can_match.group(1)
                                else ""
                            )
                            precision_str = (
                                can_match.group(2) if can_match.group(2) else None
                            )

                            # Normalize type, get length
                            normalized_type = type_normalization.get(type_str, type_str)
                            length = type_lengths.get(normalized_type)
                            if length is None:
                                print(
                                    f"Warning: Row {row_num}, CAN ID {packet_id_str}: Unknown CAN data type '{type_str}'. Assuming length 1 for field '{name}'."
                                )
                                length = 1
                                conv_type = type_str
                            else:
                                conv_type = normalized_type

                            # Parse CAN precision
                            precision = 1.0  # Default
                            if precision_str:
                                precision_str = precision_str.strip()
                                pow2_match = pow2_pattern.match(precision_str)
                                if pow2_match:
                                    try:
                                        exponent = int(pow2_match.group(1))
                                        precision = math.pow(2, exponent)
                                    except ValueError:
                                        print(
                                            f"Warning: Row {row_num}, CAN ID {packet_id_str}: Invalid exponent '{precision_str}'. Using 1.0 for field '{name}'."
                                        )
                                else:
                                    try:
                                        numeric_part_match = re.match(
                                            r"([-+]?\d*\.?\d+([eE][-+]?\d+)?)",
                                            precision_str,
                                        )
                                        if numeric_part_match:
                                            precision = float(
                                                numeric_part_match.group(1)
                                            )
                                        elif precision_str.isalpha():
                                            precision = 1.0  # Treat boolean etc. as 1.0
                                        else:
                                            print(
                                                f"Warning: Row {row_num}, CAN ID {packet_id_str}: Could not parse CAN precision '{precision_str}'. Using 1.0 for field '{name}'."
                                            )
                                    except ValueError:
                                        print(
                                            f"Warning: Row {row_num}, CAN ID {packet_id_str}: Invalid CAN precision format '{precision_str}'. Using 1.0 for field '{name}'."
                                        )

                            # Check for gaps and update index
                            if byte_num > current_byte_index:
                                current_byte_index = byte_num

                            # --- Create Byte Definition ---
                            byte_def = {
                                "start_byte": current_byte_index,
                                "name": name,
                                "length": length,
                                "conv_type": conv_type,
                                "precision": precision,
                            }
                            # Add protobuf info if found
                            if protobuf_info:
                                byte_def["protobuf"] = protobuf_info

                            bytes_list.append(byte_def)
                            current_byte_index += length  # Increment index

                        else:
                            # Handle simple CAN type definitions like "VSM (byte)" within can_part
                            simple_type_match = re.match(r"\(\s*(\w+)\s*\)", can_part)
                            if simple_type_match:
                                type_str = simple_type_match.group(1).lower().strip()
                                normalized_type = type_normalization.get(
                                    type_str, type_str
                                )
                                length = type_lengths.get(normalized_type)
                                if length:
                                    paren_index = can_part.find("(")
                                    name = (
                                        can_part[:paren_index].strip()
                                        or f"Field_{byte_num}"
                                    )

                                    if byte_num > current_byte_index:
                                        current_byte_index = byte_num

                                    byte_def = {
                                        "start_byte": current_byte_index,
                                        "name": name,
                                        "length": length,
                                        "conv_type": normalized_type,
                                        "precision": 1.0,  # Default precision
                                    }
                                    # Add protobuf info if found (even for simple CAN types)
                                    if protobuf_info:
                                        byte_def["protobuf"] = protobuf_info

                                    bytes_list.append(byte_def)
                                    current_byte_index += length
                                else:
                                    print(
                                        f"Warning: Row {row_num}, CAN ID {packet_id_str}: Simple CAN type '{type_str}' found but unknown length in '{can_part}'. Skipping field."
                                    )
                            # else: # Optionally warn about unparsed CAN part structure
                            # print(f"Warning: Row {row_num}, CAN ID {packet_id_str}: Could not parse CAN signal structure in '{can_part}'. Skipping field.")

                    # --- 4. Construct Packet JSON ---
                    if packet_id is not None:
                        can_packet_json = {
                            "packet_id": packet_id,
                            "packet_name": packet_name,
                            "from": from_participants,
                            "to": to_participants,
                            "data_length": dlc,
                            "frequency_ms": frequency_ms,
                            "frequency": frequency_hz,
                            "quantity": quantity,
                            "bytes": bytes_list,
                        }
                        can_packets.append(can_packet_json)
                        processed_rows += 1

                except ValueError as ve:
                    print(
                        f"Error processing row {row_num} (ValueError): {ve}. Skipping row. Data: {row}"
                    )
                except KeyError as ke:
                    print(
                        f"Error processing row {row_num} (Missing Key): {ke}. Check CSV headers. Skipping row. Data: {row}"
                    )
                except Exception as e:
                    print(
                        f"Unexpected error processing row {row_num}: {e}. Skipping row. Data: {row}"
                    )

    except FileNotFoundError:
        print(f"Error: CSV file not found at '{csv_filepath}'")
        return []
    except Exception as e:
        print(f"An critical error occurred while reading the CSV file: {e}")
        return []

    print(f"Total rows processed successfully: {processed_rows}")
    return can_packets


# --- Main Execution ---
if __name__ == "__main__":
    print(f"Processing CSV file: {CSV_FILENAME}")
    processed_packets = process_csv(CSV_FILENAME)

    if processed_packets:
        json_output = json.dumps(processed_packets, indent=4)
        try:
            with open(JSON_FILENAME, "w", encoding="utf-8") as outfile:
                outfile.write(json_output)
            print(f"\nOutput successfully written to {JSON_FILENAME}")
        except IOError as e:
            print(f"\nError writing JSON to file: {e}")
    else:
        print("No valid packets were processed or an error occurred.")
