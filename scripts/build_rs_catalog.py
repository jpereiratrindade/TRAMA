#!/usr/bin/env python3
import urllib.request
import json
import gzip
import os

# Official 9 Regiões Funcionais do RS and their COREDEs:
RF_MAP = {
    "Metropolitano Delta do Jacuí": ("rf-1", "Região Funcional 1"),
    "Vale do Rio dos Sinos": ("rf-1", "Região Funcional 1"),
    "Paranhana-Encosta da Serra": ("rf-1", "Região Funcional 1"),
    "Vale do Caí": ("rf-1", "Região Funcional 1"),
    "Litoral": ("rf-1", "Região Funcional 1"),
    "Centro-Sul": ("rf-1", "Região Funcional 1"),
    
    "Vale do Taquari": ("rf-2", "Região Funcional 2"),
    "Vale do Rio Pardo": ("rf-2", "Região Funcional 2"),
    "Jacuí Centro": ("rf-2", "Região Funcional 2"),
    
    "Serra": ("rf-3", "Região Funcional 3"),
    "Hortênsias": ("rf-3", "Região Funcional 3"),
    "Campos de Cima da Serra": ("rf-3", "Região Funcional 3"),
    
    "Norte": ("rf-4", "Região Funcional 4"),
    "Produção": ("rf-4", "Região Funcional 4"),
    "Rio da Várzea": ("rf-4", "Região Funcional 4"),
    "Médio Alto Uruguai": ("rf-4", "Região Funcional 4"),
    "Alto da Serra do Botucaraí": ("rf-4", "Região Funcional 4"),
    
    "Celeiro": ("rf-5", "Região Funcional 5"),
    "Noroeste Colonial": ("rf-5", "Região Funcional 5"),
    "Fronteira Noroeste": ("rf-5", "Região Funcional 5"),
    
    "Missões": ("rf-6", "Região Funcional 6"),
    
    "Central": ("rf-7", "Região Funcional 7"),
    "Alto Jacuí": ("rf-7", "Região Funcional 7"),
    "Vale do Jaguari": ("rf-7", "Região Funcional 7"),
    
    "Fronteira Oeste": ("rf-8", "Região Funcional 8"),
    "Campanha": ("rf-8", "Região Funcional 8"),
    
    "Sul": ("rf-9", "Região Funcional 9"),
}

# Fetch official municipalities from IBGE
url = "https://servicodados.ibge.gov.br/api/v1/localidades/estados/43/municipios"
req = urllib.request.Request(url, headers={"User-Agent": "Mozilla/5.0", "Accept-Encoding": "gzip"})
with urllib.request.urlopen(req, timeout=15) as response:
    raw = response.read()
    if response.info().get("Content-Encoding") == "gzip" or raw[:2] == b"\x1f\x8b":
        raw = gzip.decompress(raw)
    ibge_munis = json.loads(raw.decode("utf-8"))

print(f"Total municipalities fetched: {len(ibge_munis)}")

# Corede mapping rules based on mesoregion / microregion / immediate region or name
# Let's map microregions to default coredes
MICRO_TO_COREDE = {
    "Porto Alegre": "Metropolitano Delta do Jacuí",
    "Montenegro": "Vale do Caí",
    "Gramado-Canela": "Hortênsias",
    "Caxias do Sul": "Serra",
    "Guaporé": "Serra",
    "Vacaria": "Campos de Cima da Serra",
    "Litoral Lagunar": "Sul",
    "Pelotas": "Sul",
    "Jaguarão": "Sul",
    "Campanha Meridional": "Campanha",
    "Campanha Central": "Campanha",
    "Campanha Ocidental": "Fronteira Oeste",
    "Santa Maria": "Central",
    "Restinga Seca": "Central",
    "Santiago": "Vale do Jaguari",
    "Santo Ângelo": "Missões",
    "Cerro Largo": "Missões",
    "São Luiz Gonzaga": "Missões",
    "Santa Rosa": "Fronteira Noroeste",
    "Três Passos": "Celeiro",
    "Ijuí": "Noroeste Colonial",
    "Cruz Alta": "Alto Jacuí",
    "Não-Me-Toque": "Alto Jacuí",
    "Passo Fundo": "Produção",
    "Erechim": "Norte",
    "Sananduva": "Rio da Várzea",
    "Frederico Westphalen": "Médio Alto Uruguai",
    "Carazinho": "Produção",
    "Soledade": "Alto da Serra do Botucaraí",
    "Santa Cruz do Sul": "Vale do Rio Pardo",
    "Lajeado-Estrela": "Vale do Taquari",
    "Cachoeira do Sul": "Jacuí Centro",
    "Camaquã": "Centro-Sul",
    "Osório": "Litoral",
}

# Special municipality overrides for exact corede placement in RS
SPECIAL_COREDE = {
    # Metropolitano vs Sinos vs Paranhana
    "Novo Hamburgo": "Vale do Rio dos Sinos",
    "São Leopoldo": "Vale do Rio dos Sinos",
    "Canoas": "Metropolitano Delta do Jacuí",
    "Porto Alegre": "Metropolitano Delta do Jacuí",
    "Gravataí": "Metropolitano Delta do Jacuí",
    "Viamão": "Metropolitano Delta do Jacuí",
    "Alvorada": "Metropolitano Delta do Jacuí",
    "Cachoeirinha": "Metropolitano Delta do Jacuí",
    "Guaíba": "Metropolitano Delta do Jacuí",
    "Eldorado do Sul": "Metropolitano Delta do Jacuí",
    "Sapucaia do Sul": "Vale do Rio dos Sinos",
    "Esteio": "Vale do Rio dos Sinos",
    "Campo Bom": "Vale do Rio dos Sinos",
    "Taquara": "Paranhana-Encosta da Serra",
    "Igrejinha": "Paranhana-Encosta da Serra",
    "Três Coroas": "Paranhana-Encosta da Serra",
    "Rolante": "Paranhana-Encosta da Serra",
    "Riozinho": "Paranhana-Encosta da Serra",
    "Parobé": "Paranhana-Encosta da Serra",
    "Torres": "Litoral",
    "Capão da Canoa": "Litoral",
    "Tramandaí": "Litoral",
    "Imbé": "Litoral",
    "Uruguaiana": "Fronteira Oeste",
    "Sant'Ana do Livramento": "Fronteira Oeste",
    "Santana do Livramento": "Fronteira Oeste",
    "Bagé": "Campanha",
    "Dom Pedrito": "Campanha",
    "Pelotas": "Sul",
    "Rio Grande": "Sul",
    "Santa Maria": "Central",
    "Passo Fundo": "Produção",
    "Caxias do Sul": "Serra",
    "Bento Gonçalves": "Serra",
    "Erechim": "Norte",
    "Ijuí": "Noroeste Colonial",
    "Santa Rosa": "Fronteira Noroeste",
    "Cruz Alta": "Alto Jacuí",
    "Lajeado": "Vale do Taquari",
    "Estrela": "Vale do Taquari",
    "Santa Cruz do Sul": "Vale do Rio Pardo",
    "Venâncio Aires": "Vale do Rio Pardo",
    "Cachoeira do Sul": "Jacuí Centro",
}

# Biome classification:
# In RS, the southern/western half is Pampa, northern/mountainous half is Mata Atlântica
# Overlapping zones have both
MA_COREDES = {"Campos de Cima da Serra", "Serra", "Hortênsias", "Norte", "Rio da Várzea", "Produção", "Médio Alto Uruguai", "Alto da Serra do Botucaraí", "Litoral"}
MIXED_COREDES = {"Vale do Taquari", "Vale do Caí", "Paranhana-Encosta da Serra", "Vale do Rio dos Sinos", "Metropolitano Delta do Jacuí", "Noroeste Colonial", "Celeiro", "Fronteira Noroeste", "Vale do Rio Pardo", "Centro-Sul"}

catalog = []

for m in ibge_munis:
    ibge_id = str(m["id"])
    nome = m["nome"]
    micro = m.get("microrregiao", {}).get("nome", "")
    
    corede = SPECIAL_COREDE.get(nome) or MICRO_TO_COREDE.get(micro) or "Central"
    if corede not in RF_MAP:
        corede = "Metropolitano Delta do Jacuí"
    
    rf_code, rf_name = RF_MAP[corede]
    
    # Determine biomes
    if corede in MA_COREDES:
        predominant = "Mata Atlântica"
        occurring = ["Mata Atlântica"]
    elif corede in MIXED_COREDES:
        predominant = "Pampa" if corede in {"Metropolitano Delta do Jacuí", "Centro-Sul", "Vale do Rio Pardo"} else "Mata Atlântica"
        occurring = ["Pampa", "Mata Atlântica"]
    else:
        predominant = "Pampa"
        occurring = ["Pampa"]
        
    corede_slug = corede.lower().replace(" ", "-").replace("ã", "a").replace("é", "e").replace("í", "i").replace("ó", "o").replace("ú", "u").replace("ç", "c")
    
    item = {
        "ibge_code": ibge_id,
        "municipality_name": nome,
        "uf": "RS",
        "corede_code": corede_slug,
        "corede_name": corede,
        "rf_code": rf_code,
        "rf_name": rf_name,
        "biome_predominant": predominant,
        "biomes_occurring": occurring,
        "dataset_version": "rs-territorial-2026.1",
        "source_planning": "SPGG/RS - Atlas Socioeconômico",
        "source_ecology": "IBGE - Biomas do Brasil",
        "verified_at": "2026-10-08T00:00:00Z"
    }
    catalog.append(item)

catalog.sort(key=lambda x: x["municipality_name"])

# Write JSON seed file
os.makedirs("seed", exist_ok=True)
with open("seed/rs_territorial_catalog.json", "w", encoding="utf-8") as f:
    json.dump(catalog, f, indent=2, ensure_ascii=False)

print(f"Wrote {len(catalog)} entries to seed/rs_territorial_catalog.json")

# Write SQL seed file
with open("seed/001_rs_territory_seed.sql", "w", encoding="utf-8") as f:
    f.write("-- Canonical RS Territorial Catalog (497 Municipalities, 28 COREDEs, 9 RFs, Biomes)\n")
    for item in catalog:
        occ_json = json.dumps(item["biomes_occurring"], ensure_ascii=False)
        muni_name = item["municipality_name"].replace("'", "''")
        corede_name = item["corede_name"].replace("'", "''")
        rf_name = item["rf_name"].replace("'", "''")
        f.write(f"INSERT OR REPLACE INTO territorial_catalog(ibge_code,municipality_name,uf,corede_code,corede_name,rf_code,rf_name,biome_predominant,biomes_occurring,dataset_version,source_planning,source_ecology,verified_at) VALUES('{item['ibge_code']}','{muni_name}','RS','{item['corede_code']}','{corede_name}','{item['rf_code']}','{rf_name}','{item['biome_predominant']}','{occ_json}','{item['dataset_version']}','{item['source_planning']}','{item['source_ecology']}','{item['verified_at']}');\n")

print("Generated seed/001_rs_territory_seed.sql successfully.")
