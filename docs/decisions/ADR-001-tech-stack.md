# ADR-001 — Pilha

Aceito: GCC 16/C++26, CMake/Ninja, cpp-httplib 0.28.0 vendorizado, nlohmann/json disponível no host, SQLite e OpenSSL. OpenSSL substitui libsodium ausente: PBKDF2-HMAC-SHA-256 com 310 mil iterações, RNG e SHA-256. Frontend Vite/TypeScript sem CDN.
