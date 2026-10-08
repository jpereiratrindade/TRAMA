# API v1

Todas as rotas de dados requerem cookie de sessão. Escritas também exigem `X-CSRF-Token` retornado pelo login. Erros têm `{error:{code,message,details},request_id}`.

```http
POST /api/v1/auth/login
Content-Type: application/json

{"login":"admin","password":"..."}
```

Projetos, unidades, atividades, observações e encaminhamentos oferecem criação, listagem, detalhe, edição e exclusão lógica. Edições exigem `revision`; conflito retorna `409`. Exclusões preservam auditoria. Também existem grupos, territórios (`/api/v1/territories/catalog`, `/api/v1/territories/context/{codigo_ibge}`), fontes, análises overview/units/groups (com filtros por COREDE, RF e Bioma), exportações e auditoria administrativa. Consulte `src/trama.cpp` para o contrato executável.

Unidades aceitam `ibge_code` e `municipality` obrigatório/autocompletável, além de `corede`, `functional_region`, `biome_predominant`, `biomes_occurring`, `latitude` e `longitude` opcionais. A API persiste o eixo de planejamento (`Município → COREDE → Região Funcional`) e os atributos ecológicos associados. Coordenadas fora de `[-90,90]`/`[-180,180]` são rejeitadas pelo banco.
