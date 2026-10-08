# TRAMA

Territórios, Registros, Atividades, Monitoramento e Análise. Aplicação local-first em C++26, SQLite WAL e TypeScript/Vite. Esta implementação entrega um núcleo operacional demonstrativo; consulte as limitações abaixo.

Licenciado sob GNU GPL v3.0. Consulte `LICENSE`.

## Início rápido

Requisitos testados: Fedora, GCC 16.2, CMake 4.3, Ninja 1.13, SQLite 3.51, OpenSSL 3.5 e Node 24/npm 11. Em Debian/Raspberry Pi OS, os pacotes equivalentes são esperados, mas não foram validados; confirme suporte a `-std=c++2c`.

```bash
./scripts/build.sh
./build/bin/trama init --data-dir ./var
./build/bin/trama setup-admin --data-dir ./var --login admin
./build/bin/trama-server --data-dir ./var --host 127.0.0.1 --port 8088
```

Abra `http://127.0.0.1:8088`. O prompt de administrador não ecoa a senha. Para testes e operação:

```bash
./scripts/test.sh
./build/bin/trama doctor --data-dir ./var
./build/bin/trama backup --data-dir ./var --output ./trama-backup.sqlite3
./build/bin/trama restore --data-dir ./var-restored --input ./trama-backup.sqlite3
./scripts/package.sh
```

## Funcionalidade atual

Login/logout, sessão server-side e CSRF; dashboard persistido com seletor de projeto, filtros de grupo/unidade, KPIs, percentuais, gráfico e tabela acessível; cadastros de projetos, unidades, atividades, observações e encaminhamentos; auditoria append-only; relatório HTML imprimível; exportações CSV/JSON; backup/restore e verificações. Integrações externas aparecem como `not_integrated`.

Dados operacionais e resultados de projetos não fazem parte do repositório público. Crie projetos pela interface/API e mantenha bases SQLite e arquivos privados fora do Git.

Limitações conhecidas: upload/download de evidências, gestão completa de usuários/papéis, PATCH de atividade/observação e suíte Playwright ainda não estão integrados. HTTPS LAN requer implementação/configuração adicional; por segurança, HTTP recusa bind não-loopback. PDF usa impressão do navegador. O PDF-fonte não está no repositório e seu hash permanece nulo. Nenhuma licença foi assumida.
