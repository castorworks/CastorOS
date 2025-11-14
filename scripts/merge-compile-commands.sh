#!/bin/bash
# Automatically merge multiple compile_commands.json files

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
OUTPUT_FILE="$PROJECT_ROOT/compile_commands.json"

echo "Generating compilation database..."

# Temporary file to store all JSON entries
TEMP_FILE=$(mktemp)
echo "[" > "$TEMP_FILE"

# Flag to track if this is the first entry
FIRST=true

# Function: Extract entries from JSON file (removing [ and ])
extract_entries() {
    local file="$1"
    if [ -f "$file" ]; then
        # Extract content between [ and ], removing whitespace
        sed '1d;$d' "$file"
    fi
}

# 1. Generate kernel compilation database
echo "[1/3] Generating kernel compilation database..."
cd "$PROJECT_ROOT"
make clean >/dev/null 2>&1 || true
bear make -j$(nproc) 2>&1 | grep -v "warning:" | grep -v "^make" || true

if [ -f "$OUTPUT_FILE" ]; then
    ENTRIES=$(extract_entries "$OUTPUT_FILE")
    if [ -n "$ENTRIES" ]; then
        echo "$ENTRIES" >> "$TEMP_FILE"
        FIRST=false
    fi
fi

# 2. Generate shell compilation database
echo "[2/3] Generating shell compilation database..."
cd "$PROJECT_ROOT/userland/shell"
make clean >/dev/null 2>&1 || true
bear make 2>&1 | grep -v "warning:" | grep -v "^make" || true

SHELL_COMPILE_COMMANDS="$PROJECT_ROOT/userland/shell/compile_commands.json"
if [ -f "$SHELL_COMPILE_COMMANDS" ]; then
    ENTRIES=$(extract_entries "$SHELL_COMPILE_COMMANDS")
    if [ -n "$ENTRIES" ]; then
        if [ "$FIRST" = false ]; then
            echo "," >> "$TEMP_FILE"
        fi
        echo "$ENTRIES" >> "$TEMP_FILE"
        FIRST=false
    fi
fi

# 3. Add other userland programs here if needed
# Example: helloworld:
if [ -d "$PROJECT_ROOT/userland/helloworld" ] && [ -f "$PROJECT_ROOT/userland/helloworld/Makefile" ]; then
    echo "[3/3] Generating helloworld compilation database..."
    cd "$PROJECT_ROOT/userland/helloworld"
    make clean >/dev/null 2>&1 || true
    bear make 2>&1 | grep -v "warning:" | grep -v "^make" || true
    
    UDP_COMPILE_COMMANDS="$PROJECT_ROOT/userland/helloworld/compile_commands.json"
    if [ -f "$UDP_COMPILE_COMMANDS" ]; then
        ENTRIES=$(extract_entries "$UDP_COMPILE_COMMANDS")
        if [ -n "$ENTRIES" ]; then
            if [ "$FIRST" = false ]; then
                echo "," >> "$TEMP_FILE"
            fi
            echo "$ENTRIES" >> "$TEMP_FILE"
        fi
    fi
else
    echo "[3/3] Skipping helloworld (directory or Makefile not found)"
fi

# Complete JSON array
echo "]" >> "$TEMP_FILE"

# Move to final location
mv "$TEMP_FILE" "$OUTPUT_FILE"

echo "[OK] Compilation database generated: $OUTPUT_FILE"
echo "Number of source files: $(grep -c '"file":' "$OUTPUT_FILE")"

cd "$PROJECT_ROOT"

