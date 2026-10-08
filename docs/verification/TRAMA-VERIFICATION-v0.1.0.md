# Verificação TRAMA v0.1.0

Data: 2026-10-08. Ambiente: Fedora, GCC 16.2.1 (`-std=gnu++26`), CMake 4.3, SQLite 3.51.2, OpenSSL 3.5.9, Node 24.19 e npm 11.17.

## Evidências executadas

- Build backend e frontend: sucesso; Vite produziu assets locais, sem CDN.
- CTest: 1/1 passou no ambiente indicado. Antes da publicação, a suíte foi alterada para não conter resultados operacionais.
- Runtime HTTP real: health, login, sessão/CSRF, criação de atividade, observação e encaminhamento, relatório HTML e CSV.
- Reinício real do servidor: a atividade criada continuou disponível (`1` ocorrência).
- `verify`: integrity `ok`, zero violações FK, WAL e quatro migrações.
- Backup real: 192 KiB via `sqlite3_backup`.
- `npm audit --omit=dev`: zero vulnerabilidades de runtime (o frontend não possui dependências runtime).
- Pacote local: `build/TRAMA-0.1.0-linux.tar.gz`.
- CRUD HTTP verificado com dados sintéticos: create/read/update/delete de projeto, unidade, atividade, observação e encaminhamento; 16 eventos de auditoria observados.
- Visão Geral restaurada com seleção de projeto, filtros de grupo/unidade, oito KPIs, gráfico com alternativa tabular, qualidade dos dados e observações por validação.

## Sanitização para publicação

Resultados, localidades, referências contratuais e totais do projeto de implantação foram removidos do conteúdo versionável. Bases SQLite, imports privados e documentos-fonte permanecem fora do Git.

## Lacunas honestas

Não foram concluídos upload/download de evidências, CRUD administrativo de usuários e memberships, PATCH de atividade/observação/unidade, leitura persistida de relatórios por ID, rate limiter durável, testes automatizados de navegador nem HTTPS embutido. A interface foi carregada em Firefox headless, mas não foi produzida captura confiável após a execução assíncrona; valide visualmente pelo procedimento do README. Esses itens impedem classificar todo o Definition of Done original como completo.
