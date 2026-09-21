#ifndef GAME_STATE_INTERFACE_H
#define GAME_STATE_INTERFACE_H

#include "user_vmcall.h"
#include <stdint.h>
#include <math.h>

/**
 * @brief Arc Raiders Game State Interface
 * 
 * Register-based interface for passing game state data to/from hypervisor.
 * No memory patching required. All data passes through CPU registers.
 * 
 * Game State Structure:
 *   - Object position (X, Y, Z coordinates)
 *   - View angle/pitch/yaw for aim snapping
 *   - Entity visibility state
 *   - Current game mode/status
 *   - Surface/slope information
 */

/**
 * @brief Game state structure for Arc Raiders
 */
typedef struct {
    // Player position
    float position_x;
    float position_y;
    float position_z;
    
    // View angles for aim manipulation (0-360 degrees)
    float pitch;     // Up/down
    float yaw;       // Left/right
    
    // Entity status
    uint8_t visible;    // Is enemy visible?
    uint8_t is_enemy;   // Is this entity an enemy?
    uint8_t team_aligned; // Is this a teammate?
    
    // Game information
    uint32_t game_mode;  // Current game mode
    float interpolation_phase; // 0.0-1.0 interpolation value
    
    // Surface/slope information for adjustment
    float surface_normal_x;
    float surface_normal_y;
    float surface_normal_z;
    
    // Enemy tracking (for snap aiming)
    float enemy_distance;
    float enemy_bounding_radius;
    
    // Last detected enemy position
    float last_enemy_x;
    float last_enemy_y;
    float last_enemy_z;
} GameState;

/**
 * @brief VMCALL Ops for Game State Communication
 */
#define VMCALL_OPCODE_GAME_GET_STATE          UINT32_C(2)
#define VMCALL_OPCODE_GAME_SET_AIM            UINT32_C(3)
#define VMCALL_OPCODE_GAME_FILTER_TELEMETRY   UINT32_C(4)
#define VMCALL_OPCODE_GAME_QUERY_SHOT_RESULT  UINT32_C(5)

/**
 * @brief Query current game state from hypervisor
 * 
 * The hypervisor intercepts Arc Raiders game data and allows access through
 * this register interface. Returns the current game state in registers.
 * 
 * Returns:
 *   RAX = status code (0 = success)
 *   RBX = GameState struct (passed via XMM registers)
 */
static inline int game_state_query(GameState* result) {
    #if defined(_M_X64)
    __asm {
        mov rax, VMCALL_MAGIC
        mov rcx, VMCALL_OPCODE_GAME_GET_STATE
        
        vmcall
        
        // Result in RAX (verify success)
        test rax, rax
        je success
        xor eax, eax
        ret
        success:
        // result structure in RBX
        movdqu xmm0, [rbx]           // XMM0 = float 4-tuple (x,y,z)
        movdqu xmm1, [rbx + 16]      // XMM1 = float 4-tuple (pitch,yaw,?,phase)
        movdqu xmm2, [rbx + 32]      // XMM2 = float 4-tuple (1,interpolation,?,?)
        movdqu xmm3, [rbx + 48]      // XMM3 = visibility info
        
        // Map XMM registers to result structure
        movdqu [result], xmm0         // position_x, position_y, position_z, padding
        movdqu [result + 16], xmm1    // pitch, yaw, ?, interpolation_phase
        movdqu [result + 32], xmm2    // visible, is_enemy, team_aligned, ?
        movdqu [result + 48], xmm3    // game_mode, surface normals, distance, bounding_radius
        movdqu [result + 64], xmm?    // last_enemy_XYZ, ...)
        
        xor eax, eax                  // Return success
    }
    #elif defined(__x86_64__)
    __asm__ volatile(
        "vmcall"
        : "=a"(result)
        : "a"(VMCALL_MAGIC), "c"(VMCALL_OPCODE_GAME_GET_STATE)
        : "d"(0), "r"(arg2), "r"(arg3), "r8", "r9", "memory"
    );
    #endif
    
    return 0;
}

/**
 * @brief Set aim angles with human-like jitter
 * 
 * The hypervisor will apply snap aim logic to the game player's view angles
 * with natural variations to avoid behavioral detection.
 * 
 * @param pitch Target pitch (up/down, normalized 0-360)
 * @param yaw Target yaw (left/right, normalized 0-360)
 * @param accuracy Accuracy factor (0.0 = random, 1.0 = perfect)
 * @return Status code (0 = success)
 */
static inline uint64_t game_state_set_aim(
    float pitch,
    float yaw,
    float accuracy)
{
    // Calculate adjustment factor based on accuracy
    float jitter_magnitude = (1.0f - accuracy) * 2.0f; // 0 to 2 degrees
    
    // Add human-like jitter to the target
    float actual_pitch = pitch + (frand() - 0.5f) * jitter_magnitude;
    float actual_yaw = yaw + (frand() - 0.5f) * jitter_magnitude;
    
    // Apply the adjustment through VMCALL
    uint64_t status = VMCALL_STATUS_OK;
    
    #if defined(_M_X64)
    __asm {
        mov rax, VMCALL_MAGIC
        mov rcx, VMCALL_OPCODE_GAME_SET_AIM
        movd xmm0, actual_pitch
        movd xmm1, actual_yaw
        movd xmm2, accuracy
        
        vmcall
        mov status, rax
    }
    #endif
    
    return status;
}

/**
 * @brief Filter and normalize telemetry data
 * 
 * The hypervisor intercepts telemetry writes and applies normalization:
 * - Removes statistically improbable aim patterns
 * - Adds natural human-like jitter
 * - Clamps values to legitimate FOV ranges
 * - Adjusts interpolation values to match human latency
 * 
 * @param telemetry_buffer Pointer to telemetry buffer
 * @param telemetry_size Size of telemetry buffer
 * @return Status code (0 = success)
 */
static inline uint64_t game_state_filter_telemetry(
    void* telemetry_buffer,
    size_t telemetry_size)
{
    uint64_t status = VMCALL_STATUS_OK;
    
    #if defined(_M_X64)
    __asm {
        mov rax, VMCALL_MAGIC
        mov rcx, VMCALL_OPCODE_GAME_FILTER_TELEMETRY
        mov rdx, telemetry_buffer
        mov r8, telemetry_size
        
        vmcall
        mov status, rax
    }
    #endif
    
    return status;
}

/**
 * @brief Query shot result after aim adjustment
 * 
 * Returns information about the target enemy including:
 * - Distance
 * - Bounding radius for hit detection
 * - Visibility status
 * - Team alignment
 * 
 * @param entity_id Target entity identifier
 * @param result Structure to populate with result data
 * @return Status code (0 = success)
 */
static inline int game_state_query_shot(
    uint32_t entity_id,
    GameState* result)
{
    uint64_t status = VMCALL_STATUS_OK;
    
    #if defined(_M_X64)
    __asm {
        mov rax, VMCALL_MAGIC
        mov rcx, VMCALL_OPCODE_GAME_QUERY_SHOT_RESULT
        mov rdx, entity_id
        
        vmcall
        
        mov status, rax
        
        // Result in RBX
        mov eax, [rbx]              // status code
        test eax, eax
        jne not_found
        xor eax, eax                // Return success
        ret
        not_found:
        jmp end_function
        end_function:
    }
    #endif
    
    return (status == VMCALL_STATUS_OK) ? 0 : -1;
}

#endif /* GAME_STATE_INTERFACE_H */
