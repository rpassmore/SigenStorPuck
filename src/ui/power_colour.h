#pragma once

#include <algorithm>
#include <cstdint>


// Dynamic color transition thresholds (Absolute distance values outward from 0)
struct GradientConfig {
    int32_t exp_mid_threshold = 1500; // Default: 2 kW turns pure Amber
    int32_t exp_high_threshold   = 5000; // Default: 5 kW turns pure Red
    int32_t imp_mid_threshold  = 1500; // Default: -2kW turns blue
    int32_t imp_high_threshold  = 5000; // Default: -5kW turns blue
};

GradientConfig s_gradient_config;

// Gradient Anchor Colors
static constexpr uint32_t POWER_COLOUR_WHITE  = 0xF2F2F7;
static constexpr uint32_t POWER_COLOUR_IMP_MID  = 0xFFBF00;
static constexpr uint32_t POWER_COLOUR_IMP_HIGH    = 0xFF0000;

static constexpr uint32_t POWER_COLOUR_EXP_MID = 0x80E480; 
static constexpr uint32_t POWER_COLOUR_EXP_HIGH  = 0x00C800;

static constexpr uint32_t POWER_COLOUR_ZERO_MARKER = 0x0F0F0F;
