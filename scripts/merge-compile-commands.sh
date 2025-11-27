#!/bin/bash
# Automatically merge multiple compile_commands.json files

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
OUTPUT_FILE="$PROJECT_ROOT/compile_commands.json"

echo "Generating compilation database..."

# Detect number of CPU cores and compilation database tool (macOS/Linux compatible)
if [[ "$OSTYPE" == "darwin"* ]]; then
    # macOS: Use compiledb instead of bear (better cross-compiler support)
    NCPU=$(sysctl -n hw.ncpu)
    IS_MACOS=true
    # Check if compiledb is available
    if ! command -v compiledb &> /dev/null; then
        echo "Warning: compiledb not found. Install with: pip3 install compiledb"
        echo "Falling back to bear (may not work with cross-compilers)..."
        USE_COMPILEDB=false
    else
        USE_COMPILEDB=true
    fi
else
    # Linux: Use bear
    NCPU=$(nproc)
    IS_MACOS=false
    USE_COMPILEDB=false
fi

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
if [ "$USE_COMPILEDB" = true ]; then
    # Use compiledb (parses make output)
    compiledb -o "$OUTPUT_FILE" make -j$NCPU 2>&1 | grep -v "warning:" | grep -v "^make" || true
elif [ "$IS_MACOS" = true ]; then
    # Fallback to bear on macOS
    bear --output "$OUTPUT_FILE" -- make -j$NCPU 2>&1 | grep -v "warning:" | grep -v "^make" || true
else
    # Use bear on Linux
    bear make -j$NCPU 2>&1 | grep -v "warning:" | grep -v "^make" || true
fi

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

SHELL_COMPILE_COMMANDS="$PROJECT_ROOT/userland/shell/compile_commands.json"
if [ "$USE_COMPILEDB" = true ]; then
    compiledb -o "$SHELL_COMPILE_COMMANDS" make 2>&1 | grep -v "warning:" | grep -v "^make" || true
elif [ "$IS_MACOS" = true ]; then
    bear --output "$SHELL_COMPILE_COMMANDS" -- make 2>&1 | grep -v "warning:" | grep -v "^make" || true
else
    bear make 2>&1 | grep -v "warning:" | grep -v "^make" || true
fi
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
    
    HELLOWORLD_COMPILE_COMMANDS="$PROJECT_ROOT/userland/helloworld/compile_commands.json"
    if [ "$USE_COMPILEDB" = true ]; then
        compiledb -o "$HELLOWORLD_COMPILE_COMMANDS" make 2>&1 | grep -v "warning:" | grep -v "^make" || true
    elif [ "$IS_MACOS" = true ]; then
        bear --output "$HELLOWORLD_COMPILE_COMMANDS" -- make 2>&1 | grep -v "warning:" | grep -v "^make" || true
    else
        bear make 2>&1 | grep -v "warning:" | grep -v "^make" || true
    fi
    if [ -f "$HELLOWORLD_COMPILE_COMMANDS" ]; then
        ENTRIES=$(extract_entries "$HELLOWORLD_COMPILE_COMMANDS")
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

# 4. Add userland tests compilation database
# Example: helloworld:
if [ -d "$PROJECT_ROOT/userland/tests" ] && [ -f "$PROJECT_ROOT/userland/tests/Makefile" ]; then
    echo "[4/4] Generating userland tests compilation database..."
    cd "$PROJECT_ROOT/userland/tests"
    make clean >/dev/null 2>&1 || true
    
    TESTS_COMPILE_COMMANDS="$PROJECT_ROOT/userland/tests/compile_commands.json"
    if [ "$USE_COMPILEDB" = true ]; then
        compiledb -o "$TESTS_COMPILE_COMMANDS" make 2>&1 | grep -v "warning:" | grep -v "^make" || true
    elif [ "$IS_MACOS" = true ]; then
        bear --output "$TESTS_COMPILE_COMMANDS" -- make 2>&1 | grep -v "warning:" | grep -v "^make" || true
    else
        bear make 2>&1 | grep -v "warning:" | grep -v "^make" || true
    fi
    if [ -f "$TESTS_COMPILE_COMMANDS" ]; then
        ENTRIES=$(extract_entries "$TESTS_COMPILE_COMMANDS")
        if [ -n "$ENTRIES" ]; then
            if [ "$FIRST" = false ]; then
                echo "," >> "$TEMP_FILE"
            fi
            echo "$ENTRIES" >> "$TEMP_FILE"
        fi
    fi
else
    echo "[4/4] Skipping userland tests (directory or Makefile not found)"
fi

# Complete JSON array
echo "]" >> "$TEMP_FILE"

# Move to final location
mv "$TEMP_FILE" "$OUTPUT_FILE"

echo "[OK] Compilation database generated: $OUTPUT_FILE"
echo "Number of source files: $(grep -c '"file":' "$OUTPUT_FILE")"

cd "$PROJECT_ROOT"

