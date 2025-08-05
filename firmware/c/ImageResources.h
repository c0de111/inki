#ifndef IMAGERESOURCES_H
#define IMAGERESOURCES_H

/**
 * @file    ImageResources.h
 * @brief   Declarations of embedded image resources for the eSign project.
 *
 * This header file provides access to pre-compiled image resources used
 * throughout the eSign project, such as background images, logos, and QR codes.
 *
 * ## Purpose
 * - Provides references to image data arrays embedded in the firmware.
 * - Enables consistent usage of images across various parts of the application.
 *
 * ## Notes
 * - Images are optimized for ePaper displays supported by the project.
 * - Each image is stored as a byte array and should match the display's
 *   resolution and color format.
 */
// Structure for a rectangular QR code placement
typedef struct {
    int x;
    int y;
    int width;
    int height;
} QRCodeConfig;

// Structure for image data used on display
typedef struct {
    const unsigned char* data;
    int width;
    int height;
} SubImage;

extern const unsigned char gImage_battery_level_1[];
extern const unsigned char gImage_battery_level_2[];
extern const unsigned char gImage_battery_level_3[];
extern const unsigned char gImage_battery_level_4[];
extern const unsigned char gImage_battery_level_5[];
extern const unsigned char gImage_battery_level_6[];
extern const unsigned char gImage_battery_level_7[];
extern const unsigned char gImage_battery_level_8[];
extern const unsigned char gImage_battery_level_9[];
extern const unsigned char gImage_battery_level_10[];

extern const unsigned char gImage_qr_Seminarraum[];

extern const unsigned char gImage_github_link[];
extern const unsigned char gImage_eSign_128x128_white_background[];
extern const unsigned char gImage_eSign_128x128_white_background3[];
extern const unsigned char gImage_eSign_100x100_3[];

// Image assets declared externally
extern const SubImage battery_levels_64x97[];
extern const SubImage eSign_128x128_white_background3;
extern const SubImage eSign_100x100_3;
extern const SubImage qr_Seminarraum;
extern const SubImage qr_github_link;

#endif
/* FILE END */


