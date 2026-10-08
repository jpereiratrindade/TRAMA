# Modelo de dados

O DDL canônico está nas migrações versionadas. Projetos contêm grupos, unidades e atividades. A localização de uma unidade forma a hierarquia `município → COREDE → região funcional → bioma`; latitude e longitude são opcionais, validadas por faixa e nunca inferidas. Observações distinguem tipo, estado epistêmico e validação. Agregados apontam para fonte documental. Revisões suportam concorrência otimista; eventos de auditoria são append-only e encadeados por hash. Datas desconhecidas permanecem `NULL`; categorias sobrepostas nunca são somadas silenciosamente.
