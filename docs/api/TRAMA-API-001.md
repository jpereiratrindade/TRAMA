# API v1

Todas as rotas de dados requerem cookie de sessão. Escritas também exigem `X-CSRF-Token` retornado pelo login. Erros têm `{error:{code,message,details},request_id}`.

```http
POST /api/v1/auth/login
Content-Type: application/json

{"login":"admin","password":"..."}
```

Projetos, unidades, atividades, observações e encaminhamentos oferecem criação, listagem, detalhe, edição e exclusão lógica. Edições exigem `revision`; conflito retorna `409`. Exclusões preservam auditoria. Também existem grupos, territórios, fontes, análises overview/units/groups, exportações e auditoria administrativa. Consulte `src/trama.cpp` para o contrato executável.
