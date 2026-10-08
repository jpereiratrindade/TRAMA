# Arquitetura

O executável CLI e o servidor usam o mesmo núcleo C++26. SQLite é acessado por statements preparados e conexões locais; migrações e escritas são transacionais. HTTP traduz JSON, autentica e aplica CSRF. A SPA TypeScript consome somente `/api/v1`; seus gráficos CSS têm tabela alternativa. Anexos ficam reservados fora do webroot. A versão atual concentra o núcleo em `src/trama.cpp`; decomposição em módulos é a próxima refatoração, sem alteração de contratos.
