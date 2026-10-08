# ADR-005 — Integração Territorial e Separação de Eixos Administrativo e Ecológico

## Status
Aprovado (2026-10-08)

## Contexto
Anteriormente, o TRAMA tratava biomas como a entidade pai da Região Funcional em uma única cadeia linear de `parent_id` (`Bioma → Região Funcional → COREDE → Município`).
Conceitualmente, biomas não são divisões administrativas superiores às Regiões Funcionais. O estado do Rio Grande do Sul possui 9 Regiões Funcionais agregando os 28 COREDEs e seus 497 municípios, enquanto os biomas (Pampa e Mata Atlântica) representam uma classificação ecológica e biogeográfica com municípios contendo áreas de transição e sobreposição de múltiplos biomas.

Além disso, a identificação dos municípios dependia exclusivamente do nome em formato texto, suscetível a variações ortográficas e descontinuidades.

## Decisão
1. **Separação de Eixos**:
   - **Eixo Administrativo/Planejamento**: Região Funcional (RF 1 a 9) → COREDE (28 conselhos) → Município (497).
   - **Eixo Ecológico/Biogeográfico**: `bioma_predominante` (referência estatística por área) e `biomas_ocorrentes` (ocorrência espacial efetiva).
2. **Identidade Estável Municipal**:
   - Adoção do `codigo_ibge` (7 dígitos) como chave canônica municipal primária.
3. **Padrão Adaptador e Operação Offline-First**:
   - O backend C++26 atua como consumidor e validador do Serviço Territorial RS, mantendo um catálogo local versionado (`territorial_catalog` em SQLite WAL) com proveniência (`dataset_version`, `source_planning`, `source_ecology`, `verified_at`).
   - O frontend Vite/TypeScript consome unicamente a API local do TRAMA, com autocompletar municipal e preenchimento automático das dimensões territoriais.
   - Resiliência total: em caso de indisponibilidade de serviços externos, o sistema opera de forma autônoma e consistente através da base local verificada.

## Consequências
- Os registros históricos e análises ganham rastreabilidade e integridade referencial.
- O TRAMA atua como cliente padrão para futuros consumidores do ecossistema territorial (ELO, Morfocampo, SisTer).
