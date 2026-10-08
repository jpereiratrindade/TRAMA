# ADR-004 — Fronteira de integração

Conectores são portas explícitas e devem informar indisponibilidade sem fabricar fallback. O TRAMA-RS é uma dependência de runtime para pesquisa e preenchimento de contexto territorial. O endpoint padrão é `http://10.163.80.176:8080`, configurável por `TRAMA_RS_URL`.

Dados territoriais inferidos localmente são proibidos. Valores desconhecidos permanecem `null`, e o estado `PRELIMINAR_NAO_HOMOLOGADO` recebido do serviço é preservado.
