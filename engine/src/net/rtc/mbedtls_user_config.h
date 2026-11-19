// Appended to Mbed TLS's default configuration (MBEDTLS_USER_CONFIG_FILE, engine/src/net/net.cmake)
// for the online co-op transport. libdatachannel's DTLS transport sets the DTLS-SRTP protection
// profiles even without media (NO_MEDIA), so the extension must be compiled in. Nothing else
// changes: the default configuration has DTLS, ECDSA and SHA-256 (the certificate fingerprint).
#pragma once

#define MBEDTLS_SSL_DTLS_SRTP
