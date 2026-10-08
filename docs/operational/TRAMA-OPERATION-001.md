# Operação

Use filesystem local. SQLite inicia com foreign keys, busy timeout, WAL e synchronous FULL. Não compartilhe o DB por filesystem de rede. `backup` usa `sqlite3_backup`; `verify` executa integrity e foreign-key checks. O servidor HTTP aceita somente loopback. Não exponha na LAN até existir terminação TLS e avaliação de permissões. Dados ficam no `--data-dir`; assets em `web/dist`.
