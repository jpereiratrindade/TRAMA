# Modelo de dados

O DDL canônico está em `migrations/001_initial.sql`. Projetos contêm grupos, unidades e atividades. Observações distinguem tipo, estado epistêmico e validação. Agregados apontam para fonte documental. Revisões suportam concorrência otimista; eventos de auditoria são append-only e encadeados por hash. Datas desconhecidas permanecem `NULL`; jovens são subconjunto, nunca parcela aditiva.
