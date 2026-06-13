#!/usr/bin/env python3
import sys
import os
import argparse

def main():
    parser = argparse.ArgumentParser(description="Deduplicate lines in a log file.")
    parser.add_argument("input_file", nargs="?", default="out.log", help="Path to the log file (default: out.log)")
    parser.add_argument("-o", "--output", help="Path to the output file (default: <input>_clean.<ext>)")
    parser.add_argument("-c", "--consecutive", action="store_true", help="Only remove consecutive duplicate lines (similar to 'uniq')")
    parser.add_argument("-i", "--inplace", action="store_true", help="Deduplicate the file in-place (overwrites the input file)")

    args = parser.parse_args()

    if not os.path.exists(args.input_file):
        print(f"Error: Log file '{args.input_file}' not found.")
        sys.exit(1)

    print(f"Reading '{args.input_file}'...")
    with open(args.input_file, "r", encoding="utf-8", errors="ignore") as f:
        lines = f.readlines()

    original_count = len(lines)
    unique_lines = []

    if args.consecutive:
        # Collapse consecutive duplicates (like uniq)
        last_line = None
        for line in lines:
            if line != last_line:
                unique_lines.append(line)
                last_line = line
    else:
        # Remove all global duplicates, preserving order
        seen = set()
        for line in lines:
            if line not in seen:
                seen.add(line)
                unique_lines.append(line)

    deduped_count = len(unique_lines)
    removed_count = original_count - deduped_count

    output_path = args.output
    if args.inplace:
        output_path = args.input_file
    elif not output_path:
        base, ext = os.path.splitext(args.input_file)
        output_path = f"{base}_clean{ext}"

    print(f"Writing {deduped_count} lines to '{output_path}'...")
    with open(output_path, "w", encoding="utf-8") as f:
        f.writelines(unique_lines)

    print(f"Done! Removed {removed_count} duplicate lines.")

if __name__ == "__main__":
    main()
