# API v1

Todas as rotas de dados requerem cookie de sessão. Escritas também exigem `X-CSRF-Token` retornado pelo login. Erros têm `{error:{code,message,details},request_id}`.

```http
POST /api/v1/auth/login
Content-Type: application/json

{"login":"admin","password":"..."}
```

Projetos, unidades, atividades, observações e encaminhamentos oferecem criação, listagem, detalhe, edição e exclusão lógica. Edições exigem `revision`; conflito retorna `409`. Exclusões preservam auditoria. Também existem grupos, territórios, fontes, análises overview/units/groups, exportações e auditoria administrativa. Consulte `src/trama.cpp` para o contrato executável.

Unidades aceitam `municipality` obrigatório e `corede`, `functional_region`, `biome`, `latitude` e `longitude` opcionais. A API persiste os quatro níveis como territórios hierárquicos. Coordenadas fora de `[-90,90]`/`[-180,180]` são rejeitadas pelo banco.
