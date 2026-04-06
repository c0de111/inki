/**
 * @file historian_config.h
 * @brief CCU-Historian integration data structures and function declarations
 *
 * This header defines the data structures and function interfaces for integrating
 * with CCU-Historian servers for time-series data visualization on ePaper displays.
 *
 * The historian functionality is conditionally compiled based on the USE_CASE_HISTORIAN
 * define in config.h, allowing the same codebase to support both SeatSurfing room
 * booking displays and historian sensor data displays.
 *
 * Key Features:
 * - Time-series data structures optimized for memory-constrained embedded systems
 * - Unix timestamp conversion with automatic DST handling for MEZ/MESZ timezone
 * - JSON-RPC parsing for CCU-Historian API responses
 * - Display optimization with configurable downsampling
 * - ePaper-optimized graph rendering options
 *
 */

#ifndef HISTORIAN_CONFIG_H
#define HISTORIAN_CONFIG_H

#include "config.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifndef HISTORIAN_CONFIG_NO_LWIP
#include "lwip/ip_addr.h"
#endif

// =============================================================================
// HISTORIAN DATA STRUCTURES
// =============================================================================

/**
 * @def MAX_DATA_POINTS
 * @brief Maximum number of data points that can be stored in a TimeSeries
 *
 * This limit balances memory usage with data resolution. For typical 24-hour
 * data collection at 1-minute intervals, this allows ~13.3 hours of data.
 * Adjust based on available RAM and display requirements.
 */
#define MAX_DATA_POINTS 800

/**
 * @brief Structure for a single data point in a time series
 *
 * Represents one measurement at a specific point in time from the historian.
 * Timestamps are stored as Unix milliseconds (UTC) for precision and compatibility.
 */
typedef struct {
    uint64_t timestamp; ///< Milliseconds since Unix epoch (1970-01-01 00:00:00 UTC)
    float value;        ///< Measured sensor value (temperature, humidity, etc.)
    uint8_t state;      ///< Data quality state (0 = good, other = error/warning states)
} DataPoint;

/**
 * @brief Structure for a complete time series dataset
 *
 * Contains all data points for a single measurement parameter (e.g. temperature sensor)
 * over a specific time range. Includes metadata for efficient display rendering.
 */
typedef struct {
    int datapoint_id; ///< Unique ID of the datapoint in the CCU-Historian database
    char name[64];    ///< Human-readable name of the measurement (e.g. "Living Room Temperature")
    char unit[16];    ///< Physical unit of measurement (e.g. "°C", "%", "W", "kWh")
    DataPoint points[MAX_DATA_POINTS]; ///< Array of measurement data points
    int count;                         ///< Number of valid data points in the points array
    float min_value;                   ///< Minimum value in the current time range (for scaling)
    float max_value;                   ///< Maximum value in the current time range (for scaling)
    float last_value;                  ///< Most recent measurement value (for current display)
} TimeSeries;

// =============================================================================
// HISTORIAN CONFIGURATION STRUCTURES
// =============================================================================

#define HISTORIAN_HOST_MAX_LEN 64
#define HISTORIAN_PATH_MAX_LEN 64
#define HISTORIAN_NAME_MAX_LEN 32

/**
 * @brief Historian server connection and query configuration data
 *
 * Defines connection parameters for CCU-Historian server and default query settings.
 * Configuration is stored in flash and can be modified via web interface.
 */
typedef struct {
    uint8_t ip[4];                     ///< IP address bytes (e.g. {192, 168, 178, 42})
    uint16_t port;                     ///< TCP port number (typically 81 for CCU-Historian)
    char path[HISTORIAN_PATH_MAX_LEN]; ///< API endpoint path (typically "/query/jsonrpc.gy")
    int datapoint_id;                  ///< Default datapoint ID to query from historian database
    int hours_back;                    ///< Default time window in hours for data retrieval
    char display_name[HISTORIAN_NAME_MAX_LEN]; ///< Human-readable name for the data source
} historian_config_data_t;

/**
 * @brief Complete historian configuration structure with CRC protection
 *
 * Wraps historian configuration data with CRC32 checksum for flash storage integrity.
 * Follows the same pattern as seatsurfing_config_t and device_config_t.
 */
typedef struct {
    historian_config_data_t data;
    uint32_t crc32;
} historian_config_t;

// Legacy typedef for backward compatibility
typedef historian_config_data_t HistorianConfig;

/**
 * @brief Graph display options for ePaper rendering
 *
 * Controls the visual presentation of time series data on the ePaper display.
 * Coordinates and dimensions are in pixels relative to the display size.
 */
typedef struct {
    int x;                 ///< X coordinate of top-left corner in pixels
    int y;                 ///< Y coordinate of top-left corner in pixels
    int width;             ///< Graph width in pixels
    int height;            ///< Graph height in pixels
    bool show_grid;        ///< Enable grid lines for better readability
    bool show_labels;      ///< Enable axis labels and value annotations
    int time_window_hours; ///< Time span to display in hours (overrides config default)
} GraphOptions;

#endif // HISTORIAN_CONFIG_H
