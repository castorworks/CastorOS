/**
 * @file stubs.c
 * @brief ARM64 Architecture Stubs
 * 
 * This file previously contained the ARM64-specific kernel_main entry point.
 * The kernel_main function has been moved to src/kernel/kernel.c as part of
 * the unified kernel initialization flow.
 * 
 * This file is now kept for any future ARM64-specific stub functions that
 * may be needed during development.
 * 
 * **Feature: arm64-kernel-integration**
 * **Validates: Requirements 10.1**
 */

#include <types.h>

/*
 * Note: kernel_main has been moved to src/kernel/kernel.c
 * 
 * The ARM64 kernel_main is now part of the common kernel code with
 * architecture-specific initialization handled via conditional compilation.
 * This allows for better code organization and easier maintenance.
 * 
 * See: src/kernel/kernel.c for the unified kernel_main implementation.
 */

/* Placeholder for future ARM64-specific stubs if needed */
