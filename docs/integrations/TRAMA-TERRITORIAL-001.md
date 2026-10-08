# TRAMA-TERRITORIAL-001: Contrato e Especificação de Integração Territorial

## 1. Visão Geral
Especificação do adaptador consumidor do serviço TRAMA-RS. O TRAMA não mantém nem gera uma fonte territorial paralela.

- Endpoint padrão: `http://10.163.80.176:8080`
- Configuração: variável `TRAMA_RS_URL`
- API consumida: `/v1/municipios`, `/v1/coredes`, `/v1/regioes-funcionais` e `/v1/biomas`
- Política de falha: indisponibilidade explícita, sem fallback inferido

## 2. Contrato de Contexto Territorial
Endpoint local de consulta:
`GET /api/v1/territories/context/{codigo_ibge}`
ou
`GET /api/v1/territories/lookup?q={termo_ou_codigo}`

### Exemplo de Resposta Canônica:
```json
{
  "municipio": {
    "codigo_ibge": "4303301",
    "nome": "Caibaté",
    "uf": "RS"
  },
  "planejamento": {
    "corede": {
      "codigo": "missoes",
      "nome": "Missões"
    },
    "regiao_funcional": {
      "codigo": "RF7",
      "nome": "Região Funcional 7"
    }
  },
  "ecologia": {
    "bioma_predominante": "pampa",
    "biomas_ocorrentes": ["mata-atlantica", "pampa"],
    "criterio": "predominancia_por_area"
  },
  "referencias": {
    "planejamento": "TRAMA-RS PRELIMINAR_NAO_HOMOLOGADO",
    "ecologia": "TRAMA-RS; classificação ecológica conforme disponibilidade declarada"
  },
  "dataset_version": "0.1.0",
  "status": "PRELIMINAR_NAO_HOMOLOGADO"
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
- **Fonte imediata**: TRAMA-RS, incluindo versão, status e proveniência declarados por ele.
- **Cobertura atual**: catálogo preliminar; somente dois exemplos municipais possuem bioma preenchido e 495 permanecem pendentes.
- **Armazenamento**: o banco TRAMA guarda dados operacionais das unidades, não uma verdade territorial gerada localmente.
- **Integridade**: campos desconhecidos permanecem `null`; filtros por bioma incompleto retornam conflito explícito.
- **Offline**: um snapshot validado ainda não foi implementado. Se o TRAMA-RS estiver indisponível, novas consultas territoriais falham expressamente.
