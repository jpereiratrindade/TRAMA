# TRAMA-TERRITORIAL-001: Contrato e Especificação de Integração Territorial

## 1. Visão Geral
Especificação do adaptador e do catálogo de referência territorial do Rio Grande do Sul no TRAMA.

## 2. Contrato de Contexto Territorial
Endpoint local de consulta:
`GET /api/v1/territories/context/{codigo_ibge}`
ou
`GET /api/v1/territories/lookup?q={termo_ou_codigo}`

### Exemplo de Resposta Canônica:
```json
{
  "municipio": {
    "codigo_ibge": "4314902",
    "nome": "Porto Alegre",
    "uf": "RS"
  },
  "planejamento": {
    "corede": {
      "codigo": "metropolitano-delta-do-jacui",
      "nome": "Metropolitano Delta do Jacuí"
    },
    "regiao_funcional": {
      "codigo": "rf-1",
      "nome": "Região Funcional 1"
    }
  },
  "ecologia": {
    "bioma_predominante": "Pampa",
    "biomas_ocorrentes": ["Pampa", "Mata Atlântica"],
    "criterio": "predominancia_por_area"
  },
  "referencias": {
    "planejamento": "SPGG/RS - Atlas Socioeconômico do RS",
    "ecologia": "IBGE - Biomas do Brasil"
  },
  "dataset_version": "rs-territorial-2026.1",
  "status": "verified"
}
```

## 3. Listagem de Catálogo e Autocomplete
`GET /api/v1/territories/catalog`
Suporta parâmetros de busca e filtros:
- `search`: busca por prefixo ou substring do nome ou código IBGE.
- `corede`: filtro por COREDE.
- `functional_region`: filtro por Região Funcional.
- `biome`: filtro por Bioma.

## 4. Estrutura de Proveniência e Fallback Offline
- **Fonte Planejamento**: 28 COREDEs e 9 Regiões Funcionais (Atlas Socioeconômico / SPGG-RS).
- **Fonte Ecologia**: IBGE Biomas 1:250.000.
- **Armazenamento**: Tabela local `territorial_catalog` em SQLite com WAL.
- **Integridade**: Atualizações preservam os registros históricos e garantem operação offline ininterrupta.
