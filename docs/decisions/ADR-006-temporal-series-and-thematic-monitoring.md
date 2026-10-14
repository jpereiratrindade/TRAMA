# ADR-006 — Monitoramento Longitudinal como Série Temporal e Agregação Temática

## Status
Aprovado (2026-10-14)

## Contexto
O projeto Recuperação de Biomas I e a metodologia das Unidades de Aprendizagem Coletiva (UACs) foram concebidos não como eventos pontuais, mas como processos permanentes de aprendizagem continuada, experimentação e acompanhamento técnico ao longo de múltiplos ciclos e rodadas de oficinas.

Anteriormente, o sistema agregava dados sob a premissa de um *snapshot* estático, sem expor os marcadores temporais das atividades nas views consolidadas (`v_attendance`), nem prover agrupamento longitudinal ou contagem de frequência de tópicos qualitativos (desafios recorrentes e potencialidades territoriais documentadas).

## Decisão
1. **Enriquecimento Temporal da Visão de Agregação**:
   - A migração `009_temporal_series.sql` atualiza `v_attendance` para expor `occurred_at`, `activity_kind` e `activity_title`.
   - Criação de índices de cobertura para `activities(occurred_at)`, `activities(kind)` e `observations(topic)`.
2. **Desacoplamento de Contagem de Unidades e Participações**:
   - A contagem de UACs atendidas passa a usar `count(DISTINCT unit_id)` para evitar superestimação quando uma mesma unidade tiver múltiplos registros de oficinas em datas distintas.
3. **Agregação de Série Temporal na API e Relatórios**:
   - O endpoint `/api/v1/analytics/overview` e a emissão de relatórios técnicos passam a fornecer o nó `timeline`, agrupando participações, proporção de gênero e juventude por ciclo/período.
   - Fornecimento de `topics_frequency` para contagem e classificação de desafios estruturais (`desafio:*`) e potencialidades territoriais (`potencialidade:*`).
4. **Interface Visual e Relatório Oficial**:
   - Dashboard e relatórios em HTML imprimível agora renderizam a tabela de evolução temporal e os gráficos de barras temáticas de desafios e potencialidades alinhados aos relatórios técnicos consolidados.

## Consequências
- O TRAMA opera nativamente como sistema de monitoramento longitudinal, permitindo acompanhar a eficácia das intervenções (ex.: manejo de capim-annoni, carrapato, retenção de jovens) rodada a rodada.
- Permanece 100% offline-first, sem dependência de serviços externos, com tempo de execução de testes de integração inferior a 100ms.
