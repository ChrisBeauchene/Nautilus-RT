/*
 * Automatically generated C config: don't edit
 * Nautilus version: 
 * Sun Mar 20 15:29:31 2016
 */
#define AUTOCONF_INCLUDED

/*
 * Platform/Arch Options
 */
#define NAUT_CONFIG_X86_64_HOST 1
#undef NAUT_CONFIG_XEON_PHI
#undef NAUT_CONFIG_HVM_HRT
#define NAUT_CONFIG_MAX_CPUS 256
#define NAUT_CONFIG_MAX_IOAPICS 16

/*
 * Nautilus AeroKernel Build Config
 */
#define NAUT_CONFIG_USE_NAUT_BUILTINS 1
#define NAUT_CONFIG_CXX_SUPPORT 1
#define NAUT_CONFIG_TOOLCHAIN_ROOT ""

/*
 * Interface Options
 */
#define NAUT_CONFIG_THREAD_EXIT_KEYCODE 196

/*
 * Nautilus AeroKernel Configuration
 */
#undef NAUT_CONFIG_USE_TICKETLOCKS

/*
 * AeroKernel Performance Optimizations
 */
#define NAUT_CONFIG_FPU_SAVE 1
#undef NAUT_CONFIG_KICK_SCHEDULE
#undef NAUT_CONFIG_HALT_WHILE_IDLE
#undef NAUT_CONFIG_THREAD_OPTIMIZE
#undef NAUT_CONFIG_USE_IDLE_THREADS
#define NAUT_CONFIG_USE_RT_SCHEDULER 1

/*
 * Debugging
 */
#undef NAUT_CONFIG_DEBUG_INFO
#undef NAUT_CONFIG_DEBUG_PRINTS
#undef NAUT_CONFIG_RT_DEBUG
#undef NAUT_CONFIG_ENABLE_ASSERTS
#undef NAUT_CONFIG_PROFILE
#undef NAUT_CONFIG_SILENCE_UNDEF_ERR
#undef NAUT_CONFIG_ENABLE_STACK_CHECK

/*
 * Parallel Runtime Integration
 */
#undef NAUT_CONFIG_LEGION_RT
#undef NAUT_CONFIG_NDPC_RT
#undef NAUT_CONFIG_NESL_RT
#define NAUT_CONFIG_NO_RT 1

/*
 * Device options
 */
#undef NAUT_CONFIG_SERIAL_REDIRECT
#define NAUT_CONFIG_SERIAL_PORT 1
#undef NAUT_CONFIG_HPET
#define NAUT_CONFIG_HZ 10
