# TRAMA

Territórios, Registros, Atividades, Monitoramento e Análise. Aplicação local-first em C++26, SQLite WAL e TypeScript/Vite. Esta implementação entrega um núcleo operacional demonstrativo; consulte as limitações abaixo.

Licenciado sob GNU GPL v3.0. Consulte `LICENSE`.

## Início rápido

Requisitos testados: Fedora, GCC 16.2, CMake 4.3, Ninja 1.13, SQLite 3.51, OpenSSL 3.5 e Node 24/npm 11. Em Debian/Raspberry Pi OS, os pacotes equivalentes são esperados, mas não foram validados; confirme suporte a `-std=c++2c`.

```bash
./scripts/build.sh
./build/bin/trama init --data-dir ./var
./build/bin/trama setup-admin --data-dir ./var --login admin
TRAMA_RS_URL=http://10.163.80.176:8080 \
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

Login/logout, sessão server-side e CSRF; dashboard persistido com seletor de projeto, filtros de grupo/unidade, KPIs, percentuais, gráfico e tabela acessível; mapa Leaflet local das unidades com município, COREDE, região funcional, bioma e coordenadas opcionais; cadastros de projetos, unidades, atividades, observações e encaminhamentos; auditoria append-only; relatório HTML imprimível; exportações CSV/JSON; backup/restore e verificações.

O catálogo territorial é consumido exclusivamente do **TRAMA-RS**. O endpoint padrão é `http://10.163.80.176:8080` e pode ser substituído com `TRAMA_RS_URL`. O TRAMA preserva o status de validação, a versão e campos `null` recebidos; não usa fallback inferido. No catálogo preliminar atual, filtros de bioma permanecem indisponíveis e retornam conflito explícito.

Ao iniciar, o servidor reconcilia as unidades já cadastradas por correspondência nominal exata com o TRAMA-RS. COREDE e Região Funcional são atualizados; código IBGE e biomas continuam nulos quando o serviço de referência ainda não os confirmou. Nomes não conciliados são relatados no log e não são corrigidos automaticamente.

Dados operacionais e resultados de projetos não fazem parte do repositório público. Crie projetos pela interface/API e mantenha bases SQLite e arquivos privados fora do Git.

Limitações conhecidas: o TRAMA-RS precisa estar acessível durante pesquisas e novos cadastros territoriais; cache offline validado ainda não foi implementado. Upload/download de evidências, gestão completa de usuários/papéis e suíte Playwright ainda não estão integrados. HTTPS LAN requer implementação/configuração adicional; por segurança, o servidor TRAMA recusa bind não-loopback. PDF usa impressão do navegador.
