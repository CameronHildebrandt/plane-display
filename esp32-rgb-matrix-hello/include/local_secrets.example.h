// Optional: copy this file to include/local_secrets.h and choose a strong
// password before using OTA on a shared network. The real file is gitignored.
#pragma once

#define LED_PLANE_OTA_PASSWORD "replace-with-a-long-random-password"

// Use either the local development backend or the public production Worker.
// Keep the real production key out of Git.
#define LED_PLANE_BACKEND_URL "http://YOUR_MAC_LAN_IP:8787/flightdata"
#define LED_PLANE_DEVICE_KEY "your-device-key"

// Production example:
// #define LED_PLANE_BACKEND_URL "https://winglet.winglet.workers.dev/flightdata"
