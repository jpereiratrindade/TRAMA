# ADR-003 — Segurança offline

Sessões têm segredo aleatório armazenado somente por hash, cookie HttpOnly/SameSite Strict e CSRF. Em loopback e rede local, o servidor escuta em 0.0.0.0. Auditoria encadeada detecta alterações acidentais, mas não é prova contra administrador do host.

