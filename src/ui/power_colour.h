#pragma once

#include <algorithm>
#include <cstdint>

// -----------------------------------------------------------------------------
// Power colour thresholds
// -----------------------------------------------------------------------------
//
// Positive power = grid import
// Negative power = grid export
//
// All values are watts.
//
// WHITE_THRESHOLD:
//   Power at or below this absolute value remains pure white.
//
// WARN_LEVEL:
//   Transition reaches amber/yellow.
//
// ALERT_LEVEL:
//   Transition reaches red/green.
//
// Values above ALERT_LEVEL remain at the final colour.
//

static constexpr float POWER_IMPORT_WHITE_THRESHOLD = 250.0f;
static constexpr float POWER_IMPORT_WARN_LEVEL       = 2500.0f;
static constexpr float POWER_IMPORT_ALERT_LEVEL      = 5000.0f;

static constexpr float POWER_EXPORT_WHITE_THRESHOLD = 250.0f;
static constexpr float POWER_EXPORT_WARN_LEVEL       = 2500.0f;
static constexpr float POWER_EXPORT_ALERT_LEVEL      = 5000.0f;


// -----------------------------------------------------------------------------
// Colours
// -----------------------------------------------------------------------------
//
// RGB888 format: 0xRRGGBB
//

static constexpr uint32_t POWER_COLOUR_WHITE  = 0xFFFFFF;
static constexpr uint32_t POWER_COLOUR_AMBER  = 0xFFBF00;
static constexpr uint32_t POWER_COLOUR_RED    = 0xFF0000;

static constexpr uint32_t POWER_COLOUR_YELLOW = 0x0080FF; //0xFFFF00;
static constexpr uint32_t POWER_COLOUR_GREEN  = 0x00C800;


// -----------------------------------------------------------------------------
// RGB interpolation
// -----------------------------------------------------------------------------

static inline uint32_t power_colour_lerp(
    uint32_t from,
    uint32_t to,
    float t)
{
    t = std::clamp(t, 0.0f, 1.0f);

    const uint8_t from_r = (from >> 16) & 0xFF;
    const uint8_t from_g = (from >> 8)  & 0xFF;
    const uint8_t from_b =  from        & 0xFF;

    const uint8_t to_r = (to >> 16) & 0xFF;
    const uint8_t to_g = (to >> 8)  & 0xFF;
    const uint8_t to_b =  to        & 0xFF;

    const uint8_t r = static_cast<uint8_t>(
        from_r + (to_r - from_r) * t + 0.5f);

    const uint8_t g = static_cast<uint8_t>(
        from_g + (to_g - from_g) * t + 0.5f);

    const uint8_t b = static_cast<uint8_t>(
        from_b + (to_b - from_b) * t + 0.5f);

    return (static_cast<uint32_t>(r) << 16) |
           (static_cast<uint32_t>(g) << 8)  |
           static_cast<uint32_t>(b);
}


// -----------------------------------------------------------------------------
// Convert power to RGB888 colour
// -----------------------------------------------------------------------------
//
// Returns:
//   0xRRGGBB
//
// Positive power:
//   white -> amber -> red
//
// Negative power:
//   white -> yellow -> green
//
// Zero:
//   white
//

static inline uint32_t power_to_colour(float power_w)
{
    // -------------------------------------------------------------------------
    // Positive power = IMPORT
    // -------------------------------------------------------------------------

    if (power_w > 0.0f)
    {
        // White dead-band
        if (power_w <= POWER_IMPORT_WHITE_THRESHOLD)
            return POWER_COLOUR_WHITE;

        // White -> Amber
        if (power_w < POWER_IMPORT_WARN_LEVEL)
        {
            const float t =
                (power_w - POWER_IMPORT_WHITE_THRESHOLD) /
                (POWER_IMPORT_WARN_LEVEL - POWER_IMPORT_WHITE_THRESHOLD);

            return power_colour_lerp(
                POWER_COLOUR_WHITE,
                POWER_COLOUR_AMBER,
                t);
        }

        // Amber -> Red
        if (power_w < POWER_IMPORT_ALERT_LEVEL)
        {
            const float t =
                (power_w - POWER_IMPORT_WARN_LEVEL) /
                (POWER_IMPORT_ALERT_LEVEL - POWER_IMPORT_WARN_LEVEL);

            return power_colour_lerp(
                POWER_COLOUR_AMBER,
                POWER_COLOUR_RED,
                t);
        }

        // At or above alert level
        return POWER_COLOUR_RED;
    }


    // -------------------------------------------------------------------------
    // Negative power = EXPORT
    // -------------------------------------------------------------------------

    if (power_w < 0.0f)
    {
        const float export_power = -power_w;

        // White dead-band
        if (export_power <= POWER_EXPORT_WHITE_THRESHOLD)
            return POWER_COLOUR_WHITE;

        // White -> Yellow
        if (export_power < POWER_EXPORT_WARN_LEVEL)
        {
            const float t =
                (export_power - POWER_EXPORT_WHITE_THRESHOLD) /
                (POWER_EXPORT_WARN_LEVEL - POWER_EXPORT_WHITE_THRESHOLD);

            return power_colour_lerp(
                POWER_COLOUR_WHITE,
                POWER_COLOUR_YELLOW,
                t);
        }

        // Yellow -> Green
        if (export_power < POWER_EXPORT_ALERT_LEVEL)
        {
            const float t =
                (export_power - POWER_EXPORT_WARN_LEVEL) /
                (POWER_EXPORT_ALERT_LEVEL - POWER_EXPORT_WARN_LEVEL);

            return power_colour_lerp(
                POWER_COLOUR_YELLOW,
                POWER_COLOUR_GREEN,
                t);
        }

        // At or above alert level
        return POWER_COLOUR_GREEN;
    }


    // Exactly zero
    return POWER_COLOUR_WHITE;
}