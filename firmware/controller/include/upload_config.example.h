// Copy this file to upload_config.h (same folder) and fill it in.
// upload_config.h is git-ignored: never commit it. It holds a Wi-Fi password and a
// JNS SPLIT timing credential, which must be treated like a password.
#pragma once

// The network ST-1 joins at the end of a session (club Wi-Fi or a phone hotspot).
#define JNS_WIFI_SSID "your-network"
#define JNS_WIFI_PASSWORD "your-password"

// JNS SPLIT's upload endpoint. Use the deployed site, or your PC's dev server on the
// same network, e.g. "http://192.168.1.20:5173/api/timing/efforts" (start it with
// `npm run dev -- --host` so other devices can reach it).
#define JNS_UPLOAD_URL "https://your-jns-split-site/api/timing/efforts"

// A Test credential from Timing controllers in JNS SPLIT (lasts 7 days).
#define JNS_UPLOAD_CREDENTIAL "controller-id.0000000000000000000000000000000000000000000000000000000000000000"

// The planned JNS SPLIT session these efforts belong to (from its address bar).
#define JNS_UPLOAD_SESSION "00000000-0000-4000-8000-000000000000"

// Until the lane reader exists, every effort is uploaded as this rider tag.
// Use a fictional test rider's tag, listed on the session.
#define JNS_UPLOAD_TAG "04A1B2C3D4E5F6"

// For an https:// address only: the site's root certificate, in PEM form. ST-1 refuses
// https without it rather than send the credential unchecked. See the README.
// static const char kJnsRootCa[]=R"PEM(
// -----BEGIN CERTIFICATE-----
// ...
// -----END CERTIFICATE-----
// )PEM";
// #define JNS_UPLOAD_ROOT_CA kJnsRootCa
