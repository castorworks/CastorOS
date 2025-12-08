#!/bin/bash
# ============================================================================
# test_build_arch.sh - Property test for build system architecture selection
# ============================================================================
# **Feature: multi-arch-support, Property: Build system selects correct compiler for each ARCH value**
# **Validates: Requirements 2.1, 2.2, 2.3**
#
# This script tests that the Makefile correctly selects the appropriate
# compiler, linker, and assembler for each supported architecture.
# ============================================================================

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

PASSED=0
FAILED=0
TOTAL=0

# Test function
test_arch_compiler() {
    local arch="$1"
    local expected_cc="$2"
    local expected_ld="$3"
    local expected_as="$4"
    local expected_define="$5"
    
    TOTAL=$((TOTAL + 1))
    
    printf "Testing ARCH=%s... " "$arch"
    
    # Get actual values from Makefile
    local output
    output=$(cd "$PROJECT_ROOT" && make info ARCH="$arch" 2>&1)
    
    local actual_cc=$(echo "$output" | awk '/^Compiler:/ {print $2}')
    local actual_ld=$(echo "$output" | awk '/^Linker:/ {print $2}')
    local actual_as=$(echo "$output" | awk '/^Assembler:/ {print $2}')
    local actual_cflags=$(echo "$output" | awk '/^CFLAGS:/ {$1=""; print $0}')

    local has_define=false
    case "$actual_cflags" in
        *"$expected_define"*) has_define=true ;;
    esac
    
    local failed=false
    local errors=""
    
    if [ "$actual_cc" != "$expected_cc" ]; then
        failed=true
        errors="$errors\n  - Compiler: expected '$expected_cc', got '$actual_cc'"
    fi
    
    if [ "$actual_ld" != "$expected_ld" ]; then
        failed=true
        errors="$errors\n  - Linker: expected '$expected_ld', got '$actual_ld'"
    fi
    
    if [ "$actual_as" != "$expected_as" ]; then
        failed=true
        errors="$errors\n  - Assembler: expected '$expected_as', got '$actual_as'"
    fi
    
    if [ "$has_define" != "true" ]; then
        failed=true
        errors="$errors\n  - CFLAGS missing define: $expected_define"
    fi
    
    if [ "$failed" = "true" ]; then
        printf "${RED}FAILED${NC}\n"
        printf "$errors\n"
        FAILED=$((FAILED + 1))
        return 1
    else
        printf "${GREEN}PASSED${NC}\n"
        PASSED=$((PASSED + 1))
        return 0
    fi
}

# Test invalid architecture rejection
test_invalid_arch() {
    TOTAL=$((TOTAL + 1))
    
    printf "Testing invalid ARCH rejection... "
    
    local output
    output=$(cd "$PROJECT_ROOT" && make info ARCH=invalid 2>&1) || true
    
    case "$output" in
        *"Invalid ARCH=invalid"*)
            printf "${GREEN}PASSED${NC}\n"
            PASSED=$((PASSED + 1))
            return 0
            ;;
        *)
            printf "${RED}FAILED${NC}\n"
            echo "  - Expected error message for invalid architecture"
            FAILED=$((FAILED + 1))
            return 1
            ;;
    esac
}

# Test default architecture
test_default_arch() {
    TOTAL=$((TOTAL + 1))
    
    printf "Testing default ARCH (should be i686)... "
    
    local output
    output=$(cd "$PROJECT_ROOT" && make info 2>&1)
    
    local actual_arch=$(echo "$output" | awk '/^Architecture:/ {print $2}')
    
    if [ "$actual_arch" = "i686" ]; then
        printf "${GREEN}PASSED${NC}\n"
        PASSED=$((PASSED + 1))
        return 0
    else
        printf "${RED}FAILED${NC}\n"
        echo "  - Expected default architecture 'i686', got '$actual_arch'"
        FAILED=$((FAILED + 1))
        return 1
    fi
}

# Main test execution
echo "============================================================================"
echo "Property Test: Build system selects correct compiler for each ARCH value"
echo "Validates: Requirements 2.1, 2.2, 2.3"
echo "============================================================================"
echo ""

# Run tests for each architecture
test_arch_compiler "i686" "i686-elf-gcc" "i686-elf-ld" "nasm" "-DARCH_I686" || true
test_arch_compiler "x86_64" "x86_64-elf-gcc" "x86_64-elf-ld" "nasm" "-DARCH_X86_64" || true
test_arch_compiler "arm64" "aarch64-elf-gcc" "aarch64-elf-ld" "aarch64-elf-as" "-DARCH_ARM64" || true

# Test invalid architecture
test_invalid_arch || true

# Test default architecture
test_default_arch || true

# Print summary
echo ""
echo "============================================================================"
echo "Test Summary"
echo "============================================================================"
echo "Total:  $TOTAL"
printf "Passed: ${GREEN}%d${NC}\n" "$PASSED"
printf "Failed: ${RED}%d${NC}\n" "$FAILED"
echo ""

if [ $FAILED -eq 0 ]; then
    printf "${GREEN}All property tests passed!${NC}\n"
    exit 0
else
    printf "${RED}Some property tests failed!${NC}\n"
    exit 1
fi
