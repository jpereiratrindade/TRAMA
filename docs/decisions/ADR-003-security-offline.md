# ADR-003 — Segurança offline

Sessões têm segredo aleatório armazenado somente por hash, cookie HttpOnly/SameSite Strict e CSRF. Em loopback o cookie não usa Secure; qualquer bind externo sem TLS é recusado. Auditoria encadeada detecta alterações acidentais, mas não é prova contra administrador do host.
